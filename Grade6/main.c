#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <stdlib.h>

#define SINGLE_ROOMS_COUNT 10
#define DOUBLE_ROOMS_COUNT 15

// Статус комнаты.
typedef enum {
    freed,
    busied_by_man,
    busied_by_woman,
    full
} room_status;

// Структура с данными о состоянии комнат.
typedef struct {
    room_status single_rooms[SINGLE_ROOMS_COUNT];
    room_status double_rooms[DOUBLE_ROOMS_COUNT];
    // Данные клиента запускаемого в данный момент процесса.
    int current_client_id;
    int current_client_gender;
    int current_client_rent_time;
} rooms_data_t;

// Ожидает разблокировки семафора.
void wait_semaphore(int sem_id) {
    struct sembuf sem_op;
    sem_op.sem_num = 0;
    sem_op.sem_op = 0;
    sem_op.sem_flg = 0;
    semop(sem_id, &sem_op, 1);
    semctl(sem_id, 0, SETVAL, 1);
}

// Обрабатывает логику клиента отеля.
void handle_client_process(rooms_data_t *data, int rooms_semaphore_id, int current_client_wait_sem_id) {
    int client_id = data->current_client_id;
    int client_gender = data->current_client_gender;
    int client_rent_time = data->current_client_rent_time;
    printf("[CLIENT-%d] started.\n", client_id);
    semctl(current_client_wait_sem_id, 0, SETVAL, 0);

    // Ищем свободную комнату, перед этим блокируем семафор.
    wait_semaphore(rooms_semaphore_id);

    // Сначала проверим комнаты на одно спальное место.
    int used_single_idx = -1;
    int used_double_idx = -1;

    for (int i = 0; i < SINGLE_ROOMS_COUNT; ++i) {
        if (data->single_rooms[i] == freed) {
            printf("[CLIENT-%d] rent single room: idx = %li.\n", client_id, i);
            data->single_rooms[i] = full;
            used_single_idx = i;
            break;
        }
    }

    // Если же не нашли на одно место, то поищем на два места, но при условии, либо комната пуста,
    // либо в ней живет человек того же пола.
    if (used_single_idx == -1) {
        for (int i = 0; i < DOUBLE_ROOMS_COUNT; ++i) {
            if (data->double_rooms[i] == freed) {
                data->double_rooms[i] = client_gender == 0 ? busied_by_man : busied_by_woman;
                printf("[CLIENT-%d] rent double room: idx = %d, gender = %d.\n", client_id, i, client_gender);
                used_double_idx = i;
                break;
            } else if ((data->double_rooms[i] == busied_by_man && client_gender == 0) ||
                       (data->double_rooms[i] == busied_by_woman && client_gender == 1)) {
                printf("[CLIENT-%d] rent double room: idx = %d, gender = %d, with_gender = %d.\n", client_id, i,
                       client_gender, client_gender);

                data->double_rooms[i] = full;
                used_double_idx = i;
                break;
            }
        }
    }

    // Разблокируем семафор, чтобы другой процесс забронировал комнату.
    semctl(rooms_semaphore_id, 0, SETVAL, 0);

    // Если нашли комнату, то значит, что статус мы изменили.
    if (used_double_idx == -1 && used_single_idx == -1) {
        printf("[CLIENT-%d] out of service!\n", client_id);
        return;
    }

    // Бронируем комнату и ждем ...
    printf("[CLIENT-%d] waiting %ds.\n", client_id, client_rent_time);
    sleep(client_rent_time);

    // Теперь освободим комнату.
    wait_semaphore(rooms_semaphore_id);

    if (used_double_idx >= 0) {
        if (data->double_rooms[used_double_idx] == full) {
            data->double_rooms[used_double_idx] = client_gender == 0 ? busied_by_man : busied_by_woman;
        } else if (data->double_rooms[used_double_idx] == busied_by_man ||
                   data->double_rooms[used_double_idx] == busied_by_woman) {
            data->double_rooms[used_double_idx] = freed;
        }
    } else if (used_single_idx >= 0) {
        data->single_rooms[used_single_idx] = freed;
    }

    printf("[CLIENT-%d] end of rent!\n", client_id);
    semctl(rooms_semaphore_id, 0, SETVAL, 0);
}

// Общие переменные для работы программы.
int rooms_fd;
bool is_child_process;
FILE *client_file;
rooms_data_t *rooms_data;
int current_client_wait_sem_id;
int rooms_semaphore_id;

// Освобождает занятые процессом ресурсы.
void free_resources() {
    // Ждем завершения всех дочерних процессов.
    while (wait(NULL) > 0) {
    }

    // Освобождаем ресурсы.
    fclose(client_file);
    semctl(rooms_semaphore_id, 0, IPC_RMID, 0);
    semctl(current_client_wait_sem_id, 0, IPC_RMID, 0);
    shmdt(rooms_data);
    exit(1);
}

// Обрабатывает сигнал завершения программы.
void handle_sigterm(__attribute__((unused)) int signal) {
    free_resources();
}

int main() {
    is_child_process = false;

    // Инициализируем доступ к shared memory для работы с состояниями комнат.
    rooms_fd = shmget(IPC_PRIVATE, sizeof(rooms_data_t), IPC_CREAT | 0666);
    rooms_data = shmat(rooms_fd, NULL, 0);

    // Инициализируем состояние комнат.
    memset(rooms_data->single_rooms, freed, sizeof(room_status) * SINGLE_ROOMS_COUNT);
    memset(rooms_data->double_rooms, freed, sizeof(room_status) * DOUBLE_ROOMS_COUNT);

    // Открываем семафоры для первичной инициализации.
    rooms_semaphore_id = semget(IPC_PRIVATE, 1, IPC_CREAT | 0666);;
    current_client_wait_sem_id = semget(IPC_PRIVATE, 1, IPC_CREAT | 0666);
    client_file = fopen("clients.txt", "r");

    // Теперь зарегистрируем обработчик SIGTERM, чтобы корректно закрыть семафоры и shared memory.
    __sighandler_t previous = signal(SIGTERM, handle_sigterm);

    // Во время чтения пишем данные запускаемого клиента в shared memory.
    // Далее ждем, пока запущенный процесс заберет эту порцию данных.
    while (fscanf(client_file, "%d %d %d",
                  &rooms_data->current_client_id,
                  &rooms_data->current_client_gender,
                  &rooms_data->current_client_rent_time) > 0) {
        // Блокируем мутекс для того, чтобы в следующий раз мы ждали запуска процесса и получения им данных.
        wait_semaphore(current_client_wait_sem_id);

        // Если же этот код уже исполняется в дочернем процессе
        if (fork() == 0) {
            is_child_process = true;
            signal(SIGTERM, previous);
            semctl(current_client_wait_sem_id, 0, SETVAL, 0);
            handle_client_process(rooms_data, rooms_semaphore_id, current_client_wait_sem_id);
            break;
        }

        wait_semaphore(current_client_wait_sem_id);
    }

    free_resources();
    return 0;
}

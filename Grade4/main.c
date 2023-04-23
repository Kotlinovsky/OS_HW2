#include <fcntl.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#define SINGLE_ROOMS_COUNT 10
#define DOUBLE_ROOMS_COUNT 15
#define ROOMS_MEM_NAME "/rooms_mem"
#define ROOMS_SEM_NAME "/rooms_sem223431"
#define CURRENT_CLIENT_WAIT_SEM_NAME "/current_client_wait_sem_hw2223323"

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

// Обрабатывает логику клиента отеля.
void handle_client_process(rooms_data_t *data, sem_t *rooms_semaphore, sem_t *current_client_wait_sem) {
    int client_id = data->current_client_id;
    int client_gender = data->current_client_gender;
    int client_rent_time = data->current_client_rent_time;
    printf("[CLIENT-%d] started.\n", client_id);
    sem_post(current_client_wait_sem);

    // Ищем свободную комнату, перед этим блокируем семафор.
    sem_wait(rooms_semaphore);

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
    sem_post(rooms_semaphore);

    // Если нашли комнату, то значит, что статус мы изменили.
    if (used_double_idx == -1 && used_single_idx == -1) {
        printf("[CLIENT-%d] out of service!\n", client_id);
        return;
    }

    // Бронируем комнату и ждем ...
    printf("[CLIENT-%d] waiting %ds.\n", client_id, client_rent_time);
    sleep(client_rent_time);

    // Теперь освободим комнату.
    sem_wait(rooms_semaphore);

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
    sem_post(rooms_semaphore);
}

// Общие переменные для работы программы.
int rooms_fd;
bool is_child_process;
FILE *client_file;
rooms_data_t *rooms_data;
sem_t *current_client_wait_sem;
sem_t *rooms_semaphore;

// Освобождает занятые процессом ресурсы.
void free_resources() {
    // Ждем завершения всех дочерних процессов.
    while (wait(NULL) > 0) {
    }

    // Освобождаем ресурсы.
    fclose(client_file);
    sem_close(rooms_semaphore);

    if (!is_child_process) {
        sem_unlink(ROOMS_SEM_NAME);
    }

    sem_destroy(rooms_semaphore);
    sem_close(current_client_wait_sem);

    if (!is_child_process) {
        sem_unlink(CURRENT_CLIENT_WAIT_SEM_NAME);
    }

    sem_destroy(current_client_wait_sem);

    if (!is_child_process) {
        munmap(rooms_data, sizeof(rooms_data_t));
        shm_unlink(ROOMS_MEM_NAME);
    }

    exit(1);
}

// Обрабатывает сигнал завершения программы.
void handle_sigterm(__attribute__((unused)) int signal) {
    free_resources();
}

int main() {
    is_child_process = false;

    // Инициализируем доступ к shared memory для работы с состояниями комнат.
    rooms_fd = shm_open(ROOMS_MEM_NAME, O_RDWR | O_CREAT, 0666);
    if (rooms_fd == -1) {
        perror("shm_open: rooms_fd == -1");
        return 1;
    }

    if (ftruncate(rooms_fd, sizeof(rooms_data_t)) == -1) {
        perror("ftruncate(rooms_fd) == -1");
        return 1;
    }

    rooms_data = mmap(NULL, sizeof(rooms_data_t), PROT_READ | PROT_WRITE, MAP_SHARED, rooms_fd, 0);
    if (rooms_data == MAP_FAILED) {
        perror("rooms_data == MAP_FAILED");
        return 1;
    }

    // Инициализируем состояние комнат.
    memset(rooms_data->single_rooms, freed, sizeof(room_status) * SINGLE_ROOMS_COUNT);
    memset(rooms_data->double_rooms, freed, sizeof(room_status) * DOUBLE_ROOMS_COUNT);

    // Открываем семафоры для первичной инициализации.
    rooms_semaphore = sem_open(ROOMS_SEM_NAME, O_CREAT | O_EXCL, 0644, 1);
    current_client_wait_sem = sem_open(CURRENT_CLIENT_WAIT_SEM_NAME, O_CREAT | O_EXCL, 0644, 1);
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
        sem_wait(current_client_wait_sem);

        // Если же этот код уже исполняется в дочернем процессе
        if (fork() == 0) {
            is_child_process = true;
            signal(SIGTERM, previous);
            sem_post(current_client_wait_sem);
            handle_client_process(rooms_data, rooms_semaphore, current_client_wait_sem);
            break;
        }

        sem_wait(current_client_wait_sem);
    }

    free_resources();
    return 0;
}

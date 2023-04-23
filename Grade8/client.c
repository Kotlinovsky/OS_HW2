#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <stdio.h>

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

// Общие переменные для работы программы.
int rooms_fd;
int rooms_semaphore_id;
rooms_data_t *rooms_data;

// Освобождает занятые процессом ресурсы.
void free_resources() {
    semctl(rooms_semaphore_id, 0, IPC_RMID, 0);
    shmdt(rooms_data);
    exit(1);
}

// Обрабатывает сигнал завершения программы.
void handle_sigterm(__attribute__((unused)) int signal) {
    if (signal == SIGTERM) {
        free_resources();
    }
}

int main(__attribute__((unused)) int argc, char *argv[]) {
    int client_id = atoi(argv[1]);
    int client_gender = atoi(argv[2]);
    int client_rent_time = atoi(argv[3]);
    key_t shm_key = ftok("/tmp", 0x182003);
    key_t sem_key = ftok("/tmp", 0x182004);

    // Инициализируем доступ к shared memory для работы с состояниями комнат.
    rooms_fd = shmget(shm_key, sizeof(rooms_data_t), IPC_CREAT | 0666);
    rooms_data = shmat(rooms_fd, NULL, 0);
    rooms_semaphore_id = semget(sem_key, 1, IPC_CREAT | 0666);
    signal(SIGTERM, handle_sigterm);

    // Ищем свободную комнату, перед этим блокируем семафор.
    wait_semaphore(rooms_semaphore_id);

    // Теперь поищем нужный нам отель!
    // Сначала проверим комнаты на одно спальное место.
    int used_single_idx = -1;
    int used_double_idx = -1;

    for (int i = 0; i < SINGLE_ROOMS_COUNT; ++i) {
        if (rooms_data->single_rooms[i] == freed) {
            printf("[CLIENT-%d] rent single room: idx = %i.\n", client_id, i);
            rooms_data->single_rooms[i] = full;
            used_single_idx = i;
            break;
        }
    }

    // Если же не нашли на одно место, то поищем на два места, но при условии, либо комната пуста,
    // либо в ней живет человек того же пола.
    if (used_single_idx == -1) {
        for (int i = 0; i < DOUBLE_ROOMS_COUNT; ++i) {
            if (rooms_data->double_rooms[i] == freed) {
                rooms_data->double_rooms[i] = client_gender == 0 ? busied_by_man : busied_by_woman;
                printf("[CLIENT-%d] rent double room: idx = %d, gender = %d.\n", client_id, i, client_gender);
                used_double_idx = i;
                break;
            } else if ((rooms_data->double_rooms[i] == busied_by_man && client_gender == 0) ||
                       (rooms_data->double_rooms[i] == busied_by_woman && client_gender == 1)) {
                printf("[CLIENT-%d] rent double room: idx = %d, gender = %d, with_gender = %d.\n", client_id, i,
                       client_gender, client_gender);

                rooms_data->double_rooms[i] = full;
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
        free_resources();
        return 0;
    }

    // Бронируем комнату и ждем ...
    printf("[CLIENT-%d] waiting %ds.\n", client_id, client_rent_time);
    sleep(client_rent_time);

    // Теперь освободим комнату.
    wait_semaphore(rooms_semaphore_id);

    if (used_double_idx >= 0) {
        if (rooms_data->double_rooms[used_double_idx] == full) {
            rooms_data->double_rooms[used_double_idx] = client_gender == 0 ? busied_by_man : busied_by_woman;
        } else if (rooms_data->double_rooms[used_double_idx] == busied_by_man ||
                   rooms_data->double_rooms[used_double_idx] == busied_by_woman) {
            rooms_data->double_rooms[used_double_idx] = freed;
        }
    } else if (used_single_idx >= 0) {
        rooms_data->single_rooms[used_single_idx] = freed;
    }

    printf("[CLIENT-%d] end of rent!\n", client_id);
    semctl(rooms_semaphore_id, 0, SETVAL, 0);
    free_resources();
    return 0;
}

#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/sem.h>
#include <sys/shm.h>

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

// Общие переменные для работы программы.
int rooms_fd;
int rooms_semaphore_id;
rooms_data_t *rooms_data;

// Освобождает занятые процессом ресурсы.
void free_resources() {
    printf("[HOTEL] Stopping ...\n");

    // Освобождаем ресурсы.
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

int main() {
    key_t shm_key = ftok("/tmp", 0x182003);
    key_t sem_key = ftok("/tmp", 0x182004);

    // Открываем семафоры для первичной инициализации.
    // Инициализируем доступ к shared memory для работы с состояниями комнат.
    rooms_fd = shmget(shm_key, sizeof(rooms_data_t), IPC_CREAT | 0666);
    rooms_data = shmat(rooms_fd, NULL, 0);
    rooms_semaphore_id = semget(sem_key, 1, IPC_CREAT | 0666);

    // Инициализируем состояние комнат.
    memset(rooms_data->single_rooms, freed, sizeof(room_status) * SINGLE_ROOMS_COUNT);
    memset(rooms_data->double_rooms, freed, sizeof(room_status) * DOUBLE_ROOMS_COUNT);

    printf("[HOTEL] Started state hosting.\n");
    signal(SIGTERM, handle_sigterm);
    fflush(stdout);

    // Ожидаем теперь SIGCONTINUE.
    pause();
    free_resources();
    return 0;
}

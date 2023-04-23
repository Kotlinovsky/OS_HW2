#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>

#define SINGLE_ROOMS_COUNT 10
#define DOUBLE_ROOMS_COUNT 15
#define ROOMS_MEM_NAME "/rooms_mem"
#define ROOMS_SEM_NAME "/rooms_sem182003"

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
rooms_data_t *rooms_data;
sem_t *rooms_semaphore;

// Освобождает занятые процессом ресурсы.
void free_resources() {
    printf("[HOTEL] Stopping ...\n");

    // Освобождаем ресурсы.
    sem_close(rooms_semaphore);
    sem_unlink(ROOMS_SEM_NAME);
    sem_destroy(rooms_semaphore);
    munmap(rooms_data, sizeof(rooms_data_t));
    shm_unlink(ROOMS_MEM_NAME);
    exit(1);
}

// Обрабатывает сигнал завершения программы.
void handle_sigterm(__attribute__((unused)) int signal) {
    if (signal == SIGTERM) {
        free_resources();
    }
}

int main() {
    // Инициализируем доступ к shared memory для работы с состояниями комнат.
    rooms_fd = shm_open(ROOMS_MEM_NAME, O_RDWR | O_CREAT, 0666);
    ftruncate(rooms_fd, sizeof(rooms_data_t));
    rooms_data = mmap(NULL, sizeof(rooms_data_t), PROT_READ | PROT_WRITE, MAP_SHARED, rooms_fd, 0);

    // Инициализируем состояние комнат.
    memset(rooms_data->single_rooms, freed, sizeof(room_status) * SINGLE_ROOMS_COUNT);
    memset(rooms_data->double_rooms, freed, sizeof(room_status) * DOUBLE_ROOMS_COUNT);

    // Открываем семафоры для первичной инициализации.
    rooms_semaphore = sem_open(ROOMS_SEM_NAME, O_CREAT | O_EXCL, 0644, 1);
    printf("[HOTEL] Started state hosting.\n");
    signal(SIGTERM, handle_sigterm);
    fflush(stdout);

    // Ожидаем теперь SIGCONTINUE.
    pause();
    free_resources();
    return 0;
}

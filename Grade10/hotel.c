#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#define SINGLE_ROOMS_COUNT 10
#define DOUBLE_ROOMS_COUNT 15
#define ROOMS_INPUT_NAME "/tmp/rooms_input23102"
#define ROOMS_OUTPUT_NAME "/tmp/rooms_output23102"

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

// Структура с данными сообщения
typedef struct {
    int packet_id;
    rooms_data_t data;
} message_t;

// Общие переменные для работы программы.
int rooms_input_fd;
int rooms_output_fd;
int rooms_semaphore_id;
message_t *input_buffer;
rooms_data_t *rooms_data;

// Освобождает занятые процессом ресурсы.
void free_resources() {
    printf("[HOTEL] Stopping ...\n");

    // Освобождаем ресурсы.
    close(rooms_input_fd);
    close(rooms_output_fd);
    semctl(rooms_semaphore_id, 0, IPC_RMID, 0);
    free(input_buffer);
    free(rooms_data);
    unlink(ROOMS_OUTPUT_NAME);
    unlink(ROOMS_INPUT_NAME);
    exit(1);
}

// Обрабатывает сигнал завершения программы.
void handle_sigterm(__attribute__((unused)) int signal) {
    if (signal == SIGTERM) {
        free_resources();
    }
}

int main() {
    mkfifo(ROOMS_INPUT_NAME, 0666);
    mkfifo(ROOMS_OUTPUT_NAME, 0666);
    key_t sem_key = ftok("/tmp", 0x18200456);

    // Открываем дескрипторы для чтения и записи
    rooms_semaphore_id = semget(sem_key, 1, IPC_CREAT | 0666);
    rooms_output_fd = open(ROOMS_OUTPUT_NAME, O_RDWR);
    rooms_input_fd = open(ROOMS_INPUT_NAME, O_RDWR);
    input_buffer = malloc(sizeof(message_t));

    // Инициализируем состояние комнат.
    rooms_data = malloc(sizeof(rooms_data_t));
    memset(rooms_data->single_rooms, freed, sizeof(room_status) * SINGLE_ROOMS_COUNT);
    memset(rooms_data->double_rooms, freed, sizeof(room_status) * DOUBLE_ROOMS_COUNT);
    printf("[HOTEL] Started state hosting.\n");
    signal(SIGTERM, handle_sigterm);

    while (1) {
        read(rooms_input_fd, input_buffer, sizeof(message_t));

        if (input_buffer->packet_id == 2) {
            for (int i = 0; i < SINGLE_ROOMS_COUNT; ++i) {
                rooms_data->single_rooms[i] = input_buffer->data.single_rooms[i];
            }
            for (int i = 0; i < DOUBLE_ROOMS_COUNT; ++i) {
                rooms_data->double_rooms[i] = input_buffer->data.double_rooms[i];
            }
        }

        write(rooms_output_fd, rooms_data, sizeof(rooms_data_t));
    }
}

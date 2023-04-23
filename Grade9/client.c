#include <fcntl.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#define SINGLE_ROOMS_COUNT 10
#define DOUBLE_ROOMS_COUNT 15
#define ROOMS_INPUT_NAME "/tmp/rooms_input23"
#define ROOMS_OUTPUT_NAME "/tmp/rooms_output23"
#define ROOMS_SEM_NAME "/rooms_sem18200345678"

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
rooms_data_t *rooms_data;
sem_t *rooms_semaphore;

// Освобождает занятые процессом ресурсы.
void free_resources() {
    sem_close(rooms_semaphore);
    sem_destroy(rooms_semaphore);
    close(rooms_output_fd);
    close(rooms_input_fd);
    free(rooms_data);
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

    // Инициализируем семафоры и сокеты.
    rooms_semaphore = sem_open(ROOMS_SEM_NAME, O_EXCL, 0644, 1);
    rooms_input_fd = open(ROOMS_INPUT_NAME, O_RDWR);
    rooms_output_fd = open(ROOMS_OUTPUT_NAME, O_RDWR);
    rooms_data = malloc(sizeof(rooms_data_t));
    signal(SIGTERM, handle_sigterm);

    // Ищем свободную комнату, перед этим блокируем семафор.
    sem_wait(rooms_semaphore);

    // Запрашиваем у отеля состояние его комнат.
    message_t get_state_msg;
    get_state_msg.packet_id = 1;
    write(rooms_input_fd, &get_state_msg, sizeof(message_t));
    read(rooms_output_fd, rooms_data, sizeof(rooms_data_t));

    // Теперь поищем нужный нам отель!
    // Сначала проверим комнаты на одно спальное место.
    int used_single_idx = -1;
    int used_double_idx = -1;

    for (int i = 0; i < SINGLE_ROOMS_COUNT; ++i) {
        if (rooms_data->single_rooms[i] == freed) {
            printf("[CLIENT-%d] rent single room: idx = %li.\n", client_id, i);
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

    message_t set_state_msg;
    set_state_msg.packet_id = 2;
    for (int i = 0; i < SINGLE_ROOMS_COUNT; ++i) {
        set_state_msg.data.single_rooms[i] = rooms_data->single_rooms[i];
    }
    for (int i = 0; i < DOUBLE_ROOMS_COUNT; ++i) {
        set_state_msg.data.double_rooms[i] = rooms_data->double_rooms[i];
    }

    write(rooms_input_fd, &set_state_msg, sizeof(message_t));
    read(rooms_output_fd, rooms_data, sizeof(rooms_data_t));

    // Разблокируем семафор, чтобы другой процесс забронировал комнату.
    sem_post(rooms_semaphore);

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
    sem_wait(rooms_semaphore);

    // Запрашиваем у отеля состояние его комнат.
    write(rooms_input_fd, &get_state_msg, sizeof(message_t));
    read(rooms_output_fd, rooms_data, sizeof(rooms_data_t));

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

    for (int i = 0; i < SINGLE_ROOMS_COUNT; ++i) {
        set_state_msg.data.single_rooms[i] = rooms_data->single_rooms[i];
    }
    for (int i = 0; i < DOUBLE_ROOMS_COUNT; ++i) {
        set_state_msg.data.double_rooms[i] = rooms_data->double_rooms[i];
    }

    write(rooms_input_fd, &set_state_msg, sizeof(message_t));
    read(rooms_output_fd, rooms_data, sizeof(rooms_data_t));

    printf("[CLIENT-%d] end of rent!\n", client_id);
    sem_post(rooms_semaphore);
    free_resources();
    return 0;
}

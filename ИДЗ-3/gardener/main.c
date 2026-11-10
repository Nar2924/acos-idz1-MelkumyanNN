#include "../common/shared_garden.h"
#include "../common/ipc_names.h"
#include "../common/log.h"
#include "gardener_logic.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <string.h>

volatile int g_stop = 0;

void signal_handler(int signo) {
    (void)signo;
    g_stop = 1;
}

int main(int argc, char *argv[]) {
    signal(SIGINT, signal_handler);

    int gardener_id = getpid();
    double speed = 15.0;

    if (argc > 1) {
        gardener_id = atoi(argv[1]);
    }
    
    // Если передали скорость аргументом, используем её. Иначе запрашиваем.
    if (argc > 2) {
        speed = atof(argv[2]);
    } else {
        char buf[256];
        printf("Введите скорость садовника ID %d (или нажмите Enter для 15.0): ", gardener_id);
        fflush(stdout);
        if (fgets(buf, sizeof(buf), stdin) != NULL) {
            if (buf[0] != '\n' && buf[0] != '\r') {
                double val = atof(buf);
                if (val > 0.0) {
                    speed = val;
                }
            }
        }
    }

    int shm_fd = shm_open(SHM_GARDEN_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        printf("Ошибка: не удалось открыть разделяемую память. Сначала запустите сад!\n");
        exit(EXIT_FAILURE);
    }

    struct stat shm_stat;
    if (fstat(shm_fd, &shm_stat) == -1) { perror("fstat"); exit(EXIT_FAILURE); }
    size_t shm_size = shm_stat.st_size;

    SharedGarden *garden = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (garden == MAP_FAILED) { perror("mmap"); exit(EXIT_FAILURE); }

    sem_t *sem_garden = sem_open(SEM_GARDEN_NAME, 0);
    sem_t *sem_sync = sem_open(SEM_GARDENER_SYNC, 0);
    sem_t *sem_start = sem_open(SEM_START_NAME, 0);
    log_init();
    
    if (sem_garden == SEM_FAILED || sem_sync == SEM_FAILED || sem_start == SEM_FAILED) {
        printf("Ошибка: не удалось открыть семафоры.\n");
        exit(EXIT_FAILURE);
    }

    // Регистрация в саду
    sem_wait(sem_garden);
    if (garden->registered_gardeners < MAX_GARDENERS) {
        garden->gardener_pids[garden->registered_gardeners++] = gardener_id;
    }
    int is_started = garden->simulation_started;
    sem_post(sem_garden);

    printf("Успешно подключено к саду! (ID садовника: %d, Скорость полива: %.1f)\n", gardener_id, speed);

    if (!is_started) {
        printf("Ожидание запуска симуляции...\n");
        sem_wait(sem_start);
    }

    printf("Симуляция началась, приступаю к работе!\n");
    int busy_flower_id = -1;

    while (!g_stop) {
        sem_wait(sem_garden);
        if (garden->stop_flag) {
            sem_post(sem_garden);
            break;
        }

        if (busy_flower_id != -1) {
            if (garden->flowers[busy_flower_id].watering_by == gardener_id) {
                sem_post(sem_garden);
                usleep(50000);
                continue;
            } else {
                busy_flower_id = -1;
            }
        }

        int target = garden_select_flower(garden);
        if (target >= 0) {
            if (garden_start_watering(garden, gardener_id, target, garden->water_volume)) {
                garden->flowers[target].watering_rate = speed;
                busy_flower_id = target;
                log_event(garden->current_time, "GARDENER %d (speed %.1f): started watering flower %d", 
                          gardener_id, speed, target);
            }
        }
        
        sem_post(sem_garden);

        if (target < 0) {
            sem_wait(sem_sync); 
        }
    }

    log_close();
    munmap(garden, shm_size);
    close(shm_fd);

    printf("Садовник %d завершил работу.\n", gardener_id);
    return 0;
}

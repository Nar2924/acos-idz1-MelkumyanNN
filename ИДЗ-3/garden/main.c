#include "../common/config.h"
#include "../common/ipc_names.h"
#include "../common/log.h"
#include "../common/shared_garden.h"
#include "garden_logic.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <unistd.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>

volatile int g_stop = 0;

void signal_handler(int signo) {
  (void)signo;
  g_stop = 1;
}

static int line_is_blank(const char *s) {
  while (*s) {
    if (!isspace((unsigned char)*s))
      return 0;
    s++;
  }
  return 1;
}

static int read_line_safe(char *buf, size_t size) {
  if (fgets(buf, (int)size, stdin) == NULL)
    return 0;
  size_t len = strlen(buf);
  if (len > 0 && buf[len - 1] == '\n')
    buf[len - 1] = '\0';
  else
    while (getchar() != '\n' && !feof(stdin))
      ;
  return 1;
}

int read_int_param(const char *prompt, int default_value) {
  char buf[256];
  printf("%s [%d]: ", prompt, default_value);
  fflush(stdout);
  if (!read_line_safe(buf, sizeof(buf)) || line_is_blank(buf))
    return default_value;
  char *end;
  long val = strtol(buf, &end, 10);
  if (errno != 0 || end == buf || val < INT_MIN || val > INT_MAX)
    return default_value;
  while (*end)
    if (!isspace((unsigned char)*end++))
      return default_value;
  return (int)val;
}

double read_double_param(const char *prompt, double default_value) {
  char buf[256];
  printf("%s [%g]: ", prompt, default_value);
  fflush(stdout);
  if (!read_line_safe(buf, sizeof(buf)) || line_is_blank(buf))
    return default_value;
  char *end;
  double val = strtod(buf, &end);
  if (errno != 0 || end == buf || !isfinite(val))
    return default_value;
  while (*end)
    if (!isspace((unsigned char)*end++))
      return default_value;
  return val;
}

void init_config_from_console(Config *config) {
  printf("--- Настройка параметров клумбы ---\n");
  config->flower_count = read_int_param("Количество цветов", 8);
  config->initial_moisture = read_double_param("Начальная влажность", 80.0);
  config->dry_rate_min = read_double_param("Мин. скорость высыхания", 1.0);
  config->dry_rate_max = read_double_param("Макс. скорость высыхания", 3.0);
  config->wilt_threshold = read_double_param("Порог увядания", 40.0);
  config->dry_threshold = read_double_param("Порог засыхания", 15.0);
  config->water_volume = read_double_param("Объем полива", 30.0);
  config->protection_interval =
      read_double_param("Интервал защиты после полива", 10.0);
  config->duration = read_double_param("Длительность симуляции", 60.0);
  config->time_step_ms = read_int_param("Шаг времени (в миллисекундах)", 100);
  printf("\nНастройки успешно применены!\n\n");
}

int main(void) {
  srand(time(NULL));
  signal(SIGINT, signal_handler);

  Config config;
  init_config_from_console(&config);

  shm_unlink(SHM_GARDEN_NAME);
  sem_unlink(SEM_GARDEN_NAME);
  sem_unlink(SEM_GARDENER_SYNC);
  sem_unlink(SEM_START_NAME);
  sem_unlink(SEM_LOG_NAME);
  remove(LOG_FILE_PATH);

  int shm_fd = shm_open(SHM_GARDEN_NAME, O_CREAT | O_RDWR, 0666);
  if (shm_fd == -1) {
    perror("shm_open");
    exit(EXIT_FAILURE);
  }

  size_t shm_size = sizeof(SharedGarden) + config.flower_count * sizeof(Flower);
  if (ftruncate(shm_fd, shm_size) == -1) {
    perror("ftruncate");
    exit(EXIT_FAILURE);
  }

  SharedGarden *garden =
      mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (garden == MAP_FAILED) {
    perror("mmap");
    exit(EXIT_FAILURE);
  }

  garden_init_data(garden, &config);

  sem_t *sem_garden = sem_open(SEM_GARDEN_NAME, O_CREAT, 0666, 1);
  sem_t *sem_sync = sem_open(SEM_GARDENER_SYNC, O_CREAT, 0666, 0);
  sem_t *sem_start = sem_open(SEM_START_NAME, O_CREAT, 0666, 0);
  log_init();

  printf("=======================================================\n");
  printf("Подключите всех садовников в других окнах терминала.\n");
  printf("Нажмите Enter после того, как все садовники будут подключены.\n");
  printf("=======================================================\n");

  int known_gardeners = 0;

  // Цикл ожидания подключения с использованием select
  while (!g_stop) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000; // 100ms

    int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
    if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
      char buf[256];
      if (fgets(buf, sizeof(buf), stdin)) {
        break; // Enter нажат
      }
    }

    sem_wait(sem_garden);
    while (known_gardeners < garden->registered_gardeners) {
      printf(">>> Садовник %d подключился к саду!\n",
             garden->gardener_pids[known_gardeners]);
      known_gardeners++;
    }
    sem_post(sem_garden);
  }

  if (g_stop)
    goto cleanup;

  // Оповещаем садовников о старте
  sem_wait(sem_garden);
  garden->simulation_started = 1;
  int total_gardeners = garden->registered_gardeners;
  sem_post(sem_garden);

  for (int i = 0; i < total_gardeners; i++) {
    sem_post(sem_start);
  }

  log_event(0.0, "Garden simulation started with %d gardeners",
            total_gardeners);
  printf("Симуляция началась! Нажмите Ctrl+C для завершения.\n");

  double dt = config.time_step_ms / 1000.0;

  while (!g_stop) {
    if (config.duration > 0 && garden->current_time >= config.duration)
      break;

    sem_wait(sem_garden);
    int alive = garden->alive_count;
    sem_post(sem_garden);

    if (alive == 0)
      break;

    sem_wait(sem_garden);
    bool needs_gardener = garden_tick(garden, dt);
    sem_post(sem_garden);

    if (needs_gardener) {
      for (int i = 0; i < total_gardeners; i++)
        sem_post(sem_sync);
    }

    usleep(config.time_step_ms * 1000);
  }

cleanup:
  g_stop = 1;
  sem_wait(sem_garden);
  garden->stop_flag = 1;
  sem_post(sem_garden);

  for (int i = 0; i < total_gardeners + 5; i++)
    sem_post(sem_sync);

  garden_print_statistics(garden);
  log_event(garden->current_time, "Garden simulation finished");

  log_close();
  munmap(garden, shm_size);
  close(shm_fd);

  shm_unlink(SHM_GARDEN_NAME);
  sem_unlink(SEM_GARDEN_NAME);
  sem_unlink(SEM_GARDENER_SYNC);
  sem_unlink(SEM_START_NAME);
  sem_unlink(SEM_LOG_NAME);

  printf("Ресурсы очищены, симуляция завершена.\n");
  return 0;
}

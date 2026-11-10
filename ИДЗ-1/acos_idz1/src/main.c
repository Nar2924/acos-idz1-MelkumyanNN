#include "config.h"
#include "garden.h"
#include "gardener.h"
#include "log.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

volatile int g_stop = 0; // <---- флаг завершения программы

void signal_handler(int signo) {
  (void)signo;
  g_stop = 1;
}

static int line_is_blank(const char *s) {
  while (*s) {
    if (!isspace((unsigned char)*s)) {
      return 0;
    }
    s++;
  }
  return 1;
}

static int read_line_safe(char *buf, size_t size) {
  if (fgets(buf, (int)size, stdin) == NULL) {
    return 0;
  }

  size_t len = strlen(buf);

  if (len > 0 && buf[len - 1] == '\n') {
    buf[len - 1] = '\0';
  } else {
    // Если строка была слишком длинной, выбрасываем остаток.
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {
      // ничего не делаем
    }
  }

  return 1;
}

int read_int_param(const char *prompt, int default_value) {
  char buf[256];

  printf("%s [%d]: ", prompt, default_value);
  fflush(stdout);

  if (!read_line_safe(buf, sizeof(buf))) {
    return default_value;
  }

  if (line_is_blank(buf)) {
    return default_value;
  }

  errno = 0;
  char *end = NULL;
  long value = strtol(buf, &end, 10);

  // end == buf означает, что число вообще не было прочитано.
  if (errno != 0 || end == buf) {
    printf(
        "Некорректный тип значения. Используется значение по умолчанию: %d\n",
        default_value);
    return default_value;
  }

  // Проверяем, что после числа нет лишнего текста.
  while (*end != '\0') {
    if (!isspace((unsigned char)*end)) {
      printf(
          "Некорректный тип значения. Используется значение по умолчанию: %d\n",
          default_value);
      return default_value;
    }
    end++;
  }

  if (value < INT_MIN || value > INT_MAX) {
    printf("Значение выходит за пределы допустимого диапазона. "
           "Используется значение по умолчанию: %d\n",
           default_value);
    return default_value;
  }

  return (int)value;
}

double read_double_param(const char *prompt, double default_value) {
  char buf[256];

  printf("%s [%g]: ", prompt, default_value);
  fflush(stdout);

  if (!read_line_safe(buf, sizeof(buf))) {
    return default_value;
  }

  if (line_is_blank(buf)) {
    return default_value;
  }

  errno = 0;
  char *end = NULL;
  double value = strtod(buf, &end);

  // end == buf означает, что число вообще не было прочитано.
  if (errno != 0 || end == buf) {
    printf(
        "Некорректный тип значения. Используется значение по умолчанию: %g\n",
        default_value);
    return default_value;
  }

  // Проверяем, что после числа нет лишнего текста.
  while (*end != '\0') {
    if (!isspace((unsigned char)*end)) {
      printf(
          "Некорректный тип значения. Используется значение по умолчанию: %g\n",
          default_value);
      return default_value;
    }
    end++;
  }

  if (!isfinite(value)) {
    printf("Значение не является корректным числом. "
           "Используется значение по умолчанию: %g\n",
           default_value);
    return default_value;
  }

  return value;
}
// Функция для интерактивной настройки конфигурации
void init_config_from_console(Config *config) {
  printf("--- Настройка параметров клумбы ---\n");
  printf("Введите значение и нажмите Enter. Чтобы оставить значение по "
         "умолчанию (в скобках), просто нажмите Enter.\n\n");

  config->flower_count = read_int_param("Количество цветов", 8);
  config->initial_moisture = read_double_param("Начальная влажность", 80.0);
  config->dry_rate_min = read_double_param("Мин. скорость высыхания", 1.0);
  config->dry_rate_max = read_double_param("Макс. скорость высыхания", 3.0);
  config->wilt_threshold = read_double_param("Порог увядания", 40.0);
  config->dry_threshold = read_double_param("Порог засыхания", 15.0);
  config->water_volume = read_double_param("Объем полива", 30.0);
  config->protection_interval =
      read_double_param("Интервал защиты после полива", 10.0);
  config->gardener_speed = read_double_param("Скорость садовника", 15.0);
  config->duration = read_double_param("Длительность симуляции", 60.0);
  config->time_step_ms = read_int_param("Шаг времени (в миллисекундах)", 100);

  printf("\nНастройки успешно применены!\n\n");
}

int main(void) {
  srand(time(NULL));
  signal(SIGINT, signal_handler);

  Config config;
  init_config_from_console(&config);

  Garden garden;
  garden_init(&garden, &config);

  Gardener g1, g2;
  gardener_init(&g1, 1, &garden, config.gardener_speed);
  gardener_init(&g2, 2, &garden, config.gardener_speed);

  log_event(0.0, "Simulation started");

  // Создаем потоки наших садовников
  pthread_create(&g1.thread, NULL, gardener_thread, &g1);
  pthread_create(&g2.thread, NULL, gardener_thread, &g2);

  double dt = config.time_step_ms / 1000.0;

  while (!g_stop) {
    if (config.duration > 0 && garden.current_time >= config.duration)
      break;

    pthread_mutex_lock(&garden.lock);
    int alive = garden.alive_count;
    pthread_mutex_unlock(&garden.lock);

    if (alive == 0)
      break;

    garden_tick(&garden, dt);
    usleep(config.time_step_ms * 1000);
  }

  g_stop = 1;

  // Будим потоки для завершения
  pthread_mutex_lock(&garden.lock);
  pthread_cond_broadcast(&garden.changed);
  pthread_mutex_unlock(&garden.lock);

  pthread_join(g1.thread, NULL);
  pthread_join(g2.thread, NULL);

  garden_print_statistics(&garden);
  garden_destroy(&garden);

  log_event(garden.current_time, "Simulation finished");
  return 0;
}

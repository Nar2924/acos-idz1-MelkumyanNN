#include "config.h"
#include "net.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

Config config_defaults(void) {
    return (Config){8, 80.0, 1.0, 3.0, 40.0, 15.0, 30.0, 10.0, 60.0, 100, FLOWER_AUTO};
}

static int read_parameter(const char *prompt, double fallback, char value[128]) {
    printf("%s [%g]: ", prompt, fallback);
    fflush(stdout);
    if (!fgets(value, 128, stdin)) {
        return feof(stdin) ? 0 : -1;
    }

    size_t length = strlen(value);
    if (length == 127 && value[length - 1] != '\n') {
        return -1;
    }
    while (length > 0 && isspace((unsigned char)value[length - 1])) {
        value[--length] = '\0';
    }
    return length != 0;
}

static int read_integer(const char *prompt, int *value, long min, long max) {
    char line[128];
    long parsed;
    int result = read_parameter(prompt, *value, line);
    if (result == 0) {
        return 0;
    }
    if (result < 0 || !parse_integer(line, min, max, &parsed)) {
        fprintf(stderr, "\nНекорректное значение: %s\n", prompt);
        return -1;
    }
    *value = (int)parsed;
    return 0;
}

static int read_real(const char *prompt, double *value) {
    char line[128];
    int result = read_parameter(prompt, *value, line);
    if (result == 0) {
        return 0;
    }
    if (result < 0 || !parse_real(line, value)) {
        fprintf(stderr, "\nНекорректное значение: %s\n", prompt);
        return -1;
    }
    return 0;
}

int config_read(Config *config) {
    puts("--- Настройка параметров клумбы ---");
    if (read_integer("Количество цветов", &config->flower_count, 1, MAX_FLOWERS) ||
        read_real("Начальная влажность", &config->initial_moisture) ||
        read_real("Мин. скорость высыхания", &config->dry_rate_min) ||
        read_real("Макс. скорость высыхания", &config->dry_rate_max) ||
        read_real("Порог увядания", &config->wilt_threshold) ||
        read_real("Порог засыхания", &config->dry_threshold) ||
        read_real("Объем полива", &config->water_volume) ||
        read_real("Интервал защиты после полива", &config->protection_interval) ||
        read_real("Длительность симуляции", &config->duration) ||
        read_integer("Шаг времени в миллисекундах", &config->time_step_ms, 1, 60000)) {
        return -1;
    }
    putchar('\n');
    return 0;
}

int config_validate(const Config *config) {
    if (config->initial_moisture < 0.0 || config->dry_rate_min < 0.0 ||
        config->dry_rate_max < config->dry_rate_min ||
        config->dry_threshold < 0.0 || config->wilt_threshold <= config->dry_threshold ||
        config->water_volume <= 0.0 || config->protection_interval < 0.0) {
        fputs("Проверьте влажность, диапазон скоростей, порядок порогов, объем и интервал полива.\n",
              stderr);
        return -1;
    }
    double moisture = config->initial_moisture;
    FlowerState state = config->initial_state;
    if (((state == FLOWER_NORMAL || state == FLOWER_WATERED) &&
         moisture <= config->wilt_threshold) ||
        (state == FLOWER_WATERED && config->protection_interval == 0.0) ||
        (state == FLOWER_WILTING &&
         (moisture <= config->dry_threshold || moisture > config->wilt_threshold)) ||
        (state == FLOWER_DRYED && moisture > config->dry_threshold)) {
        fputs("Начальное состояние не согласуется с влажностью, порогами или интервалом защиты.\n",
              stderr);
        return -1;
    }
    return 0;
}

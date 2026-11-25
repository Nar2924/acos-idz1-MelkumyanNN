#ifndef CONFIG_H
#define CONFIG_H

#include "flower.h"

#define MAX_FLOWERS 1000

typedef struct {
    int flower_count;
    double initial_moisture;
    double dry_rate_min;
    double dry_rate_max;
    double wilt_threshold;
    double dry_threshold;
    double water_volume;
    double protection_interval;
    double duration;
    int time_step_ms;
    FlowerState initial_state;
} Config;

Config config_defaults(void);
int config_read(Config *config);
int config_validate(const Config *config);

#endif

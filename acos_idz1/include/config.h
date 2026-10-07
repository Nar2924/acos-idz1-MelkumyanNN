#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    int flower_count;
    double initial_moisture;
    double dry_rate_min;
    double dry_rate_max;
    double wilt_threshold;
    double dry_threshold;
    double water_volume;
    double protection_interval;
    double gardener_speed;
    double duration;
    int time_step_ms;
} Config;

#endif

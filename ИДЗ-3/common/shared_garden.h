#ifndef SHARED_GARDEN_H
#define SHARED_GARDEN_H

#include "config.h"
#include "flower.h"

#define MAX_GARDENERS 64

// Структура в разделяемой памяти
typedef struct {
    int flower_count;
    double current_time;
    double wilt_threshold;
    double dry_threshold;
    double water_volume;
    double protection_interval;
    int alive_count;
    int stop_flag;
    
    // Новые поля для фазы подключения
    int simulation_started;
    int registered_gardeners;
    int gardener_pids[MAX_GARDENERS];
    
    Flower flowers[];
} SharedGarden;

#endif

#ifndef GARDEN_LOGIC_H
#define GARDEN_LOGIC_H

#include "../common/config.h"
#include "../common/flower.h"

typedef struct {
    Config config;
    double current_time;
    int alive_count;
    Flower flowers[];
} Garden;

typedef enum { FLOWER_CHANGED, WATERING_FINISHED } GardenEvent;
typedef void (*GardenNotify)(void *context, GardenEvent event, int flower_id, int previous);
typedef enum { WATER_RETRY, WATER_STARTED, WATER_OVERFLOW } WateringResult;

Garden *garden_create(const Config *config);
void garden_tick(Garden *garden, double dt, GardenNotify notify, void *context);
bool garden_is_available(const Garden *garden, const Flower *flower);
WateringResult garden_try_watering(Garden *garden, int flower_id, int observed_cycles,
                                 int gardener_id, double speed);
void garden_cancel_watering(Flower *flower);
const char *flower_state_name(FlowerState state);

#endif

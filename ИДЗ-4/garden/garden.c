#include "garden_logic.h"

#include <stdlib.h>

static FlowerState calculate_flower_state(const Garden *garden, const Flower *flower);

Garden *garden_create(const Config *config) {
    Garden *garden = calloc(1, sizeof(*garden) + (size_t)config->flower_count * sizeof(Flower));
    if (!garden) {
        return NULL;
    }
    garden->config = *config;
    garden->alive_count = config->flower_count;
    for (int i = 0; i < config->flower_count; ++i) {
        Flower *flower = &garden->flowers[i];
        flower->moisture = config->initial_moisture;
        flower->dry_rate = config->dry_rate_min +
            (config->dry_rate_max - config->dry_rate_min) * ((double)rand() / RAND_MAX);
        flower->watering_by = -1;
        flower->state = config->initial_state == FLOWER_AUTO ?
            calculate_flower_state(garden, flower) : config->initial_state;
        if (flower->state == FLOWER_WATERED) {
            flower->protected_until = config->protection_interval;
        }
        if (flower->state == FLOWER_DRYED || flower->state == FLOWER_DEAD_OVERWATER) {
            flower->dead = true;
            --garden->alive_count;
        }
    }
    return garden;
}

static FlowerState calculate_flower_state(const Garden *garden, const Flower *flower) {
    if (flower->moisture <= garden->config.dry_threshold) {
        return FLOWER_DRYED;
    }
    if (flower->moisture <= garden->config.wilt_threshold) {
        return FLOWER_WILTING;
    }
    if (garden->current_time < flower->protected_until) {
        return FLOWER_WATERED;
    }
    return FLOWER_NORMAL;
}

const char *flower_state_name(FlowerState state) {
    switch (state) {
    case FLOWER_AUTO: return "AUTO";
    case FLOWER_WATERED: return "WATERED";
    case FLOWER_NORMAL: return "NORMAL";
    case FLOWER_WILTING: return "WILTING";
    case FLOWER_DRYED: return "DRIED";
    case FLOWER_DEAD_OVERWATER: return "DEAD_OVERWATER";
    }
    return "UNKNOWN";
}

void garden_tick(Garden *garden, double dt, GardenNotify notify, void *context) {
    garden->current_time += dt;
    for (int i = 0; i < garden->config.flower_count; ++i) {
        Flower *flower = &garden->flowers[i];
        if (flower->dead) {
            continue;
        }

        if (flower->watering_by == -1) {
            flower->moisture -= flower->dry_rate * dt;
            if (flower->moisture < 0.0) {
                flower->moisture = 0.0;
            }
        } else {
            double added = flower->watering_rate * dt;
            if (added > flower->remaining_water) {
                added = flower->remaining_water;
            }
            flower->moisture += added;
            flower->remaining_water -= added;
            if (flower->remaining_water <= 0.0) {
                int gardener_id = flower->watering_by;
                garden_cancel_watering(flower);
                flower->protected_until = garden->current_time + garden->config.protection_interval;
                ++flower->water_count;
                notify(context, WATERING_FINISHED, i, gardener_id);
            }
        }

        FlowerState previous = flower->state;
        flower->state = calculate_flower_state(garden, flower);
        if (flower->state == FLOWER_DRYED) {
            flower->dead = true;
            --garden->alive_count;
        }
        if (previous != flower->state) {
            notify(context, FLOWER_CHANGED, i, previous);
        }
    }
}

bool garden_is_available(const Garden *garden, const Flower *flower) {
    return flower->state == FLOWER_WILTING && flower->watering_by == -1 &&
        garden->current_time >= flower->protected_until;
}

WateringResult garden_try_watering(Garden *garden, int flower_id, int observed_cycles,
                                 int gardener_id, double speed) {
    Flower *flower = &garden->flowers[flower_id];
    // A delayed request based on an earlier cycle is a conflict, not a second watering.
    if (flower->dead || flower->watering_by != -1 || flower->water_count != observed_cycles) {
        return WATER_RETRY;
    }
    if (garden->current_time < flower->protected_until) {
        flower->state = FLOWER_DEAD_OVERWATER;
        flower->dead = true;
        --garden->alive_count;
        return WATER_OVERFLOW;
    }
    if (!garden_is_available(garden, flower)) {
        return WATER_RETRY;
    }
    flower->watering_by = gardener_id;
    flower->remaining_water = garden->config.water_volume;
    flower->watering_rate = speed;
    return WATER_STARTED;
}

void garden_cancel_watering(Flower *flower) {
    flower->watering_by = -1;
    flower->remaining_water = 0.0;
    flower->watering_rate = 0.0;
}

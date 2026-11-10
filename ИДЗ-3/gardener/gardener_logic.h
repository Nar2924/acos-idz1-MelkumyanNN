#ifndef GARDENER_LOGIC_H
#define GARDENER_LOGIC_H
#include "../common/shared_garden.h"
#include <stdbool.h>

int garden_select_flower(SharedGarden *garden);
bool garden_start_watering(SharedGarden *garden, int gardener_id, int flower_id, double volume);

#endif

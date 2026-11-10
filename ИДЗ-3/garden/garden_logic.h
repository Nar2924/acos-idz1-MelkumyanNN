#ifndef GARDEN_LOGIC_H
#define GARDEN_LOGIC_H
#include "../common/shared_garden.h"
#include <stdbool.h>

void garden_init_data(SharedGarden *garden, const Config *config);
bool garden_tick(SharedGarden *garden, double dt);
void garden_print_statistics(const SharedGarden *garden);

#endif

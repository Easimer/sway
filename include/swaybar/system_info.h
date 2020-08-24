#ifndef _SWAYBAR_SYSTEM_INFO_H
#define _SWAYBAR_SYSTEM_INFO_H

#include "badges.h"

// Returns 0 if the device is charging
int si_get_battery_capacity(int* battery_capacity);
int get_network_status(char* buffer, size_t max, enum badge_rarity_t* rarity);

#endif


#ifndef _SWAYBAR_SYSTEM_INFO_H
#define _SWAYBAR_SYSTEM_INFO_H

#include "badges.h"

struct keyboard_layout_provider_t;

// Returns 0 if the device is charging
int si_get_battery_capacity(int* battery_capacity);
int get_network_status(char* buffer, size_t max,
		enum badge_quality_t* quality, int* vpn_up);

struct keyboard_layout_provider_t* create_keyboard_layout_provider();
void destroy_keyboard_layout_provider(struct keyboard_layout_provider_t *klp);
const char* get_current_keyboard_layout(struct keyboard_layout_provider_t *klp);

#endif


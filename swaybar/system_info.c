#include <stdio.h>
#include "swaybar/system_info.h"

#define AC_ONLINE_PATH "/sys/class/power_supply/AC/online"
#define BATTERY_CAPACITY_PATH "/sys/class/power_supply/BAT%d/capacity"
#define MAX_BATTERY_IDX (2)

// Returns 1 if battery exists and capacity is valid
static int get_battery_capacity_idx(int idx, int* battery_capacity) {
	char path[128];
	size_t fmtres = snprintf(path, 127, BATTERY_CAPACITY_PATH, idx);
	path[fmtres] = 0;
	FILE* f = fopen(path, "r");

	if(f != NULL) {
		char buf[4];
		int res = fread(buf, 1, 3, f);
		buf[3] = 0;
		fclose(f);
		if(res > 0) {
			int cap = 0;

			for(int i = 0; i < res; i++) {
				if(buf[i] < '0' || buf[i] > '9') {
					break;
				}

				cap *= 10;
				cap += buf[i] - '0';
			}

			if(cap >= 0 && cap <= 100) {
				*battery_capacity = cap;
				return 1;
			}
		}
	}

	return 0;
}

static int is_charging() {
	FILE* f = fopen(AC_ONLINE_PATH, "r");
	if(f == NULL) {
		return 0;
	}

	char ch;
	int res = fread(&ch, 1, 1, f);
	fclose(f);
	if(res > 0) {
		return ch == '1';
	} else {
		return 0;
	}
}

int si_get_battery_capacity(int* battery_capacity) {
	if(is_charging()) {
		return 0;
	}

	for(int idx = 0; idx < MAX_BATTERY_IDX; idx++) {
		if(get_battery_capacity_idx(idx, battery_capacity)) {
			return 1;
		}
	}

	return -1;
}

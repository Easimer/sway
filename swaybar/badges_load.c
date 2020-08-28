#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "swaybar/badges.h"
#include "swaybar/badges_internal.h"
#include "log.h"

#define LOAD_THRESHOLD_NOTEWORTHY (0.6)
#define LOAD_THRESHOLD_HIGH (1.0)
#define TEMP_THRESHOLD_WARM (50.0)
#define TEMP_THRESHOLD_HOT (75.0)
#define PATH_THERMAL_TYPE "/sys/class/thermal/thermal_zone%d/type"
#define PATH_THERMAL_TEMP "/sys/class/thermal/thermal_zone%d/temp"
#define group ((struct group_load_t*)user)

struct group_load_t {
	struct badges_t *B;
	struct badge_t *badge;
	struct badge_t *badge_temp;

#define STATE_SIZ (32)
	char state[STATE_SIZ];
#define TEMPERATURE_SIZ (32)
	char temperature[TEMPERATURE_SIZ];
};

static void on_load_update(double load_1min, void *user) {
	if(load_1min > LOAD_THRESHOLD_NOTEWORTHY) {
		snprintf(group->state, STATE_SIZ-1, "LOAD %.2f", load_1min);

		if(group->badge == NULL) {
			group->badge = create_badge(group->B);
			group->badge->text = group->state;
		}

		if(load_1min >= LOAD_THRESHOLD_HIGH) {
			map_badge_quality_to_colors(BADGE_QUALITY_ERROR, group->badge);
		} else {
			map_badge_quality_to_colors(BADGE_QUALITY_NORMAL, group->badge);
		}
		group->badge->anim.should_be_visible = 1;
	} else {
		if(group->badge != NULL) {
			group->badge->anim.should_be_visible = 0;
		}
	}
}

static int query_thermal_zone(int index,
		double *temp,
		int *is_cpu) {
	assert(index >= 0);
	assert(temp != NULL);
	assert(is_cpu != NULL);

	size_t rd;
	FILE *f;
#define PATH_BUFFER_SIZ 128
	char path_buffer[PATH_BUFFER_SIZ];
#define READ_BUFFER_SIZ 64
	char read_buffer[READ_BUFFER_SIZ];

	// Read thermal zone type
	snprintf(path_buffer, PATH_BUFFER_SIZ-1, PATH_THERMAL_TYPE, index);
	path_buffer[PATH_BUFFER_SIZ-1] = 0;

	f = fopen(path_buffer, "r");
	if(f == NULL) {
		goto fail_end;
	}

	rd = fread(read_buffer, 1, READ_BUFFER_SIZ-1, f);
	if(rd == 0) {
		goto fail_file;
	}
	fclose(f);
	read_buffer[rd] = '\0';
	*is_cpu = (strncmp(read_buffer, "x86_pkg_temp", READ_BUFFER_SIZ-1) == 0);

	// Read thermal zone temperature
	snprintf(path_buffer, PATH_BUFFER_SIZ-1, PATH_THERMAL_TEMP, index);
	path_buffer[PATH_BUFFER_SIZ-1] = 0;

	f = fopen(path_buffer, "r");
	if(f == NULL) {
		goto fail_end;
	}

	rd = fread(read_buffer, 1, READ_BUFFER_SIZ-1, f);
	if(rd == 0) {
		goto fail_file;
	}
	fclose(f);
	read_buffer[rd] = '\0';
	rd = sscanf(read_buffer, "%lf", temp);
	if(rd <= 0) {
		goto fail_end;
	}

	return 1;
fail_file:
	fclose(f);
fail_end:
	return 0;
#undef READ_BUFFER_SIZ
#undef PATH_BUFFER_SIZ
}

static int get_system_temperature(double *temp) {
	int ret = 0;
	for(int index = 0;; index++) {
		int is_cpu, rc;
		double buf;

		rc = query_thermal_zone(index, &buf, &is_cpu);

		if(rc == 0) {
			break;
		}

		// temperature is in millidegrees Celsius
		buf /= 1000.0;
		*temp = buf;
		ret = 1;
		if(is_cpu) {
			return 1;
		}
	}

	return ret;
}

static void update_system_temperature(void *user) {
	double temp;
	if(get_system_temperature(&temp)) {
		snprintf(group->temperature, TEMPERATURE_SIZ-1, "CPU %.1f°C", temp);

		if(group->badge_temp == NULL) {
			group->badge_temp = create_badge(group->B);
			group->badge_temp->text = group->temperature;
		}
		map_badge_quality_to_colors(
				temp >= TEMP_THRESHOLD_HOT ?
					BADGE_QUALITY_ERROR : BADGE_QUALITY_NORMAL,
				group->badge_temp);
		if(temp > TEMP_THRESHOLD_WARM) {
			group->badge_temp->anim.should_be_visible = 1;
		} else {
			group->badge_temp->anim.should_be_visible = 0;
		}
	} else {
		if(group->badge_temp != NULL) {
			group->badge_temp->anim.should_be_visible = 0;
		}
	}
}

static void* setup(struct badges_t *B) {
	struct group_load_t *g = malloc(sizeof(struct group_load_t));
	memset(g, 0, sizeof(struct group_load_t));
	g->B = B;
	return g;
}

static void update(struct badges_t *B, void *user, double dt) {
	double load_1min;
	if(getloadavg(&load_1min, 1) > 0) {
		on_load_update(load_1min, user);
	} else {
		sway_log(SWAY_DEBUG, "System load is unknown");
	}

	update_system_temperature(user);
}

static void cleanup(struct badges_t *B, void *user) {
	destroy_badge(B, group->badge_temp);
	destroy_badge(B, group->badge);
	free(group);
}

static struct badge_group_t _group = {
	.setup = setup,
	.update = update,
	.cleanup = cleanup,
};

DEFINE_BADGE_GROUP_REGISTER(load) {
	register_badge_group(B, &_group);
}

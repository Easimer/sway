#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include "swaybar/badges.h"
#include "swaybar/badges_internal.h"
#include "swaybar/system_info.h"
#include "log.h"

#define group ((struct group_battery_t*)user)
#define UPDATE_INTERVAL_SECS (15)

struct group_battery_t {
	struct badge_t *badge;

#define STATE_SIZ (32)
	char state[STATE_SIZ];
	double time_since_last_update;
};

static void update_battery_state(struct group_battery_t *g) {
	int battery_capacity = 0;

	char *buf = g->state;
	struct badge_t *b = g->badge;

	int res = si_get_battery_capacity(&battery_capacity);

	if(res > 0) {
		snprintf(buf, STATE_SIZ-1, "BAT %d%%", battery_capacity);
		if(battery_capacity < 30) {
			map_badge_quality_to_colors(BADGE_QUALITY_ERROR, b);
		} else {
			map_badge_quality_to_colors(BADGE_QUALITY_NORMAL, b);
		}
	} else if(res == 0) {
		snprintf(buf, STATE_SIZ-1, "Charging");
		map_badge_quality_to_colors(BADGE_QUALITY_NORMAL, b);
	} else {
		b->anim.should_be_visible = 0;
		b->text = NULL;
	}
}

static void* setup(struct badges_t *B) {
	struct group_battery_t *g = malloc(sizeof(struct group_battery_t));
	int battery_capacity;
	g->time_since_last_update = UPDATE_INTERVAL_SECS;

	int res = si_get_battery_capacity(&battery_capacity);
	if(res >= 0) {
		g->badge = create_badge(B);
		g->badge->text = g->state;
		map_badge_quality_to_colors(BADGE_QUALITY_NORMAL, g->badge);
		g->badge->anim.should_be_visible = 1;
	} else {
		sway_log(SWAY_INFO, "Power supply badge will not be shown: no battery was found!\n");
		g->badge = NULL;
	}

	return g;
}

static void update(struct badges_t *B, void *user, double dt) {
	if(group->badge == NULL) {
		return;
	}

	group->time_since_last_update += dt;

	if(group->time_since_last_update > UPDATE_INTERVAL_SECS) {
		group->time_since_last_update -= UPDATE_INTERVAL_SECS;

		update_battery_state(group);
	}
}

static void cleanup(struct badges_t *B, void* user) {
	if(group->badge != NULL) {
		destroy_badge(B, group->badge);
	}
	free(group);
}

static struct badge_group_t _group = {
	.setup = setup,
	.update = update,
	.cleanup = cleanup,
};

DEFINE_BADGE_GROUP_REGISTER(battery) {
	register_badge_group(B, &_group);
}

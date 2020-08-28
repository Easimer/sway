#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "swaybar/badges.h"
#include "swaybar/badges_internal.h"
#include "log.h"

#define LOAD_THRESHOLD_NOTEWORTHY (0.6)
#define LOAD_THRESHOLD_HIGH (1.0)
#define group ((struct group_load_t*)user)

struct group_load_t {
	struct badges_t *B;
	struct badge_t *badge;

#define STATE_SIZ (64)
	char state[STATE_SIZ];
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
		group->state[0] = '\0';
		if(group->badge != NULL) {
			group->badge->anim.should_be_visible = 0;
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
}

static void cleanup(struct badges_t *B, void *user) {
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

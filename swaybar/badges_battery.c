#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include "swaybar/badges.h"
#include "swaybar/badges_internal.h"
#include "swaybar/system_info.h"

#define USERLEN (64)
#define GET_USERDATA() (char*)((b)->user)

static void setup(struct badge_t* b) {
	b->user = malloc(USERLEN);

	b->text = GET_USERDATA();
	map_badge_quality_to_colors(BADGE_QUALITY_NORMAL, b);
	b->anim.should_be_visible = 1;
}

static void update(struct badge_t* b) {
	char* buf = GET_USERDATA();
	int battery_capacity = 0;

	int res = si_get_battery_capacity(&battery_capacity);
	if(res > 0) {
		snprintf(buf, USERLEN-1, "BAT %d%%", battery_capacity);
		if(battery_capacity < 30) {
			map_badge_quality_to_colors(BADGE_QUALITY_ERROR, b);
		} else {
			map_badge_quality_to_colors(BADGE_QUALITY_NORMAL, b);
		}
	} else if(res == 0) {
		snprintf(buf, USERLEN-1, "Charging");
		map_badge_quality_to_colors(BADGE_QUALITY_NORMAL, b);
	} else {
		b->anim.should_be_visible = 0;
		if(b->text != NULL) {
			b->text = NULL;
		}
	}
}

static void cleanup(struct badge_t* b) {
	if(b->user != NULL) {
		free(b->user);
	}
}

static struct badge_class_t class = {
	.setup = setup,
	.update = update,
	.cleanup = cleanup,
};

DEFINE_BADGE_CLASS_REGISTER(battery) {
	register_badge_class(B, &class);
}


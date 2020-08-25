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
	enum badge_quality_t quality;
	if(get_network_status(GET_USERDATA(), USERLEN, &quality)) {
		map_badge_quality_to_colors(quality, b);
		b->anim.should_be_visible = 1;
	} else {
		map_badge_quality_to_colors(quality, b);
		b->anim.should_be_visible = 0;
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

DEFINE_BADGE_CLASS_REGISTER(network) {
	register_badge_class(B, &class);
}


#include <stddef.h>
#include <stdlib.h>
#include "swaybar/badges.h"
#include "swaybar/badges_internal.h"
#include "swaybar/system_info.h"

#define USERLEN (64)
#define GET_USERDATA() (struct keyboard_layout_provider_t*)((b)->user)

static void setup(struct badge_t* b) {
	b->user = create_keyboard_layout_provider();
	b->text = get_current_keyboard_layout(GET_USERDATA());
	map_badge_quality_to_colors(BADGE_QUALITY_NORMAL, b);
	b->anim.should_be_visible = 1;
}

static void update(struct badge_t* b) {
	b->text = get_current_keyboard_layout(GET_USERDATA());
}

static void cleanup(struct badge_t* b) {
	if(b->user != NULL) {
		destroy_keyboard_layout_provider(GET_USERDATA());
	}
}

static struct badge_class_t class = {
	.setup = setup,
	.update = update,
	.cleanup = cleanup,
};

DEFINE_BADGE_CLASS_REGISTER(kbd_layout) {
	register_badge_class(B, &class);
}


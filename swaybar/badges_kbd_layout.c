#include <stddef.h>
#include <stdlib.h>
#include "swaybar/badges.h"
#include "swaybar/badges_internal.h"
#include "swaybar/system_info.h"
#include "log.h"

#define group ((struct group_kbd_layout_t*)user)

struct group_kbd_layout_t {
	struct keyboard_layout_provider_t *kbd_layout;
	struct badge_t *badge;

#define STATE_SIZ (64)
	char state[STATE_SIZ];
};

static void* setup(struct badges_t *B) {
	struct group_kbd_layout_t *g = malloc(sizeof(struct group_kbd_layout_t));

	g->kbd_layout = create_keyboard_layout_provider();
	if(g->kbd_layout != NULL) {
		g->badge = create_badge(B);
		if(g->badge != NULL) {
			g->badge->text = get_current_keyboard_layout(g->kbd_layout);
			map_badge_quality_to_colors(BADGE_QUALITY_NORMAL, g->badge);
			g->badge->anim.should_be_visible = 1;
		} else {
			sway_log(SWAY_ERROR, "Couldn't create kbd layout badge!");
		}
	}

	return g;
}

static void update(struct badges_t *B, void *user, double dt) {
	if(group->kbd_layout != NULL) {
		group->badge->text = get_current_keyboard_layout(group->kbd_layout);
	}
}

static void cleanup(struct badges_t *B, void *user) {
	if(group->kbd_layout != NULL) {
		destroy_keyboard_layout_provider(group->kbd_layout);
	}
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

DEFINE_BADGE_GROUP_REGISTER(kbd_layout) {
	register_badge_group(B, &_group);
}

#include <stddef.h>
#include <stdlib.h>
#include <time.h>
#include "swaybar/badges.h"
#include "swaybar/badges_internal.h"

#define group ((struct group_datetime_t*)user)

struct group_datetime_t {
	struct badge_t *badge;

#define STATE_SIZ (64)
	char state[STATE_SIZ];
};

static void* setup(struct badges_t *B) {
	struct group_datetime_t *g = malloc(sizeof(struct group_datetime_t));
	g->badge = create_badge(B);

	g->badge->text = g->state;
	map_badge_quality_to_colors(BADGE_QUALITY_NORMAL, g->badge);
	g->badge->anim.should_be_visible = 1;

	return g;
}

static void update(struct badges_t *B, void *user, double dt) {
	struct tm tm;
	time_t t = time(NULL);
	localtime_r(&t, &tm);
	size_t res = strftime(group->state, STATE_SIZ-1, "%D %l:%M%p", &tm);
	group->state[res] = '\0';
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

DEFINE_BADGE_GROUP_REGISTER(datetime) {
	register_badge_group(B, &_group);
}

#include <stddef.h>
#include <stdlib.h>
#include <time.h>
#include "swaybar/badges.h"
#include "swaybar/badges_internal.h"

#define USERLEN (64)
#define GET_USERDATA() (char*)((b)->user)

static void setup(struct badge_t* b) {
	b->user = malloc(USERLEN);

	b->text = GET_USERDATA();
	map_badge_quality_to_colors(BADGE_QUALITY_NORMAL, b);
	b->anim.should_be_visible = 1;
}

static void update(struct badge_t* b, double dt) {
	char* buf = GET_USERDATA();
	struct tm tm;
	time_t t = time(NULL);
	localtime_r(&t, &tm);
	size_t res = strftime(buf, USERLEN - 1, "%D %l:%M%p", &tm);
	buf[res] = 0;
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

DEFINE_BADGE_CLASS_REGISTER(datetime) {
	register_badge_class(B, &class);
}

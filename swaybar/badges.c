#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "swaybar/badges.h"
#include "swaybar/badges_internal.h"

#define SLIDE_IN_SPEED  (0.75f)
#define SLIDE_OUT_SPEED (1.0f)

#define MAX_BADGE_COUNT (8)

struct badges_t {
	struct timespec last_update;
	int animated;

	struct badge_t badges[MAX_BADGE_COUNT];
};

static struct palette_t badge_quality_palettes[BADGE_QUALITY_MAX] = {
	{ .col_bg = 0x285577FF, .col_border = 0x4C7899FF, .col_text = 0xFFFFFFFF },
	{ .col_bg = 0xA54242FF, .col_border = 0xCC6666FF, .col_text = 0xC5C8C6FF },
	{ .col_bg = 0xDE935FFF, .col_border = 0xF0C674FF, .col_text = 0xFFFFFFFF },
};

static double get_elapsed_time(struct timespec* then, struct timespec* now) {
	time_t delta_sec = now->tv_sec - then->tv_sec;
	long delta_nsec = now->tv_nsec - then->tv_nsec;
	if(delta_nsec < 0) {
		--delta_sec;
		delta_nsec += 1000000000L;
	}

	return (double)delta_sec + (double)(delta_nsec / 1000000000.0);
}

void register_badge_class(struct badges_t *B,
		struct badge_class_t *cls) {
	if(B == NULL) return;
	if(cls == NULL) return;

	// Find free slot
	int index;
	for(index = 0; index < MAX_BADGE_COUNT; index++) {
		if(!B->badges[index].present) {
			break;
		}
	}

	if(index < MAX_BADGE_COUNT) {
		// Found a free slot, initialize it
		struct badge_t* badge = &B->badges[index];
		memset(badge, 0, sizeof(struct badge_t));
		badge->present = 1;
		
		badge->anim.should_be_visible = 1;
		badge->anim.visible_ratio = 0.0f;

		badge->class = *cls;
		badge->user = NULL;

		cls->setup(badge);
	}
}

static int update_badge_animinfo(struct badge_animinfo_t *a, double dt) {
	if(a->should_be_visible && a->visible_ratio < 1) {
		a->visible_ratio += SLIDE_IN_SPEED * dt;
		if(a->visible_ratio > 1) a->visible_ratio = 1;
		return 1;
	}

	if(!a->should_be_visible && a->visible_ratio > 0) {
		a->visible_ratio -= SLIDE_OUT_SPEED * dt;
		if(a->visible_ratio < 0) a->visible_ratio = 0;
		return 1;
	}

	return 0;
}

void map_badge_quality_to_colors(enum badge_quality_t q, struct badge_t *b) {
	b->color = badge_quality_palettes[(unsigned)q];
}

void update_badges(struct badges_t *b) {
	if(b == NULL) return;
	struct timespec then = b->last_update;
	clock_gettime(CLOCK_MONOTONIC, &b->last_update);

	double dt = get_elapsed_time(&then, &b->last_update);

	b->animated = 0;
	for(int i = 0; i < MAX_BADGE_COUNT; i++) {
		struct badge_t *badge = &b->badges[i];
		if(!badge->present) continue;

		badge->class.update(badge, dt);
		if(update_badge_animinfo(&badge->anim, dt)) {
			b->animated = 1;
		}
	}
}

int get_badges_count(struct badges_t *b) {
	if(b == NULL) return 0;

	return MAX_BADGE_COUNT;
}

int get_badge_colors(struct badges_t *b, int index,
		uint32_t *col_bg, uint32_t *col_border, uint32_t *col_text) {
	if(b == NULL) return 0;
	if(index < 0 || index >= MAX_BADGE_COUNT) return 0;
	if(!b->badges[index].present) return 0;

	struct badge_t *badge = &b->badges[index];

	*col_bg = badge->color.col_bg;
	*col_border = badge->color.col_border;
	*col_text = badge->color.col_text;

	return 1;
}

const char* get_badge_text(struct badges_t *b, int index) {
	if(b == NULL) return NULL;
	if(index < 0 || index >= MAX_BADGE_COUNT) return NULL;
	if(!b->badges[index].present) return NULL;

	return b->badges[index].text;
}

double get_badge_x_offset(struct badges_t *b, int index) {
	if(b == NULL) return 0;
	if(index < 0 || index >= MAX_BADGE_COUNT) return 0;
	if(!b->badges[index].present) return 0;

	return 1.0f - b->badges[index].anim.visible_ratio;
}

DECLARE_BADGE_CLASS_REGISTER(datetime);
DECLARE_BADGE_CLASS_REGISTER(battery);
DECLARE_BADGE_CLASS_REGISTER(network);
DECLARE_BADGE_CLASS_REGISTER(kbd_layout);
DECLARE_BADGE_CLASS_REGISTER(notifications);

struct badges_t* create_badges() {
	void* p = malloc(sizeof(struct badges_t));
	struct badges_t* b = (struct badges_t*)p;

	for(int i = 0; i < MAX_BADGE_COUNT; i++) {
		b->badges[i].present = 0;
	}

	CALL_REGISTER_BADGE_CLASS(datetime, b);
	CALL_REGISTER_BADGE_CLASS(battery, b);
	CALL_REGISTER_BADGE_CLASS(network, b);
	CALL_REGISTER_BADGE_CLASS(kbd_layout, b);
	CALL_REGISTER_BADGE_CLASS(notifications, b);

	b->animated = 0;
	clock_gettime(CLOCK_MONOTONIC, &b->last_update);

	return b;
}

void free_badges(struct badges_t *b) {
	if(b != NULL) {
		for(int i = 0; i < MAX_BADGE_COUNT; i++) {
			if(b->badges[i].present) {
				if(b->badges[i].user) {
					free(b->badges[i].user);
				}
			}
		}
		free(b);
	}
}

int should_fast_redraw(struct badges_t *b) {
	if(b == NULL) return 0;
	return b->animated;
}
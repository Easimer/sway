#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "swaybar/badges.h"
#include "swaybar/system_info.h"

#define SLIDE_IN_SPEED  (0.75f)
#define SLIDE_OUT_SPEED (1.0f)

#define MAX_BADGE_COUNT (3)

#define COLOR_BLACK (0x2B303BFF)
#define COLOR_GRAY  (0x65737EFF)
#define COLOR_RED   (0xBF616AFF)
#define COLOR_WHITE (0xC0C5CEFF)
#define COLOR_GREEN (0xA3BE8CFF)

#define COLOR_COMMON    (0xFFFFFFFF)
#define COLOR_UNCOMMON  (0x1EFF00FF)
#define COLOR_RARE      (0x0070DDFF)
#define COLOR_EPIC      (0xA335EEFF)
#define COLOR_LEGENDARY (0xFF8000FF)

struct badge_animinfo_t {
	int should_be_visible;
	double visible_ratio;
};

struct badge_t;

typedef void(*badge_update_t)(struct badge_t*);

struct badge_t {
	int present;
	char const* text;
	uint32_t col_bg, col_border, col_text;
	struct badge_animinfo_t anim;
	void* user;

	badge_update_t update;
};

struct badges_t {
	struct timespec last_update;
	int animated;

	struct badge_t badges[MAX_BADGE_COUNT];
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

static void register_badge(struct badges_t *b, badge_update_t update) {
	if(b == NULL) return;
	if(update == NULL) return;

	int index;
	for(index = 0; index < MAX_BADGE_COUNT; index++) {
		if(!b->badges[index].present) {
			break;
		}
	}

	if(index < MAX_BADGE_COUNT) {
		memset(&b->badges[index], 0, sizeof(struct badge_t));
		b->badges[index].present = 1;
		b->badges[index].anim.should_be_visible = 1;
		b->badges[index].anim.visible_ratio = 0.0f;
		b->badges[index].update = update;
		b->badges[index].user = NULL;

		update(&b->badges[index]);
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
		if(a->visible_ratio > 0) a->visible_ratio = 0;
		return 1;
	}

	return 0;
}

static void update_badge__datetime(struct badge_t *b) {
	if(b->user == NULL) {
		b->user = malloc(64 * sizeof(char));

		b->text = (char*)b->user;
		b->col_bg = COLOR_GRAY;
		b->col_border = COLOR_BLACK;
		b->col_text= COLOR_WHITE;
		b->anim.should_be_visible = 1;
	}
	char* buf = (char*)b->user;
	struct tm tm;
	time_t t = time(NULL);
	localtime_r(&t, &tm);
	size_t res = strftime(buf, 63, "%D %l:%M%p", &tm);
	buf[res] = 0;
}

static uint32_t map_rarity_to_color(enum badge_rarity_t r) {
	switch(r) {
		case BADGE_RARITY_COMMON: return COLOR_COMMON;
		case BADGE_RARITY_UNCOMMON: return COLOR_UNCOMMON;
		case BADGE_RARITY_RARE: return COLOR_RARE;
		case BADGE_RARITY_EPIC: return COLOR_EPIC;
		case BADGE_RARITY_LEGENDARY: return COLOR_LEGENDARY;
		default: return COLOR_WHITE;
	}
}

static void update_badge__battery(struct badge_t *b) {
	if(b->user == NULL) {
		b->user = malloc(64 * sizeof(char));

		b->text = (char*)b->user;
		b->col_bg = COLOR_GRAY;
		b->col_border = COLOR_BLACK;
		b->col_text = COLOR_WHITE;
		b->anim.should_be_visible = 1;
	}

	char* buf = (char*)b->user;
	int battery_capacity = 0;

	int res = si_get_battery_capacity(&battery_capacity);
	if(res > 0) {
		snprintf(buf, 63, "BAT %d%%", battery_capacity);
		if(battery_capacity < 30) {
			b->col_bg = COLOR_WHITE;
			b->col_border = COLOR_RED;
			b->col_text = COLOR_RED;
		} else {
			b->col_bg = COLOR_GRAY;
			b->col_border = COLOR_BLACK;
			b->col_text = COLOR_WHITE;
		}
	} else if(res == 0) {
		snprintf(buf, 63, "Charging");
		b->col_bg = COLOR_GRAY;
		b->col_border = COLOR_BLACK;
		b->col_text = COLOR_WHITE;
	} else {
		b->anim.should_be_visible = 0;
		if(b->user != NULL) {
			free(b->user);
			b->user = NULL;
			b->text = NULL;
		}
	}
}

static void update_badge__network(struct badge_t *b) {
	if(b->user == NULL) {
		b->user = malloc(64 * sizeof(char));

		b->text = (char*)b->user;
		b->col_bg = COLOR_GRAY;
		b->col_border = COLOR_BLACK;
		b->col_text = COLOR_WHITE;
		b->anim.should_be_visible = 0;
	}

	char* buf = (char*)b->user;

	enum badge_rarity_t rarity;
	if(get_network_status(buf, 64, &rarity)) {
		b->col_text = map_rarity_to_color(rarity);
		b->anim.should_be_visible = 1;
	} else {
		b->col_text = map_rarity_to_color(rarity);
		b->anim.should_be_visible = 0;
	}
}

void update_badges(struct badges_t *b) {
	if(b == NULL) return;
	struct timespec then = b->last_update;
	clock_gettime(CLOCK_MONOTONIC, &b->last_update);

	double dt = get_elapsed_time(&then, &b->last_update);

	b->animated = 0;
	for(int i = 0; i < MAX_BADGE_COUNT; i++) {
		if(!b->badges[i].present) continue;

		b->badges[i].update(&b->badges[i]);
		if(update_badge_animinfo(&b->badges[i].anim, dt)) {
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

	*col_bg = b->badges[index].col_bg;
	*col_border = b->badges[index].col_border;
	*col_text = b->badges[index].col_text;

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

struct badges_t* create_badges() {
	void* p = malloc(sizeof(struct badges_t));
	struct badges_t* b = (struct badges_t*)p;

	for(int i = 0; i < MAX_BADGE_COUNT; i++) {
		b->badges[i].present = 0;
	}

	register_badge(b, &update_badge__datetime);
	register_badge(b, &update_badge__battery);
	register_badge(b, &update_badge__network);

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

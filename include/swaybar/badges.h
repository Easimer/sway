#ifndef _SWAYBAR_BADGES_H
#define _SWAYBAR_BADGES_H

#include <stdint.h>

struct badges_t;

struct badges_t* create_badges();
void free_badges(struct badges_t*);

int update_badges(struct badges_t*);

int get_badges_count(struct badges_t*);
int get_badge_colors(struct badges_t*, int index,
		uint32_t *col_bg, uint32_t *col_border, uint32_t *col_text);
const char* get_badge_text(struct badges_t*, int index);
double get_badge_x_offset(struct badges_t*, int index);

enum badge_quality_t {
	BADGE_QUALITY_NORMAL = 0,
	BADGE_QUALITY_ERROR,
	BADGE_QUALITY_GOLD,
	BADGE_QUALITY_MAX
};

#endif

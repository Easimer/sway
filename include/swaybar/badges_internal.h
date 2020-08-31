#ifndef _SWAYBAR_BADGES_INTERNAL_H
#define _SWAYBAR_BADGES_INTERNAL_H

#include "swaybar/badges.h"

struct palette_t {
	uint32_t col_bg;
	uint32_t col_border;
	uint32_t col_text;
};

struct badge_animinfo_t {
	int should_be_visible;

	double t;
	double visible_ratio;
};

struct badge_t {
	int present;

	const char* text;
	struct palette_t color;
	struct badge_animinfo_t anim;

	void* user;
};

typedef void* (*badge_group_setup_t)(struct badges_t *B);
typedef void  (*badge_group_update_t)(struct badges_t *B, void *user, double dt);
typedef void  (*badge_group_cleanup_t)(struct badges_t *B, void *user);

struct badge_group_t {
	badge_group_setup_t setup;
	badge_group_update_t update;
	badge_group_cleanup_t cleanup;
};

void register_badge_group(struct badges_t *B, struct badge_group_t *grp);

struct badge_t* create_badge(struct badges_t *B);
void destroy_badge(struct badges_t *B, struct badge_t *badge);

#define DEFINE_BADGE_GROUP_REGISTER(groupname) \
	void register_badge_group_##groupname(struct badges_t *B)
#define DECLARE_BADGE_GROUP_REGISTER(groupname) \
	extern DEFINE_BADGE_GROUP_REGISTER(groupname)
#define CALL_REGISTER_BADGE_GROUP(groupname, b) register_badge_group_##groupname(b)

void map_badge_quality_to_colors(enum badge_quality_t q, struct badge_t *badge);

#endif

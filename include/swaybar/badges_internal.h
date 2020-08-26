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
	double visible_ratio;
};

struct badge_t;

typedef void(*badge_setup_t)(struct badge_t*);
typedef void(*badge_update_t)(struct badge_t*, double dt);
typedef void(*badge_cleanup_t)(struct badge_t*);

struct badge_class_t {
	badge_setup_t setup;
	badge_update_t update;
	badge_cleanup_t cleanup;
};

struct badge_t {
	int present;

	const char* text;
	struct palette_t color;
	struct badge_animinfo_t anim;

	void* user;
	struct badge_class_t class;
};

void register_badge_class(struct badges_t *B, struct badge_class_t *cls);
void map_badge_quality_to_colors(enum badge_quality_t q, struct badge_t *badge);

#define DEFINE_BADGE_CLASS_REGISTER(classname) \
	void register_badge_class_##classname(struct badges_t *B)
#define DECLARE_BADGE_CLASS_REGISTER(classname) \
	extern DEFINE_BADGE_CLASS_REGISTER(classname)
#define CALL_REGISTER_BADGE_CLASS(classname, b) register_badge_class_##classname(b)


#endif

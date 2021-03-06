#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include "swaybar/badges.h"
#include "swaybar/badges_internal.h"
#include "swaybar/system_info.h"
#include "log.h"

#define group ((struct group_network_t*)user)

struct group_network_t {
	struct badge_t *badge;
	struct badge_t *badge_vpn;

#define STATE_SIZ (64)
	char state[STATE_SIZ];
};

static void activate_badge(struct badges_t *B, struct group_network_t *g, enum badge_quality_t quality) {
	if(g->badge == NULL) {
		g->badge = create_badge(B);
	}
	g->badge->text = g->state;
	map_badge_quality_to_colors(quality, g->badge);
	g->badge->anim.should_be_visible = 1;
}

static void deactivate_badge(struct badges_t *B, struct group_network_t *g) {
	destroy_badge(B, g->badge);
	g->badge = NULL;
}

static void show_vpn_badge(struct badges_t *B, struct group_network_t *g) {
	if(g->badge_vpn != NULL) return;

	g->badge_vpn = create_badge(B);
	g->badge_vpn->text = "VPN up";
	g->badge_vpn->anim.should_be_visible = 1;
	map_badge_quality_to_colors(BADGE_QUALITY_GOLD, g->badge_vpn);
}

static void hide_vpn_badge(struct badges_t *B, struct group_network_t *g) {
	if(g->badge_vpn == NULL) return;
	destroy_badge(B, g->badge_vpn);
	g->badge_vpn = NULL;
}

static void update_network_status(struct badges_t *B, struct group_network_t *g) {
	enum badge_quality_t quality;
	int vpn_up = 0;
	if(get_network_status(g->state, STATE_SIZ, &quality, &vpn_up)) {
		activate_badge(B, g, quality);
	} else {
		deactivate_badge(B, g);
	}

	if(vpn_up) {
		show_vpn_badge(B, g);
	} else {
		hide_vpn_badge(B, g);
	}
}

static void* setup(struct badges_t *B) {
	struct group_network_t *g = malloc(sizeof(struct group_network_t));

	g->badge = NULL;
	g->badge_vpn = NULL;

	return g;
}

static void update(struct badges_t *B, void *user, double dt) {
	update_network_status(B, group);
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

DEFINE_BADGE_GROUP_REGISTER(network) {
	register_badge_group(B, &_group);
}

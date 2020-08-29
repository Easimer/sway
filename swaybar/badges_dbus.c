#include <string.h>
#include <stdlib.h>
#include <systemd/sd-bus.h>
#include "swaybar/badges.h"
#include "swaybar/badges_internal.h"
#include "log.h"

/*
 * External programs can create their own badges through using the
 * /net/easimer/swaybar/badges/BadgeGroup1 object available on the
 * net.easimer.swaybar.Badges service.
 *
 * The badges themselves can be managed using a proxy object created
 * through the aforementioned interface. This object has the type of
 * 'net.easimer.swaybar.badges.Badge1'.
 *
 * The documentation of these interfaces can be found in the
 * 'dbus-1/interfaces/' directory.
 */

#define group ((struct dbus_group_t*)user)
#define this_badge ((struct dbus_badge_t*)user)

struct dbus_badge_t {
	int present;
	struct badges_t *B;
	struct badge_t *badge;
	sd_bus_slot *slot;
#define BUFFER_SIZ (256)
	char buffer[BUFFER_SIZ];
};

struct dbus_group_t {
	struct badges_t *B;
	sd_bus *bus;
	sd_bus_slot *slot;

#define BADGES_MAX_SIZ (8)
	struct dbus_badge_t badges[BADGES_MAX_SIZ];
};

#define BADGE_PATH_FMT "/net/easimer/swaybar/Badges/%d"

static sd_bus_error g_err_enospc =
SD_BUS_ERROR_MAKE_CONST("net.easimer.swaybar.badges.ENOSPC", "Out of space");
static sd_bus_error g_err_einval =
SD_BUS_ERROR_MAKE_CONST("net.easimer.swaybar.badges.EINVAL", "Argument is out of range");
static sd_bus_error g_err_enoent =
SD_BUS_ERROR_MAKE_CONST("net.easimer.swaybar.badges.ENOENT", "No such entity");

static int method_badge_set_visible(
		sd_bus_message *m, void *user, sd_bus_error *ret_err) {
	int visible;
	int r;

	r = sd_bus_message_read(m, "b", &visible);
	if(r < 0) {
		sway_log(SWAY_ERROR, "Failed to parse parameters: %s", strerror(-r));
		return r;
	}

	this_badge->badge->anim.should_be_visible = visible;

	return sd_bus_reply_method_return(m, "");
}

static int method_badge_set_text(
		sd_bus_message *m, void *user, sd_bus_error *ret_err) {
	const char* text;
	int r;

	r = sd_bus_message_read(m, "s", &text);
	if(r < 0) {
		sway_log(SWAY_ERROR, "Failed to parse parameters: %s", strerror(-r));
		return r;
	}

	strncpy(this_badge->buffer, text, BUFFER_SIZ-1);
	this_badge->buffer[BUFFER_SIZ-1] = '\0';

	return sd_bus_reply_method_return(m, "");
}

static int method_badge_set_quality(
		sd_bus_message *m, void *user, sd_bus_error *ret_err) {
	int quality;
	int r;

	r = sd_bus_message_read(m, "i", &quality);
	if(r < 0) {
		sway_log(SWAY_ERROR, "Failed to parse parameters: %s", strerror(-r));
		return r;
	}

	if(BADGE_QUALITY_NORMAL <= quality && quality < BADGE_QUALITY_MAX) {
		map_badge_quality_to_colors((enum badge_quality_t)quality, this_badge->badge);
		return sd_bus_reply_method_return(m, "");
	} else {
		return sd_bus_reply_method_error(m, &g_err_einval);
	}
}

static const sd_bus_vtable vtable_badge[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("SetVisible", "b", "", method_badge_set_visible, 0),
	SD_BUS_METHOD("SetText", "s", "", method_badge_set_text, 0),
	SD_BUS_METHOD("SetQuality", "i", "", method_badge_set_quality, 0),
	SD_BUS_VTABLE_END
};

static int create_dbus_badge_proxy(char* path_buffer, size_t max, void *user) {
	int index, rc;

	for(index = 0; index < BADGES_MAX_SIZ; index++) {
		if(!group->badges[index].present) break;
	}

	if(index == BADGES_MAX_SIZ) return 0;

	snprintf(path_buffer, max-1, BADGE_PATH_FMT, index);
	path_buffer[max-1] = '\0';

	struct dbus_badge_t *badge = &group->badges[index];
	badge->present = 1;
	badge->B = group->B;
	badge->badge = NULL;
	badge->slot = NULL;
	badge->buffer[0] = '\0';

	rc = sd_bus_add_object_vtable(group->bus, &badge->slot,
			path_buffer,
			"net.easimer.swaybar.badges.Badge1",
			vtable_badge, badge);

	if(rc < 0) {
		goto err_end;
	}

	badge->badge = create_badge(group->B);

	if(badge->badge == NULL) {
		goto err_badge;
	}

	badge->badge->text = badge->buffer;

	return 1;

err_badge:
	badge->present = 0;
err_end:
	return 0;
}

static int method_group_create(
		sd_bus_message *m, void *user, sd_bus_error *ret_err) {
	char path_buf[256];
	if(create_dbus_badge_proxy(path_buf, 256, user)) {
		return sd_bus_reply_method_return(m, "o", path_buf);
	} else {
		return sd_bus_reply_method_error(m, &g_err_enospc);
	}
}

static int method_group_destroy(
		sd_bus_message *m, void *user, sd_bus_error *ret_err) {
	const char* object;
	int r;

	r = sd_bus_message_read(m, "o", &object);

	if(r < 0) {
		sway_log(SWAY_ERROR, "Failed to parse parameters: %s", strerror(-r));
		return r;
	}

	int index;
	r = sscanf(object, BADGE_PATH_FMT, &index);
	if(r < 1) {
		sway_log(SWAY_ERROR, "Failed to parse parameters: invalid object path");
		return -1;
	}

	if(index >= BADGES_MAX_SIZ) {
		return sd_bus_reply_method_error(m, &g_err_enoent);
	}

	if(group->badges[index].present) {
		group->badges[index].present = 0;
		destroy_badge(group->B, group->badges[index].badge);
		sd_bus_slot_unref(group->badges[index].slot);
	} else {
		return sd_bus_reply_method_error(m, &g_err_enoent);
	}

	return sd_bus_reply_method_return(m, "");
}

static const sd_bus_vtable vtable_group[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("Create", "", "o", method_group_create, 0),
	SD_BUS_METHOD("Destroy", "o", "", method_group_destroy, 0),
	SD_BUS_VTABLE_END
};

static void* setup(struct badges_t *B) {
	int rc;
	struct dbus_group_t *user = malloc(sizeof(struct dbus_group_t));
	memset(group, 0, sizeof(struct dbus_group_t));
	group->B = B;

	rc = sd_bus_open_user(&group->bus);

	if(rc < 0) {
		sway_log(SWAY_ERROR, "Can't open bus");
		goto err_end;
	}

	rc = sd_bus_add_object_vtable(group->bus, &group->slot,
			"/net/easimer/swaybar/Badges/BadgeGroup1",
			"net.easimer.swaybar.badges.BadgeGroup1",
			vtable_group, group);

	if(rc < 0) {
		sway_log(SWAY_ERROR, "Can't create manager object");
		goto err_bus;
	}

	rc = sd_bus_request_name(group->bus, "net.easimer.swaybar.Badges", 0);

	if(rc < 0) {
		sway_log(SWAY_ERROR, "Can't request service name");
		goto err_slot;
	}

	return group;

err_slot:
	sd_bus_slot_unref(group->slot);
	group->slot = NULL;
err_bus:
	sd_bus_unref(group->bus);
	group->bus = NULL;
err_end:
	return group;
}

static void update(struct badges_t *B, void *user, double dt) {
	if(group == NULL) return;
	int rc;

	do {
		rc = sd_bus_process(group->bus, NULL);
	} while(rc > 0);
}

static void cleanup(struct badges_t *B, void *user) {
	if(group != NULL) {
		if(group->slot != NULL) {
			sd_bus_slot_unref(group->slot);
		}
		if(group->bus != NULL) {
			sd_bus_unref(group->bus);
		}
		free(group);
	}
}

static struct badge_group_t _group = {
	.setup = setup,
	.update = update,
	.cleanup = cleanup,
};

DEFINE_BADGE_GROUP_REGISTER(dbus) {
	register_badge_group(B, &_group);
}

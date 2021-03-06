#include <stddef.h>
#include <stdlib.h>
#include <time.h>
#include <systemd/sd-bus.h>
#include "swaybar/badges.h"
#include "swaybar/badges_internal.h"
#include "log.h"

#define GET_USERDATA() (struct notifications_t*)((b)->user)
#define THIS() ((struct notifications_t*)(user))
#define group ((struct notifications_t*)user)

#define URG_LOW      (0)
#define URG_NORMAL   (1)
#define URG_CRITICAL (2)

#define TIMEOUT_NEVER   (0)
#define TIMEOUT_DEPENDS (-1)

#define CYCLE_INTERVAL (10.0)

struct notification_t {
	uint32_t id;
#define APP_NAME_SIZ (32)
	char app_name[APP_NAME_SIZ];
#define SUMMARY_SIZ (128)
	char summary[SUMMARY_SIZ];
#define BODY_SIZ (128)
	char body[BODY_SIZ];

	double time_remaining;
};

struct notification_list_t {
	struct notification_t data;

	struct notification_list_t *next;
};

static struct notification_list_t* create_notification_list_node() {
	struct notification_list_t* ret = malloc(sizeof(struct notification_list_t));

	memset(&ret->data, 0, sizeof(ret->data));
	ret->next = NULL;

	return ret;
}

static void append_notification_list_node(
		struct notification_list_t **head,
		struct notification_list_t *node) {
	struct notification_list_t *cur = *head;

	if(cur != NULL) {
		// List is not empty
		while(cur->next != NULL) {
			cur = cur->next;
		}

		cur->next = node;
	} else {
		*head = node;
	}
}

static void remove_notification_list_node_at(
		struct notification_list_t **node
	) {
	if(*node != NULL) {
		struct notification_list_t* next;
		next = (*node)->next;
		free(*node);

		*node = next;
	}
}

struct dbus_group_t;

struct notifications_t {
	sd_bus *bus;
	sd_bus_slot *slot;

	struct dbus_group_t *proxy;

	struct badge_t *badge;

#define BADGE_BUFFER_SIZ (512)
	char buffer[BADGE_BUFFER_SIZ];

	// initially 1
	uint32_t next_notification_id;

	struct notification_list_t *list_permanent;
	struct notification_list_t *list_expiring;

	// Current permanent notification being displayed
	struct notification_list_t *permanent_current;
	double cycle_timer;
};

static uint32_t generate_notification_id(struct notifications_t *n) {
	uint32_t ret = n->next_notification_id;

	n->next_notification_id++;

	// spec says a notification id cannot be 0
	if(n->next_notification_id == 0) {
		n->next_notification_id++;
	}

	return ret;
}

static void tick_notifications(struct notifications_t *n, double dt) {
	struct notification_list_t *expiring_top;
	struct notification_t *noti = NULL;

	expiring_top = n->list_expiring;
	if(expiring_top != NULL) {
		noti = &expiring_top->data;
		noti->time_remaining -= dt;
		if(noti->time_remaining <= 0.0) {
			remove_notification_list_node_at(&n->list_expiring);
			noti = NULL;
		}
	}

	//
	expiring_top = n->list_expiring;
	if(expiring_top != NULL) {
		noti = &expiring_top->data;
	} else {
		n->cycle_timer -= dt;
		if(n->cycle_timer <= 0) {
			if(n->permanent_current != NULL) {
				n->permanent_current = n->permanent_current->next;
				if(n->permanent_current == NULL) {
					n->permanent_current = n->list_permanent;
				}
			}

			n->cycle_timer = CYCLE_INTERVAL;
		}

		if(n->permanent_current != NULL) {
			noti = &n->permanent_current->data;
		}
	}

	if(noti != NULL) {
		n->badge->anim.should_be_visible = 1;
		snprintf(n->buffer, BADGE_BUFFER_SIZ-1, "%s - %s: %s",
				noti->app_name, noti->summary, noti->body);
	} else {
		n->badge->anim.should_be_visible = 0;
	}
}

static const char *g_server_name = "swaybar";
static const char *g_server_vendor = "easimer.net";
static const char *g_server_version = SWAY_VERSION;
static const char *g_server_spec_version = "1.2";

static int method_get_capabilities(
		sd_bus_message *m,
		void* user,
		sd_bus_error *ret_err) {
	sway_log(SWAY_DEBUG, "GetCapabilities");
	// NOTE: we're lying here
	return sd_bus_reply_method_return(m, "as", 2, "actions", "body");
}

#define CHECK_SDBUS_RESULT() \
	if(r < 0) { \
		sway_log(SWAY_ERROR, "Failed to parse parameters: %s\n", strerror(-r)); \
		return r; \
	}

// strlcpy implementation that strips HTML tags from the source string
static void strlcpy_strip_html(char *dst, const char *src, size_t dst_siz) {
	int level = 0;
	size_t dst_cursor = 0;
	size_t dst_cursor_max = dst_siz - 1;

	while(dst_cursor < dst_cursor_max && (*src != '\0')) {
		if(*src == '<') {
			level++;
		}
		if(level == 0) {
			dst[dst_cursor] = *src;
			dst_cursor++;
		} else {
			if(*src == '>') {
				level--;
			}
		}
		src++;
	}

	dst[dst_cursor] = '\0';
}

static int method_notify(
		sd_bus_message *m,
		void* user,
		sd_bus_error *ret_err) {
	int r;
	const char *app_name = NULL;
	uint32_t replaces_id;
	const char *app_icon = NULL;
	const char *summary = NULL;
	const char *body = NULL;
	int32_t expire_timeout;

	uint32_t notification_id;

	r = sd_bus_message_read(m, "susss",
			&app_name, &replaces_id, &app_icon, &summary, &body);
	CHECK_SDBUS_RESULT();

	sway_log(SWAY_DEBUG, "Notification summary: '%s'", summary);

	r = sd_bus_message_skip(m, "as");
	CHECK_SDBUS_RESULT();

	r = sd_bus_message_skip(m, "a{sv}");
	CHECK_SDBUS_RESULT();

	r = sd_bus_message_read(m, "i", &expire_timeout);
	CHECK_SDBUS_RESULT();

	notification_id = generate_notification_id(THIS());

	if(expire_timeout == TIMEOUT_DEPENDS) {
		expire_timeout = 1000;
	}

	struct notification_list_t *node = create_notification_list_node();
	struct notification_t *noti = &node->data;
	noti->id = notification_id;
	strlcpy_strip_html(noti->app_name, app_name, APP_NAME_SIZ-1);
	strlcpy_strip_html(noti->summary, summary, SUMMARY_SIZ-1);
	strlcpy_strip_html(noti->body, body, BODY_SIZ-1);

	if(expire_timeout == TIMEOUT_NEVER) {
		append_notification_list_node(&THIS()->list_permanent, node);
		if(THIS()->permanent_current == NULL) {
			THIS()->permanent_current = node;
		}
	} else {
		noti->time_remaining = expire_timeout / 1000.0;
		append_notification_list_node(&THIS()->list_expiring, node);
	}

	return sd_bus_reply_method_return(m, "u", notification_id);
}

static int remove_notification_from_list(
		struct notifications_t *n,
		struct notification_list_t **list,
		uint32_t id) {
	struct notification_list_t *cur;
	struct notification_list_t *prev;

	// Walk the list of permanent notifications
	prev = NULL;
	cur = *list;
	while(cur != NULL) {
		struct notification_list_t *next = cur->next;

		if(cur->data.id == id) {
			if(prev != NULL) {
				prev->next = next;
			} else {
				*list = next;
			}

			if(n->permanent_current == cur) {
				if(next != NULL) {
					n->permanent_current = next;
				} else {
					n->permanent_current = n->list_permanent;
				}
			}

			free(cur);

			return 1;
		}

		prev = cur;
		cur = next;
	}

	return 0;
}

static int remove_notification(
		struct notifications_t *n,
		uint32_t id) {
	if(n != NULL && id != 0) {
		if(remove_notification_from_list(n, &n->list_permanent, id)) {
			return 1;
		}

		if(remove_notification_from_list(n, &n->list_expiring, id)) {
			return 1;
		}
	}

	return 0;
}

static sd_bus_error g_err_notfound =
SD_BUS_ERROR_MAKE_CONST("net.easimer.swaybar.NotFound", "");

static int method_close_notification(
		sd_bus_message *m,
		void* user,
		sd_bus_error *ret_err) {
	int r;
	uint32_t id;

	r = sd_bus_message_read(m, "u", &id);

	if(r < 0) {
		sway_log(SWAY_ERROR, "Failed to parse parameters: %s\n", strerror(-r));
		return r;
	}

	if(remove_notification(THIS(), id)) {
		// TODO: emit signal
		return sd_bus_reply_method_return(m, "");
	} else {
		return sd_bus_reply_method_error(m, &g_err_notfound);
	}
}

static int method_get_server_information(
		sd_bus_message *m,
		void* user,
		sd_bus_error *ret_err) {

	return sd_bus_reply_method_return(m, "ssss",
			g_server_name, g_server_vendor,
			g_server_version, g_server_spec_version);
}

static int method_pop_top(
		sd_bus_message *m,
		void *user,
		sd_bus_error *ret_err) {
	if(THIS()->list_permanent != NULL) {
		if(THIS()->permanent_current == THIS()->list_permanent) {
			THIS()->permanent_current = NULL;
		}
		remove_notification_list_node_at(&(THIS()->list_permanent));
		tick_notifications(THIS(), 0.001);
	}

	return sd_bus_reply_method_return(m, "");
}

static const sd_bus_vtable vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("CloseNotification", "u", "", method_close_notification, 0),
	SD_BUS_METHOD("GetCapabilities", "", "as", method_get_capabilities, 0),
	SD_BUS_METHOD("GetServerInformation", "", "ssss", method_get_server_information, 0),
	SD_BUS_METHOD("Notify", "susssasa{sv}i", "u", method_notify, 0),
	SD_BUS_METHOD("PopTopNotification", "", "", method_pop_top, 0),
	SD_BUS_SIGNAL("ActionInvoked", "us", 0),
	SD_BUS_SIGNAL("NotificationClosed", "uu", 0),
	SD_BUS_VTABLE_END
};

static void* setup(struct badges_t* B) {
	int r;
	struct notifications_t *n = malloc(sizeof(struct notifications_t));

	n->buffer[0] = '\0';
	n->bus = NULL;
	n->slot = NULL;

	n->next_notification_id = 1;
	n->list_permanent = NULL;
	n->list_expiring = NULL;
	n->permanent_current = NULL;
	n->cycle_timer = 0;

	r = sd_bus_open_user(&n->bus);
	if(r < 0) {
		sway_log(SWAY_ERROR, "Failed to connect to the user bus, r=%d", r);
		goto err_end;
	}

	r = sd_bus_add_object_vtable(n->bus, &n->slot,
			"/org/freedesktop/Notifications",
			"org.freedesktop.Notifications",
			vtable,
			n);

	if(r < 0) {
		sway_log(SWAY_ERROR, "Failed to install object: %s", strerror(-r));
		goto err_bus;
	}

	r = sd_bus_add_object_vtable(n->bus, &n->slot,
			"/net/easimer/swaybar/Notifications1",
			"net.easimer.swaybar.Notifications1",
			vtable,
			n);

	if(r < 0) {
		sway_log(SWAY_ERROR, "Failed to add vtable to object: %s", strerror(-r));
		goto err_bus;
	}

	r = sd_bus_request_name(n->bus, "org.freedesktop.Notifications", 0);

	if(r < 0) {
		sway_log(SWAY_ERROR, "Failed to request service name: %s", strerror(-r));
		goto err_slot;
	}

	n->badge = create_badge(B);
	if(n->badge == NULL) {
		goto err_slot;
	}

	map_badge_quality_to_colors(BADGE_QUALITY_NORMAL, n->badge);
	n->badge->anim.should_be_visible = 1;
	n->badge->text = n->buffer;

	return n;

err_slot:
	sd_bus_slot_unref(n->slot);
	n->slot = NULL;
err_bus:
	sd_bus_unref(n->bus);
	n->bus = NULL;
err_end:
	free(n);

	return NULL;
}

static void update(struct badges_t *B, void *user, double dt) {
	int r;

	if(user == NULL) return;

	do {
		// returns 0 if there are no more messages
		// and a negative value on error
		r = sd_bus_process(group->bus, NULL);
	} while(r > 0);

	tick_notifications(group, dt);
}

static void cleanup(struct badges_t* B, void *user) {
	if(group->slot != NULL) {
		sd_bus_slot_unref(group->slot);
	}
	if(group->bus != NULL) {
		sd_bus_unref(group->bus);
	}
	free(group);
}

static struct badge_group_t _group = {
	.setup = setup,
	.update = update,
	.cleanup = cleanup,
};

DEFINE_BADGE_GROUP_REGISTER(notifications) {
	register_badge_group(B, &_group);
}

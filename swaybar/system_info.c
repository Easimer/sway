#include <stdio.h>
#include <string.h>
#include <ifaddrs.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <linux/wireless.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <math.h>
#include <json.h>
#include "swaybar/ipc.h"
#include "swaybar/system_info.h"

#define AC_ONLINE_PATH "/sys/class/power_supply/AC/online"
#define BATTERY_CAPACITY_PATH "/sys/class/power_supply/BAT%d/capacity"
#define MAX_BATTERY_IDX (2)

// Returns 1 if battery exists and capacity is valid
static int get_battery_capacity_idx(int idx, int* battery_capacity) {
	char path[128];
	size_t fmtres = snprintf(path, 127, BATTERY_CAPACITY_PATH, idx);
	path[fmtres] = 0;
	FILE* f = fopen(path, "r");

	if(f != NULL) {
		char buf[4];
		int res = fread(buf, 1, 3, f);
		buf[3] = 0;
		fclose(f);
		if(res > 0) {
			int cap = 0;

			for(int i = 0; i < res; i++) {
				if(buf[i] < '0' || buf[i] > '9') {
					break;
				}

				cap *= 10;
				cap += buf[i] - '0';
			}

			if(cap >= 0 && cap <= 100) {
				*battery_capacity = cap;
				return 1;
			}
		}
	}

	return 0;
}

static int is_charging() {
	FILE* f = fopen(AC_ONLINE_PATH, "r");
	if(f == NULL) {
		return 0;
	}

	char ch;
	int res = fread(&ch, 1, 1, f);
	fclose(f);
	if(res > 0) {
		return ch == '1';
	} else {
		return 0;
	}
}

int si_get_battery_capacity(int* battery_capacity) {
	if(is_charging()) {
		return 0;
	}

	for(int idx = 0; idx < MAX_BATTERY_IDX; idx++) {
		if(get_battery_capacity_idx(idx, battery_capacity)) {
			return 1;
		}
	}

	return -1;
}

struct wireless_info_t {
	char ssid[64];
};

struct wired_info_t {
	int speed;
};

struct interface_info_t {
	int present;
	int is_wireless;
    enum badge_quality_t rarity;
	union {
		struct wireless_info_t wireless;
		struct wired_info_t wired;
	};
};

int get_interface_info(char* ifa_name, struct interface_info_t* interface) {
	int sock = -1;
	struct iwreq pwrq;
	struct ifreq ifr;
	memset(&pwrq, 0, sizeof(pwrq));
	memset(&ifr, 0, sizeof(ifr));
	strncpy(pwrq.ifr_name, ifa_name, IFNAMSIZ);
	strncpy(ifr.ifr_name, ifa_name, IFNAMSIZ);

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock == -1) {
		return 0;
	}

	if(ioctl(sock, SIOCGIFFLAGS, &ifr) == -1) {
		close(sock);
		return 0;
	}

	// Interface must be up, must be running and must not be a loopback
	int if_up = ifr.ifr_flags & IFF_UP;
	int if_loopback = ifr.ifr_flags & IFF_LOOPBACK;
	int if_running = ifr.ifr_flags & IFF_RUNNING;

	if(if_loopback || !if_up  || !if_running) {
		close(sock);
		return 0;
	}

	if(ioctl(sock, SIOCGIWNAME, &pwrq) == -1) {
		// Wireless Extensions is not present on this interface
		// It could be wired

		struct ethtool_cmd ecmd;
		ecmd.cmd = ETHTOOL_GSET;

		ifr.ifr_data = &ecmd;
		if(ioctl(sock, SIOCETHTOOL, &ifr) >= 0) {
			interface->present = 1;
			interface->is_wireless = 0;
			interface->wired.speed = 0;
			interface->rarity = BADGE_QUALITY_NORMAL;
			switch(ethtool_cmd_speed(&ecmd)) {
				case SPEED_10:
					interface->wired.speed = 10;
					break;
				case SPEED_100:
					interface->wired.speed = 100;
					break;
				case SPEED_1000:
					interface->rarity = BADGE_QUALITY_GOLD;
					interface->wired.speed = 1000;
					break;
				case SPEED_2500:
					interface->rarity = BADGE_QUALITY_GOLD;
					interface->wired.speed = 2500;
					break;
				case SPEED_10000:
					interface->rarity = BADGE_QUALITY_GOLD;
					interface->wired.speed = 10000;
					break;
			}
		}

		close(sock);
		return 1;
	} else {
		// Wireless Extensions is present, so it's a wireless interface
		char essid[IW_ESSID_MAX_SIZE];
		pwrq.u.essid.pointer = essid;
		pwrq.u.data.length = IW_ESSID_MAX_SIZE;
		pwrq.u.data.flags = 0;
		memset(essid, 0, IW_ESSID_MAX_SIZE);

		if(ioctl(sock, SIOCGIWESSID, &pwrq) != -1) {
			interface->present = 1;
			interface->is_wireless = 1;
			strncpy(interface->wireless.ssid, essid, 63);
			interface->wireless.ssid[63] = 0;
		}

		if(ioctl(sock, SIOCGIWFREQ, &pwrq) != -1) {
			interface->rarity = BADGE_QUALITY_NORMAL;

			double freq = ((double)pwrq.u.freq.m) * pow(10, pwrq.u.freq.e);

			if(freq / 1000000000 > 2.6) {
				// Connected to a 5GHz AP
				// Grant uncommon rarity
				interface->rarity = BADGE_QUALITY_GOLD;
			}
		}

		close(sock);
		return 0;
	}
}

int get_network_status(char* buffer, size_t max,
		enum badge_quality_t* out_rarity) {
	struct ifaddrs *ifaddr, *ifa;
	struct interface_info_t interface = { 0 };

	if(buffer == NULL || max == 0) {
		return 0;
	}

	if(getifaddrs(&ifaddr) == -1) {
		return 0;
	}

	for(ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		if(ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_PACKET) {
			continue;
		}

		int res = get_interface_info(ifa->ifa_name, &interface);

		if(res == 1) {
			break;
		}
	}

	freeifaddrs(ifaddr);

	if(interface.present) {
		if(interface.is_wireless) {
			snprintf(buffer, max - 1, "%s", interface.wireless.ssid);
		} else {
			if(interface.wired.speed > 0) {
				snprintf(buffer, max - 1, "ETH %d", interface.wired.speed);
			} else {
				snprintf(buffer, max - 1, "ETH");
			}
		}
		buffer[max - 1] = 0;
		*out_rarity = interface.rarity;
		return 1;
	}

	return 0;
}

#pragma pack(push, 1)
struct i3_ipc_header_t {
	char magic[6];
	uint32_t len;
	uint32_t kind;
};
#pragma pack(pop)

struct ipc_session_t {
	int fd;
};

struct keyboard_layout_provider_t {
	struct ipc_session_t session;
#define KLP_LAYOUT_MAX_LEN (64)
	char layout_id[KLP_LAYOUT_MAX_LEN];
};

#define MESSAGE_SUBSCRIBE   (0x00000002)
#define MESSAGE_GET_INPUTS  (0x00000064)
#define EVENT_INPUT         (0x80000015)

static int init_session(struct ipc_session_t *session) {
	struct sockaddr_un addr;
	const char *path = getenv("SWAYSOCK");
	int fd;
	int rc;

	session->fd = -1;

	if(path == NULL) {
		printf("getenv(SWAYSOCK) failed\n");
		return 0;
	}

	fd = socket(AF_UNIX, SOCK_STREAM, 0);

	if(fd == -1) {
		perror("failed to get the keyboard layout: couldn't open new socket");
		return 0;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
	rc = connect(fd, (struct sockaddr*)&addr, sizeof(addr));

	if(rc != 0) {
		perror("failed to get the keyboard layout: couldn't connect()");
		close(fd);
		return 0;
	}

	session->fd = fd;
	return 1;
}

static void teardown_session(struct ipc_session_t *session) {
	if(session->fd >= 0) {
		close(session->fd);
		session->fd = -1;
	}
}

static int ipc_subscribe(struct ipc_session_t *session, const char *name) {
	char buf[128];
	size_t idx = snprintf(buf, 127, "[\"%s\"]", name);
	ssize_t rc;

	struct i3_ipc_header_t hdr;
	hdr.magic[0] = 'i'; hdr.magic[1] = '3'; hdr.magic[2] = '-';
	hdr.magic[3] = 'i'; hdr.magic[4] = 'p'; hdr.magic[5] = 'c';
	hdr.len = idx;
	hdr.kind = MESSAGE_SUBSCRIBE;

	rc  = write(session->fd, &hdr, sizeof(hdr));
	rc += write(session->fd, buf, idx);

	return (rc == (ssize_t)(sizeof(hdr) + idx));
}

static int ipc_empty_command(struct ipc_session_t *session, uint32_t kind) {
	struct i3_ipc_header_t hdr;
	ssize_t rc;
	hdr.magic[0] = 'i'; hdr.magic[1] = '3'; hdr.magic[2] = '-';
	hdr.magic[3] = 'i'; hdr.magic[4] = 'p'; hdr.magic[5] = 'c';
	hdr.len = 0;
	hdr.kind = kind;

	rc = write(session->fd, &hdr, sizeof(hdr));

	return rc == sizeof(hdr);
}

struct keyboard_layout_provider_t* create_keyboard_layout_provider() {
	struct keyboard_layout_provider_t *ret;

	ret = malloc(sizeof(*ret));

	if(ret != NULL) {
		if(!init_session(&ret->session)) {
			printf("Session init failed\n");
			free(ret);
			ret = NULL;
		}

		if(!ipc_subscribe(&ret->session, "input")) {
			printf("IPC subscribe failed\n");
			teardown_session(&ret->session);
			free(ret);
			ret = NULL;
		}

		ret->layout_id[0] = 0;

		ipc_empty_command(&ret->session, MESSAGE_GET_INPUTS);
	}

	return ret;
}

void destroy_keyboard_layout_provider(struct keyboard_layout_provider_t* klp) {
	if(klp != NULL) {
		teardown_session(&klp->session);
		free(klp);
	}
}

static void process_input_object(struct keyboard_layout_provider_t *klp,
		struct json_object *input) {
	struct json_object *xkb_active_layout_name;
	if(!json_object_object_get_ex(input,
				"xkb_active_layout_name", &xkb_active_layout_name)) {
		printf("xkb_active_layout_name null\n");
		return;
	}

	const char* layout = json_object_get_string(xkb_active_layout_name);
	if(layout == NULL) {
		printf("layout null\n");
		return;
	}

	strncpy(klp->layout_id, layout, KLP_LAYOUT_MAX_LEN);
}
static void process_input_event(struct keyboard_layout_provider_t *klp,
		const char* payload) {
	struct json_object *root;
	struct json_object *changed;
	struct json_object *input;

	root = json_tokener_parse(payload);

	if(root != NULL) {
		if(!json_object_object_get_ex(root, "change", &changed)) {
			goto free_root;
		}

		const char* changed_s = json_object_get_string(changed);

		if(changed_s == NULL) {
			goto free_root;
		}

		if(strcmp(changed_s, "xkb_layout") != 0) {
			goto free_root;
		}

		if(!json_object_object_get_ex(root, "input", &input)) {
			goto free_root;
		}

		process_input_object(klp, input);

free_root:
		json_object_put(root);
	}
}

static void process_get_inputs_reply(struct keyboard_layout_provider_t *klp,
		const char* payload) {
	struct json_object *root;

	root = json_tokener_parse(payload);

	if(root != NULL) {
		struct json_object *cur;
		struct json_object *type;
		size_t count = json_object_array_length(root);

		for(size_t i = 0; i < count; i++) {
			cur = json_object_array_get_idx(root, i);
			if(cur == NULL) continue;

			type = json_object_object_get(cur, "type");
			if(type == NULL) continue;

			const char* type_s = json_object_get_string(type);
			if(type_s == NULL) continue;

			if(strcmp(type_s, "keyboard") == 0) {
				process_input_object(klp, cur);
			}
		}
		json_object_put(root);
	}
}

static void process_message(struct keyboard_layout_provider_t *klp,
		struct i3_ipc_header_t* hdr, const char* payload) {
	switch(hdr->kind) {
		case EVENT_INPUT:
		process_input_event(klp, payload);
		break;
		case MESSAGE_GET_INPUTS:
		process_get_inputs_reply(klp, payload);
		break;
		default:
		printf("ipc incoming packet of kind %u len %u ignored\n",
				hdr->kind, hdr->len);
		break;
	}
}

static void read_messages(struct keyboard_layout_provider_t *klp) {
	int bytes_avail;
	int rc;
	ssize_t rd;
	struct i3_ipc_header_t hdr;
	char* payload;
	do {
		bytes_avail = 0;
		if((rc = ioctl(klp->session.fd, FIONREAD, &bytes_avail)) == 0) {
			if(bytes_avail > 0) {
				rd = read(klp->session.fd, &hdr, sizeof(hdr));
				if(rd == sizeof(hdr)) {
					payload = malloc(hdr.len + 1);
					rd = read(klp->session.fd, payload, hdr.len);
					payload[hdr.len] = 0;
					if(rd == hdr.len) {
						process_message(klp, &hdr, payload);
					}
					free(payload);
				}
			}
		} else {
			printf("ioctl failed rc=%d\n", rc);
		}
	} while(bytes_avail > 0);
}

const char* get_current_keyboard_layout(struct keyboard_layout_provider_t* klp) {
	if(klp != NULL) {
		read_messages(klp);
		return klp->layout_id;
	}

	return NULL;
}

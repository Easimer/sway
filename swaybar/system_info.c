#include <stdio.h>
#include <string.h>
#include <ifaddrs.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <linux/wireless.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <math.h>
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
    enum badge_rarity_t rarity;
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

			switch(ethtool_cmd_speed(&ecmd)) {
				case SPEED_10:
					interface->rarity = BADGE_RARITY_COMMON;
					interface->wired.speed = 10;
					break;
				case SPEED_100:
					interface->rarity = BADGE_RARITY_UNCOMMON;
					interface->wired.speed = 100;
					break;
				case SPEED_1000:
					interface->rarity = BADGE_RARITY_RARE;
					interface->wired.speed = 1000;
					break;
				case SPEED_2500:
					interface->rarity = BADGE_RARITY_EPIC;
					interface->wired.speed = 2500;
					break;
				case SPEED_10000:
					interface->rarity = BADGE_RARITY_LEGENDARY;
					interface->wired.speed = 10000;
					break;
				default:
					interface->rarity = BADGE_RARITY_COMMON;
					interface->wired.speed = 0;
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
			interface->rarity = BADGE_RARITY_COMMON;

			double freq = ((double)pwrq.u.freq.m) * pow(10, pwrq.u.freq.e);

			if(freq / 1000000000 > 2.6) {
				// Connected to a 5GHz AP
				// Grant uncommon rarity
				interface->rarity = BADGE_RARITY_UNCOMMON;
			}
		}

		close(sock);
		return 0;
	}
}

int get_network_status(char* buffer, size_t max,
		enum badge_rarity_t* out_rarity) {
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

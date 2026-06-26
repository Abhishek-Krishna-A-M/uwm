#define _GNU_SOURCE
#include "ubar.h"
#include "data.h"
#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/timerfd.h>
#include <sys/sysinfo.h>
#include <pthread.h>
#include <errno.h>
#include <dirent.h>
#include <poll.h>

/* PulseAudio (PipeWire-compatible) for volume monitoring */
#include <pulse/pulseaudio.h>

/* D-Bus for UPower battery monitoring */
#include <dbus/dbus.h>

/* Netlink for network event monitoring */
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>

/* udev for HDMI/LED event monitoring */
#include <libudev.h>

#define NOTIFY_AUDIO   'a'
#define NOTIFY_BATTERY 'b'
#define NOTIFY_NETWORK 'n'
#define NOTIFY_DISPLAY 'd'

static uint64_t prev_cpu_idle = 0, prev_cpu_total = 0;
static int cached_thermal_zone = -1;
static pthread_mutex_t g_data_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile bool g_monitors_running = false;

/* Shared globals for volume (written by PA callback, read by main thread) */
int g_vol_pct = 0;
bool g_muted = false;

/* --- Clock (1s timer, minute-change detection) --- */

bool data_update_clock(State *state) {
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);

	if (!state->time_detailed && tm.tm_min == state->prev_minute)
		return false;

	state->prev_minute = tm.tm_min;

	if (state->time_detailed)
		strftime(state->time_str, sizeof(state->time_str), "%a %b %d, %I:%M:%S %p", &tm);
	else
		strftime(state->time_str, sizeof(state->time_str), "%I:%M %p", &tm);
	return true;
}

/* --- CPU (delta-based, /proc/stat) --- */

bool data_update_cpu(State *state) {
	FILE *f = fopen("/proc/stat", "r");
	if (!f) return false;
	char line[256];
	if (!fgets(line, sizeof(line), f)) { fclose(f); return false; }
	fclose(f);

	unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
	if (sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
		&user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) < 4)
		return false;

	unsigned long long cur_idle = idle + iowait;
	unsigned long long cur_total = cur_idle + user + nice + system + irq + softirq + steal;
	unsigned long long d_total = cur_total - prev_cpu_total;
	unsigned long long d_idle = cur_idle - prev_cpu_idle;
	prev_cpu_total = cur_total;
	prev_cpu_idle = cur_idle;

	int old_pct = state->cpu_pct;
	if (d_total > 0)
		state->cpu_pct = (int)((d_total - d_idle) * 100 / d_total);
	else
		state->cpu_pct = 0;
	return state->cpu_pct != old_pct;
}

/* --- Temperature (cached thermal zone, /sys) --- */

bool data_update_temp(State *state) {
	if (cached_thermal_zone >= 0) {
		char path[64];
		snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/temp",
			cached_thermal_zone);
		FILE *f = fopen(path, "r");
		if (f) {
			int raw;
			if (fscanf(f, "%d", &raw) == 1) {
				int new_temp = (raw / 1000 > 0) ? raw / 1000 : 0;
				fclose(f);
				if (new_temp == state->temp_c) return false;
				state->temp_c = new_temp;
				return true;
			}
			fclose(f);
		}
		cached_thermal_zone = -1;
	}

	char path[64];
	for (int i = 0; i < 6; i++) {
		snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/temp", i);
		FILE *f = fopen(path, "r");
		if (!f) continue;
		int raw;
		if (fscanf(f, "%d", &raw) == 1) {
			state->temp_c = (raw / 1000 > 0) ? raw / 1000 : 0;
			fclose(f);
			cached_thermal_zone = i;
			return true;
		}
		fclose(f);
	}
	return false;
}

/* --- Memory (sysinfo syscall, no /proc parse) --- */

bool data_update_memory(State *state) {
	struct sysinfo si;
	if (sysinfo(&si) < 0) return false;

	unsigned long total_kb = si.totalram * si.mem_unit / 1024;
	unsigned long free_kb = si.freeram * si.mem_unit / 1024;
	unsigned long avail_kb = free_kb + si.bufferram * si.mem_unit / 1024;

	if (total_kb == 0) return false;
	int new_pct = (int)((total_kb - avail_kb) * 100 / total_kb);
	bool changed = (new_pct != state->ram_pct) ||
		(total_kb != state->ram_total_kb) || (avail_kb != state->ram_avail_kb);
	state->ram_pct = new_pct;
	state->ram_total_kb = total_kb;
	state->ram_avail_kb = avail_kb;
	return changed;
}

/* --- Battery hardware read (sysfs, used at init + D-Bus fallback) --- */

bool data_update_battery_hardware(State *state) {
	for (int bat = 0; bat <= 1; bat++) {
		char path[64];
		snprintf(path, sizeof(path), "/sys/class/power_supply/BAT%d/capacity", bat);
		FILE *f = fopen(path, "r");
		if (!f) continue;
		int new_pct = state->bat_pct;
		if (fscanf(f, "%d", &new_pct) != 1) new_pct = 0;
		fclose(f);

		bool new_charging = state->charging;
		snprintf(path, sizeof(path), "/sys/class/power_supply/BAT%d/status", bat);
		f = fopen(path, "r");
		if (f) {
			char status[16];
			if (fgets(status, sizeof(status), f))
				new_charging = (strncmp(status, "Charging", 8) == 0);
			fclose(f);
		}

		bool changed = (new_pct != state->bat_pct || new_charging != state->charging);
		state->bat_pct = new_pct;
		state->charging = new_charging;
		return changed;
	}
	bool changed = (state->bat_pct != 0 || state->charging != false);
	state->bat_pct = 0;
	state->charging = false;
	return changed;
}

/* --- HDMI hardware read (sysfs) --- */

bool data_update_hdmi_hardware(State *state) {
	bool new_hdmi = false;
	char path[128];
	for (int i = 0; i < 8; i++) {
		snprintf(path, sizeof(path), "/sys/class/drm/card%d-HDMI-A-%d/status", i / 4, i % 4 + 1);
		FILE *f = fopen(path, "r");
		if (!f) continue;
		char buf[16];
		if (fgets(buf, sizeof(buf), f) && strncmp(buf, "connected", 9) == 0) {
			new_hdmi = true;
			fclose(f);
			break;
		}
		fclose(f);
	}
	if (new_hdmi == state->hdmi) return false;
	state->hdmi = new_hdmi;
	return true;
}

/* --- Locks hardware read (sysfs) --- */

bool data_update_locks_hardware(State *state) {
	bool new_caps = false;
	bool new_num = false;

	DIR *d = opendir("/sys/class/leds");
	if (!d) return false;

	struct dirent *ent;
	while ((ent = readdir(d)) != NULL) {
		if (ent->d_name[0] == '.') continue;

		char path[512];
		snprintf(path, sizeof(path), "/sys/class/leds/%s/brightness", ent->d_name);

		FILE *f = fopen(path, "r");
		if (!f) continue;

		char buf[8];
		int on = 0;
		if (fgets(buf, sizeof(buf), f))
			on = (buf[0] == '1');
		fclose(f);

		if (strstr(ent->d_name, "::capslock"))
			new_caps = on;
		else if (strstr(ent->d_name, "::numlock"))
			new_num = on;
	}
	closedir(d);

	bool changed = (new_caps != state->caps) || (new_num != state->num);
	state->caps = new_caps;
	state->num = new_num;
	return changed;
}

/* ================================================================
 * PULSEAUDIO / PIPEWIRE VOLUME MONITOR (event-driven, no wpctl)
 * ================================================================ */

struct audio_monitor_state {
	State *app;
	pa_threaded_mainloop *mainloop;
	pa_context *context;
	char default_sink[256];
};

static void audio_sink_info_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
	struct audio_monitor_state *ams = userdata;
	if (eol || !i) return;

	bool changed = false;
	pthread_mutex_lock(&g_data_mutex);
	int new_vol = (int)(pa_cvolume_avg(&i->volume) * 100.0 / PA_VOLUME_NORM + 0.5);
	if (new_vol > 100) new_vol = 100;
	if (new_vol < 0) new_vol = 0;
	bool new_muted = i->mute;
	if (new_vol != g_vol_pct || new_muted != g_muted) {
		g_vol_pct = new_vol;
		g_muted = new_muted;
		changed = true;
	}
	pthread_mutex_unlock(&g_data_mutex);

	if (changed && g_monitors_running)
		write(ams->app->audio_pipe[1], &((char){NOTIFY_AUDIO}), 1);
}

static void audio_subscription_cb(pa_context *c, pa_subscription_event_type_t t,
		uint32_t idx, void *userdata) {
	struct audio_monitor_state *ams = userdata;
	uint32_t facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
	uint32_t type = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;

	if (facility == PA_SUBSCRIPTION_EVENT_SINK &&
	    type == PA_SUBSCRIPTION_EVENT_CHANGE) {
		pa_operation *op = pa_context_get_sink_info_by_index(c, idx, audio_sink_info_cb, ams);
		if (op) pa_operation_unref(op);
	}
}

static void audio_context_state_cb(pa_context *c, void *userdata) {
	struct audio_monitor_state *ams = userdata;
	pa_context_state_t state = pa_context_get_state(c);

	switch (state) {
	case PA_CONTEXT_READY: {
		pa_context_set_subscribe_callback(c, audio_subscription_cb, ams);
		pa_operation *op = pa_context_subscribe(c,
			PA_SUBSCRIPTION_MASK_SINK, NULL, NULL);
		if (op) pa_operation_unref(op);

		/* Read initial volume */
		if (ams->default_sink[0]) {
			op = pa_context_get_sink_info_by_name(c, ams->default_sink,
				audio_sink_info_cb, ams);
			if (op) pa_operation_unref(op);
		}
		break;
	}
	case PA_CONTEXT_FAILED:
	case PA_CONTEXT_TERMINATED:
		LOG("pulseaudio context error/terminated");
		break;
	default:
		break;
	}
}

static void *audio_monitor_thread(void *arg) {
	State *state = arg;
	struct audio_monitor_state ams = {0};
	ams.app = state;

	ams.mainloop = pa_threaded_mainloop_new();
	if (!ams.mainloop) {
		LOG("pa_threaded_mainloop_new failed");
		return NULL;
	}

	pa_mainloop_api *api = pa_threaded_mainloop_get_api(ams.mainloop);
	ams.context = pa_context_new(api, "ubar");
	if (!ams.context) {
		pa_threaded_mainloop_free(ams.mainloop);
		return NULL;
	}

	/* Find default sink from PulseAudio server name property */
	pa_proplist *proplist = pa_proplist_new();
	pa_proplist_sets(proplist, PA_PROP_APPLICATION_NAME, "ubar");
	pa_proplist_sets(proplist, PA_PROP_APPLICATION_ID, "org.ubar");
	pa_context *tmp_ctx = pa_context_new_with_proplist(api, "ubar-init", proplist);
	pa_proplist_free(proplist);
	if (tmp_ctx) {
		/* We can't easily get the default sink name without connecting.
		 * Use the well-known PipeWire default sink name. */
		pa_context_disconnect(tmp_ctx);
		pa_context_unref(tmp_ctx);
	}

	/* Use @DEFAULT_AUDIO_SINK@ via environment or fallback to common names */
	const char *sink_env = getenv("PULSE_SINK");
	if (sink_env)
		snprintf(ams.default_sink, sizeof(ams.default_sink), "%s", sink_env);
	else
		snprintf(ams.default_sink, sizeof(ams.default_sink), "@DEFAULT_AUDIO_SINK@");

	pa_context_set_state_callback(ams.context, audio_context_state_cb, &ams);

	pa_threaded_mainloop_lock(ams.mainloop);
	pa_context_connect(ams.context, NULL, PA_CONTEXT_NOFLAGS, NULL);
	pa_threaded_mainloop_unlock(ams.mainloop);

	pa_threaded_mainloop_start(ams.mainloop);

	while (g_monitors_running) {
		usleep(100000); /* 100ms check interval for shutdown */
	}

	pa_threaded_mainloop_lock(ams.mainloop);
	pa_threaded_mainloop_stop(ams.mainloop);
	pa_context_disconnect(ams.context);
	pa_context_unref(ams.context);
	pa_threaded_mainloop_unlock(ams.mainloop);
	pa_threaded_mainloop_free(ams.mainloop);

	return NULL;
}

/* ================================================================
 * D-BUS / UPOWER BATTERY MONITOR (event-driven)
 * ================================================================ */

static void *battery_monitor_thread(void *arg) {
	State *state = arg;
	DBusError err;
	dbus_error_init(&err);

	DBusConnection *conn = dbus_bus_get_private(DBUS_BUS_SYSTEM, &err);
	if (!conn) {
		LOG("dbus_bus_get_private failed: %s", err.message);
		dbus_error_free(&err);
		return NULL;
	}

	dbus_connection_set_exit_on_disconnect(conn, FALSE);

	/* Listen for UPower PropertiesChanged on battery devices */
	dbus_bus_add_match(conn,
		"type='signal',interface='org.freedesktop.DBus.Properties',"
		"member='PropertiesChanged',path_namespace='/org/freedesktop/UPower/devices'",
		&err);
	if (dbus_error_is_set(&err)) {
		LOG("dbus add_match failed: %s", err.message);
		dbus_error_free(&err);
	}

	while (g_monitors_running) {
		dbus_connection_read_write(conn, 500); /* 500ms timeout */

		DBusMessage *msg;
		while ((msg = dbus_connection_pop_message(conn)) != NULL) {
			if (dbus_message_is_signal(msg,
					"org.freedesktop.DBus.Properties",
					"PropertiesChanged")) {
				/* UPower battery changed — re-read from sysfs for simplicity */
				if (g_monitors_running) {
					write(state->battery_pipe[1], &((char){NOTIFY_BATTERY}), 1);
				}
			}
			dbus_message_unref(msg);
		}
	}

	dbus_connection_close(conn);
	dbus_connection_unref(conn);
	return NULL;
}

/* ================================================================
 * NETLINK NETWORK MONITOR (event-driven, no polling)
 * ================================================================ */

static void *network_monitor_thread(void *arg) {
	State *state = arg;

	int sock = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
	if (sock < 0) {
		LOG("netlink socket failed: %s", strerror(errno));
		return NULL;
	}

	struct sockaddr_nl addr = {
		.nl_family = AF_NETLINK,
		.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR,
		.nl_pid = 0,
	};
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		LOG("netlink bind failed: %s", strerror(errno));
		close(sock);
		return NULL;
	}

	/* Initial state read */
	write(state->network_pipe[1], &((char){NOTIFY_NETWORK}), 1);

	while (g_monitors_running) {
		struct pollfd pfd = { .fd = sock, .events = POLLIN };
		int ret = poll(&pfd, 1, 500);
		if (ret <= 0) continue;

		/* Drain all pending netlink messages */
		char buf[4096];
		ssize_t len;
		while ((len = recv(sock, buf, sizeof(buf), MSG_DONTWAIT)) > 0) {
			/* Any netlink event means something changed */
		}

		if (g_monitors_running)
			write(state->network_pipe[1], &((char){NOTIFY_NETWORK}), 1);
	}

	close(sock);
	return NULL;
}

/* ================================================================
 * UDEV HDMI + LED MONITOR (event-driven, no polling)
 * ================================================================ */

static void *display_monitor_thread(void *arg) {
	State *state = arg;

	struct udev *udev = udev_new();
	if (!udev) {
		LOG("udev_new failed");
		return NULL;
	}

	struct udev_monitor *mon = udev_monitor_new_from_netlink(udev, "udev");
	if (!mon) {
		LOG("udev_monitor_new_from_netlink failed");
		udev_unref(udev);
		return NULL;
	}

	udev_monitor_filter_add_match_subsystem_devtype(mon, "drm", NULL);
	udev_monitor_filter_add_match_subsystem_devtype(mon, "leds", NULL);
	udev_monitor_enable_receiving(mon);

	int udev_fd = udev_monitor_get_fd(mon);

	/* Initial state read */
	write(state->display_pipe[1], &((char){NOTIFY_DISPLAY}), 1);

	while (g_monitors_running) {
		struct pollfd pfd = { .fd = udev_fd, .events = POLLIN };
		int ret = poll(&pfd, 1, 500);
		if (ret <= 0) continue;

		/* Drain all pending udev events */
		struct udev_device *dev;
		while ((dev = udev_monitor_receive_device(mon)) != NULL) {
			udev_device_unref(dev);
		}

		if (g_monitors_running)
			write(state->display_pipe[1], &((char){NOTIFY_DISPLAY}), 1);
	}

	udev_monitor_unref(mon);
	udev_unref(udev);
	return NULL;
}

/* ================================================================
 * MONITOR LIFECYCLE
 * ================================================================ */

void data_init_fast(State *state) {
	data_update_clock(state);
	data_update_cpu(state);
	data_update_temp(state);
	data_update_memory(state);
	data_update_battery_hardware(state);
	data_update_hdmi_hardware(state);
	data_update_locks_hardware(state);
}

void data_start_monitors(State *state) {
	g_monitors_running = true;

	pthread_t t1, t2, t3, t4;
	pthread_create(&t1, NULL, audio_monitor_thread, state);
	pthread_detach(t1);
	pthread_create(&t2, NULL, battery_monitor_thread, state);
	pthread_detach(t2);
	pthread_create(&t3, NULL, network_monitor_thread, state);
	pthread_detach(t3);
	pthread_create(&t4, NULL, display_monitor_thread, state);
	pthread_detach(t4);
}

void data_stop_monitors(State *state) {
	g_monitors_running = false;
	/* Give threads time to exit */
	usleep(200000);
	(void)state;
}

/* ================================================================
 * TIMER DATA (called from main loop on 1s tick)
 * ================================================================ */

bool data_update_all_timer(State *state) {
	return data_update_clock(state);
}

bool data_update_slow_timer(State *state) {
	bool changed = false;
	changed |= data_update_cpu(state);
	changed |= data_update_temp(state);
	changed |= data_update_memory(state);
	return changed;
}

/* ================================================================
 * SYNC (called from main thread after pipe notify)
 * ================================================================ */

void data_sync_to_state(State *state) {
	/* Volume is synced in real-time by the audio monitor callback,
	 * but we re-read from globals for the state struct. */
	pthread_mutex_lock(&g_data_mutex);
	state->vol_pct = g_vol_pct;
	state->muted = g_muted;
	pthread_mutex_unlock(&g_data_mutex);

	/* Battery, HDMI, locks: re-read from sysfs (triggered by events) */
	data_update_battery_hardware(state);
	data_update_hdmi_hardware(state);
	data_update_locks_hardware(state);

	/* Network: re-read from /proc/net/dev and cache iface name */
	/* Network info is maintained by the netlink monitor. We read
	 * the interface state here to update the display. */
	char active_iface[256] = {0};
	bool is_wifi = false;
	DIR *d = opendir("/sys/class/net");
	if (d) {
		struct dirent *ent;
		while ((ent = readdir(d)) != NULL) {
			if (ent->d_name[0] == '.') continue;
			if (strcmp(ent->d_name, "lo") == 0) continue;
			if (strncmp(ent->d_name, "veth", 4) == 0) continue;
			if (strncmp(ent->d_name, "docker", 6) == 0) continue;
			if (strncmp(ent->d_name, "br-", 3) == 0) continue;

			char path[512];
			snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", ent->d_name);
			FILE *f = fopen(path, "r");
			if (!f) continue;
			char st[16] = {0};
			if (fgets(st, sizeof(st), f) && strncmp(st, "up", 2) == 0) {
				snprintf(active_iface, sizeof(active_iface), "%s", ent->d_name);
				is_wifi = (ent->d_name[0] == 'w');
				fclose(f);
				break;
			}
			fclose(f);
		}
		closedir(d);
	}

	bool online = (active_iface[0] != 0);
	pthread_mutex_lock(&g_data_mutex);
	if (!online) {
		if (strcmp(state->net_name, "󰖪 Offline") != 0) {
			snprintf(state->net_name, sizeof(state->net_name), "󰖪 Offline");
			snprintf(state->net_speed, sizeof(state->net_speed), "%s", state->net_name);
		}
	} else {
		/* Get WiFi name via nmcli (only on change, cached) */
		static char cached_iface[256] = {0};
		static char cached_name[256] = {0};
		if (strcmp(cached_iface, active_iface) != 0) {
			snprintf(cached_iface, sizeof(cached_iface), "%s", active_iface);
			cached_name[0] = 0;
			if (is_wifi) {
				char cmd[128];
				snprintf(cmd, sizeof(cmd), "nmcli -t -f GENERAL.CONNECTION device show %s 2>/dev/null", active_iface);
				FILE *nm = popen(cmd, "r");
				if (nm) {
					char line[256];
					while (fgets(line, sizeof(line), nm)) {
						if (strncmp(line, "GENERAL.CONNECTION:", 19) == 0) {
							char *val = line + 19;
							char *nl = strchr(val, '\n');
							if (nl) *nl = '\0';
							if (val[0] && strcmp(val, "--") != 0) {
								val[55] = '\0';
								snprintf(cached_name, sizeof(cached_name), "%s", val);
							}
							break;
						}
					}
					pclose(nm);
				}
			}
			if (!cached_name[0])
				snprintf(cached_name, sizeof(cached_name), "%s", is_wifi ? "WiFi" : "Ethernet");
		}

		const char *net_icon = is_wifi ? "󰖩" : "󰈀";
		snprintf(state->net_name, sizeof(state->net_name), "%s %s", net_icon, cached_name);

		/* Read traffic stats from /proc/net/dev */
		uint64_t rx = 0, tx = 0;
		static uint64_t prev_rx = 0, prev_tx = 0;
		FILE *f = fopen("/proc/net/dev", "r");
		if (f) {
			char line[256];
			while (fgets(line, sizeof(line), f)) {
				if (strstr(line, active_iface)) {
					char iface[32];
					unsigned long long r, t;
					if (sscanf(line, "%31[^:]: %llu %*u %*u %*u %*u %*u %*u %*u %llu",
						iface, &r, &t) >= 3) {
						rx = r;
						tx = t;
					}
					break;
				}
			}
			fclose(f);
		}

		uint64_t drx = rx - prev_rx;
		uint64_t dtx = tx - prev_tx;
		prev_rx = rx;
		prev_tx = tx;

		char rx_str[16], tx_str[16];
		double kb_r = drx / 1024.0;
		if (kb_r < 1000.0) snprintf(rx_str, sizeof(rx_str), "%.0fK", kb_r);
		else snprintf(rx_str, sizeof(rx_str), "%.1fM", kb_r / 1024.0);
		double kb_t = dtx / 1024.0;
		if (kb_t < 1000.0) snprintf(tx_str, sizeof(tx_str), "%.0fK", kb_t);
		else snprintf(tx_str, sizeof(tx_str), "%.1fM", kb_t / 1024.0);

		snprintf(state->net_speed, sizeof(state->net_speed), "%s \u2193%s \u2191%s", net_icon, rx_str, tx_str);
	}
	pthread_mutex_unlock(&g_data_mutex);
}

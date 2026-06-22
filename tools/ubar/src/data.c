#include "ubar.h"
#include "data.h"
#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/timerfd.h>
#include <pthread.h>
#include <errno.h>
#include <dirent.h>

int g_vol_pct = 0;
bool g_muted = false;
char g_net_name[128] = {0};
char g_net_speed[128] = {0};
char g_net_iface[32] = {0};
static bool g_net_online = false;
static bool g_net_is_wifi = false;
static uint64_t prev_rx = 0, prev_tx = 0;
static uint64_t prev_cpu_idle = 0, prev_cpu_total = 0;
static int cached_thermal_zone = -1;  /* -1 = not yet probed */
static int cached_bat = -1;           /* -1 = not yet probed, 0/1 = bat index */
pthread_mutex_t g_data_mutex = PTHREAD_MUTEX_INITIALIZER;

void data_update_clock(State *state) {
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	if (state->time_detailed)
		strftime(state->time_str, sizeof(state->time_str), "%a %b %d, %I:%M:%S %p", &tm);
	else
		strftime(state->time_str, sizeof(state->time_str), "%I:%M %p", &tm);
}

void data_update_cpu(State *state) {
	FILE *f = fopen("/proc/stat", "r");
	if (!f) { state->cpu_pct = 0; return; }
	char line[256];
	if (!fgets(line, sizeof(line), f)) { fclose(f); state->cpu_pct = 0; return; }
	fclose(f);

	unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
	if (sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
		&user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) < 4) {
		state->cpu_pct = 0; return;
	}

	unsigned long long cur_idle = idle + iowait;
	unsigned long long cur_total = cur_idle + user + nice + system + irq + softirq + steal;
	unsigned long long d_total = cur_total - prev_cpu_total;
	unsigned long long d_idle = cur_idle - prev_cpu_idle;
	prev_cpu_total = cur_total;
	prev_cpu_idle = cur_idle;

	if (d_total > 0)
		state->cpu_pct = (int)((d_total - d_idle) * 100 / d_total);
	else
		state->cpu_pct = 0;
}

void data_update_temp(State *state) {
	/* Use cached thermal zone if known */
	if (cached_thermal_zone >= 0) {
		char path[64];
		snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/temp",
			cached_thermal_zone);
		FILE *f = fopen(path, "r");
		if (f) {
			int raw;
			if (fscanf(f, "%d", &raw) == 1) {
				state->temp_c = (raw / 1000 > 0) ? raw / 1000 : 0;
				fclose(f);
				return;
			}
			fclose(f);
		}
		/* Cached zone disappeared, re-probe */
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
			return;
		}
		fclose(f);
	}
	state->temp_c = 0;
}

void data_update_memory(State *state) {
	FILE *f = fopen("/proc/meminfo", "r");
	if (!f) { state->ram_pct = 0; return; }
	char line[128];
	unsigned long total = 0, avail = 0;
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "MemTotal: %lu kB", &total) == 1) continue;
		if (sscanf(line, "MemAvailable: %lu kB", &avail) == 1) continue;
	}
	fclose(f);
	if (total == 0) { state->ram_pct = 0; return; }
	state->ram_pct = (int)((total - avail) * 100 / total);
	state->ram_total_kb = total;
	state->ram_avail_kb = avail;
}

void data_update_battery(State *state) {
	/* Use cached battery index if known */
	if (cached_bat >= 0) {
		char path[64];
		snprintf(path, sizeof(path), "/sys/class/power_supply/BAT%d/capacity", cached_bat);
		FILE *f = fopen(path, "r");
		if (f) {
			if (fscanf(f, "%d", &state->bat_pct) != 1) state->bat_pct = 0;
			fclose(f);

			snprintf(path, sizeof(path), "/sys/class/power_supply/BAT%d/status", cached_bat);
			f = fopen(path, "r");
			if (f) {
				char status[16];
				if (fgets(status, sizeof(status), f))
					state->charging = (strncmp(status, "Charging", 8) == 0);
				fclose(f);
			}
			return;
		}
		/* Cached battery disappeared, re-probe */
		cached_bat = -1;
	}

	/* Try BAT0 first, then BAT1 */
	for (int bat = 0; bat <= 1; bat++) {
		char path[64];
		snprintf(path, sizeof(path), "/sys/class/power_supply/BAT%d/capacity", bat);
		FILE *f = fopen(path, "r");
		if (!f) continue;
		if (fscanf(f, "%d", &state->bat_pct) != 1) state->bat_pct = 0;
		fclose(f);

		snprintf(path, sizeof(path), "/sys/class/power_supply/BAT%d/status", bat);
		f = fopen(path, "r");
		if (f) {
			char status[16];
			if (fgets(status, sizeof(status), f))
				state->charging = (strncmp(status, "Charging", 8) == 0);
			fclose(f);
		}
		cached_bat = bat;
		return;
	}
	/* No battery found */
	state->bat_pct = 0;
	state->charging = false;
}

void data_update_hdmi(State *state) {
	state->hdmi = false;
	char path[128];
	/* Scan only first 2 cards (most systems have 0-1) with 4 ports each */
	for (int i = 0; i < 8; i++) {
		snprintf(path, sizeof(path), "/sys/class/drm/card%d-HDMI-A-%d/status", i / 4, i % 4 + 1);
		FILE *f = fopen(path, "r");
		if (!f) continue;
		char buf[16];
		if (fgets(buf, sizeof(buf), f) && strncmp(buf, "connected", 9) == 0) {
			state->hdmi = true;
			fclose(f);
			return;
		}
		fclose(f);
	}
}

void data_update_locks(State *state) {
	state->caps = false;
	state->num = false;

	DIR *d = opendir("/sys/class/leds");
	if (!d) return;

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
			state->caps = on;
		else if (strstr(ent->d_name, "::numlock"))
			state->num = on;
	}
	closedir(d);
}

/* --- Network: direct sysfs reads, no popen --- */

static void discover_active_iface(char *out_iface, size_t len, bool *out_is_wifi) {
	out_iface[0] = 0;
	*out_is_wifi = false;

	DIR *d = opendir("/sys/class/net");
	if (!d) {
		LOG("opendir /sys/class/net failed: %s", strerror(errno));
		return;
	}
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
		if (fgets(st, sizeof(st), f)) {
			LOG("interface %s operstate: %s", ent->d_name, st);
		}
		if (f && strncmp(st, "up", 2) == 0) {
			snprintf(out_iface, len, "%s", ent->d_name);
			*out_is_wifi = (ent->d_name[0] == 'w');
			fclose(f);
			break;
		}
		if (f)
			fclose(f);
	}
	closedir(d);
}

void data_update_network(void) {
	char active_iface[256] = {0};
	bool is_wifi = false;

	discover_active_iface(active_iface, sizeof(active_iface), &is_wifi);

	g_net_online = (active_iface[0] != 0);
	g_net_is_wifi = is_wifi;

	if (!g_net_online) {
		LOG("no active interface found");
		pthread_mutex_lock(&g_data_mutex);
		snprintf(g_net_name, sizeof(g_net_name), "󰖪 Offline");
		snprintf(g_net_speed, sizeof(g_net_speed), "%s", g_net_name);
		g_net_iface[0] = 0;
		pthread_mutex_unlock(&g_data_mutex);
		return;
	}

	LOG("active iface: %s (wifi=%d)", active_iface, is_wifi);
	pthread_mutex_lock(&g_data_mutex);
	snprintf(g_net_iface, sizeof(g_net_iface), "%s", active_iface);
	pthread_mutex_unlock(&g_data_mutex);

	/* Try to get a human-readable connection name via nmcli */
	char conn_name[256] = {0};
	if (is_wifi) {
		char cmd[128];
		snprintf(cmd, sizeof(cmd), "nmcli -t -f GENERAL.CONNECTION device show %s 2>/dev/null", active_iface);
		FILE *nm = popen(cmd, "r");
		if (nm) {
			char line[256];
			while (fgets(line, sizeof(line), nm)) {
				if (strncmp(line, "GENERAL.CONNECTION:", 19) == 0) {
					char *val = line + 19;
					/* Strip trailing newline */
					char *nl = strchr(val, '\n');
					if (nl) *nl = '\0';
					if (val[0] && strcmp(val, "--") != 0) {
						val[55] = '\0';
						snprintf(conn_name, sizeof(conn_name), "%s", val);
					}
					break;
				}
			}
			pclose(nm);
		}
	}
	if (!conn_name[0]) {
		snprintf(conn_name, sizeof(conn_name), "%s", is_wifi ? "WiFi" : "Ethernet");
	}

	const char *net_icon;
	if (is_wifi) {
		net_icon = "󰖩";
	} else {
		net_icon = "󰈀";
	}
	pthread_mutex_lock(&g_data_mutex);
	snprintf(g_net_name, sizeof(g_net_name), "%s %s", net_icon, conn_name);
	pthread_mutex_unlock(&g_data_mutex);

	uint64_t rx = 0, tx = 0;
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
	{
		double kb = drx / 1024.0;
		if (kb < 1000.0) snprintf(rx_str, sizeof(rx_str), "%.0fK", kb);
		else snprintf(rx_str, sizeof(rx_str), "%.1fM", kb / 1024.0);
	}
	{
		double kb = dtx / 1024.0;
		if (kb < 1000.0) snprintf(tx_str, sizeof(tx_str), "%.0fK", kb);
		else snprintf(tx_str, sizeof(tx_str), "%.1fM", kb / 1024.0);
	}

	pthread_mutex_lock(&g_data_mutex);
	snprintf(g_net_speed, sizeof(g_net_speed), "%s \u2193%s \u2191%s", net_icon, rx_str, tx_str);
	pthread_mutex_unlock(&g_data_mutex);
}

/* --- Volume: runs in background thread to avoid blocking main --- */

bool data_update_volume(void) {
	char buf[64];
	int vol = 0;
	bool muted = false;

	LOG("fetching volume via wpctl...");
	FILE *f = popen("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", "r");
	if (f) {
		if (fgets(buf, sizeof(buf), f)) {
			/* Parse "Volume: 0.75\n" or "Volume: 0.75 [MUTED]\n" */
			char *p = strstr(buf, "Volume:");
			if (p) {
				p += 7;
				while (*p == ' ') p++;
				float v;
				if (sscanf(p, "%f", &v) == 1) {
					vol = (int)(v * 100.0f + 0.5f);
					if (vol > 100) vol = 100;
					if (vol < 0) vol = 0;
				}
			}
			muted = strstr(buf, "[MUTED]") != NULL;
		}
		pclose(f);
		LOG("wpctl returned: vol=%d%% muted=%d", vol, muted);
	} else {
		LOG("wpctl popen FAILED: %s", strerror(errno));
	}

	bool changed;
	pthread_mutex_lock(&g_data_mutex);
	changed = (vol != g_vol_pct) || (muted != g_muted);
	g_vol_pct = vol;
	g_muted = muted;
	pthread_mutex_unlock(&g_data_mutex);
	return changed;
}

/* --- Sync globals to state (called from main thread after pipe notify) --- */

void data_sync_to_state(State *state) {
	pthread_mutex_lock(&g_data_mutex);
	state->vol_pct = g_vol_pct;
	state->muted = g_muted;
	if (strcmp(state->net_name, g_net_name) != 0) {
		LOG("syncing net_name: \"%s\" -> \"%s\"", state->net_name, g_net_name);
		snprintf(state->net_name, sizeof(state->net_name), "%s", g_net_name);
	}
	if (strcmp(state->net_speed, g_net_speed) != 0) {
		LOG("syncing net_speed: \"%s\" -> \"%s\"", state->net_speed, g_net_speed);
		snprintf(state->net_speed, sizeof(state->net_speed), "%s", g_net_speed);
	}
	pthread_mutex_unlock(&g_data_mutex);
}

/* --- Timer data --- */

void data_update_all_timer(State *state) {
	data_update_clock(state);
	data_update_cpu(state);
	data_update_temp(state);
	data_update_memory(state);
	data_update_battery(state);
	data_update_hdmi(state);
	data_update_locks(state);
}

/* --- Fast initial load --- */

void data_init_fast(State *state) {
	data_update_clock(state);
	data_update_cpu(state);
	data_update_temp(state);
	data_update_memory(state);
	data_update_battery(state);
	data_update_hdmi(state);
	data_update_locks(state);
}

/* --- Background monitors --- */

static void *network_monitor(void *arg) {
	State *state = (State *)arg;
	LOG("network monitor thread started");

	data_update_network();
	write(state->notify_fds[1], "n", 1);
	LOG("network monitor: initial fetch done, name=\"%s\"", g_net_name);

	while (state->running) {
		struct timespec ts = { .tv_sec = 2, .tv_nsec = 0 };
		nanosleep(&ts, NULL);
		if (!state->running) break;
		data_update_network();
		write(state->notify_fds[1], "n", 1);
	}
	LOG("network monitor thread exiting");
	return NULL;
}

static void *audio_monitor(void *arg) {
	State *state = (State *)arg;
	LOG("audio monitor thread started");

	data_update_volume();
	write(state->notify_fds[1], "a", 1);
	LOG("audio monitor: initial volume fetch done (vol=%d muted=%d)", g_vol_pct, g_muted);

	while (state->running) {
		struct timespec ts = { .tv_sec = 0, .tv_nsec = 200000000 };
		nanosleep(&ts, NULL);
		if (!state->running) break;
		if (data_update_volume())
			write(state->notify_fds[1], "a", 1);
	}

	LOG("audio monitor thread exiting");
	return NULL;
}

void data_start_monitors(State *state) {
	pthread_t net_thread, audio_thread;
	pthread_create(&net_thread, NULL, network_monitor, state);
	pthread_detach(net_thread);
	pthread_create(&audio_thread, NULL, audio_monitor, state);
	pthread_detach(audio_thread);
}

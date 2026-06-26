#ifndef UBAR_H
#define UBAR_H

#include <stdint.h>
#include <stdbool.h>
#include <wayland-client.h>
#include <cairo.h>
#include <pango/pango.h>
#include "uwm-bar-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define MAX_WORKSPACES 9
#define MAX_ZONES 32
#define MAX_TITLE 256
#define MAX_STR 64
#define BAR_HEIGHT 30

#define WARNING_COLOR   0xffc46464

struct pool_buffer {
	struct wl_buffer *buffer;
	cairo_surface_t *surface;
	cairo_t *cairo;
	uint32_t width, height;
	void *data;
	size_t size;
	bool busy;
};

typedef struct {
	uint32_t id;
	bool active;
	bool occupied;
} Workspace;

typedef struct {
	int x, width;
	enum {
		ZONE_NONE,
		ZONE_WORKSPACE,
		ZONE_RAM,
		ZONE_VOLUME,
		ZONE_NETWORK,
		ZONE_TIME
	} type;
	int data;
} HotZone;

typedef struct {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct zwp_uwm_bar_v1 *bar_manager;
	struct zwp_uwm_workspace_group_v1 *workspace_group;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	int32_t width;
	uint32_t height;

	int timer_fd;
	int clock_timer_fd;
	bool running;
	bool need_redraw;
	bool configured;

	int slow_timer;
	int prev_minute;

	struct wl_callback *frame_callback;
	bool frame_pending;

	struct pool_buffer bufs[3];

	int ws_count;
	bool ws_batch_pending;
	Workspace workspaces[MAX_WORKSPACES];
	char focused_title[MAX_TITLE];

	char time_str[MAX_STR];
	bool time_detailed;
	int cpu_pct;
	int temp_c;
	int ram_pct;
	bool ram_detailed;
	int vol_pct;
	bool muted;
	int bat_pct;
	bool charging;
	char net_name[128];
	char net_speed[128];
	bool net_detailed;
	bool hdmi;
	bool caps;
	bool num;

	char font[64];
	PangoFontDescription *font_desc;
	uint32_t bg_color;
	uint32_t fg_color;
	uint32_t ws_focused_text;
	uint32_t ws_inactive_text;
	uint32_t ws_urgent_text;

	unsigned long ram_total_kb;
	unsigned long ram_avail_kb;

	HotZone zones[MAX_ZONES];
	int zone_count;

	int pointer_x;

	/* Event-driven monitor pipes (each subsystem writes 1 byte on change) */
	int audio_pipe[2];
	int battery_pipe[2];
	int network_pipe[2];
	int display_pipe[2];

	/* Partial damage: previous frame item positions */
	int prev_zones[MAX_ZONES];
	int prev_zone_count;
} State;

uint32_t parse_color(const char *hex);
void cairo_set_source_hex(cairo_t *cr, uint32_t color);
void destroy_buffer(struct pool_buffer *buf);
void set_clock_timer(State *s);

extern const struct wl_callback_listener frame_listener;

#endif

#ifndef ULAUNCH_H
#define ULAUNCH_H

#define _GNU_SOURCE

#include <stdint.h>
#include <stdbool.h>
#include <wayland-client.h>
#include <cairo.h>
#include <pango/pango.h>
#include <xkbcommon/xkbcommon.h>
#include "theme.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define INPUT_BUF_MAX 512
#define ENTRIES_INIT 256
#define VISIBLE_PAD 2

enum mode {
	MODE_DMENU,
	MODE_DRUN,
};

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
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct wl_seat *seat;
	struct wl_keyboard *keyboard;
	struct xkb_context *xkb_ctx;
	struct xkb_keymap *xkb_keymap;
	struct xkb_state *xkb_state;

	struct pool_buffer bufs[3];
	struct wl_callback *frame_callback;
	bool frame_pending;

	int32_t width;
	uint32_t height;
	int32_t output_w;
	uint32_t output_h;
	bool running;
	bool need_redraw;
	bool configured;

	Theme theme;

	enum mode mode;
	char **entries;
	char **exec_cmds;
	int n_entries;
	int cap_entries;
	int *hits;

	int *filtered;
	float *scores;
	int n_filtered;
	int cursor;

	/* Incremental filtering: entries[i] can only match a longer query if it
	 * already matched the previous (shorter) query, so a plain append
	 * re-scores just the surviving candidate set instead of every entry.
	 * Reset to an empty query (backspace / ctrl+u / ctrl+w) forces a full
	 * rescan. The candidate set itself lives in filter.c and is uncapped;
	 * n_cand here is only informational. */
	char prev_input[INPUT_BUF_MAX];
	int prev_input_len;
	int n_cand;

	char input[INPUT_BUF_MAX];
	int input_len;
	int input_cursor;

	int visible_start;

	/* key repeat */
	int timerfd;
	uint32_t repeat_key;
	xkb_keysym_t repeat_sym;
	bool repeat_ctrl;
	char repeat_utf8[8];
	int repeat_utf8_len;
	int repeat_delay_ms;
	int repeat_rate_ms;

	/* cached xkb modifier index for Ctrl — looked up once per keymap
	 * instead of once per key press */
	xkb_mod_index_t ctrl_mod_index;

	/* dmenu incremental */
	char dmenu_buf[4096];
	int dmenu_buf_len;
	bool dmenu_stdin_done;
} State;

uint32_t parse_color(const char *hex);
void cairo_set_hex(cairo_t *cr, uint32_t color);
void destroy_buffer(struct pool_buffer *buf);
/* filter.c owns the match/result arrays; reserve() must be called with the
 * entry count before filter_update(), and again whenever entries grow. */
bool filter_reserve(int need);
void filter_update(void);
void filter_fini(void);

extern State state;
extern const struct wl_registry_listener registry_listener;
extern const struct zwlr_layer_surface_v1_listener layer_surface_listener;
extern const struct wl_callback_listener frame_listener;

#endif

#include "ubar.h"
#include "render.h"
#include "data.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <fcntl.h>
#include <pango/pangocairo.h>

#define ITEM_GAP 12
#define EDGE_PAD 12
#define WS_GAP 14

void destroy_buffer(struct pool_buffer *buf) {
	if (!buf || !buf->buffer) return;
	if (buf->cairo) cairo_destroy(buf->cairo);
	if (buf->surface) cairo_surface_destroy(buf->surface);
	if (buf->buffer) wl_buffer_destroy(buf->buffer);
	if (buf->data) munmap(buf->data, buf->size);
	buf->buffer = NULL;
	buf->surface = NULL;
	buf->cairo = NULL;
	buf->data = NULL;
	buf->size = 0;
	buf->width = 0;
	buf->height = 0;
	buf->busy = false;
}

static void buffer_handle_release(void *data, struct wl_buffer *wl_buffer) {
	struct pool_buffer *buf = (struct pool_buffer *)data;
	buf->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {
	.release = buffer_handle_release,
};

struct pool_buffer *get_next_buffer(State *state, uint32_t width, uint32_t height) {
	struct pool_buffer *buf = NULL;
	for (int i = 0; i < 3; i++) {
		if (!state->bufs[i].busy) { buf = &state->bufs[i]; break; }
	}
	if (!buf) return NULL;

	if (buf->width == width && buf->height == height && buf->buffer)
		return buf;

	destroy_buffer(buf);

	int stride = width * 4;
	int size = stride * height;

	char name[64];
	int fd;
	for (int i = 0; i < 100; i++) {
		snprintf(name, sizeof(name), "/ubar-%d-%d", getpid(), i);
		fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0) {
			shm_unlink(name);
			break;
		}
	}
	if (fd < 0) return NULL;

	if (ftruncate(fd, size) < 0) { close(fd); return NULL; }

	void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) { close(fd); return NULL; }

	struct wl_shm_pool *pool = wl_shm_create_pool(state->shm, fd, size);
	struct wl_buffer *wb = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);

	cairo_surface_t *surf = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32, width, height, stride);
	cairo_t *cr = cairo_create(surf);

	wl_buffer_add_listener(wb, &buffer_listener, buf);

	buf->buffer = wb;
	buf->surface = surf;
	buf->cairo = cr;
	buf->data = data;
	buf->size = size;
	buf->width = width;
	buf->height = height;
	return buf;
}

static void text_extents(cairo_t *cr, PangoFontDescription *desc, const char *text, int *w, int *h) {
	PangoLayout *layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, desc);
	pango_layout_set_text(layout, text, -1);
	pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
	pango_cairo_update_layout(cr, layout);
	int tw, th;
	pango_layout_get_pixel_size(layout, &tw, &th);
	if (w) *w = tw;
	if (h) *h = th;
	g_object_unref(layout);
}

static void draw_text(cairo_t *cr, PangoFontDescription *desc, double x, double bar_h, const char *text, uint32_t color) {
	PangoLayout *layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, desc);
	pango_layout_set_text(layout, text, -1);
	pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
	pango_cairo_update_layout(cr, layout);
	int tw, th;
	pango_layout_get_pixel_size(layout, &tw, &th);
	cairo_set_source_hex(cr, color);
	cairo_move_to(cr, x, (bar_h - th) / 2.0);
	pango_cairo_show_layout(cr, layout);
	g_object_unref(layout);
}

void render_frame(State *state) {
	if (state->width <= 0) return;

	struct pool_buffer *buf = get_next_buffer(state, state->width, state->height);
	if (!buf) return;

	cairo_t *cr = buf->cairo;

	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_restore(cr);

	double w = state->width;
	double h = state->height;
	PangoFontDescription *font_desc = state->font_desc;

	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_hex(cr, state->bg_color);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	int zone_idx = 0;
	state->zone_count = 0;

	// === LEFT: workspace numbers style ===
	int lx = EDGE_PAD;

	for (int i = 0; i < state->ws_count; i++) {
		if (!state->workspaces[i].occupied && !state->workspaces[i].active)
			continue;

		char ws_str[16];
		bool active = state->workspaces[i].active;
		snprintf(ws_str, sizeof(ws_str), "\u300C%d\u300D", state->workspaces[i].id + 1);

		int tw, th;
		text_extents(cr, font_desc, ws_str, &tw, &th);

		uint32_t tc = active ? state->ws_focused_text : state->ws_inactive_text;
		draw_text(cr, font_desc, lx, h, ws_str, tc);

		if (zone_idx < MAX_ZONES) {
			state->zones[zone_idx].x = lx;
			state->zones[zone_idx].width = tw;
			state->zones[zone_idx].type = ZONE_WORKSPACE;
			state->zones[zone_idx].data = state->workspaces[i].id;
			zone_idx++;
		}

		lx += tw + WS_GAP;
	}

	// Separator between workspaces and title
	if (lx > EDGE_PAD) {
		lx += 4;
		cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.15);
		cairo_set_line_width(cr, 1);
		cairo_move_to(cr, lx, 6);
		cairo_line_to(cr, lx, h - 6);
		cairo_stroke(cr);
		lx += 10;
	}

	// === LEFT: window title (Safe Pango Ellipsization) ===
	if (state->focused_title[0]) {
		PangoLayout *layout = pango_cairo_create_layout(cr);
		pango_layout_set_font_description(layout, font_desc);
		pango_layout_set_text(layout, state->focused_title, -1);
		
		// Cap title boundary at 400px wide to avoid right-side collision
		pango_layout_set_width(layout, 400 * PANGO_SCALE);
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
		
		int tw, th;
		pango_layout_get_pixel_size(layout, &tw, &th);
		
		cairo_set_source_hex(cr, state->fg_color);
		cairo_move_to(cr, lx, (h - th) / 2.0);
		pango_cairo_show_layout(cr, layout);
		
		g_object_unref(layout);
		lx += tw;
	} else {
		int tw;
		text_extents(cr, font_desc, "Desktop", &tw, NULL);
		draw_text(cr, font_desc, lx, h, "Desktop", state->ws_inactive_text);
		lx += tw;
	}

	// === RIGHT: status items ===
	char keys_str[64] = {0};
	char mem_str[MAX_STR] = {0};
	char vol_str[MAX_STR] = {0};
	char bat_str[MAX_STR] = {0};
	char net_str[128] = {0};

	if (state->caps)
		strcat(keys_str, "[CAPS] ");
	if (state->num)
		strcat(keys_str, "[NUM] ");

	// System metrics v3 Nerd Font updates
	if (state->ram_detailed) {
		double used_gb = (state->ram_total_kb - state->ram_avail_kb) / (1024.0 * 1024.0);
		snprintf(mem_str, sizeof(mem_str), "\uf2db %d%% \uf2c9 %d\u00b0C 󰘚 %.1fGiB",
			state->cpu_pct, state->temp_c, used_gb);
	} else {
		snprintf(mem_str, sizeof(mem_str), "󰘚 %d%%", state->ram_pct);
	}

	// Volume Icons (FontAwesome Stable)
	const char *vol_icon = state->muted ? " " : " ";
	if (state->muted)
		snprintf(vol_str, sizeof(vol_str), "%s Muted", vol_icon);
	else
		snprintf(vol_str, sizeof(vol_str), "%s %d%%", vol_icon, state->vol_pct);

	// Dynamic Battery Selection logic
	const char *bat_icon = "󰁺"; // Default Empty
	if (state->charging) {
		bat_icon = "󰂄"; // Lightning bolt charging indicator
	} else {
		if (state->bat_pct > 85)       bat_icon = "󰁹"; // Full
		else if (state->bat_pct > 60)  bat_icon = "󰂀"; // 3/4
		else if (state->bat_pct > 35)  bat_icon = "󰁾"; // 1/2
		else if (state->bat_pct > 15)  bat_icon = "󰁻"; // 1/4
	}
	snprintf(bat_str, sizeof(bat_str), "%s %d%%", bat_icon, state->bat_pct);

	snprintf(net_str, sizeof(net_str), "%s", state->net_detailed ? state->net_speed : state->net_name);

	// Draw right-aligned items (right to left): time is first = rightmost
	struct { const char *text; int type; uint32_t color; } items[] = {
		{ state->time_str, ZONE_TIME,    state->fg_color },
		{ net_str,         ZONE_NETWORK, state->fg_color },
		{ bat_str,         ZONE_NONE,    state->bat_pct > 0 && state->bat_pct < 20 && !state->charging ? WARNING_COLOR : state->fg_color },
		{ vol_str,         ZONE_VOLUME,  state->fg_color },
		{ mem_str,         ZONE_RAM,     state->ram_pct > 85 ? WARNING_COLOR : state->fg_color },
		{ keys_str,        ZONE_NONE,    WARNING_COLOR },
	};
	int item_count = sizeof(items) / sizeof(items[0]);

	int rx = w - EDGE_PAD;
	for (int i = 0; i < item_count; i++) {
		if (!items[i].text[0]) continue;
		int tw;
		text_extents(cr, font_desc, items[i].text, &tw, NULL);
		rx -= tw;

		if (zone_idx < MAX_ZONES && items[i].type != ZONE_NONE) {
			state->zones[zone_idx].x = rx;
			state->zones[zone_idx].width = tw;
			state->zones[zone_idx].type = items[i].type;
			state->zones[zone_idx].data = 0;
			zone_idx++;
		}

		draw_text(cr, font_desc, rx, h, items[i].text, items[i].color);
		rx -= ITEM_GAP;
	}

	state->zone_count = zone_idx;
	buf->busy = true;

	if (state->frame_callback) {
		wl_callback_destroy(state->frame_callback);
		state->frame_callback = NULL;
	}
	state->frame_callback = wl_surface_frame(state->surface);
	wl_callback_add_listener(state->frame_callback, &frame_listener, state);
	state->frame_pending = true;

	wl_surface_attach(state->surface, buf->buffer, 0, 0);
	wl_surface_damage(state->surface, 0, 0, state->width, state->height);
	wl_surface_commit(state->surface);
	wl_display_flush(state->display);
}

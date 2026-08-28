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
#define BLOCK_GAP 20
#define UNDERLINE_SIZE 3

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

static void text_extents(PangoLayout *layout, const char *text, int *w, int *h) {
	pango_layout_set_text(layout, text, -1);
	pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
	int tw, th;
	pango_layout_get_pixel_size(layout, &tw, &th);
	if (w) *w = tw;
	if (h) *h = th;
}

static void draw_text(PangoLayout *layout, cairo_t *cr, double x, double bar_h, const char *text, uint32_t color) {
	pango_layout_set_text(layout, text, -1);
	pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
	int tw, th;
	pango_layout_get_pixel_size(layout, &tw, &th);
	cairo_set_source_hex(cr, color);
	cairo_move_to(cr, x, (bar_h - th) / 2.0);
	pango_cairo_show_layout(cr, layout);
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

	PangoLayout *layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, font_desc);

	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_hex(cr, state->bg_color);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	int zone_idx = 0;
	state->zone_count = 0;

	/* ---- Prepare right-side strings (for measuring right_w) ---- */
	char caps_str[16] = {0};
	char num_str[16] = {0};
	char hdmi_str[16] = {0};
	char mem_str[MAX_STR] = {0};
	char vol_str[MAX_STR] = {0};
	char bat_str[MAX_STR] = {0};
	char net_str[128] = {0};

	if (state->caps)
		snprintf(caps_str, sizeof(caps_str), "[CAPS]");
	if (state->num)
		snprintf(num_str, sizeof(num_str), "[NUM]");
	if (state->hdmi)
		snprintf(hdmi_str, sizeof(hdmi_str), "[HDMI]");

	if (state->ram_detailed) {
		double used_gb = (state->ram_total_kb - state->ram_avail_kb) / (1024.0 * 1024.0);
		snprintf(mem_str, sizeof(mem_str), "\uf2db %d%% \uf2c9 %d\u00b0C 󰘚 %.1fGiB",
			state->cpu_pct, state->temp_c, used_gb);
	} else {
		snprintf(mem_str, sizeof(mem_str), "󰘚 %d%%", state->ram_pct);
	}

	const char *vol_icon = state->muted ? " " : " ";
	if (state->muted)
		snprintf(vol_str, sizeof(vol_str), "%s Muted", vol_icon);
	else
		snprintf(vol_str, sizeof(vol_str), "%s %d%%", vol_icon, state->vol_pct);

	const char *bat_icon = "󰁺";
	if (state->charging) {
		bat_icon = "󰂄";
	} else {
		if (state->bat_pct > 85)       bat_icon = "󰁹";
		else if (state->bat_pct > 60)  bat_icon = "󰂀";
		else if (state->bat_pct > 35)  bat_icon = "󰁾";
		else if (state->bat_pct > 15)  bat_icon = "󰁻";
	}
	snprintf(bat_str, sizeof(bat_str), "%s %d%%", bat_icon, state->bat_pct);

	snprintf(net_str, sizeof(net_str), "%s", state->net_detailed ? state->net_speed : state->net_name);

	/* Right block items – time is NOT here, it is center */
	struct { const char *text; int type; uint32_t color; } right_items[] = {
		{ net_str,  ZONE_NETWORK, state->fg_color },
		{ bat_str,  ZONE_NONE,    state->bat_pct > 0 && state->bat_pct < 20 && !state->charging ? WARNING_COLOR : state->fg_color },
		{ vol_str,  ZONE_VOLUME,  state->fg_color },
		{ mem_str,  ZONE_RAM,     state->ram_pct > 85 ? WARNING_COLOR : state->fg_color },
		{ hdmi_str, ZONE_NONE,    state->fg_color },
		{ caps_str, ZONE_CAPS,    WARNING_COLOR },
		{ num_str,  ZONE_NUM,     WARNING_COLOR },
	};
	int right_count = sizeof(right_items) / sizeof(right_items[0]);

	/* ---- Measure left_w, center_w, right_w ---- */
	int left_w = 0;
	/* workspaces */
	for (int i = 0; i < state->ws_count; i++) {
		if (!state->workspaces[i].occupied && !state->workspaces[i].active)
			continue;
		char ws_str[16];
		snprintf(ws_str, sizeof(ws_str), "%d", state->workspaces[i].id + 1);
		int tw;
		text_extents(layout, ws_str, &tw, NULL);
		left_w += tw + WS_GAP;
	}
	bool has_ws = left_w > 0;
	if (has_ws) {
		/* separator: original adds 4 + line + 10 =14 */
		left_w += 14;
	}
	/* title – must mirror drawing logic with ellipsize 400 */
	int title_tw = 0;
	if (state->focused_title[0]) {
		pango_layout_set_text(layout, state->focused_title, -1);
		pango_layout_set_width(layout, 400 * PANGO_SCALE);
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
		int tw, th;
		pango_layout_get_pixel_size(layout, &tw, &th);
		title_tw = tw;
		pango_layout_set_width(layout, -1);
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
	} else {
		text_extents(layout, "Desktop", &title_tw, NULL);
	}
	left_w += title_tw;

	int center_w = 0;
	if (state->time_str[0]) {
		text_extents(layout, state->time_str, &center_w, NULL);
	}

	int right_w = 0;
	int visible_right = 0;
	for (int i = 0; i < right_count; i++) {
		if (!right_items[i].text[0]) continue;
		int tw;
		text_extents(layout, right_items[i].text, &tw, NULL);
		if (visible_right > 0) right_w += ITEM_GAP;
		right_w += tw;
		visible_right++;
	}

	/* ---- Polybar fixed-center geometry (renderer.cpp:387) ---- */
	double gap = BLOCK_GAP;
	double min_pos = EDGE_PAD + left_w + (left_w > 0 && center_w > 0 ? gap : 0);
	double max_pos = w - EDGE_PAD - right_w - (right_w > 0 && center_w > 0 ? gap : 0);
	/* max_pos is the x where center may END (right_start - gap) */
	double base = w / 2.0;
	double base_clamped = fmin(base, max_pos - center_w / 2.0);
	double cx = base_clamped - center_w / 2.0;
	if (center_w > 0) {
		cx = fmax(cx, min_pos);
		/* if left forces cx beyond max_pos, left has priority – keep cx at min_pos
		 * (center will overlap right, right will be pushed/clipped – same as polybar) */
	}
	/* right_x: left edge of right block, keep gap from center if both visible */
	double right_x = w - EDGE_PAD - right_w;
	if (center_w > 0 && right_w > 0) {
		double needed = cx + center_w + gap;
		if (right_x < needed) right_x = needed; /* will extend beyond screen – clipped */
	}
	int rx_edge = (int)(right_x + right_w); /* right edge for loop (w-EDGE_PAD normally) */
	if (right_w == 0) rx_edge = (int)(w - EDGE_PAD);

	/* ---- Draw left block with underline ---- */
	int lx = EDGE_PAD;
	for (int i = 0; i < state->ws_count; i++) {
		if (!state->workspaces[i].occupied && !state->workspaces[i].active)
			continue;

		char ws_str[16];
		bool active = state->workspaces[i].active;
		snprintf(ws_str, sizeof(ws_str), "%d", state->workspaces[i].id + 1);

		int tw, th;
		text_extents(layout, ws_str, &tw, &th);

		uint32_t tc = active ? state->ws_focused_text : state->ws_inactive_text;
		draw_text(layout, cr, lx, h, ws_str, tc);

		/* underline for focused workspace – polybar line-size 3 line-color primary */
		if (active) {
			cairo_set_source_hex(cr, state->ws_focused_text);
			cairo_rectangle(cr, lx, h - UNDERLINE_SIZE, tw, UNDERLINE_SIZE);
			cairo_fill(cr);
		}

		if (zone_idx < MAX_ZONES) {
			state->zones[zone_idx].x = lx;
			state->zones[zone_idx].width = tw;
			state->zones[zone_idx].type = ZONE_WORKSPACE;
			state->zones[zone_idx].data = state->workspaces[i].id;
			zone_idx++;
		}

		lx += tw + WS_GAP;
	}

	if (has_ws) {
		lx += 4;
		cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.15);
		cairo_set_line_width(cr, 1);
		cairo_move_to(cr, lx, 6);
		cairo_line_to(cr, lx, h - 6);
		cairo_stroke(cr);
		lx += 10;
	}

	if (state->focused_title[0]) {
		pango_layout_set_text(layout, state->focused_title, -1);
		pango_layout_set_width(layout, 400 * PANGO_SCALE);
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

		int tw, th;
		pango_layout_get_pixel_size(layout, &tw, &th);

		cairo_set_source_hex(cr, state->fg_color);
		cairo_move_to(cr, lx, (h - th) / 2.0);
		pango_cairo_show_layout(cr, layout);

		pango_layout_set_width(layout, -1);
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
		lx += tw;
	} else {
		int tw;
		text_extents(layout, "Desktop", &tw, NULL);
		draw_text(layout, cr, lx, h, "Desktop", state->ws_inactive_text);
		lx += tw;
	}

	/* ---- Draw center time (fixed-center) ---- */
	if (center_w > 0) {
		if (zone_idx < MAX_ZONES) {
			state->zones[zone_idx].x = (int)cx;
			state->zones[zone_idx].width = center_w;
			state->zones[zone_idx].type = ZONE_TIME;
			state->zones[zone_idx].data = 0;
			zone_idx++;
		}
		draw_text(layout, cr, cx, h, state->time_str, state->fg_color);
	}

	/* ---- Draw right block (right-to-left, preserves original visual order) ---- */
	int rx = rx_edge;
	for (int i = 0; i < right_count; i++) {
		if (!right_items[i].text[0]) continue;
		int tw;
		text_extents(layout, right_items[i].text, &tw, NULL);
		rx -= tw;

		if (zone_idx < MAX_ZONES && right_items[i].type != ZONE_NONE) {
			state->zones[zone_idx].x = rx;
			state->zones[zone_idx].width = tw;
			state->zones[zone_idx].type = right_items[i].type;
			state->zones[zone_idx].data = 0;
			zone_idx++;
		}

		draw_text(layout, cr, rx, h, right_items[i].text, right_items[i].color);
		rx -= ITEM_GAP;
	}

	g_object_unref(layout);

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

	/* opaque region for compositor fast-path (bg is opaque) */
	{
		static struct wl_region *opaque = NULL;
		static uint32_t last_w = 0, last_h = 0;
		if (!opaque || last_w != (uint32_t)state->width || last_h != (uint32_t)state->height) {
			if (opaque) wl_region_destroy(opaque);
			opaque = wl_compositor_create_region(state->compositor);
			wl_region_add(opaque, 0, 0, state->width, state->height);
			wl_surface_set_opaque_region(state->surface, opaque);
			last_w = state->width; last_h = state->height;
		}
	}

	wl_surface_damage(state->surface, 0, 0, state->width, state->height);

	wl_surface_commit(state->surface);
	/* flush coalesced in main loop; keep here for immediate but main loop also flushes */
}

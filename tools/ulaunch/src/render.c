#include "ulaunch.h"
#include "render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <math.h>
#include <pango/pangocairo.h>

#define SEP_HEIGHT 1

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

static struct pool_buffer *get_next_buffer(uint32_t width, uint32_t height) {
	struct pool_buffer *buf = NULL;
	for (int i = 0; i < 3; i++) {
		if (!state.bufs[i].busy) { buf = &state.bufs[i]; break; }
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
		snprintf(name, sizeof(name), "/ulaunch-%d-%d", getpid(), i);
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

	struct wl_shm_pool *pool = wl_shm_create_pool(state.shm, fd, size);
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

void render_frame(void) {
	if (state.width <= 0 || !state.configured) return;

	int o_w = state.output_w;
	int o_h = state.output_h;
	int pad = state.theme.padding;
	int bw = state.theme.border_width;

	struct pool_buffer *buf = get_next_buffer(o_w, o_h);
	if (!buf) return;

	cairo_t *cr = buf->cairo;

	/* compute heights from font metrics */
	PangoLayout *layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, state.theme.font_desc);
	int ref_w, ref_h;
	pango_layout_set_text(layout, "X", -1);
	pango_layout_get_pixel_size(layout, &ref_w, &ref_h);
	int item_h = ref_h + 8;
	int prompt_h = ref_h + 12;

	int visible = state.theme.max_items;
	if (visible > state.n_filtered)
		visible = state.n_filtered;

	if (state.cursor < state.visible_start)
		state.visible_start = state.cursor;
	if (state.cursor >= state.visible_start + visible)
		state.visible_start = state.cursor - visible + 1;

	int list_h = visible * item_h;
	int box_w = o_w * state.theme.width_pct / 100;
	if (box_w < 200) box_w = 200;
	int box_h = prompt_h + SEP_HEIGHT + list_h + pad * 2 + bw * 2;
	int bx = (o_w - box_w) / 2;
	int by = (o_h - box_h) / 2;
	if (by < 0) by = 0;

	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_restore(cr);

	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	/* border */
	cairo_set_hex(cr, state.theme.border_color);
	cairo_rectangle(cr, bx, by, box_w, box_h);
	cairo_fill(cr);

	/* background */
	cairo_set_hex(cr, state.theme.bg);
	cairo_rectangle(cr, bx + bw, by + bw, box_w - bw * 2, box_h - bw * 2);
	cairo_fill(cr);

	int content_w = box_w - bw * 2 - pad * 2;
	int x = bx + bw + pad;
	int y = by + bw + pad;

	/* prompt */
	pango_layout_set_text(layout, state.theme.prompt, -1);
	int pw, ph;
	pango_layout_get_pixel_size(layout, &pw, &ph);

	cairo_set_hex(cr, state.theme.prompt_color);
	cairo_move_to(cr, x, y + (prompt_h - ph) / 2.0);
	pango_cairo_show_layout(cr, layout);

	if (state.input_len > 0) {
		pango_layout_set_text(layout, state.input, -1);
		pango_layout_set_width(layout, (content_w - pw) * PANGO_SCALE);
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
		int iw, ih;
		pango_layout_get_pixel_size(layout, &iw, &ih);
		cairo_set_hex(cr, state.theme.fg);
		cairo_move_to(cr, x + pw, y + (prompt_h - ih) / 2.0);
		pango_cairo_show_layout(cr, layout);
		pango_layout_set_width(layout, -1);
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
	}

	/* separator */
	int sep_y = y + prompt_h;
	cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
	cairo_set_line_width(cr, SEP_HEIGHT);
	cairo_move_to(cr, x, sep_y);
	cairo_line_to(cr, x + content_w, sep_y);
	cairo_stroke(cr);

	/* list */
	int list_y = sep_y + SEP_HEIGHT;

	for (int i = 0; i < visible; i++) {
		int idx = state.visible_start + i;
		if (idx >= state.n_filtered) break;
		int entry_idx = state.filtered[idx];
		const char *text = state.entries[entry_idx];

		int iy = list_y + i * item_h;
		bool selected = (idx == state.cursor);

		if (selected) {
			cairo_set_hex(cr, state.theme.highlight_bg);
			cairo_rectangle(cr, bx + bw, iy, box_w - bw * 2, item_h);
			cairo_fill(cr);
		}

		pango_layout_set_text(layout, text, -1);
		pango_layout_set_width(layout, content_w * PANGO_SCALE);
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

		int th;
		pango_layout_get_pixel_size(layout, NULL, &th);

		cairo_set_hex(cr, selected ? state.theme.highlight_fg : state.theme.fg);
		cairo_move_to(cr, x, iy + (item_h - th) / 2.0);
		pango_cairo_show_layout(cr, layout);

		pango_layout_set_width(layout, -1);
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
	}

	g_object_unref(layout);

	buf->busy = true;

	if (state.frame_callback) {
		wl_callback_destroy(state.frame_callback);
		state.frame_callback = NULL;
	}
	state.frame_callback = wl_surface_frame(state.surface);
	wl_callback_add_listener(state.frame_callback, &frame_listener, &state);
	state.frame_pending = true;

	wl_surface_attach(state.surface, buf->buffer, 0, 0);
	wl_surface_damage(state.surface, 0, 0, o_w, buf->height);
	wl_surface_commit(state.surface);
	wl_display_flush(state.display);
}

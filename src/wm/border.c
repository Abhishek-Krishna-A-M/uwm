#include <stdlib.h>
#include <wlr/types/wlr_scene.h>
#include "window.h"
#include "workspace.h"
#include "output.h"
#include "server.h"
#include "config.h"

static void border_color_floats(float out[4]) {
	uint32_t c = border_color;
	float a = ((c >> 24) & 0xFF) / 255.0f;
	float r = ((c >> 16) & 0xFF) / 255.0f;
	float g = ((c >> 8) & 0xFF) / 255.0f;
	float b = (c & 0xFF) / 255.0f;
	out[0] = r; out[1] = g; out[2] = b; out[3] = a;
	if (out[3] == 0) out[3] = 1.0f;
}

static struct wlr_scene_tree *border_parent(struct uwm_toplevel *t) {
	if (!t || !t->scene_tree) return NULL;
	struct wlr_scene_node *n = &t->scene_tree->node;
	if (!n->parent) return NULL;
	return (struct wlr_scene_tree *)n->parent;
}

static void ensure_parent(struct uwm_toplevel *t) {
	if (!t->border_top) return;
	struct wlr_scene_tree *parent = border_parent(t);
	if (!parent) return;
	struct wlr_scene_tree *top_parent = t->border_top->node.parent;
	if (top_parent == parent) return;
	/* reparent borders to current window parent (handles floating toggle) */
	wlr_scene_node_reparent(&t->border_top->node, parent);
	wlr_scene_node_reparent(&t->border_bottom->node, parent);
	wlr_scene_node_reparent(&t->border_left->node, parent);
	wlr_scene_node_reparent(&t->border_right->node, parent);
}

void toplevel_create_border(struct uwm_toplevel *t) {
	if (!t || !t->scene_tree) return;
	struct wlr_scene_tree *parent = border_parent(t);
	if (!parent) return;
	float color[4];
	border_color_floats(color);
	int bw = borderpx;
	if (bw <= 0) return;
	t->border_top = wlr_scene_rect_create(parent, 0, 0, color);
	t->border_bottom = wlr_scene_rect_create(parent, 0, 0, color);
	t->border_left = wlr_scene_rect_create(parent, 0, 0, color);
	t->border_right = wlr_scene_rect_create(parent, 0, 0, color);
	if (!t->border_top || !t->border_bottom || !t->border_left || !t->border_right) {
		toplevel_destroy_border(t);
		return;
	}
	/* hide initially, keep below window */
	wlr_scene_node_set_enabled(&t->border_top->node, false);
	wlr_scene_node_set_enabled(&t->border_bottom->node, false);
	wlr_scene_node_set_enabled(&t->border_left->node, false);
	wlr_scene_node_set_enabled(&t->border_right->node, false);
	t->border_shown = false;
	t->border_x = t->border_y = t->border_w = t->border_h = 0;
	/* ensure window stays on top of its borders */
	wlr_scene_node_raise_to_top(&t->scene_tree->node);
}

void toplevel_destroy_border(struct uwm_toplevel *t) {
	if (!t) return;
	if (t->border_top) wlr_scene_node_destroy(&t->border_top->node);
	if (t->border_bottom) wlr_scene_node_destroy(&t->border_bottom->node);
	if (t->border_left) wlr_scene_node_destroy(&t->border_left->node);
	if (t->border_right) wlr_scene_node_destroy(&t->border_right->node);
	t->border_top = t->border_bottom = t->border_left = t->border_right = NULL;
	t->border_shown = false;
	t->border_x = t->border_y = t->border_w = t->border_h = 0;
}

static bool should_show_border(struct uwm_toplevel *t) {
	if (!t || !t->workspace || !t->scene_tree) return false;
	if (!t->scene_tree->node.enabled) return false;
	/* Menus/tooltips/popup dialogs are not workspace members and must not
	 * be framed. */
	if (toplevel_is_override_redirect(t)) return false;
	if (t->fullscreen) return false;
	if (!t->workspace->output) return false;
	if (t->workspace->output->current_workspace != t->workspace->id) return false;
	if (t->workspace->focused != t) return false;
	if (t->workspace->monocle && !t->floating) return false;
	if (t->floating) return true;
	/* tiled: only if more than 1 tiled window in workspace */
	int tiled = 0;
	struct uwm_toplevel *tl;
	wl_list_for_each(tl, &t->workspace->toplevels, workspace_link) {
		if (!tl->floating && !tl->fullscreen) tiled++;
		if (tiled > 1) break;
	}
	return tiled > 1;
}

void toplevel_update_border(struct uwm_toplevel *t) {
	if (!t) return;
	/* Menus/override-redirect windows never get a border, so don't even
	 * allocate the four rects for them. */
	if (toplevel_is_override_redirect(t)) {
		toplevel_destroy_border(t);
		return;
	}
	if (!t->border_top) {
		toplevel_create_border(t);
		if (!t->border_top) return;
	}
	ensure_parent(t);
	bool show = should_show_border(t);

	/* Early-out when visibility didn't change. raise_to_top() dirties the
	 * whole parent scene layer, so re-issuing it on every unrelated surface
	 * commit is the single biggest source of needless repaint here. */
	if (t->border_shown != show) {
		t->border_shown = show;
		wlr_scene_node_set_enabled(&t->border_top->node, show);
		wlr_scene_node_set_enabled(&t->border_bottom->node, show);
		wlr_scene_node_set_enabled(&t->border_left->node, show);
		wlr_scene_node_set_enabled(&t->border_right->node, show);
	}
	if (!show) return;

	int bw = borderpx;
	if (bw <= 0) return;

	struct wlr_box box = toplevel_content_box(t);
	int x = box.x;
	int y = box.y;
	int w = box.width;
	int h = box.height;
	if (w <= 0 || h <= 0) {
		if (t->floating) {
			w = t->float_width; h = t->float_height;
			x = t->float_x; y = t->float_y;
		} else if (t->workspace && t->workspace->root) {
			struct uwm_bsp_node *leaf = bsp_find_leaf(t->workspace->root, t);
			if (leaf) { x = leaf->x; y = leaf->y; w = leaf->width; h = leaf->height; }
		}
		if (w <= 0 || h <= 0) return;
	}

	if (t->border_x == x && t->border_y == y
			&& t->border_w == w && t->border_h == h)
		return; /* geometry unchanged — nothing to damage */

	t->border_x = x; t->border_y = y;
	t->border_w = w; t->border_h = h;

	/* keep window on top of borders */
	wlr_scene_node_raise_to_top(&t->scene_tree->node);

	wlr_scene_rect_set_size(t->border_top, w + 2*bw, bw);
	wlr_scene_node_set_position(&t->border_top->node, x - bw, y - bw);

	wlr_scene_rect_set_size(t->border_bottom, w + 2*bw, bw);
	wlr_scene_node_set_position(&t->border_bottom->node, x - bw, y + h);

	wlr_scene_rect_set_size(t->border_left, bw, h);
	wlr_scene_node_set_position(&t->border_left->node, x - bw, y);

	wlr_scene_rect_set_size(t->border_right, bw, h);
	wlr_scene_node_set_position(&t->border_right->node, x + w, y);
}

void workspace_update_borders(struct uwm_workspace *ws) {
	if (!ws) return;
	struct uwm_toplevel *tl;
	wl_list_for_each(tl, &ws->toplevels, workspace_link) {
		toplevel_update_border(tl);
	}
	wl_list_for_each(tl, &ws->floating_windows, floating_link) {
		toplevel_update_border(tl);
	}
}

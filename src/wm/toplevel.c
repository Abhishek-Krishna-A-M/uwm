#include <assert.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include "config.h"
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include "window.h"
#include "input.h"
#include "bsp.h"
#include "floating.h"
#include "layout.h"
#include "server.h"
#include "rules.h"
#include "output.h"
#include "uwm_bar.h"
#include <wlr/config.h>

struct wlr_surface *toplevel_surface(struct uwm_toplevel *t) {
	if (!t) return NULL;
	if (t->type == UWM_TOPLEVEL_XDG && t->xdg_toplevel)
		return t->xdg_toplevel->base->surface;
#if WLR_HAS_XWAYLAND
	if (t->type == UWM_TOPLEVEL_XWAYLAND && t->xwayland_surface)
		return t->xwayland_surface->surface;
#endif
	return NULL;
}

struct wlr_box toplevel_geometry(struct uwm_toplevel *t) {
	struct wlr_box box = {0};
	if (!t) return box;
	if (t->type == UWM_TOPLEVEL_XDG && t->xdg_toplevel) {
		box = t->xdg_toplevel->base->geometry;
	} else {
#if WLR_HAS_XWAYLAND
		if (t->type == UWM_TOPLEVEL_XWAYLAND && t->xwayland_surface && t->xwayland_surface->surface) {
			box.x = 0; box.y = 0;
			box.width = t->xwayland_surface->surface->current.width;
			box.height = t->xwayland_surface->surface->current.height;
			if (box.width == 0) box.width = t->xwayland_surface->width;
			if (box.height == 0) box.height = t->xwayland_surface->height;
		}
#endif
	}
	return box;
}

void toplevel_set_size(struct uwm_toplevel *t, int w, int h) {
	if (!t) return;
	if (t->type == UWM_TOPLEVEL_XDG && t->xdg_toplevel) {
		wlr_xdg_toplevel_set_size(t->xdg_toplevel, w, h);
	}
#if WLR_HAS_XWAYLAND
	else if (t->type == UWM_TOPLEVEL_XWAYLAND && t->xwayland_surface) {
		int x = t->floating ? t->float_x : (t->scene_tree ? t->scene_tree->node.x : t->xwayland_surface->x);
		int y = t->floating ? t->float_y : (t->scene_tree ? t->scene_tree->node.y : t->xwayland_surface->y);
		wlr_xwayland_surface_configure(t->xwayland_surface, x, y, w, h);
	}
#endif
}

void toplevel_set_activated(struct uwm_toplevel *t, bool activated) {
	if (!t) return;
	if (t->type == UWM_TOPLEVEL_XDG && t->xdg_toplevel) {
		wlr_xdg_toplevel_set_activated(t->xdg_toplevel, activated);
	}
#if WLR_HAS_XWAYLAND
	else if (t->type == UWM_TOPLEVEL_XWAYLAND && t->xwayland_surface) {
		wlr_xwayland_surface_activate(t->xwayland_surface, activated);
		if (activated) {
			wlr_xwayland_surface_restack(t->xwayland_surface, NULL, XCB_STACK_MODE_ABOVE);
		}
	}
#endif
	if (t->foreign_toplevel)
		wlr_foreign_toplevel_handle_v1_set_activated(t->foreign_toplevel, activated);
}

void toplevel_set_fullscreen(struct uwm_toplevel *t, bool fs) {
	if (!t) return;
	if (t->type == UWM_TOPLEVEL_XDG && t->xdg_toplevel) {
		wlr_xdg_toplevel_set_fullscreen(t->xdg_toplevel, fs);
	}
#if WLR_HAS_XWAYLAND
	else if (t->type == UWM_TOPLEVEL_XWAYLAND && t->xwayland_surface) {
		wlr_xwayland_surface_set_fullscreen(t->xwayland_surface, fs);
	}
#endif
}

const char *toplevel_app_id(struct uwm_toplevel *t) {
	if (!t) return NULL;
	if (t->type == UWM_TOPLEVEL_XDG && t->xdg_toplevel) return t->xdg_toplevel->app_id;
#if WLR_HAS_XWAYLAND
	if (t->type == UWM_TOPLEVEL_XWAYLAND && t->xwayland_surface) return t->xwayland_surface->class;
#endif
	return NULL;
}
const char *toplevel_title(struct uwm_toplevel *t) {
	if (!t) return NULL;
	if (t->type == UWM_TOPLEVEL_XDG && t->xdg_toplevel) return t->xdg_toplevel->title;
#if WLR_HAS_XWAYLAND
	if (t->type == UWM_TOPLEVEL_XWAYLAND && t->xwayland_surface) return t->xwayland_surface->title;
#endif
	return NULL;
}

void toplevel_send_close(struct uwm_toplevel *t) {
	if (!t) return;
	if (t->type == UWM_TOPLEVEL_XDG && t->xdg_toplevel) wlr_xdg_toplevel_send_close(t->xdg_toplevel);
#if WLR_HAS_XWAYLAND
	else if (t->type == UWM_TOPLEVEL_XWAYLAND && t->xwayland_surface) wlr_xwayland_surface_close(t->xwayland_surface);
#endif
}

bool should_tile_toplevel(struct uwm_toplevel *toplevel) {
	if (toplevel->type == UWM_TOPLEVEL_XDG) {
		struct wlr_xdg_toplevel *xdt = toplevel->xdg_toplevel;
		if (!xdt) return true;
		if (xdt->parent) return false;
		if (xdt->current.min_width > 0 && xdt->current.min_height > 0
				&& xdt->current.min_width == xdt->current.max_width
				&& xdt->current.min_height == xdt->current.max_height) {
			return false;
		}
		return true;
	}
#if WLR_HAS_XWAYLAND
	if (toplevel->type == UWM_TOPLEVEL_XWAYLAND) {
		struct wlr_xwayland_surface *xs = toplevel->xwayland_surface;
		if (!xs) return true;
		if (xs->override_redirect) return false;
		if (xs->parent) return false;
		if (xs->size_hints) {
			if (xs->size_hints->min_width > 0 && xs->size_hints->min_height > 0 &&
			    xs->size_hints->max_width == xs->size_hints->min_width &&
			    xs->size_hints->max_height == xs->size_hints->min_height)
				return false;
		}
		if (wlr_xwayland_surface_has_window_type(xs, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DIALOG) ||
		    wlr_xwayland_surface_has_window_type(xs, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_UTILITY) ||
		    wlr_xwayland_surface_has_window_type(xs, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_TOOLBAR) ||
		    wlr_xwayland_surface_has_window_type(xs, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_SPLASH) ||
		    wlr_xwayland_surface_has_window_type(xs, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_MENU) ||
		    wlr_xwayland_surface_has_window_type(xs, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DROPDOWN_MENU) ||
		    wlr_xwayland_surface_has_window_type(xs, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_POPUP_MENU) ||
		    wlr_xwayland_surface_has_window_type(xs, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_TOOLTIP) ||
		    wlr_xwayland_surface_has_window_type(xs, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_NOTIFICATION)) {
			return false;
		}
		return true;
	}
#endif
	return true;
}

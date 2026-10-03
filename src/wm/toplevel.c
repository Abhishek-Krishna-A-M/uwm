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

/* The visible content of a toplevel, in layout coordinates.
 *
 * `wlr_scene_xdg_surface_create()` documents that "the origin of the
 * returned scene-graph node will match the top-left corner of the
 * xdg_surface window geometry" — wlroots internally offsets the surface
 * by -geo.x/-geo.y inside the tree. Therefore scene_tree->node.x/y is
 * already the content origin; adding geo.x/geo.y again double-counts the
 * frame inset and shifts borders/cursor math on every client-side
 * decorated client (e.g. a browser with a non-system title bar). */
struct wlr_box toplevel_content_box(struct uwm_toplevel *t) {
	struct wlr_box box = {0};
	if (!t) return box;
	struct wlr_box geo = toplevel_geometry(t);
	box.x = t->scene_tree ? t->scene_tree->node.x : 0;
	box.y = t->scene_tree ? t->scene_tree->node.y : 0;
	box.width = geo.width;
	box.height = geo.height;
	return box;
}

void toplevel_set_size(struct uwm_toplevel *t, int w, int h) {
	if (!t) return;
	/* Record what we asked for. The client's reported geometry differs
	 * from the requested size on client-side-decorated windows (the frame
	 * is not part of geometry.width), so comparing against it to decide
	 * whether a configure is needed produces a permanent configure loop
	 * on those clients. */
	t->req_width = w;
	t->req_height = h;
	if (t->type == UWM_TOPLEVEL_XDG && t->xdg_toplevel) {
		wlr_xdg_toplevel_set_size(t->xdg_toplevel, w, h);
	}
#if WLR_HAS_XWAYLAND
	else if (t->type == UWM_TOPLEVEL_XWAYLAND && t->xwayland_surface) {
		/* wlr_xwayland_surface_configure() requires a mapped, managed
		 * surface. Override-redirect windows (menus, tooltips) are
		 * positioned by the client itself and must not be configured. */
		if (!t->xwayland_surface->surface || !t->xwayland_surface->surface->mapped
				|| t->xwayland_surface->override_redirect) {
			return;
		}
		int x = t->floating ? t->float_x : (t->scene_tree ? t->scene_tree->node.x : t->xwayland_surface->x);
		int y = t->floating ? t->float_y : (t->scene_tree ? t->scene_tree->node.y : t->xwayland_surface->y);
		wlr_xwayland_surface_configure(t->xwayland_surface, x, y, w, h);
	}
#endif
}

bool toplevel_is_override_redirect(struct uwm_toplevel *t) {
#if WLR_HAS_XWAYLAND
	return t && t->type == UWM_TOPLEVEL_XWAYLAND && t->xwayland_surface
		&& t->xwayland_surface->override_redirect;
#else
	(void)t;
	return false;
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
		if (activated && !t->xwayland_surface->override_redirect) {
			/* restack() asserts !override_redirect; menus and other
			 * override-redirect windows own their own stacking. */
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

#include <assert.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include "window.h"

static void set_window_opacity(struct wlr_scene_buffer *buffer,
		int sx, int sy, void *data)
{
	float *opacity = data;
	wlr_scene_buffer_set_opacity(buffer, *opacity);
	(void)sx;
	(void)sy;
}
#include "input.h"
#include "bsp.h"
#include "floating.h"
#include "layout.h"
#include "server.h"
#include "rules.h"

void focus_toplevel(struct uwm_toplevel *toplevel) {
	if (toplevel == NULL) {
		return;
	}
	/* Transient helpers must never receive keyboard focus.
	 * We use the flag set once at map time instead of should_tile_toplevel()
	 * because the client may commit non-zero constraints after map, which
	 * would make the heuristic return the wrong answer. */
	if (toplevel->is_transient) {
		return;
	}
	struct uwm_server *server = toplevel->server;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
	if (prev_surface == surface) {
		return;
	}

	const char *new_app_id = toplevel->xdg_toplevel->app_id;
	const char *new_title = toplevel->xdg_toplevel->title;
	wlr_log(WLR_INFO, "FOCUS: new app_id=%s title=%s",
		new_app_id ? new_app_id : "(nil)", new_title ? new_title : "(nil)");
	if (prev_surface) {
		struct wlr_xdg_toplevel *prev_toplevel = wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
		if (prev_toplevel != NULL) {
			wlr_xdg_toplevel_set_activated(prev_toplevel, false);
			struct wlr_scene_tree *prev_tree = prev_toplevel->base->data;
			if (prev_tree) {
				struct uwm_toplevel *prev = prev_tree->node.data;
				if (prev && !prev->fullscreen) {
					float dim = 0.85f;
					wlr_scene_node_for_each_buffer(
						&prev_tree->node, set_window_opacity, &dim);
				}
			}
		}
	}

	struct uwm_workspace *ws = toplevel->workspace;
	ws->last_focused = ws->focused;
	ws->focused = toplevel;

	/* Update tabbed/monocle container active_child */
	if (ws->root && !toplevel->floating && !toplevel->fullscreen) {
		struct uwm_bsp_node *leaf = bsp_find_leaf(ws->root, toplevel);
		if (leaf) {
			struct uwm_bsp_node *cont = bsp_find_tabbed_parent(leaf);
			if (cont && cont->active_child != leaf) {
				cont->active_child = leaf;
				update_layout_visibility(cont);
			}
		}
	}

	/* Update display for workspace-level monocle */
	if (ws->monocle) {
		int out_w, out_h;
		get_output_size(server, &out_w, &out_h);
		bsp_arrange(ws, out_w, out_h, server->config.inner_gap);
	}

	if (toplevel->floating) {
		raise_floating(toplevel);
	} else {
		wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
	}

	wl_list_remove(&toplevel->link);
	wl_list_insert(&server->toplevels, &toplevel->link);
	wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
	if (toplevel->decoration) {
		wlr_xdg_toplevel_decoration_v1_set_mode(toplevel->decoration,
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
	float full = 1.0f;
	wlr_scene_node_for_each_buffer(
		&toplevel->scene_tree->node, set_window_opacity, &full);
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	if (keyboard != NULL) {
		wlr_log(WLR_INFO, "KEYBOARD_ENTER: surface=%p app_id=%s title=%s",
			(void *)surface,
			toplevel->xdg_toplevel->app_id ? toplevel->xdg_toplevel->app_id : "(nil)",
			toplevel->xdg_toplevel->title ? toplevel->xdg_toplevel->title : "(nil)");
		wlr_seat_keyboard_notify_enter(seat, surface,
			keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	}

	/* Warp cursor to center of window if not already inside */
	struct wlr_box geo = toplevel->xdg_toplevel->base->geometry;
	double wx = toplevel->scene_tree->node.x + geo.x;
	double wy = toplevel->scene_tree->node.y + geo.y;
	double ww = geo.width;
	double wh = geo.height;
	if (ww > 0 && wh > 0 &&
			(server->cursor->x < wx || server->cursor->x >= wx + ww ||
			 server->cursor->y < wy || server->cursor->y >= wy + wh)) {
		wlr_cursor_warp(server->cursor, NULL, wx + ww / 2.0, wy + wh / 2.0);
	}
}

struct uwm_toplevel *desktop_toplevel_at(
		struct uwm_server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	/* This returns the topmost node in the scene at the given layout coords.
	 * We only care about surface nodes as we are specifically looking for a
	 * surface in the surface tree of a uwm_toplevel. */
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
	if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}
	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
	if (!scene_surface) {
		return NULL;
	}

	*surface = scene_surface->surface;

	/* Don't return layer surfaces (or their subsurfaces/popups) as toplevels.
	 * Walk up the surface tree to check if any ancestor is a layer surface. */
	struct wlr_surface *check = *surface;
	do {
		if (!check) break;
		if (wlr_layer_surface_v1_try_from_wlr_surface(check)) {
			return NULL;
		}
		struct wlr_subsurface *sub = wlr_subsurface_try_from_wlr_surface(check);
		if (sub) {
			check = wlr_surface_get_root_surface(check);
			continue;
		}
		struct wlr_xdg_surface *xdg = wlr_xdg_surface_try_from_wlr_surface(check);
		if (xdg && xdg->role == WLR_XDG_SURFACE_ROLE_POPUP) {
			if (!xdg->popup || !xdg->popup->parent) break;
			check = wlr_surface_get_root_surface(xdg->popup->parent);
			continue;
		}
		break;
	} while (true);

	/* Find the node corresponding to the uwm_toplevel at the root of this
	 * surface tree, it is the only one for which we set the data field. */
	struct wlr_scene_tree *tree = node->parent;
	while (tree != NULL && tree->node.data == NULL) {
		tree = tree->node.parent;
	}
	if (!tree)
		return NULL;
	struct uwm_toplevel *result = tree->node.data;
	if (result->is_transient) {
		*surface = NULL;
		return NULL;
	}
	return result;
}

bool should_tile_toplevel(struct uwm_toplevel *toplevel) {
	struct wlr_xdg_toplevel *xdt = toplevel->xdg_toplevel;

	/* Transient helpers (clipboard managers, portals, etc.) create a
	 * toplevel, do their work, and exit within a single event loop.
	 * At map time they have not set any size constraints, so all four
	 * min/max values are zero.  A real application window will always
	 * have at least a non-zero max size from its configure. */
	if (xdt->current.min_width == 0 && xdt->current.min_height == 0
			&& xdt->current.max_width == 0 && xdt->current.max_height == 0) {
		return false;
	}

	return true;
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
	/* Called when the surface is mapped, or ready to display on-screen. */
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, map);

	const char *app_id = toplevel->xdg_toplevel->app_id;
	const char *title = toplevel->xdg_toplevel->title;
	struct wlr_seat *seat = toplevel->server->seat;
	wlr_log(WLR_INFO, "MAP: app_id=%s title=%s kb_focus_before=%p",
		app_id ? app_id : "(nil)", title ? title : "(nil)",
		(void *)seat->keyboard_state.focused_surface);

	wl_list_insert(&toplevel->server->toplevels, &toplevel->link);
	wl_list_insert(&toplevel->workspace->toplevels, &toplevel->workspace_link);

	/* Mark transient helpers at map time. The flag is permanent —
	 * should_tile_toplevel() can give different results later once the
	 * client commits non-zero constraints, so we snapshot the decision here. */
	if (!should_tile_toplevel(toplevel)) {
		toplevel->is_transient = true;
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
		return;
	}

	struct uwm_workspace *current = &toplevel->server->workspaces.workspaces[toplevel->server->workspaces.current];

	rule_apply_all(&toplevel->server->config, toplevel);

	if (!toplevel->floating && !toplevel->fullscreen) {
		bsp_insert(toplevel->workspace, toplevel);
	}

	int w, h;
	get_output_size(toplevel->server, &w, &h);
	bsp_arrange(toplevel->workspace, w, h, toplevel->server->config.inner_gap);

	if (toplevel->workspace != current
			|| (toplevel->workspace->fullscreen_window
				&& toplevel->workspace->fullscreen_window != toplevel)) {
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	}

	if (!toplevel->workspace->fullscreen_window
			|| toplevel->workspace->fullscreen_window == toplevel) {
		focus_toplevel(toplevel);
	}
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
	/* Called when the surface is unmapped, and should no longer be shown. */
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);

	const char *app_id = toplevel->xdg_toplevel->app_id;
	const char *title = toplevel->xdg_toplevel->title;
	struct wlr_seat *seat = toplevel->server->seat;
	wlr_log(WLR_INFO, "UNMAP: app_id=%s title=%s kb_focused_surface=%p",
		app_id ? app_id : "(nil)", title ? title : "(nil)",
		(void *)seat->keyboard_state.focused_surface);

	/* Reset the cursor mode if the grabbed toplevel was unmapped. */
	if (toplevel == toplevel->server->grabbed_toplevel) {
		reset_cursor_mode(toplevel->server);
	}

	wl_list_remove(&toplevel->link);
	wl_list_remove(&toplevel->workspace_link);
	wl_list_remove(&toplevel->floating_link);
	wl_list_init(&toplevel->workspace_link);

	bsp_remove(toplevel->workspace, toplevel);

	int w, h;
	get_output_size(toplevel->server, &w, &h);
	bsp_arrange(toplevel->workspace, w, h, toplevel->server->config.inner_gap);

	struct uwm_workspace *ws = toplevel->workspace;
	if (ws->last_focused == toplevel)
		ws->last_focused = NULL;

	bool focus_was_displaced = (ws->focused == toplevel);
	if (focus_was_displaced) {
		ws->focused = NULL;
		struct uwm_toplevel *candidate;
		wl_list_for_each(candidate, &ws->toplevels, workspace_link) {
			if (!candidate->is_transient) {
				ws->focused = candidate;
				break;
			}
		}
		if (!ws->focused && !wl_list_empty(&ws->floating_windows)) {
			candidate = wl_container_of(ws->floating_windows.next, candidate, floating_link);
			ws->focused = candidate;
		}
	}

	/* Exit monocle mode if no tiled windows remain */
	if (ws->monocle && wl_list_empty(&ws->toplevels)) {
		ws->monocle = false;
		if (ws->root) {
			set_children_visible(ws->root, true);
			bsp_arrange(ws, w, h, toplevel->server->config.inner_gap);
		}
	}

	/* Restore keyboard focus only if the unmap actually displaced the
	 * focused window (i.e. ws->focused was pointing to the toplevel that
	 * just unmapped). Transient helpers never take focus (focus_toplevel
	 * refuses them), so ws->focused never points to them, and this block
	 * is correctly skipped — no spurious leave/enter events are sent. */
	if (focus_was_displaced && ws->focused) {
		struct wlr_surface *target_surface = ws->focused->xdg_toplevel->base->surface;
		struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
		if (keyboard) {
			wlr_log(WLR_INFO, "UNMAP: restoring kb focus to app_id=%s title=%s",
				ws->focused->xdg_toplevel->app_id ? ws->focused->xdg_toplevel->app_id : "(nil)",
				ws->focused->xdg_toplevel->title ? ws->focused->xdg_toplevel->title : "(nil)");
			/* Don't call clear_focus — just enter the new surface directly.
			 * Calling clear_focus can corrupt seat state if another event handler
			 * (like layer shell) also tries to update focus. DWL and Sway both
			 * avoid calling clear_focus in event handlers — they only call enter. */
			wlr_seat_keyboard_notify_enter(seat, target_surface,
				keyboard->keycodes, keyboard->num_keycodes,
				&keyboard->modifiers);
		}
	} else if (!ws->focused) {
		wlr_log(WLR_INFO, "UNMAP: no focused window, clearing keyboard focus");
		wlr_seat_keyboard_notify_clear_focus(seat);
	}
}

static void decoration_handle_destroy(struct wl_listener *listener, void *data) {
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, decoration_destroy);
	toplevel->decoration = NULL;
	wl_list_remove(&toplevel->decoration_destroy.link);
	wl_list_remove(&toplevel->decoration_request_mode.link);
}

static void decoration_handle_request_mode(struct wl_listener *listener, void *data) {
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, decoration_request_mode);
	if (!toplevel->decoration) {
		return;
	}
	/* If the surface isn't initialized yet, we can't schedule a configure.
	 * The initial commit handler will override the mode anyway. */
	if (!toplevel->xdg_toplevel->base->initialized) {
		return;
	}
	wlr_xdg_toplevel_decoration_v1_set_mode(toplevel->decoration,
		WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
	/* Called when a new surface state is committed. */
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, commit);

	if (toplevel->xdg_toplevel->base->initial_commit) {
		if (toplevel->decoration) {
			wlr_xdg_toplevel_decoration_v1_set_mode(toplevel->decoration,
				WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
		}
		wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
	}
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
	/* Called when the xdg_toplevel is destroyed. */
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);

	wl_list_remove(&toplevel->map.link);
	wl_list_remove(&toplevel->unmap.link);
	wl_list_remove(&toplevel->commit.link);
	wl_list_remove(&toplevel->destroy.link);
	wl_list_remove(&toplevel->request_move.link);
	wl_list_remove(&toplevel->request_resize.link);
	wl_list_remove(&toplevel->request_maximize.link);
	wl_list_remove(&toplevel->request_fullscreen.link);
	if (toplevel->decoration) {
		wl_list_remove(&toplevel->decoration_destroy.link);
		wl_list_remove(&toplevel->decoration_request_mode.link);
	}

	struct uwm_workspace *ws = toplevel->workspace;
	if (ws->last_focused == toplevel)
		ws->last_focused = NULL;
	if (ws->focused == toplevel)
		ws->focused = NULL;
	if (ws->fullscreen_window == toplevel)
		ws->fullscreen_window = NULL;

	free(toplevel);
}

static void xdg_toplevel_request_move(struct wl_listener *listener, void *data) {
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
	struct wlr_xdg_toplevel_move_event *event = data;
	if (event->serial != toplevel->server->last_button_serial)
		return;
	begin_interactive(toplevel, UWM_CURSOR_MOVE, 0);
}

static void xdg_toplevel_request_resize(struct wl_listener *listener, void *data) {
	struct wlr_xdg_toplevel_resize_event *event = data;
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
	if (event->serial != toplevel->server->last_button_serial)
		return;
	begin_interactive(toplevel, UWM_CURSOR_RESIZE, event->edges);
}

static void xdg_toplevel_request_maximize(struct wl_listener *listener, void *data) {
	/* This event is raised when a client would like to maximize itself,
	 * typically because the user clicked on the maximize button on client-side
	 * decorations. uwm doesn't support maximization, but to conform to
	 * xdg-shell protocol we still must send a configure.
	 * wlr_xdg_surface_schedule_configure() is used to send an empty reply.
	 * However, if the request was sent before an initial commit, we don't do
	 * anything and let the client finish the initial surface setup. */
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, request_maximize);
	if (toplevel->xdg_toplevel->base->initialized) {
		wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
	}
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener, void *data) {
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, request_fullscreen);
	(void)data;
	if (!toplevel->xdg_toplevel->base->initialized)
		return;

	if (toplevel->xdg_toplevel->requested.fullscreen != toplevel->fullscreen)
		toggle_fullscreen(toplevel);
	wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
}

void server_new_toplevel_decoration(struct wl_listener *listener,
		void *data) {
	/* Cannot call set_mode here because the xdg_surface has not yet been
	 * initialized (no initial commit). Store the decoration and wait for
	 * the commit handler to set the mode. */
	struct wlr_xdg_toplevel_decoration_v1 *decoration = data;
	struct wlr_xdg_surface *xdg_surface = decoration->toplevel->base;
	struct wlr_scene_tree *tree = xdg_surface->data;
	struct uwm_toplevel *toplevel = tree->node.data;
	toplevel->decoration = decoration;

	toplevel->decoration_destroy.notify = decoration_handle_destroy;
	wl_signal_add(&decoration->events.destroy, &toplevel->decoration_destroy);
	toplevel->decoration_request_mode.notify = decoration_handle_request_mode;
	wl_signal_add(&decoration->events.request_mode, &toplevel->decoration_request_mode);
}

void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
	/* This event is raised when a client creates a new toplevel (application window). */
	struct uwm_server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *xdg_toplevel = data;

	/* Allocate a uwm_toplevel for this surface */
	struct uwm_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	toplevel->server = server;
	toplevel->xdg_toplevel = xdg_toplevel;
	toplevel->workspace = &server->workspaces.workspaces[server->workspaces.current];
	wl_list_init(&toplevel->link);
	wl_list_init(&toplevel->workspace_link);
	toplevel->scene_tree = wlr_scene_xdg_surface_create(toplevel->server->tiled_layer, xdg_toplevel->base);
	toplevel->scene_tree->node.data = toplevel;
	xdg_toplevel->base->data = toplevel->scene_tree;
	wl_list_init(&toplevel->floating_link);

	/* Listen to the various events it can emit */
	toplevel->map.notify = xdg_toplevel_map;
	wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);
	toplevel->unmap.notify = xdg_toplevel_unmap;
	wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);
	toplevel->commit.notify = xdg_toplevel_commit;
	wl_signal_add(&xdg_toplevel->base->surface->events.commit, &toplevel->commit);

	toplevel->destroy.notify = xdg_toplevel_destroy;
	wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);

	/* cotd */
	toplevel->request_move.notify = xdg_toplevel_request_move;
	wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);
	toplevel->request_resize.notify = xdg_toplevel_request_resize;
	wl_signal_add(&xdg_toplevel->events.request_resize, &toplevel->request_resize);
	toplevel->request_maximize.notify = xdg_toplevel_request_maximize;
	wl_signal_add(&xdg_toplevel->events.request_maximize, &toplevel->request_maximize);
	toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen, &toplevel->request_fullscreen);
}

static void xdg_popup_commit(struct wl_listener *listener, void *data) {
	/* Called when a new surface state is committed. */
	struct uwm_popup *popup = wl_container_of(listener, popup, commit);

	if (popup->xdg_popup->base->initial_commit) {
		/* When an xdg_surface performs an initial commit, the compositor must
		 * reply with a configure so the client can map the surface.
		 * uwm sends an empty configure. A more sophisticated compositor
		 * might change an xdg_popup's geometry to ensure it's not positioned
		 * off-screen, for example. */
		wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
	}
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
	/* Called when the xdg_popup is destroyed. */
	struct uwm_popup *popup = wl_container_of(listener, popup, destroy);

	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->destroy.link);

	free(popup);
}

void server_new_xdg_popup(struct wl_listener *listener, void *data) {
	/* This event is raised when a client creates a new popup. */
	struct wlr_xdg_popup *xdg_popup = data;

	struct uwm_popup *popup = calloc(1, sizeof(*popup));
	popup->xdg_popup = xdg_popup;

	/* We must add xdg popups to the scene graph so they get rendered. The
	 * wlroots scene graph provides a helper for this, but to use it we must
	 * provide the proper parent scene node of the xdg popup. To enable this,
	 * we always set the user data field of xdg_surfaces to the corresponding
	 * scene node. */
	struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
	assert(parent != NULL);
	struct wlr_scene_tree *parent_tree = parent->data;
	xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

	popup->commit.notify = xdg_popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

	popup->destroy.notify = xdg_popup_destroy;
	wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

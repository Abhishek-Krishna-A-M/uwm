#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_output_layout.h>
#include "window.h"
#include "input.h"
#include "bsp.h"
#include "floating.h"
#include "layout.h"
#include "server.h"

void focus_toplevel(struct uwm_toplevel *toplevel) {
	if (toplevel == NULL) {
		return;
	}
	struct uwm_server *server = toplevel->server;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
	if (prev_surface == surface) {
		return;
	}
	if (prev_surface) {
		struct wlr_xdg_toplevel *prev_toplevel = wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
		if (prev_toplevel != NULL) {
			wlr_xdg_toplevel_set_activated(prev_toplevel, false);
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
				update_tab_bar(cont);
			}
		}
	}

	/* Update display for workspace-level monocle */
	if (ws->monocle) {
		int out_w, out_h;
		get_output_size(server, &out_w, &out_h);
		bsp_arrange(ws, out_w, out_h);
	}

	if (toplevel->floating) {
		raise_floating(toplevel);
	} else {
		wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
	}

	wl_list_remove(&toplevel->link);
	wl_list_insert(&server->toplevels, &toplevel->link);
	wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(seat, surface,
			keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
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
	/* Find the node corresponding to the uwm_toplevel at the root of this
	 * surface tree, it is the only one for which we set the data field. */
	struct wlr_scene_tree *tree = node->parent;
	while (tree != NULL && tree->node.data == NULL) {
		tree = tree->node.parent;
	}
	if (!tree)
		return NULL;
	return tree->node.data;
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
	/* Called when the surface is mapped, or ready to display on-screen. */
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, map);

	wl_list_insert(&toplevel->server->toplevels, &toplevel->link);
	wl_list_insert(&toplevel->workspace->toplevels, &toplevel->workspace_link);

	struct uwm_workspace *current = &toplevel->server->workspaces.workspaces[toplevel->server->workspaces.current];

	bsp_insert(toplevel->workspace, toplevel);

	int w, h;
	get_output_size(toplevel->server, &w, &h);
	bsp_arrange(toplevel->workspace, w, h);

	if (toplevel->workspace != current
			|| toplevel->workspace->fullscreen_window) {
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	}

	if (!toplevel->workspace->fullscreen_window) {
		focus_toplevel(toplevel);
	}
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
	/* Called when the surface is unmapped, and should no longer be shown. */
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);

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
	bsp_arrange(toplevel->workspace, w, h);

	struct uwm_workspace *ws = toplevel->workspace;
	if (ws->last_focused == toplevel)
		ws->last_focused = NULL;

	if (ws->focused == toplevel) {
		if (!wl_list_empty(&ws->toplevels)) {
			struct uwm_toplevel *next = wl_container_of(ws->toplevels.next, next, workspace_link);
			ws->focused = next;
		} else if (!wl_list_empty(&ws->floating_windows)) {
			struct uwm_toplevel *next = wl_container_of(ws->floating_windows.next, next, floating_link);
			ws->focused = next;
		} else {
			ws->focused = NULL;
		}
	}
	if (ws->focused)
		focus_toplevel(ws->focused);
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
	/* Called when a new surface state is committed. */
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, commit);

	if (toplevel->xdg_toplevel->base->initial_commit) {
		/* When an xdg_surface performs an initial commit, the compositor must
		 * reply with a configure so the client can map the surface. uwm
		 * configures the xdg_toplevel with 0,0 size to let the client pick the
		 * dimensions itself. */
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
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

void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
	/* This event is raised when a client creates a new toplevel (application window). */
	struct uwm_server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *xdg_toplevel = data;

	/* Allocate a uwm_toplevel for this surface */
	struct uwm_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	toplevel->server = server;
	toplevel->xdg_toplevel = xdg_toplevel;
	toplevel->workspace = &server->workspaces.workspaces[server->workspaces.current];
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

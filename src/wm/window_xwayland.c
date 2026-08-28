#include <assert.h>
#include <stdlib.h>
#include <string.h>
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

static void handle_foreign_toplevel_request_activate(
		struct wl_listener *listener, void *data) {
	struct uwm_toplevel *toplevel = wl_container_of(
		listener, toplevel, foreign_toplevel_request_activate);
	workspace_switch(toplevel->server, toplevel->workspace->id);
	focus_toplevel(toplevel);
}

static void handle_foreign_toplevel_request_close(
		struct wl_listener *listener, void *data) {
	struct uwm_toplevel *toplevel = wl_container_of(
		listener, toplevel, foreign_toplevel_request_close);
	toplevel_send_close(toplevel);
}

#if WLR_HAS_XWAYLAND
/* ========== XWayland helpers ========== */
static void xwayland_toplevel_map(struct wl_listener *listener, void *data) {
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, map);
	struct wlr_xwayland_surface *xs = toplevel->xwayland_surface;
	(void)data;
	wlr_log(WLR_DEBUG, "XWAYLAND MAP: title=%s class=%s", xs->title ? xs->title : "(nil)", xs->class ? xs->class : "(nil)");

	/* create scene surface if not already */
	if (!toplevel->scene_tree) return;
	if (xs->surface && !toplevel->scene_tree->children.next) {
		/* subsurface tree handles subsurfaces + damage */
		wlr_scene_subsurface_tree_create(toplevel->scene_tree, xs->surface);
	}

	wl_list_insert(&toplevel->server->toplevels, &toplevel->link);
	wl_list_insert(&toplevel->workspace->toplevels, &toplevel->workspace_link);

	if (toplevel->server->foreign_toplevel_list) {
		struct wlr_ext_foreign_toplevel_handle_v1_state st = {
			.title = xs->title, .app_id = xs->class,
		};
		toplevel->ext_foreign_toplevel = wlr_ext_foreign_toplevel_handle_v1_create(
			toplevel->server->foreign_toplevel_list, &st);
		if (toplevel->ext_foreign_toplevel) toplevel->ext_foreign_toplevel->data = toplevel;
	}
	if (toplevel->server->foreign_toplevel_manager) {
		toplevel->foreign_toplevel = wlr_foreign_toplevel_handle_v1_create(
			toplevel->server->foreign_toplevel_manager);
		if (toplevel->foreign_toplevel) {
			wlr_foreign_toplevel_handle_v1_set_title(toplevel->foreign_toplevel, xs->title);
			wlr_foreign_toplevel_handle_v1_set_app_id(toplevel->foreign_toplevel, xs->class);
			if (toplevel->workspace && toplevel->workspace->output)
				wlr_foreign_toplevel_handle_v1_output_enter(toplevel->foreign_toplevel, toplevel->workspace->output->wlr_output);
			toplevel->foreign_toplevel_request_activate.notify = handle_foreign_toplevel_request_activate;
			wl_signal_add(&toplevel->foreign_toplevel->events.request_activate, &toplevel->foreign_toplevel_request_activate);
			toplevel->foreign_toplevel_request_close.notify = handle_foreign_toplevel_request_close;
			wl_signal_add(&toplevel->foreign_toplevel->events.request_close, &toplevel->foreign_toplevel_request_close);
		}
	}

	int x, y, w, h;
	get_output_size(toplevel->workspace, &x, &y, &w, &h);
	/* override_redirect unmanaged always floating at xs->x,y */
	if (xs->override_redirect) {
		/* ensure subsurface tree exists */
		if (xs->surface && wl_list_empty(&toplevel->scene_tree->children))
			wlr_scene_subsurface_tree_create(toplevel->scene_tree, xs->surface);
		wlr_scene_node_reparent(&toplevel->scene_tree->node, toplevel->server->floating_layer);
		wlr_scene_node_set_position(&toplevel->scene_tree->node, xs->x, xs->y);
		/* track as floating but not in BSP — always ensure in floating list */
		toplevel->floating = true;
		toplevel->float_x = xs->x; toplevel->float_y = xs->y;
		toplevel->float_width = xs->width ? xs->width : toplevel->float_width;
		toplevel->float_height = xs->height ? xs->height : toplevel->float_height;
		wl_list_remove(&toplevel->workspace_link);
		wl_list_init(&toplevel->workspace_link);
		/* remove from floating_link if already there (no-op if empty) then insert */
		wl_list_remove(&toplevel->floating_link);
		wl_list_init(&toplevel->floating_link);
		wl_list_insert(&toplevel->workspace->floating_windows, &toplevel->floating_link);
		/* server toplevels list */
		if (wl_list_empty(&toplevel->link))
			wl_list_insert(&toplevel->server->toplevels, &toplevel->link);
		else {
			wl_list_remove(&toplevel->link);
			wl_list_insert(&toplevel->server->toplevels, &toplevel->link);
		}
		/* unmanaged still needs focus handling for override-redirect */
		if (wlr_xwayland_surface_override_redirect_wants_focus(xs)) {
			wlr_xwayland_set_seat(toplevel->server->xwayland, toplevel->server->seat);
			focus_toplevel(toplevel);
		}
		workspace_update_borders(toplevel->workspace);
		return;
	}
	if (!should_tile_toplevel(toplevel)) goto x_float;
	rule_apply_all(&toplevel->server->config, toplevel);
	if (toplevel->floating || toplevel->fullscreen) goto x_float;
	if (!toplevel->floating && !toplevel->fullscreen) bsp_insert(toplevel->workspace, toplevel);
	bsp_arrange(toplevel->workspace, x, y, w, h, toplevel->server->config.inner_gap);
	if (!toplevel->workspace->fullscreen_window || toplevel->workspace->fullscreen_window == toplevel) focus_toplevel(toplevel);
	if (toplevel->workspace != &toplevel->server->workspaces.workspaces[toplevel->server->workspaces.current]
			|| (toplevel->workspace->fullscreen_window && toplevel->workspace->fullscreen_window != toplevel))
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	return;
x_float:
	if (!toplevel->floating && !toplevel->fullscreen) {
		toplevel->float_width = (int)(w * floating_default_width_ratio);
		toplevel->float_height = (int)(h * floating_default_height_ratio);
		if (toplevel->float_width < floating_create_min_width) toplevel->float_width = floating_create_min_width;
		if (toplevel->float_height < floating_create_min_height) toplevel->float_height = floating_create_min_height;
		toplevel->float_x = (w - toplevel->float_width) / 2;
		toplevel->float_y = (h - toplevel->float_height) / 2;
		toplevel->floating = true;
		wl_list_remove(&toplevel->workspace_link);
		wl_list_init(&toplevel->workspace_link);
		wl_list_insert(&toplevel->workspace->floating_windows, &toplevel->floating_link);
		wlr_scene_node_reparent(&toplevel->scene_tree->node, toplevel->server->floating_layer);
		wlr_scene_node_set_position(&toplevel->scene_tree->node, toplevel->float_x, toplevel->float_y);
		wlr_xwayland_surface_configure(xs, toplevel->float_x, toplevel->float_y, toplevel->float_width, toplevel->float_height);
		toplevel_update_border(toplevel);
	}
	focus_toplevel(toplevel);
	workspace_update_borders(toplevel->workspace);
}

static void xwayland_toplevel_unmap(struct wl_listener *listener, void *data) {
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
	if (toplevel->ext_foreign_toplevel) { wlr_ext_foreign_toplevel_handle_v1_destroy(toplevel->ext_foreign_toplevel); toplevel->ext_foreign_toplevel = NULL; }
	if (toplevel->foreign_toplevel) {
		wl_list_remove(&toplevel->foreign_toplevel_request_activate.link);
		wl_list_remove(&toplevel->foreign_toplevel_request_close.link);
		wlr_foreign_toplevel_handle_v1_destroy(toplevel->foreign_toplevel);
		toplevel->foreign_toplevel = NULL;
	}
	struct wlr_seat *seat = toplevel->server->seat;
	if (toplevel == toplevel->server->grabbed_toplevel) reset_cursor_mode(toplevel->server);
	wl_list_remove(&toplevel->link); wl_list_init(&toplevel->link);
	wl_list_remove(&toplevel->workspace_link); wl_list_init(&toplevel->workspace_link);
	struct uwm_workspace *ws = toplevel->workspace;
	struct uwm_toplevel *sib = NULL;
	if (ws->root && !toplevel->floating) {
		struct uwm_bsp_node *leaf = bsp_find_leaf(ws->root, toplevel);
		if (leaf && leaf->parent) {
			struct uwm_bsp_node *s = (leaf->parent->first == leaf) ? leaf->parent->second : leaf->parent->first;
			sib = s->toplevel;
		}
	}
	bsp_remove(ws, toplevel);
	int x, y, w, h; get_output_size(ws, &x, &y, &w, &h);
	bool was_fs = toplevel->fullscreen;
	if (was_fs) {
		toplevel->fullscreen = false; ws->fullscreen_window = NULL;
		struct uwm_toplevel *tl; wl_list_for_each(tl, &ws->toplevels, workspace_link) wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
		wl_list_for_each(tl, &ws->floating_windows, floating_link) wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
		if (ws->output) { wlr_scene_node_set_enabled(&ws->output->layer_top->node, true); wlr_scene_node_set_enabled(&ws->output->layer_overlay->node, true); }
	}
	if (ws->last_focused == toplevel) ws->last_focused = NULL;
	bool displaced = (ws->focused == toplevel);
	if (displaced) {
		ws->focused = NULL;
		if (sib) ws->focused = sib;
		if (!ws->focused) { struct uwm_toplevel *c; wl_list_for_each(c, &ws->toplevels, workspace_link) { ws->focused = c; break; } }
		if (!ws->focused && !wl_list_empty(&ws->floating_windows)) ws->focused = wl_container_of(ws->floating_windows.next, ws->focused, floating_link);
	}
	bool will_exit = false;
	if (ws->monocle) {
		int cnt = 0; struct uwm_toplevel *_tl; wl_list_for_each(_tl, &ws->toplevels, workspace_link) cnt++;
		if (cnt <= 1) will_exit = true;
	}
	if (will_exit) ws->monocle = false;
	if (ws->root) bsp_arrange(ws, x, y, w, h, toplevel->server->config.inner_gap);
	if (will_exit && ws->root) set_children_visible(ws->root, true);
	workspace_update_borders(ws);
	if (displaced && ws->focused) focus_toplevel(ws->focused);
	else if (!ws->focused) wlr_seat_keyboard_notify_clear_focus(seat);
}

static void xwayland_toplevel_commit(struct wl_listener *listener, void *data) {
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, commit);
	struct wlr_xwayland_surface *xs = toplevel->xwayland_surface;
	toplevel_update_border(toplevel);
	if (!xs || !xs->surface) return;
	const char *cur_title = xs->title;
	const char *cur_app = xs->class;
	bool title_changed = (cur_title == NULL && toplevel->last_title != NULL) ||
		(cur_title != NULL && toplevel->last_title == NULL) ||
		(cur_title && toplevel->last_title && strcmp(cur_title, toplevel->last_title) != 0);
	bool app_changed = (cur_app == NULL && toplevel->last_app_id != NULL) ||
		(cur_app != NULL && toplevel->last_app_id == NULL) ||
		(cur_app && toplevel->last_app_id && strcmp(cur_app, toplevel->last_app_id) != 0);
	if (title_changed || app_changed) {
		free(toplevel->last_title); toplevel->last_title = cur_title ? strdup(cur_title) : NULL;
		free(toplevel->last_app_id); toplevel->last_app_id = cur_app ? strdup(cur_app) : NULL;
		if (toplevel->ext_foreign_toplevel) {
			struct wlr_ext_foreign_toplevel_handle_v1_state st = { .title = cur_title, .app_id = cur_app };
			wlr_ext_foreign_toplevel_handle_v1_update_state(toplevel->ext_foreign_toplevel, &st);
		}
		if (toplevel->foreign_toplevel) {
			wlr_foreign_toplevel_handle_v1_set_title(toplevel->foreign_toplevel, cur_title);
			wlr_foreign_toplevel_handle_v1_set_app_id(toplevel->foreign_toplevel, cur_app);
		}
	}
	toplevel_update_border(toplevel);
}

static void xwayland_toplevel_destroy(struct wl_listener *listener, void *data) {
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);
	if (toplevel->ext_foreign_toplevel) { wlr_ext_foreign_toplevel_handle_v1_destroy(toplevel->ext_foreign_toplevel); toplevel->ext_foreign_toplevel = NULL; }
	if (toplevel->foreign_toplevel) {
		wl_list_remove(&toplevel->foreign_toplevel_request_activate.link);
		wl_list_remove(&toplevel->foreign_toplevel_request_close.link);
		wlr_foreign_toplevel_handle_v1_destroy(toplevel->foreign_toplevel); toplevel->foreign_toplevel = NULL;
	}
	wl_list_remove(&toplevel->map.link); wl_list_remove(&toplevel->unmap.link);
	wl_list_remove(&toplevel->commit.link); wl_list_remove(&toplevel->destroy.link);
	wl_list_remove(&toplevel->request_move.link); wl_list_remove(&toplevel->request_resize.link);
	wl_list_remove(&toplevel->request_maximize.link); wl_list_remove(&toplevel->request_fullscreen.link);
	wl_list_remove(&toplevel->xwayland_activate.link);
	wl_list_remove(&toplevel->xwayland_configure.link);
	wl_list_remove(&toplevel->xwayland_set_geometry.link);
	wl_list_remove(&toplevel->associate.link);
	wl_list_remove(&toplevel->dissociate.link);
	wl_list_remove(&toplevel->override_redirect.link);
	if (toplevel->image_capture_scene) { wlr_scene_node_destroy(&toplevel->image_capture_scene->tree.node); toplevel->image_capture_scene = NULL; }
	free(toplevel->last_title);
	free(toplevel->last_app_id);
	if (toplevel == toplevel->server->grabbed_toplevel) reset_cursor_mode(toplevel->server);
	struct uwm_workspace *ws = toplevel->workspace;
	if (ws->last_focused == toplevel) ws->last_focused = NULL;
	if (ws->focused == toplevel) ws->focused = NULL;
	if (ws->fullscreen_window == toplevel) ws->fullscreen_window = NULL;
	wl_list_remove(&toplevel->link); wl_list_init(&toplevel->link);
	wl_list_remove(&toplevel->workspace_link); wl_list_init(&toplevel->workspace_link);
	if (ws->root && !toplevel->floating && !toplevel->fullscreen) {
		struct uwm_bsp_node *leaf = bsp_find_leaf(ws->root, toplevel);
		if (leaf) { bsp_remove(ws, toplevel); int x,y,w,h; get_output_size(ws,&x,&y,&w,&h); bsp_arrange(ws,x,y,w,h,toplevel->server->config.inner_gap); }
	}
	if (toplevel->server->bar_manager && toplevel->server->active_output) uwm_bar_send_output(toplevel->server->active_output);
	struct uwm_output *output; wl_list_for_each(output, &toplevel->server->outputs, link) if (output->bar_focused_toplevel == toplevel) output->bar_focused_toplevel = NULL;
	toplevel_destroy_border(toplevel);
	if (toplevel->scene_tree) wlr_scene_node_destroy(&toplevel->scene_tree->node);
	free(toplevel);
}

static void xwayland_handle_request_activate(struct wl_listener *listener, void *data) {
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, xwayland_activate);
	struct wlr_xwayland_surface *xs = toplevel->xwayland_surface;
	if (!xs || !xs->surface || !xs->surface->mapped) return;
	workspace_switch(toplevel->server, toplevel->workspace->id);
	focus_toplevel(toplevel);
	wlr_xwayland_surface_restack(xs, NULL, XCB_STACK_MODE_ABOVE);
}
static void xwayland_handle_request_configure(struct wl_listener *listener, void *data) {
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, xwayland_configure);
	struct wlr_xwayland_surface_configure_event *ev = data;
	struct wlr_xwayland_surface *xs = toplevel->xwayland_surface;
	if (!xs || !xs->surface || !xs->surface->mapped) {
		wlr_xwayland_surface_configure(xs, ev->x, ev->y, ev->width, ev->height);
		return;
	}
	if (toplevel->floating) {
		wlr_xwayland_surface_configure(xs, ev->x, ev->y, ev->width, ev->height);
		toplevel->float_x = ev->x; toplevel->float_y = ev->y;
		toplevel->float_width = ev->width; toplevel->float_height = ev->height;
		wlr_scene_node_set_position(&toplevel->scene_tree->node, ev->x, ev->y);
		toplevel_update_border(toplevel);
	} else {
		/* tiled: ignore client configure, force layout geometry */
		int x,y,w,h; get_output_size(toplevel->workspace,&x,&y,&w,&h);
		bsp_arrange(toplevel->workspace, x,y,w,h, toplevel->server->config.inner_gap);
	}
}
static void xwayland_handle_set_geometry(struct wl_listener *listener, void *data) {
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, xwayland_set_geometry);
	(void)data;
	if (toplevel->xwayland_surface && toplevel->scene_tree) {
		wlr_scene_node_set_position(&toplevel->scene_tree->node, toplevel->xwayland_surface->x, toplevel->xwayland_surface->y);
		toplevel_update_border(toplevel);
	}
}
static void xwayland_handle_request_move(struct wl_listener *listener, void *data) {
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
	(void)data;
	if (!toplevel->xwayland_surface || !toplevel->xwayland_surface->surface || !toplevel->xwayland_surface->surface->mapped) return;
	begin_interactive(toplevel, UWM_CURSOR_MOVE, 0);
}
static void xwayland_handle_request_resize(struct wl_listener *listener, void *data) {
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
	struct wlr_xwayland_resize_event *ev = data;
	if (!toplevel->xwayland_surface || !toplevel->xwayland_surface->surface || !toplevel->xwayland_surface->surface->mapped) return;
	begin_interactive(toplevel, UWM_CURSOR_RESIZE, ev->edges);
}
static void xwayland_handle_request_maximize(struct wl_listener *listener, void *data) {
	(void)listener; (void)data;
}
static void xwayland_handle_request_fullscreen(struct wl_listener *listener, void *data) {
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, request_fullscreen);
	(void)data;
	if (!toplevel->xwayland_surface || !toplevel->xwayland_surface->surface || !toplevel->xwayland_surface->surface->mapped) return;
	bool fs = toplevel->xwayland_surface->fullscreen;
	if (fs != toplevel->fullscreen) toggle_fullscreen(toplevel);
}

static void xwayland_handle_associate(struct wl_listener *listener, void *data) {
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, associate);
	struct wlr_xwayland_surface *xs = toplevel->xwayland_surface;
	(void)data;
	if (!xs->surface) return;
	/* create subsurface tree for the wl_surface */
	if (toplevel->scene_tree && wl_list_empty(&toplevel->scene_tree->children))
		wlr_scene_subsurface_tree_create(toplevel->scene_tree, xs->surface);
	wl_signal_add(&xs->surface->events.map, &toplevel->map);
	toplevel->map.notify = xwayland_toplevel_map;
	wl_signal_add(&xs->surface->events.unmap, &toplevel->unmap);
	toplevel->unmap.notify = xwayland_toplevel_unmap;
	wl_signal_add(&xs->surface->events.commit, &toplevel->commit);
	toplevel->commit.notify = xwayland_toplevel_commit;
	if (xs->surface->mapped)
		xwayland_toplevel_map(&toplevel->map, NULL);
}

static void xwayland_handle_dissociate(struct wl_listener *listener, void *data) {
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, dissociate);
	(void)data;
	wl_list_remove(&toplevel->map.link);
	wl_list_init(&toplevel->map.link);
	wl_list_remove(&toplevel->unmap.link);
	wl_list_init(&toplevel->unmap.link);
	wl_list_remove(&toplevel->commit.link);
	wl_list_init(&toplevel->commit.link);
}

static void xwayland_handle_override_redirect(struct wl_listener *listener, void *data) {
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, override_redirect);
	struct wlr_xwayland_surface *xs = toplevel->xwayland_surface;
	struct uwm_server *server = toplevel->server;
	(void)data;
	bool mapped = xs->surface && xs->surface->mapped;
	bool associated = xs->surface != NULL;
	if (mapped)
		xwayland_toplevel_unmap(&toplevel->unmap, NULL);
	if (associated)
		xwayland_handle_dissociate(&toplevel->dissociate, NULL);
	/* destroy current toplevel and recreate as opposite type (managed <-> unmanaged) */
	wl_list_remove(&toplevel->associate.link);
	wl_list_remove(&toplevel->dissociate.link);
	wl_list_remove(&toplevel->destroy.link);
	wl_list_remove(&toplevel->override_redirect.link);
	wl_list_remove(&toplevel->xwayland_activate.link);
	wl_list_remove(&toplevel->xwayland_configure.link);
	wl_list_remove(&toplevel->xwayland_set_geometry.link);
	wl_list_remove(&toplevel->request_move.link);
	wl_list_remove(&toplevel->request_resize.link);
	wl_list_remove(&toplevel->request_maximize.link);
	wl_list_remove(&toplevel->request_fullscreen.link);
	if (toplevel->scene_tree)
		wlr_scene_node_destroy(&toplevel->scene_tree->node);
	xs->data = NULL;
	free(toplevel);
	/* re-create with correct handling (managed vs unmanaged handled in new_surface) */
	server_new_xwayland_surface(&server->xwayland_surface, xs);
}

void server_new_xwayland_surface(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, xwayland_surface);
	struct wlr_xwayland_surface *xs = data;
	/* override_redirect -> unmanaged (floating, not tiled, on floating_layer) */
	bool unmanaged = xs->override_redirect;
	struct uwm_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	if (!toplevel) return;
	toplevel->type = UWM_TOPLEVEL_XWAYLAND;
	toplevel->server = server;
	toplevel->xwayland_surface = xs;
	toplevel->workspace = &server->workspaces.workspaces[server->workspaces.current];
	wl_list_init(&toplevel->link); wl_list_init(&toplevel->workspace_link);
	wl_list_init(&toplevel->map.link); wl_list_init(&toplevel->unmap.link); wl_list_init(&toplevel->commit.link);
	xs->data = toplevel;
	/* scene tree: unmanaged goes to floating_layer at xs->x/y, managed to tiled_layer */
	if (unmanaged) {
		toplevel->scene_tree = wlr_scene_tree_create(server->floating_layer);
		toplevel->floating = true; /* unmanaged always floating */
	} else {
		toplevel->scene_tree = wlr_scene_tree_create(server->tiled_layer);
	}
	if (!toplevel->scene_tree) { free(toplevel); xs->data = NULL; return; }
	toplevel->scene_tree->node.data = toplevel;
	toplevel_create_border(toplevel);
	if (unmanaged && xs->surface) {
		/* position unmanaged at requested x/y */
		wlr_scene_node_set_position(&toplevel->scene_tree->node, xs->x, xs->y);
	}

	/* core xwayland signals — always listen on xs */
	toplevel->destroy.notify = xwayland_toplevel_destroy;
	wl_signal_add(&xs->events.destroy, &toplevel->destroy);
	toplevel->associate.notify = xwayland_handle_associate;
	wl_signal_add(&xs->events.associate, &toplevel->associate);
	toplevel->dissociate.notify = xwayland_handle_dissociate;
	wl_signal_add(&xs->events.dissociate, &toplevel->dissociate);
	toplevel->override_redirect.notify = xwayland_handle_override_redirect;
	wl_signal_add(&xs->events.set_override_redirect, &toplevel->override_redirect);
	toplevel->xwayland_activate.notify = xwayland_handle_request_activate;
	wl_signal_add(&xs->events.request_activate, &toplevel->xwayland_activate);
	toplevel->xwayland_configure.notify = xwayland_handle_request_configure;
	wl_signal_add(&xs->events.request_configure, &toplevel->xwayland_configure);
	toplevel->xwayland_set_geometry.notify = xwayland_handle_set_geometry;
	wl_signal_add(&xs->events.set_geometry, &toplevel->xwayland_set_geometry);
	toplevel->request_move.notify = xwayland_handle_request_move;
	wl_signal_add(&xs->events.request_move, &toplevel->request_move);
	toplevel->request_resize.notify = xwayland_handle_request_resize;
	wl_signal_add(&xs->events.request_resize, &toplevel->request_resize);
	toplevel->request_maximize.notify = xwayland_handle_request_maximize;
	wl_signal_add(&xs->events.request_maximize, &toplevel->request_maximize);
	toplevel->request_fullscreen.notify = xwayland_handle_request_fullscreen;
	wl_signal_add(&xs->events.request_fullscreen, &toplevel->request_fullscreen);

	/* if surface already associated, wire map/unmap/commit now */
	if (xs->surface) {
		xwayland_handle_associate(&toplevel->associate, NULL);
	}
}
#endif

static void xdg_popup_commit(struct wl_listener *listener, void *data) {
	struct uwm_popup *popup = wl_container_of(listener, popup, commit);

	if (popup->xdg_popup->base->initial_commit) {
		wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
	}
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
	struct uwm_popup *popup = wl_container_of(listener, popup, destroy);

	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->destroy.link);

	free(popup);
}

void server_new_xdg_popup(struct wl_listener *listener, void *data) {
	struct wlr_xdg_popup *xdg_popup = data;

	struct uwm_popup *popup = calloc(1, sizeof(*popup));
	if (!popup)
		return;
	popup->xdg_popup = xdg_popup;

	/* Wire listeners FIRST so the client always receives a configure
	 * event on commit. If scene-tree setup fails below, the popup
	 * won't render but the client will not hang. */
	popup->commit.notify = xdg_popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

	popup->destroy.notify = xdg_popup_destroy;
	wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);

	struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
	if (!parent) {
		wl_list_remove(&popup->commit.link);
		wl_list_remove(&popup->destroy.link);
		free(popup);
		return;
	}
	struct wlr_scene_tree *parent_tree = parent->data;
	if (!parent_tree) {
		wl_list_remove(&popup->commit.link);
		wl_list_remove(&popup->destroy.link);
		free(popup);
		return;
	}
	xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);
	if (!xdg_popup->base->data) {
		wl_list_remove(&popup->commit.link);
		wl_list_remove(&popup->destroy.link);
		free(popup);
		return;
	}

	/* Find the parent toplevel and its output, then unconstrain the
	 * popup to the output's bounds so context menus near the screen
	 * edge are repositioned by wlroots to stay on-screen. */
	struct uwm_toplevel *toplevel = NULL;
	struct wlr_scene_tree *tree = parent_tree;
	while (tree && !tree->node.data)
		tree = tree->node.parent;
	if (tree)
		toplevel = tree->node.data;
	if (toplevel && toplevel->workspace && toplevel->workspace->output) {
		struct uwm_output *output = toplevel->workspace->output;
		struct wlr_box box = {
			.x = output->lx,
			.y = output->ly,
			.width = output->wlr_output->width,
			.height = output->wlr_output->height,
		};
		wlr_xdg_popup_unconstrain_from_box(xdg_popup, &box);
	}
}

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

void focus_toplevel(struct uwm_toplevel *toplevel) {
	if (toplevel == NULL) {
		return;
	}
	struct uwm_server *server = toplevel->server;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	struct wlr_surface *surface = toplevel_surface(toplevel);
	if (prev_surface == surface) {
		return;
	}

	const char *new_app_id = toplevel_app_id(toplevel);
	const char *new_title = toplevel_title(toplevel);
	wlr_log(WLR_DEBUG, "FOCUS: new app_id=%s title=%s",
		new_app_id ? new_app_id : "(nil)", new_title ? new_title : "(nil)");
	if (prev_surface) {
		struct wlr_xdg_toplevel *prev_toplevel = wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
		if (prev_toplevel != NULL) {
			wlr_xdg_toplevel_set_activated(prev_toplevel, false);
			struct wlr_scene_tree *prev_tree = prev_toplevel->base->data;
			if (prev_tree) {
				struct uwm_toplevel *prev = prev_tree->node.data;
				if (prev) toplevel_set_activated(prev, false);
			}
		}
#if WLR_HAS_XWAYLAND
		else {
			struct wlr_xwayland_surface *prev_xs = wlr_xwayland_surface_try_from_wlr_surface(prev_surface);
			if (prev_xs) wlr_xwayland_surface_activate(prev_xs, false);
		}
#endif
	}

	struct uwm_workspace *ws = toplevel->workspace;
	ws->last_focused = ws->focused;
	ws->focused = toplevel;

	/* Update active output to the one displaying this workspace */
	if (ws->output) {
		server->active_output = ws->output;
	}

	/* Update monocle container active_child */
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

	if (ws->monocle) {
		struct uwm_toplevel *old = ws->last_focused;
		if (old && old != toplevel
				&& !old->floating && !old->fullscreen) {
			wlr_scene_node_set_enabled(&old->scene_tree->node, false);
			if (!toplevel->floating && !toplevel->fullscreen)
				wlr_scene_node_set_enabled(
					&toplevel->scene_tree->node, true);
		} else {
			struct uwm_toplevel *tl;
			wl_list_for_each(tl, &ws->toplevels, workspace_link) {
				if (!tl->floating && !tl->fullscreen)
					wlr_scene_node_set_enabled(&tl->scene_tree->node,
						tl == toplevel);
			}
		}
	if (ws->output) {
			struct uwm_output *o = ws->output;
			int ogap = o->server->config.outer_gap;
			int x = o->lx + o->usable_area.x + ogap;
			int y = o->ly + o->usable_area.y + ogap;
			int w = o->usable_area.width - 2 * ogap;
			int h = o->usable_area.height - 2 * ogap;
			wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);
			struct wlr_box cur = toplevel_geometry(toplevel);
			if (cur.width != w || cur.height != h)
				toplevel_set_size(toplevel, w, h);
		}
	}

	if (toplevel->floating) {
		raise_floating(toplevel);
	} else {
		wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
	}

	wl_list_remove(&toplevel->link);
	wl_list_insert(&server->toplevels, &toplevel->link);
	toplevel_set_activated(toplevel, true);
	if (toplevel->decoration && toplevel->type == UWM_TOPLEVEL_XDG) {
		wlr_xdg_toplevel_decoration_v1_set_mode(toplevel->decoration,
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
#if WLR_HAS_XWAYLAND
	/* Sway parity: seat_send_focus (input/seat.c:194) sets the XWayland seat
	 * on every XWayland focus so the XWM receives the current keymap. UWM
	 * previously only did this for override-redirect (xwayland_toplevel_map:870),
	 * leaving the X server without a keymap → “Failed to compile keymap”. */
	if (toplevel->type == UWM_TOPLEVEL_XWAYLAND && server->xwayland) {
		wlr_xwayland_set_seat(server->xwayland, server->seat);
	}
#endif
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	if (keyboard != NULL) {
		wlr_log(WLR_DEBUG, "KEYBOARD_ENTER: surface=%p app_id=%s title=%s",
			(void *)surface,
			toplevel_app_id(toplevel) ? toplevel_app_id(toplevel) : "(nil)",
			toplevel_title(toplevel) ? toplevel_title(toplevel) : "(nil)");
		wlr_seat_keyboard_notify_enter(seat, surface,
			keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	}

	/* Warp cursor to center of window if not already inside, then update
	 * pointer focus so scroll events go to the newly focused window without
	 * requiring mouse motion. When the cursor is already inside the window,
	 * pointer focus already tracks it (real motion keeps it current), so skip
	 * the scene lookup and enter re-delivery.
	 * Skip during interactive move/resize to avoid fighting the user. */
	/* 1.5 fix: don't warp on every focus when focus_follows_pointer.
	 * Warp is surprising and doubles motion events. Only warp when
	 * focus_follows_pointer is off (explicit keyboard focus). */
	if (server->cursor_mode == UWM_CURSOR_PASSTHROUGH && !ws->focus_follows_pointer) {
		struct wlr_box geo = toplevel_geometry(toplevel);
		double wx = toplevel->scene_tree->node.x + geo.x;
		double wy = toplevel->scene_tree->node.y + geo.y;
		double ww = geo.width;
		double wh = geo.height;
		bool inside = ww > 0 && wh > 0 &&
			server->cursor->x >= wx && server->cursor->x < wx + ww &&
			server->cursor->y >= wy && server->cursor->y < wy + wh;
		if (!inside) {
			if (ww > 0 && wh > 0) {
				wlr_cursor_warp(server->cursor, NULL,
					wx + ww / 2.0, wy + wh / 2.0);
			}

			double sx, sy;
			struct wlr_surface *surface = NULL;
			desktop_toplevel_at(server, server->cursor->x, server->cursor->y,
				&surface, &sx, &sy);
			if (surface) {
				wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
			}
		}
	}

	/* Notify bar clients about focus change.
	 * Skip during move/resize to batch updates. */
	if (server->cursor_mode == UWM_CURSOR_PASSTHROUGH) {
		uwm_bar_notify_focus(server, toplevel);
	}
	/* update borders for old and new workspace */
	workspace_update_borders(toplevel->workspace);
}

struct uwm_toplevel *desktop_toplevel_at(
		struct uwm_server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
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

	struct wlr_scene_tree *tree = node->parent;
	while (tree != NULL && tree->node.data == NULL) {
		tree = tree->node.parent;
	}
	if (!tree)
		return NULL;
	struct uwm_toplevel *result = tree->node.data;
	return result;
}

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

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, map);

	const char *app_id = toplevel->xdg_toplevel->app_id;
	const char *title = toplevel->xdg_toplevel->title;
	struct wlr_seat *seat = toplevel->server->seat;
	wlr_log(WLR_DEBUG, "MAP: app_id=%s title=%s kb_focus_before=%p",
		app_id ? app_id : "(nil)", title ? title : "(nil)",
		(void *)seat->keyboard_state.focused_surface);

	wl_list_insert(&toplevel->server->toplevels, &toplevel->link);
	wl_list_insert(&toplevel->workspace->toplevels, &toplevel->workspace_link);

	if (toplevel->server->foreign_toplevel_list) {
		struct wlr_ext_foreign_toplevel_handle_v1_state state = {
			.title = toplevel->xdg_toplevel->title,
			.app_id = toplevel->xdg_toplevel->app_id,
		};
		toplevel->ext_foreign_toplevel =
			wlr_ext_foreign_toplevel_handle_v1_create(
				toplevel->server->foreign_toplevel_list, &state);
		toplevel->ext_foreign_toplevel->data = toplevel;
	}

	if (toplevel->server->foreign_toplevel_manager) {
		toplevel->foreign_toplevel =
			wlr_foreign_toplevel_handle_v1_create(
				toplevel->server->foreign_toplevel_manager);
		wlr_foreign_toplevel_handle_v1_set_title(
			toplevel->foreign_toplevel, toplevel->xdg_toplevel->title);
		wlr_foreign_toplevel_handle_v1_set_app_id(
			toplevel->foreign_toplevel, toplevel->xdg_toplevel->app_id);
		if (toplevel->workspace && toplevel->workspace->output) {
			wlr_foreign_toplevel_handle_v1_output_enter(
				toplevel->foreign_toplevel,
				toplevel->workspace->output->wlr_output);
		}
		toplevel->foreign_toplevel_request_activate.notify =
			handle_foreign_toplevel_request_activate;
		wl_signal_add(&toplevel->foreign_toplevel->events.request_activate,
			&toplevel->foreign_toplevel_request_activate);
		toplevel->foreign_toplevel_request_close.notify =
			handle_foreign_toplevel_request_close;
		wl_signal_add(&toplevel->foreign_toplevel->events.request_close,
			&toplevel->foreign_toplevel_request_close);
	}

	int x, y, w, h;
	get_output_size(toplevel->workspace, &x, &y, &w, &h);

	if (!should_tile_toplevel(toplevel)) {
		goto float_window;
	}

	struct uwm_workspace *current = &toplevel->server->workspaces.workspaces[toplevel->server->workspaces.current];

	rule_apply_all(&toplevel->server->config, toplevel);

	if (toplevel->floating || toplevel->fullscreen)
		goto float_window;

	if (!toplevel->floating && !toplevel->fullscreen) {
		bsp_insert(toplevel->workspace, toplevel);
	}

	bsp_arrange(toplevel->workspace, x, y, w, h, toplevel->server->config.inner_gap);

	if (!toplevel->workspace->fullscreen_window
			|| toplevel->workspace->fullscreen_window == toplevel) {
		focus_toplevel(toplevel);
	}

	if (toplevel->workspace != current
			|| (toplevel->workspace->fullscreen_window
				&& toplevel->workspace->fullscreen_window != toplevel)) {
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	}
	return;

float_window:
	/* Float dialogs, transients, and rule-floated windows */
	if (!toplevel->floating && !toplevel->fullscreen) {
		toplevel->float_width = (int)(w * floating_default_width_ratio);
		toplevel->float_height = (int)(h * floating_default_height_ratio);
		if (toplevel->float_width < floating_create_min_width)
			toplevel->float_width = floating_create_min_width;
		if (toplevel->float_height < floating_create_min_height)
			toplevel->float_height = floating_create_min_height;
		toplevel->float_x = (w - toplevel->float_width) / 2;
		toplevel->float_y = (h - toplevel->float_height) / 2;

		toplevel->floating = true;
		wl_list_remove(&toplevel->workspace_link);
		wl_list_init(&toplevel->workspace_link);
		wl_list_insert(&toplevel->workspace->floating_windows,
			&toplevel->floating_link);

		wlr_scene_node_reparent(&toplevel->scene_tree->node,
			toplevel->server->floating_layer);
		wlr_scene_node_set_position(&toplevel->scene_tree->node,
			toplevel->float_x, toplevel->float_y);
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
			toplevel->float_width, toplevel->float_height);
	}

	focus_toplevel(toplevel);
	workspace_update_borders(toplevel->workspace);
	return;
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);

	if (toplevel->ext_foreign_toplevel) {
		wlr_ext_foreign_toplevel_handle_v1_destroy(toplevel->ext_foreign_toplevel);
		toplevel->ext_foreign_toplevel = NULL;
	}

	if (toplevel->foreign_toplevel) {
		wl_list_remove(&toplevel->foreign_toplevel_request_activate.link);
		wl_list_remove(&toplevel->foreign_toplevel_request_close.link);
		wlr_foreign_toplevel_handle_v1_destroy(toplevel->foreign_toplevel);
		toplevel->foreign_toplevel = NULL;
	}

	const char *app_id = toplevel->xdg_toplevel->app_id;
	const char *title = toplevel->xdg_toplevel->title;
	struct wlr_seat *seat = toplevel->server->seat;
	wlr_log(WLR_DEBUG, "UNMAP: app_id=%s title=%s kb_focused_surface=%p",
		app_id ? app_id : "(nil)", title ? title : "(nil)",
		(void *)seat->keyboard_state.focused_surface);

	if (toplevel == toplevel->server->grabbed_toplevel) {
		reset_cursor_mode(toplevel->server);
	}

	wl_list_remove(&toplevel->link);
	wl_list_init(&toplevel->link);
	wl_list_remove(&toplevel->workspace_link);
	wl_list_init(&toplevel->workspace_link);

	struct uwm_workspace *ws = toplevel->workspace;

	struct uwm_toplevel *bsp_sibling = NULL;
	if (ws->root && !toplevel->floating) {
		struct uwm_bsp_node *leaf = bsp_find_leaf(ws->root, toplevel);
		if (leaf && leaf->parent) {
			struct uwm_bsp_node *sib = (leaf->parent->first == leaf)
				? leaf->parent->second : leaf->parent->first;
			bsp_sibling = sib->toplevel;
		}
	}

	bsp_remove(ws, toplevel);

	int x, y, w, h;
	get_output_size(ws, &x, &y, &w, &h);

	bool was_fullscreen = toplevel->fullscreen;
	if (was_fullscreen) {
		toplevel->fullscreen = false;
		ws->fullscreen_window = NULL;
		struct uwm_toplevel *tl;
		wl_list_for_each(tl, &ws->toplevels, workspace_link) {
			wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
		}
		wl_list_for_each(tl, &ws->floating_windows, floating_link) {
			wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
		}
		if (ws->output) {
			wlr_scene_node_set_enabled(&ws->output->layer_top->node, true);
			wlr_scene_node_set_enabled(&ws->output->layer_overlay->node, true);
		}
	}

	if (ws->last_focused == toplevel)
		ws->last_focused = NULL;

	bool focus_was_displaced = (ws->focused == toplevel);
	if (focus_was_displaced) {
		ws->focused = NULL;
		if (bsp_sibling) ws->focused = bsp_sibling;
		if (!ws->focused) {
			struct uwm_toplevel *candidate;
			wl_list_for_each(candidate, &ws->toplevels, workspace_link) { ws->focused = candidate; break; }
		}
		if (!ws->focused && !wl_list_empty(&ws->floating_windows)) {
			struct uwm_toplevel *candidate = wl_container_of(ws->floating_windows.next, candidate, floating_link);
			ws->focused = candidate;
		}
	}

	/* 1.10 fix: coalesce bsp_arrange to single call after fullscreen/monocle state settled */
	bool will_exit_monocle = false;
	if (ws->monocle) {
		int tiled_count = 0;
		struct uwm_toplevel *_tl;
		wl_list_for_each(_tl, &ws->toplevels, workspace_link) tiled_count++;
		if (tiled_count <= 1) will_exit_monocle = true;
	}
	if (will_exit_monocle) ws->monocle = false;
	/* single arrange */
	if (ws->root) bsp_arrange(ws, x, y, w, h, toplevel->server->config.inner_gap);
	if (will_exit_monocle && ws->root) set_children_visible(ws->root, true);
	else if (ws->monocle && focus_was_displaced && ws->focused) {
		/* monocle with many windows: ensure focused visible; bsp_arrange already done */
	}

	if (focus_was_displaced && ws->focused) {
		wlr_log(WLR_DEBUG, "UNMAP: restoring focus to app_id=%s title=%s",
			toplevel_app_id(ws->focused) ? toplevel_app_id(ws->focused) : "(nil)",
			toplevel_title(ws->focused) ? toplevel_title(ws->focused) : "(nil)");
		focus_toplevel(ws->focused);
	} else if (!ws->focused) {
		wlr_log(WLR_DEBUG, "UNMAP: no focused window, clearing keyboard focus");
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
	if (!toplevel->xdg_toplevel->base->initialized) {
		return;
	}
	wlr_xdg_toplevel_decoration_v1_set_mode(toplevel->decoration,
		WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, commit);
	toplevel_update_border(toplevel);

	if (toplevel->xdg_toplevel->base->initial_commit) {
		if (toplevel->decoration) {
			wlr_xdg_toplevel_decoration_v1_set_mode(toplevel->decoration,
				WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
		}
		wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
	}

	const char *cur_title = toplevel->xdg_toplevel->title;
	const char *cur_app = toplevel->xdg_toplevel->app_id;
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
			struct wlr_ext_foreign_toplevel_handle_v1_state state = { .title = cur_title, .app_id = cur_app };
			wlr_ext_foreign_toplevel_handle_v1_update_state(toplevel->ext_foreign_toplevel, &state);
		}
		if (toplevel->foreign_toplevel) {
			wlr_foreign_toplevel_handle_v1_set_title(toplevel->foreign_toplevel, cur_title);
			wlr_foreign_toplevel_handle_v1_set_app_id(toplevel->foreign_toplevel, cur_app);
		}
	}
	toplevel_update_border(toplevel);
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
	struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);

	if (toplevel->ext_foreign_toplevel) {
		wlr_ext_foreign_toplevel_handle_v1_destroy(toplevel->ext_foreign_toplevel);
		toplevel->ext_foreign_toplevel = NULL;
	}

	if (toplevel->foreign_toplevel) {
		wl_list_remove(&toplevel->foreign_toplevel_request_activate.link);
		wl_list_remove(&toplevel->foreign_toplevel_request_close.link);
		wlr_foreign_toplevel_handle_v1_destroy(toplevel->foreign_toplevel);
		toplevel->foreign_toplevel = NULL;
	}

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

	if (toplevel->image_capture_scene) {
		wlr_scene_node_destroy(&toplevel->image_capture_scene->tree.node);
		toplevel->image_capture_scene = NULL;
	}
	free(toplevel->last_title);
	free(toplevel->last_app_id);

	if (toplevel == toplevel->server->grabbed_toplevel)
		reset_cursor_mode(toplevel->server);

	struct uwm_workspace *ws = toplevel->workspace;
	if (ws->last_focused == toplevel)
		ws->last_focused = NULL;
	if (ws->focused == toplevel)
		ws->focused = NULL;
	if (ws->fullscreen_window == toplevel)
		ws->fullscreen_window = NULL;

	/* Remove from all lists (safe if already removed by unmap) */
	wl_list_remove(&toplevel->link);
	wl_list_init(&toplevel->link);
	wl_list_remove(&toplevel->workspace_link);
	wl_list_init(&toplevel->workspace_link);

	/* If unmap was not called (e.g. force-close), clean up BSP tree */
	if (ws->root && !toplevel->floating && !toplevel->fullscreen) {
		struct uwm_bsp_node *leaf = bsp_find_leaf(ws->root, toplevel);
		if (leaf) {
			bsp_remove(ws, toplevel);
			int x, y, w, h;
			get_output_size(ws, &x, &y, &w, &h);
			bsp_arrange(ws, x, y, w, h, toplevel->server->config.inner_gap);
		}
	}

	/* Notify bar: workspace occupancy may have changed */
	if (toplevel->server->bar_manager && toplevel->server->active_output) {
		uwm_bar_send_output(toplevel->server->active_output);
	}

	/* Drop stale bar dedup references so a recycled toplevel address
	 * can't suppress future bar updates. */
	struct uwm_output *output;
	wl_list_for_each(output, &toplevel->server->outputs, link) {
		if (output->bar_focused_toplevel == toplevel)
			output->bar_focused_toplevel = NULL;
	}

	toplevel_destroy_border(toplevel);
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
	struct uwm_server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *xdg_toplevel = data;

	struct uwm_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	if (!toplevel)
		return;
	toplevel->type = UWM_TOPLEVEL_XDG;
	toplevel->server = server;
	toplevel->xdg_toplevel = xdg_toplevel;
	toplevel->workspace = &server->workspaces.workspaces[server->workspaces.current];
	wl_list_init(&toplevel->link);
	wl_list_init(&toplevel->workspace_link);
	toplevel->scene_tree = wlr_scene_xdg_surface_create(toplevel->server->tiled_layer, xdg_toplevel->base);
	if (!toplevel->scene_tree) {
		free(toplevel);
		return;
	}
	toplevel->scene_tree->node.data = toplevel;
	xdg_toplevel->base->data = toplevel->scene_tree;
	toplevel_create_border(toplevel);

	/* image_capture_scene is created lazily on first capture request */

	toplevel->map.notify = xdg_toplevel_map;
	wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);
	toplevel->unmap.notify = xdg_toplevel_unmap;
	wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);
	toplevel->commit.notify = xdg_toplevel_commit;
	wl_signal_add(&xdg_toplevel->base->surface->events.commit, &toplevel->commit);

	toplevel->destroy.notify = xdg_toplevel_destroy;
	wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);

	toplevel->request_move.notify = xdg_toplevel_request_move;
	wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);
	toplevel->request_resize.notify = xdg_toplevel_request_resize;
	wl_signal_add(&xdg_toplevel->events.request_resize, &toplevel->request_resize);
	toplevel->request_maximize.notify = xdg_toplevel_request_maximize;
	wl_signal_add(&xdg_toplevel->events.request_maximize, &toplevel->request_maximize);
	toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen, &toplevel->request_fullscreen);
}

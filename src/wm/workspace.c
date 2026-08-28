#include "workspace.h"
#include "bsp.h"
#include "window.h"
#include "server.h"
#include "floating.h"
#include "layout.h"
#include "output.h"
#include <stdint.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xdg_shell.h>

static void workspace_arrange_on_output(struct uwm_workspace *ws,
		struct uwm_output *output, int gap);

static void restore_container_visibility(struct uwm_bsp_node *node)
{
	if (!node)
		return;
	if (node->first) {
		if (node->mode == UWM_NODE_MONOCLE) {
			update_layout_visibility(node);
		}
		restore_container_visibility(node->first);
		restore_container_visibility(node->second);
	}
}

void workspace_manager_init(struct uwm_workspace_manager *wm)
{
	for (uint32_t i=0; i<UWM_WORKSPACE_COUNT; i++) {
		wm->workspaces[i].id=i;
		wl_list_init(&wm->workspaces[i].toplevels);
		wm->workspaces[i].focused=NULL;
		wm->workspaces[i].last_focused=NULL;
		wm->workspaces[i].root=NULL;
		wm->workspaces[i].focus_follows_pointer=false;
		wl_list_init(&wm->workspaces[i].floating_windows);
		wm->workspaces[i].fullscreen_window=NULL;
		wm->workspaces[i].monocle=false;
		wm->workspaces[i].tree_gen=0;
		wm->workspaces[i].output=NULL;
	}
	wm->current=0;
	wm->last=0;
}

void workspace_manager_finish(struct uwm_workspace_manager *wm,
		struct uwm_bsp_pool *pool)
{
	for (uint32_t i=0; i<UWM_WORKSPACE_COUNT; i++) {
		if (wm->workspaces[i].root) {
			bsp_destroy(wm->workspaces[i].root, pool);
			wm->workspaces[i].root = NULL;
		}
		wm->workspaces[i].output = NULL;
	}
}

static void workspace_hide(struct uwm_workspace *ws)
{
	if (ws->fullscreen_window) {
		wlr_scene_node_set_enabled(
			&ws->fullscreen_window->scene_tree->node, false);
		toplevel_update_border(ws->fullscreen_window);
		return;
	}
	struct uwm_toplevel *toplevel;
	wl_list_for_each(toplevel, &ws->toplevels, workspace_link)
	{
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
		if (toplevel->border_top)
			wlr_scene_node_set_enabled(&toplevel->border_top->node, false);
		if (toplevel->border_bottom)
			wlr_scene_node_set_enabled(&toplevel->border_bottom->node, false);
		if (toplevel->border_left)
			wlr_scene_node_set_enabled(&toplevel->border_left->node, false);
		if (toplevel->border_right)
			wlr_scene_node_set_enabled(&toplevel->border_right->node, false);
	}
	wl_list_for_each(toplevel, &ws->floating_windows, floating_link)
	{
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
		if (toplevel->border_top)
			wlr_scene_node_set_enabled(&toplevel->border_top->node, false);
		if (toplevel->border_bottom)
			wlr_scene_node_set_enabled(&toplevel->border_bottom->node, false);
		if (toplevel->border_left)
			wlr_scene_node_set_enabled(&toplevel->border_left->node, false);
		if (toplevel->border_right)
			wlr_scene_node_set_enabled(&toplevel->border_right->node, false);
	}
}

void workspace_hide_from_output(struct uwm_workspace *ws,
		struct uwm_output *output)
{
	if (ws->output)
		return;

	/* Restore bar layers when switching away from a fullscreen ws */
	if (ws->fullscreen_window && output) {
		wlr_scene_node_set_enabled(&output->layer_top->node, true);
		wlr_scene_node_set_enabled(&output->layer_overlay->node, true);
	}

	workspace_hide(ws);
}

static void workspace_show(struct uwm_workspace *ws)
{
	if (ws->fullscreen_window) {
		wlr_scene_node_set_enabled(
			&ws->fullscreen_window->scene_tree->node, true);
		toplevel_update_border(ws->fullscreen_window);
		return;
	}
	struct uwm_toplevel *toplevel;
	wl_list_for_each(toplevel, &ws->toplevels, workspace_link)
	{
		if (ws->monocle) {
			wlr_scene_node_set_enabled(&toplevel->scene_tree->node,
				toplevel == ws->focused);
		} else {
			wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
		}
	}
	wl_list_for_each(toplevel, &ws->floating_windows, floating_link)
	{
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
	}
	if (!ws->monocle && ws->root) {
		restore_container_visibility(ws->root);
	}
	workspace_update_borders(ws);
}

void workspace_show_on_output(struct uwm_workspace *ws,
		struct uwm_output *output)
{
	/* Hide bar layers when switching to a fullscreen workspace */
	if (ws->fullscreen_window && output) {
		wlr_scene_node_set_enabled(&output->layer_top->node, false);
		wlr_scene_node_set_enabled(&output->layer_overlay->node, false);
	}

	workspace_show(ws);
}

void workspace_switch(struct uwm_server *server, uint32_t workspace)
{
	if (workspace >= UWM_WORKSPACE_COUNT)
		return;

	/* Determine which output to switch on */
	struct uwm_output *output = server->active_output;
	if (!output) {
		output = output_first(server);
		if (!output)
			return;
	}

	output_set_workspace(output, workspace);
	workspace_update_borders(&server->workspaces.workspaces[workspace]);
}

void workspace_focus_previous(struct uwm_server *server)
{
	if (!server)
		return;
	struct uwm_workspace *ws = &server->workspaces.workspaces[server->workspaces.current];
	if (ws->last_focused == NULL)
		return;

	focus_toplevel(ws->last_focused);
}

static void workspace_arrange_on_output(struct uwm_workspace *ws,
		struct uwm_output *output, int gap)
{
	if (output && ws->root) {
		bsp_arrange(ws, output->lx + output->usable_area.x,
			output->ly + output->usable_area.y,
			output->usable_area.width, output->usable_area.height, gap);
	}
}

void workspace_move_toplevel(struct uwm_toplevel *toplevel, uint32_t workspace)
{
	if (workspace>=UWM_WORKSPACE_COUNT){
		return;
	}

	struct uwm_workspace *old_ws=toplevel->workspace;
	struct uwm_workspace_manager *wm=&toplevel->server->workspaces;
	struct uwm_workspace *new_ws=&wm->workspaces[workspace];

	if (old_ws == new_ws)
		return;

	if (toplevel->fullscreen) {
		toggle_fullscreen(toplevel);
	}

	bool was_focused = (old_ws->focused == toplevel);

	if (old_ws->last_focused == toplevel)
		old_ws->last_focused = NULL;

	if (toplevel->floating) {
		wl_list_remove(&toplevel->floating_link);
		wl_list_init(&toplevel->floating_link);
	} else {
		bsp_remove(old_ws, toplevel);
		wl_list_remove(&toplevel->workspace_link);
		wl_list_init(&toplevel->workspace_link);

		if (old_ws->monocle) {
			int count = 0;
			struct uwm_toplevel *tl;
			wl_list_for_each(tl, &old_ws->toplevels, workspace_link) {
				count++;
			}
			if (count <= 1) {
				old_ws->monocle = false;
				if (old_ws->root) {
					workspace_arrange_on_output(old_ws, old_ws->output,
						toplevel->server->config.inner_gap);
					set_children_visible(old_ws->root, true);
				}
			}
		}
	}

	toplevel->workspace = new_ws;

	if (was_focused) {
		old_ws->last_focused = NULL;
		if (!wl_list_empty(&old_ws->toplevels)) {
			struct uwm_toplevel *next = wl_container_of(
				old_ws->toplevels.next, next, workspace_link);
			old_ws->focused = next;
		} else if (!wl_list_empty(&old_ws->floating_windows)) {
			struct uwm_toplevel *next = wl_container_of(
				old_ws->floating_windows.next, next, floating_link);
			old_ws->focused = next;
		} else {
			old_ws->focused = NULL;
		}
	}

	if (toplevel->floating) {
		wl_list_insert(&new_ws->floating_windows, &toplevel->floating_link);
	} else {
		wl_list_insert(&new_ws->toplevels, &toplevel->workspace_link);
		bsp_insert(new_ws, toplevel);
	}

	/* Only show if the target workspace is currently displayed on an output */
	if (new_ws->output) {
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
		new_ws->focused = toplevel;
		focus_toplevel(toplevel);
	} else {
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
		new_ws->focused = toplevel;
	}

	workspace_arrange_on_output(old_ws, old_ws->output,
		toplevel->server->config.inner_gap);
	workspace_arrange_on_output(new_ws, new_ws->output,
		toplevel->server->config.inner_gap);
	workspace_update_borders(old_ws);
	workspace_update_borders(new_ws);
}

void workspace_cycle_next(struct uwm_server *server)
{
	if (!server)
		return;
	struct uwm_workspace *ws = &server->workspaces.workspaces[server->workspaces.current];

	if (ws->fullscreen_window)
		return;

	struct uwm_toplevel *windows[UWM_MAX_WINDOWS];
	int count = 0;

	struct uwm_toplevel *tl;
	wl_list_for_each(tl, &ws->toplevels, workspace_link) {
		if (count < UWM_MAX_WINDOWS)
			windows[count++] = tl;
	}
	wl_list_for_each(tl, &ws->floating_windows, floating_link) {
		if (count < UWM_MAX_WINDOWS)
			windows[count++] = tl;
	}

	if (count < 2)
		return;

	int idx = -1;
	for (int i = 0; i < count; i++) {
		if (windows[i] == ws->focused) {
			idx = i;
			break;
		}
	}

	int next = (idx + 1) % count;
	struct uwm_toplevel *next_tl = windows[next];
	focus_toplevel(next_tl);

	/* also warp cursor to centered window */
	if (next_tl && next_tl->scene_tree) {
		struct wlr_box geo = toplevel_geometry(next_tl);
		double wx = next_tl->scene_tree->node.x + geo.x;
		double wy = next_tl->scene_tree->node.y + geo.y;
		double ww = geo.width;
		double wh = geo.height;
		if (ww > 0 && wh > 0) {
			wlr_cursor_warp(server->cursor, NULL, wx + ww / 2.0, wy + wh / 2.0);
			/* ensure pointer focus follows warp */
			double sx, sy;
			struct wlr_surface *surface = NULL;
			struct uwm_toplevel *found = desktop_toplevel_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
			if (surface) {
				wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
			}
		}
	}
}

void workspace_prev(struct uwm_server *server)
{
	uint32_t current = server->workspaces.current;
	uint32_t prev = (current == 0) ? UWM_WORKSPACE_COUNT - 1 : current - 1;
	workspace_switch(server, prev);
}

void workspace_next(struct uwm_server *server)
{
	uint32_t current = server->workspaces.current;
	uint32_t next = (current + 1) % UWM_WORKSPACE_COUNT;
	workspace_switch(server, next);
}

static bool workspace_is_active(struct uwm_workspace *ws) {
	if (ws->output) return true;
	if (!wl_list_empty(&ws->toplevels)) return true;
	if (!wl_list_empty(&ws->floating_windows)) return true;
	return false;
}

void workspace_next_active(struct uwm_server *server)
{
	if (!server) return;
	uint32_t cur = server->active_output ? server->active_output->current_workspace : server->workspaces.current;
	for (uint32_t i = 1; i < UWM_WORKSPACE_COUNT; i++) {
		uint32_t cand = (cur + i) % UWM_WORKSPACE_COUNT;
		struct uwm_workspace *ws = &server->workspaces.workspaces[cand];
		if (workspace_is_active(ws)) {
			workspace_switch(server, cand);
			return;
		}
	}
}

void workspace_prev_active(struct uwm_server *server)
{
	if (!server) return;
	uint32_t cur = server->active_output ? server->active_output->current_workspace : server->workspaces.current;
	for (uint32_t i = 1; i < UWM_WORKSPACE_COUNT; i++) {
		uint32_t cand = (cur + UWM_WORKSPACE_COUNT - i) % UWM_WORKSPACE_COUNT;
		struct uwm_workspace *ws = &server->workspaces.workspaces[cand];
		if (workspace_is_active(ws)) {
			workspace_switch(server, cand);
			return;
		}
	}
}

struct uwm_workspace *workspace_for_output(struct uwm_server *server,
		struct uwm_output *output)
{
	return &server->workspaces.workspaces[output->current_workspace];
}

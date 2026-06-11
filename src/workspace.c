#include "workspace.h"
#include "bsp.h"
#include "window.h"
#include "server.h"
#include "floating.h"
#include "layout.h"
#include <stdint.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xdg_shell.h>

static void restore_container_visibility(struct uwm_bsp_node *node)
{
	if (!node)
		return;
	if (node->first) {
		if (node->mode == UWM_NODE_TABBED
				|| node->mode == UWM_NODE_MONOCLE) {
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
	}
	wm->current=0;
	wm->last=0;
}

void workspace_manager_finish(struct uwm_workspace_manager *wm)
{
	for (uint32_t i=0; i<UWM_WORKSPACE_COUNT; i++) {
		if (wm->workspaces[i].root) {
			bsp_destroy(wm->workspaces[i].root);
			wm->workspaces[i].root = NULL;
		}
	}
}

static void workspace_hide(struct uwm_workspace *ws)
{
	if (ws->fullscreen_window) {
		wlr_scene_node_set_enabled(
			&ws->fullscreen_window->scene_tree->node, false);
		return;
	}
	struct uwm_toplevel *toplevel, *tmp;
	wl_list_for_each_safe(toplevel, tmp, &ws->toplevels, workspace_link)
	{
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	}
	wl_list_for_each_safe(toplevel, tmp, &ws->floating_windows, floating_link)
	{
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	}
}

static void workspace_show(struct uwm_workspace *ws)
{
	if (ws->fullscreen_window) {
		wlr_scene_node_set_enabled(
			&ws->fullscreen_window->scene_tree->node, true);
		return;
	}
	struct uwm_toplevel *toplevel, *tmp;
	wl_list_for_each_safe(toplevel, tmp, &ws->toplevels, workspace_link)
	{
		if (ws->monocle) {
			wlr_scene_node_set_enabled(&toplevel->scene_tree->node,
				toplevel == ws->focused);
		} else {
			wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
		}
	}
	wl_list_for_each_safe(toplevel, tmp, &ws->floating_windows, floating_link)
	{
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
	}
	if (!ws->monocle && ws->root) {
		restore_container_visibility(ws->root);
	}
}

void workspace_switch(struct uwm_server *server, uint32_t workspace)
{
	if(workspace>=UWM_WORKSPACE_COUNT){
		return;
	}
	struct uwm_workspace_manager *wm = &server->workspaces;
	if(wm->current==workspace){
		return;
	}
	workspace_hide(&wm->workspaces[wm->current]);
	wm->last = wm->current;
	wm->current=workspace;
	workspace_show(&wm->workspaces[wm->current]);

	struct uwm_workspace *new_ws = &wm->workspaces[wm->current];
	if (new_ws->fullscreen_window && new_ws->fullscreen_window != new_ws->focused) {
		focus_toplevel(new_ws->fullscreen_window);
	} else if (new_ws->focused) {
		focus_toplevel(new_ws->focused);
	}
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

void workspace_move_toplevel(struct uwm_toplevel *toplevel, uint32_t workspace)
{
	if(workspace>=UWM_WORKSPACE_COUNT){
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
	}

	wl_list_remove(&toplevel->workspace_link);
	wl_list_insert(&new_ws->toplevels, &toplevel->workspace_link);
	toplevel->workspace=new_ws;

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
		bsp_insert(new_ws, toplevel);
	}

	if (wm->current == workspace) {
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
		new_ws->focused = toplevel;
		focus_toplevel(toplevel);
	} else {
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
		new_ws->focused = toplevel;
	}

	int area_w, area_h;
	get_output_size(toplevel->server, &area_w, &area_h);
	bsp_arrange(old_ws, area_w, area_h, toplevel->server->config.inner_gap);
	bsp_arrange(new_ws, area_w, area_h, toplevel->server->config.inner_gap);
}

void workspace_cycle_next(struct uwm_server *server)
{
	if (!server)
		return;
	struct uwm_workspace *ws = &server->workspaces.workspaces[server->workspaces.current];

	if (ws->fullscreen_window)
		return;

	struct uwm_toplevel *windows[256];
	int count = 0;

	struct uwm_toplevel *tl;
	wl_list_for_each(tl, &ws->toplevels, workspace_link) {
		if (count < 256)
			windows[count++] = tl;
	}
	wl_list_for_each(tl, &ws->floating_windows, floating_link) {
		if (count < 256)
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
	focus_toplevel(windows[next]);

	if (ws->monocle) {
		int out_w, out_h;
		get_output_size(server, &out_w, &out_h);
		bsp_arrange(ws, out_w, out_h, server->config.inner_gap);
	}
}

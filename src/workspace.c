#include "workspace.h"
#include "bsp.h"
#include "window.h"
#include "server.h"
#include <stdint.h>
#include <wlr/types/wlr_scene.h>

void workspace_manager_init(struct uwm_workspace_manager *wm)
{
	for (uint32_t i=0; i<UWM_WORKSPACE_COUNT; i++) {
		wm->workspaces[i].id=i;
		wl_list_init(&wm->workspaces[i].toplevels);
		wm->workspaces[i].focused=NULL;
		wm->workspaces[i].last_focused=NULL;
		wm->workspaces[i].root=NULL;
		wm->workspaces[i].focus_follows_pointer=false;
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
	struct uwm_toplevel *toplevel, *tmp;
	wl_list_for_each_safe(toplevel, tmp, &ws->toplevels, workspace_link)
	{
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	}
}

static void workspace_show(struct uwm_workspace *ws)
{
	struct uwm_toplevel *toplevel, *tmp;
	wl_list_for_each_safe(toplevel, tmp, &ws->toplevels, workspace_link)
	{
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
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
	if(new_ws->focused){
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

	bool was_focused = (old_ws->focused == toplevel);

	if (old_ws->last_focused == toplevel)
		old_ws->last_focused = NULL;

	bsp_remove(old_ws, toplevel);

	wl_list_remove(&toplevel->workspace_link);
	wl_list_insert(&new_ws->toplevels, &toplevel->workspace_link);
	toplevel->workspace=new_ws;

	if (was_focused) {
		if (!wl_list_empty(&old_ws->toplevels)) {
			struct uwm_toplevel *next = wl_container_of(
				old_ws->toplevels.next, next, workspace_link);
			old_ws->focused = next;
		} else {
			old_ws->focused = NULL;
		}
	}

	bsp_insert(new_ws, toplevel);

	if(wm->current==workspace){
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
		focus_toplevel(toplevel);
	}else{
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
		if (old_ws == &wm->workspaces[wm->current] && old_ws->focused) {
			focus_toplevel(old_ws->focused);
		}
	}

	struct wlr_output_layout *layout = toplevel->server->output_layout;
	struct wlr_output_layout_output *lo;
	int area_w = 0, area_h = 0;
	wl_list_for_each(lo, &layout->outputs, link) {
		struct wlr_output *output = lo->output;
		if (output->enabled) {
			area_w = output->width;
			area_h = output->height;
			break;
		}
	}

	bsp_arrange(old_ws, area_w, area_h);
	bsp_arrange(new_ws, area_w, area_h);
}

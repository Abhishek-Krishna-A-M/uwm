#include "workspace.h"
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
		wm->workspaces[i].layout=UWM_LAYOUT_BSP;
		wm->workspaces[i].layout_data=NULL;
	}
	wm->current=0;
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
	wm->current=workspace;
	workspace_show(&wm->workspaces[wm->current]);

	struct uwm_workspace *new_ws = &wm->workspaces[wm->current];
	if(new_ws->focused){
		focus_toplevel(new_ws->focused);
	}
}

void workspace_move_toplevel(struct uwm_toplevel *toplevel, uint32_t workspace)
{
	if(workspace>=UWM_WORKSPACE_COUNT){
		return;
	}
	struct uwm_workspace *old_ws=toplevel->workspace;
	struct uwm_workspace_manager *wm=&toplevel->server->workspaces;
	struct uwm_workspace *new_ws=&wm->workspaces[workspace];

	wl_list_remove(&toplevel->workspace_link);
	wl_list_insert(&new_ws->toplevels, &toplevel->workspace_link);
	toplevel->workspace=new_ws;

	if(old_ws->focused==toplevel){
		old_ws->focused=NULL;
	}

	if(wm->current==workspace){
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
	}else{
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	}
}

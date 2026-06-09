#ifndef WORKSPACE_H
#define WORKSPACE_H

#include <stdint.h>
#include <wayland-server-core.h>

struct uwm_toplevel;

#define UWM_WORKSPACE_COUNT 9

enum uwm_layout_mode {
	UWM_LAYOUT_BSP,
	UWM_LAYOUT_MONOCLE,
	UWM_LAYOUT_TABBED,
	UWM_LAYOUT_FLOATING
};

struct uwm_workspace{
	uint32_t id;
	struct wl_list toplevels;
	struct uwm_toplevel *focused;
	enum uwm_layout_mode layout;
	void *layout_data;
};

struct uwm_workspace_manager{
	struct uwm_workspace workspaces[UWM_WORKSPACE_COUNT];
	uint32_t current;
};

struct uwm_server;

void workspace_manager_init(struct uwm_workspace_manager *wm);
void workspace_switch(struct uwm_server *server, uint32_t workspace);
void workspace_move_toplevel(struct uwm_toplevel *toplevel, uint32_t workspace);
#endif

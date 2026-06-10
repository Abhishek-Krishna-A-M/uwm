#ifndef WORKSPACE_H
#define WORKSPACE_H

#include <stdint.h>
#include <stdbool.h>
#include <wayland-server-core.h>

struct uwm_toplevel;

#define UWM_WORKSPACE_COUNT 9

#include "bsp.h"

struct uwm_workspace{
	uint32_t id;
	struct wl_list toplevels;
	struct uwm_toplevel *focused;
	struct uwm_toplevel *last_focused;
	struct uwm_bsp_node *root;
	bool focus_follows_pointer;
	struct wl_list floating_windows;
	struct uwm_toplevel *fullscreen_window;
	bool monocle;
	uint32_t tree_gen;
};

struct uwm_workspace_manager{
	struct uwm_workspace workspaces[UWM_WORKSPACE_COUNT];
	uint32_t current;
	uint32_t last;
};

struct uwm_server;

void workspace_manager_init(struct uwm_workspace_manager *wm);
void workspace_manager_finish(struct uwm_workspace_manager *wm);
void workspace_switch(struct uwm_server *server, uint32_t workspace);
void workspace_move_toplevel(struct uwm_toplevel *toplevel, uint32_t workspace);
void workspace_focus_previous(struct uwm_server *server);
void workspace_cycle_next(struct uwm_server *server);
#endif

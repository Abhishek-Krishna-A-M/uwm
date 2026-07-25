#ifndef WORKSPACE_H
#define WORKSPACE_H

#include <stdint.h>
#include <stdbool.h>
#include <wayland-server-core.h>

struct uwm_toplevel;
struct uwm_output;

#include "config.h"
#include "bsp.h"

struct uwm_workspace{
	struct uwm_toplevel *focused;
	struct uwm_toplevel *last_focused;
	struct uwm_bsp_node *root;
	struct uwm_toplevel *fullscreen_window;
	struct uwm_output *output;
	struct wl_list toplevels;
	struct wl_list floating_windows;
	uint32_t id;
	uint32_t tree_gen;
	bool focus_follows_pointer;
	bool monocle;
	bool layout_dirty;
};

struct uwm_workspace_manager{
	struct uwm_workspace workspaces[UWM_WORKSPACE_COUNT];
	uint32_t current;            /* workspace index of the focused output */
	uint32_t last;
};

struct uwm_server;

void workspace_manager_init(struct uwm_workspace_manager *wm);
void workspace_manager_finish(struct uwm_workspace_manager *wm,
	struct uwm_bsp_pool *pool);

/* Workspace switching on the focused output */
void workspace_switch(struct uwm_server *server, uint32_t workspace);
void workspace_move_toplevel(struct uwm_toplevel *toplevel, uint32_t workspace);
void workspace_focus_previous(struct uwm_server *server);
void workspace_cycle_next(struct uwm_server *server);
void workspace_prev(struct uwm_server *server);
void workspace_next(struct uwm_server *server);

/* Output-derived helpers */
struct uwm_workspace *workspace_for_output(struct uwm_server *server,
	struct uwm_output *output);

/* Per-output workspace show/hide */
void workspace_hide_from_output(struct uwm_workspace *ws,
	struct uwm_output *output);
void workspace_show_on_output(struct uwm_workspace *ws,
	struct uwm_output *output);

#endif

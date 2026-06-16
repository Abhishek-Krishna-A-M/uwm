#ifndef WINDOW_H
#define WINDOW_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include "server.h"

struct uwm_toplevel {
	struct wl_list link;
	struct uwm_server *server;
	struct uwm_workspace *workspace;
	struct wl_list workspace_link;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree;
	struct wlr_xdg_toplevel_decoration_v1 *decoration;

	bool floating;
	bool fullscreen;
	bool is_transient;
	int float_x, float_y, float_width, float_height;

	int saved_x, saved_y, saved_width, saved_height;
	bool saved_floating;

	struct wl_list floating_link;

	struct uwm_toplevel *bsp_saved_sibling;
	enum uwm_split bsp_saved_split;
	float bsp_saved_ratio;
	enum uwm_node_mode bsp_saved_mode;
	bool bsp_saved_is_second;
	bool bsp_saved;

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;

	struct wl_listener decoration_destroy;
	struct wl_listener decoration_request_mode;

	struct wlr_ext_foreign_toplevel_handle_v1 *ext_foreign_toplevel;
};

struct uwm_popup {
	struct wlr_xdg_popup *xdg_popup;
	struct wl_listener commit;
	struct wl_listener destroy;
};

void focus_toplevel(struct uwm_toplevel *toplevel);
bool should_tile_toplevel(struct uwm_toplevel *toplevel);
struct uwm_toplevel *desktop_toplevel_at(
		struct uwm_server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy);

void server_new_xdg_toplevel(struct wl_listener *listener, void *data);
void server_new_xdg_popup(struct wl_listener *listener, void *data);
void server_new_toplevel_decoration(struct wl_listener *listener, void *data);

#endif /* WINDOW_H */

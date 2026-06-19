#ifndef WINDOW_H
#define WINDOW_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include "server.h"

#define UWM_TOPLEVEL_POOL_SIZE 256

struct uwm_toplevel {
	/* --- hot fields (accessed frequently) --- */
	struct wl_list link;
	struct uwm_server *server;
	struct uwm_workspace *workspace;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree;
	union {
		struct wl_list workspace_link;
		struct wl_list floating_link;
	};

	/* --- geometry --- */
	int float_x, float_y, float_width, float_height;
	int saved_x, saved_y, saved_width, saved_height;

	/* --- decoration --- */
	struct wlr_xdg_toplevel_decoration_v1 *decoration;

	/* --- BSP restore --- */
	struct uwm_toplevel *bsp_saved_sibling;
	float bsp_saved_ratio;
	enum uwm_split bsp_saved_split;
	enum uwm_node_mode bsp_saved_mode;

	/* --- screen sharing (lazy) --- */
	struct wlr_ext_foreign_toplevel_handle_v1 *ext_foreign_toplevel;
	struct wlr_foreign_toplevel_handle_v1 *foreign_toplevel;
	struct wlr_scene *image_capture_scene;

	/* --- flags packed together --- */
	unsigned int floating : 1;
	unsigned int fullscreen : 1;
	unsigned int is_transient : 1;
	unsigned int saved_floating : 1;
	unsigned int bsp_saved_is_second : 1;
	unsigned int bsp_saved : 1;

	/* --- listeners (cold path) --- */
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener foreign_toplevel_request_activate;
	struct wl_listener foreign_toplevel_request_close;
	struct wl_listener decoration_destroy;
	struct wl_listener decoration_request_mode;
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

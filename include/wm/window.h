#ifndef WINDOW_H
#define WINDOW_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/config.h>
#if WLR_HAS_XWAYLAND
#include <wlr/xwayland.h>
#endif
#include "server.h"

#define UWM_TOPLEVEL_POOL_SIZE 256

enum uwm_toplevel_type {
	UWM_TOPLEVEL_XDG,
#if WLR_HAS_XWAYLAND
	UWM_TOPLEVEL_XWAYLAND,
#endif
};

struct uwm_toplevel {
	/* --- hot fields (accessed frequently) --- */
	enum uwm_toplevel_type type;
	struct wl_list link;
	struct uwm_server *server;
	struct uwm_workspace *workspace;
	struct wlr_xdg_toplevel *xdg_toplevel;
#if WLR_HAS_XWAYLAND
	struct wlr_xwayland_surface *xwayland_surface;
#endif
	struct wlr_scene_tree *scene_tree;
	union {
		struct wl_list workspace_link;
		struct wl_list floating_link;
	};

	/* --- border (active window only) --- */
	struct wlr_scene_rect *border_top;
	struct wlr_scene_rect *border_bottom;
	struct wlr_scene_rect *border_left;
	struct wlr_scene_rect *border_right;

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
	int bsp_saved_depth;
	struct uwm_bsp_node *bsp_leaf; /* O(1) leaf cache — fix 1.4 */

	/* --- screen sharing (lazy) --- */
	struct wlr_ext_foreign_toplevel_handle_v1 *ext_foreign_toplevel;
	struct wlr_foreign_toplevel_handle_v1 *foreign_toplevel;
	struct wlr_scene *image_capture_scene;
	char *last_title;
	char *last_app_id;

	/* --- flags packed together --- */
	unsigned int floating : 1;
	unsigned int fullscreen : 1;
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
#if WLR_HAS_XWAYLAND
	struct wl_listener xwayland_activate;
	struct wl_listener xwayland_configure;
	struct wl_listener xwayland_set_geometry;
	struct wl_listener associate;
	struct wl_listener dissociate;
	struct wl_listener override_redirect;
#endif
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

/* helpers — type-agnostic */
struct wlr_surface *toplevel_surface(struct uwm_toplevel *t);
struct wlr_box toplevel_geometry(struct uwm_toplevel *t);
void toplevel_set_size(struct uwm_toplevel *t, int w, int h);
void toplevel_set_activated(struct uwm_toplevel *t, bool activated);
void toplevel_set_fullscreen(struct uwm_toplevel *t, bool fs);
const char *toplevel_app_id(struct uwm_toplevel *t);
const char *toplevel_title(struct uwm_toplevel *t);
void toplevel_send_close(struct uwm_toplevel *t);

void server_new_xdg_toplevel(struct wl_listener *listener, void *data);
void server_new_xdg_popup(struct wl_listener *listener, void *data);
void server_new_toplevel_decoration(struct wl_listener *listener, void *data);
#if WLR_HAS_XWAYLAND
void server_new_xwayland_surface(struct wl_listener *listener, void *data);
#endif

void toplevel_create_border(struct uwm_toplevel *toplevel);
void toplevel_update_border(struct uwm_toplevel *toplevel);
void toplevel_destroy_border(struct uwm_toplevel *toplevel);
void workspace_update_borders(struct uwm_workspace *ws);

#endif /* WINDOW_H */

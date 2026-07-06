#ifndef OUTPUT_H
#define OUTPUT_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include "server.h"

struct uwm_output {
	struct wl_list link;                     /* server->outputs */
	struct uwm_server *server;
	struct wlr_output *wlr_output;
	struct wlr_scene_output *scene_output;
	struct wlr_output_layout_output *layout_output;

	/* Per-layer scene trees, ordered bottom to top.
	 * Children of the root scene, shared across workspaces. */
	struct wlr_scene_tree *layer_background;
	struct wlr_scene_tree *layer_bottom;
	struct wlr_scene_tree *layer_floating;
	struct wlr_scene_tree *layer_top;
	struct wlr_scene_tree *layer_overlay;
	struct wlr_scene_tree *layer_lock;          /* session lock surfaces (topmost) */

	/* Usable area after layer shell exclusive zones are applied */
	struct wlr_box usable_area;

	/* Layout position (from wlr_output_layout) */
	int lx, ly;
	uint32_t current_workspace;              /* workspace displayed on this output */

	/* Session lock surface (NULL when not locked) */
	struct wlr_session_lock_surface_v1 *lock_surface;
	struct wl_listener lock_surface_destroy;

	/* Listeners — cold path */
	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
};

void server_new_output(struct wl_listener *listener, void *data);

/* Per-output workspace management */
void output_set_workspace(struct uwm_output *output, uint32_t workspace_id);
struct uwm_output *output_from_wlr_output(struct uwm_server *server,
	struct wlr_output *wlr_output);
struct uwm_output *output_first(struct uwm_server *server);
/* Output layout change callback (hooked in server.c) */
void handle_output_layout_change(struct wl_listener *listener, void *data);

/* Update wlr-output-manager-v1 with current output state */
void output_manager_update(struct uwm_server *server);

#endif /* OUTPUT_H */

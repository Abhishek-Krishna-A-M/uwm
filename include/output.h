#ifndef OUTPUT_H
#define OUTPUT_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include "server.h"

struct uwm_output {
	struct wl_list link;
	struct uwm_server *server;
	struct wlr_output *wlr_output;
	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;

	/* Per-layer scene trees, ordered bottom to top */
	struct wlr_scene_tree *layer_background;
	struct wlr_scene_tree *layer_bottom;
	struct wlr_scene_tree *layer_tiled;
	struct wlr_scene_tree *layer_floating;
	struct wlr_scene_tree *layer_top;
	struct wlr_scene_tree *layer_overlay;
};

void server_new_output(struct wl_listener *listener, void *data);

#endif /* OUTPUT_H */

#ifndef LAYER_SHELL_H
#define LAYER_SHELL_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>

struct uwm_server;
struct uwm_output;

struct uwm_layer_surface {
	struct wl_list link;
	struct uwm_output *output;
	struct wlr_layer_surface_v1 *layer_surface;
	struct wlr_scene_layer_surface_v1 *scene_layer_surface;
	struct wlr_scene_node *scene_node;
	bool mapped;

	struct wl_listener surface_commit;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener new_popup;
	struct wl_listener node_destroy;
};

struct uwm_layer_shell {
	struct uwm_server *server;
	struct wlr_layer_shell_v1 *layer_shell;

	struct wl_listener new_layer_surface;
};

bool layer_shell_create(struct uwm_server *server);
void layer_shell_destroy(struct uwm_server *server);

void layer_surface_arrange(struct uwm_output *output);
void layer_surface_arrange_all(struct uwm_server *server);

struct uwm_layer_surface *layer_surface_create(
	struct uwm_server *server,
	struct wlr_layer_surface_v1 *wlr_layer_surface);

void layer_surface_destroy(struct uwm_layer_surface *layer_surface);

void layer_surface_get_exclusive_zones(
	struct uwm_output *output,
	int *top, int *bottom, int *left, int *right);

#endif /* LAYER_SHELL_H */

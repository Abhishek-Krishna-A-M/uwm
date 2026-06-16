#include <stdlib.h>
#include <math.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>
#include "layer_shell.h"
#include "server.h"
#include "output.h"
#include "window.h"
#include "bsp.h"
#include "workspace.h"

struct wlr_scene_tree *layer_surface_get_scene(struct uwm_output *output,
		enum zwlr_layer_shell_v1_layer layer) {
	switch (layer) {
	case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
		return output->layer_background;
	case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
		return output->layer_bottom;
	case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
		return output->layer_top;
	case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
		return output->layer_overlay;
	default:
		return output->layer_background;
	}
}

static void arrange_surface(struct uwm_output *output,
		const struct wlr_box *full_area, struct wlr_box *usable_area,
		struct wlr_scene_tree *tree, bool exclusive) {
	struct wlr_scene_node *node;
	wl_list_for_each(node, &tree->children, link) {
		struct uwm_layer_surface *surface = node->data;
		if (!surface) continue;
		if (!surface->layer_surface->initialized) continue;
		if ((surface->layer_surface->current.exclusive_zone > 0) != exclusive) {
			continue;
		}
		wlr_scene_layer_surface_v1_configure(surface->scene_layer_surface,
			full_area, usable_area);
	}
}

void layer_surface_arrange(struct uwm_output *output) {
	if (!output) {
		return;
	}

	struct wlr_output *wlr_output = output->wlr_output;
	if (!wlr_output || !wlr_output->enabled) {
		return;
	}

	struct wlr_box usable_area = {
		.width = wlr_output->width,
		.height = wlr_output->height,
	};
	const struct wlr_box full_area = usable_area;

	/* Pass 1: exclusive surfaces (top->bottom) */
	arrange_surface(output, &full_area, &usable_area, output->layer_overlay, true);
	arrange_surface(output, &full_area, &usable_area, output->layer_top, true);
	arrange_surface(output, &full_area, &usable_area, output->layer_bottom, true);
	arrange_surface(output, &full_area, &usable_area, output->layer_background, true);

	/* Pass 2: non-exclusive surfaces (top->bottom) */
	arrange_surface(output, &full_area, &usable_area, output->layer_overlay, false);
	arrange_surface(output, &full_area, &usable_area, output->layer_top, false);
	arrange_surface(output, &full_area, &usable_area, output->layer_bottom, false);
	arrange_surface(output, &full_area, &usable_area, output->layer_background, false);

	/* Store usable area for window arrangement */
	output->usable_area = usable_area;

	/* Rearrange tiled windows if usable area changed */
	struct uwm_server *server = output->server;
	for (uint32_t i = 0; i < UWM_WORKSPACE_COUNT; i++) {
		struct uwm_workspace *ws = &server->workspaces.workspaces[i];
		if (ws->root) {
			bsp_arrange(ws, usable_area.x, usable_area.y,
				usable_area.width, usable_area.height,
				server->config.inner_gap);
		}
	}

	/* Find topmost keyboard-interactive exclusive layer and focus it */
	struct uwm_layer_surface *topmost = NULL;
	struct wlr_scene_node *node;

	/* Check overlay layer first */
	wl_list_for_each_reverse(node, &output->layer_overlay->children, link) {
		struct uwm_layer_surface *surface = node->data;
		if (!surface) continue;
		if (surface->layer_surface->current.keyboard_interactive
				== ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
				&& surface->layer_surface->surface->mapped) {
			topmost = surface;
			break;
		}
	}
	if (!topmost) {
		/* Then check top layer */
		wl_list_for_each_reverse(node, &output->layer_top->children, link) {
			struct uwm_layer_surface *surface = node->data;
			if (!surface) continue;
			if (surface->layer_surface->current.keyboard_interactive
					== ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
					&& surface->layer_surface->surface->mapped) {
				topmost = surface;
				break;
			}
		}
	}

	if (topmost) {
		struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
		if (keyboard) {
			wlr_seat_keyboard_notify_enter(server->seat,
				topmost->layer_surface->surface,
				keyboard->keycodes,
				keyboard->num_keycodes,
				&keyboard->modifiers);
		}
	}
}

void layer_surface_arrange_all(struct uwm_server *server) {
	struct uwm_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		layer_surface_arrange(output);
	}
}

void layer_surface_get_exclusive_zones(
		struct uwm_output *output,
		int *top, int *bottom, int *left, int *right) {
	if (!output) {
		*top = 0;
		*bottom = 0;
		*left = 0;
		*right = 0;
		return;
	}

	*top = 0;
	*bottom = 0;
	*left = 0;
	*right = 0;

	struct wlr_scene_node *node;
	struct wlr_scene_tree *layers[] = {
		output->layer_overlay,
		output->layer_top,
		output->layer_bottom,
		output->layer_background,
	};

	for (size_t i = 0; i < sizeof(layers) / sizeof(layers[0]); i++) {
		wl_list_for_each(node, &layers[i]->children, link) {
			struct uwm_layer_surface *surface = node->data;
			if (!surface) continue;
			if (!surface->layer_surface->initialized) continue;

			struct wlr_layer_surface_v1_state *state =
				&surface->layer_surface->current;

			if (state->exclusive_zone <= 0) continue;

			int zone = state->exclusive_zone;

			if (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)
				*top += zone;
			if (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)
				*bottom += zone;
			if (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)
				*left += zone;
			if (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)
				*right += zone;
		}
	}
}

static void handle_surface_commit(struct wl_listener *listener, void *data) {
	struct uwm_layer_surface *surface =
		wl_container_of(listener, surface, surface_commit);
	struct wlr_layer_surface_v1 *layer_surface = surface->layer_surface;
	(void)data;

	/* On any commit with state changes, re-arrange layers */
	if (layer_surface->initial_commit
			|| layer_surface->current.committed
			|| layer_surface->surface->mapped != surface->mapped) {
		surface->mapped = layer_surface->surface->mapped;
		if (surface->output) {
			layer_surface_arrange(surface->output);
		}
	}
}

static void handle_map(struct wl_listener *listener, void *data) {
	struct uwm_layer_surface *surface =
		wl_container_of(listener, surface, map);
	struct uwm_server *server = surface->output->server;
	(void)data;

	struct wlr_layer_surface_v1 *layer_surface = surface->layer_surface;

	/* Only focus keyboard-interactive overlay or top layers */
	if (layer_surface->current.keyboard_interactive
			&& (layer_surface->current.layer == ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY
			|| layer_surface->current.layer == ZWLR_LAYER_SHELL_V1_LAYER_TOP)) {
		struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
		if (keyboard) {
			wlr_seat_keyboard_notify_enter(server->seat,
				layer_surface->surface,
				keyboard->keycodes,
				keyboard->num_keycodes,
				&keyboard->modifiers);
		}
	}
}

static void handle_unmap(struct wl_listener *listener, void *data) {
	struct uwm_layer_surface *surface =
		wl_container_of(listener, surface, unmap);
	struct uwm_server *server = surface->output->server;
	(void)data;

	/* If this layer had keyboard focus, restore to workspace window */
	if (surface->layer_surface->surface ==
			server->seat->keyboard_state.focused_surface) {
		struct uwm_workspace *ws =
			&server->workspaces.workspaces[server->workspaces.current];
		if (ws->focused) {
			struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
			if (keyboard) {
				wlr_seat_keyboard_notify_enter(server->seat,
					ws->focused->xdg_toplevel->base->surface,
					keyboard->keycodes,
					keyboard->num_keycodes,
					&keyboard->modifiers);
			}
		} else {
			wlr_seat_keyboard_notify_clear_focus(server->seat);
		}
	}
}

static void handle_new_popup(struct wl_listener *listener, void *data) {
	(void)listener;
	(void)data;
}

static void handle_node_destroy(struct wl_listener *listener, void *data) {
	struct uwm_layer_surface *surface =
		wl_container_of(listener, surface, node_destroy);
	(void)data;

	/* Re-arrange layers after this surface is removed */
	if (surface->output) {
		layer_surface_arrange(surface->output);
	}

	/* Remove all listeners */
	wl_list_remove(&surface->surface_commit.link);
	wl_list_remove(&surface->map.link);
	wl_list_remove(&surface->unmap.link);
	wl_list_remove(&surface->new_popup.link);
	wl_list_remove(&surface->node_destroy.link);

	/* Clear data pointer on the layer surface to prevent dangling refs */
	surface->layer_surface->data = NULL;

	wl_list_remove(&surface->link);
	free(surface);
}

struct uwm_layer_surface *layer_surface_create(
		struct uwm_server *server,
		struct wlr_layer_surface_v1 *wlr_layer_surface) {
	struct uwm_layer_surface *surface = calloc(1, sizeof(*surface));
	if (!surface) {
		return NULL;
	}

	surface->layer_surface = wlr_layer_surface;

	/* Assign output */
	if (!wlr_layer_surface->output) {
		struct uwm_output *output;
		wl_list_for_each(output, &server->outputs, link) {
			wlr_layer_surface->output = output->wlr_output;
			break;
		}
	}

	/* Find the matching uwm_output */
	if (wlr_layer_surface->output) {
		struct uwm_output *output;
		wl_list_for_each(output, &server->outputs, link) {
			if (output->wlr_output == wlr_layer_surface->output) {
				surface->output = output;
				break;
			}
		}
	}

	if (!surface->output) {
		wlr_log(WLR_ERROR, "No output for layer surface");
		free(surface);
		return NULL;
	}

	/* Get the scene tree for this layer type */
	struct wlr_scene_tree *output_layer = layer_surface_get_scene(
		surface->output, wlr_layer_surface->pending.layer);

	/* Create scene layer surface on the per-output layer tree */
	surface->scene_layer_surface = wlr_scene_layer_surface_v1_create(
		output_layer, wlr_layer_surface);
	if (!surface->scene_layer_surface) {
		free(surface);
		return NULL;
	}

	/* Store our surface struct as the scene node data */
	surface->scene_layer_surface->tree->node.data = surface;

	/* Set up listeners */
	surface->surface_commit.notify = handle_surface_commit;
	wl_signal_add(&wlr_layer_surface->surface->events.commit,
		&surface->surface_commit);
	surface->map.notify = handle_map;
	wl_signal_add(&wlr_layer_surface->surface->events.map, &surface->map);
	surface->unmap.notify = handle_unmap;
	wl_signal_add(&wlr_layer_surface->surface->events.unmap, &surface->unmap);
	surface->new_popup.notify = handle_new_popup;
	wl_signal_add(&wlr_layer_surface->events.new_popup, &surface->new_popup);
	surface->node_destroy.notify = handle_node_destroy;
	wl_signal_add(&surface->scene_layer_surface->tree->node.events.destroy,
		&surface->node_destroy);

	/* Store the layer surface data pointer for reverse lookup */
	wlr_layer_surface->data = surface;

	wl_list_insert(&server->layer_surfaces, &surface->link);

	wlr_log(WLR_INFO, "Layer surface created for layer %d",
		wlr_layer_surface->pending.layer);

	return surface;
}

void layer_surface_destroy(struct uwm_layer_surface *layer_surface) {
	if (!layer_surface) {
		return;
	}

	wl_list_remove(&layer_surface->link);
	wl_list_remove(&layer_surface->surface_commit.link);
	wl_list_remove(&layer_surface->map.link);
	wl_list_remove(&layer_surface->unmap.link);
	wl_list_remove(&layer_surface->new_popup.link);
	wl_list_remove(&layer_surface->node_destroy.link);

	free(layer_surface);
}

static void layer_shell_handle_new_layer_surface(struct wl_listener *listener, void *data) {
	struct uwm_layer_shell *layer_shell =
		wl_container_of(listener, layer_shell, new_layer_surface);
	struct wlr_layer_surface_v1 *wlr_layer_surface = data;

	layer_surface_create(layer_shell->server, wlr_layer_surface);
}

bool layer_shell_create(struct uwm_server *server) {
	server->layer_shell.layer_shell = wlr_layer_shell_v1_create(
		server->wl_display, 3);
	if (!server->layer_shell.layer_shell) {
		wlr_log(WLR_ERROR, "Failed to create layer shell");
		return false;
	}

	server->layer_shell.server = server;

	server->layer_shell.new_layer_surface.notify = layer_shell_handle_new_layer_surface;
	wl_signal_add(&server->layer_shell.layer_shell->events.new_surface,
		&server->layer_shell.new_layer_surface);

	wl_list_init(&server->layer_surfaces);

	wlr_log(WLR_INFO, "Layer shell protocol initialized");
	return true;
}

void layer_shell_destroy(struct uwm_server *server) {
	struct uwm_layer_surface *layer_surface, *tmp;
	wl_list_for_each_safe(layer_surface, tmp, &server->layer_surfaces, link) {
		layer_surface_destroy(layer_surface);
	}

	wl_list_remove(&server->layer_shell.new_layer_surface.link);

	wlr_log(WLR_INFO, "Layer shell protocol destroyed");
}

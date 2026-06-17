#include <stdlib.h>
#include <time.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include "layer_shell.h"
#include "output.h"
#include "bsp.h"
#include "window.h"
#include "workspace.h"
#include "server.h"
#include "uwm_bar.h"

static void output_frame(struct wl_listener *listener, void *data) {
	struct uwm_output *output = wl_container_of(listener, output, frame);
	struct uwm_server *server = output->server;

	wlr_scene_output_commit(output->scene_output, NULL);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(output->scene_output, &now);
}

static void output_request_state(struct wl_listener *listener, void *data) {
	struct uwm_output *output = wl_container_of(listener, output, request_state);
	const struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(output->wlr_output, event->state);

	layer_surface_arrange(output);

	/* Rearrange the workspace displayed on this output */
	struct uwm_workspace *ws = &output->server->workspaces.workspaces[output->current_workspace];
	if (ws->root) {
		bsp_arrange(ws, output->usable_area.x, output->usable_area.y,
			output->usable_area.width, output->usable_area.height,
			output->server->config.inner_gap);
	}
}

static void output_destroy(struct wl_listener *listener, void *data) {
	struct uwm_output *output = wl_container_of(listener, output, destroy);
	struct uwm_server *server = output->server;

	/* Invalidate output pointer on all layer surfaces referencing this
	 * output so that unmap handlers don't access freed memory */
	struct uwm_layer_surface *ls;
	wl_list_for_each(ls, &server->layer_surfaces, link) {
		if (ls->output == output)
			ls->output = NULL;
	}

	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);

	/* Evacuate workspace from this output to the first remaining output. */
	struct uwm_workspace *ws = &server->workspaces.workspaces[output->current_workspace];
	ws->output = NULL;

	struct uwm_output *target = NULL;
	if (!wl_list_empty(&server->outputs)) {
		target = wl_container_of(server->outputs.next, target, link);
	}

	if (target) {
		struct uwm_workspace *target_ws = &server->workspaces.workspaces[target->current_workspace];
		struct uwm_toplevel *tl, *tmp;
		wl_list_for_each_safe(tl, tmp, &ws->toplevels, workspace_link) {
			workspace_move_toplevel(tl, target->current_workspace);
		}
		wl_list_for_each_safe(tl, tmp, &ws->floating_windows, floating_link) {
			workspace_move_toplevel(tl, target->current_workspace);
		}
		if (target_ws->root) {
			bsp_arrange(target_ws, target->usable_area.x, target->usable_area.y,
				target->usable_area.width, target->usable_area.height,
				server->config.inner_gap);
		}
	}

	if (server->active_output == output) {
		if (target)
			server->active_output = target;
		else
			server->active_output = NULL;
	}

	/* Destroy per-output layer scene trees.
	 * All layer surfaces on this output have already received their
	 * node destroy callbacks when wlr_output handles destruction. */
	if (output->layer_background)
		wlr_scene_node_destroy(&output->layer_background->node);
	if (output->layer_bottom)
		wlr_scene_node_destroy(&output->layer_bottom->node);
	if (output->layer_floating)
		wlr_scene_node_destroy(&output->layer_floating->node);
	if (output->layer_top)
		wlr_scene_node_destroy(&output->layer_top->node);
	if (output->layer_overlay)
		wlr_scene_node_destroy(&output->layer_overlay->node);

	free(output);
}

void handle_output_layout_change(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, output_layout_change);
	/* Update output lx, ly positions from the output layout */
	struct uwm_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		struct wlr_box box;
		wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
		output->lx = box.x;
		output->ly = box.y;
	}
}

static uint32_t find_unused_workspace(struct uwm_server *server) {
	bool used[UWM_WORKSPACE_COUNT] = {false};
	struct uwm_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output->current_workspace < UWM_WORKSPACE_COUNT)
			used[output->current_workspace] = true;
	}
	for (uint32_t i = 0; i < UWM_WORKSPACE_COUNT; i++) {
		if (!used[i])
			return i;
	}
	/* All in use, wrap around */
	return 0;
}

void output_set_workspace(struct uwm_output *output, uint32_t workspace_id) {
	if (workspace_id >= UWM_WORKSPACE_COUNT)
		return;

	struct uwm_server *server = output->server;
	struct uwm_workspace_manager *wm = &server->workspaces;
	struct uwm_workspace *new_ws = &wm->workspaces[workspace_id];
	struct uwm_workspace *old_ws = &wm->workspaces[output->current_workspace];

	if (output->current_workspace == workspace_id &&
			new_ws->output == output)
		return;

	/* If the target workspace is already on another output, swap or move.
	 * Simple policy: move the workspace from the old output to this one. */
	if (new_ws->output && new_ws->output != output) {
		struct uwm_output *old_output = new_ws->output;
		/* Swap: the other output takes this output's workspace */
		old_output->current_workspace = output->current_workspace;
	}

	/* Remove workspace from old output */
	if (old_ws->output == output) {
		old_ws->output = NULL;
	}

	/* Move old workspace windows off this output (hide them) */
	workspace_hide_from_output(old_ws, output);

	/* Assign new workspace to this output */
	output->current_workspace = workspace_id;
	new_ws->output = output;

	/* Arrange and show new workspace windows on this output */
	if (new_ws->root) {
		bsp_arrange(new_ws, output->usable_area.x, output->usable_area.y,
			output->usable_area.width, output->usable_area.height,
			server->config.inner_gap);
	}
	workspace_show_on_output(new_ws, output);

	/* Update focus */
	if (new_ws->focused) {
		focus_toplevel(new_ws->focused);
	} else if (!wl_list_empty(&new_ws->toplevels)) {
		struct uwm_toplevel *tl = wl_container_of(
			new_ws->toplevels.next, tl, workspace_link);
		focus_toplevel(tl);
	} else if (!wl_list_empty(&new_ws->floating_windows)) {
		struct uwm_toplevel *tl = wl_container_of(
			new_ws->floating_windows.next, tl, floating_link);
		focus_toplevel(tl);
	}

	/* Update server convenience fields */
	wm->last = wm->current;
	wm->current = workspace_id;
	server->active_output = output;

	/* Notify bar clients */
	uwm_bar_send_output(output);
}

void server_new_output(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	wlr_output_init_render(wlr_output, server->allocator, server->renderer);

	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);

	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode != NULL) {
		wlr_output_state_set_mode(&state, mode);
	}

	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	struct uwm_output *output = calloc(1, sizeof(*output));
	output->wlr_output = wlr_output;
	output->server = server;

	/* Assign an unused workspace */
	uint32_t ws_id = find_unused_workspace(server);
	output->current_workspace = ws_id;

	/* Create per-output layer trees.
	 * Note: tiled/floating windows live in global server->tiled_layer/
	 * server->floating_layer, not in per-output trees. */
	output->layer_background = wlr_scene_tree_create(&server->scene->tree);
	output->layer_bottom = wlr_scene_tree_create(&server->scene->tree);
	output->layer_floating = wlr_scene_tree_create(&server->scene->tree);
	output->layer_top = wlr_scene_tree_create(&server->scene->tree);
	output->layer_overlay = wlr_scene_tree_create(&server->scene->tree);

	wlr_scene_node_place_below(&output->layer_background->node,
		&server->tiled_layer->node);
	wlr_scene_node_place_above(&output->layer_bottom->node,
		&output->layer_background->node);
	wlr_scene_node_place_below(&output->layer_floating->node,
		&server->floating_layer->node);
	wlr_scene_node_place_above(&output->layer_top->node,
		&server->floating_layer->node);
	wlr_scene_node_place_above(&output->layer_overlay->node,
		&output->layer_top->node);

	/* Set up listeners */
	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	output->request_state.notify = output_request_state;
	wl_signal_add(&wlr_output->events.request_state, &output->request_state);
	output->destroy.notify = output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);

	wl_list_insert(&server->outputs, &output->link);

	/* Add to output layout (auto-arranged) */
	output->layout_output = wlr_output_layout_add_auto(
		server->output_layout, wlr_output);

	/* Create scene output and attach to layout */
	output->scene_output = wlr_scene_output_create(server->scene, wlr_output);
	wlr_scene_output_layout_add_output(
		server->scene_layout, output->layout_output, output->scene_output);

	/* Get layout position */
	struct wlr_box box;
	wlr_output_layout_get_box(server->output_layout, wlr_output, &box);
	output->lx = box.x;
	output->ly = box.y;

	/* Arrange layer surfaces */
	layer_surface_arrange(output);

	/* Assign the workspace to this output */
	struct uwm_workspace *ws = &server->workspaces.workspaces[ws_id];
	ws->output = output;

	/* Set as active output if none set */
	if (!server->active_output) {
		server->active_output = output;
		server->workspaces.current = ws_id;
	}

	/* Show workspace windows on this output */
	if (ws->root) {
		bsp_arrange(ws, output->usable_area.x, output->usable_area.y,
			output->usable_area.width, output->usable_area.height,
			server->config.inner_gap);
	}
	workspace_show_on_output(ws, output);

	/* Focus first window if any */
	if (ws->focused) {
		focus_toplevel(ws->focused);
	} else if (!wl_list_empty(&ws->toplevels)) {
		struct uwm_toplevel *tl = wl_container_of(
			ws->toplevels.next, tl, workspace_link);
		focus_toplevel(tl);
	}

	wlr_log(WLR_INFO, "New output: %s (%s) workspace=%u lx=%d ly=%d",
		wlr_output->name,
		wlr_output->description ? wlr_output->description : "no description",
		ws_id, output->lx, output->ly);
}

/* Lookup helpers */
struct uwm_output *output_from_wlr_output(struct uwm_server *server,
		struct wlr_output *wlr_output) {
	struct uwm_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output->wlr_output == wlr_output)
			return output;
	}
	return NULL;
}

struct uwm_output *output_from_cursor(struct uwm_server *server) {
	struct wlr_output *wlr_out = wlr_output_layout_output_at(
		server->output_layout, server->cursor->x, server->cursor->y);
	if (!wlr_out)
		return NULL;
	return output_from_wlr_output(server, wlr_out);
}

struct uwm_output *output_first(struct uwm_server *server) {
	if (wl_list_empty(&server->outputs))
		return NULL;
	struct uwm_output *output;
	output = wl_container_of(server->outputs.next, output, link);
	return output;
}

struct uwm_output *output_next(struct uwm_output *output) {
	struct wl_list *next = output->link.next;
	if (next == &output->server->outputs)
		return NULL;
	struct uwm_output *next_out;
	next_out = wl_container_of(next, next_out, link);
	return next_out;
}

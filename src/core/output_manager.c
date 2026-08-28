#include "output_manager.h"
#include "window.h"
#include "output.h"
#include "bsp.h"
#include "workspace.h"
#include "layer_shell.h"
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

void handle_output_manager_apply(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, output_manager_apply);
	struct wlr_output_configuration_v1 *config = data;

	struct wlr_output_configuration_head_v1 *config_head;
	wl_list_for_each(config_head, &config->heads, link) {
		struct wlr_output *wlr_output = config_head->state.output;
		struct uwm_output *output = output_from_wlr_output(server, wlr_output);
		bool was_enabled = wlr_output->enabled;
		bool will_be_enabled = config_head->state.enabled;

		struct wlr_output_state state;
		wlr_output_state_init(&state);
		wlr_output_head_v1_state_apply(&config_head->state, &state);

		if (!wlr_output_test_state(wlr_output, &state)) {
			wlr_log(WLR_ERROR, "output %s: test failed", wlr_output->name);
			wlr_output_configuration_v1_send_failed(config);
			wlr_output_state_finish(&state);
			wlr_output_configuration_v1_destroy(config);
			return;
		}

		if (!wlr_output_commit_state(wlr_output, &state)) {
			wlr_log(WLR_ERROR, "output %s: commit failed", wlr_output->name);
			wlr_output_configuration_v1_send_failed(config);
			wlr_output_state_finish(&state);
			wlr_output_configuration_v1_destroy(config);
			return;
		}
		wlr_output_state_finish(&state);

		if (was_enabled && !will_be_enabled && output) {
			struct uwm_workspace *ws =
				&server->workspaces.workspaces[output->current_workspace];
			ws->output = NULL;

			struct uwm_output *target = NULL;
			struct uwm_output *iter;
			wl_list_for_each(iter, &server->outputs, link) {
				if (iter != output) { target = iter; break; }
			}

			if (server->active_output == output) {
				server->active_output = target ? target : NULL;
			}
		}

		if (!was_enabled && will_be_enabled && output) {
			uint32_t ws_id = output->current_workspace;
			struct uwm_workspace *ws =
				&server->workspaces.workspaces[ws_id];

			if (ws->output && ws->output != output) {
				ws->output->current_workspace = ws_id;
			}
			ws->output = output;

			layer_surface_arrange(output);

			if (!server->active_output) {
				server->active_output = output;
				server->workspaces.current = ws_id;
			}

			if (ws->root)
				bsp_arrange(ws, output->lx + output->usable_area.x,
					output->ly + output->usable_area.y,
					output->usable_area.width,
					output->usable_area.height,
					server->config.inner_gap);
			workspace_show_on_output(ws, output);

			if (ws->focused)
				focus_toplevel(ws->focused);
			else if (!wl_list_empty(&ws->toplevels)) {
				struct uwm_toplevel *tl = wl_container_of(
					ws->toplevels.next, tl, workspace_link);
				focus_toplevel(tl);
			} else if (!wl_list_empty(&ws->floating_windows)) {
				struct uwm_toplevel *tl = wl_container_of(
					ws->floating_windows.next, tl, floating_link);
				focus_toplevel(tl);
			}
		}
	}

	wl_list_for_each(config_head, &config->heads, link) {
		struct wlr_output *wlr_output = config_head->state.output;
		wlr_output_layout_add(server->output_layout, wlr_output,
			config_head->state.x, config_head->state.y);
	}

	wlr_output_configuration_v1_send_succeeded(config);
	wlr_output_manager_v1_set_configuration(server->output_manager_v1, config);
}

void handle_cursor_shape_request(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, cursor_shape_request);
	struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;

	if (event->device_type != WLR_CURSOR_SHAPE_MANAGER_V1_DEVICE_TYPE_POINTER) {
		return;
	}

	struct wlr_seat_client *focused_client =
		server->seat->pointer_state.focused_client;
	if (focused_client != event->seat_client) {
		return;
	}

	const char *name = wlr_cursor_shape_v1_name(event->shape);
	if (name) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, name);
	}
}

void handle_output_manager_test(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, output_manager_test);
	struct wlr_output_configuration_v1 *config = data;

	struct wlr_output_configuration_head_v1 *config_head;
	bool ok = true;
	wl_list_for_each(config_head, &config->heads, link) {
		struct wlr_output *wlr_output = config_head->state.output;
		struct wlr_output_state state;
		wlr_output_state_init(&state);
		wlr_output_head_v1_state_apply(&config_head->state, &state);
		if (!wlr_output_test_state(wlr_output, &state)) {
			ok = false;
			wlr_output_state_finish(&state);
			break;
		}
		wlr_output_state_finish(&state);
	}

	if (ok)
		wlr_output_configuration_v1_send_succeeded(config);
	else
		wlr_output_configuration_v1_send_failed(config);
	wlr_output_configuration_v1_destroy(config);
}

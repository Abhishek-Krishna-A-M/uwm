#include <stdlib.h>
#include <wlr/util/log.h>
#include "uwm_bar.h"
#include "output.h"
#include "workspace.h"
#include "window.h"
#include "server.h"
#include "protocol/uwm-bar-unstable-v1-protocol.h"

/* ── Workspace Group ───────────────────────────────────────────────────── */

static void workspace_group_handle_destroy(struct wl_client *client,
		struct wl_resource *resource)
{
	(void)client;
	wl_resource_destroy(resource);
}

static const struct zwp_uwm_workspace_group_v1_interface
	workspace_group_impl = {
	.destroy = workspace_group_handle_destroy,
};

static void workspace_group_destroy(struct wl_resource *resource)
{
	struct uwm_workspace_group *group = wl_resource_get_user_data(resource);
	if (!group)
		return;
	wl_list_remove(&group->link);
	free(group);
}

/* ── Bar Manager ───────────────────────────────────────────────────────── */

static void bar_manager_handle_get_workspace_group(struct wl_client *client,
		struct wl_resource *manager_resource,
		struct wl_resource *output_resource,
		uint32_t id)
{
	struct uwm_bar_manager *manager = wl_resource_get_user_data(manager_resource);
	struct uwm_output *output = NULL;

	if (output_resource) {
		struct wlr_output *wlr_out = wl_resource_get_user_data(output_resource);
		output = output_from_wlr_output(manager->server, wlr_out);
	}

	struct uwm_workspace_group *group = calloc(1, sizeof(*group));
	if (!group) {
		wl_resource_post_no_memory(manager_resource);
		return;
	}

	uint32_t version = wl_resource_get_version(manager_resource);
	group->resource = wl_resource_create(client,
		&zwp_uwm_workspace_group_v1_interface, version, id);
	if (!group->resource) {
		free(group);
		wl_resource_post_no_memory(manager_resource);
		return;
	}

	group->output = output;
	group->manager = manager;
	wl_resource_set_implementation(group->resource,
		&workspace_group_impl, group, workspace_group_destroy);
	wl_list_insert(&manager->groups, &group->link);

	/* Send initial state */
	uwm_bar_send_workspace_state(group);

	/* Find focused window title */
	struct uwm_output *title_out = output
		? output : manager->server->active_output;
	const char *title = "";
	if (title_out) {
		struct uwm_workspace *ws = &manager->server->workspaces
			.workspaces[title_out->current_workspace];
		if (ws && ws->focused && ws->focused->xdg_toplevel->title)
			title = ws->focused->xdg_toplevel->title;
	}
	uwm_bar_send_focused_title(group, title);
	zwp_uwm_workspace_group_v1_send_done(group->resource);
}

static void bar_manager_handle_destroy(struct wl_client *client,
		struct wl_resource *resource)
{
	(void)client;
	wl_resource_destroy(resource);
}

static const struct zwp_uwm_bar_v1_interface bar_manager_impl = {
	.get_workspace_group = bar_manager_handle_get_workspace_group,
	.destroy = bar_manager_handle_destroy,
};

static void bar_manager_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id)
{
	struct uwm_bar_manager *manager = data;
	struct wl_resource *resource = wl_resource_create(client,
		&zwp_uwm_bar_v1_interface, version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource,
		&bar_manager_impl, manager, NULL);
}

/* ── Idle callback for batched bar notifications ───────────────────────── */

static void bar_send_idle_handler(void *data) {
	struct uwm_server *server = data;
	server->bar_idle_source = NULL;
	if (server->bar_manager) {
		uwm_bar_send_all(server);
	}
}

/* ── Public API ────────────────────────────────────────────────────────── */

bool uwm_bar_manager_create(struct uwm_server *server)
{
	struct uwm_bar_manager *manager = calloc(1, sizeof(*manager));
	if (!manager)
		return false;

	manager->server = server;
	wl_list_init(&manager->groups);

	manager->global = wl_global_create(server->wl_display,
		&zwp_uwm_bar_v1_interface, 1, manager, bar_manager_bind);
	if (!manager->global) {
		free(manager);
		return false;
	}

	server->bar_manager = manager;
	wlr_log(WLR_INFO, "UWM bar manager created");
	return true;
}

void uwm_bar_manager_destroy(struct uwm_server *server)
{
	if (!server || !server->bar_manager)
		return;

	struct uwm_bar_manager *manager = server->bar_manager;

	/* Remove pending idle callback */
	if (server->bar_idle_source) {
		wl_event_source_remove(server->bar_idle_source);
		server->bar_idle_source = NULL;
	}

	/* Destroy all workspace groups */
	struct uwm_workspace_group *group, *tmp;
	wl_list_for_each_safe(group, tmp, &manager->groups, link) {
		wl_list_remove(&group->link);
		if (group->resource)
			wl_resource_set_user_data(group->resource, NULL);
		free(group);
	}

	if (manager->global)
		wl_global_destroy(manager->global);

	free(manager);
	server->bar_manager = NULL;
}

void uwm_bar_send_workspace_state(struct uwm_workspace_group *group)
{
	if (!group || !group->resource)
		return;

	struct uwm_server *server = group->output
		? group->output->server
		: group->manager->server;

	if (!server)
		return;

	struct uwm_output *out = group->output
		? group->output
		: server->active_output;

	for (uint32_t i = 0; i < UWM_WORKSPACE_COUNT; i++) {
		struct uwm_workspace *ws = &server->workspaces.workspaces[i];
		bool occupied = !wl_list_empty(&ws->toplevels)
			|| !wl_list_empty(&ws->floating_windows);
		bool active = (out && i == out->current_workspace);
		zwp_uwm_workspace_group_v1_send_workspace(
			group->resource, i, active ? 1 : 0,
			occupied ? 1 : 0);
	}
}

void uwm_bar_send_focused_title(struct uwm_workspace_group *group,
		const char *title)
{
	if (!group || !group->resource)
		return;
	zwp_uwm_workspace_group_v1_send_focused_title(
		group->resource, title ? title : "");
}

void uwm_bar_send_all(struct uwm_server *server)
{
	if (!server || !server->bar_manager)
		return;

	struct uwm_workspace_group *group;
	wl_list_for_each(group, &server->bar_manager->groups, link) {
		uwm_bar_send_workspace_state(group);

		const char *title = "";
		if (group->output) {
			struct uwm_workspace *ws = &server->workspaces
				.workspaces[group->output->current_workspace];
			if (ws && ws->focused && ws->focused->xdg_toplevel->title)
				title = ws->focused->xdg_toplevel->title;
		} else if (server->active_output) {
			struct uwm_workspace *ws = &server->workspaces
				.workspaces[server->active_output->current_workspace];
			if (ws && ws->focused && ws->focused->xdg_toplevel->title)
				title = ws->focused->xdg_toplevel->title;
		}
		uwm_bar_send_focused_title(group, title);
		zwp_uwm_workspace_group_v1_send_done(group->resource);
	}
}

void uwm_bar_send_output(struct uwm_output *output)
{
	if (!output || !output->server || !output->server->bar_manager)
		return;

	struct uwm_server *server = output->server;
	if (!server->bar_idle_source) {
		struct wl_event_loop *loop = wl_display_get_event_loop(
			server->wl_display);
		server->bar_idle_source = wl_event_loop_add_idle(loop,
			bar_send_idle_handler, server);
	}
}

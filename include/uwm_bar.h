#ifndef UWM_BAR_H
#define UWM_BAR_H

#include <stdbool.h>
#include <wayland-server-core.h>

struct uwm_server;
struct uwm_output;

struct uwm_workspace_group {
	struct wl_resource *resource;
	struct uwm_output *output;
	struct uwm_bar_manager *manager;
	struct wl_list link;
};

struct uwm_bar_manager {
	struct uwm_server *server;
	struct wl_global *global;
	struct wl_list groups;
};

bool uwm_bar_manager_create(struct uwm_server *server);
void uwm_bar_manager_destroy(struct uwm_server *server);
void uwm_bar_send_workspace_state(struct uwm_workspace_group *group);
void uwm_bar_send_focused_title(struct uwm_workspace_group *group,
	const char *title);
void uwm_bar_send_all(struct uwm_server *server);
void uwm_bar_send_output(struct uwm_output *output);

#endif /* UWM_BAR_H */

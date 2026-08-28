#ifndef IDLE_INHIBIT_H
#define IDLE_INHIBIT_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>

struct uwm_server;

struct uwm_idle_inhibitor {
	struct wl_list link;
	struct uwm_server *server;
	struct wlr_idle_inhibitor_v1 *inhibitor;

	struct wl_listener destroy;
};

struct uwm_idle_inhibit {
	struct uwm_server *server;
	struct wlr_idle_inhibit_manager_v1 *manager;

	struct wl_listener new_inhibitor;
};

bool idle_inhibit_create(struct uwm_server *server);
void idle_inhibit_destroy(struct uwm_server *server);

#endif /* IDLE_INHIBIT_H */

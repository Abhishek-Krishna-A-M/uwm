#include <stdlib.h>
#include <wlr/util/log.h>
#include "idle_inhibit.h"
#include "server.h"

static void idle_inhibitor_handle_destroy(struct wl_listener *listener, void *data) {
	struct uwm_idle_inhibitor *inhibitor =
		wl_container_of(listener, inhibitor, destroy);

	wl_list_remove(&inhibitor->link);
	wl_list_remove(&inhibitor->destroy.link);

	free(inhibitor);
}

static void idle_inhibit_handle_new_inhibitor(struct wl_listener *listener, void *data) {
	struct uwm_idle_inhibit *idle_inhibit =
		wl_container_of(listener, idle_inhibit, new_inhibitor);
	struct wlr_idle_inhibitor_v1 *wlr_inhibitor = data;

	struct uwm_idle_inhibitor *inhibitor = calloc(1, sizeof(*inhibitor));
	if (!inhibitor) {
		return;
	}

	inhibitor->server = idle_inhibit->server;
	inhibitor->inhibitor = wlr_inhibitor;

	/* Set up listener for inhibitor destruction */
	inhibitor->destroy.notify = idle_inhibitor_handle_destroy;
	wl_signal_add(&wlr_inhibitor->events.destroy, &inhibitor->destroy);

	/* Add to server's inhibitor list */
	wl_list_insert(&idle_inhibit->server->idle_inhibitors, &inhibitor->link);

	wlr_log(WLR_INFO, "Idle inhibitor created");
}

bool idle_inhibit_create(struct uwm_server *server) {
	/* Create idle inhibit protocol */
	server->idle_inhibit.manager = wlr_idle_inhibit_v1_create(server->wl_display);
	if (!server->idle_inhibit.manager) {
		wlr_log(WLR_ERROR, "Failed to create idle inhibit manager");
		return false;
	}

	server->idle_inhibit.server = server;

	/* Set up listener for new inhibitors */
	server->idle_inhibit.new_inhibitor.notify = idle_inhibit_handle_new_inhibitor;
	wl_signal_add(&server->idle_inhibit.manager->events.new_inhibitor,
		&server->idle_inhibit.new_inhibitor);

	/* Initialize inhibitors list */
	wl_list_init(&server->idle_inhibitors);

	wlr_log(WLR_INFO, "Idle inhibit protocol initialized");
	return true;
}

void idle_inhibit_destroy(struct uwm_server *server) {
	if (!server->idle_inhibit.manager)
		return;

	/* Destroy all inhibitors */
	struct uwm_idle_inhibitor *inhibitor, *tmp;
	wl_list_for_each_safe(inhibitor, tmp, &server->idle_inhibitors, link) {
		wl_list_remove(&inhibitor->link);
		wl_list_remove(&inhibitor->destroy.link);
		free(inhibitor);
	}

	/* Remove listener */
	wl_list_remove(&server->idle_inhibit.new_inhibitor.link);

	wlr_log(WLR_INFO, "Idle inhibit protocol destroyed");
}

bool idle_inhibit_is_inhibited(struct uwm_server *server) {
	return !wl_list_empty(&server->idle_inhibitors);
}

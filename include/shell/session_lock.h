#ifndef SESSION_LOCK_H
#define SESSION_LOCK_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_session_lock_v1.h>

struct uwm_server;

struct uwm_session_lock {
	struct uwm_server *server;
	struct wlr_session_lock_manager_v1 *manager;
	struct wlr_session_lock_v1 *lock;

	struct wl_listener new_lock;
	struct wl_listener manager_destroy;

	/* Per-lock listeners (reused, only one active lock at a time) */
	struct wl_listener new_surface;
	struct wl_listener unlock;
	struct wl_listener lock_destroy;
};

bool session_lock_create(struct uwm_server *server);
void session_lock_destroy(struct uwm_server *server);

#endif

#include <stdlib.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_output.h>
#include "session_lock.h"
#include "server.h"
#include "output.h"
#include "window.h"

static void handle_lock_surface_destroy(struct wl_listener *listener, void *data);

static void handle_new_lock_surface(struct wl_listener *listener, void *data) {
	struct uwm_session_lock *session_lock =
		wl_container_of(listener, session_lock, new_surface);
	struct wlr_session_lock_surface_v1 *lock_surface = data;
	struct uwm_server *server = session_lock->server;

	struct uwm_output *output = lock_surface->output->data;

	struct wlr_scene_tree *scene_tree = wlr_scene_subsurface_tree_create(
		output->layer_lock, lock_surface->surface);
	if (!scene_tree) {
		wlr_log(WLR_ERROR, "Failed to create lock surface scene tree");
		return;
	}
	lock_surface->surface->data = scene_tree;

	wlr_scene_node_set_position(&scene_tree->node, output->lx, output->ly);
	wlr_session_lock_surface_v1_configure(lock_surface,
		output->wlr_output->width, output->wlr_output->height);

	lock_surface->data = output;
	output->lock_surface = lock_surface;

	output->lock_surface_destroy.notify = handle_lock_surface_destroy;
	wl_signal_add(&lock_surface->events.destroy,
		&output->lock_surface_destroy);

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	if (keyboard) {
		wlr_seat_keyboard_notify_enter(server->seat,
			lock_surface->surface,
			keyboard->keycodes, keyboard->num_keycodes,
			&keyboard->modifiers);
	}
}

static void handle_lock_unlock(struct wl_listener *listener, void *data) {
	struct uwm_session_lock *session_lock =
		wl_container_of(listener, session_lock, unlock);
	struct uwm_server *server = session_lock->server;

	server->locked = false;

	wlr_seat_keyboard_notify_clear_focus(server->seat);

	struct uwm_workspace *ws =
		&server->workspaces.workspaces[server->workspaces.current];
	if (ws->focused) {
		focus_toplevel(ws->focused);
	}

	wl_list_remove(&session_lock->new_surface.link);
	wl_list_remove(&session_lock->unlock.link);
	wl_list_remove(&session_lock->lock_destroy.link);

	session_lock->lock = NULL;
}

static void handle_lock_destroy(struct wl_listener *listener, void *data) {
	struct uwm_session_lock *session_lock =
		wl_container_of(listener, session_lock, lock_destroy);
	struct uwm_server *server = session_lock->server;

	/* Keep server->locked true so screen stays locked if lock client crashed.
	 * A new lock attempt will be accepted since session_lock->lock is cleared. */

	wl_list_remove(&session_lock->new_surface.link);
	wl_list_remove(&session_lock->unlock.link);
	wl_list_remove(&session_lock->lock_destroy.link);

	session_lock->lock = NULL;

	wlr_seat_keyboard_notify_clear_focus(server->seat);
}

static void handle_lock_surface_destroy(struct wl_listener *listener, void *data) {
	struct uwm_output *output = wl_container_of(listener, output, lock_surface_destroy);
	output->lock_surface = NULL;
	wl_list_remove(&output->lock_surface_destroy.link);
}

static void handle_new_lock(struct wl_listener *listener, void *data) {
	struct uwm_session_lock *session_lock =
		wl_container_of(listener, session_lock, new_lock);
	struct wlr_session_lock_v1 *wlr_lock = data;
	struct uwm_server *server = session_lock->server;

	if (session_lock->lock) {
		wlr_log(WLR_INFO, "Already locked, rejecting new lock");
		wlr_session_lock_v1_destroy(wlr_lock);
		return;
	}

	session_lock->lock = wlr_lock;
	wlr_lock->data = session_lock;
	server->locked = true;

	wlr_seat_keyboard_notify_clear_focus(server->seat);

	session_lock->new_surface.notify = handle_new_lock_surface;
	wl_signal_add(&wlr_lock->events.new_surface,
		&session_lock->new_surface);
	session_lock->unlock.notify = handle_lock_unlock;
	wl_signal_add(&wlr_lock->events.unlock,
		&session_lock->unlock);
	session_lock->lock_destroy.notify = handle_lock_destroy;
	wl_signal_add(&wlr_lock->events.destroy,
		&session_lock->lock_destroy);

	wlr_session_lock_v1_send_locked(wlr_lock);

	wlr_log(WLR_INFO, "Session locked");
}

static void handle_manager_destroy(struct wl_listener *listener, void *data) {
	struct uwm_session_lock *session_lock =
		wl_container_of(listener, session_lock, manager_destroy);
	session_lock->manager = NULL;
}

bool session_lock_create(struct uwm_server *server) {
	server->session_lock.manager =
		wlr_session_lock_manager_v1_create(server->wl_display);
	if (!server->session_lock.manager) {
		wlr_log(WLR_ERROR, "Failed to create session lock manager");
		return false;
	}

	server->session_lock.server = server;
	server->session_lock.lock = NULL;
	server->locked = false;

	server->session_lock.new_lock.notify = handle_new_lock;
	wl_signal_add(&server->session_lock.manager->events.new_lock,
		&server->session_lock.new_lock);

	server->session_lock.manager_destroy.notify = handle_manager_destroy;
	wl_signal_add(&server->session_lock.manager->events.destroy,
		&server->session_lock.manager_destroy);

	wlr_log(WLR_INFO, "Session lock protocol initialized");
	return true;
}

void session_lock_destroy(struct uwm_server *server) {
	wl_list_remove(&server->session_lock.new_lock.link);
	wl_list_remove(&server->session_lock.manager_destroy.link);
}

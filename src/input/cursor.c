#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>
#include <libinput.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include "input.h"
#include "config.h"
#include "window.h"
#include "bsp.h"
#include "floating.h"
#include "layout.h"
#include "server.h"
#include "output.h"

/* ========== Cursor motion ========== */

static void process_cursor_move(struct uwm_server *server) {
	struct uwm_toplevel *toplevel = server->grabbed_toplevel;
	double new_x = server->cursor->x - server->grab_x;
	double new_y = server->cursor->y - server->grab_y;

	if (toplevel->floating) {
		toplevel->float_x = (int)new_x;
		toplevel->float_y = (int)new_y;
	}

	wlr_scene_node_set_position(&toplevel->scene_tree->node, new_x, new_y);
}

static void process_cursor_resize(struct uwm_server *server) {
	struct uwm_toplevel *toplevel = server->grabbed_toplevel;
	double border_x = server->cursor->x - server->grab_x;
	double border_y = server->cursor->y - server->grab_y;
	int new_left = server->grab_geobox.x;
	int new_right = server->grab_geobox.x + server->grab_geobox.width;
	int new_top = server->grab_geobox.y;
	int new_bottom = server->grab_geobox.y + server->grab_geobox.height;

	if (server->resize_edges & WLR_EDGE_TOP) {
		new_top = border_y;
		if (new_top >= new_bottom) new_top = new_bottom - 1;
	} else if (server->resize_edges & WLR_EDGE_BOTTOM) {
		new_bottom = border_y;
		if (new_bottom <= new_top) new_bottom = new_top + 1;
	}
	if (server->resize_edges & WLR_EDGE_LEFT) {
		new_left = border_x;
		if (new_left >= new_right) new_left = new_right - 1;
	} else if (server->resize_edges & WLR_EDGE_RIGHT) {
		new_right = border_x;
		if (new_right <= new_left) new_right = new_left + 1;
	}

	int new_width = new_right - new_left;
	int new_height = new_bottom - new_top;

	if (toplevel->floating) {
		if (new_width < floating_min_width) {
			new_width = floating_min_width;
			if (server->resize_edges & WLR_EDGE_RIGHT)
				new_right = new_left + floating_min_width;
			else
				new_left = new_right - floating_min_width;
		}
		if (new_height < floating_min_height) {
			new_height = floating_min_height;
			if (server->resize_edges & WLR_EDGE_BOTTOM)
				new_bottom = new_top + floating_min_height;
			else
				new_top = new_bottom - floating_min_height;
		}
		toplevel->float_x = new_left;
		toplevel->float_y = new_top;
		toplevel->float_width = new_width;
		toplevel->float_height = new_height;
	}

	wlr_scene_node_set_position(&toplevel->scene_tree->node, new_left, new_top);
	/* 1.1 coalesce: defer client configure to frame */
	server->pending_x = new_left;
	server->pending_y = new_top;
	server->pending_w = new_width;
	server->pending_h = new_height;
	server->pending_resize = true;
}

static void process_cursor_motion(struct uwm_server *server, uint32_t time) {
	if (server->locked) {
		reset_cursor_mode(server);
	}

	if (server->cursor_mode == UWM_CURSOR_MOVE) {
		process_cursor_move(server);
		return;
	} else if (server->cursor_mode == UWM_CURSOR_RESIZE) {
		process_cursor_resize(server);
		return;
	}

	double sx, sy;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *surface = NULL;
	struct uwm_toplevel *toplevel = desktop_toplevel_at(server,
		server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	if (surface) {
		/* Only emit enter when the pointed surface actually changed;
		 * wlr_seat_pointer_notify_enter on an unchanged surface is
		 * pure overhead on every motion event. */
		if (surface != seat->pointer_state.focused_surface) {
			wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
		}
		wlr_seat_pointer_notify_motion(seat, time, sx, sy);

		if (toplevel) {
			struct uwm_workspace *ws = toplevel->workspace;
			if (ws == &server->workspaces.workspaces[server->workspaces.current]
					&& ws->focus_follows_pointer) {
				focus_toplevel(toplevel);
			}
		}
	} else if (seat->pointer_state.focused_surface) {
		/* Sway parity: keep pointer focus during drag/select (foot).
		 * Clearing focus while a button is held breaks selection when
		 * cursor leaves window (foot stops). Sway keeps grab. */
		if (seat->pointer_state.button_count == 0 && !seat->pointer_state.grab) {
			/* Cursor left all surfaces: drop pointer focus so clicks and
			 * scrolls on the empty desktop don't land on the last window. */
			wlr_seat_pointer_clear_focus(seat);
		} else {
			/* Drag in progress: keep sending motion to focused surface
			 * even outside window (selection). Compute sx/sy relative to
			 * focused surface's toplevel. */
			struct wlr_surface *focused = seat->pointer_state.focused_surface;
			struct wlr_surface *root = wlr_surface_get_root_surface(focused);
			struct uwm_toplevel *ftl = NULL;
			struct uwm_toplevel *tmp;
			wl_list_for_each(tmp, &server->toplevels, link) {
				if (toplevel_surface(tmp) == root || toplevel_surface(tmp) == focused) {
					ftl = tmp;
					break;
				}
			}
			if (ftl && ftl->scene_tree) {
				struct wlr_box geo = toplevel_geometry(ftl);
				double tx = ftl->scene_tree->node.x + geo.x;
				double ty = ftl->scene_tree->node.y + geo.y;
				double nsx = server->cursor->x - tx;
				double nsy = server->cursor->y - ty;
				wlr_seat_pointer_notify_motion(seat, time, nsx, nsy);
			} else {
				/* fallback: use last sx/sy + delta */
				wlr_seat_pointer_notify_motion(seat, time,
					seat->pointer_state.sx, seat->pointer_state.sy);
			}
		}
	}
}

void server_cursor_motion(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

void server_cursor_motion_absolute(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
	process_cursor_motion(server, event->time_msec);
}

void server_cursor_button(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;

	if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
		uint32_t modifiers = 0;
		struct wlr_keyboard *kbd = wlr_seat_get_keyboard(server->seat);
		if (kbd)
			modifiers = wlr_keyboard_get_modifiers(kbd);

		double sx, sy;
		struct wlr_surface *surface = NULL;
		struct uwm_toplevel *toplevel = desktop_toplevel_at(server,
			server->cursor->x, server->cursor->y, &surface, &sx, &sy);

		if ((modifiers & WLR_MODIFIER_LOGO) && toplevel) {
			focus_toplevel(toplevel);
			if (event->button == BTN_LEFT) {
				begin_interactive(toplevel, UWM_CURSOR_MOVE, 0);
				return;
			} else if (event->button == BTN_RIGHT) {
				if (!toplevel->floating)
					toggle_floating(toplevel);
				begin_interactive(toplevel, UWM_CURSOR_RESIZE,
					WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM);
				return;
			}
		}

		server->last_button_serial = wlr_seat_pointer_notify_button(
			server->seat, event->time_msec, event->button, event->state);
		wlr_seat_pointer_notify_frame(server->seat);

		if (toplevel)
			focus_toplevel(toplevel);
	} else {
		wlr_seat_pointer_notify_button(server->seat,
			event->time_msec, event->button, event->state);
		wlr_seat_pointer_notify_frame(server->seat);
		reset_cursor_mode(server);
	}
}

void server_cursor_axis(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;
	wlr_seat_pointer_notify_axis(server->seat,
		event->time_msec, event->orientation, event->delta,
		event->delta_discrete, event->source, event->relative_direction);
}

void server_cursor_frame(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, cursor_frame);
	if (server->pending_resize && server->grabbed_toplevel && server->cursor_mode == UWM_CURSOR_RESIZE) {
		toplevel_set_size(server->grabbed_toplevel, server->pending_w, server->pending_h);
		server->pending_resize = false;
	}
	wlr_seat_pointer_notify_frame(server->seat);
}

void server_cursor_swipe_begin(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, cursor_swipe_begin);
	struct wlr_pointer_swipe_begin_event *event = data;
	if (server->pointer_gestures)
		wlr_pointer_gestures_v1_send_swipe_begin(
			server->pointer_gestures, server->seat,
			event->time_msec, event->fingers);
}

void server_cursor_swipe_update(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, cursor_swipe_update);
	struct wlr_pointer_swipe_update_event *event = data;
	if (server->pointer_gestures)
		wlr_pointer_gestures_v1_send_swipe_update(
			server->pointer_gestures, server->seat,
			event->time_msec, event->dx, event->dy);
}

void server_cursor_swipe_end(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, cursor_swipe_end);
	struct wlr_pointer_swipe_end_event *event = data;
	if (server->pointer_gestures)
		wlr_pointer_gestures_v1_send_swipe_end(
			server->pointer_gestures, server->seat,
			event->time_msec, event->cancelled);
}

void server_cursor_pinch_begin(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, cursor_pinch_begin);
	struct wlr_pointer_pinch_begin_event *event = data;
	if (server->pointer_gestures)
		wlr_pointer_gestures_v1_send_pinch_begin(
			server->pointer_gestures, server->seat,
			event->time_msec, event->fingers);
}

void server_cursor_pinch_update(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, cursor_pinch_update);
	struct wlr_pointer_pinch_update_event *event = data;
	if (server->pointer_gestures)
		wlr_pointer_gestures_v1_send_pinch_update(
			server->pointer_gestures, server->seat,
			event->time_msec, event->dx, event->dy,
			event->scale, event->rotation);
}

void server_cursor_pinch_end(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, cursor_pinch_end);
	struct wlr_pointer_pinch_end_event *event = data;
	if (server->pointer_gestures)
		wlr_pointer_gestures_v1_send_pinch_end(
			server->pointer_gestures, server->seat,
			event->time_msec, event->cancelled);
}

void server_cursor_hold_begin(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, cursor_hold_begin);
	struct wlr_pointer_hold_begin_event *event = data;
	if (server->pointer_gestures)
		wlr_pointer_gestures_v1_send_hold_begin(
			server->pointer_gestures, server->seat,
			event->time_msec, event->fingers);
}

void server_cursor_hold_end(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, cursor_hold_end);
	struct wlr_pointer_hold_end_event *event = data;
	if (server->pointer_gestures)
		wlr_pointer_gestures_v1_send_hold_end(
			server->pointer_gestures, server->seat,
			event->time_msec, event->cancelled);
}

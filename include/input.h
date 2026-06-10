#ifndef INPUT_H
#define INPUT_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include "server.h"
#include "window.h"

struct uwm_keyboard {
	struct wl_list link;
	struct uwm_server *server;
	struct wlr_keyboard *wlr_keyboard;

	struct wl_event_source *repeat_timer;
	xkb_keysym_t repeat_sym;
	uint32_t repeat_keycode;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

void reset_cursor_mode(struct uwm_server *server);
void begin_interactive(struct uwm_toplevel *toplevel, enum uwm_cursor_mode mode, uint32_t edges);

void server_new_input(struct wl_listener *listener, void *data);
void seat_request_cursor(struct wl_listener *listener, void *data);
void seat_pointer_focus_change(struct wl_listener *listener, void *data);
void seat_request_set_selection(struct wl_listener *listener, void *data);

void server_cursor_motion(struct wl_listener *listener, void *data);
void server_cursor_motion_absolute(struct wl_listener *listener, void *data);
void server_cursor_button(struct wl_listener *listener, void *data);
void server_cursor_axis(struct wl_listener *listener, void *data);
void server_cursor_frame(struct wl_listener *listener, void *data);

#endif /* INPUT_H */

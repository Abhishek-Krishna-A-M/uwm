#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>
#include <libinput.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/backend/libinput.h>
#include "input.h"
#include "config.h"
#include "window.h"
#include "bsp.h"
#include "floating.h"
#include "layout.h"
#include "server.h"

void reset_cursor_mode(struct uwm_server *server) {
	/* Reset the cursor mode to passthrough. */
	server->cursor_mode = UWM_CURSOR_PASSTHROUGH;
	server->grabbed_toplevel = NULL;
}

void begin_interactive(struct uwm_toplevel *toplevel, enum uwm_cursor_mode mode, uint32_t edges) {
	struct uwm_server *server = toplevel->server;

	if (!toplevel->floating) {
		if (mode == UWM_CURSOR_MOVE) {
			toggle_floating(toplevel);
		} else {
			return;
		}
	}

	server->grabbed_toplevel = toplevel;
	server->cursor_mode = mode;

	if (mode == UWM_CURSOR_MOVE) {
		if (toplevel->floating) {
			server->grab_x = server->cursor->x - toplevel->float_x;
			server->grab_y = server->cursor->y - toplevel->float_y;
		} else {
			server->grab_x = server->cursor->x - toplevel->scene_tree->node.x;
			server->grab_y = server->cursor->y - toplevel->scene_tree->node.y;
		}
	} else {
		struct wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;

		if (toplevel->floating) {
			double border_x = toplevel->float_x +
				((edges & WLR_EDGE_RIGHT) ? toplevel->float_width : 0);
			double border_y = toplevel->float_y +
				((edges & WLR_EDGE_BOTTOM) ? toplevel->float_height : 0);
			server->grab_x = server->cursor->x - border_x;
			server->grab_y = server->cursor->y - border_y;

			server->grab_geobox.x = toplevel->float_x;
			server->grab_geobox.y = toplevel->float_y;
			server->grab_geobox.width = toplevel->float_width;
			server->grab_geobox.height = toplevel->float_height;
		} else {
			double border_x = (toplevel->scene_tree->node.x + geo_box->x) +
				((edges & WLR_EDGE_RIGHT) ? geo_box->width : 0);
			double border_y = (toplevel->scene_tree->node.y + geo_box->y) +
				((edges & WLR_EDGE_BOTTOM) ? geo_box->height : 0);
			server->grab_x = server->cursor->x - border_x;
			server->grab_y = server->cursor->y - border_y;

			server->grab_geobox = *geo_box;
			server->grab_geobox.x += toplevel->scene_tree->node.x;
			server->grab_geobox.y += toplevel->scene_tree->node.y;
		}

		server->resize_edges = edges;
	}
}

static bool handle_keybinding(struct uwm_server *server, xkb_keysym_t sym, uint32_t modifiers);

static int keyboard_repeat_handler(void *data) {
	struct uwm_keyboard *keyboard = data;
	struct uwm_server *server = keyboard->server;

	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
	if (!(modifiers & WLR_MODIFIER_LOGO)) {
		wl_event_source_timer_update(keyboard->repeat_timer, 0);
		return 0;
	}

	handle_keybinding(server, keyboard->repeat_sym, modifiers);

	int rate = server->config.key_repeat_rate;
	if (rate > 0) {
		wl_event_source_timer_update(keyboard->repeat_timer, 1000 / rate);
	}
	return 0;
}

static void keyboard_repeat_stop(struct uwm_keyboard *keyboard) {
	if (keyboard->repeat_timer)
		wl_event_source_timer_update(keyboard->repeat_timer, 0);
}

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
	struct uwm_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
	if (!(modifiers & WLR_MODIFIER_LOGO))
		keyboard_repeat_stop(keyboard);
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat, &keyboard->wlr_keyboard->modifiers);
}

static void bsp_arrange_current_workspace(struct uwm_server *server)
{
	struct uwm_workspace *ws = &server->workspaces.workspaces[server->workspaces.current];
	int w, h;
	get_output_size(server, &w, &h);
	bsp_arrange(ws, w, h);
}

static struct uwm_workspace *current_ws(struct uwm_server *server)
{
	return &server->workspaces.workspaces[server->workspaces.current];
}

static void close_focused(struct uwm_server *server)
{
	struct uwm_toplevel *focused = current_ws(server)->focused;
	if (focused && focused->xdg_toplevel)
		wlr_xdg_toplevel_send_close(focused->xdg_toplevel);
}

static void force_close_focused(struct uwm_server *server)
{
	struct uwm_toplevel *focused = current_ws(server)->focused;
	if (focused && focused->xdg_toplevel) {
		struct wl_client *client = wl_resource_get_client(
			focused->xdg_toplevel->base->resource);
		if (client)
			wl_client_destroy(client);
	}
}

static bool handle_keybinding(struct uwm_server *server, xkb_keysym_t sym, uint32_t modifiers) {
	sym = xkb_keysym_to_lower(sym);
	switch (sym) {
	case XKB_KEY_Escape:
		wl_display_terminate(server->wl_display);
		break;
	case XKB_KEY_Tab:
		if (server->workspaces.last != server->workspaces.current)
			workspace_switch(server, server->workspaces.last);
		break;
	case XKB_KEY_h: {
		if ((modifiers & WLR_MODIFIER_SHIFT) && (modifiers & WLR_MODIFIER_ALT)) {
			struct uwm_toplevel *focused = current_ws(server)->focused;
			if (focused && focused->floating) {
				focused->float_x += 20;
				focused->float_width -= 20;
				if (focused->float_width < 100)
					focused->float_width = 100;
				wlr_scene_node_set_position(&focused->scene_tree->node,
					focused->float_x, focused->float_y);
				wlr_xdg_toplevel_set_size(focused->xdg_toplevel,
					focused->float_width, focused->float_height);
			}
		} else if (modifiers & WLR_MODIFIER_SHIFT) {
			bsp_swap_direction(current_ws(server),
				current_ws(server)->focused, 0);
			bsp_arrange_current_workspace(server);
		} else if (modifiers & WLR_MODIFIER_ALT) {
			struct uwm_toplevel *focused = current_ws(server)->focused;
			if (focused && focused->floating) {
				focused->float_x -= 20;
				focused->float_width += 20;
				wlr_scene_node_set_position(&focused->scene_tree->node,
					focused->float_x, focused->float_y);
				wlr_xdg_toplevel_set_size(focused->xdg_toplevel,
					focused->float_width, focused->float_height);
			} else {
				struct uwm_workspace *ws = current_ws(server);
				bsp_resize(ws, ws->focused, -0.05f);
				bsp_arrange_current_workspace(server);
			}
		} else {
			struct uwm_toplevel *focused = current_ws(server)->focused;
			if (focused && focused->floating) {
				focused->float_x -= 20;
				wlr_scene_node_set_position(&focused->scene_tree->node,
					focused->float_x, focused->float_y);
			} else {
				struct uwm_toplevel *target = bsp_focus_left(current_ws(server));
				if (target) focus_toplevel(target);
			}
		}
		break;
	}
	case XKB_KEY_j: {
		if ((modifiers & WLR_MODIFIER_SHIFT) && (modifiers & WLR_MODIFIER_ALT)) {
			struct uwm_toplevel *focused = current_ws(server)->focused;
			if (focused && focused->floating) {
				focused->float_height -= 20;
				if (focused->float_height < 60)
					focused->float_height = 60;
				wlr_scene_node_set_position(&focused->scene_tree->node,
					focused->float_x, focused->float_y);
				wlr_xdg_toplevel_set_size(focused->xdg_toplevel,
					focused->float_width, focused->float_height);
			}
		} else if (modifiers & WLR_MODIFIER_SHIFT) {
			bsp_swap_direction(current_ws(server),
				current_ws(server)->focused, 3);
			bsp_arrange_current_workspace(server);
		} else if (modifiers & WLR_MODIFIER_ALT) {
			struct uwm_toplevel *focused = current_ws(server)->focused;
			if (focused && focused->floating) {
				focused->float_height += 20;
				wlr_scene_node_set_position(&focused->scene_tree->node,
					focused->float_x, focused->float_y);
				wlr_xdg_toplevel_set_size(focused->xdg_toplevel,
					focused->float_width, focused->float_height);
			} else {
				struct uwm_workspace *ws = current_ws(server);
				bsp_resize(ws, ws->focused, -0.05f);
				bsp_arrange_current_workspace(server);
			}
		} else {
			struct uwm_toplevel *focused = current_ws(server)->focused;
			if (focused && focused->floating) {
				focused->float_y += 20;
				wlr_scene_node_set_position(&focused->scene_tree->node,
					focused->float_x, focused->float_y);
			} else {
				struct uwm_toplevel *target = bsp_focus_down(current_ws(server));
				if (target) focus_toplevel(target);
			}
		}
		break;
	}
	case XKB_KEY_k: {
		if ((modifiers & WLR_MODIFIER_SHIFT) && (modifiers & WLR_MODIFIER_ALT)) {
			struct uwm_toplevel *focused = current_ws(server)->focused;
			if (focused && focused->floating) {
				focused->float_y += 20;
				focused->float_height -= 20;
				if (focused->float_height < 60)
					focused->float_height = 60;
				wlr_scene_node_set_position(&focused->scene_tree->node,
					focused->float_x, focused->float_y);
				wlr_xdg_toplevel_set_size(focused->xdg_toplevel,
					focused->float_width, focused->float_height);
			}
		} else if (modifiers & WLR_MODIFIER_SHIFT) {
			bsp_swap_direction(current_ws(server),
				current_ws(server)->focused, 2);
			bsp_arrange_current_workspace(server);
		} else if (modifiers & WLR_MODIFIER_ALT) {
			struct uwm_toplevel *focused = current_ws(server)->focused;
			if (focused && focused->floating) {
				focused->float_y -= 20;
				focused->float_height += 20;
				wlr_scene_node_set_position(&focused->scene_tree->node,
					focused->float_x, focused->float_y);
				wlr_xdg_toplevel_set_size(focused->xdg_toplevel,
					focused->float_width, focused->float_height);
			} else {
				struct uwm_workspace *ws = current_ws(server);
				bsp_resize(ws, ws->focused, 0.05f);
				bsp_arrange_current_workspace(server);
			}
		} else {
			struct uwm_toplevel *focused = current_ws(server)->focused;
			if (focused && focused->floating) {
				focused->float_y -= 20;
				wlr_scene_node_set_position(&focused->scene_tree->node,
					focused->float_x, focused->float_y);
			} else {
				struct uwm_toplevel *target = bsp_focus_up(current_ws(server));
				if (target) focus_toplevel(target);
			}
		}
		break;
	}
	case XKB_KEY_l: {
		if ((modifiers & WLR_MODIFIER_SHIFT) && (modifiers & WLR_MODIFIER_ALT)) {
			struct uwm_toplevel *focused = current_ws(server)->focused;
			if (focused && focused->floating) {
				focused->float_width -= 20;
				if (focused->float_width < 100)
					focused->float_width = 100;
				wlr_scene_node_set_position(&focused->scene_tree->node,
					focused->float_x, focused->float_y);
				wlr_xdg_toplevel_set_size(focused->xdg_toplevel,
					focused->float_width, focused->float_height);
			}
		} else if (modifiers & WLR_MODIFIER_SHIFT) {
			bsp_swap_direction(current_ws(server),
				current_ws(server)->focused, 1);
			bsp_arrange_current_workspace(server);
		} else if (modifiers & WLR_MODIFIER_ALT) {
			struct uwm_toplevel *focused = current_ws(server)->focused;
			if (focused && focused->floating) {
				focused->float_width += 20;
				wlr_scene_node_set_position(&focused->scene_tree->node,
					focused->float_x, focused->float_y);
				wlr_xdg_toplevel_set_size(focused->xdg_toplevel,
					focused->float_width, focused->float_height);
			} else {
				struct uwm_workspace *ws = current_ws(server);
				bsp_resize(ws, ws->focused, 0.05f);
				bsp_arrange_current_workspace(server);
			}
		} else {
			struct uwm_toplevel *focused = current_ws(server)->focused;
			if (focused && focused->floating) {
				focused->float_x += 20;
				wlr_scene_node_set_position(&focused->scene_tree->node,
					focused->float_x, focused->float_y);
			} else {
				struct uwm_toplevel *target = bsp_focus_right(current_ws(server));
				if (target) focus_toplevel(target);
			}
		}
		break;
	}
	case XKB_KEY_r:
		if (modifiers & WLR_MODIFIER_SHIFT) {
			bsp_rotate_focused_split(current_ws(server));
			bsp_arrange_current_workspace(server);
		} else if (fork() == 0) {
			execl("/usr/bin/fuzzel", "fuzzel", (void *)NULL);
			_exit(1);
		}
		break;
	case XKB_KEY_Return:
		if (fork() == 0) {
			execl("/usr/bin/foot", "foot", (void *)NULL);
			_exit(1);
		}
		break;
	case XKB_KEY_c:
		workspace_cycle_next(server);
		break;
	case XKB_KEY_f:
		if (modifiers & WLR_MODIFIER_SHIFT)
			toggle_floating(current_ws(server)->focused);
		else
			toggle_fullscreen(current_ws(server)->focused);
		break;
	case XKB_KEY_m: {
		if (modifiers & WLR_MODIFIER_SHIFT)
			toggle_monocle(current_ws(server));
		/* Tabbed mode is out of scope until core is stable */
		break;
	}
	case XKB_KEY_t:
		set_bsp_mode(current_ws(server));
		break;
	case XKB_KEY_w: {
		if (modifiers & WLR_MODIFIER_SHIFT)
			force_close_focused(server);
		else
			close_focused(server);
		break;
	}
	default: {
		uint32_t ws = 0;
		bool is_num = false;
		switch (sym) {
		case XKB_KEY_1: case XKB_KEY_exclam:
			ws = 0; is_num = true; break;
		case XKB_KEY_2: case XKB_KEY_at: case XKB_KEY_quotedbl:
			ws = 1; is_num = true; break;
		case XKB_KEY_3: case XKB_KEY_numbersign:
		case XKB_KEY_sterling: case XKB_KEY_section:
			ws = 2; is_num = true; break;
		case XKB_KEY_4: case XKB_KEY_dollar:
			ws = 3; is_num = true; break;
		case XKB_KEY_5: case XKB_KEY_percent:
			ws = 4; is_num = true; break;
		case XKB_KEY_6: case XKB_KEY_asciicircum:
			ws = 5; is_num = true; break;
		case XKB_KEY_7: case XKB_KEY_ampersand: case XKB_KEY_slash:
			ws = 6; is_num = true; break;
		case XKB_KEY_8: case XKB_KEY_asterisk: case XKB_KEY_parenleft:
			ws = 7; is_num = true; break;
		case XKB_KEY_9: case XKB_KEY_parenright:
			ws = 8; is_num = true; break;
		}
		if (is_num) {
			if (modifiers & WLR_MODIFIER_SHIFT) {
				struct uwm_toplevel *focused = current_ws(server)->focused;
				if (focused) {
					workspace_move_toplevel(focused, ws);
					workspace_switch(server, ws);
					focus_toplevel(focused);
				}
			} else {
				workspace_switch(server, ws);
			}
			return true;
		}
		return false;
	}
	}
	return true;
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
	/* This event is raised when a key is pressed or released. */
	struct uwm_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct uwm_server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;
	struct wlr_seat *seat = server->seat;

	/* Translate libinput keycode -> xkbcommon */
	uint32_t keycode = event->keycode + 8;
	/* Get a list of keysyms based on the keymap for this keyboard */
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);

	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);

	if ((modifiers & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT)) == (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT)
			&& event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		for (int i = 0; i < nsyms; i++) {
			if (syms[i] >= XKB_KEY_F1 && syms[i] <= XKB_KEY_F12) {
				unsigned vt = syms[i] - XKB_KEY_F1 + 1;
				if (server->session) {
					wlr_session_change_vt(server->session, vt);
				} else {
					wlr_log(WLR_ERROR,
						"VT switching unavailable: no session backend");
				}
				handled = true;
			}
		}
	}

	if (!handled && (modifiers & WLR_MODIFIER_LOGO) && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		for (int i = 0; i < nsyms; i++) {
			handled = handle_keybinding(server, syms[i], modifiers);
			if (handled) {
				keyboard->repeat_sym = syms[i];
				keyboard->repeat_keycode = event->keycode;
				int delay = server->config.key_repeat_delay;
				if (delay > 0)
					wl_event_source_timer_update(keyboard->repeat_timer, delay);
			}
		}
	}

	if (event->state == WL_KEYBOARD_KEY_STATE_RELEASED
			&& event->keycode == keyboard->repeat_keycode) {
		keyboard_repeat_stop(keyboard);
	}

	if (!handled) {
		/* Otherwise, we pass it along to the client. */
		wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
		wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
	}
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
	struct uwm_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
	if (keyboard->repeat_timer)
		wl_event_source_remove(keyboard->repeat_timer);
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

static void server_new_keyboard(struct uwm_server *server, struct wlr_input_device *device) {
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

	struct uwm_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;

	/* We need to prepare an XKB keymap and assign it to the keyboard. This
	 * assumes the defaults (e.g. layout = "us"). */
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);

	wlr_keyboard_set_keymap(wlr_keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(wlr_keyboard,
		server->config.key_repeat_rate,
		server->config.key_repeat_delay);

	struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
	keyboard->repeat_timer = wl_event_loop_add_timer(loop,
		keyboard_repeat_handler, keyboard);
	keyboard->repeat_sym = XKB_KEY_NoSymbol;
	keyboard->repeat_keycode = 0;

	/* Here we set up listeners for keyboard events. */
	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

	/* And add the keyboard to our list of keyboards */
	wl_list_insert(&server->keyboards, &keyboard->link);
}

static void server_new_pointer(struct uwm_server *server, struct wlr_input_device *device) {
	wlr_cursor_attach_input_device(server->cursor, device);

	if (wlr_input_device_is_libinput(device)) {
		struct libinput_device *libinput_dev =
			wlr_libinput_get_device_handle(device);
		if (libinput_dev) {
			if (libinput_device_config_tap_get_finger_count(libinput_dev) > 0)
				libinput_device_config_tap_set_enabled(libinput_dev,
					server->config.tap_to_click
					? LIBINPUT_CONFIG_TAP_ENABLED
					: LIBINPUT_CONFIG_TAP_DISABLED);
			if (libinput_device_config_scroll_has_natural_scroll(libinput_dev))
				libinput_device_config_scroll_set_natural_scroll_enabled(
					libinput_dev, server->config.natural_scroll);
			if (libinput_device_config_accel_is_available(libinput_dev)) {
				enum libinput_config_accel_profile profile =
					server->config.accel_profile == 1
					? LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT
					: LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
				libinput_device_config_accel_set_profile(
					libinput_dev, profile);
			}
		}
	}
}

void server_new_input(struct wl_listener *listener, void *data) {
	/* This event is raised by the backend when a new input device becomes
	 * available. */
	struct uwm_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		server_new_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		server_new_pointer(server, device);
		break;
	default:
		break;
	}
	/* We need to let the wlr_seat know what our capabilities are, which is
	 * communiciated to the client. In uwm we always have a cursor, even if
	 * there are no pointer devices, so we always include that capability. */
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

void seat_request_cursor(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, request_cursor);
	/* This event is raised by the seat when a client provides a cursor image */
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client = server->seat->pointer_state.focused_client;
	/* This can be sent by any client, so we check to make sure this one is
	 * actually has pointer focus first. */
	if (focused_client == event->seat_client) {
		/* Once we've vetted the client, we can tell the cursor to use the
		 * provided surface as the cursor image. It will set the hardware cursor
		 * on the output that it's currently on and continue to do so as the
		 * cursor moves between outputs. */
		wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
	}
}

void seat_pointer_focus_change(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, pointer_focus_change);
	/* This event is raised when the pointer focus is changed, including when the
	 * client is closed. We set the cursor image to its default if target surface
	 * is NULL */
	struct wlr_seat_pointer_focus_change_event *event = data;
	if (event->new_surface == NULL) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}
}

void seat_request_set_selection(struct wl_listener *listener, void *data) {
	/* This event is raised by the seat when a client wants to set the selection,
	 * usually when the user copies something. wlroots allows compositors to
	 * ignore such requests if they so choose, but in uwm we always honor */
	struct uwm_server *server = wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

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
		if (new_top >= new_bottom) {
			new_top = new_bottom - 1;
		}
	} else if (server->resize_edges & WLR_EDGE_BOTTOM) {
		new_bottom = border_y;
		if (new_bottom <= new_top) {
			new_bottom = new_top + 1;
		}
	}
	if (server->resize_edges & WLR_EDGE_LEFT) {
		new_left = border_x;
		if (new_left >= new_right) {
			new_left = new_right - 1;
		}
	} else if (server->resize_edges & WLR_EDGE_RIGHT) {
		new_right = border_x;
		if (new_right <= new_left) {
			new_right = new_left + 1;
		}
	}

	int new_width = new_right - new_left;
	int new_height = new_bottom - new_top;

	if (toplevel->floating) {
		if (new_width < 100) {
			new_width = 100;
			if (server->resize_edges & WLR_EDGE_RIGHT)
				new_right = new_left + 100;
			else
				new_left = new_right - 100;
		}
		if (new_height < 60) {
			new_height = 60;
			if (server->resize_edges & WLR_EDGE_BOTTOM)
				new_bottom = new_top + 60;
			else
				new_top = new_bottom - 60;
		}
		toplevel->float_x = new_left;
		toplevel->float_y = new_top;
		toplevel->float_width = new_width;
		toplevel->float_height = new_height;
	}

	struct wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;
	wlr_scene_node_set_position(&toplevel->scene_tree->node,
		new_left - geo_box->x, new_top - geo_box->y);
	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, new_width, new_height);
}

static void process_cursor_motion(struct uwm_server *server, uint32_t time) {
	/* If the mode is non-passthrough, delegate to those functions. */
	if (server->cursor_mode == UWM_CURSOR_MOVE) {
		process_cursor_move(server);
		return;
	} else if (server->cursor_mode == UWM_CURSOR_RESIZE) {
		process_cursor_resize(server);
		return;
	}

	/* Otherwise, find the toplevel under the pointer and send the event along. */
	double sx, sy;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *surface = NULL;
	struct uwm_toplevel *toplevel = desktop_toplevel_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	if (!toplevel) {
		/* If there's no toplevel under the cursor, set the cursor image to a
		 * default. This is what makes the cursor image appear when you move it
		 * around the screen, not over any toplevels. */
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}
	if (surface) {
		/* Send pointer enter and motion events.
		 *
		 * The enter event gives the surface "pointer focus", which is distinct
		 * from keyboard focus. You get pointer focus by moving the pointer over
		 * a window.
		 *
		 * Note that wlroots will avoid sending duplicate enter/motion events if
		 * the surface has already has pointer focus or if the client is already
		 * aware of the coordinates passed. */
		wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(seat, time, sx, sy);

		struct uwm_workspace *ws = toplevel->workspace;
		if (ws == &server->workspaces.workspaces[server->workspaces.current]
				&& ws->focus_follows_pointer) {
			focus_toplevel(toplevel);
		}
	} else {
		/* Clear pointer focus so future button events and such are not sent to
		 * the last client to have the cursor over it. */
		wlr_seat_pointer_clear_focus(seat);
	}
}

void server_cursor_motion(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits a _relative_
	 * pointer motion event (i.e. a delta) */
	struct uwm_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	/* The cursor doesn't move unless we tell it to. The cursor automatically
	 * handles flattening the motion to the output layout, as well as any
	 * special configuration applied for the specific input device which
	 * generated the event. You can pass NULL for the device if you want to move
	 * the cursor around without any input. */
	wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

void server_cursor_motion_absolute(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits an _absolute_
	 * motion event, from 0..1 on each axis. This happens, for example, when
	 * wlroots is running under a Wayland window rather than KMS+DRM, and you
	 * move the mouse over the window. You could enter the window from any edge,
	 * so we have to warp the mouse there. There is also some hardware which
	 * emits these events. */
	struct uwm_server *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
	process_cursor_motion(server, event->time_msec);
}

void server_cursor_button(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits a button event. */
	struct uwm_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;

	if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
		uint32_t modifiers = 0;
		struct wlr_keyboard *kbd = wlr_seat_get_keyboard(server->seat);
		if (kbd)
			modifiers = wlr_keyboard_get_modifiers(kbd);

		if (modifiers & WLR_MODIFIER_LOGO) {
			double sx, sy;
			struct wlr_surface *surface = NULL;
			struct uwm_toplevel *toplevel = desktop_toplevel_at(server,
				server->cursor->x, server->cursor->y, &surface, &sx, &sy);
			if (toplevel) {
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
		}

		server->last_button_serial = wlr_seat_pointer_notify_button(
			server->seat, event->time_msec, event->button, event->state);

		double sx, sy;
		struct wlr_surface *surface = NULL;
		struct uwm_toplevel *toplevel = desktop_toplevel_at(server,
			server->cursor->x, server->cursor->y, &surface, &sx, &sy);
		if (toplevel)
			focus_toplevel(toplevel);
	} else {
		wlr_seat_pointer_notify_button(server->seat,
			event->time_msec, event->button, event->state);
		reset_cursor_mode(server);
	}
}

void server_cursor_axis(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits an axis event,
	 * for example when you move the scroll wheel. */
	struct uwm_server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;
	/* Notify the client with pointer focus of the axis event. */
	wlr_seat_pointer_notify_axis(server->seat,
			event->time_msec, event->orientation, event->delta,
			event->delta_discrete, event->source, event->relative_direction);
}

void server_cursor_frame(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits an frame
	 * event. Frame events are sent after regular pointer events to group
	 * multiple events together. For instance, two axis events may happen at the
	 * same time, in which case a frame event won't be sent in between. */
	struct uwm_server *server = wl_container_of(listener, server, cursor_frame);
	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(server->seat);
}

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

void reset_cursor_mode(struct uwm_server *server) {
	/* flush coalesced resize before dropping grab — ensures final geometry reaches client */
	if (server->pending_resize && server->grabbed_toplevel) {
		toplevel_set_size(server->grabbed_toplevel, server->pending_w, server->pending_h);
		server->pending_resize = false;
	}
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
		struct wlr_box geo = toplevel_geometry(toplevel);
		struct wlr_box *geo_box = &geo;

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

/* ========== Keybinding dispatch ========== */

static bool handle_keybinding(
		xkb_keysym_t sym, uint32_t modifiers,
		const struct key *karr, size_t klen,
		xkb_mod_mask_t ctrl, xkb_mod_mask_t alt,
		xkb_mod_mask_t logo, xkb_mod_mask_t shift)
{
	sym = xkb_keysym_to_lower(sym);
	char name[64];
	xkb_keysym_get_name(sym, name, sizeof(name));
	wlr_log(WLR_DEBUG, "key sym %s (0x%x) mods 0x%x (logo 0x%x alt 0x%x ctrl 0x%x shift 0x%x) vs %zu bindings",
		name, sym, modifiers, logo, alt, ctrl, shift, klen);

	for (size_t i = 0; i < klen; i++) {
		if (karr[i].keysym != sym)
			continue;

		uint32_t required = karr[i].mod;
		xkb_mod_mask_t binding_mask = 0;
		if (required & WLR_MODIFIER_CTRL)  binding_mask |= ctrl;
		if (required & WLR_MODIFIER_ALT)   binding_mask |= alt;
		if (required & WLR_MODIFIER_LOGO)  binding_mask |= logo;
		if (required & WLR_MODIFIER_SHIFT) binding_mask |= shift;

		xkb_mod_mask_t significant = ctrl | alt | logo | shift;
		if ((modifiers & significant) == binding_mask) {
			karr[i].func(&karr[i].arg);
			return true;
		}
	}
	return false;
}

/* ========== Key repeat ========== */

static int keyboard_repeat_handler(void *data) {
	struct uwm_keyboard *keyboard = data;
	struct uwm_server *server = keyboard->server;

	if (server->locked) {
		return 0;
	}

	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
	if (!(modifiers & WLR_MODIFIER_LOGO)) {
		wl_event_source_timer_update(keyboard->repeat_timer, 0);
		return 0;
	}

	handle_keybinding(keyboard->repeat_sym, modifiers,
		keys, keys_len,
		keyboard->cached_ctrl, keyboard->cached_alt,
		keyboard->cached_logo, keyboard->cached_shift);

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
	/* Modifier indices are constant per keymap — cached once at keyboard
	 * creation. No need to recompute them on every modifier change. */
	if (!(modifiers & keyboard->cached_logo))
		keyboard_repeat_stop(keyboard);
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat, &keyboard->wlr_keyboard->modifiers);
}

/* ========== Keyboard handler ========== */

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
	struct uwm_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct uwm_server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;
	struct wlr_seat *seat = server->seat;

	uwm_server = server;

	uint32_t keycode = event->keycode + 8;

	struct xkb_keymap *keymap = keyboard->wlr_keyboard->keymap;
	const xkb_keysym_t *syms;
	int nsyms = xkb_keymap_key_get_syms_by_level(
		keymap, keycode, 0, 0, &syms);
	xkb_keysym_t syms_copy[32];
	int nsyms_copy = nsyms > 32 ? 32 : nsyms;
	for (int i = 0; i < nsyms_copy; i++)
		syms_copy[i] = syms[i];
	nsyms = nsyms_copy;

	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);

	bool ctrl_held = (modifiers & keyboard->cached_ctrl) != 0;
	bool alt_held = (modifiers & keyboard->cached_alt) != 0;

	if (ctrl_held && alt_held
			&& event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		for (int i = 0; i < nsyms; i++) {
			unsigned vt = 0;
			if (syms_copy[i] >= XKB_KEY_F1 && syms_copy[i] <= XKB_KEY_F12)
				vt = syms_copy[i] - XKB_KEY_F1 + 1;
			else if (syms_copy[i] >= XKB_KEY_XF86Switch_VT_1
					&& syms_copy[i] <= XKB_KEY_XF86Switch_VT_12)
				vt = syms_copy[i] - XKB_KEY_XF86Switch_VT_1 + 1;
			if (vt > 0) {
				if (server->session) {
					if (wlr_session_change_vt(server->session, vt))
						wlr_log(WLR_DEBUG, "Switched to VT %u", vt);
					else
						wlr_log(WLR_ERROR, "VT switch to %u failed", vt);
				} else {
					wlr_log(WLR_ERROR,
						"VT switching unavailable: no session backend."
						" Install seatd or elogind.");
				}
				handled = true;
			}
		}
	}

	/* While locked, skip all keybindings and forward keys to the seat */
	if (server->locked) {
		if (!handled) {
			wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
			wlr_seat_keyboard_notify_key(seat, event->time_msec,
				event->keycode, event->state);
		}
		return;
	}

	if (!handled && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		for (int i = 0; i < nsyms; i++) {
		if (handle_keybinding(syms_copy[i], modifiers,
				keys_unmodified, keys_unmodified_len,
				keyboard->cached_ctrl, keyboard->cached_alt,
				keyboard->cached_logo, keyboard->cached_shift)) {
				handled = true;
			}
		}
	}

	if (!handled && (modifiers & keyboard->cached_logo)
			&& event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		for (int i = 0; i < nsyms; i++) {
			if (handle_keybinding(syms_copy[i], modifiers,
					keys, keys_len,
					keyboard->cached_ctrl, keyboard->cached_alt,
					keyboard->cached_logo, keyboard->cached_shift)) {
				handled = true;
				keyboard->repeat_sym = syms_copy[i];
				keyboard->repeat_keycode = event->keycode;
				int delay = server->config.key_repeat_delay;
				if (delay > 0)
					wl_event_source_timer_update(
						keyboard->repeat_timer, delay);
			}
		}
	}

	if (event->state == WL_KEYBOARD_KEY_STATE_RELEASED
			&& event->keycode == keyboard->repeat_keycode) {
		keyboard_repeat_stop(keyboard);
	}

	if (!handled) {
		wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
		wlr_seat_keyboard_notify_key(seat, event->time_msec,
			event->keycode, event->state);
	}
}

/* ========== Keyboard device management ========== */

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
	if (!keyboard)
		return;
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;

	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!context) {
		free(keyboard);
		return;
	}
	struct xkb_rule_names names = {
		.rules = NULL,
		.model = NULL,
		.layout = NULL,
		.variant = NULL,
		.options = *xkb_options ? xkb_options : NULL,
	};
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!keymap) {
		xkb_context_unref(context);
		free(keyboard);
		return;
	}

	wlr_keyboard_set_keymap(wlr_keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(wlr_keyboard,
		server->config.key_repeat_rate,
		server->config.key_repeat_delay);

	keyboard->cached_ctrl = 0;
	keyboard->cached_alt = 0;
	keyboard->cached_logo = 0;
	keyboard->cached_shift = 0;
	if (wlr_keyboard->keymap) {
		xkb_mod_index_t idx;
		idx = xkb_keymap_mod_get_index(wlr_keyboard->keymap, XKB_MOD_NAME_CTRL);
		if (idx != XKB_MOD_INVALID) keyboard->cached_ctrl = (xkb_mod_mask_t)1 << idx;
		idx = xkb_keymap_mod_get_index(wlr_keyboard->keymap, XKB_MOD_NAME_ALT);
		if (idx != XKB_MOD_INVALID) keyboard->cached_alt = (xkb_mod_mask_t)1 << idx;
		idx = xkb_keymap_mod_get_index(wlr_keyboard->keymap, XKB_MOD_NAME_LOGO);
		if (idx != XKB_MOD_INVALID) keyboard->cached_logo = (xkb_mod_mask_t)1 << idx;
		idx = xkb_keymap_mod_get_index(wlr_keyboard->keymap, XKB_MOD_NAME_SHIFT);
		if (idx != XKB_MOD_INVALID) keyboard->cached_shift = (xkb_mod_mask_t)1 << idx;
	}

	struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
	keyboard->repeat_timer = wl_event_loop_add_timer(loop,
		keyboard_repeat_handler, keyboard);
	keyboard->repeat_sym = XKB_KEY_NoSymbol;
	keyboard->repeat_keycode = 0;

	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
	wl_list_insert(&server->keyboards, &keyboard->link);
#if WLR_HAS_XWAYLAND
	/* Ensure XWayland XWM has the current keymap even before any X surface
	 * gets focus – otherwise lazy-spawned Xwayland (first X client) starts
	 * with no seat keymap and falls back to RMLVO, which on this system
	 * reproduces “XKB: Failed to compile keymap”. Re-assigning the seat
	 * forces xwm to sync the just-installed keymap (see Sway
	 * input/seat.c seat_configure_keyboard → seat_keyboard_notify_enter). */
	if (server->xwayland) {
		wlr_xwayland_set_seat(server->xwayland, server->seat);
	}
#endif
}

/* ========== Pointer device management ========== */

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
				libinput_device_config_accel_set_speed(
					libinput_dev, server->config.pointer_speed);
			}
		}
	}
}

/* ========== Input device discovery ========== */

void server_new_input(struct wl_listener *listener, void *data) {
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
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards))
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	wlr_seat_set_capabilities(server->seat, caps);
}

/* ========== Seat event handlers ========== */

void seat_request_cursor(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client = server->seat->pointer_state.focused_client;
	if (focused_client == event->seat_client) {
		wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
	}
}

void seat_pointer_focus_change(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, pointer_focus_change);
	struct wlr_seat_pointer_focus_change_event *event = data;
	if (event->new_surface == NULL) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}
	if (event->new_surface != event->old_surface) {
		wlr_log(WLR_DEBUG, "POINTER_FOCUS: old=%p new=%p kb_focus=%p",
			(void *)event->old_surface, (void *)event->new_surface,
			(void *)server->seat->keyboard_state.focused_surface);
	}
}

void seat_request_set_selection(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_log(WLR_DEBUG, "SEAT_FOCUS: set_selection serial=%u source=%p",
		event->serial, (void *)event->source);
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

void seat_request_set_primary_selection(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, request_set_primary_selection);
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(server->seat, event->source, event->serial);
}


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
#include "input.h"
#include "config.h"
#include "window.h"
#include "bsp.h"
#include "floating.h"
#include "layout.h"
#include "server.h"

void reset_cursor_mode(struct uwm_server *server) {
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

/* ========== Global server pointer for action functions ========== */
static struct uwm_server *uwm_server;

/* ========== Workspace/arrangement helpers ========== */
static struct uwm_workspace *current_ws(void)
{
	return &uwm_server->workspaces.workspaces[uwm_server->workspaces.current];
}

static void bsp_arrange_current_workspace(void)
{
	struct uwm_workspace *ws = current_ws();
	int x, y, w, h;
	get_output_size(uwm_server, &x, &y, &w, &h);
	bsp_arrange(ws, x, y, w, h, uwm_server->config.inner_gap);
}

/* ========== Action functions — called via keys[] dispatch ========== */

void spawn(const union arg *arg)
{
	if (fork() == 0) {
		setsid();
		execvp((char *)arg->argv[0], (char **)arg->argv);
		_exit(1);
	}
}

void quit(const union arg *arg)
{
	(void)arg;
	wl_display_terminate(uwm_server->wl_display);
}

void closewindow(const union arg *arg)
{
	(void)arg;
	struct uwm_toplevel *focused = current_ws()->focused;
	if (focused && focused->xdg_toplevel)
		wlr_xdg_toplevel_send_close(focused->xdg_toplevel);
}

void forceclose(const union arg *arg)
{
	(void)arg;
	struct uwm_toplevel *focused = current_ws()->focused;
	if (focused && focused->xdg_toplevel) {
		struct wl_client *client = wl_resource_get_client(
			focused->xdg_toplevel->base->resource);
		if (client)
			wl_client_destroy(client);
	}
}

/* focus movement — also moves floating windows */
static void focus_move(struct uwm_workspace *ws, int dx, int dy,
		struct uwm_toplevel *(*bsp_fn)(struct uwm_workspace *))
{
	if (ws->fullscreen_window)
		return;
	struct uwm_toplevel *focused = ws->focused;
	if (focused && focused->floating) {
		focused->float_x += dx;
		focused->float_y += dy;
		wlr_scene_node_set_position(&focused->scene_tree->node,
			focused->float_x, focused->float_y);
	} else {
		struct uwm_toplevel *target = bsp_fn(ws);
		if (target)
			focus_toplevel(target);
	}
}

void moveleft(const union arg *arg)  { (void)arg; focus_move(current_ws(), -20, 0, bsp_focus_left); }
void moveright(const union arg *arg) { (void)arg; focus_move(current_ws(), 20, 0, bsp_focus_right); }
void moveup(const union arg *arg)    { (void)arg; focus_move(current_ws(), 0, -20, bsp_focus_up); }
void movedown(const union arg *arg)  { (void)arg; focus_move(current_ws(), 0, 20, bsp_focus_down); }

/* BSP swap */
static void swap_dir(struct uwm_workspace *ws, int dir)
{
	bsp_swap_direction(ws, ws->focused, dir);
	bsp_arrange_current_workspace();
}

void swapleft(const union arg *arg)  { (void)arg; swap_dir(current_ws(), 0); }
void swapright(const union arg *arg) { (void)arg; swap_dir(current_ws(), 1); }
void swapup(const union arg *arg)    { (void)arg; swap_dir(current_ws(), 2); }
void swapdown(const union arg *arg)  { (void)arg; swap_dir(current_ws(), 3); }

/* resize — tiled (BSP ratio) or floating */
static void resize_float(struct uwm_toplevel *focused, int dx, int dy, int dw, int dh)
{
	if (!focused || !focused->floating)
		return;
	focused->float_x += dx;
	focused->float_y += dy;
	focused->float_width += dw;
	focused->float_height += dh;
	if (focused->float_width < floating_min_width)
		focused->float_width = floating_min_width;
	if (focused->float_height < floating_min_height)
		focused->float_height = floating_min_height;
	wlr_scene_node_set_position(&focused->scene_tree->node,
		focused->float_x, focused->float_y);
	wlr_xdg_toplevel_set_size(focused->xdg_toplevel,
		focused->float_width, focused->float_height);
}

static void resize_tiled(float delta)
{
	struct uwm_workspace *ws = current_ws();
	bsp_resize(ws, ws->focused, delta);
	bsp_arrange_current_workspace();
}

void resizeleft(const union arg *arg)
{
	(void)arg;
	struct uwm_toplevel *focused = current_ws()->focused;
	if (focused && focused->floating)
		resize_float(focused, -20, 0, 20, 0);
	else
		resize_tiled(-resizefactor);
}

void resizeright(const union arg *arg)
{
	(void)arg;
	struct uwm_toplevel *focused = current_ws()->focused;
	if (focused && focused->floating)
		resize_float(focused, 0, 0, 20, 0);
	else
		resize_tiled(resizefactor);
}

void resizeup(const union arg *arg)
{
	(void)arg;
	struct uwm_toplevel *focused = current_ws()->focused;
	if (focused && focused->floating)
		resize_float(focused, 0, -20, 0, 20);
	else
		resize_tiled(resizefactor);
}

void resizedown(const union arg *arg)
{
	(void)arg;
	struct uwm_toplevel *focused = current_ws()->focused;
	if (focused && focused->floating)
		resize_float(focused, 0, 0, 0, 20);
	else
		resize_tiled(-resizefactor);
}

/* floating shrink (Shift+Alt+arrow) */
void resizeshleft(const union arg *arg)
{
	(void)arg;
	resize_float(current_ws()->focused, 20, 0, -20, 0);
}

void resizeshright(const union arg *arg)
{
	(void)arg;
	resize_float(current_ws()->focused, 0, 0, -20, 0);
}

void resizeshup(const union arg *arg)
{
	(void)arg;
	resize_float(current_ws()->focused, 0, 20, 0, -20);
}

void resizeshdown(const union arg *arg)
{
	(void)arg;
	resize_float(current_ws()->focused, 0, 0, 0, -20);
}

/* workspace switching */
void workspace(const union arg *arg)
{
	if ((uint32_t)arg->i < UWM_WORKSPACE_COUNT)
		workspace_switch(uwm_server, (uint32_t)arg->i);
}

void movetows(const union arg *arg)
{
	struct uwm_toplevel *focused = current_ws()->focused;
	if (!focused)
		return;
	uint32_t ws = (uint32_t)arg->i;
	if (ws < UWM_WORKSPACE_COUNT) {
		workspace_move_toplevel(focused, ws);
		workspace_switch(uwm_server, ws);
		focus_toplevel(focused);
	}
}

void workspaceinc(const union arg *arg)
{
	(void)arg;
	workspace_next(uwm_server);
}

void workspacedec(const union arg *arg)
{
	(void)arg;
	workspace_prev(uwm_server);
}

void workspaceprev(const union arg *arg)
{
	(void)arg;
	if (uwm_server->workspaces.last != uwm_server->workspaces.current)
		workspace_switch(uwm_server, uwm_server->workspaces.last);
}

/* layout toggles */
void togglefloating(const union arg *arg)
{
	(void)arg;
	toggle_floating(current_ws()->focused);
}

void togglefullscreen(const union arg *arg)
{
	(void)arg;
	toggle_fullscreen(current_ws()->focused);
}

void togglemonocle(const union arg *arg)
{
	(void)arg;
	toggle_monocle(current_ws());
}

void setbsp(const union arg *arg)
{
	(void)arg;
	set_bsp_mode(current_ws());
}

void cyclefocus(const union arg *arg)
{
	(void)arg;
	workspace_cycle_next(uwm_server);
}

void rotatesplit(const union arg *arg)
{
	(void)arg;
	bsp_rotate_focused_split(current_ws());
	bsp_arrange_current_workspace();
}

/* ========== Keybinding dispatch ========== */

static bool handle_keybinding(
		xkb_keysym_t sym, uint32_t modifiers,
		const struct key *karr, size_t klen,
		struct xkb_keymap *keymap)
{
	sym = xkb_keysym_to_lower(sym);

	xkb_mod_mask_t ctrl = 0, alt = 0, shift = 0, logo = 0;
	if (keymap) {
		xkb_mod_index_t idx;
		idx = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CTRL);
		if (idx != XKB_MOD_INVALID) ctrl = (xkb_mod_mask_t)1 << idx;
		idx = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_ALT);
		if (idx != XKB_MOD_INVALID) alt = (xkb_mod_mask_t)1 << idx;
		idx = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_LOGO);
		if (idx != XKB_MOD_INVALID) logo = (xkb_mod_mask_t)1 << idx;
		idx = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
		if (idx != XKB_MOD_INVALID) shift = (xkb_mod_mask_t)1 << idx;
	} else {
		ctrl = WLR_MODIFIER_CTRL;
		alt = WLR_MODIFIER_ALT;
		logo = WLR_MODIFIER_LOGO;
		shift = WLR_MODIFIER_SHIFT;
	}

	for (size_t i = 0; i < klen; i++) {
		if (karr[i].keysym != sym)
			continue;

		uint32_t required = karr[i].mod;
		xkb_mod_mask_t binding_mask = 0;
		if (required & WLR_MODIFIER_CTRL)  binding_mask |= ctrl;
		if (required & WLR_MODIFIER_ALT)   binding_mask |= alt;
		if (required & WLR_MODIFIER_LOGO)  binding_mask |= logo;
		if (required & WLR_MODIFIER_SHIFT) binding_mask |= shift;

		if ((modifiers & binding_mask) == binding_mask) {
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

	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
	if (!(modifiers & WLR_MODIFIER_LOGO)) {
		wl_event_source_timer_update(keyboard->repeat_timer, 0);
		return 0;
	}

	handle_keybinding(keyboard->repeat_sym, modifiers,
		keys, keys_len, keyboard->wlr_keyboard->keymap);

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

/* ========== Keyboard handler ========== */

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
	struct uwm_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct uwm_server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;
	struct wlr_seat *seat = server->seat;

	uwm_server = server;

	/* libinput keycode -> xkbcommon */
	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);
	xkb_keysym_t syms_copy[32];
	int nsyms_copy = nsyms > 32 ? 32 : nsyms;
	for (int i = 0; i < nsyms_copy; i++)
		syms_copy[i] = syms[i];
	nsyms = nsyms_copy;

	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);

	/* Resolve actual modifier indices from the XKB keymap */
	struct xkb_keymap *keymap = keyboard->wlr_keyboard->keymap;
	xkb_mod_mask_t ctrl_mask = 0, alt_mask = 0, logo_mask = 0;
	xkb_mod_index_t idx;
	idx = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CTRL);
	if (idx != XKB_MOD_INVALID) ctrl_mask = (xkb_mod_mask_t)1 << idx;
	idx = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_ALT);
	if (idx != XKB_MOD_INVALID) alt_mask = (xkb_mod_mask_t)1 << idx;
	idx = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_LOGO);
	if (idx != XKB_MOD_INVALID) logo_mask = (xkb_mod_mask_t)1 << idx;

	/* VT switching (Ctrl+Alt+F[1-12]) — uses hardware keycodes for
	 * correctness with wlroots 0.20 timing */
	bool ctrl_held = false, alt_held = false;
	for (size_t i = 0; i < keyboard->wlr_keyboard->num_keycodes; i++) {
		uint32_t kc = keyboard->wlr_keyboard->keycodes[i];
		if (kc == KEY_LEFTCTRL || kc == KEY_RIGHTCTRL) ctrl_held = true;
		if (kc == KEY_LEFTALT || kc == KEY_RIGHTALT) alt_held = true;
	}

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
						wlr_log(WLR_INFO, "Switched to VT %u", vt);
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

	/* Unmodified key bindings (no modifier required) */
	if (!handled && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		for (int i = 0; i < nsyms; i++) {
			if (handle_keybinding(syms_copy[i], modifiers,
					keys_unmodified, keys_unmodified_len, keymap)) {
				handled = true;
			}
		}
	}

	/* Super-modifier key bindings */
	if (!handled && (modifiers & logo_mask)
			&& event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		for (int i = 0; i < nsyms; i++) {
			if (handle_keybinding(syms_copy[i], modifiers,
					keys, keys_len, keymap)) {
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
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;

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

	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
	wl_list_insert(&server->keyboards, &keyboard->link);
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
		wlr_log(WLR_INFO, "POINTER_FOCUS: old=%p new=%p kb_focus=%p",
			(void *)event->old_surface, (void *)event->new_surface,
			(void *)server->seat->keyboard_state.focused_surface);
	}
}

void seat_request_set_selection(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_log(WLR_INFO, "SEAT_FOCUS: set_selection serial=%u source=%p",
		event->serial, (void *)event->source);
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

void seat_request_set_primary_selection(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, request_set_primary_selection);
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(server->seat, event->source, event->serial);
}

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
	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, new_width, new_height);
}

static void process_cursor_motion(struct uwm_server *server, uint32_t time) {
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
	if (!toplevel) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}
	if (surface) {
		wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(seat, time, sx, sy);

		if (toplevel) {
			struct uwm_workspace *ws = toplevel->workspace;
			if (ws == &server->workspaces.workspaces[server->workspaces.current]
					&& ws->focus_follows_pointer) {
				focus_toplevel(toplevel);
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
		wlr_seat_pointer_notify_frame(server->seat);

		double sx, sy;
		struct wlr_surface *surface = NULL;
		struct uwm_toplevel *toplevel = desktop_toplevel_at(server,
			server->cursor->x, server->cursor->y, &surface, &sx, &sy);
		if (toplevel && !toplevel->is_transient)
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
	wlr_seat_pointer_notify_frame(server->seat);
}

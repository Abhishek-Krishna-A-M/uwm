#include "ulaunch.h"
#include "input.h"
#include "render.h"
#include "filter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <xkbcommon/xkbcommon.h>

static void repeat_stop(void);
static void repeat_start(xkb_keysym_t sym, uint32_t raw_key, bool ctrl);

static void keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t format, int32_t fd, uint32_t size) {
	char *map_str = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (map_str == MAP_FAILED) { close(fd); return; }

	if (state.xkb_keymap)
		xkb_keymap_unref(state.xkb_keymap);
	if (state.xkb_state)
		xkb_state_unref(state.xkb_state);

	state.xkb_keymap = xkb_keymap_new_from_string(state.xkb_ctx, map_str,
		XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(map_str, size);
	close(fd);

	if (!state.xkb_keymap) return;
	state.xkb_state = xkb_state_new(state.xkb_keymap);
}

static void keyboard_enter(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {}
static void keyboard_leave(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, struct wl_surface *surface) {
	state.running = false;
}
static void keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
		uint32_t mods_locked, uint32_t group) {
	if (state.xkb_state)
		xkb_state_update_mask(state.xkb_state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
		int32_t rate, int32_t delay) {
	state.repeat_delay_ms = delay;
	state.repeat_rate_ms = rate > 0 ? 1000 / rate : 40;
}

static void emit(int idx) {
	/* no matching entries — fallback to raw input */
	if (idx < 0 || idx >= state.n_filtered) {
		if (!state.input[0]) {
			state.running = false;
			return;
		}
		if (state.mode == MODE_DMENU) {
			printf("%s\n", state.input);
			fflush(stdout);
		} else {
			pid_t pid = fork();
			if (pid == 0) {
				setsid();
				freopen("/dev/null", "r", stdin);
				freopen("/dev/null", "w", stdout);
				freopen("/dev/null", "w", stderr);
				execl("/bin/sh", "sh", "-c", state.input, NULL);
				_exit(127);
			}
		}
		state.running = false;
		return;
	}

	int entry_idx = state.filtered[idx];

	if (state.mode == MODE_DRUN && state.exec_cmds && state.exec_cmds[entry_idx]) {
		if (state.hits) state.hits[entry_idx]++;
		pid_t pid = fork();
		if (pid == 0) {
			setsid();
			freopen("/dev/null", "r", stdin);
			freopen("/dev/null", "w", stdout);
			freopen("/dev/null", "w", stderr);
			execl("/bin/sh", "sh", "-c", state.exec_cmds[entry_idx], NULL);
			_exit(127);
		}
		state.running = false;
	} else {
		printf("%s\n", state.entries[entry_idx]);
		fflush(stdout);
		state.running = false;
	}
}

static void repeat_stop(void) {
	state.repeat_key = UINT32_MAX;
	struct itimerspec its = {0};
	timerfd_settime(state.timerfd, 0, &its, NULL);
}

static void repeat_start(xkb_keysym_t sym, uint32_t raw_key, bool ctrl) {
	state.repeat_key = raw_key;
	state.repeat_sym = sym;
	state.repeat_ctrl = ctrl;
	state.repeat_utf8_len = 0;

	int delay = state.repeat_delay_ms > 0 ? state.repeat_delay_ms : 300;
	int rate = state.repeat_rate_ms > 0 ? state.repeat_rate_ms : 20;

	struct itimerspec its = {
		.it_value = { .tv_sec = delay / 1000, .tv_nsec = (delay % 1000) * 1000000LL },
		.it_interval = { .tv_sec = rate / 1000, .tv_nsec = (rate % 1000) * 1000000LL },
	};
	timerfd_settime(state.timerfd, 0, &its, NULL);
}

void input_repeat_fire(void) {
	if (state.repeat_key == UINT32_MAX) return;

	/* printable character from stored utf8 */
	if (state.repeat_utf8_len > 0) {
		if (state.input_len + state.repeat_utf8_len < INPUT_BUF_MAX - 1) {
			memcpy(state.input + state.input_len, state.repeat_utf8, state.repeat_utf8_len);
			state.input_len += state.repeat_utf8_len;
			state.input[state.input_len] = '\0';
			filter_update();
			state.need_redraw = true;
		}
		return;
	}

	/* navigation / editing — re-run press logic */
	switch (state.repeat_sym) {
	case XKB_KEY_Up:
	case XKB_KEY_KP_Up:
		if (state.cursor > 0) state.cursor--;
		state.need_redraw = true;
		return;
	case XKB_KEY_Down:
	case XKB_KEY_KP_Down:
		if (state.cursor < state.n_filtered - 1) state.cursor++;
		state.need_redraw = true;
		return;
	case XKB_KEY_Page_Up:
		state.cursor -= state.theme.max_items;
		if (state.cursor < 0) state.cursor = 0;
		state.need_redraw = true;
		return;
	case XKB_KEY_Page_Down:
		state.cursor += state.theme.max_items;
		if (state.cursor >= state.n_filtered)
			state.cursor = state.n_filtered - 1;
		state.need_redraw = true;
		return;
	case XKB_KEY_BackSpace:
		if (state.input_len > 0) {
			state.input[--state.input_len] = '\0';
			if (state.input_cursor > state.input_len)
				state.input_cursor = state.input_len;
			filter_update();
			state.need_redraw = true;
		}
		return;
	case XKB_KEY_Tab:
		if (state.n_filtered > 0) {
			state.cursor = (state.cursor + 1) % state.n_filtered;
			state.need_redraw = true;
		}
		return;
	}
}

static void handle_press(xkb_keysym_t sym, uint32_t raw_key, bool ctrl) {
	switch (sym) {
	case XKB_KEY_Escape:
		repeat_stop();
		state.running = false;
		return;

	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		repeat_stop();
		emit(state.cursor);
		return;

	case XKB_KEY_Up:
	case XKB_KEY_KP_Up:
		if (state.cursor > 0) state.cursor--;
		state.need_redraw = true;
		repeat_start(sym, raw_key, ctrl);
		return;

	case XKB_KEY_Down:
	case XKB_KEY_KP_Down:
		if (state.cursor < state.n_filtered - 1) state.cursor++;
		state.need_redraw = true;
		repeat_start(sym, raw_key, ctrl);
		return;

	case XKB_KEY_Page_Up:
		state.cursor -= state.theme.max_items;
		if (state.cursor < 0) state.cursor = 0;
		state.need_redraw = true;
		repeat_start(sym, raw_key, ctrl);
		return;

	case XKB_KEY_Page_Down:
		state.cursor += state.theme.max_items;
		if (state.cursor >= state.n_filtered)
			state.cursor = state.n_filtered - 1;
		state.need_redraw = true;
		repeat_start(sym, raw_key, ctrl);
		return;

	case XKB_KEY_Home:
		if (ctrl) {
			state.cursor = 0;
			state.need_redraw = true;
		}
		return;

	case XKB_KEY_End:
		if (ctrl) {
			state.cursor = state.n_filtered - 1;
			state.need_redraw = true;
		}
		return;

	case XKB_KEY_BackSpace:
		if (state.input_len > 0) {
			state.input[--state.input_len] = '\0';
			if (state.input_cursor > state.input_len)
				state.input_cursor = state.input_len;
			filter_update();
			state.need_redraw = true;
		}
		repeat_start(sym, raw_key, ctrl);
		return;

	case XKB_KEY_Tab:
		if (state.n_filtered > 0) {
			state.cursor = (state.cursor + 1) % state.n_filtered;
			state.need_redraw = true;
		}
		repeat_start(sym, raw_key, ctrl);
		return;

	default:
		if (ctrl) {
			switch (sym) {
			case XKB_KEY_w:
				while (state.input_len > 0 && state.input[state.input_len - 1] == ' ')
					state.input[--state.input_len] = '\0';
				while (state.input_len > 0 && state.input[state.input_len - 1] != ' ')
					state.input[--state.input_len] = '\0';
				filter_update();
				state.need_redraw = true;
				return;
			case XKB_KEY_u:
				state.input_len = 0;
				state.input[0] = '\0';
				filter_update();
				state.need_redraw = true;
				return;
			case XKB_KEY_a:
				state.cursor = 0;
				state.need_redraw = true;
				return;
			case XKB_KEY_e:
				state.cursor = state.n_filtered - 1;
				state.need_redraw = true;
				return;
			case XKB_KEY_c:
				state.running = false;
				return;
			case XKB_KEY_i:
			case XKB_KEY_Tab:
				return;
			}
			return;
		}

		/* printable character */
		char buf[8];
		int utf8_len = xkb_state_key_get_utf8(state.xkb_state, raw_key + 8, buf, sizeof(buf));
		if (utf8_len > 0 && state.input_len + utf8_len < INPUT_BUF_MAX - 1) {
			memcpy(state.input + state.input_len, buf, utf8_len);
			state.input_len += utf8_len;
			state.input[state.input_len] = '\0';
			filter_update();
			state.need_redraw = true;
			repeat_start(sym, raw_key, ctrl);
			state.repeat_utf8_len = utf8_len;
			memcpy(state.repeat_utf8, buf, utf8_len);
		}
		return;
	}
}

static void keyboard_key(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t time, uint32_t key, uint32_t wl_state) {
	if (!state.xkb_state) return;

	if (wl_state == WL_KEYBOARD_KEY_STATE_RELEASED) {
		if (key == state.repeat_key)
			repeat_stop();
		return;
	}

	xkb_keysym_t sym = xkb_state_key_get_one_sym(state.xkb_state, key + 8);
	xkb_mod_index_t ctrl_idx = xkb_keymap_mod_get_index(state.xkb_keymap, XKB_MOD_NAME_CTRL);
	xkb_mod_mask_t mods = xkb_state_serialize_mods(state.xkb_state, XKB_STATE_MODS_DEPRESSED);
	bool ctrl = ctrl_idx != XKB_MOD_INVALID && (mods & (1 << ctrl_idx)) != 0;

	handle_press(sym, key, ctrl);
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_keymap,
	.enter = keyboard_enter,
	.leave = keyboard_leave,
	.key = keyboard_key,
	.modifiers = keyboard_modifiers,
	.repeat_info = keyboard_repeat_info,
};

void input_init(struct wl_keyboard *keyboard) {
	state.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!state.xkb_ctx) return;

	state.repeat_delay_ms = 300;
	state.repeat_rate_ms = 20;
	state.repeat_key = UINT32_MAX;

	wl_keyboard_add_listener(keyboard, &keyboard_listener, NULL);
	wl_keyboard_set_user_data(keyboard, NULL);
}

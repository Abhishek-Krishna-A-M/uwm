#include "ubar.h"
#include "input.h"
#include "data.h"
#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <linux/uinput.h>

static State *g_state = NULL;

static int uinput_init(void) {
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (fd < 0) {
		LOG("uinput_init: open failed: %m");
		return -1;
	}

	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_KEYBIT, KEY_CAPSLOCK);
	ioctl(fd, UI_SET_KEYBIT, KEY_NUMLOCK);

	struct uinput_setup usetup = {
		.name = "ubar-kbd",
		.id = { .bustype = BUS_USB, .vendor = 1, .product = 1, .version = 1 }
	};
	ioctl(fd, UI_DEV_SETUP, &usetup);
	if (ioctl(fd, UI_DEV_CREATE) < 0) {
		LOG("uinput_init: UI_DEV_CREATE failed: %m");
		close(fd);
		return -1;
	}

	return fd;
}

static void uinput_send_key(int fd, unsigned int keycode) {
	if (fd < 0) return;

	struct input_event ev = {
		.type = EV_KEY,
		.code = keycode,
		.value = 1
	};
	write(fd, &ev, sizeof(ev));

	ev.value = 0;
	write(fd, &ev, sizeof(ev));

	struct input_event syn = {
		.type = EV_SYN,
		.code = SYN_REPORT,
		.value = 0
	};
	write(fd, &syn, sizeof(syn));
}

static void pointer_enter(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
	State *state = (State *)data;
	state->pointer_x = wl_fixed_to_int(sx);
}

static void pointer_leave(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface) {
}

static void pointer_motion(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
	State *state = (State *)data;
	state->pointer_x = wl_fixed_to_int(sx);
}

static HotZone *find_zone(State *state, int x) {
	for (int i = 0; i < state->zone_count; i++) {
		if (x >= state->zones[i].x && x < state->zones[i].x + state->zones[i].width)
			return &state->zones[i];
	}
	return NULL;
}

static void pointer_button(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state_w) {
	State *state = (State *)data;
	if (state_w != WL_POINTER_BUTTON_STATE_PRESSED)
		return;

	HotZone *zone = find_zone(state, state->pointer_x);
	if (!zone) return;

	switch (zone->type) {
	case ZONE_TIME:
		state->time_detailed = !state->time_detailed;
		state->prev_minute = -1;
		data_update_clock(state);
		set_clock_timer(state);
		state->need_redraw = true;
		break;

	case ZONE_RAM:
		state->ram_detailed = !state->ram_detailed;
		state->need_redraw = true;
		break;

	case ZONE_VOLUME:
		if (fork() == 0) {
			execlp("wpctl", "wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle", NULL);
			_exit(0);
		}
		/* PulseAudio subscription detects the change via audio_pipe */
		break;

	case ZONE_NETWORK:
		state->net_detailed = !state->net_detailed;
		state->need_redraw = true;
		break;

	case ZONE_WORKSPACE:
		if (fork() == 0) {
			char id_str[8];
			snprintf(id_str, sizeof(id_str), "%d", zone->data);
			execlp("uwm", "uwm", "-t", "workspace", id_str, NULL);
			_exit(0);
		}
		break;

	case ZONE_CAPS:
		uinput_send_key(state->uinput_fd, KEY_CAPSLOCK);
		break;

	case ZONE_NUM:
		uinput_send_key(state->uinput_fd, KEY_NUMLOCK);
		break;

	default:
		break;
	}
}

static void pointer_axis(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value) {
	State *state = (State *)data;
	if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
		return;

	int val = wl_fixed_to_int(value);
	HotZone *zone = find_zone(state, state->pointer_x);
	if (!zone || zone->type != ZONE_VOLUME)
		return;

	if (val > 0) {
		if (fork() == 0) {
			execlp("wpctl", "wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "0.05-", NULL);
			_exit(0);
		}
	} else {
		if (fork() == 0) {
			execlp("wpctl", "wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "0.05+", NULL);
			_exit(0);
		}
	}
	/* PulseAudio subscription detects the change via audio_pipe */
}

static void pointer_frame(void *data, struct wl_pointer *wl_pointer) {}
static void pointer_axis_source(void *data, struct wl_pointer *wl_pointer, uint32_t axis_source) {}
static void pointer_axis_stop(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis) {}
static void pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t discrete) {}
static void pointer_axis_value120(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t value120) {}
static void pointer_axis_relative_direction(void *data, struct wl_pointer *wl_pointer, uint32_t axis, uint32_t direction) {}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
	.frame = pointer_frame,
	.axis_source = pointer_axis_source,
	.axis_stop = pointer_axis_stop,
	.axis_discrete = pointer_axis_discrete,
	.axis_value120 = pointer_axis_value120,
	.axis_relative_direction = pointer_axis_relative_direction,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
	State *state = (State *)data;
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !state->pointer) {
		state->pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(state->pointer, &pointer_listener, state);
	} else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && state->pointer) {
		wl_pointer_destroy(state->pointer);
		state->pointer = NULL;
	}
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_capabilities,
	.name = seat_name,
};

void input_init(State *state, struct wl_seat *seat) {
	g_state = state;
	state->seat = seat;
	state->uinput_fd = uinput_init();
	wl_seat_add_listener(seat, &seat_listener, state);
}

void input_destroy(State *state) {
	if (state->uinput_fd >= 0) {
		ioctl(state->uinput_fd, UI_DEV_DESTROY);
		close(state->uinput_fd);
		state->uinput_fd = -1;
	}
}

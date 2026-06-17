#include "ubar.h"
#include "input.h"
#include "data.h"
#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static State *g_state = NULL;

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
		data_update_clock(state);
		state->need_redraw = true;
		break;

	case ZONE_RAM:
		state->ram_detailed = !state->ram_detailed;
		state->need_redraw = true;
		break;

	case ZONE_VOLUME:
		if (fork() == 0) {
			execlp("pactl", "pactl", "set-sink-mute", "@DEFAULT_SINK@", "toggle", NULL);
			_exit(0);
		}
		/* Don't call data_update_volume() here — it uses popen() on the main thread.
		 * The audio_monitor thread will detect the change via 'pactl subscribe'. */
		LOG("volume toggle: forked pactl, waiting for subscribe callback");
		break;

	case ZONE_NETWORK:
		state->net_detailed = !state->net_detailed;
		state->need_redraw = true;
		break;

	case ZONE_WORKSPACE:
		if (fork() == 0) {
			char cmd[32];
			snprintf(cmd, sizeof(cmd), "uwm -t workspace %d", zone->data);
			execlp("sh", "sh", "-c", cmd, NULL);
			_exit(0);
		}
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
			execlp("pactl", "pactl", "set-sink-volume", "@DEFAULT_SINK@", "-5%", NULL);
			_exit(0);
		}
	} else {
		if (fork() == 0) {
			execlp("pactl", "pactl", "set-sink-volume", "@DEFAULT_SINK@", "+5%", NULL);
			_exit(0);
		}
	}
	/* Don't call data_update_volume() here — let audio_monitor thread handle it. */
	LOG("volume scroll: forked pactl, waiting for subscribe callback");
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
	wl_seat_add_listener(seat, &seat_listener, state);
}

#define _GNU_SOURCE
#include "ubar.h"
#include "render.h"
#include "data.h"
#include "input.h"
#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/timerfd.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>

static State state = {0};

void cairo_set_source_hex(cairo_t *cr, uint32_t color) {
	double a = ((color >> 24) & 0xFF) / 255.0;
	double r = ((color >> 16) & 0xFF) / 255.0;
	double g = ((color >> 8) & 0xFF) / 255.0;
	double b = (color & 0xFF) / 255.0;
	cairo_set_source_rgba(cr, r, g, b, a);
}

uint32_t parse_color(const char *hex) {
	if (hex[0] == '#') hex++;
	unsigned int r, g, b;
	if (sscanf(hex, "%02x%02x%02x", &r, &g, &b) != 3)
		return 0xFFFFFFFF;
	return (0xFF << 24) | (r << 16) | (g << 8) | b;
}

/* ====== Pipe helper ====== */

static void create_pipe(int fds[2]) {
	pipe2(fds, O_CLOEXEC | O_NONBLOCK);
}

static void drain_pipe(int fd) {
	char buf[64];
	while (read(fd, buf, sizeof(buf)) > 0)
		;
}

/* ====== Clock timer control ====== */

void set_clock_timer(State *s) {
	struct itimerspec ts = {0};
	if (s->time_detailed) {
		/* Show seconds: 1s interval */
		ts.it_interval = (struct timespec){ .tv_sec = 1, .tv_nsec = 0 };
		ts.it_value    = (struct timespec){ .tv_sec = 1, .tv_nsec = 0 };
	} else {
		/* Show HH:MM only: 60s interval */
		ts.it_interval = (struct timespec){ .tv_sec = 60, .tv_nsec = 0 };
		ts.it_value    = (struct timespec){ .tv_sec = 60, .tv_nsec = 0 };
	}
	timerfd_settime(s->clock_timer_fd, 0, &ts, NULL);
}

/* ====== Wayland globals binding ====== */

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	State *s = (State *)data;

	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		s->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 5);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		s->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		s->layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 4);
	} else if (strcmp(interface, zwp_uwm_bar_v1_interface.name) == 0) {
		s->bar_manager = wl_registry_bind(registry, name, &zwp_uwm_bar_v1_interface, 1);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		s->seat = wl_registry_bind(registry, name, &wl_seat_interface, 8);
	}
}

static void handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

/* ====== Layer surface listener ====== */

static void layer_surface_configure(void *data,
		struct zwlr_layer_surface_v1 *surface,
		uint32_t serial, uint32_t width, uint32_t height) {
	State *s = (State *)data;
	zwlr_layer_surface_v1_ack_configure(surface, serial);
	s->width = width > 0 ? (int32_t)width : 1920;
	s->height = height > 0 ? height : BAR_HEIGHT;
	s->configured = true;
	s->need_redraw = true;
}

static void layer_surface_closed(void *data,
		struct zwlr_layer_surface_v1 *surface) {
	State *s = (State *)data;
	s->running = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

/* ====== UWM workspace group listener ====== */

static void ws_workspace(void *data,
		struct zwp_uwm_workspace_group_v1 *group,
		uint32_t id, uint32_t active, uint32_t occupied) {
	State *s = (State *)data;

	if (s->ws_batch_pending) {
		for (int i = 0; i < MAX_WORKSPACES; i++) {
			s->workspaces[i].active = false;
			s->workspaces[i].occupied = false;
		}
		s->ws_batch_pending = false;
	}

	if (id >= MAX_WORKSPACES) return;
	bool new_active = (active != 0);
	bool new_occupied = (occupied != 0);
	if (s->workspaces[id].active != new_active ||
	    s->workspaces[id].occupied != new_occupied)
		s->need_redraw = true;
	s->workspaces[id].id = id;
	s->workspaces[id].active = new_active;
	s->workspaces[id].occupied = new_occupied;
}

static void ws_focused_title(void *data,
		struct zwp_uwm_workspace_group_v1 *group,
		const char *title) {
	State *s = (State *)data;
	if (strcmp(s->focused_title, title) != 0) {
		strncpy(s->focused_title, title, sizeof(s->focused_title) - 1);
		s->need_redraw = true;
	}
}

static void ws_done(void *data,
		struct zwp_uwm_workspace_group_v1 *group) {
	State *s = (State *)data;
	s->ws_count = 0;
	for (int i = 0; i < MAX_WORKSPACES; i++) {
		if (s->workspaces[i].occupied || s->workspaces[i].active)
			s->ws_count = i + 1;
	}
	s->ws_batch_pending = true;
}

static const struct zwp_uwm_workspace_group_v1_listener workspace_group_listener = {
	.workspace = ws_workspace,
	.focused_title = ws_focused_title,
	.done = ws_done,
};

/* ====== Frame callback ====== */

void frame_done(void *data, struct wl_callback *cb, uint32_t time) {
	State *s = (State *)data;
	if (s->frame_callback) {
		wl_callback_destroy(s->frame_callback);
		s->frame_callback = NULL;
	}
	s->frame_pending = false;
}

const struct wl_callback_listener frame_listener = {
	.done = frame_done,
};

/* ====== Main ====== */

/* Poll FD indices */
#define FD_WL     0
#define FD_TIMER  1
#define FD_CLOCK  2
#define FD_AUDIO  3
#define FD_BATT   4
#define FD_NET    5
#define FD_DISP   6
#define NFDS      7

int main(int argc, char **argv) {
	strncpy(state.font, "JetBrainsMono Nerd Font 10", sizeof(state.font) - 1);
	state.font_desc = pango_font_description_from_string(state.font);
	state.bg_color = parse_color("#000409");
	state.fg_color = parse_color("#ffffff");
	state.ws_focused_text = parse_color("#ffffff");
	state.ws_inactive_text = parse_color("#555e6b");
	state.ws_urgent_text = parse_color("#c46464");
	state.running = true;
	state.height = BAR_HEIGHT;

	int opt;
	while ((opt = getopt(argc, argv, "f:h:")) != -1) {
		switch (opt) {
		case 'f':
			strncpy(state.font, optarg, sizeof(state.font) - 1);
			if (state.font_desc)
				pango_font_description_free(state.font_desc);
			state.font_desc = pango_font_description_from_string(state.font);
			break;
		case 'h':
			state.height = atoi(optarg);
			break;
		}
	}

	/* Connect to Wayland */
	state.display = wl_display_connect(NULL);
	if (!state.display) return 1;

	struct wl_registry *registry = wl_display_get_registry(state.display);
	wl_registry_add_listener(registry, &registry_listener, &state);

	if (wl_display_roundtrip(state.display) < 0) {
		wl_display_disconnect(state.display);
		return 1;
	}

	if (!state.compositor || !state.shm || !state.layer_shell) {
		wl_display_disconnect(state.display);
		return 1;
	}

	if (state.seat)
		input_init(&state, state.seat);

	/* Create layer surface */
	state.surface = wl_compositor_create_surface(state.compositor);
	state.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
		state.layer_shell, state.surface, NULL,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP, "ubar");

	zwlr_layer_surface_v1_add_listener(state.layer_surface,
		&layer_surface_listener, &state);

	zwlr_layer_surface_v1_set_size(state.layer_surface, 0, state.height);
	zwlr_layer_surface_v1_set_anchor(state.layer_surface,
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(state.layer_surface, state.height);
	zwlr_layer_surface_v1_set_keyboard_interactivity(
		state.layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

	wl_surface_commit(state.surface);

	for (int i = 0; i < 3 && !state.configured; i++)
		wl_display_roundtrip(state.display);

	/* Bind UWM protocol */
	if (state.bar_manager) {
		state.workspace_group = zwp_uwm_bar_v1_get_workspace_group(
			state.bar_manager, NULL);
		zwp_uwm_workspace_group_v1_add_listener(state.workspace_group,
			&workspace_group_listener, &state);
		wl_display_roundtrip(state.display);
	}

	/* Buffers */
	for (int i = 0; i < 3; i++) {
		state.bufs[i].buffer = NULL;
		state.bufs[i].busy = false;
	}
	state.frame_callback = NULL;
	state.frame_pending = false;

	/* Slow data timer — always 1s (CPU/temp/RAM counter) */
	state.timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	struct itimerspec ts = {
		.it_interval = { .tv_sec = 1, .tv_nsec = 0 },
		.it_value = { .tv_sec = 1, .tv_nsec = 0 },
	};
	timerfd_settime(state.timer_fd, 0, &ts, NULL);
	state.slow_timer = 0;
	state.prev_minute = -1;

	/* Clock timer — 1s when showing seconds, 60s when showing HH:MM */
	state.clock_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	set_clock_timer(&state);

	/* Create per-subsystem notification pipes */
	create_pipe(state.audio_pipe);
	create_pipe(state.battery_pipe);
	create_pipe(state.network_pipe);
	create_pipe(state.display_pipe);

	/* Init fast data (synchronous) */
	data_init_fast(&state);

	/* Start event-driven monitors */
	data_start_monitors(&state);

	signal(SIGINT, SIG_IGN);
	signal(SIGTERM, SIG_IGN);

	/* Main event loop */
	int wl_fd = wl_display_get_fd(state.display);
	struct pollfd fds[NFDS] = {
		[FD_WL]    = { .fd = wl_fd,                  .events = POLLIN },
		[FD_TIMER] = { .fd = state.timer_fd,          .events = POLLIN },
		[FD_CLOCK] = { .fd = state.clock_timer_fd,    .events = POLLIN },
		[FD_AUDIO] = { .fd = state.audio_pipe[0],     .events = POLLIN },
		[FD_BATT]  = { .fd = state.battery_pipe[0],   .events = POLLIN },
		[FD_NET]   = { .fd = state.network_pipe[0],   .events = POLLIN },
		[FD_DISP]  = { .fd = state.display_pipe[0],   .events = POLLIN },
	};

	while (state.running) {
		if (state.need_redraw && state.configured) {
			render_frame(&state);
			state.need_redraw = false;
		}

		if (wl_display_flush(state.display) < 0) {
			if (errno == EAGAIN) {
				fds[FD_WL].events = POLLIN | POLLOUT;
				poll(&fds[FD_WL], 1, 100);
				fds[FD_WL].events = POLLIN;
				continue;
			}
			break;
		}

		while (wl_display_prepare_read(state.display) != 0)
			wl_display_dispatch_pending(state.display);

		int ret = poll(fds, NFDS, -1);
		if (ret < 0) {
			wl_display_cancel_read(state.display);
			if (errno == EINTR) continue;
			break;
		}

		/* Wayland events */
		if (fds[FD_WL].revents & (POLLIN | POLLHUP | POLLERR)) {
			wl_display_read_events(state.display);
			wl_display_dispatch_pending(state.display);
		} else {
			wl_display_cancel_read(state.display);
		}

		if (fds[FD_WL].revents & (POLLERR | POLLHUP))
			break;

		/* Slow data timer tick (1s) — CPU/temp/RAM counter */
		if (fds[FD_TIMER].revents & POLLIN) {
			uint64_t exp;
			read(state.timer_fd, &exp, sizeof(exp));
			state.slow_timer++;
			if (state.slow_timer >= 1) {
				state.slow_timer = 0;
				if (data_update_slow_timer(&state))
					state.need_redraw = true;
			}
		}

		/* Clock timer tick (1s or 60s depending on mode) */
		if (fds[FD_CLOCK].revents & POLLIN) {
			uint64_t exp;
			read(state.clock_timer_fd, &exp, sizeof(exp));
			if (data_update_all_timer(&state))
				state.need_redraw = true;
		}

		/* Audio volume changed (PipeWire/PulseAudio event) */
		if (fds[FD_AUDIO].revents & POLLIN) {
			drain_pipe(state.audio_pipe[0]);
			data_sync_to_state(&state);
			state.need_redraw = true;
		}

		/* Battery changed (D-Bus UPower event) */
		if (fds[FD_BATT].revents & POLLIN) {
			drain_pipe(state.battery_pipe[0]);
			data_sync_to_state(&state);
			state.need_redraw = true;
		}

		/* Network changed (netlink event) */
		if (fds[FD_NET].revents & POLLIN) {
			drain_pipe(state.network_pipe[0]);
			data_sync_to_state(&state);
			state.need_redraw = true;
		}

		/* Display/HDMI/LEDs changed (udev event) */
		if (fds[FD_DISP].revents & POLLIN) {
			drain_pipe(state.display_pipe[0]);
			data_sync_to_state(&state);
			state.need_redraw = true;
		}
	}

	/* Shutdown */
	data_stop_monitors(&state);

	if (state.workspace_group)
		zwp_uwm_workspace_group_v1_destroy(state.workspace_group);
	if (state.bar_manager)
		zwp_uwm_bar_v1_destroy(state.bar_manager);
	if (state.pointer)
		wl_pointer_destroy(state.pointer);
	if (state.seat)
		wl_seat_destroy(state.seat);
	if (state.layer_surface)
		zwlr_layer_surface_v1_destroy(state.layer_surface);
	if (state.surface)
		wl_surface_destroy(state.surface);
	if (state.compositor)
		wl_compositor_destroy(state.compositor);
	if (state.shm)
		wl_shm_destroy(state.shm);
	if (state.layer_shell)
		zwlr_layer_shell_v1_destroy(state.layer_shell);

	input_destroy(&state);

	if (state.font_desc)
		pango_font_description_free(state.font_desc);
	if (state.frame_callback)
		wl_callback_destroy(state.frame_callback);
	for (int i = 0; i < 3; i++)
		destroy_buffer(&state.bufs[i]);

	close(state.timer_fd);
	close(state.clock_timer_fd);
	close(state.audio_pipe[0]); close(state.audio_pipe[1]);
	close(state.battery_pipe[0]); close(state.battery_pipe[1]);
	close(state.network_pipe[0]); close(state.network_pipe[1]);
	close(state.display_pipe[0]); close(state.display_pipe[1]);

	wl_display_disconnect(state.display);

	if (state.running) {
		execvp(argv[0], argv);
		return 1;
	}

	return 0;
}

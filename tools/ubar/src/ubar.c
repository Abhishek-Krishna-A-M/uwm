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

// ====== Wayland globals binding ======

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	State *s = (State *)data;

	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		s->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 5);
		LOG("bound wl_compositor v5");
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		s->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
		LOG("bound wl_shm v1");
	} else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		s->layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 4);
		LOG("bound zwlr_layer_shell_v1 v4");
	} else if (strcmp(interface, zwp_uwm_bar_v1_interface.name) == 0) {
		s->bar_manager = wl_registry_bind(registry, name, &zwp_uwm_bar_v1_interface, 1);
		LOG("bound zwp_uwm_bar_v1 v1");
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		s->seat = wl_registry_bind(registry, name, &wl_seat_interface, 8);
		LOG("bound wl_seat v8");
	} else {
		LOG("skipped global: %s v%u", interface, version);
	}
}

static void handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	LOG("global removed: name=%u", name);
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

// ====== Layer surface listener ======

static void layer_surface_configure(void *data,
		struct zwlr_layer_surface_v1 *surface,
		uint32_t serial, uint32_t width, uint32_t height) {
	State *s = (State *)data;
	zwlr_layer_surface_v1_ack_configure(surface, serial);
	s->width = width > 0 ? (int32_t)width : 1920;
	s->height = height > 0 ? height : BAR_HEIGHT;
	s->configured = true;
	s->need_redraw = true;
	LOG("layer surface configured: %ux%u serial=%u", s->width, s->height, serial);
}

static void layer_surface_closed(void *data,
		struct zwlr_layer_surface_v1 *surface) {
	State *s = (State *)data;
	LOG("layer surface closed by compositor");
	s->running = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

// ====== UWM workspace group listener ======

static void ws_workspace(void *data,
		struct zwp_uwm_workspace_group_v1 *group,
		uint32_t id, uint32_t active, uint32_t occupied) {
	State *s = (State *)data;

	/* On first event of a new batch, reset all workspace states so stale
	 * workspaces (that the compositor didn't send an update for) get cleared. */
	if (s->ws_batch_pending) {
		for (int i = 0; i < MAX_WORKSPACES; i++) {
			s->workspaces[i].active = false;
			s->workspaces[i].occupied = false;
		}
		s->ws_batch_pending = false;
	}

	if (id >= MAX_WORKSPACES) return;
	s->workspaces[id].id = id;
	s->workspaces[id].active = (active != 0);
	s->workspaces[id].occupied = (occupied != 0);
	LOG("workspace %u: active=%u occupied=%u", id, active, occupied);
}

static void ws_focused_title(void *data,
		struct zwp_uwm_workspace_group_v1 *group,
		const char *title) {
	State *s = (State *)data;
	if (strcmp(s->focused_title, title) != 0) {
		strncpy(s->focused_title, title, sizeof(s->focused_title) - 1);
		LOG("focused title: \"%s\"", title);
	}
}

static void ws_done(void *data,
		struct zwp_uwm_workspace_group_v1 *group) {
	State *s = (State *)data;
	int old_count = s->ws_count;
	s->ws_count = 0;
	for (int i = 0; i < MAX_WORKSPACES; i++) {
		if (s->workspaces[i].occupied || s->workspaces[i].active)
			s->ws_count = i + 1;
	}
	s->need_redraw = true;
	s->ws_batch_pending = true;
	if (old_count != s->ws_count)
		LOG("ws_count changed: %d -> %d", old_count, s->ws_count);
}

static const struct zwp_uwm_workspace_group_v1_listener workspace_group_listener = {
	.workspace = ws_workspace,
	.focused_title = ws_focused_title,
	.done = ws_done,
};

// ====== Frame callback ======

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

// ====== Main ======

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

	LOG("connecting to Wayland display...");
	state.display = wl_display_connect(NULL);
	if (!state.display) {
		LOG("FAILED to connect: %s", strerror(errno));
		return 1;
	}
	LOG("connected");

	struct wl_registry *registry = wl_display_get_registry(state.display);
	wl_registry_add_listener(registry, &registry_listener, &state);

	int rt = wl_display_roundtrip(state.display);
	LOG("roundtrip returned %d", rt);

	if (rt < 0) {
		LOG("roundtrip failed");
		wl_display_disconnect(state.display);
		return 1;
	}

	if (!state.compositor || !state.shm || !state.layer_shell) {
		LOG("missing globals: compositor=%p shm=%p layer_shell=%p",
			(void*)state.compositor, (void*)state.shm, (void*)state.layer_shell);
		wl_display_disconnect(state.display);
		return 1;
	}

	if (state.seat) {
		input_init(&state, state.seat);
		LOG("seat initialized");
	}

	// Create layer surface
	state.surface = wl_compositor_create_surface(state.compositor);
	state.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
		state.layer_shell, state.surface, NULL,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP, "ubar");
	LOG("layer surface created");

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

	for (int i = 0; i < 3 && !state.configured; i++) {
		wl_display_roundtrip(state.display);
	}
	LOG("configured=%d width=%d height=%u", state.configured, state.width, state.height);

	// Bind UWM protocol
	if (state.bar_manager) {
		state.workspace_group = zwp_uwm_bar_v1_get_workspace_group(
			state.bar_manager, NULL);
		zwp_uwm_workspace_group_v1_add_listener(state.workspace_group,
			&workspace_group_listener, &state);
		wl_display_roundtrip(state.display);
		LOG("UWM bar protocol bound");
	} else {
		LOG("no UWM bar manager — workspace info unavailable");
	}

	// Buffers
	for (int i = 0; i < 3; i++) {
		state.bufs[i].buffer = NULL;
		state.bufs[i].busy = false;
	}
	state.frame_callback = NULL;
	state.frame_pending = false;

	// Timer
	state.timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	struct itimerspec ts = {
		.it_interval = { .tv_sec = 1, .tv_nsec = 0 },
		.it_value = { .tv_sec = 1, .tv_nsec = 0 },
	};
	timerfd_settime(state.timer_fd, 0, &ts, NULL);
	LOG("timer created (1s interval)");

	// Notify pipe
	pipe2(state.notify_fds, O_CLOEXEC | O_NONBLOCK);
	LOG("notify pipe created (fd=%d,%d)", state.notify_fds[0], state.notify_fds[1]);

	// Init fast data
	data_init_fast(&state);
	LOG("fast data loaded: cpu=%d%% ram=%d%% bat=%d%% temp=%dC time=%s",
		state.cpu_pct, state.ram_pct, state.bat_pct, state.temp_c, state.time_str);

	// Start monitors
	data_start_monitors(&state);
	LOG("background monitors started");

	// Signals
	signal(SIGINT, SIG_IGN);
	signal(SIGTERM, SIG_IGN);

	// Main loop
	int wl_fd = wl_display_get_fd(state.display);
	struct pollfd fds[3] = {
		{ .fd = wl_fd,          .events = POLLIN },
		{ .fd = state.timer_fd, .events = POLLIN },
		{ .fd = state.notify_fds[0], .events = POLLIN },
	};

	LOG("entering main loop (wl_fd=%d)", wl_fd);

	while (state.running) {
		if (state.need_redraw && state.configured && !state.frame_pending) {
			render_frame(&state);
			state.need_redraw = false;
		}

		/* Flush pending Wayland events before polling */
		if (wl_display_flush(state.display) < 0) {
			if (errno == EAGAIN) {
				fds[0].events = POLLIN | POLLOUT;
				poll(fds, 1, 100);
				fds[0].events = POLLIN;
				continue;
			}
			LOG("wl_display_flush failed: %s", strerror(errno));
			break;
		}

		/* Prepare to read Wayland events (may need to dispatch pending first) */
		while (wl_display_prepare_read(state.display) != 0)
			wl_display_dispatch_pending(state.display);

		int ret = poll(fds, 3, -1);
		if (ret < 0) {
			wl_display_cancel_read(state.display);
			if (errno == EINTR) continue;
			LOG("poll failed: %s", strerror(errno));
			break;
		}

		/* Only read Wayland events if the Wayland fd actually has data */
		if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
			wl_display_read_events(state.display);
			wl_display_dispatch_pending(state.display);
		} else {
			wl_display_cancel_read(state.display);
		}

		if (fds[0].revents & (POLLERR | POLLHUP)) {
			LOG("Wayland fd error revents=0x%x", fds[0].revents);
			break;
		}

		if (fds[1].revents & POLLIN) {
			uint64_t exp;
			read(state.timer_fd, &exp, sizeof(exp));
			data_update_all_timer(&state);
			state.need_redraw = true;
		}

		if (fds[2].revents & POLLIN) {
			char buf[64];
			ssize_t n = read(state.notify_fds[0], buf, sizeof(buf));
			if (n > 0) {
				data_sync_to_state(&state);
				LOG("pipe notify: %.*s (vol=%d muted=%d net=%s)",
					(int)n, buf, state.vol_pct, state.muted, state.net_name);
			}
			state.need_redraw = true;
		}
	}

	LOG("exiting main loop");

	// Cleanup
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

	if (state.font_desc)
		pango_font_description_free(state.font_desc);
	if (state.frame_callback)
		wl_callback_destroy(state.frame_callback);
	for (int i = 0; i < 3; i++)
		destroy_buffer(&state.bufs[i]);

	close(state.timer_fd);
	close(state.notify_fds[0]);
	close(state.notify_fds[1]);

	wl_display_disconnect(state.display);

	if (state.running) {
		/* Unexpected disconnect (e.g. compositor crash recovery during VT
		 * switch). Re-exec ourselves to reconnect. */
		LOG("unexpected disconnect, reconnecting...");
		execvp(argv[0], argv);
		LOG("exec failed: %s", strerror(errno));
		return 1;
	}

	LOG("clean exit");
	return 0;
}

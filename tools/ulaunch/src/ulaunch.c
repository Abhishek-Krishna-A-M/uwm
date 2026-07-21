#include "ulaunch.h"
#include "render.h"
#include "input.h"
#include "theme.h"
#include "mode_dmenu.h"
#include "mode_drun.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <signal.h>
#include <getopt.h>
#include <sys/mman.h>
#include <sys/timerfd.h>

State state = {0};

uint32_t parse_color(const char *hex) {
	if (*hex == '#') hex++;
	unsigned int r, g, b;
	if (sscanf(hex, "%02x%02x%02x", &r, &g, &b) != 3)
		return 0xFFFFFFFF;
	return (0xFF << 24) | (r << 16) | (g << 8) | b;
}

void cairo_set_hex(cairo_t *cr, uint32_t color) {
	double a = ((color >> 24) & 0xFF) / 255.0;
	double r = ((color >> 16) & 0xFF) / 255.0;
	double g = ((color >> 8) & 0xFF) / 255.0;
	double b = (color & 0xFF) / 255.0;
	cairo_set_source_rgba(cr, r, g, b, a);
}

/* ====== Wayland ====== */

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	(void)data;
	if (strcmp(interface, wl_compositor_interface.name) == 0)
		state.compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 5);
	else if (strcmp(interface, wl_shm_interface.name) == 0)
		state.shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0)
		state.layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 4);
	else if (strcmp(interface, wl_seat_interface.name) == 0) {
		state.seat = wl_registry_bind(registry, name, &wl_seat_interface, 5);
		state.keyboard = wl_seat_get_keyboard(state.seat);
	}
}

static void handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {}

const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

/* ====== Layer surface ====== */

static void layer_surface_configure(void *data,
		struct zwlr_layer_surface_v1 *surface,
		uint32_t serial, uint32_t width, uint32_t height) {
	(void)data;
	zwlr_layer_surface_v1_ack_configure(surface, serial);
	state.output_w = (int32_t)width;
	state.output_h = height;
	state.width = (int32_t)width;
	state.height = height;
	state.configured = true;
	state.need_redraw = true;
}

static void layer_surface_closed(void *data,
		struct zwlr_layer_surface_v1 *surface) {
	(void)data;
	state.running = false;
}

const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

/* ====== Frame callback ====== */

static void frame_done(void *data, struct wl_callback *cb, uint32_t time) {
	(void)data;
	(void)time;
	if (state.frame_callback) {
		wl_callback_destroy(state.frame_callback);
		state.frame_callback = NULL;
	}
	state.frame_pending = false;
}

const struct wl_callback_listener frame_listener = {
	.done = frame_done,
};

/* ====== Help ====== */

static void print_usage(const char *name) {
	fprintf(stderr, "Usage: %s [options]\n", name);
	fprintf(stderr, "  -d, --dmenu      dmenu mode (read from stdin)\n");
	fprintf(stderr, "  -D, --drun       drun mode (list desktop entries)\n");
	fprintf(stderr, "  -p, --prompt STR prompt string\n");
	fprintf(stderr, "  -c, --config PATH config file path\n");
	fprintf(stderr, "     --daemon      run as background daemon (drun)\n");
	fprintf(stderr, "     --quit        quit running daemon\n");
	fprintf(stderr, "  -h, --help       show this help\n");
}

/* ====== Main ====== */

int main(int argc, char **argv) {
	state.mode = MODE_DMENU;
	const char *config_path = NULL;
	const char *cli_prompt = NULL;

	static struct option long_opts[] = {
		{"dmenu",  no_argument, 0, 'd'},
		{"drun",   no_argument, 0, 'D'},
		{"prompt", required_argument, 0, 'p'},
		{"config", required_argument, 0, 'c'},
		{"help",   no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "dDp:c:h", long_opts, NULL)) != -1) {
		switch (opt) {
		case 'd': state.mode = MODE_DMENU; break;
		case 'D': state.mode = MODE_DRUN; break;
		case 'p': cli_prompt = optarg; break;
		case 'c': config_path = optarg; break;
		case 'h': default: print_usage(argv[0]); return 0;
		}
	}

	/* load config */
	const char *home = getenv("HOME");
	char default_config[512] = {0};
	if (!config_path && home) {
		snprintf(default_config, sizeof(default_config), "%s/.config/ulaunch/config", home);
		config_path = default_config;
	}
	if (config_path)
		theme_load(&state.theme, config_path);

	/* CLI -p overrides config */
	if (cli_prompt)
		snprintf(state.theme.prompt, THEME_PROMPT_MAX, "%s", cli_prompt);

	/* --- single-shot: load entries --- */
	if (state.mode == MODE_DMENU) {
		if (mode_dmenu() != 0) {
			fprintf(stderr, "ulaunch: no input\n");
			return 1;
		}
	} else if (state.mode == MODE_DRUN) {
		if (mode_drun() != 0) {
			fprintf(stderr, "ulaunch: no desktop entries found\n");
			return 1;
		}
	}

	/* --- Wayland init --- */
	state.display = wl_display_connect(NULL);
	if (!state.display) goto err;

	struct wl_registry *registry = wl_display_get_registry(state.display);
	wl_registry_add_listener(registry, &registry_listener, NULL);

	if (wl_display_roundtrip(state.display) < 0) goto err;

	if (!state.compositor || !state.shm || !state.layer_shell) goto err;

	if (state.keyboard)
		input_init(state.keyboard);

	/* create layer surface */
	state.surface = wl_compositor_create_surface(state.compositor);
	state.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
		state.layer_shell, state.surface, NULL,
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "ulaunch");

	zwlr_layer_surface_v1_add_listener(state.layer_surface,
		&layer_surface_listener, NULL);

	zwlr_layer_surface_v1_set_size(state.layer_surface, 0, 0);
	zwlr_layer_surface_v1_set_anchor(state.layer_surface,
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);

	zwlr_layer_surface_v1_set_keyboard_interactivity(
		state.layer_surface,
		ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);

	wl_surface_commit(state.surface);

	for (int i = 0; i < 3 && !state.configured; i++)
		wl_display_roundtrip(state.display);

	if (!state.configured) goto err;

	for (int i = 0; i < 3; i++) {
		state.bufs[i].buffer = NULL;
		state.bufs[i].busy = false;
	}

	state.running = true;

	state.timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);

	filter_update();
	state.need_redraw = true;

	/* --- event loop --- */
	int wl_fd = wl_display_get_fd(state.display);

	signal(SIGINT, SIG_IGN);
	signal(SIGTERM, SIG_IGN);

	while (state.running) {
		if (state.need_redraw && state.configured) {
			render_frame();
			state.need_redraw = false;
		}

		if (wl_display_flush(state.display) < 0) {
			if (errno == EAGAIN) {
				struct pollfd pf = { .fd = wl_fd, .events = POLLIN | POLLOUT };
				poll(&pf, 1, 100);
				continue;
			}
			break;
		}

		while (wl_display_prepare_read(state.display) != 0)
			wl_display_dispatch_pending(state.display);

		struct pollfd fds[3];
		int nfds = 0, stdin_idx = -1, timer_idx;

		fds[nfds].fd = wl_fd;
		fds[nfds].events = POLLIN;
		nfds++;

		if (state.mode == MODE_DMENU && !state.dmenu_stdin_done) {
			fds[nfds].fd = STDIN_FILENO;
			fds[nfds].events = POLLIN;
			stdin_idx = nfds;
			nfds++;
		}

		fds[nfds].fd = state.timerfd;
		fds[nfds].events = POLLIN;
		timer_idx = nfds;
		nfds++;

		int ret = poll(fds, nfds, -1);
		if (ret < 0) {
			wl_display_cancel_read(state.display);
			if (errno == EINTR) continue;
			break;
		}

		if (stdin_idx >= 0 && (fds[stdin_idx].revents & (POLLIN | POLLHUP | POLLERR))) {
			dmenu_pump();
			filter_update();
			state.need_redraw = true;
		}

		if (fds[timer_idx].revents & POLLIN) {
			uint64_t exp;
			read(state.timerfd, &exp, sizeof(exp));
			input_repeat_fire();
		}

		if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
			wl_display_read_events(state.display);
			wl_display_dispatch_pending(state.display);
		} else {
			wl_display_cancel_read(state.display);
		}

		if (fds[0].revents & (POLLERR | POLLHUP))
			break;
	}

	close(state.timerfd);

	/* shutdown */
	if (state.frame_callback)
		wl_callback_destroy(state.frame_callback);
	if (state.keyboard) {
		wl_keyboard_release(state.keyboard);
		state.keyboard = NULL;
	}
	for (int i = 0; i < 3; i++)
		destroy_buffer(&state.bufs[i]);
	if (state.xkb_state) xkb_state_unref(state.xkb_state);
	if (state.xkb_keymap) xkb_keymap_unref(state.xkb_keymap);
	if (state.xkb_ctx) xkb_context_unref(state.xkb_ctx);
	if (state.layer_surface)
		zwlr_layer_surface_v1_destroy(state.layer_surface);
	if (state.surface)
		wl_surface_destroy(state.surface);
	if (state.seat)
		wl_seat_destroy(state.seat);
	if (state.layer_shell)
		zwlr_layer_shell_v1_destroy(state.layer_shell);
	if (state.compositor)
		wl_compositor_destroy(state.compositor);
	if (state.shm)
		wl_shm_destroy(state.shm);
	wl_display_disconnect(state.display);

	/* cleanup entries */
	for (int i = 0; i < state.n_entries; i++) {
		free(state.entries[i]);
		if (state.exec_cmds)
			free(state.exec_cmds[i]);
	}
	free(state.entries);
	free(state.exec_cmds);
	free(state.filtered);
	free(state.scores);
	free(state.hits);

	if (state.theme.font_desc)
		pango_font_description_free(state.theme.font_desc);

	return 0;

err:
	fprintf(stderr, "ulaunch: failed to initialize Wayland\n");
	return 1;
}

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include "server.h"
#include "config.h"
#include "layer_shell.h"
#include "idle_inhibit.h"

static void spawn_cmd(const char *cmd)
{
	/* Skip if this command is already running */
	if (fork() == 0) {
		setsid();
		/* Extract first token for pgrep -x exact match */
		char check[256];
		snprintf(check, sizeof(check),
			"pgrep -x \"$(echo '%s' | awk '{print $1}')\" >/dev/null 2>&1",
			cmd);
		if (system(check) == 0)
			_exit(0);
		char *args[] = { "sh", "-c", (char *)cmd, NULL };
		execvp("sh", args);
		_exit(1);
	}
}

static void run_autostart(void)
{
	for (const char *const *cmd = autostart; *cmd; cmd++)
		spawn_cmd(*cmd);
}

int main(int argc, char *argv[]) {
	wlr_log_init(WLR_DEBUG, NULL);
	char *startup_cmd = NULL;

	int c;
	while ((c = getopt(argc, argv, "s:h")) != -1) {
		switch (c) {
		case 's':
			startup_cmd = optarg;
			break;
		default:
			printf("Usage: %s [-s startup command]\n", argv[0]);
			return 0;
		}
	}
	if (optind < argc) {
		printf("Usage: %s [-s startup command]\n", argv[0]);
		return 0;
	}

	struct uwm_server server = {0};

	if (!server_init(&server)) {
		return 1;
	}

	const char *socket = wl_display_add_socket_auto(server.wl_display);
	if (!socket) {
		wlr_backend_destroy(server.backend);
		return 1;
	}

	if (!wlr_backend_start(server.backend)) {
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.wl_display);
		return 1;
	}

	setenv("WAYLAND_DISPLAY", socket, true);

	/* Run compile-time autostart commands */
	run_autostart();

	/* Run command-line startup command (if any) */
	if (startup_cmd)
		spawn_cmd(startup_cmd);

	wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s", socket);
	wl_display_run(server.wl_display);

	server_finish(&server);

	return 0;
}

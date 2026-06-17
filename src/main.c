#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <errno.h>
#include <wlr/util/log.h>
#include "server.h"
#include "config.h"
#include "layer_shell.h"
#include "idle_inhibit.h"

static const char *pid_dir = "/tmp/uwm-autostart";

static void ensure_pid_dir(void)
{
	mkdir(pid_dir, 0755);
}

/* Generate a stable filename from a command string by hashing the first
 * token (the executable name). This avoids issues with special characters
 * in command strings and keeps filenames short. */
static void cmd_to_pidfile(const char *cmd, char *buf, size_t len)
{
	char exe[64] = {0};
	sscanf(cmd, "%63s", exe);
	/* Simple hash for stable filename */
	unsigned long h = 5381;
	for (const char *p = exe; *p; p++)
		h = ((h << 5) + h) + (unsigned char)*p;
	snprintf(buf, len, "%s/%lu.pid", pid_dir, h);
}

static int read_pid(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) return -1;
	int pid = -1;
	fscanf(f, "%d", &pid);
	fclose(f);
	return pid;
}

static bool pid_alive(int pid)
{
	if (pid <= 0) return false;
	return kill(pid, 0) == 0 || errno == EPERM;
}

static void spawn_cmd(const char *cmd)
{
	ensure_pid_dir();

	char pidfile[128];
	cmd_to_pidfile(cmd, pidfile, sizeof(pidfile));

	/* Check if an existing instance is still running */
	int old_pid = read_pid(pidfile);
	if (old_pid > 0 && pid_alive(old_pid)) {
		return; /* already running */
	}

	/* Clean up stale pid file */
	if (old_pid > 0)
		unlink(pidfile);

	if (fork() == 0) {
		setsid();
		/* Write our PID before exec so future invocations can detect us */
		FILE *pf = fopen(pidfile, "w");
		if (pf) {
			fprintf(pf, "%d\n", getpid());
			fclose(pf);
		}
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

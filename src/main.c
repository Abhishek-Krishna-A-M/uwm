#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <wlr/util/log.h>
#include <wayland-server-core.h>
#include "server.h"
#include "config.h"
#include "layer_shell.h"
#include "idle_inhibit.h"

static void crash_handler(int sig) {
	if (g_crash_jmpbuf_valid) {
		g_crash_jmpbuf_valid = 0;
		write(STDERR_FILENO, "UWM: crash caught, recovering\n", 31);
		siglongjmp(g_crash_jmpbuf, sig);
	}
	signal(sig, SIG_DFL);
	raise(sig);
}

static void install_crash_handlers(struct sigaction *old) {
	struct sigaction sa;
	sa.sa_handler = crash_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NODEFER | SA_RESTART;
	sigaction(SIGSEGV, &sa, &old[0]);
	sigaction(SIGABRT, &sa, &old[1]);
	sigaction(SIGBUS, &sa, &old[2]);
	sigaction(SIGFPE, &sa, &old[3]);
	sigaction(SIGILL, &sa, &old[4]);
}

static void restore_crash_handlers(struct sigaction *old) {
	sigaction(SIGSEGV, &old[0], NULL);
	sigaction(SIGABRT, &old[1], NULL);
	sigaction(SIGBUS, &old[2], NULL);
	sigaction(SIGFPE, &old[3], NULL);
	sigaction(SIGILL, &old[4], NULL);
}

static char pid_dir[256];

static void resolve_pid_dir(void)
{
	const char *rt = getenv("XDG_RUNTIME_DIR");
	if (rt && rt[0]) {
		snprintf(pid_dir, sizeof(pid_dir), "%s/uwm-autostart", rt);
	} else {
		snprintf(pid_dir, sizeof(pid_dir), "/tmp/uwm-autostart");
	}
}

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

static void clean_pid_dir(void)
{
	ensure_pid_dir();
	DIR *dir = opendir(pid_dir);
	if (!dir) return;

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_type != DT_REG)
			continue;
		size_t len = strlen(entry->d_name);
		if (len < 4 || strcmp(entry->d_name + len - 4, ".pid") != 0)
			continue;

		char path[128];
		snprintf(path, sizeof(path), "%s/%s", pid_dir, entry->d_name);
		int pid = read_pid(path);
		if (!pid_alive(pid))
			unlink(path);
	}
	closedir(dir);
}

static void run_autostart(void)
{
	clean_pid_dir();
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

	resolve_pid_dir();

	/* Run compile-time autostart commands */
	run_autostart();

	/* Run command-line startup command (if any) */
	if (startup_cmd)
		spawn_cmd(startup_cmd);


	wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s", socket);

	struct sigaction old_handlers[5];
	install_crash_handlers(old_handlers);

	if (sigsetjmp(g_crash_jmpbuf, 1) == 0) {
		g_crash_jmpbuf_valid = 1;
		wl_display_run(server.wl_display);
		g_crash_jmpbuf_valid = 0;
	} else {
		g_crash_jmpbuf_valid = 0;
		write(STDERR_FILENO, "UWM: recovered, rebuilding\n", 28);
		uwm_rebuild_session_listeners(&server);
		uwm_call_session_active(&server);
		write(STDERR_FILENO, "UWM: restarting event loop\n", 28);
		g_crash_jmpbuf_valid = 1;
		wl_display_run(server.wl_display);
		g_crash_jmpbuf_valid = 0;
	}

	restore_crash_handlers(old_handlers);

	server_finish(&server);

	return 0;
}

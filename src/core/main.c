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
#include <wlr/config.h>
#include <wayland-server-core.h>
#include "server.h"
#include "config.h"
#include "child.h"
#include "layer_shell.h"
#include "idle_inhibit.h"

/* Crash handling.
 *
 * Recovering in-process with siglongjmp() re-enters wl_display_run() at an
 * arbitrary point with libinput/DRM/dmabuf state half-torn down. If the crash
 * is deterministic (which it usually is — the same client event replays) the
 * retry crashes immediately, and because the handler stays armed it loops
 * siglongjmp -> wl_display_run -> crash -> siglongjmp forever. That is a
 * 100% CPU spin with no input processing and no repaints: the whole desktop
 * looks dead, and only killing the process (or a lucky state change) recovers.
 *
 * So: allow a small number of in-process retries, each of which disarms the
 * handler first, then fall back to execv() so the compositor restarts from a
 * clean address space. Clients reconnect on their own. */
#define UWM_MAX_CRASH_RESTARTS 3

static int crash_restarts;

static void crash_handler(int sig) {
	if (g_crash_jmpbuf_valid) {
		g_crash_jmpbuf_valid = 0;
		ssize_t rc = write(STDERR_FILENO, "UWM: crash caught, recovering\n", 31);
		(void)rc;
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
	if (fscanf(f, "%d", &pid) != 1) pid = -1;
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

	pid_t pid = fork();
	if (pid < 0) {
		wlr_log(WLR_ERROR, "fork() failed for '%s': %s", cmd, strerror(errno));
		return;
	}
	if (pid == 0) {
		/* Drop the compositor's signal mask and dispositions before exec:
		 * SIGINT/SIGTERM are blocked process-wide by libwayland's signalfd
		 * and that block survives exec, so an autostart entry would be born
		 * unable to be terminated by systemd's session teardown. */
		uwm_child_reset_signals();
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

	/* Remember it so server_finish() can guarantee nothing outlives the
	 * session scope. */
	uwm_child_register(pid);
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
	/* Debug logging is opt-in (UWM_DEBUG=1) so the input/focus hot
	 * paths don't pay fprintf syscalls on every event. */
	wlr_log_init(getenv("UWM_DEBUG") ? WLR_DEBUG : WLR_ERROR, NULL);
	char *startup_cmd = NULL;

#if WLR_HAS_XWAYLAND
	/* Keep the default compositor session pure Wayland. XWayland is an
	 * explicit opt-in via -x so launching uwm cannot create an extra X server
	 * unexpectedly. */
	bool enable_xwayland = false;
#else
	bool enable_xwayland = false;
#endif
	int c;
	while ((c = getopt(argc, argv, "s:xXh")) != -1) {
		switch (c) {
		case 's':
			startup_cmd = optarg;
			break;
		case 'x':
			enable_xwayland = true;
			break;
		case 'X':
			enable_xwayland = false;
			break;
		default:
			printf("Usage: %s [-s startup command] [-x|-X]\n", argv[0]);
			printf("  -x  enable XWayland\n");
			printf("  -X  disable XWayland (pure Wayland)\n");
			return 0;
		}
	}
	if (optind < argc) {
		printf("Usage: %s [-s startup command] [-x|-X]\n", argv[0]);
		return 0;
	}

	struct uwm_server server = {0};
#if WLR_HAS_XWAYLAND
	server.xwayland_enabled = enable_xwayland;
#else
	if (enable_xwayland) {
		fprintf(stderr, "uwm: XWayland not compiled in (WLR_HAS_XWAYLAND=0)\n");
		return 1;
	}
#endif

	if (!server_init(&server)) {
		return 1;
	}

	if (!wlr_backend_start(server.backend)) {
		server_finish(&server);
		return 1;
	}

	resolve_pid_dir();

	/* Run compile-time autostart commands */
	run_autostart();

	/* Run command-line startup command (if any) */
	if (startup_cmd)
		spawn_cmd(startup_cmd);

	wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s", server.socket);

	struct sigaction old_handlers[5];
	/* Escape hatch for debugging: our SIGSEGV/SIGABRT handlers replace
	 * whatever was there (including ASan's), so a sanitizer or gdb run
	 * needs to be able to keep the original handler and see the real
	 * fault instead of a re-raised default action. */
	if (!getenv("UWM_NO_CRASH_HANDLER"))
		install_crash_handlers(old_handlers);

	/* Bounded recovery loop. Each iteration arms the handler for exactly one
	 * fault; the handler clears g_crash_jmpbuf_valid before jumping, so a
	 * fault inside the recovery path escalates to SIG_DFL instead of
	 * re-entering the loop. Once the budget is spent we re-exec. */
	while (crash_restarts < UWM_MAX_CRASH_RESTARTS) {
		g_crash_jmpbuf_valid = 1;
		if (sigsetjmp(g_crash_jmpbuf, 1) == 0) {
			wl_display_run(server.wl_display);
			g_crash_jmpbuf_valid = 0;
			break;
		}
		crash_restarts++;
		g_crash_jmpbuf_valid = 0;

		if (crash_restarts >= UWM_MAX_CRASH_RESTARTS) {
			/* Re-exec rather than keep limping along a torn-down state.
			 * restore_crash_handlers() below never runs on this path. */
			char msg[96];
			int n = snprintf(msg, sizeof(msg),
				"UWM: crash budget exhausted, re-exec\n");
			if (n > 0) {
				ssize_t ignored = write(STDERR_FILENO, msg, (size_t)n);
				(void)ignored;
			}
			restore_crash_handlers(old_handlers);
			execv(argv[0], argv);
			/* execv only returns on failure — fall through and exit. */
			break;
		}

		ssize_t rc = write(STDERR_FILENO, "UWM: recovered, rebuilding\n", 28);
		(void)rc;
		uwm_rebuild_session_listeners(&server);
		uwm_call_session_active(&server);
		rc = write(STDERR_FILENO, "UWM: restarting event loop\n", 28);
		(void)rc;
	}

	restore_crash_handlers(old_handlers);

	server_finish(&server);

	return 0;
}

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include "child.h"
#include "session_env.h"

/* The variables that make up a Wayland desktop session. Only the ones actually
 * set are exported: DISPLAY only exists with `uwm -x`, and asking
 * dbus-update-activation-environment to import an unset name is a no-op we do
 * not need. */
static const char *const session_vars[] = {
	"XDG_CURRENT_DESKTOP",
	"XDG_SESSION_DESKTOP",
	"XDG_SESSION_TYPE",
	"WAYLAND_DISPLAY",
	"DISPLAY",
};

#define SESSION_VAR_COUNT (sizeof(session_vars) / sizeof(session_vars[0]))

void uwm_session_setup_env(void) {
	/* Overwrite rather than set-if-absent: uwm is the session, and a stale
	 * XDG_CURRENT_DESKTOP from a previous login on the same TTY would make
	 * portals and apps pick the wrong backend. */
	setenv("XDG_CURRENT_DESKTOP", "UWM", 1);
	setenv("XDG_SESSION_DESKTOP", "UWM", 1);
	setenv("XDG_SESSION_TYPE", "wayland", 1);
}

/* dbus-update-activation-environment, detached.
 *
 * bus_path, when non-NULL, overrides DBUS_SESSION_BUS_ADDRESS in the child.
 * systemd is only passed --systemd when a caller asks for it; see the comment on
 * uwm_session_export_env() for why that is not always the same bus. */
static void spawn_env_helper(const char *bus_path, bool systemd, char **vars, size_t nvars) {
	pid_t pid = fork();
	if (pid < 0) {
		wlr_log(WLR_ERROR, "fork() for dbus-update-activation-environment failed: %s",
			strerror(errno));
		return;
	}

	if (pid == 0) {
		uwm_child_reset_signals();

		/* Nothing of the compositor's terminal should be inherited by a
		 * helper that outlives a few milliseconds. */
		int devnull = open("/dev/null", O_RDWR);
		if (devnull >= 0) {
			dup2(devnull, STDIN_FILENO);
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
			if (devnull > STDERR_FILENO)
				close(devnull);
		}

		if (bus_path)
			setenv("DBUS_SESSION_BUS_ADDRESS", bus_path, 1);

		char *argv[SESSION_VAR_COUNT + 4];
		size_t n = 0;
		argv[n++] = (char *)"dbus-update-activation-environment";
		if (systemd)
			argv[n++] = (char *)"--systemd";
		for (size_t i = 0; i < nvars; i++)
			argv[n++] = vars[i];
		argv[n] = NULL;

		execvp(argv[0], argv);
		_exit(127);
	}

	/* Deliberately not registered as a uwm child: it is a one-shot that must
	 * never be part of the shutdown sequence. The SIGCHLD handler reaps it. */
}

void uwm_session_export_env(void) {
	if (!getenv("DBUS_SESSION_BUS_ADDRESS")) {
		/* Launched without a session bus. There is no activation
		 * environment to update and no systemd user manager to talk to, so
		 * running the helper would only print an error. */
		return;
	}

	char *vars[SESSION_VAR_COUNT];
	size_t nvars = 0;
	for (size_t i = 0; i < SESSION_VAR_COUNT; i++) {
		if (getenv(session_vars[i]))
			vars[nvars++] = (char *)session_vars[i];
	}
	if (nvars == 0)
		return;

	/* Two different buses are involved, and conflating them silently loses
	 * half of the session:
	 *
	 *   - DBUS_SESSION_BUS_ADDRESS is the bus uwm's own children talk to.
	 *     With the documented `dbus-run-session uwm` launcher that is a
	 *     private bus that only this session's processes are on. Its
	 *     activators — xdg-desktop-portal and its backends, D-Bus activated
	 *     apps — inherit their environment from it.
	 *   - The systemd --user manager always lives on $XDG_RUNTIME_DIR/bus,
	 *     regardless of what DBUS_SESSION_BUS_ADDRESS says. User services
	 *     inherit from there.
	 *
	 * So the activation environment goes to the current bus, and the systemd
	 * user manager gets its own pass pointed at its own socket. Doing only
	 * `--systemd` on the current bus works for a normal login but does
	 * nothing at all under `dbus-run-session` — the helper just warns that
	 * org.freedesktop.systemd1 is unreachable and exits 0. Doing both passes
	 * unconditionally is harmless when the two buses are the same one, since
	 * the operation is idempotent. */
	spawn_env_helper(NULL, false, vars, nvars);

	char user_bus[PATH_MAX + sizeof("unix:path=")];
	const char *rt = getenv("XDG_RUNTIME_DIR");
	if (rt && rt[0]
			&& (size_t)snprintf(user_bus, sizeof(user_bus), "unix:path=%s/bus", rt) < sizeof(user_bus)
			&& access(user_bus + sizeof("unix:path=") - 1, F_OK) == 0) {

		spawn_env_helper(user_bus, true, vars, nvars);
	}
}
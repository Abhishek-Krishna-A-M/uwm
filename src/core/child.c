#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include "child.h"

/* Slot table rather than a list: uwm_child_reaped() runs from the SIGCHLD
 * handler and may only perform plain stores, so reaped pids are dropped by
 * writing 0 over their slot and no allocation can happen behind its back.
 *
 * 512 concurrent children is far more than a session needs; a full table only
 * costs us the ability to terminate the excess, which is logged. */
#define UWM_MAX_CHILDREN 512
static volatile sig_atomic_t child_pids[UWM_MAX_CHILDREN];

/* Grace period before the survivors get SIGKILL. Short: children that honour
 * SIGTERM (every Wayland app does, since their connection is closing too) are
 * gone within a few milliseconds. */
#define UWM_CHILD_GRACE_TRIES 50
#define UWM_CHILD_GRACE_NS    (20 * 1000 * 1000) /* 20ms */

void uwm_child_reset_signals(void) {
	sigset_t empty;
	sigemptyset(&empty);
	sigprocmask(SIG_SETMASK, &empty, NULL);

	/* Back to the defaults a normal program expects. SIGINT/SIGTERM are
	 * already SIG_DFL in uwm (libwayland reads them from a signalfd), but
	 * being explicit keeps this correct if that ever changes. */
	signal(SIGHUP, SIG_DFL);
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	signal(SIGPIPE, SIG_DFL);
}

void uwm_child_register(pid_t pid) {
	if (pid <= 0)
		return;

	for (size_t i = 0; i < UWM_MAX_CHILDREN; i++) {
		if (child_pids[i] != 0)
			continue;

		child_pids[i] = pid;

		/* The child can already be gone by now. Either the SIGCHLD that
		 * announced it has not been delivered yet, so the handler had no
		 * pid to clear, or it fired before the pid was recorded above. One
		 * non-blocking wait settles both, so a recycled pid can never end
		 * up in the table and be signalled as one of ours at shutdown. */
		if (waitpid(pid, NULL, WNOHANG) == pid)
			child_pids[i] = 0;
		return;
	}

	wlr_log(WLR_ERROR, "child table full, pid %d will not be terminated on exit",
		(int)pid);
}

void uwm_child_reaped(pid_t pid) {
	for (size_t i = 0; i < UWM_MAX_CHILDREN; i++) {
		if (child_pids[i] == pid)
			child_pids[i] = 0;
	}
}

void uwm_children_terminate(void) {
	pid_t live[UWM_MAX_CHILDREN];
	size_t n = 0;

	for (size_t i = 0; i < UWM_MAX_CHILDREN; i++) {
		pid_t pid = child_pids[i];
		if (pid <= 0)
			continue;
		child_pids[i] = 0;
		live[n++] = pid;
	}

	if (n == 0)
		return;

	/* setsid() in the fork helpers makes each child a process group leader,
	 * so -pid takes out its whole subtree. */
	for (size_t i = 0; i < n; i++) {
		if (kill(-live[i], SIGTERM) < 0 && errno != ESRCH)
			wlr_log(WLR_ERROR, "SIGTERM to pid %d failed: %s",
				(int)live[i], strerror(errno));
	}

	struct timespec grace = {0, UWM_CHILD_GRACE_NS};
	for (int tries = 0; tries < UWM_CHILD_GRACE_TRIES; tries++) {
		size_t alive = 0;
		for (size_t i = 0; i < n; i++) {
			/* ESRCH means the group is empty. EPERM means it exists but
			 * we may not signal it, which still counts as alive. */
			if (kill(-live[i], 0) == 0 || errno != ESRCH)
				alive++;
		}
		if (alive == 0)
			return;
		nanosleep(&grace, NULL);
	}

	for (size_t i = 0; i < n; i++) {
		if (kill(-live[i], SIGKILL) < 0 && errno != ESRCH)
			wlr_log(WLR_ERROR, "SIGKILL to pid %d failed: %s",
				(int)live[i], strerror(errno));
	}
}
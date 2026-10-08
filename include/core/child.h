#ifndef CHILD_H
#define CHILD_H

#include <stdbool.h>
#include <sys/types.h>

/* Child processes started by uwm.
 *
 * Everything uwm spawns (autostart entries, the -s startup command, keybinding
 * spawns) is a session process: it lives in the same systemd session scope as
 * uwm. systemd cannot stop session-N.scope until that cgroup is empty, and it
 * refuses to force the issue before TimeoutStopSec (90s for a scope) has passed.
 * So a single surviving child is enough to stall poweroff/reboot on
 * "A stop job is running for Session N of User X". uwm therefore owns its
 * children: it remembers them and terminates them on the way out.
 *
 * Every child is put in its own session by the fork helpers, so signalling the
 * process group (-pid) also reaches grandchildren (a terminal starts a server
 * process which then starts clients).
 */

/* Put a forked child back into a pristine signal state. Call in the child,
 * immediately after fork() and before exec().
 *
 * Two separate pieces of state are inherited across fork() and survive exec():
 *
 *  - the signal mask: wl_event_loop_add_signal() makes libwayland block
 *    SIGINT and SIGTERM process-wide so it can read them from a signalfd. A
 *    blocked signal can never be delivered, so every autostart program is born
 *    unable to react to systemd's KillSignal=15 and would only ever die to the
 *    90s SIGKILL fallback.
 *  - the dispositions: uwm runs with SIGHUP and SIGPIPE set to SIG_IGN, which is
 *    correct for a compositor but wrong for the applications it starts.
 */
void uwm_child_reset_signals(void);

/* Record a child so it can be terminated at shutdown. Safe to call from the
 * main thread only. */
void uwm_child_register(pid_t pid);

/* Clear a pid that has already been reaped. Async-signal-safe: called from the
 * SIGCHLD handler. */
void uwm_child_reaped(pid_t pid);

/* SIGTERM every tracked child, wait briefly, then SIGKILL whatever is left.
 * Idempotent, and never blocks longer than the grace period. */
void uwm_children_terminate(void);

#endif /* CHILD_H */
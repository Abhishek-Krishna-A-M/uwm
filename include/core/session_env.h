#ifndef SESSION_ENV_H
#define SESSION_ENV_H

/* Desktop session environment.
 *
 * uwm is launched by hand from a TTY (`dbus-run-session uwm [options]`), so
 * nothing else sets the session variables for it. Anything that is started by
 * the session bus or by a systemd --user unit inherits its environment from the
 * D-Bus daemon / user manager, not from uwm, so uwm has to push the Wayland
 * session variables into both explicitly. Otherwise D-Bus activated
 * applications, xdg-desktop-portal backends and systemd user services come up
 * with no WAYLAND_DISPLAY and cannot reach the compositor.
 */

/* Publish XDG_CURRENT_DESKTOP, XDG_SESSION_DESKTOP and XDG_SESSION_TYPE into
 * uwm's own environment. Call once the Wayland socket exists — it must not run
 * before then, and it must run before any autostart child is spawned so the
 * children inherit it. */
void uwm_session_setup_env(void);

/* Push the session variables into the D-Bus activation environment and into
 * the systemd --user manager environment
 * (dbus-update-activation-environment --systemd). Call after
 * uwm_session_setup_env(), and again once new variables become available (for
 * example DISPLAY after XWayland reports ready). */
void uwm_session_export_env(void);

#endif /* SESSION_ENV_H */
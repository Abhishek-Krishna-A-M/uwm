# Changelog

All notable changes to UWM are recorded here. The project uses
[semantic versioning](https://semver.org/); release tags are prefixed `v`
(`v1.1.0`, `v1.0.0`, …).

## 1.1.0

Correctness and robustness release. The headline item is that context menus
work: right-click menus, `<select>` dropdowns and overlay settings panes never
rendered, because first-level `xdg_popup` surfaces were dropped before they
reached the scene graph.

### Fixed

- **Context menus and overlay popups did not render.** A popup parented
  directly to a toplevel — the shape of every context menu — was skipped
  instead of being given a scene tree. Popups are now constrained to the
  output's *logical* box and re-constrained on every commit, so menus opened
  near a screen edge are flipped back on-screen instead of running off it.
- **Focused-window border was offset on client-side-decorated windows.**
  wlroots places an xdg scene node so its origin already *is* the window
  geometry origin; uwm was adding `geo.x/geo.y` on top, double-counting the
  frame. Visible on any browser using its own title bar rather than the
  system one.
- **Monocle cycled windows whenever a transient dialog closed.** A file
  picker or upload box returning focus picked the head of the workspace list
  instead of the window that was actually visible. Monocle also no longer
  collapses when a workspace is down to one tiled window.
- **`ulaunch` aborted on any entry list over 512 items.** The filter wrote an
  unbounded count into a fixed stack array, so `Super+e` (2506 commands) and
  `Super+Alt+f` (tens of thousands of files) died with a stack-smash while
  `Super+r` (49 desktop entries) kept working.
- **`Super+Alt+f` never opened the file.** It ran `cd "$HOME/$f"` on a *file*,
  which fails with `ENOTDIR`, so the `&&` short-circuited and nvim never
  launched. It also listed all of `$HOME` with `fd --hidden`, a scan that does
  not finish on a real home directory; it now uses `rg --files` and reuses
  the running terminal via `footclient`.
- **`Super+e` ran commands with no arguments.** `RUN` piped the selection into
  `xargs -r`, which performs no shell expansion; it now pipes into `sh -s`, so
  tilde, quoting, pipes and globs all work.
- **A crash could freeze the whole desktop.** The crash handler retried
  `siglongjmp` once with the signal handler still armed, so a deterministic
  fault looped forever at 100% CPU with no input and no repaints. Recovery is
  now bounded and falls back to re-exec.
- **A startup-time null pointer.** The global server pointer was only assigned
  on the first key press, so anything reached before that saw `NULL`.
- **Fullscreen used physical pixels.** It is now sized and positioned in
  logical layout coordinates, which also fixes fullscreen on a secondary
  monitor.
- Scroll wheel no longer changes volume. It previously reached the bar and
  forked a `wpctl` process on every wheel tick; use the volume keys or click
  the volume zone to mute.

### Added

- Layer-shell popup support, so menus parented to a bar or notification
  surface work.
- `make install` now installs `uwm`, `ubar` and `ulaunch` together.
- `UWM_NO_CRASH_HANDLER=1` keeps the default fault handlers so sanitizers and
  `gdb` report real faults.
- `ulaunch` filters incrementally and ingests stdin in 64 KB chunks: a
  36,000-entry list settles in ~9 re-filters (~130 ms) instead of ~440.

### Performance

- Border updates skip redundant work — they no longer re-raise, and thereby
  damage the whole tiled layer, when nothing changed — and duplicate
  per-commit updates were removed.
- Window size configures are compared against the last *requested* size rather
  than the client's reported geometry. On a client-side-decorated window those
  differ by the frame, which previously caused a configure on every arrange.
- `ulaunch` caches the Ctrl modifier index instead of doing an xkb lookup per
  key press, removed a duplicate `wl_display_flush`, gained the missing frame
  throttle, and uses `damage_buffer` instead of the deprecated `damage`.

### Tests

The suite is now tracked and passes 11/11. All eight files failed beforehand:
six referenced paths from before the source tree was split into
`src/{core,input,output,shell,ui,wm}`, and two asserted behaviour that never
existed.

### Upgrading

`config.h` is regenerated from `config.def.h` by `make`. Two macros changed
behaviour, both launcher commands:

- `FINDFILE` now lists with `rg --files` and opens via `footclient` in the
  file's directory.
- `RUN` now pipes the selection into `sh -s`.

If you keep local overrides in `config.h`, review them against
`config.def.h` — otherwise the old `xargs -r` and the broken
`cd "$HOME/$f"` come back.

## 1.0.0

First stable release after 0.9.1. Includes focused-window borders, improved
floating-window behavior, monocle-mode improvements, multi-output fixes, UBar
updates, performance and resource-management work, and Xwayland support.

Xwayland became opt-in rather than always-on:

```sh
uwm       # Pure Wayland session
uwm -x    # Enable Xwayland for X11 applications
uwm -X    # Explicitly disable Xwayland
```

## Earlier releases

`v0.9.1`, `v0.9.0` and `v0.8.0` predate this file; see the git history and
tag annotations for those.

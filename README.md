# UWM

UWM is a lightweight BSP-based Wayland compositor built on wlroots, inspired by bspwm and dwl. It focuses on simplicity, performance, and a keyboard-driven workflow.

## Release 1.1.0

UWM 1.1.0 is a correctness and robustness release. The headline item is that
context menus work: right-click menus, `<select>` dropdowns and overlay
settings panes never rendered because first-level `xdg_popup` surfaces were
dropped before they reached the scene graph.

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
  `siglongjmp` once with the handler still armed, so a deterministic fault
  looped forever at 100% CPU with no input or repaints. Recovery is now
  bounded and falls back to re-exec.
- **A startup-time null pointer.** The global server pointer was only assigned
  on the first key press, so anything reached before that saw `NULL`.
- Scroll wheel no longer changes volume (it reached the bar and forked a
  `wpctl` per wheel tick); use the volume keys or click the bar to mute.

### Added

- Layer-shell popup support, so menus parented to a bar or notification
  surface work.
- `make install` now installs `uwm`, `ubar` and `ulaunch` together.
- `UWM_NO_CRASH_HANDLER=1` keeps the default fault handlers so sanitizers and
  `gdb` report real faults.
- `ulaunch` filters incrementally and ingests stdin in 64 KB chunks: a
  36,000-entry list now settles in ~9 re-filters (~130 ms) instead of ~440.

### Performance

Border updates skip redundant work — they no longer re-raise (and thereby
damage the whole tiled layer) when nothing changed, and duplicate per-commit
updates were removed. Window size configures are compared against the last
*requested* size rather than the client's reported geometry, which on a
client-side-decorated window differs by the frame and previously caused a
configure on every arrange. Fullscreen windows are sized and positioned in
logical layout coordinates instead of physical pixels.

### Tests

The suite is now tracked and passes 11/11. All eight files failed beforehand:
six referenced paths from before the source tree was split into
`src/{core,input,output,shell,ui,wm}`, and two asserted behaviour that never
existed.

## Release 1.0.0

UWM 1.0.0 is the first stable release after 0.9.1. It includes focused-window borders, improved floating-window behavior, monocle-mode improvements, multi-output fixes, UBar updates, performance and resource-management work, and Xwayland support.

Xwayland is opt-in:

```sh
uwm       # Pure Wayland session
uwm -x    # Enable Xwayland for X11 applications
uwm -X    # Explicitly disable Xwayland
```

## Philosophy

- **BSP-first workflow**: Windows are organized in a binary space partitioning tree. This is the primary layout mode.
- **Minimalism**: No blur, no animations, no shadows, no unnecessary visual effects. Only what improves the workflow.
- **Keyboard-driven**: All operations are accessible through keyboard shortcuts. Mouse interaction is supported for floating windows only.
- **Compile-time configuration**: Configuration is done in `config.h` before building. No runtime config parser. Recompile after changes.
- **Low resource usage**: Event-driven architecture with no polling loops. Static object pools for predictable memory usage.
- **Daily-driver oriented**: Designed for personal daily use, not for feature completeness.

## Features

### Window Management

- Dynamic BSP tiling with vertical and horizontal splits
- Floating windows with pointer move and resize
- Fullscreen mode
- Monocle mode (workspace-level, preserves tree structure)
- Focus movement in all directions
- Window swap in all directions
- Resize support (tiled ratio and floating dimensions)
- Focused-window border for tiled and floating windows
- Keyboard movement and resizing with synchronized borders
- Split rotation
- Focus cycling

### Workspaces

- 9 workspaces (configurable at compile time)
- Workspace switching
- Workspace movement (move windows between workspaces)
- Previous workspace toggle
- Workspace increment/decrement
- Per-output workspace assignment

### Outputs

- Multi-monitor support
- Output hotplug detection
- Extend mode (each output owns different workspaces)
- Mirror mode (configurable via `MIRROR_NEW_OUTPUTS`)
- Auto-arrangement of outputs
- Per-output layer shell surfaces

### Wayland Protocols

- xdg-shell v3
- Layer shell v5 (wlr-layer-shell-unstable-v1)
- xdg-decoration (server-side decoration)
- KDE server-decoration (fallback for GTK3)
- Idle inhibit
- Screencopy (wlr-screencopy-v1)
- ext-image-copy-capture-v1 (per-window screen sharing)
- ext-output-image-capture-source-v1 (monitor selection)
- ext-foreign-toplevel-list-v1 (window listing for portals)
- ext-foreign-toplevel-image-capture-source-v1 (per-window capture)
- export-dmabuf-v1 (zero-copy frame access)
- linux-dmabuf-v1 (PipeWire screen capture)
- Transient seat protocol
- Primary selection
- Xwayland (enabled with `uwm -x`)

### Desktop Integration

- Wallpaper support via swaybg (external)
- Notifications via mako (external)
- Fuzzel compatibility (application launcher)
- UBar integration (custom bar protocol: `zwp_uwm_bar_v1`)
- PipeWire and WirePlumber autostart
- xdg-desktop-portal support

### Input

- Focus follows pointer (configurable)
- Tap to click
- Natural scroll
- Acceleration profile selection
- Key repeat (configurable delay and rate)
- VT switching (Ctrl+Alt+F1-F12)

### Configuration

- Compile-time keybindings
- Compile-time window rules (app_id/title matching with globs)
- Compile-time autostart commands
- Appearance settings (border, gaps, floating dimensions)

## Screenshots

Coming soon.

## Dependencies

- wlroots 0.20
- wayland-server
- xkbcommon
- libinput
- xorg-xwayland (optional, required for X11 applications)

## Building

```sh
git clone <repository-url>
cd uwm
make
```

### ASAN Build

```sh
make ASAN=1
```

### Clean

```sh
make clean
make distclean
```

## Configuration

UWM uses compile-time configuration via header files.

1. Copy `config.def.h` to `config.h`
2. Edit `config.h` to customize settings
3. Recompile with `make`

All configuration changes require recompilation. There is no runtime configuration parser.

## Default Keybindings

All keybindings use **Super (Logo)** as the primary modifier.

### Launchers

| Binding | Action |
|---------|--------|
| `Super+Return` | Launch terminal (footclient) |
| `Super+r` | Launch application launcher (fuzzel) |
| `Super+e` | Launch run command |

### Navigation

| Binding | Action |
|---------|--------|
| `Super+h` / `Super+Left` | Focus left |
| `Super+j` / `Super+Down` | Focus down |
| `Super+k` / `Super+Up` | Focus up |
| `Super+l` / `Super+Right` | Focus right |
| `Super+c` | Cycle focus to next window |
| `Super+Tab` | Switch to previous workspace |

### Workspace Management

| Binding | Action |
|---------|--------|
| `Super+1`..`Super+9` | Switch to workspace N |
| `Super+Shift+1`..`Super+Shift+9` | Move window to workspace N |
| `Super+bracketleft` | Previous workspace |
| `Super+bracketright` | Next workspace |

### BSP Layout

| Binding | Action |
|---------|--------|
| `Super+Shift+h` / `Super+Shift+Left` | Swap with left window |
| `Super+Shift+j` / `Super+Shift+Down` | Swap with window below |
| `Super+Shift+k` / `Super+Shift+Up` | Swap with window above |
| `Super+Shift+l` / `Super+Shift+Right` | Swap with right window |
| `Super+Shift+r` | Rotate split direction |
| `Super+Alt+h` / `Super+Alt+Left` | Resize (decrease ratio) |
| `Super+Alt+j` / `Super+Alt+Down` | Resize (decrease ratio) |
| `Super+Alt+k` / `Super+Alt+Up` | Resize (increase ratio) |
| `Super+Alt+l` / `Super+Alt+Right` | Resize (increase ratio) |
| `Super+Alt+Shift+h` / `Super+Alt+Shift+Left` | Shrink floating left |
| `Super+Alt+Shift+j` / `Super+Alt+Shift+Down` | Shrink floating down |
| `Super+Alt+Shift+k` / `Super+Alt+Shift+Up` | Shrink floating up |
| `Super+Alt+Shift+l` / `Super+Alt+Shift+Right` | Shrink floating right |

### Window Operations

| Binding | Action |
|---------|--------|
| `Super+f` | Toggle fullscreen |
| `Super+s` | Toggle floating |
| `Super+m` | Toggle monocle |
| `Super+t` | Set BSP mode (exit floating/monocle/fullscreen) |
| `Super+w` | Close window |
| `Super+Shift+w` | Force-close window |
| `Super+space` | Window switcher script |
| `Super+Shift+f` | File manager (lf) |
| `Super+Alt+f` | Find file |
| `Super+Alt+x` | Power menu |

### Screenshots

| Binding | Action |
|---------|--------|
| `Super+Print` | Screenshot full screen |
| `Super+Shift+s` | Screenshot region to clipboard |
| `Print` | Screenshot region and save + copy |

### System

| Binding | Action |
|---------|--------|
| `Super+Alt+q` | Quit UWM |
| `Super+Alt+space` | HDMI script |

### Unmodified Keys (No Modifier)

| Binding | Action |
|---------|--------|
| `XF86AudioRaiseVolume` | Volume up |
| `XF86AudioLowerVolume` | Volume down |
| `XF86AudioMute` | Toggle mute |
| `XF86MonBrightnessUp` | Brightness up |
| `XF86MonBrightnessDown` | Brightness down |
| `Print` | Screenshot region |

## Project Structure

```
uwm/
├── Makefile                # Build system (outputs to build/)
├── config.def.h            # Default fallback configuration (tracked)
├── config.h                # Active local configuration (git-ignored)
├── README.md               # Project documentation
├── uwm.desktop             # Display manager entry
├── backup/                 # Crash dumps, binary snapshots
├── build/                  # Generated artifacts (mirrors src/ layout)
│   ├── uwm                 # Final binary (symlinked as ./uwm)
│   ├── core/               # core/*.o
│   ├── input/              # input/*.o
│   ├── output/             # output/*.o
│   ├── shell/              # shell/*.o
│   ├── ui/                 # ui/*.o
│   ├── wm/                 # wm/*.o
│   └── protocol/           # protocol/*.o
├── docs/                   # Extended docs, notes, man pages (kept)
│   ├── sway/               # reference (read-only)
│   ├── bspwm/              # bspwm reference
│   └── ...
├── protocol/               # Wayland XML + generated C/H
│   ├── xdg-shell-protocol.c/h
│   ├── wlr-layer-shell-unstable-v1-protocol.c/h
│   ├── uwm-bar-unstable-v1-protocol.c/h
│   └── *.xml
├── tools/                  # Helper tools (ubar, ulaunch)
│   ├── ubar/
│   └── ulaunch/
├── include/                # Headers grouped by domain
│   ├── core/
│   │   ├── config.h
│   │   └── server.h
│   ├── input/
│   │   └── input.h
│   ├── output/
│   │   └── output.h
│   ├── shell/
│   │   ├── idle_inhibit.h
│   │   ├── layer_shell.h
│   │   └── session_lock.h
│   ├── ui/
│   │   └── uwm_bar.h
│   └── wm/
│       ├── bsp.h
│       ├── floating.h
│       ├── layout.h
│       ├── rules.h
│       ├── window.h
│       └── workspace.h
└── src/                    # Sources grouped by domain (mirrors include/)
    ├── core/
    │   ├── main.c          # Entry, autostart, crash recovery
    │   ├── server.c        # Server init/teardown (957 → split into session/capture)
    │   └── config.c        # Compile-time config glue
    ├── input/
    │   └── input.c         # Keyboard/pointer/seat (1005 → cursor/keyboard/actions)
    ├── output/
    │   └── output.c        # Output management
    ├── shell/
    │   ├── idle_inhibit.c
    │   ├── layer_shell.c
    │   └── session_lock.c
    ├── ui/
    │   └── uwm_bar.c
    └── wm/
        ├── bsp.c           # BSP tree (739 → pool/tree/arrange/nav)
        ├── floating.c
        ├── layout.c
        ├── rules.c
        ├── window.c        # 1278 → toplevel/focus/xdg/xwayland
        └── workspace.c
```

## Performance

UWM is designed for minimal resource usage:

- **Minimal CPU usage**: Event-driven architecture with no polling loops. The compositor sleeps until an event occurs.
- **Low memory usage**: Static object pools (512 BSP nodes, 256 toplevels) avoid runtime allocations in hot paths. No dynamic growth of core state.
- **Responsive under load**: Damage tracking ensures only changed regions are redrawn. Focus and layout operations are direct and predictable.

## Inspirations

- **bspwm**: BSP tiling model, keyboard-driven philosophy
- **dwl**: wlroots-based compositor design, minimal architecture, compile-time configuration
- **wlroots**: Wayland compositor library providing the rendering and protocol foundation

UWM does not claim compatibility with any of these projects. It is an independent compositor that borrows design principles.

## Non-goals

- No animations
- No blur
- No runtime configuration parser
- No unnecessary abstractions
- No plugin system
- No scripting engine
- Xwayland is disabled unless UWM is started with `-x`
- No IPC/uwmctl (planned but not implemented)

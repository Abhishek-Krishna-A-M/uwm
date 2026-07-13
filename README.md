# UWM

UWM is a lightweight BSP-based Wayland compositor built on wlroots, inspired by bspwm and dwl. It focuses on simplicity, performance, and a keyboard-driven workflow.

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
├── src/                    # Source files
│   ├── main.c              # Entry point, autostart
│   ├── server.c            # Server initialization and teardown
│   ├── bsp.c               # BSP tree management
│   ├── workspace.c         # Workspace management
│   ├── floating.c          # Floating window support
│   ├── layout.c            # Layout modes (tabbed, monocle)
│   ├── window.c            # Window lifecycle and focus
│   ├── input.c             # Keyboard and pointer handling
│   ├── output.c            # Output management
│   ├── layer_shell.c       # Layer shell protocol
│   ├── uwm_bar.c           # UWM bar protocol
│   ├── idle_inhibit.c      # Idle inhibit protocol
│   ├── rules.c             # Window rules
│   └── config.c            # Configuration loading
├── include/                # Header files
│   ├── server.h
│   ├── bsp.h
│   ├── workspace.h
│   ├── floating.h
│   ├── layout.h
│   ├── window.h
│   ├── input.h
│   ├── output.h
│   ├── layer_shell.h
│   ├── uwm_bar.h
│   ├── idle_inhibit.h
│   ├── rules.h
│   └── config.h
├── protocol/               # Wayland protocol implementations
│   ├── uwm-bar-unstable-v1.xml
│   ├── wlr-layer-shell-unstable-v1-protocol.c
│   └── uwm-bar-unstable-v1-protocol.c
├── docs/                   # Documentation
│   ├── ARCHITECTURE.md
│   ├── KEYBINDS.md
│   ├── ROADMAP.md
│   └── SCREEN_SHARING_PLAN.md
├── config.def.h            # Default configuration
├── Makefile                # Build system
└── startup.sh              # Startup script
```

## Performance

UWM is designed for minimal resource usage:

- **Minimal CPU usage**: Event-driven architecture with no polling loops. The compositor sleeps until an event occurs.
- **Low memory usage**: Static object pools (512 BSP nodes, 256 toplevels) avoid runtime allocations in hot paths. No dynamic growth of core state.
- **Responsive under load**: Damage tracking ensures only changed regions are redrawn. Focus and layout operations are direct and predictable.

## Inspirations

- **bspwm**: BSP tiling model, keyboard-driven philosophy, compile-time configuration
- **dwl**: wlroots-based compositor design, minimal architecture
- **wlroots**: Wayland compositor library providing the rendering and protocol foundation

UWM does not claim compatibility with any of these projects. It is an independent compositor that borrows design principles.

## Non-goals

- No animations
- No blur
- No runtime configuration parser
- No unnecessary abstractions
- No plugin system
- No scripting engine
- No XWayland support (planned but not implemented)
- No IPC/uwmctl (planned but not implemented)

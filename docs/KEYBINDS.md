# UWM Keybindings Reference

This document describes the default UWM keybindings.

Keyboard bindings are registered through:

```text
river_xkb_bindings_v1
```

Pointer bindings are registered through:

```text
river_seat_v1.get_pointer_binding()
```

All keybindings ultimately dispatch commands through UWM's command system.

---

# Modifier Convention

| Symbol | Key             |
| ------ | --------------- |
| `$mod` | Super / Windows |
| `$alt` | Alt             |
| `S`    | Shift           |

UWM follows a BSPWM / Sway inspired convention.

---

# Terminal

| Binding         | Action      |
| --------------- | ----------- |
| `$mod + Return` | Launch foot |

---

# Session

| Binding           | Action               |
| ----------------- | -------------------- |
| `$mod + S + p`    | Lock screen          |
| `$mod + $alt + q` | Exit UWM             |
| `$mod + Escape`   | Reload configuration |

---

# Application Launchers

| Binding        | Action               |
| -------------- | -------------------- |
| `$mod + r`     | Application launcher |
| `$mod + S + r` | Command launcher     |
| `$mod + Space` | Window launcher      |

---

# Window Actions

| Binding        | Action                    |
| -------------- | ------------------------- |
| `$mod + q`     | Close focused window      |
| `$mod + S + q` | Force kill focused window |

---

# Focus Movement

| Binding          | Action          |
| ---------------- | --------------- |
| `$mod + h`       | Focus left      |
| `$mod + j`       | Focus down      |
| `$mod + k`       | Focus up        |
| `$mod + l`       | Focus right     |
| `$mod + Tab`     | Next window     |
| `$mod + S + Tab` | Previous window |

---

# Window Movement

| Binding        | Action     |
| -------------- | ---------- |
| `$mod + S + h` | Move left  |
| `$mod + S + j` | Move down  |
| `$mod + S + k` | Move up    |
| `$mod + S + l` | Move right |

---

# Layout Controls

## BSP Splits

| Binding    | Action           |
| ---------- | ---------------- |
| `$mod + o` | Split horizontal |
| `$mod + e` | Split vertical   |

---

## Container Modes

| Binding        | Action                  |
| -------------- | ----------------------- |
| `$mod + m`     | Toggle tabbed container |
| `$mod + S + m` | Toggle monocle          |
| `$mod + f`     | Toggle fullscreen       |
| `$mod + s`     | Toggle floating         |
| `$mod + t`     | Force tiled             |

---

# Resize

| Binding           | Action        |
| ----------------- | ------------- |
| `$mod + $alt + h` | Shrink width  |
| `$mod + $alt + l` | Grow width    |
| `$mod + $alt + j` | Grow height   |
| `$mod + $alt + k` | Shrink height |

---

# Workspaces

## Navigation

| Binding    | Action      |
| ---------- | ----------- |
| `$mod + 1` | Workspace 1 |
| `$mod + 2` | Workspace 2 |
| `$mod + 3` | Workspace 3 |
| `$mod + 4` | Workspace 4 |
| `$mod + 5` | Workspace 5 |

---

## Move Window

| Binding        | Action              |
| -------------- | ------------------- |
| `$mod + S + 1` | Send to workspace 1 |
| `$mod + S + 2` | Send to workspace 2 |
| `$mod + S + 3` | Send to workspace 3 |
| `$mod + S + 4` | Send to workspace 4 |
| `$mod + S + 5` | Send to workspace 5 |

---

# Floating Window Controls

| Binding        | Action                     |
| -------------- | -------------------------- |
| `$mod + Left`  | Move floating window left  |
| `$mod + Right` | Move floating window right |
| `$mod + Up`    | Move floating window up    |
| `$mod + Down`  | Move floating window down  |

---

# Media Keys

| Binding                 | Action         |
| ----------------------- | -------------- |
| `XF86AudioRaiseVolume`  | Volume +5%     |
| `XF86AudioLowerVolume`  | Volume -5%     |
| `XF86AudioMute`         | Toggle mute    |
| `XF86MonBrightnessUp`   | Brightness +5% |
| `XF86MonBrightnessDown` | Brightness -5% |

---

# Screenshots

| Binding        | Action                |
| -------------- | --------------------- |
| `Print`        | Region screenshot     |
| `$mod + Print` | Fullscreen screenshot |

---

# Browser

| Binding    | Action         |
| ---------- | -------------- |
| `$mod + b` | Launch browser |

---

# Monocle Behavior

Monocle is a container state.

It is not a workspace layout.

Enabling monocle:

* Preserves BSP tree
* Preserves split ratios
* Preserves tab state
* Preserves focus state

Only the focused window remains visible.

Disabling monocle restores the original structure instantly.

---

# Command System

All keybindings dispatch commands.

Example:

```text
Mod+H
    ↓
focus-left
    ↓
command dispatcher
    ↓
focus engine
```

The same commands are reused by:

* Keybindings
* IPC
* uwmctl
* Future automation tools

---

# Notes

* Unbound keys are forwarded to the focused application.
* Keybindings are registered per seat.
* Keyboard bindings use `river_xkb_bindings_v1`.
* Pointer bindings use `river_seat_v1.get_pointer_binding()`.
* Bindings are enabled during a manage sequence.
* The exact default bindings may evolve before UWM v1.0.

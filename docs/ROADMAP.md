# Development Roadmap

UWM is built incrementally.

Every version must:

* Compile successfully
* Run under River 0.4.5
* Be testable
* Be usable as the foundation for the next phase

No speculative implementations.

No future-phase code.

No placeholders pretending to work.

---

# v0.1 — River Integration

## Goal

Prove that UWM can participate correctly in the River 0.4.5 protocol lifecycle.

Success criteria:

```text
UWM starts
    ↓
Connects to Wayland
    ↓
Discovers River globals
    ↓
Registers Super+Return
    ↓
Launches foot
    ↓
Tracks created windows
```

## Deliverables

* Wayland connection
* Registry discovery
* Bind `river_window_manager_v1`
* Bind `river_xkb_bindings_v1`
* Window tracking
* Output tracking
* Seat tracking
* Manage sequence handling
* Render sequence handling
* Application spawning
* Super+Return → foot

## Excluded

* Layout engine
* BSP
* Workspaces
* Floating
* Fullscreen
* Tabbed
* Monocle
* IPC
* Configuration system

## Test

```text
Start River
    ↓
Start UWM
    ↓
Press Super+Return
    ↓
foot launches
    ↓
Window appears
```

## Repository

```text
src/
├── main.c
├── registry.c
├── wm.c
├── window.c
├── output.c
├── seat.c
├── binding.c

include/
├── registry.h
├── wm.h
├── window.h
├── output.h
├── seat.h
├── binding.h

protocol/
```

---

# v0.2 — Basic Layout Engine

## Goal

Create the first real tiling behavior.

Single output.

Single workspace.

All windows arranged horizontally.

## Deliverables

* Layout engine
* Window positioning
* Window sizing
* Focus newest window
* Close focused window

## Test

```text
Open 3 terminals

+-------+-------+-------+
|   1   |   2   |   3   |
+-------+-------+-------+
```

Close one window.

Remaining windows expand automatically.

---

# v0.3 — Dynamic BSP

## Goal

Replace the simple layout with dynamic BSP insertion.

## Deliverables

* BSP tree
* Horizontal split
* Vertical split
* Split ratio tracking
* Focus movement
* Window swapping

## Keybinds

```text
Super+H
Super+J
Super+K
Super+L

Focus movement

Super+O
Horizontal split

Super+E
Vertical split
```

## Test

```text
Open multiple terminals

Split horizontally
Split vertically

Navigate with hjkl
```

---

# v0.4 — Workspaces

## Goal

Independent workspace state.

## Deliverables

* Workspace abstraction
* Workspace switching
* Per-workspace BSP trees
* Window migration between workspaces

## Keybinds

```text
Super+1..9
Switch workspace

Super+Shift+1..9
Move window
```

## Test

```text
Workspace 1
    Firefox

Workspace 2
    Foot

Switch between them
```

---

# v0.5 — Floating and Fullscreen

## Goal

Support non-tiled workflows.

## Deliverables

* Floating windows
* Fullscreen windows
* Interactive move
* Interactive resize
* Pointer operations

## Keybinds

```text
Super+S
Toggle floating

Super+F
Toggle fullscreen
```

## Test

```text
Toggle floating
Move window

Toggle fullscreen
Restore window
```

---

# v0.6 — Tabbed Containers

## Goal

Introduce tabbed containers.

## Deliverables

* Tabbed container type
* Tab switching
* Convert split → tabbed
* Convert tabbed → split

## Keybinds

```text
Super+M
Toggle tabbed

Super+Tab
Next tab
```

## Test

```text
3 windows

[Firefox]
[Foot]
[Nvim]

Switch tabs
```

---

# v0.7 — Monocle State

## Goal

Monocle is a container state, not a workspace layout.

## Deliverables

* Monocle state
* Focus cycling
* Hide non-focused windows
* Preserve underlying tree

## Requirements

Monocle must preserve:

* Split ratios
* Focus history
* Child ordering
* Tab state

Disabling monocle restores the original structure instantly.

## Keybinds

```text
Super+Shift+M
Toggle monocle
```

## Test

```text
Several windows

Enable monocle

Only focused window visible

Disable monocle

Original layout restored
```

---

# v0.8 — IPC and Control Interface

## Goal

Expose UWM functionality externally.

## Deliverables

* Unix domain socket
* Command dispatch
* Query system
* Event subscriptions

## Tools

```text
uwmctl
```

## Examples

```text
uwmctl focus-left
uwmctl toggle-monocle
uwmctl query focused-window
```

---

# v0.9 — UWM Bar

## Goal

Build a dedicated status bar for UWM.

## Deliverables

* Workspace display
* Focused window title
* System information
* IPC subscriptions
* Event-driven updates

## Requirements

* No polling where possible
* Low memory usage
* Fast startup
* Separate process from UWM

---

# v1.0 — Daily Driver

## Goal

Replace existing window managers for daily use.

Requirements:

* Stable
* Reliable
* Responsive
* Documented

Features:

* BSP
* Floating
* Fullscreen
* Tabbed
* Monocle
* Workspaces
* IPC
* uwmctl
* uwm-bar

UWM v1.0 is considered complete when it can be used as a primary desktop environment under River without requiring another window manager.

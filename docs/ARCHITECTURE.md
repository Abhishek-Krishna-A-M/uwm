# UWM Architecture

## Overview

UWM is a River 0.4.x window manager that implements the `river-window-management-v1` protocol, giving it control over window management policy:

* Keybindings
* Focus
* Window placement
* Window sizing
* Decorations
* Layout management

UWM communicates with River exclusively through Wayland protocol objects.

River 0.4.x separates the compositor and the window manager into independent processes.

River is responsible for:

* Rendering
* DRM/KMS
* Input backend management
* Output management
* Frame scheduling
* Protocol dispatch

UWM is responsible for:

* Keybindings
* Focus policy
* Layout policy
* Window placement
* Workspace management
* Floating management
* Fullscreen management
* Monocle management

---

## Project Goals

UWM is designed for:

* Low-end hardware
* Low memory usage
* Deterministic behavior
* Maximum responsiveness
* Minimal complexity
* Productivity-oriented workflows

Target hardware:

* Intel i3 5th generation
* 4 GB RAM
* Low-end laptops
* Embedded Linux systems

Primary goals:

* Dynamic BSP insertion
* Horizontal split containers
* Vertical split containers
* Tabbed containers
* Floating windows
* Fullscreen windows
* Monocle state
* Multi-workspace support
* Multi-monitor support
* Configurable keybindings

Non-goals:

* Blur
* Shadows
* Rounded corners
* Heavy animations
* Plugin systems
* Runtime scripting engines

---

## Implementation Language

UWM is implemented in C17.

Reasons:

* Direct compatibility with River reference implementations
* Direct compatibility with Wayland client APIs
* Minimal abstraction overhead
* Easier protocol exploration during early development
* Straightforward integration with River protocol headers

Performance is expected to come primarily from:

* Data layout
* Cache locality
* Allocation strategy
* Event-driven architecture
* Simplicity

rather than from language choice.

---

## Protocol Stack

A River 0.4.x window manager binds the following Wayland globals:

| Priority | Global                    | Version | Purpose                                                              | Required |
| -------- | ------------------------- | ------- | -------------------------------------------------------------------- | -------- |
| 1        | `river_window_manager_v1` | 4       | Window lifecycle, focus, fullscreen, decorations, session management | Yes      |
| 2        | `river_xkb_bindings_v1`   | 1       | Keyboard bindings (xkbcommon keysyms)                                | Yes      |

### Interfaces Created by river_window_manager_v1 Events

| Event    | Interface         | Purpose                                         |
| -------- | ----------------- | ----------------------------------------------- |
| `window` | `river_window_v1` | Per-window control                              |
| `output` | `river_output_v1` | Output management                               |
| `seat`   | `river_seat_v1`   | Focus, pointer bindings, interactive operations |

### Interfaces Created by Requests

| Request                 | Interface               | Returns                    |
| ----------------------- | ----------------------- | -------------------------- |
| `get_node()`            | `river_window_v1`       | `river_node_v1`            |
| `get_xkb_binding()`     | `river_xkb_bindings_v1` | `river_xkb_binding_v1`     |
| `get_pointer_binding()` | `river_seat_v1`         | `river_pointer_binding_v1` |

---

## Window Management vs Rendering State

River separates state into two categories.

### Window Management State

Can only be modified during a manage sequence.

Examples:

* `focus_window()`
* `clear_focus()`
* `propose_dimensions()`
* `close()`
* `use_csd()`

Changes become active after:

```text
manage_finish()
```

### Rendering State

Can be modified during a manage sequence or render sequence.

Examples:

* `river_node_v1.set_position()`
* `river_node_v1.place_top()`
* `hide()`
* `show()`
* `set_borders()`

Changes become active after:

```text
render_finish()
```

---

## Manage / Render Lifecycle

The core protocol loop is controlled by River.

```text
1. River sends all state changes since last manage cycle
   followed by:

   manage_start

2. UWM responds during the manage sequence

   ├── focus_window()
   ├── clear_focus()
   ├── propose_dimensions()
   ├── close()
   ├── use_csd()
   └── manage_finish()

3. River applies window-management state

4. River waits for clients to acknowledge updates

5. River sends updated dimensions events

6. River sends:

   render_start

7. UWM responds during render sequence

   ├── river_node_v1.set_position()
   ├── river_node_v1.place_top()
   ├── hide()
   ├── show()
   ├── set_borders()
   └── render_finish()

8. River presents the updated frame
```

The manage/render cycle repeats whenever:

* Windows are created
* Windows are destroyed
* Focus changes
* Layout changes
* Rendering state changes

---

## Keybinding System

UWM owns all keyboard and pointer bindings.

Keyboard bindings use:

```text
river_xkb_bindings_v1
```

Pointer bindings use:

```text
river_seat_v1.get_pointer_binding()
```

---

### Keyboard Bindings

```text
river_xkb_bindings_v1
    ↓
get_xkb_binding()
    ↓
river_xkb_binding_v1
    ↓
pressed/released events
```

Bindings must be enabled during a manage sequence.

```text
enable()
```

---

### Pointer Bindings

```text
river_seat_v1
    ↓
get_pointer_binding()
    ↓
river_pointer_binding_v1
    ↓
pressed/released events
```

Bindings must be enabled during a manage sequence.

```text
enable()
```

---

### Binding Dispatch Flow

```text
Key Press
    ↓
River matches registered binding
    ↓
River sends pressed event
    ↓
UWM stores pending action
    ↓
River sends manage_start
    ↓
UWM dispatches action
    ↓
manage_finish
    ↓
Input resumes
```

Actions may include:

* Spawn application
* Focus window
* Close window
* Toggle fullscreen
* Toggle floating
* Toggle monocle
* Switch workspace

---

### Modifier Bitmask

| Bit | Value | Name  | Common Name |
| --- | ----- | ----- | ----------- |
| 0   | 1     | shift | Shift       |
| 2   | 4     | ctrl  | Ctrl        |
| 3   | 8     | mod1  | Alt         |
| 5   | 32    | mod3  | —           |
| 6   | 64    | mod4  | Super       |
| 7   | 128   | mod5  | AltGr       |

---

## Focus Management

Focus is controlled through:

```text
river_seat_v1.focus_window()
```

Focus removal:

```text
river_seat_v1.clear_focus()
```

Z-order is controlled through:

```text
river_node_v1.place_top()
```

Focus policy is entirely owned by UWM.

---

## Startup Sequence

```text
1. wl_display_connect()

2. wl_display_get_registry()

3. Register registry listener

4. wl_display_roundtrip()

5. Discover globals

   ├── river_window_manager_v1
   └── river_xkb_bindings_v1

6. Validate required globals exist

7. Register river_window_manager_v1 listener

8. Enter dispatch loop
```

---

## Event Loop

Initial implementation:

```c
while (true) {
    wl_display_dispatch(display);
}
```

Characteristics:

* Single-threaded
* Blocking
* Event-driven
* No polling
* No busy waiting

The initial implementation does not use:

* epoll
* IPC multiplexing
* timerfd
* eventfd

These may be introduced later when additional subsystems require them.

---

## Architecture Evolution

UWM is developed incrementally.

### v0.1

* Wayland connection
* River protocol binding
* Keybinding registration
* Application spawning
* Window tracking

### v0.2

* Single workspace
* Single output
* Simple layout engine

### v0.3

* Dynamic BSP insertion
* Focus movement

### v0.4

* Workspaces

### v0.5

* Floating
* Fullscreen

### v0.6

* Tabbed containers

### v0.7

* Monocle state

### v0.8

* IPC
* uwmctl

### v0.9

* uwm-bar integration

Correctness and protocol understanding take priority over optimization during early development.

Optimization is introduced only after behavior is verified.

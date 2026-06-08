# UWM River Protocol Reference

## Overview

UWM communicates with River 0.4.x exclusively through Wayland protocol objects. This document describes every protocol interface UWM binds, its purpose, and how it is used.

Protocol knowledge comes from:
1. **`river-window-management-v1.xml`** — canonical protocol definition (from River source tree, mirrored in `tinyrwm/c/protocol/`)
2. **`river-xkb-bindings-v1.xml`** — canonical keyboard binding protocol (from River source tree, mirrored in `tinyrwm/c/protocol/`)
3. **tinyrwm** — canonical reference implementation ([codeberg.org/river/tinyrwm](https://codeberg.org/river/tinyrwm))

---

## Protocols Overview

All interfaces are created through the Wayland registry or as child objects of `river_window_manager_v1`:

| Global | Interface | Version | Purpose |
|--------|-----------|---------|---------|
| `river_window_manager_v1` | `river_window_manager_v1` | 4 | Primary WM protocol: lifecycle, focus, fullscreen |
| `river_xkb_bindings_v1` | `river_xkb_bindings_v1` | 1 | Keyboard binding registration |

Child interfaces created by `river_window_manager_v1` events:

| Event | Interface | Purpose |
|-------|-----------|---------|
| `window` | `river_window_v1` | Per-window control |
| `output` | `river_output_v1` | Per-output state |
| `seat` | `river_seat_v1` | Focus, pointer bindings, interactive ops |

Child interfaces created by requests:

| Request | On Interface | Returns |
|---------|-------------|---------|
| `get_node` | `river_window_v1` | `river_node_v1` |
| `get_xkb_binding` | `river_xkb_bindings_v1` | `river_xkb_binding_v1` |
| `get_pointer_binding` | `river_seat_v1` | `river_pointer_binding_v1` |

---

## `river_window_manager_v1`

**Version**: 4
**Registered from**: Wayland registry global `river_window_manager_v1`
**Singleton**: Yes (only one WM client at a time)

### Events Received (River → UWM)

| Event | Since | Payload | Purpose |
|-------|-------|---------|---------|
| `unavailable` | 1 | — | Another WM is already active |
| `finished` | 1 | — | Server finished with this WM |
| `manage_start` | 1 | — | Begin manage sequence (modify window management state) |
| `render_start` | 1 | — | Begin render sequence (modify rendering state) |
| `window` | 1 | `new_id river_window_v1` | New window created |
| `output` | 1 | `new_id river_output_v1` | New output connected |
| `seat` | 1 | `new_id river_seat_v1` | New seat (keyboard/mouse) |
| `session_locked` | 1 | — | Session locked |
| `session_unlocked` | 1 | — | Session unlocked |

### Requests Sent (UWM → River)

| Request | Since | Payload | Purpose |
|---------|-------|---------|---------|
| `stop` | 1 | — | Stop receiving events |
| `destroy` | 1 | (destructor) | Destroy this object |
| `manage_finish` | 1 | — | Finish manage sequence, apply state changes |
| `manage_dirty` | 1 | — | Request a new manage sequence |
| `render_finish` | 1 | — | Finish render sequence, display changes |
| `get_shell_surface` | 1 | `wl_surface → new_id river_shell_surface_v1` | Create WM UI surface |
| `exit_session` | 4 | — | Exit entire Wayland session |

### Manage / Render Sequence Protocol

The core protocol loop:

1. **River sends** all state changes since last manage (window created, dimensions changed, closed, etc.)
2. **River sends** `manage_start`
3. **UWM** sets position, proposes dimensions, sets focus, closes windows, etc.
4. **UWM sends** `manage_finish`
5. **River applies** window management state, waits for window responses
6. **River sends** window `dimensions` events
7. **River sends** `render_start`
8. **UWM** sets rendering position, z-order, borders, visibility
9. **UWM sends** `render_finish`
10. Loop back to step 6 if dimensions change, or step 1 if new manage state needed

Two categories of state:
- **Window management state** — set during manage sequence only: `propose_dimensions`, `close`, `use_csd`, `use_ssd`, `set_tiled`, `focus_window`, `inform_*`, `fullscreen`, `set_capabilities`
- **Rendering state** — set during manage or render sequence: `river_node_v1.set_position`, `place_top/bottom/above/below`, `hide`, `show`, `set_borders`

### Error Codes

| Error | Value | Meaning |
|-------|-------|---------|
| `sequence_order` | 0 | Request violates manage/render ordering |
| `role` | 1 | wl_surface already has a role |
| `unresponsive` | 2 | Window manager unresponsive |

---

## `river_seat_v1`

**Version**: 4
**Created by**: `river_window_manager_v1.seat` event

### Events Received (River → UWM)

| Event | Since | Payload | Purpose |
|-------|-------|---------|---------|
| `removed` | 1 | — | Seat removed |
| `wl_seat` | 1 | `name: u32` | Corresponding wl_seat global name |
| `pointer_enter` | 1 | `window: river_window_v1?` | Pointer entered a window |
| `pointer_leave` | 1 | — | Pointer left the entered window |
| `window_interaction` | 1 | `window: river_window_v1` | Window was clicked/touched |
| `shell_surface_interaction` | 1 | `shell_surface` | Shell surface was clicked/touched |
| `op_delta` | 1 | `dx: int, dy: int` | Total cumulative motion since op start |
| `op_release` | 1 | — | All pointer buttons released |
| `pointer_position` | 2 | `x: int, y: int` | Current pointer position |

### Requests Sent (UWM → River)

| Request | Since | Payload | Purpose |
|---------|-------|---------|---------|
| `destroy` | 1 | (destructor) | Destroy this object |
| `focus_window` | 1 | `window: river_window_v1` | Give keyboard focus to a window |
| `focus_shell_surface` | 1 | `shell_surface` | Give keyboard focus to a shell surface |
| `clear_focus` | 1 | — | Remove keyboard focus |
| `op_start_pointer` | 1 | — | Start interactive pointer operation |
| `op_end` | 1 | — | End interactive operation |
| `get_pointer_binding` | 1 | `button: u32, modifiers: u32` → `river_pointer_binding_v1` | Create pointer binding |
| `set_xcursor_theme` | 2 | `name: string, size: uint` | Set cursor theme |
| `pointer_warp` | 3 | `x: int, y: int` | Warp pointer to position |

### Modifier Bitmask

| Bit | Value | Name | Common Name |
|-----|-------|------|-------------|
| 0 | 1 | shift | Shift |
| 2 | 4 | ctrl | Ctrl |
| 3 | 8 | mod1 | Alt |
| 5 | 32 | mod3 | — |
| 6 | 64 | mod4 | Super/Logo |
| 7 | 128 | mod5 | AltGr |

Note: The bit positions are NOT sequential. Use the correct enum values from `river_seat_v1.modifiers`.

---

## `river_window_v1`

**Version**: 4
**Created by**: `river_window_manager_v1.window` event

### Events Received (River → UWM)

| Event | Since | Payload | Purpose |
|-------|-------|---------|---------|
| `closed` | 1 | — | Window was closed by the client |
| `dimensions` | 1 | `width: int, height: int` | Window content dimensions |
| `dimensions_hint` | 1 | `min_w, min_h, max_w, max_h: int` | Preferred size constraints |
| `app_id` | 1 | `app_id: string?` | Application ID |
| `title` | 1 | `title: string?` | Window title |
| `parent` | 1 | `parent: river_window_v1?` | Parent window (for dialogs) |
| `decoration_hint` | 1 | `hint: u32` | CSD/SSD preference |
| `pointer_move_requested` | 1 | `seat: river_seat_v1` | Window requested interactive move |
| `pointer_resize_requested` | 1 | `seat, edges` | Window requested interactive resize |
| `show_window_menu_requested` | 1 | `x, y: int` | Window requested menu |
| `maximize_requested` | 1 | — | Window requested maximize |
| `unmaximize_requested` | 1 | — | Window requested unmaximize |
| `fullscreen_requested` | 1 | `output: river_output_v1?` | Window requested fullscreen |
| `exit_fullscreen_requested` | 1 | — | Window requested exit fullscreen |
| `minimize_requested` | 1 | — | Window requested minimize |
| `unreliable_pid` | 2 | `pid: int` | Unreliable PID of window creator |
| `presentation_hint` | 4 | `hint: u32` | Preferred presentation mode |
| `identifier` | 4 | `identifier: string` | Unique window identifier |

### Requests Sent (UWM → River)

| Request | Since | Payload | Purpose |
|---------|-------|---------|---------|
| `destroy` | 1 | (destructor) | Destroy this window object |
| `close` | 1 | — | Request window close |
| `get_node` | 1 | `new_id river_node_v1` | Get render list node |
| `propose_dimensions` | 1 | `width: int, height: int` | Propose window size (0 = let window decide) |
| `hide` | 1 | — | Hide window (rendering state) |
| `show` | 1 | — | Show window (rendering state) |
| `use_csd` | 1 | — | Request client-side decorations |
| `use_ssd` | 1 | — | Request server-side decorations |
| `set_borders` | 1 | `edges, width, r, g, b, a` | Compositor-drawn borders (rendering state) |
| `set_tiled` | 1 | `edges` | Inform window it is tiled |
| `inform_resize_start` | 1 | — | Inform window resize started |
| `inform_resize_end` | 1 | — | Inform window resize ended |
| `inform_fullscreen` | 1 | — | Inform window it is fullscreen |
| `inform_not_fullscreen` | 1 | — | Inform window it is not fullscreen |
| `inform_maximized` | 1 | — | Inform window it is maximized |
| `inform_unmaximized` | 1 | — | Inform window it is not maximized |
| `fullscreen` | 1 | `output: river_output_v1` | Make window fullscreen |
| `exit_fullscreen` | 1 | — | Exit fullscreen |
| `set_capabilities` | 1 | `caps: u32` | Declare supported capabilities |
| `set_clip_box` | 2 | `x, y, w, h: int` | Clip window content (rendering state) |
| `set_content_clip_box` | 3 | `x, y, w, h: int` | Clip window borders/decoration (rendering state) |
| `set_dimension_bounds` | 4 | `max_w, max_h: int` | Recommend max dimensions |

---

## `river_output_v1`

**Version**: 4
**Created by**: `river_window_manager_v1.output` event

### Events Received (River → UWM)

| Event | Since | Payload |
|-------|-------|---------|
| `removed` | 1 | — |
| `wl_output` | 1 | `name: u32` |
| `position` | 1 | `x: int, y: int` |
| `dimensions` | 1 | `width: int, height: int` |

### Requests Sent (UWM → River)

| Request | Since | Payload |
|---------|-------|---------|
| `destroy` | 1 | (destructor) |
| `set_presentation_mode` | 4 | `mode: u32` (rendering state) |

---

## `river_node_v1`

**Version**: 4
**Created by**: `river_window_v1.get_node` request

The render list node determines position and z-order.

### Requests (all rendering state)

| Request | Since | Payload |
|---------|-------|---------|
| `destroy` | 1 | (destructor) |
| `set_position` | 1 | `x: int, y: int` |
| `place_top` | 1 | — |
| `place_bottom` | 1 | — |
| `place_above` | 1 | `other: river_node_v1` |
| `place_below` | 1 | `other: river_node_v1` |

---

## `river_xkb_bindings_v1`

**Version**: 1
**Registered from**: Wayland registry global `river_xkb_bindings_v1`

This separate protocol handles keyboard bindings defined in terms of xkbcommon keysyms.

### Requests Sent (UWM → River)

| Request | Payload | Purpose |
|---------|---------|---------|
| `destroy` | (destructor) | Destroy this object |
| `get_xkb_binding` | `seat: river_seat_v1, keysym: u32, modifiers: u32` → `river_xkb_binding_v1` | Create a keyboard binding |

---

## `river_xkb_binding_v1`

**Version**: 1
**Created by**: `river_xkb_bindings_v1.get_xkb_binding`

### Events Received (River → UWM)

| Event | Payload | Purpose |
|-------|---------|---------|
| `pressed` | — | Key was pressed (triggers next manage_start) |
| `released` | — | Key was released |

### Requests Sent (UWM → River)

| Request | Purpose |
|---------|---------|
| `destroy` | (destructor) |
| `set_layout_override` | Override active xkb layout for this binding |
| `enable` | Enable binding (must be during manage sequence) |
| `disable` | Disable binding |

### Lifecycle

1. `river_xkb_bindings_v1.get_xkb_binding(seat, keysym, modifiers)` → `river_xkb_binding_v1`
2. Add listener for `pressed`/`released` events
3. Call `enable()` during a manage sequence
4. On `pressed` event: store pending action, respond to manage_start

---

## `river_pointer_binding_v1`

**Version**: 1
**Created by**: `river_seat_v1.get_pointer_binding`

### Events Received (River → UWM)

| Event | Payload | Purpose |
|-------|---------|---------|
| `pressed` | — | Pointer button was pressed |
| `released` | — | Pointer button was released |

### Requests Sent (UWM → River)

| Request | Purpose |
|---------|---------|
| `destroy` | (destructor) |
| `enable` | Enable binding (must be during manage sequence) |
| `disable` | Disable binding |

---

## Protocol Object Lifecycle

```
UWM startup:
  wl_display_connect()
  wl_display_get_registry()
  wl_registry_add_listener({global, global_remove})

  [registry.global: "river_window_manager_v1"]
  → wl_registry_bind() → river_window_manager_v1 proxy

  [registry.global: "river_xkb_bindings_v1"]
  → wl_registry_bind() → river_xkb_bindings_v1 proxy

Window created:
  river_window_manager_v1.window  →  river_window_v1 proxy
  river_window_v1.get_node()     →  river_node_v1
  UWM stores window + node in tracking list

Manage sequence:
  river_window_manager_v1.manage_start
  → UWM: propose_dimensions, set_position (via node), focus_window, binding enable
  → river_window_manager_v1.manage_finish

Render sequence:
  river_window_manager_v1.render_start
  → UWM: set_position (via node), place_top
  → river_window_manager_v1.render_finish

Window closed:
  river_window_v1.closed
  → UWM marks closed, cleans up on next manage_start
  → river_window_v1.destroy

UWM shutdown:
  river_window_manager_v1.stop
  river_window_manager_v1.destroy
  wl_display_disconnect()
```

---

## Omitted Protocols

`river_layout_manager_v3` is **not used** by UWM. The window management protocol (`river_window_manager_v1`) provides full control over window geometry without the layout protocol. River 0.4.5 may not expose this global to WM clients.

`river_shell_surface_v1` is for WM-created surfaces (bars, overlays). Not used in base UWM.

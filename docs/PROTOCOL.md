# UWM River Protocol Reference

## Overview

UWM communicates with River 0.4.x exclusively through Wayland protocol objects. This document describes every protocol interface UWM binds, its purpose, and how it is used.

Protocol knowledge comes from:
1. **`river-window-management-v1.xml`** — canonical protocol definition from the River source tree
2. **`river-layout-v3.xml`** — canonical protocol definition (copy in `protocol/river-layout-v3.xml`)
3. **River 0.4.x behavior documented at** [isaacfreund.com/docs/wayland/](https://isaacfreund.com/docs/wayland/)
4. **tinyrwm** — example River 0.4 window manager ([codeberg.org/river/tinyrwm](https://codeberg.org/river/tinyrwm))

---

## Protocols Overview

| Protocol | Interface | Version | Status | Purpose |
|----------|-----------|---------|--------|---------|
| river-window-management-v1 | `river_window_manager_v1` | 4 | **Primary** | Window lifecycle, focus, fullscreen, decorations, session management |
| river-window-management-v1 | `river_seat_v1` | 4 | **Primary** | Keyboard bindings, keyboard focus |
| river-window-management-v1 | `river_window_v1` | 4 | **Primary** | Per-window control (dimensions, position, close, borders) |
| river-window-management-v1 | `river_output_v1` | 4 | **Primary** | Per-output management (position, tags, layout) |
| river-layout-v3 | `river_layout_manager_v3` | 3 | **Optional** | Layout demand events, bulk geometry push |

---

## `river_window_manager_v1` (Primary)

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

1. **River sends** all state changes (window created, dimensions changed, etc.)
2. **River sends** `manage_start`
3. **UWM responds**: `set_position`, `propose_dimensions`, `keyboard_focus`, `close`, etc.
4. **UWM sends** `manage_finish`
5. **River applies** window management state, waits for window responses
6. **River sends** window `dimensions` events
7. **River sends** `render_start`
8. **UWM responds**: `set_position` (rendering), borders, visibility
9. **UWM sends** `render_finish`
10. Loop back to step 6 if dimensions change, or step 1 if new manage state needed

### Error Codes

| Error | Value | Meaning |
|-------|-------|---------|
| `sequence_order` | 0 | Request violates manage/render ordering |
| `role` | 1 | wl_surface already has a role |
| `unresponsive` | 2 | Window manager unresponsive |

---

## `river_seat_v1` (Primary)

**Version**: 4
**Created by**: `river_window_manager_v1.seat` event

### Events Received (River → UWM)

| Event | Since | Payload | Purpose |
|-------|-------|---------|---------|
| `binding` | 1 | `keysym: u32, modifiers: u32` | A registered keybinding was pressed |
| `cursor_image` | 1 | `image: u32` | Cursor image change |
| `keyboard_focus` | 1 | `window: river_window_v1?` | River suggests focused window (may ignore) |
| `pointer_focus` | 1 | `window: river_window_v1?, x: f64, y: f64` | Pointer entered/exited window |
| `modifiers` | 1 | `modifiers: u32` | Active modifiers changed |

### Requests Sent (UWM → River)

| Request | Since | Payload | Purpose |
|---------|-------|---------|---------|
| `destroy` | 1 | (destructor) | Destroy this object |
| `add_binding` | 1 | `keysym: u32, modifiers: u32` | Register a keybinding |
| `remove_binding` | 1 | `keysym: u32, modifiers: u32` | Remove a registered keybinding |
| `keyboard_focus` | 1 | `window: river_window_v1?` | Set keyboard focus to a window |
| `cursor_image` | 1 | `image: u32` | Set cursor image |
| `set_pointer_focus` | 1 | `window: river_window_v1?` | Set pointer focus |
| `op_start_pointer` | 1 | `window: u32` | Start interactive operation (move/resize) |

### Keybinding Details

`add_binding` registers a keybinding. Arguments:
- `keysym`: XKB keysym (e.g., `XKB_KEY_Return`, `XKB_KEY_h`)
- `modifiers`: Bitmask of modifier keys
  - bit 0: shift
  - bit 1: control
  - bit 2: alt
  - bit 3: meta (usually Super/Windows key)
  - bit 4: mod5 (usually AltGr)

When the user presses the bound combination, River sends a `binding` event with the same `keysym` and `modifiers`. UWM looks up the command associated with this binding and dispatches it.

**Important**: `add_binding` is purely about keyboard shortcuts. The WM does NOT need to register every key — only keys that trigger actions. Unregistered keys pass through to the focused window.

---

## `river_window_v1` (Primary)

**Version**: 4
**Created by**: `river_window_manager_v1.window` event

### Events Received (River → UWM)

| Event | Since | Payload | Purpose |
|-------|-------|---------|---------|
| `closed` | 1 | — | Window was closed by the client |
| `dimensions` | 1 | `width: int, height: int` | Window content dimensions changed |
| `dimensions_hint` | 1 | `min_w, min_h, max_w, max_h: int` | Window's preferred size constraints |
| `app_id` | 1 | `app_id: string?` | Application ID |
| `title` | 1 | `title: string?` | Window title |
| `parent` | 1 | `parent: river_window_v1?` | Parent window (for dialogs) |
| `decoration_hint` | 1 | `hint: u32` | CSD/SSD preference |
| `pointer_move_requested` | 1 | `seat: river_seat_v1` | Window requested interactive move |
| `pointer_resize_requested` | 1 | `seat, edges` | Window requested interactive resize |
| `fullscreen_requested` | 1 | `output: river_output_v1?` | Window requested fullscreen |
| `exit_fullscreen_requested` | 1 | — | Window requested exit fullscreen |
| `maximize_requested` | 1 | — | Window requested maximize |
| `unmaximize_requested` | 1 | — | Window requested unmaximize |
| `minimize_requested` | 1 | — | Window requested minimize |
| `show_window_menu_requested` | 1 | `x, y: int` | Window requested menu |

### Requests Sent (UWM → River)

| Request | Since | Payload | Purpose |
|---------|-------|---------|---------|
| `destroy` | 1 | (destructor) | Destroy this window object |
| `close` | 1 | — | Request window close |
| `get_node` | 1 | `new_id river_node_v1` | Get render list node (z-ordering) |
| `propose_dimensions` | 1 | `width: int, height: int` | Propose window size (0 = let window decide) |
| `hide` | 1 | — | Hide window |
| `show` | 1 | — | Show window |
| `use_csd` | 1 | — | Request client-side decorations |
| `use_ssd` | 1 | — | Request server-side decorations |
| `set_borders` | 1 | `edges, width, r, g, b, a` | Set compositor-drawn borders |
| `set_tiled` | 1 | `edges` | Inform window it is tiled |
| `inform_resize_start` | 1 | — | Inform window resize started |
| `inform_resize_end` | 1 | — | Inform window resize ended |
| `inform_fullscreen` | 1 | — | Inform window it is fullscreen |
| `inform_not_fullscreen` | 1 | — | Inform window it is not fullscreen |
| `inform_maximized` | 1 | — | Inform window it is maximized |
| `inform_unmaximized` | 1 | — | Inform window it is not maximized |
| `fullscreen` | 1 | `output: river_output_v1` | Actually make window fullscreen |
| `exit_fullscreen` | 1 | — | Exit fullscreen |
| `set_capabilities` | 1 | `caps: u32` | Declare supported capabilities |

### Window Management vs. Rendering State

**Window management state** (set during manage sequence):
- `propose_dimensions`, `close`, `use_csd`, `use_ssd`, `set_tiled`
- `inform_resize_start/end`, `inform_fullscreen/not`, `inform_maximized/unmaximized`
- `fullscreen`, `exit_fullscreen`, `set_capabilities`

**Rendering state** (set during manage or render sequence):
- `set_position` (passed through `river_node_v1.set_position` or `river_output_v1.set_position`)
- `hide`, `show`, `set_borders`

---

## `river_output_v1` (Primary)

**Version**: 4
**Created by**: `river_window_manager_v1.output` event

### Events Received (River → UWM)

| Event | Since | Payload | Purpose |
|-------|-------|---------|---------|
| `position` | 1 | `x: int, y: int` | Output position in layout |
| `dimensions` | 1 | `width: int, height: int` | Output dimensions |
| `removed` | 1 | — | Output disconnected |
| `tags` | 1 | `tags: u32` | Active tags changed |
| `focused_tags` | 1 | `tags: u32` | Focused tags changed |
| `usable_dimensions` | 1 | `x, y, w, h: int` | Usable area (excluding exclusive zone surfaces) |
| `layout` | 1 | `layout: string` | Layout namespace changed |
| `exclusive_zone` | 1 | `x, y, w, h: int` | Exclusive zone (from bar/panel) |

### Requests Sent (UWM → River)

| Request | Since | Payload | Purpose |
|---------|-------|---------|---------|
| `destroy` | 1 | (destructor) | Destroy this output object |
| `enter_tags` | 1 | `tags: u32` | Set tags to view |
| `focus_tags` | 1 | `tags: u32` | Set focused tags (for tag cycling) |
| `set_position` | 1 | `x, y: int` | Set output position in layout |
| `add_exclusive_zone` | 1 | `edges: u32, x, y, w, h: int` | Reserve space for bar |

---

## `river_layout_manager_v3` (Optional)

**Version**: 3
**Registered from**: Wayland registry global `river_layout_manager_v3`

### Events Received (River → UWM)

| Event | Since | Payload | Purpose |
|-------|-------|---------|---------|
| `layout_demand` | 3 | `serial, view_count, layout, tags: u32` | Compositor requests layout |
| `views` | 3 | `serial: u32` | View list changed for this output |
| `view_tags_changed` | 3 | `serial: u32, view: wl_surface` | View tags changed |
| `output_tags_changed` | 3 | `serial: u32, output: wl_output` | Output tags changed |
| `window_inhibited_changed` | 3 | `serial, view, inhibited: u32` | Window inhibited state changed |
| `user_command` | 3 | `command: string` | User command from riverctl |

### Requests Sent (UWM → River)

| Request | Since | Payload | Purpose |
|---------|-------|---------|---------|
| `destroy` | 3 | (destructor) | Destroy this object |
| `push_layout` | 3 | `view: wl_surface, layout: uint, x, y, w, h: int/uint` | Push geometry for a view |
| `commit` | 3 | `serial: u32` | Commit the layout push |

### Usage Notes

`river_layout_manager_v3` is OPTIONAL for River 0.4 window managers. It provides two benefits:
1. **Notification** of when to recompute layout (via `layout_demand`)
2. **Bulk geometry push** (via `push_layout` + `commit`) — more efficient than individually `propose_dimensions` for every window

Without this protocol, UWM would still function — it would use `propose_dimensions` during manage sequences to set window sizes.

The `user_command` event is **not used** by UWM for normal operation. In River 0.4, keybindings are handled through `river_seat_v1` directly. The `user_command` event exists in the layout protocol for backward compatibility with riverctl but is not the primary command path.

---

## Protocol Object Lifecycle

```
UWM startup:
  wl_display_connect()
  wl_display_get_registry()
  wl_registry_add_listener({global, global_remove})

  [registry.global: "river_window_manager_v1"]
  → wl_registry_bind() → river_window_manager_v1 proxy

  [registry.global: "river_layout_manager_v3"]  (optional)
  → wl_registry_bind() → river_layout_manager_v3 proxy

Window created:
  river_window_manager_v1.window  →  river_window_v1 proxy created
  river_window_v1.app_id          →  store app_id
  river_window_v1.title           →  store title
  UWM allocates node in pool

Manage sequence:
  river_window_manager_v1.manage_start
  → UWM sets position + dimensions + focus for all windows
  → river_window_manager_v1.manage_finish

Render sequence:
  river_window_manager_v1.render_start
  → UWM sets render position + borders for all windows
  → river_window_manager_v1.render_finish

Window closed:
  river_window_v1.closed
  → UWM frees node, removes from view list
  → river_window_v1.destroy

UWM shutdown:
  river_window_manager_v1.stop
  river_window_manager_v1.destroy
  wl_display_disconnect()
```

---

## Uncertainty Flags

The following areas require further investigation through testing against actual River 0.4.5:

1. **`river_node_v1`** — The render list node interface allows z-ordering. Exact usage pattern (z-order management) is untested.
2. **`river_shell_surface_v1`** — For WM-created surfaces (bars, overlays). Not yet implemented.
3. **Server-side decorations (`use_ssd`)** — Whether River 0.4.5 supports SSD or if CSD is the only option.
4. **Exclusive zones** — Bar integration via `river_output_v1.exclusive_zone` / `add_exclusive_zone`. Not yet implemented.
5. **Multiple seats** — Handling multiple keyboard/mouse seats. Theoretical support but untested.
6. **Interactive operations** — `op_start_pointer` for move/resize. Not yet implemented.
7. **Session lock** — `session_locked`/`session_unlocked` events. Not yet tested.
8. **Layout protocol version** — Whether `river_layout_manager_v3` is available as version 3 or different version on River 0.4.5.

# UWM Architecture

## Overview

UWM is a River 0.4.x window manager designed for maximum performance on low-end hardware. It uses a flat, index-based container tree with zero-allocation hot paths.

Unlike a traditional layout manager plugin (which only provides layout suggestions), UWM implements the full `river-window-management-v1` protocol, giving it complete control over window management policy: keybindings, focus, window position/size, decorations, and layout.

## Memory Architecture

```
                          ┌─────────────────┐
                          │  Arena Allocator │  (config parsing only)
                          │   64 KB mmap     │  → freed after init
                          └────────┬────────┘
                                   │
                          ┌────────▼────────┐
                          │  Fixed Buffer   │  (runtime)
                          │  Pre-allocated  │
                          └────────┬────────┘
                                   │
              ┌────────────────────┼────────────────────┐
              │                    │                    │
     ┌────────▼────────┐ ┌────────▼────────┐ ┌────────▼────────┐
     │   Node Pool      │ │  Workspace Pool │ │   Output Pool   │
     │  4096 × 64 B    │ │  32 × 256 B     │ │   8 × 128 B     │
     │  = 256 KB       │ │  = 8 KB         │ │  = 1 KB         │
     └─────────────────┘ └─────────────────┘ └─────────────────┘
```

### Allocation Policy

| Phase | Allocator | Duration |
|-------|-----------|----------|
| Startup | Arena (bump) | Config parse → freed |
| Runtime | FixedBufferAllocator | Process lifetime |
| Layout hot path | **None available** | Enforced by API |

## Node Model

```
Node (64 bytes, cache-line-aligned)
├── parent: u32        (4 B)
├── first_child: u32   (4 B)
├── next_sibling: u32  (4 B)
├── prev_sibling: u32  (4 B)
├── x, y, w, h: f64    (32 B)
├── workspace: u32     (4 B)
├── focus_child: u32   (4 B)
├── node_type: u8      ┐
├── split_dir: u8      ├ (3 B)
├── state: u8          ┘
└── padding: [5]u8     (5 B)
```

Total: 64 bytes (exact cache line)

### Constants

```zig
const MAX_NODES      = 4096;
const MAX_WORKSPACES = 32;
const MAX_OUTPUTS    = 8;
const MAX_FOCUS_HIST = 64;
const MAX_IPC_CLIENTS = 16;
const TAG_COUNT       = 9;
```

## Container States (Orthogonal to Type)

```
normal ──► monocle     (preserves ratios, order, focus, tab/stack state)
normal ──► floating    (detached from tiling tree)
normal ──► fullscreen  (bypasses layout, gets full output geometry)
any    ──► scratchpad  (moved to hidden workspace)
```

State transitions are O(1) node field updates. No tree reconstruction.

## Geometry Engine

Iterative, explicit stack, zero-allocation:

```
for dirty_outputs:
  for dirty_workspaces:
    stack.push(root)
    while stack not empty:
      node = stack.pop()
      compute child rects based on node_type
      push visible children
      if leaf (window): emit geometry
```

Resize ratios are stored in a parallel `ratios[MAX_NODES]f64` array to keep geometry data cache-hot separately.

## Focus System

Per-workspace ring buffer (`RingBuf(u32, 64)`):
- `push(node)` — O(1), overwrites oldest
- `last()` — O(1), most recent focus
- `prev(n)` — O(n) bounded, walk back n steps

Focus is communicated to River via `river_seat_v1.keyboard_focus()` during manage sequences.

## Keybinding System

UWM owns all keybindings. At startup, UWM registers keybindings with River via `river_seat_v1.add_binding(keysym, modifiers)`. When a bound key is pressed, River sends an event to UWM's seat handler. UWM looks up the binding in its internal key table and dispatches the corresponding command.

Keybinding configuration:
```
Key press
  ↓
River sends binding event to UWM
  ↓
UWM looks up Binding (mods + keysym)
  ↓
UWM dispatches Command (focus, spawn, toggle, etc.)
  ↓
Command handler modifies state
  ↓
On next manage/render sequence: state changes sent to River
```

## IPC Protocol

Binary frame protocol over Unix domain socket:

```
┌────┬──────┬──────┬──────┬──────────┐
│Magic│Length│Opcode│ Tag  │ Payload  │
│ 4 B │ 4 B  │ 4 B  │ 4 B  │ variable │
└────┴──────┴──────┴──────┴──────────┘
```

### Opcode Categories
- 0x0001–0x00FF: Commands
- 0x0101–0x01FF: Queries
- 0x0201–0x02FF: Events
- 0x0301–0x03FF: Subscriptions

## Event Loop (epoll-driven)

```
epoll_wait()
├── wayland fd       → wl_dispatch() → River protocol events
│                       ├── window management events (manage_start, render_start, ...)
│                       ├── seat events (binding triggered, ...)
│                       └── layout events (layout_demand, ...)
├── ipc listen fd    → accept() → add client
├── ipc client fds   → read frames → dispatch
├── eventfd          → deferred actions, timers
└── after dispatch:
    ├── if dirty_output: Geometry.run()
    ├── if dirty_workspace: push layout to River (layout protocol)
    └── broadcast IPC events
```

## Startup Sequence

1. Process init (args, signals)
2. Memory init (mmap pools, free lists)
3. Config parsing (arena, freed after)
4. Wayland connection
5. **Bind river_window_manager_v1** (primary protocol)
6. **Bind river_seat_v1** (for keybindings + focus)
7. **Bind river_layout_manager_v3** (optional, for bulk geometry push)
8. IPC socket creation
9. Event system startup (epoll)
10. Workspace init (default workspaces)
11. Layout engine startup (initial layout)
12. **Register keybindings via seat interface**
13. Event loop

## River Protocol Flow

### Window Management Protocol (primary)

```
UWM                          River (compositor)
  │                             │
  │──── wl_registry_bind ──────►│  (bind river_window_manager_v1)
  │                             │
  │◄──── window event ──────────│  (new window created)
  │◄──── seat event ────────────│  (new seat)
  │◄──── output event ──────────│  (new output)
  │◄──── manage_start ──────────│  (begin manage sequence)
  │                             │
  │──── set_position ──────────►│
  │──── propose_dimensions ────►│
  │──── seat.keyboard_focus ───►│
  │──── manage_finish ─────────►│
  │                             │
  │◄──── render_start ──────────│  (begin render sequence)
  │                             │
  │──── set_position ──────────►│  (rendering state)
  │──── render_finish ─────────►│
  │                             │
```

### Layout Protocol (optional, for bulk geometry sync)

```
  │                             │
  │◄──── layout_demand ─────────│  (compositor requests layout)
  │──── push_layout_view1 ────►│
  │──── push_layout_view2 ────►│
  │──── commit ───────────────►│
  │                             │
```

### Keybinding Flow

```
  │                             │
  │──── add_binding(Mod+Return)►│  (register at startup)
  │                             │
  │◄──── binding triggered ─────│  (user presses Mod+Return)
  │                             │
  │  [UWM looks up binding]     │
  │  [dispatches "spawn foot"]  │
  │  [fork+execve("foot")]      │
  │                             │
```

## Zero-Allocation Hot Paths

- Focus changes: O(1), no allocs
- Layout calculation: O(n), no allocs (n ≤ 128 per workspace)
- Geometry propagation: O(n), no allocs
- Command execution: O(1) dispatch, no allocs
- Workspace switch: O(1), no allocs
- Window insert/remove: O(1) pool ops, no allocs
- Resize: O(1) ratio update + dirty mark, no allocs
- **Keybinding dispatch: O(1) table lookup, no allocs**
- **Manage/render sequence: O(n) state flush, no allocs**

## Forbidden Allocation Sites

- layout/geometry functions
- focus functions
- command handlers
- event dispatch
- keybinding dispatch
- manage/render sequence handlers
- IPC frame handlers (buffers pre-allocated)

---

# River 0.4.x Integration Notes

## Non-Monolithic Architecture

River 0.4.x separates the compositor and window manager into two independent processes. They communicate exclusively through Wayland protocol objects. There is no shared memory, no D-Bus, and no other IPC channel for WM ↔ compositor communication.

## Protocol Stack

A River 0.4.x window manager binds the following Wayland globals in priority order:

| Priority | Protocol | Purpose | Required |
|----------|----------|---------|----------|
| 1 | `river_window_manager_v1` | Window lifecycle, focus, fullscreen, decorations, session management | **Yes** |
| 2 | `river_seat_v1` | Keyboard bindings, focus management | **Yes** |
| 3 | `river_output_v1` | Output geometry, workspace tags | Yes |
| 4 | `river_layout_manager_v3` | Bulk geometry push, view enumeration | Optional |

## Keybindings

In River 0.4.x, the window manager **owns** all keybindings. There is no separate keybinding daemon. The WM registers bindings with the compositor via `river_seat_v1.add_binding(keysym, modifiers)`.

When a registered key combo is pressed, River sends a **binding event** to the WM. The WM looks up the binding in its internal table and dispatches the corresponding command.

Keybinding registration happens at startup and can be updated dynamically.

### What riverctl does NOT do

- River 0.4.x does NOT route keybindings through `user_command` events for WM-managed bindings
- riverctl is a user-facing CLI tool, not a WM component
- The WM does not parse riverctl config files — it has its own binding configuration

## Window Lifecycle

1. **Window created**: River sends `window` event → UWM creates a `river_window_v1` proxy and allocates a node
2. **Manage sequence**: River sends `manage_start` → UWM sets position, dimensions, focus, decorations, tiling state
3. **Render sequence**: River sends `render_start` → UWM sets render position, borders, visibility
4. **Window resized**: River sends `dimensions` event → UWM updates node geometry, marks dirty
5. **Window closed**: River sends `closed` event → UWM frees node, removes from view list

## Tag System

River uses a 9-bit tag system (bits 0–8) for workspace/output association. Each output has a set of active tags. Windows are visible on outputs where tags overlap. The WM manages tag state through the output protocol objects.

## Layout Protocol Usage

The `river_layout_manager_v3` protocol is OPTIONAL. UWM uses it as a convenience layer for:
- Receiving `layout_demand` notifications (when River wants a layout update)
- Iterating visible views per output via `views` events
- Pushing bulk geometry via `push_layout` + `commit`

Without the layout protocol, UWM would need to individually `propose_dimensions` for every window during each manage sequence. The layout protocol batch mechanism is more efficient for bulk geometry updates.

## Differences from River pre-0.4 (river-classic)

| Aspect | river-classic (pre-0.4) | River 0.4.x |
|--------|------------------------|--------------|
| WM is | Layout manager plugin | Full window manager |
| Primary protocol | `river_layout_manager_v3` | `river_window_manager_v1` |
| Keybindings | Configured via riverctl | Owned by WM, registered via seat |
| `user_command` | Primary command input | Only in layout protocol, not for WM commands |
| Window control | Compositor manages windows | WM manages everything |
| Spawning | `riverctl spawn ...` or WM | WM owns spawning via fork/exec |
| riverctl | Required for config | Optional user tool |

## Dependencies

- **wayland-client** (libwayland-client) — Wayland protocol client library
- **River** 0.4.x — compositor (loaded at runtime via protocol globals)
- **libc** — for `fork()`/`execve()` (spawning programs)

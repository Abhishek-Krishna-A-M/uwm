# Development Roadmap

## Target: Daily-Driver River 0.4.5 Window Manager

UWM is a full River 0.4.x window manager, not a layout manager plugin. The roadmap reflects the actual River 0.4 architecture: window manager protocol first, layout protocol as optimization.

---

## Phase 1: Foundation ✓
- [x] Project scaffolding
- [x] `build.zig` + `build.zig.zon` with wayland-client dependency
- [x] `util/` data structures: Pool, RingBuf, Stack, Arena, IntrusiveList, BitSet
- [x] Node type + NodePool
- [x] Memory initialization (pools initialized in Server.init)

## Phase 2: Core Tree ✓
- [x] Workspace module (tiling root, floating root)
- [x] Output module (geometry, tag mappings)
- [x] Node tree operations: prepend, append, insertAfter, remove, reparent
- [x] Tree invariant validation (validateTree)
- [x] Focus history (FocusHistory ring buffer in Workspace)

## Phase 3: Layout Engine ✓
- [x] Geometry engine (iterative stack walk via layoutTree)
- [x] Split layout (horizontal/vertical)
- [x] Tabbed layout
- [x] Stacked layout
- [x] Monocle state
- [x] Dirty state system
- [x] Ratio storage + resize module

## Phase 4: State Machine ✓
- [x] Container state transitions (setState, toggleState)
- [x] Floating window management (floatWindow, tileWindow, toggleFloating)
- [x] Fullscreen handling (setFullscreen, toggleFullscreen)
- [x] Scratchpad workspace (Scratchpad ring buffer, scratchpadHide/show)
- [x] State orthogonality validation (validateStateTransition)

## Phase 5: Command System ✓
- [x] Command enum + dispatch table
- [x] Built-in command registration (focus, toggle, scratchpad, quit)
- [x] Parser for user command strings
- [x] `exec` handler (fork+execve for spawning programs)
- [x] Command dispatch with context pointer

## Phase 6: River 0.4 Window Management Protocol ← CURRENT
- [x] `river_window_manager_v1` C bindings (all requests + events)
- [x] `river_seat_v1` C bindings (keyboard focus, add_binding)
- [x] `river_window_v1` C bindings (dimensions, close, set_position, ...)
- [x] `river_output_v1` C bindings (position, tags)
- [x] Registry binding for all required globals
- [x] Window event handler (create river_window_v1 proxy, allocate node, BSP insertion)
- [x] Manage sequence handler (set position via river_node_v1, propose dimensions, focus)
- [x] Render sequence handler (set render position, borders, visibility)
- [x] Keybinding registration (add_binding for each configured binding)
- [x] Keybinding event handler (lookup + dispatch)
- [x] Window close handler (cleanup river_node_v1, free node)
- [x] Seat event handling
- [x] `river_layout_manager_v3` as optional secondary (bulk geometry push)
- [x] Layout demand handler (iterate leaves → push_layout → commit)

## Phase 7: Workspaces + Tags ← NEXT
- [ ] Output tag management
- [ ] Workspace tag assignment
- [ ] Window tag filtering
- [ ] `workspace_next` / `workspace_prev` commands
- [ ] `send_to_workspace` command
- [ ] Tag-based visibility

## Phase 8: Floating + Fullscreen
- [ ] Floating window positioning (mouse-driven)
- [ ] Fullscreen toggle via window management protocol
- [ ] Interactive resize
- [ ] Interactive move

## Phase 9: IPC
- [ ] Unix domain socket lifecycle
- [ ] Binary frame protocol (encode/decode)
- [ ] Command dispatch via IPC
- [ ] Query support
- [ ] Event broadcasting + subscriptions
- [ ] Sway-compatible IPC subset for bar integration
- [ ] `uwmctl` prototype

## Phase 10: Configuration
- [ ] Config file parser
- [ ] Keybinding configuration
- [ ] Window rule engine
- [ ] Default config
- [ ] Output configuration

## Phase 11: Polish
- [ ] Signal handling + graceful shutdown
- [ ] Output hotplug handling
- [ ] Error recovery
- [ ] Edge case hardening

## Phase 12: Testing & Benchmarks
- [ ] Unit tests for every module
- [ ] Integration integration with River protocol
- [ ] Benchmarks: insertion, focus, geometry, switching, IPC
- [ ] Performance regression tests

## Phase 13: Ecosystem
- [ ] Architecture doc finalization
- [ ] IPC protocol spec
- [ ] Configuration guide
- [ ] `uwmctl` full implementation
- [ ] Bar protocol integration

---

## Key Milestones

### Milestone 1: Window Tracking (Phase 6)
- UWM binds protocols, receives window events, tracks windows in node pool
- Window appears on screen with basic positioning
- **Test**: Launch UWM + River, open foot → foot is visible and managed

### Milestone 2: Basic Interaction (Phase 6)
- Keybindings work: focus movement, spawn foot
- **Test**: Super+Return spawns foot, focus moves with Super+h/j/k/l

### Milestone 3: Daily Driver (Phases 6 + 7 + 8)
- Window tracking, BSP insertion, splits, focus, workspaces, floating, fullscreen
- Basic bar communication via IPC
- **Test**: Full daily-driver workflow

### Milestone 4: Polish (Phases 9 + 10 + 11)
- Config file, IPC tools, edge case hardening
- **Test**: Multi-monitor hotplug, session restart, error recovery

---

## Feature Priority (Daily-Driver Focus)

| Priority | Feature | Required for DD | Phase |
|----------|---------|-----------------|-------|
| P0 | River protocol binding (window management) | Yes | 6 |
| P0 | Window tracking + node allocation | Yes | 6 |
| P0 | Keybinding registration + dispatch | Yes | 6 |
| P0 | Focus movement | Yes | 6 |
| P0 | BSP insertion | Yes | 6 |
| P0 | Horizontal/vertical split | Yes | 6 |
| P0 | Geometry apply | Yes | 6 |
| P1 | Workspaces + tags | Yes | 7 |
| P1 | Floating windows | Yes | 8 |
| P1 | Fullscreen | Yes | 8 |
| P2 | IPC + bar integration | Yes | 9 |
| P2 | Tabbed containers | Nice | 6 |
| P2 | Stacked containers | Nice | 6 |
| P2 | Monocle state | Nice | 6 |
| P3 | Config file | Later | 10 |
| P3 | Scratchpad | Later | 6 |
| P3 | Resize commands | Later | 6 |
| P3 | Window rules | Later | 10 |

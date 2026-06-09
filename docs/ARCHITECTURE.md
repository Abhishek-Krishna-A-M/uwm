# UWM — Building Strategy and Architecture

## Intent

UWM is a personal Wayland compositor built from **TinyWL/wlroots** with one goal:

**maximum responsiveness with the least possible resource use**.

This project is not trying to be flashy. It is not trying to be a general-purpose desktop environment. It is a focused, single-owner compositor that only includes features that directly improve daily use.

## Core Principles

UWM follows these rules:

1. **No dead code**

   * Every subsystem must justify its existence.
   * If a feature is not used, it does not belong in the core.

2. **No eye candy by default**

   * No blur.
   * No animations.
   * No shadows.
   * No rounded corners.
   * No fancy effects.

3. **Only useful visual effects**

   * Transparency.
   * Per-window opacity rules.
   * Nothing else unless there is a real workflow gain.

4. **Deterministic behavior**

   * Avoid unpredictable runtime costs.
   * Prefer fixed-size pools, intrusive lists, and predictable traversal.
   * Avoid allocations in hot paths.

5. **Event-driven only**

   * No polling loops.
   * No busy waiting.
   * No repeated scanning if an event can wake the compositor.

6. **Single-user optimization**

   * This is for personal use.
   * Features are admitted only if they improve the owner’s workflow.
   * No compatibility burden for other users.

---

## Target Feature Set

These are the features UWM should support in the first complete version:

* Workspaces
* BSP tiling
* Horizontal split
* Vertical split
* Tabbed containers
* Monocle
* Floating windows
* Fullscreen
* Resize in tiled mode
* Resize in floating mode
* Swap / reorder windows in tiled mode
* Pointer move / resize for floating windows
* Scratchpad
* Window rules
* Transparency
* Opacity rules for focused / unfocused windows
* IPC
* `uwmctl`
* `uwm-bar`
* Wallpaper management
* HDMI extend / mirror behavior
* Screen sharing support
* Optional / toggleable XWayland
* Configuration in `~/.config/uwm`

Stacked mode is intentionally not required. Monocle is enough for the workflow that would otherwise use stacking.

---

## High-Level Architecture

UWM is a compositor built on wlroots. TinyWL is the base starting point because it already gives:

* Wayland display and event loop
* Inputs and seats
* Outputs and output layout
* Rendering
* Cursor handling
* XDG shell window management
* Move / resize support

UWM adds the desktop-policy layer on top of that.

### Layer Model

```text
Applications
   ↓
UWM policy layer
   ├─ Workspaces
   ├─ BSP / tabbed / monocle / floating
   ├─ Focus and swap
   ├─ Window rules
   ├─ IPC and commands
   ├─ Opacity policy
   ├─ Output policy
   └─ Wallpaper policy
   ↓
wlroots / TinyWL base
   ├─ Input backend
   ├─ Output backend
   ├─ Rendering backend
   ├─ XDG shell
   ├─ XWayland
   └─ Screen capture primitives
   ↓
Linux / DRM / Mesa / libinput
```

---

## Memory and Data Design

UWM should be designed around static capacity and predictable memory use.

### Data Structures

Prefer:

* fixed arrays
* intrusive linked lists
* index-based references
* object pools
* small structs
* explicit ownership

Avoid:

* uncontrolled heap usage
* repeated malloc/free in hot paths
* recursive traversal where iteration is enough
* hash tables for tiny fixed sets
* complex generic abstraction in the hot path

### Recommended Pools

* window pool
* node pool
* workspace pool
* output pool
* scratchpad pool
* IPC client pool

### Recommended References

* use `wl_list` for compositor-owned object lists
* use integer IDs for public-facing references
* use pooled nodes for BSP and container structures
* keep geometry data separate from metadata where that reduces cache misses

### Allocation Policy

* startup allocations are allowed
* config parsing allocations are allowed
* runtime allocations in layout, focus, resize, and render logic should be avoided
* if a subsystem needs dynamic growth, preallocate to a sensible ceiling and fail cleanly when full

### Why this matters

The goal is not to eliminate every allocation from the entire process. The goal is to eliminate non-deterministic allocations where they affect latency.

---

## Event Loop Philosophy

The compositor should wake only when something happens.

### Event Sources

* Wayland client events
* input events
* output frame events
* output hotplug events
* IPC events
* timer events only when absolutely needed

### Rules

* no continuous polling for window state
* no background scanning of all windows on every frame
* no “check everything” loops
* no sleeping threads for compositor policy

TinyWL’s model is already the right starting point: a single event-driven main loop, with rendering and input handled by callbacks.

---

## Geometry and Layout Strategy

### Primary Layout: BSP

BSP is the primary tiling mode.

It should support:

* dynamic insertion
* horizontal split
* vertical split
* split ratio changes
* ordered window movement
* reordering / swapping in-place

### Tabbed

Tabbed containers should be a container mode, not a separate tree.

They should:

* preserve the underlying tree
* show one visible child at a time
* display window titles as tabs
* allow next / previous tab switching
* allow reordering tabs

### Monocle

Monocle should be a state, not a layout tree.

It should:

* preserve the underlying BSP structure
* preserve tab order
* preserve focus history
* preserve split ratios
* show only the active child visibly
* restore instantly when disabled

### Floating

Floating windows should be separate from the tiled tree.

Floating mode must support:

* pointer move
* pointer resize
* keyboard move / resize if desired later
* focus handling separate from tiling flow

### Fullscreen

Fullscreen should be a direct state on the window or container.

It should:

* cover the target output fully
* bypass normal tiling geometry while active
* restore previous geometry when turned off

### Resize and Swap

The layout engine must support:

* resizing in tiled mode
* resizing in floating mode
* swapping window order inside a container
* moving windows between workspaces

---

## Workspaces

Workspaces are central to the design.

### Workspace Model

Each workspace should own:

* a root BSP/container tree
* focus history
* scratchpad mapping
* output association if desired
* rules for visible windows

### Behaviors

* switch workspace
* move window to workspace
* move container to workspace
* restore last workspace
* keep workspace state independent

### Output Association

Workspaces should be able to map to outputs in both:

* extend mode
* mirror mode

---

## Output Management

UWM should understand multiple outputs directly.

### Required Behavior

* detect output add/remove
* track output dimensions and position
* assign workspaces to outputs
* support monitor hotplug
* support extend mode
* support mirror mode

### Extend Mode

Each monitor can own a different workspace or workspace range.

### Mirror Mode

One output can mirror the content of another.

This is useful for presentations and duplicate displays.

### HDMI Handling

UWM should react when HDMI output appears or disappears and reflow workspaces accordingly.

---

## Wallpaper Management

Wallpaper should not be drawn inside the compositor core.

The compositor should manage a wallpaper backend such as:

* `swaybg`
* `swww`

UWM can start, stop, or replace the backend through configuration or IPC.

This keeps the compositor clean while still giving full control.

---

## Transparency and Opacity

This is the only “visual polish” that is intentionally kept.

### Goal

Make apps like:

* foot
* fuzzel

support useful transparency without adding visual gimmicks.

### Policy

* focused windows may be fully opaque
* unfocused windows may become partially transparent
* floating windows may use a different opacity rule
* fullscreen windows may remain fully opaque

### Important

There should be no blur system, no shadow system, and no animation system attached to opacity.

Opacity is enough.

---

## XWayland

XWayland should be optional and toggleable.

### Desired Behavior

* default: off or lazy-start depending on preference
* enable on demand if an X11 app needs it
* disable if the user chooses to avoid XWayland entirely

### Design Rule

XWayland must not infect the core design.

It should be a compatibility layer, not a primary dependency.

---

## Screen Sharing

Screen sharing should be supported through the compositor stack, not through the window manager policy layer.

### Requirements

* integrate the protocols needed for screen capture
* work with portal-based screen sharing
* remain inactive unless requested by an application

Screen sharing should not wake the compositor constantly.

---

## IPC and Commands

UWM needs a small, fast IPC layer.

### Goals

* control the compositor externally
* support `uwmctl`
* support `uwm-bar`
* support future scripts or tools if needed

### Policy

* use a local Unix domain socket
* use a binary protocol if practical
* keep parsing simple
* avoid JSON on the hot path

### Command Model

Everything should be command-driven:

* keybinds
* IPC
* future tools

All should route to the same internal command dispatcher.

### Example Commands

* focus left
* focus right
* focus up
* focus down
* move window to workspace
* swap windows
* toggle floating
* toggle fullscreen
* toggle monocle
* toggle tabbed
* set opacity rule
* set wallpaper
* toggle XWayland

---

## `uwmctl`

`uwmctl` should be the command-line control tool for the compositor.

It should be able to:

* query state
* focus windows
* move windows
* switch workspaces
* toggle modes
* manage wallpaper
* control XWayland
* subscribe to events if needed

This tool should be thin and should not duplicate compositor logic.

---

## `uwm-bar`

The bar should be a separate process.

### It should display:

* workspaces
* active mode
* focused window title
* clock
* battery
* audio
* network

### It should not:

* manage windows
* calculate layout
* own policy
* poll compositor state aggressively

The bar should subscribe to compositor events.

---

## Configuration

UWM should load config from:

```text
~/.config/uwm/config
```

### Config Should Cover

* keybindings
* default layout mode
* workspace names
* opacity rules
* floating rules
* output rules
* wallpaper command
* XWayland policy
* screen sharing related toggles if needed

### Config Policy

* simple format
* parse once at startup
* keep runtime overhead low
* do not require scripting just to get basic behavior

---

## Feature Admission Policy

Every feature must answer one question:

**Does this directly improve the way I use the compositor?**

If the answer is no, do not add it.

### Rejected by default

* blur
* animations
* shadows
* rounded corners
* gradients
* heavy theme engines
* plugin systems
* general-purpose scripting
* complex compatibility layers that do not pay for themselves

### Accepted only when needed

* screen sharing
* XWayland
* output mirroring
* wallpaper backend management
* input configuration
* opacity rules

---

## Performance Policies

### CPU

* minimize wakeups
* minimize redundant work
* avoid full-tree scans where subtree updates are enough
* keep rendering work proportional to visible changes

### RAM

* use static pools where practical
* keep state compact
* avoid hidden caches that grow unbounded

### Latency

* focus changes should be direct and predictable
* resize should not cause a storm of unnecessary recalculation
* output changes should affect only the relevant outputs and workspaces

### Rendering

* render only what changed
* prefer damage tracking
* avoid overdraw when damage tracking is available
* keep opacity handling simple

---

## Damage Tracking

Damage tracking is required for performance.

### Purpose

Only redraw the portions of the screen that changed.

### What should trigger damage

* window movement
* window resize
* new window map
* window close
* opacity changes
* workspace switch
* output change
* wallpaper change
* pointer move / resize during floating drag

### What should not

* constant full-screen redraws
* polling-driven repaints
* redraws without visible state change

---

## Input Policy

### Keyboard

Keyboard bindings should be fast, direct, and command-based.

### Pointer

Pointer movement should support floating windows and resizing.

### Focus

Focus should be predictable and visible.

If a window is focused, the compositor should know it immediately.

---

## Testing Policy

UWM should be tested in layers:

1. startup and shutdown
2. output hotplug
3. input handling
4. focus movement
5. BSP layout correctness
6. floating move / resize
7. workspace movement
8. IPC commands
9. wallpaper backend control
10. XWayland toggle behavior
11. screen sharing behavior
12. opacity behavior

The first goal is not completeness.
The first goal is correctness and responsiveness.

---

## Suggested Build Order

### Phase 1

* TinyWL base cleaned up
* compositor starts
* outputs and input work
* simple fullscreen windows render

### Phase 2

* BSP tiling
* window focus
* split H/V
* resize
* swap

### Phase 3

* workspaces
* floating
* fullscreen
* monocle
* tabbed

### Phase 4

* scratchpad
* window rules
* opacity rules
* wallpaper backend

### Phase 5

* IPC
* `uwmctl`
* `uwm-bar`

### Phase 6

* XWayland toggle support
* HDMI mirror / extend polish
* screen sharing integration

---

## Final Design Goal

The final compositor should feel:

* immediate
* predictable
* clean
* easy to reason about
* free of unused machinery
* small enough to understand completely

UWM should be light as a feather because it only contains what is actually needed.

---

## Build Policy

UWM is built from TinyWL and then aggressively simplified and extended only where the user actually needs the feature.

### What TinyWL gives us

TinyWL already provides a minimal compositor foundation:

* Wayland display management
* wlroots backend startup
* outputs and output layout
* input devices and cursor handling
* xdg-shell window tracking
* frame rendering hooks
* move/resize interaction

That means UWM does **not** need to invent compositor primitives from scratch. It should reuse the wlroots model, then add only the policy layer that makes the desktop feel better.

### What UWM adds

UWM adds the personal desktop logic:

* workspaces
* BSP tiling
* split mode control
* tabbed containers
* monocle state
* floating and fullscreen policy
* swap and resize commands
* scratchpad behavior
* window placement rules
* transparency and opacity rules
* output assignment and mirror/extend behavior
* IPC and `uwmctl`
* `uwm-bar` integration
* wallpaper backend control
* optional XWayland toggle

### What UWM rejects

The compositor must stay small and predictable. Reject by default:

* blur
* shadows
* rounded corners
* animations
* fancy transitions
* gradients
* plugin systems
* scripting engines
* dead compatibility features
* anything that adds wakeups without a workflow benefit

---

## Core Data Model

UWM should keep its state compact and mostly static.

### Primary objects

* `struct uwm_window`
* `struct uwm_node`
* `struct uwm_workspace`
* `struct uwm_output`
* `struct uwm_seat`
* `struct uwm_binding`
* `struct uwm_rule`
* `struct uwm_ipc_client`

### Suggested ownership model

* outputs own workspaces
* workspaces own BSP roots and scratchpad links
* BSP nodes own either children or a leaf window
* windows own render metadata and protocol objects
* seats own bindings and focus state
* IPC clients own subscription state only

### Suggested invariants

* a window appears in exactly one place in the tiling tree or floating list
* a workspace has exactly one active layout state
* floating windows are not part of the tiled BSP tree
* monocle does not destroy tree structure
* fullscreen temporarily overrides normal placement but preserves restore state

---

## Memory Policy

The compositor must avoid surprise allocations in the hot path.

### Allowed allocations

* startup initialization
* config parsing
* first-time creation of fixed pools
* protocol object creation when a window, seat, or output appears

### Disallowed allocations in hot paths

* focus movement
* split / swap / resize logic
* frame render traversal
* damage application
* IPC command dispatch
* ordinary motion handling

### Recommended implementation

Use:

* intrusive `wl_list` links for compositor-owned object lists
* fixed pools for windows, nodes, workspaces, outputs, and scratchpad entries
* arrays for workspace tables and binding tables
* compact structs with explicit fields

Do not use heap allocation as the normal way to grow core state.

---

## Geometry Engine

Geometry is the heart of the compositor.

### Required behavior

* BSP insertion must be deterministic
* split orientation must be explicit
* workspace layout changes must update only the affected subtree
* floating geometry must be tracked separately from tiled geometry
* resize operations must be direct and predictable

### Traversal policy

Use iterative traversal where practical.
Avoid recursive tree walks if an explicit stack or parent/child walk is enough.

### Geometry outputs

For every visible window, the compositor should compute:

* x
* y
* width
* height
* visibility
* opacity
* output association

Only visible windows should contribute to drawing work.

---

## Damage Tracking Policy

Damage tracking is mandatory for performance.

### Principle

Only repaint what changed.

### Damage sources

* window commit
* window move
* window resize
* workspace switch
* split operation
* opacity change
* output change
* wallpaper change
* pointer-driven floating move

### Implementation idea

Maintain damage per output and merge dirty regions from the windows that actually changed.

If nothing changed, do not redraw the whole scene just because a frame event happened.

This is one of the most important performance decisions in the whole project.

---

## Input Policy

Input should remain simple and direct.

### Keyboard

* default bindings are declared in config
* each keybinding maps to a command
* command dispatch should be O(1) or close to it

### Pointer

* pointer move and resize are only meaningful for floating windows and interactive resize modes
* pointer binding handling should stay small and explicit

### Focus

Focus should be updated immediately and predictably.
No hidden focus rules.
No expensive search when the current focus is already known.

---

## Render Policy

Rendering should stay lean.

### Rendering rules

* render only visible surfaces
* preserve draw order
* support transparency by alpha blending only
* do not add blur or decoration effects unless they are truly needed
* prefer whole-frame simplicity over multiple layered render passes

### Transparency rules

Transparent apps such as foot and fuzzel can be handled with per-window opacity values.

Opacity is enough.
Blur is intentionally excluded.

---

## XWayland Policy

XWayland should be a compatibility option, not a default burden.

### Goal

* allow X11 apps when needed
* keep it toggleable
* keep startup behavior light

### Policy

* support lazy start if possible
* permit disabling it if the user wants a pure Wayland session
* keep it out of the hot path when not used

---

## Output Management Policy

Outputs are treated as first-class layout boundaries.

### Required behavior

* detect HDMI / display hotplug
* handle output add and remove
* support extend mode
* support mirror mode
* allow workspace assignment per output
* keep output state separate from window state

### Practical rule

When a monitor appears or disappears, only the affected outputs and workspaces should be updated.
Do not re-evaluate the entire desktop if a smaller change is enough.

---

## Screen Sharing Policy

Screen sharing is supported through compositor-side protocols and the portal/PipeWire stack.

### Goal

* enable portals and screencopy clients when needed
* keep capture support dormant unless requested
* avoid making screen sharing part of the normal render path

Screen sharing should be available, but never be a source of unnecessary overhead.

---

## IPC Policy

IPC is for control and status, not for core layout logic.

### Rules

* use a local Unix socket
* keep messages small and structured
* avoid JSON if a binary format is practical
* support commands, queries, and subscriptions
* keep the bar and `uwmctl` as thin clients

### Internal model

Everything external should map into the same command dispatcher used by keybindings.

That prevents duplicate code paths and keeps behavior consistent.

---

## Wallpaper Policy

Wallpaper should be managed by a backend process rather than by a custom compositor subsystem.

### Acceptable backends

* swaybg
* swww
* any lightweight equivalent

### Policy

* UWM starts or stops the backend
* UWM may choose the backend per output
* UWM does not become a wallpaper renderer itself

This keeps the compositor focused and reduces complexity.

---

## Build Order

The project should be built in the following order:

1. compositor boots and renders
2. outputs and input work
3. windows appear and are focused
4. BSP tiling works
5. workspace movement works
6. floating and fullscreen work
7. tabbed and monocle work
8. opacity and transparency work
9. rules and scratchpad work
10. output mirror/extend works
11. IPC and `uwmctl` work
12. `uwm-bar` works
13. XWayland toggle works
14. screen sharing works
15. wallpaper backend control works

Do not jump ahead and do not add features before the supporting infrastructure exists.

---

## Engineering Policy

### What to optimize for

* fewer wakeups
* fewer allocations
* smaller state
* simpler traversal
* fewer code paths
* better cache locality
* lower redraw cost

### What not to optimize for

* novelty
* feature count
* visual polish
* compatibility with every possible desktop workflow
* future hypothetical use cases

The project should be built for the actual workflows the owner uses every day.

# UWM: A Complete Textbook on Wayland Compositor Engineering

**Author:** UWM Project Documentation

**Audience:** Computer Science students aiming to become systems engineers, Linux graphics engineers, backend engineers, or Wayland compositor developers.

**Prerequisites:** C programming, basic operating systems concepts, data structures.

**Goal:** Mastery of UWM and the ability to independently maintain, extend, optimize, debug, and confidently discuss it in technical interviews. This textbook teaches you to read wlroots source code, understand competing compositors (Sway, River, Hyprland, tinywl), and make architectural decisions.

---

## Table of Contents

**Part 0 — Introduction**
- What is UWM?
- Project Goals
- Architecture Overview
- Repository Structure
- Learning Roadmap
- How to Read This Textbook

**Part 1 — Operating System Fundamentals**
- Processes and Memory
- Virtual Memory and Shared Memory
- File Descriptors and Signals
- Event Loops: epoll, poll, select
- The Unix Philosophy
- IPC and Sockets
- Ownership and Resource Lifetime
- Threads vs Event Loops

**Part 2 — The Linux Graphics Stack**
- DRM and KMS
- GBM and EGL
- DMA-BUF
- The Display Pipeline: CRTCs, Planes, Connectors
- libinput and libseat
- How a Frame Reaches the Monitor
- Seat Management: seatd and logind

**Part 3 — Wayland**
- The Wayland Architecture
- Objects, Globals, and the Registry
- Requests, Events, and Serials
- The Display Server
- The Wayland XML Protocol Language
- Protocol Generation
- Object Lifecycle
- Surface and Buffer Concepts

**Part 4 — wlroots**
- Why wlroots Exists
- Backend, Renderer, Allocator
- The Scene Graph
- The Seat and Input
- Output and Output Layout
- XDG Shell, Layer Shell, and Other Protocols
- Object Ownership and Lifetime in wlroots
- How UWM Uses wlroots

**Part 5 — UWM Architecture**
- Why This Architecture?
- How Modules Communicate
- Ownership and Lifetime
- Event Flow
- Startup Sequence
- Shutdown Sequence
- Window Lifecycle
- Focus Lifecycle
- Rendering Lifecycle
- Input Lifecycle

**Part 6 — Every Source File**
- main.c
- server.c
- window.c
- output.c
- input.c
- bsp.c
- workspace.c
- floating.c
- layout.c
- layer_shell.c
- idle_inhibit.c
- session_lock.c
- uwm_bar.c
- rules.c
- config.c

**Part 7 — Algorithms**
- BSP Tree Algorithm
- Workspace Switching
- Focus Management
- Floating Window Management
- Fullscreen
- Monocle Layout
- Hit Testing
- Cursor Movement
- Complexity Analysis

**Part 8 — Rendering**
- The Scene Graph
- Damage Tracking
- Frame Callbacks
- The Rendering Pipeline
- Buffer Lifecycle
- Surface Trees
- Opacity and Transforms

**Part 9 — Input**
- The Keyboard Pipeline
- The Pointer Pipeline
- The Seat and Focus
- Cursor Management
- Gestures
- Key Repeat
- Modifiers
- Keymap Management

**Part 10 — Outputs**
- Output Lifecycle
- Output Layout
- Frame Scheduling
- Mode Changes
- Scaling
- Hotplug Handling
- Multiple Monitors: Mirror and Extend
- The HDMI Script

**Part 11 — Layer Shell**
- The Protocol
- Exclusive Zones
- Layers and Z-Order
- Keyboard Focus for Layer Surfaces
- Integration with UWM

**Part 12 — Screen Sharing**
- PipeWire and WirePlumber
- xdg-desktop-portal
- Foreign Toplevel Protocol
- DMA-BUF Screen Capture
- How Browsers Capture Windows
- How OBS Works

**Part 13 — Performance**
- CPU Profiling with perf
- Flamegraphs
- Memory Profiling
- Cache Locality
- Branch Prediction
- False Sharing
- Heap Fragmentation
- Pool Allocation
- Hot Paths and Cold Paths
- Why Each Optimization in UWM Helped

**Part 14 — Debugging**
- Debugging Philosophy
- VT Switching
- Renderer Loss
- Deadlocks
- Use-After-Free
- Memory Leaks
- ASAN, Valgrind, GDB
- strace and WAYLAND_DEBUG
- How Every Major UWM Bug Was Solved

**Part 15 — Security**
- The Wayland Security Model
- Capabilities and Sandboxing
- Why Wayland is Safer than X11
- PipeWire Permissions
- Portals

**Part 16 — Future**
- Fractional Scaling
- Cursor Shape Protocol
- XDG Activation
- Text Input
- XWayland
- HDR and Color Management
- Explicit Sync
- Future Wayland Protocols

---

# Part 0: Introduction

## Learning Objectives

After completing this part, you will be able to:
- State what UWM is and is not
- Articulate the project's design goals and constraints
- Navigate the repository structure from memory
- Plan your learning path through the textbook

## What is UWM?

UWM is a Wayland compositor. A Wayland compositor is the display server for a Wayland-based Linux desktop. It owns the screen, receives input from the user, manages windows, and renders the final image that appears on your monitor.

Unlike X11, where the X server is an intermediary between clients and the display hardware, Wayland compositors talk directly to the GPU via the Linux kernel's DRM (Direct Rendering Manager) subsystem. There is no server component between the compositor and the hardware. The compositor is the server.

UWM is built on wlroots, a library that provides the core infrastructure for Wayland compositors. wlroots handles the low-level details: talking to DRM, managing libinput devices, implementing Wayland protocol objects, and providing a scene graph for rendering. UWM layers its own logic on top: the tiling algorithm, workspace management, keybindings, and custom protocols.

UWM is explicitly not trying to be Sway (an i3 clone), not trying to be River (a dynamic tiling compositor), and not trying to be Hyprland (a visually rich compositor with animations). UWM is a minimal, keyboard-driven BSP tiling compositor optimized for determinism, performance under memory pressure, and complete developer understanding.

## Project Goals

The design goals of UWM, in priority order:

1. **Minimalism.** The compositor should do one thing (tile windows) and do it well. Every feature must earn its place. If a feature can be implemented as an external tool communicating over a protocol, it should be. UBar is a separate process, not built into the compositor.

2. **Performance.** The compositor must never lag. Under extreme memory pressure (swap thrashing, OOM situations), the compositor must remain responsive. This means predictable allocation patterns, no hidden mallocs on hot paths, and careful use of static memory pools.

3. **Determinism.** Given the same sequence of events, UWM should produce the same output. Non-deterministic behavior (heap fragmentation from random allocation patterns, unpredictable garbage collection pauses, race conditions) is unacceptable.

4. **Understandability.** Every part of UWM should be understandable by a single person. No magic. No "it works but I don't know why." The codebase is designed to be read in its entirety.

5. **Extensibility via protocols.** Instead of adding features to the compositor, UWM exports custom Wayland protocols (like `uwm-bar-unstable-v1`) that external tools can use. This keeps the compositor core minimal while allowing unlimited extension.

## Architecture Overview

UWM's architecture follows a simple pattern:

```
+------------------+
|    main.c        |  Entry point, crash recovery, autostart
+------------------+
        |
        v
+------------------+
|   server.c       |  Creates all wlroots objects, protocols, input, outputs
+------------------+
   |    |    |     |
   v    v    v     v
+----+ +----+ +----+ +------+
|input| |output| |window| |layers| ... (per-protocol modules)
+----+ +----+ +----+ +------+
   |    |    |     |
   +----v----+-----+
        |
   +---------+
   |  bsp.c   |  BSP tree algorithm
   +---------+
   | workspace.c |  Virtual desktops
   +---------+
   | floating.c  |  Float/fullscreen management
   +---------+
   | layout.c    |  Layout mode toggles
   +---------+
```

The key insight: **wlroots owns the hardware interface; UWM owns the layout algorithm.** All input events flow through wlroots into UWM's input module, which dispatches to keybindings or cursor actions. Those actions modify the BSP tree, which triggers rearrangement. The scene graph (owned by wlroots) reflects the new layout, and on the next frame, the output renders the scene.

## Repository Structure

```
uwm/
├── src/              # Compositor source files (*.c)
│   ├── main.c         # Entry point, crash handlers, autostart
│   ├── server.c       # Server init/finish, protocol globals
│   ├── window.c       # XDG toplevel/popup lifecycle
│   ├── output.c       # Output (monitor) management
│   ├── input.c        # Keyboard/mouse/gesture processing
│   ├── bsp.c          # BSP tree implementation
│   ├── workspace.c    # Workspace (virtual desktop) manager
│   ├── floating.c     # Floating/fullscreen toggle
│   ├── layout.c       # Layout mode (monocle/BSP) toggles
│   ├── layer_shell.c  # wlr-layer-shell implementation
│   ├── idle_inhibit.c # Idle inhibit protocol
│   ├── session_lock.c # ext-session-lock protocol
│   ├── uwm_bar.c      # Custom bar protocol
│   ├── rules.c        # Window rules engine
│   └── config.c       # Compile-time configuration
├── include/          # Header files
│   ├── server.h
│   ├── window.h
│   ├── output.h
│   ├── input.h
│   ├── bsp.h
│   ├── workspace.h
│   ├── floating.h
│   ├── layout.h
│   ├── layer_shell.h
│   ├── idle_inhibit.h
│   ├── session_lock.h
│   ├── uwm_bar.h
│   ├── rules.h
│   └── config.h
├── protocol/         # Wayland protocol XML + generated code
│   ├── uwm-bar-unstable-v1.xml
│   ├── wlr-layer-shell-unstable-v1.xml
│   └── generated .c/.h files
├── tools/ubar/       # The UBar status bar (separate process)
├── config.def.h      # Default compile-time configuration
├── config.h          # User's compile-time configuration (auto-generated)
├── Makefile
└── docs/
    ├── ARCHITECTURE.md
    └── TEXTBOOK.md   # This document
```

## Learning Roadmap

This textbook is designed to be read in order. Each part builds on the previous one. However, if you already have strong OS and graphics knowledge, you can skip ahead:

- **If you know OS fundamentals:** Start at Part 2.
- **If you know Linux graphics:** Start at Part 3.
- **If you know Wayland:** Start at Part 4.
- **If you know wlroots:** Start at Part 5.

For each part, you should:
1. Read the theory section (WHY before HOW)
2. Read the implementation section (how UWM does it)
3. Draw the ASCII diagram from memory
4. Do the exercises
5. Answer the interview questions
6. Read the suggested external sources

## How to Read This Textbook

This is not a reference manual. It is a textbook. Every concept is introduced with:
1. **The problem** it solves
2. **Why the problem exists** (historical/technical context)
3. **Why this particular solution** was chosen (alternatives and tradeoffs)
4. **How it works** (step by step, with diagrams)
5. **How UWM implements it** (source code walkthrough)
6. **Common bugs and debugging methods**
7. **Exercises and interview questions**

You should have the UWM source code open as you read. Every reference to a function or struct should be looked up in the actual code. By the end, you should be able to navigate the codebase without looking at the file tree.

---

## Key Takeaways

- UWM is a minimal, keyboard-driven BSP tiling Wayland compositor
- Built on wlroots, which handles hardware interface
- Performance and determinism under memory pressure are primary goals
- Features are pushed to external tools via custom Wayland protocols
- Every part of UWM is designed to be fully understood by one person

---

## Exercises

1. Clone the UWM repository and build it from source. Read every file in `src/` without running the code. Write down what you think each file does.
2. Run UWM in a VM or on a spare machine. Note every behavior you observe.
3. Compare the actual behaviors to your predictions from exercise 1.
4. Draw the architecture diagram from memory. Compare it to the one in this chapter.

---

## Interview Questions

**Q:** What is the difference between a Wayland compositor and an X11 server?
**A:** An X11 server is a network-transparent intermediary between clients and hardware. A Wayland compositor is the display server itself — it talks directly to DRM, there is no intermediary, and there is no network transparency (by design).

**Q:** Why did UWM choose to push features to separate processes via protocols?
**A:** Each feature added to the compositor increases the attack surface, memory footprint, and code complexity. By using Wayland protocols, features can be implemented as separate processes that can be started, stopped, and debugged independently. The compositor remains a minimal trusted computing base.

**Q:** What are the tradeoffs of static memory pools?
**A:** Positive: O(1) allocation, predictable memory layout, no fragmentation, no malloc failures. Negative: fixed maximum capacity (must be sized correctly), wasted memory if underutilized, harder to change at runtime. UWM uses pools for BSP nodes (512) and toplevel tracking (256) because these are bounded by the number of windows (the user does not open 500 windows simultaneously).

---

# Part 1: Operating System Fundamentals

## Learning Objectives

After completing this part, you will be able to:
- Explain the process abstraction and its relationship to virtual memory
- Describe how file descriptors work at the kernel level
- Compare event loop mechanisms (poll, epoll, select)
- Articulate the tradeoffs between threads and event-driven architectures
- Trace ownership and resource lifetime in C programs
- Apply Unix philosophy principles to systems design

## 1.1 Processes and Virtual Memory

### The Problem

A computer runs many programs simultaneously, but each program must believe it has exclusive access to memory. If one program writes to address 0x1000 and another reads from address 0x1000, the second program must not see the first program's data (unless explicitly shared). This is process isolation.

### Why This Exists

Without memory protection, any program bug could corrupt any other program's memory, the kernel's memory, or the entire system. Early operating systems (MS-DOS) had no memory protection — a buggy program crashed the entire machine. Modern operating systems use virtual memory to give each process its own private address space.

### How Virtual Memory Works

```
Process A                    Process B
   |                            |
   v                            v
Page Table A               Page Table B
   |                            |
   +--------+    +-------+     +--------+
            |    |       |     |
            v    v       v     v
         Physical RAM (or swap)
```

Each process has a **page table** maintained by the MMU (Memory Management Unit). The page table maps virtual addresses to physical addresses. Process A's virtual page 0x1000 can map to physical page frame 0x500, while Process B's virtual page 0x1000 can map to physical page frame 0x800. They both think they're using the same address, but they're using different physical memory.

### The Tradeoffs

Virtual memory costs:
- **TLB misses.** The Translation Lookaside Buffer caches page table entries. When a process switches, the TLB must be flushed (or tagged), which is expensive.
- **Page table overhead.** Each process has its own page table. A process with a large address space (64-bit) can have multi-level page tables that take significant memory.
- **Context switch cost.** Switching between processes requires saving/restoring registers, flushing the TLB, and potentially refilling caches.

Virtual memory benefits:
- **Isolation.** One process cannot access another's memory.
- **Simplified memory model.** Each process has a contiguous address space, even if physical memory is fragmented.
- **Demand paging.** Pages can be loaded from disk on demand, allowing programs larger than physical RAM.
- **Shared memory.** Pages can be shared between processes (common for shared libraries like libc).

### How UWM Uses Virtual Memory

UWM is a process like any other. It has its own virtual address space. When wlroots calls `mmap()` to create a shared memory buffer for the compositor's rendering, that buffer appears in UWM's address space. When a Wayland client creates a shared memory buffer via `wl_shm`, that buffer is shared between client and compositor using file descriptors and `mmap`.

The compositor must never trust client-provided data. A malicious client could provide a shared memory buffer with invalid content; the compositor must handle this gracefully (usually, it just renders whatever is in the buffer — a corrupted frame is not a security issue for the compositor).

### Processes and fork()

UWM uses `fork()` in several places:

1. **Autostart.** `main.c`'s `spawn_cmd()` forks and executes autostart programs.
2. **Keybinding actions.** The `spawn()` action in `input.c` forks and executes the user's command.
3. **UBar's volume toggle.** The `pointer_button()` handler in `tools/ubar/src/input.c` forks to run `wpctl`.

Each fork creates a new process. The child process gets a copy of the parent's address space (with copy-on-write optimization). The child then calls `exec()` to replace itself with the target program.

**Common bug:** Forgetting to call `_exit()` in the child after `exec()` fails. If `exec()` returns, it means it failed — the child should exit immediately, not fall through to the parent's code.

---

## 1.2 Threads vs Event Loops

### The Problem

A server (like a Wayland compositor) must handle many concurrent activities: accept new clients, process input events, render frames, manage timers. It must do this without blocking — if the compositor blocks waiting for one event, it cannot process others.

### Thread-Based Architecture

The naive solution is to spawn one thread per activity:
```
Thread 1: wait for input events
Thread 2: process rendering
Thread 3: handle client requests
Thread 4: manage timers
```

**Problems with threads:**
- **Data races.** Multiple threads accessing shared state need synchronization (mutexes, atomics). Getting this wrong causes crashes (data races, torn reads/writes).
- **Deadlocks.** Thread A locks mutex 1 then waits for mutex 2; Thread B locks mutex 2 then waits for mutex 1. Neither can proceed.
- **Cache bouncing.** When multiple threads on different CPU cores access the same cache line, the cache coherence protocol invalidates the line on every access, causing massive performance degradation (up to 100x slower).
- **Complexity.** Reasoning about interleaved execution is hard. Bugs are statistically unlikely to reproduce, making them hard to debug.
- **Overhead.** Thread creation and context switching cost CPU time.

### Event Loop Architecture

An event loop solves these problems by using a **single-threaded** model:

```
while (running) {
    event = wait_for_event();  // blocks until something happens
    process(event);            // handles the event
}
```

The key insight: **there is only one thread of execution, so there are no data races.** All state is accessed sequentially. No mutexes needed for UWM's internal state.

### How Event Loops Work

The event loop uses a **multiplexing system call** — `poll()`, `epoll()`, or `select()` — to wait on multiple file descriptors simultaneously. When any FD becomes readable (or writable, or has an error), the call returns and the event loop processes the event.

```
+------------------+
|  Event Loop      |
|                  |
| fd1: keyboard    |----> process_key_event()
| fd2: client      |----> process_client_request()
| fd3: timer       |----> process_timer()
| fd4: signal      |----> process_signal()
| fd5: pipe        |----> process_monitor_data()
+------------------+
```

### poll vs epoll vs select

**select()** — Oldest, POSIX standard.
- Fixed maximum FD set size (typically 1024, via FD_SETSIZE)
- O(n) scanning: must iterate all FDs to find which are ready
- Mutates the FD set on return (must reinitialize each call)

**poll()** — POSIX standard, no fixed limit.
- O(n) scanning: must iterate all FDs to find which are ready
- No FD set mutation issue (uses events/revents flags)
- Used by Wayland's `wl_display` internally

**epoll()** — Linux-specific, scalable.
- O(1) ready set retrieval: kernel maintains interest list internally
- Can be edge-triggered (only notified on state changes) or level-triggered
- Better performance with thousands of FDs
- wlroots uses epoll internally

### Why Wayland Uses an Event Loop

A Wayland compositor is fundamentally event-driven:
- The compositor receives input events (keyboard, mouse, touch) asynchronously
- The compositor receives client requests asynchronously
- Timers need to fire at specific intervals
- Output frames need to be committed at the display's refresh rate

Using a single event loop avoids all threading complexity. UWM uses wlroots' event loop, which is built on epoll. UBar uses the Wayland client library's event loop, which uses poll() internally.

### When UWM Does Use Threads

UWM does spawn threads in one place: **UBar's data collection.** The `data.c` in `tools/ubar/src` creates three threads:
1. PulseAudio volume monitoring
2. Netlink network monitoring
3. UDEV HDMI/display monitoring

These threads communicate with the main thread via **pipes**. When a thread detects a change, it writes one byte to a pipe. The main thread's event loop polls the read end of the pipe and processes the data synchronously.

This is the correct pattern: threads perform blocking I/O (PA mainloop, netlink recv, udev recv) that cannot be integrated into the main event loop (because the libraries don't expose raw FDs). The threads communicate via pipes, which the main event loop can poll. No shared mutable state is accessed without synchronization (the audio volume globals use a mutex).

### Key Takeaway

**Event loops are simpler and safer than threads for I/O-bound servers.** Use threads only when you must block on library calls that don't expose FDs. When you must use threads, communicate via pipes, not shared mutable state.

---

## 1.3 File Descriptors

### The Problem

Processes need a way to refer to open files, sockets, pipes, and other I/O objects. The kernel needs to track which process has which object open, and what operations are allowed on each.

### What a File Descriptor Is

A file descriptor is an integer index into a per-process **file descriptor table**. Each entry in the table points to a kernel data structure representing an open file description (for regular files) or a socket/pipe/etc.

```
Process
  |
  | FD table:
  |  0 -> stdin (pipe to terminal)
  |  1 -> stdout (pipe to terminal)
  |  2 -> stderr (pipe to terminal)
  |  3 -> /dev/dri/card0 (DRM device)
  |  4 -> wayland client socket
  |  5 -> timerfd
  |  6 -> eventfd
  |  7 -> memfd (shared memory)
```

### Why FDs Matter for UWM

Almost everything in a Wayland compositor involves file descriptors:

1. **DRM device.** UWM opens `/dev/dri/card0` to talk to the GPU.
2. **libinput.** UWM opens `/dev/input/*` event devices through libinput.
3. **Wayland sockets.** UWM creates a Unix domain socket for clients to connect.
4. **Shared memory.** Clients create `memfd` and pass the FD to the compositor.
5. **Timers.** UWM uses `timerfd_create()` for periodic events.
6. **Signals.** UWM uses `signalfd()` for signal handling.
7. **Pipes.** UBar uses pipes for thread-to-main-thread communication.
8. **uinput.** UBar opens `/dev/uinput` to inject keyboard events.

### FD Ownership

Every FD is a resource that must be closed. Leaking FDs eventually causes the process to run out (ulimit -n, typically 1024 or 65536). The compositor must track every FD it opens and close it during teardown.

**Common bugs:**
- Forking without closing inherited FDs (child inherits parent's FDs; if the child is a long-lived process like a terminal, it holds the DRM FD open, preventing the compositor from reinitializing DRM)
- Not setting `O_CLOEXEC` on FDs (same problem: exec'd children inherit unwanted FDs)
- Double-close (can close another thread's newly opened FD — rare in single-threaded code)
- Using FDs after they've been closed (use-after-close)

### How UWM Handles FDs

UWM's FDs are managed by:
- **wlroots** opens/closes DRM, libinput, and Wayland socket FDs
- **UBar** opens/closes timerfd, uinput, pipe, and netlink FDs
- **The event loop** (wl_display) owns the main wakeup FDs

The golden rule: **the component that opens an FD must close it.** UWM's `server_finish()` destroys all wlroots objects, which closes their FDs. UBar's cleanup functions close their FDs.

---

## 1.4 Signals

### The Problem

The kernel needs to notify a process of exceptional conditions: segmentation faults, termination requests, timer expiration, child process exit. Interrupt-driven notification is better than polling.

### Signal Handling in UWM

UWM installs signal handlers for:
- **SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL** — Crash recovery. These are caught by `crash_handler()` in `main.c`, which uses `siglongjmp()` to return to a safe state in `main()`.
- **SIGINT, SIGTERM** — Graceful shutdown via `handle_term_signal()` in `server.c`.

### Why sigsetjmp/siglongjmp?

The problem with signal handlers is that they run in a restricted context. You cannot safely call most library functions (malloc, printf, etc.) from a signal handler because you might be interrupting those functions.

UWM's solution: install a signal handler that calls `siglongjmp()` back to the main loop. This restores the saved register state (including the stack pointer), effectively unwinding the stack to the point where `sigsetjmp()` was called. This is dangerous — it skips all cleanup code between the current execution point and the jump target — but it's better than crashing silently.

The sequence:
1. `main()` calls `sigsetjmp(env, 1)`
2. If return is 0: normal path, continue into `wl_display_run()`
3. If return is non-zero: a crash was caught, call `server_init()` again, and re-enter `wl_display_run()`
4. In the signal handler: `siglongjmp(env, 1)` restores the saved state

**Tradeoff:** This is not safe signal handling. It can leave the process in an inconsistent state. But for a Wayland compositor, crashing is worse than attempting recovery. The alternative (resetting everything) is what Sway and other compositors do anyway.

---

## 1.5 Unix Philosophy

### The Problem

How should a large system be decomposed into smaller pieces?

### The Unix Answer

Do one thing and do it well. Programs should work together. Use text streams as universal interface.

### How UWM Follows This

1. **The compositor is one thing.** UWM manages windows. It does not include a bar, a lock screen, a launcher, or a notification daemon. These are separate programs that communicate via Wayland protocols.

2. **Protocols as interfaces.** The `uwm-bar-unstable-v1` protocol is the interface between UWM and UBar. Any bar that implements this protocol (not just UBar) can work with UWM. This is better than embedding bar functionality.

3. **Fork/exec for actions.** Actions like spawning a terminal or launching a program use fork/exec, not library calls. The compositor doesn't link against terminal emulators.

4. **Shell scripts for configuration.** HDMI hotplug handling is delegated to `~/.config/custom_scripts/hdmi.sh`. The compositor doesn't need to know about `wlr-randr` or mirroring logic.

### The Tradeoffs

- **Process overhead.** Starting a new process for each action is slower than calling a library function (fork+exec takes ~1ms). For keybindings, this is acceptable.
- **Protocol complexity.** Designing a Wayland protocol requires more work than adding a function call. But the decoupling is worth it.
- **Debugging.** Debugging across process boundaries is harder. D-Bus or Wayland protocol tracing tools are needed.

---

## Exercises

1. Explain UWM's `spawn_cmd()` function. What happens if `fork()` fails? What happens if `execvp()` fails? What happens to FDs in the child process?
2. Draw the event loop of UWM. What FDs does it poll on? What events does each FD generate?
3. UWM's crash handler uses `siglongjmp`. Why is this unsafe? What could go wrong? Can you suggest a safer alternative?
4. Trace the lifecycle of a `wl_listener` in UWM. When is it initialized? When is it removed? What happens if a listener fires after its parent object is freed?

---

## Interview Questions

**Q (Junior):** What is a file descriptor?
**A:** A non-negative integer that the kernel uses to identify an open file, socket, pipe, or other I/O object within a process. Each process has a file descriptor table, and each entry points to a kernel-internal file description or object.

**Q (Mid):** Why does UWM use an event loop instead of threads?
**A:** Event loops eliminate data races and deadlocks because all state is accessed from a single thread. Threads require synchronization (mutexes, atomics) which adds complexity, performance overhead (cache bouncing), and risk of subtle bugs. UWM only uses threads when it must block on library calls that don't expose file descriptors (PulseAudio, netlink, udev) and communicates results back via pipes.

**Q (Senior):** The UBar uses pthread_detach and then sleeps 200ms hoping threads exit. What's wrong with this?
**A:** This is a resource leak disguised as cleanup. `pthread_detach` tells the system to reclaim thread resources when the thread exits, but it provides no way to wait for thread completion. The 200ms sleep is a race condition: if the threads take longer than 200ms to notice `g_monitors_running = false` and exit, they continue running after `data_stop_monitors` returns. When the process exits (or reinitializes), these threads are accessing freed memory. The fix is to store the pthread_t values and call `pthread_join` to wait for clean exit. (Note: this was fixed in the audit.)

**Q (Senior):** How would you support 1000 concurrent clients in UWM?
**A:** The main challenge is the event loop. With 1000 clients, each sending frequent commits, the compositor's event loop must process them all without starving any client. wlroots uses epoll, which handles this efficiently (O(1) ready set retrieval). The bottleneck becomes CPU: processing 1000 surface commits per frame requires walking the scene graph, computing damage, and potentially re-rendering. Scene graph optimizations (culling invisible surfaces, damage tracking, dirty flags) matter more than event loop efficiency. BSP tree operations are O(log n), so 1000 leaves add ~10 levels — still fast. Memory for 1000 toplevel structs at ~200 bytes each = 200KB, trivial. The pool size would need to increase (currently 512). The real bottleneck is rendering: rendering 1000 surfaces at 60fps requires compositing them all each frame, and GPU memory for 1000 textures may be significant.

---

## Key Takeaways

- Processes provide isolation through virtual memory. The MMU maps virtual to physical addresses.
- Event loops are simpler than threads. Use threads only for blocking library calls that don't expose FDs.
- File descriptors are the fundamental resource abstraction in Unix. Track ownership and lifetime carefully.
- The Unix philosophy (do one thing well, compose via protocols) guides UWM's architecture.
- Signal handling for crash recovery is a pragmatic tradeoff, not a safe pattern.

---

# Part 2: The Linux Graphics Stack

## Learning Objectives

After completing this part, you will be able to:
- Describe the full graphics pipeline from application to monitor
- Explain DRM, KMS, GBM, and EGL and how they relate
- Draw the data flow for a single frame from compositor to display
- Understand DMA-BUF and its role in zero-copy sharing
- Explain seat management and the role of libseat/logind

## 2.1 The Problem: Getting Pixels to a Monitor

An application wants to display a frame. The frame lives in the application's memory (a buffer). The monitor expects a stream of pixels at a fixed refresh rate (e.g., 60 Hz). How do the pixels get from the application's buffer to the monitor's cable?

The naive approach: the kernel copies data from the application's buffer to the framebuffer memory on the graphics card, which the monitor reads. This works but has problems:
- **Copy overhead.** Copying a 4K frame (8.3 MB at 32bpp) 60 times per second is 498 MB/s just for one monitor.
- **Tearing.** If the copy happens while the monitor is scanning out, the user sees a partial frame (tearing).
- **Synchronization.** The application must wait for vertical blank (vblank) before updating the buffer.

## 2.2 DRM and KMS

### Direct Rendering Manager (DRM)

DRM is the Linux kernel subsystem for graphics. It provides:
- A device file (`/dev/dri/card0`) that userspace opens for GPU access
- IOCTL calls for memory management, command submission, and mode setting
- Buffer object management (GEM — Graphics Execution Manager)

GEM is the memory manager within DRM. It handles allocation, sharing, and synchronization of GPU buffers.

### Kernel Mode Setting (KMS)

KMS is the part of DRM that configures the display pipeline. It manages:

**CRTCs (Cathode Ray Tube Controllers).** Despite the name, these are the scanout engines. Each CRTC generates a pixel stream from a framebuffer and sends it to one or more encoders. Modern terms: "display controller" or "scanout engine."

**Planes.** Hardware layers that can be composited by the display hardware. A typical GPU has at least one primary plane (for the main framebuffer), plus optional overlay planes (for cursor, video playback) and cursor planes. Planes can have different pixel formats, positions, and alpha blending.

**Connectors.** Physical or virtual ports where monitors are attached (HDMI, DisplayPort, eDP for laptops, etc.). Connectors report the capabilities of the attached display (EDID — Extended Display Identification Data).

**Encoders.** Bridge between CRTCs and connectors. An encoder converts the CRTC's pixel stream into the format required by the connector (e.g., TMDS for HDMI).

**Modes.** Display timing parameters (resolution, refresh rate, timing intervals). Each connector supports a set of modes (from EDID or user-specified).

```
                +----------+
                | Framebuf |
                +----+-----+
                     |
                     v
                +---------+
                |  CRTC   |   (scanout engine)
                +----+----+
                     |
                     v
                +---------+
                | Encoder |   (converts pixel stream)
                +----+----+
                     |
                     v
                +----------+
                | Connector|   (HDMI, DP, etc.)
                +----+-----+
                     |
                     v
                +----------+
                |  Monitor |
                +----------+
```

### The KMS API Flow

```
1. Open /dev/dri/card0
2. Get resources: drmModeGetResources() -> list of CRTCs, connectors, encoders
3. Get connector info: drmModeGetConnector() -> modes, connection status
4. Create framebuffer: drmModeAddFB() or drmModeAddFB2() (for multi-planar formats)
5. Set mode: drmModeSetCrtc(crtc_id, fb_id, connector_id, mode)
6. Page flip: drmModePageFlip(crtc_id, fb_id, flags, user_data)
   - Atomic: drmModeAtomicCommit() with DRM_MODE_ATOMIC_ALLOW_MODESET
```

### Atomic Modesetting

The legacy KMS API (drmModeSetCrtc, drmModePageFlip) applies changes one at a time. This can cause intermediate states where the display is partially configured. Atomic modesetting (DRM_MODE_ATOMIC_ALLOW_MODESET) applies a set of changes atomically — either all succeed or none do.

### How wlroots Uses DRM

wlroots opens the DRM device through its backend. The backend:
1. Discovers GPU devices (primary GPU for rendering, secondary for offloading)
2. Opens `/dev/dri/card0` (or whatever the primary GPU is)
3. Calls KMS to set up initial mode
4. Creates framebuffers via GBM (see below)
5. Handles page flips via atomic commits
6. Manages hotplug events via udev (connector plugged/unplugged changes)

UWM never directly calls DRM IOCTLs. wlroots abstracts all of this behind `wlr_output` and `wlr_backend`.

---

## 2.3 GBM and EGL

### GBM (Generic Buffer Manager)

GBM is a userspace API for allocating buffers that can be used with both the GPU (for rendering) and KMS (for scanout). It provides:
- Buffer allocation (width, height, format)
- Buffer sharing via DMA-BUF (file descriptors)
- Buffer import from other processes

### Why GBM Exists

The problem: the compositor needs buffers that the GPU can render into (via OpenGL/EGL) and that KMS can scan out. These must be the same physical buffer (or at least the same memory) to avoid copies.

GBM bridges the two worlds:
- GBM allocates a buffer from the GPU's memory manager (GEM)
- The compositor imports the buffer into EGL for OpenGL rendering
- The compositor passes the same buffer to KMS for scanout

```
+----------+       +----------+       +---------+
| EGL/GL   | ----> | GBM buf  | ----> | KMS fb  |
| renders  |       | (GEM)    |       | (scan)  |
| into     |       |          |       |         |
+----------+       +----------+       +---------+
```

### EGL (Embedded-System Graphics Library)

EGL is the interface between OpenGL (or OpenGL ES) and the underlying native window system. In the context of a Wayland compositor, EGL provides:
- Creating an OpenGL context bound to a GBM surface
- Making the context current (which thread can issue GL commands)
- Swapping buffers (eglSwapBuffers) to present a rendered frame

### How UWM Uses GBM/EGL

UWM calls:
```
wlr_renderer_autocreate(backend)   // Creates a wlr_renderer backed by EGL/GLES2
wlr_allocator_autocreate(backend, renderer)  // Creates a wlr_allocator backed by GBM
```

The renderer provides `wlr_renderer_begin()` and `wlr_renderer_end()` for rendering frames. The allocator provides `wlr_allocator_create_buffer()` for allocating renderable buffers. UWM never directly calls EGL or GBM functions — wlroots handles this.

---

## 2.4 DMA-BUF

### The Problem

Buffers need to be shared between:
- The GPU driver (for rendering)
- KMS (for scanout)
- Other processes (screen sharing via PipeWire)
- Other drivers (e.g., a video encoder on a different GPU)

Each of these might be in different kernel subsystems or even different devices. Copying between them is expensive.

### The Solution: DMA-BUF

A DMA-BUF is a buffer that can be shared across kernel subsystems and processes via file descriptors. The key insight: **a DMA-BUF is a file descriptor that points to a buffer in physical memory that DMA-capable devices can access.**

```
Process A creates GEM buffer
       |
       v
dma_buf fd = drmPrimeHandleToFD(gem_handle)
       |
       v
Send fd via Unix domain socket to Process B
       |
       v
Process B imports: gem_handle = drmPrimeFDToHandle(fd)
       |
       v
Process B can now render to / read from the same physical memory
```

### DMA-BUF in Screen Sharing

When PipeWire captures a window, the compositor exports a DMA-BUF FD for each frame's buffer. PipeWire imports the FD and can encode the frame (for OBS) or display it (for a browser WebRTC call). No data is copied — the encoder reads directly from the GPU's memory.

### DMA-BUF in UWM

UWM creates DMA-BUFs for screen capture via:
```
wlr_export_dmabuf_manager_v1_create(display)   // Enables dma-buf export
wlr_linux_dmabuf_v1_create_with_renderer(display, renderer)  // Enables linux-dmabuf protocol
```

The ext-image-capture-capture-source protocol (used by xdg-desktop-portal) captures frames as DMA-BUFs:
```
handle_new_ext_image_capture_session() -> wlr_ext_image_capture_source_v1
```

When a frame is committed, UWM sends the DMA-BUF to the capture session via `wlr_ext_image_capture_source_v1_send_frame()`.

---

## 2.5 libinput and libseat

### libinput

libinput is the input device handling library. It:
- Opens `/dev/input/event*` devices (keyboards, mice, touchpads, touchscreens)
- Processes raw evdev events from the kernel
- Provides a high-level API: "pointer moved by (dx, dy)", "key pressed with keycode X", "touchpad gesture pinch"
- Handles device quirks (e.g., trackpoint sensitivity, keyboard layout detection via udev)
- Provides configuration: tap-to-click, natural scrolling, pointer acceleration, disable-while-typing

```
Kernel evdev (/dev/input/event0, event1, ...)
    |
    v
libinput (processes raw events -> high-level events)
    |
    v
wlroots backend (receives libinput events)
    |
    v
wlr_seat (dispatches to keyboard/pointer/touch)
    |
    v
UWM (server_cursor_motion, server_cursor_button, etc.)
```

### libseat and seatd

**A "seat"** is the set of input and output devices assigned to a user. At the Linux console, there is typically one seat. In a multi-user setup, each user has their own seat.

**The problem:** The kernel needs to arbitrate access to DRM and evdev devices. Only one process should have access to the GPU at a time. Traditionally, this was done by:
1. Running as root and dropping privileges
2. Using logind (systemd-logind) to manage sessions and device access

**logind** (part of systemd) provides session management. When you log in, logind creates a session and gives your compositor access to the DRM device and input devices. When you switch VTs (Ctrl+Alt+F2), logind revokes access.

**seatd** is a standalone seat management daemon for systems that don't use systemd. It provides the same functionality: device arbitration, session management, VT switching.

**libseat** is the library that compositors use to talk to either logind or seatd. It provides a uniform API:
```
struct wlr_session *session = wlr_session_create(backend);
// session->active = true when we have device access
```

### How UWM Uses Seats

UWM creates a session in `server_init()`:
```
struct wlr_session *session = wlr_session_create(backend);
```

The session's `active` signal fires when the session is activated (user switches to this VT) or deactivated (user switches away). UWM handles this in `handle_session_active()`:
- On deactivate: stop rendering
- On reactivate: schedule frames on all outputs

VT switching is handled in `input.c`:
```
if (keysym >= XKB_KEY_XF86Switch_VT_1 && keysym <= XKB_KEY_XF86Switch_VT_12) {
    wlr_session_change_vt(server.session, keysym - XKB_KEY_XF86Switch_VT_1 + 1);
}
```

---

## 2.6 How a Frame Reaches the Monitor — Full Pipeline

```
Application (e.g., Firefox)
    |
    | (1) Creates wl_surface, attaches wl_buffer (SHM or DMA-BUF)
    | (2) wl_surface_commit()
    v
Compositor (UWM)
    |
    | (3) xdg_toplevel_commit handler
    | (4) Scene graph node updated with new buffer
    | (5) Damage region computed
    v
wlr_scene
    |
    | (6) Scene graph walks tree, composites surfaces
    | (7) Renders to output buffer via EGL/GLES2
    v
wlr_renderer (GLES2)
    |
    | (8) OpenGL renders scene into GBM buffer
    v
GBM buffer
    |
    | (9) Buffer passed to DRM/KMS for scanout
    v
DRM/KMS
    |
    | (10) drmModeAtomicCommit or drmModePageFlip
    | (11) Hardware scans out buffer to encoder
    v
GPU Display Hardware
    |
    | (12) Encoder sends pixel data via HDMI/DP
    v
Monitor
    |
    | (13) Monitor displays pixels at refresh rate
    v
    You see the frame
```

### Step-by-Step Detail

**Step 1-2: Client commits a surface.**
The client has rendered its frame (via Cairo, Skia, WebRender, etc.) into a buffer. Buffers can be:
- **Shared memory (wl_shm):** A buffer in system RAM, shared via memfd. The compositor reads it via CPU. This is slow because the compositor must copy the data to the GPU. Used by simple clients (terminals, simple toolkits).
- **DMA-BUF (wl_linux_dmabuf):** A buffer in GPU memory. The compositor imports it directly as a GL texture. Zero-copy. Used by complex clients (Firefox, Chromium, GTK4, Qt6).

The client calls `wl_surface_attach(buffer)` then `wl_surface_commit()` to tell the compositor "this buffer is ready."

**Step 3-5: Compositor processes commit.**
UWM's `xdg_toplevel_commit` callback receives the event. The scene graph node for this surface is updated. A damage region is computed: which parts of the output changed.

**Step 6-8: Scene graph renders.**
wlroots' scene graph walks the tree of all visible surfaces. For each surface, it computes where on the output it should appear (considering transforms, scales, and positions). It then renders the scene into an offscreen buffer using GLES2. The renderer uses the current output's buffer (a GBM buffer allocated for this output).

**Step 9-11: Page flip.**
When rendering is complete, wlroots calls KMS to flip the framebuffer. The CRTC starts scanning out from the newly rendered buffer on the next vertical blank (vblank) interval.

**Step 12-13: Display.**
The encoder sends the pixel stream over the physical connection (HDMI, DisplayPort, USB-C). The monitor's scaler/timing controller converts the signal to the panel's native resolution and refresh rate.

---

## Exercises

1. Open your monitor's settings and check its refresh rate and resolution. Calculate how many bytes per second the GPU must push to drive it.
2. Run `sudo cat /sys/kernel/debug/dri/0/state` to see the current KMS atomic state.
3. Install `drm_info` and inspect your GPU's CRTCs, planes, and connectors.
4. Trace the full pipeline for a keystroke in a terminal: input event -> compositor -> frame render -> monitor.
5. Why does the compositor need to handle vblank? What happens if the compositor renders faster than the monitor's refresh rate? What if it renders slower?

---

## Interview Questions

**Q (Junior):** What is DRM?
**A:** Direct Rendering Manager. A Linux kernel subsystem that provides an interface to GPUs. It handles buffer management (GEM), command submission, and display configuration (KMS).

**Q (Mid):** What is a CRTC?
**A:** A CRTC (Cathode Ray Tube Controller) is a display controller within the GPU that generates a pixel stream from a framebuffer and sends it to a connector. In modern terms, it's a scanout engine. Each CRTC can drive one display independently. A GPU typically has 1-6 CRTCs.

**Q (Mid):** What's the difference between a primary plane and a cursor plane?
**A:** A primary plane is the main framebuffer for a CRTC — the display's primary content. A cursor plane is a small, separate hardware layer for the mouse cursor. The GPU composites the cursor over the primary plane without needing to re-render the entire frame, which saves GPU bandwidth when only the cursor moves.

**Q (Senior):** Why did wlroots switch from the legacy KMS API to atomic modesetting?
**A:** The legacy API requires multiple IOCTL calls to change the display configuration (set CRTC mode, set plane position, set connector state). Each call can fail independently, leaving the display in an inconsistent state. Atomic modesetting applies all changes as a single transaction: either all succeed or none do. This is critical for hotplug handling (a monitor disconnects while you're reconfiguring) and for complex configurations (YAML split, multi-monitor with different timings).

**Q (Senior):** Describe how triple buffering works in a Wayland compositor.
**A:** The compositor maintains three buffers per output: one being scanned out by KMS (front buffer), one being rendered into (back buffer), and one ready to be queued for scanout (middle buffer). This prevents the "buffer is busy" problem where the compositor must wait for vblank to start rendering the next frame. With triple buffering, the compositor can always start rendering immediately. However, it adds one frame of latency (the content rendered is displayed two frames later). UWM uses triple buffering in its `pool_buffer` mechanism (3 buffers per output).

---

# Part 3: Wayland

## Learning Objectives

After completing this part, you will be able to:
- Explain the Wayland architecture and why it exists
- Trace a Wayland protocol message from client to compositor
- Read and write Wayland protocol XML files
- Understand object lifecycle in Wayland
- Explain why Wayland has no X server

## 3.1 Why Wayland Exists

### The X11 Problem

X11 was designed in 1984. Its architecture:

```
Application -> [Xlib/XCB] -> X Server -> [DDX] -> Hardware
                  ^
                  |
            Window Manager
                  ^
                  |
            Compositor (optional, like Compiz)
```

The X server is a network-transparent display server. It manages windows, handles input, and provides rendering via the X protocol. A separate window manager controls window placement and decorations. A compositor (like Compiz, Mutter, or KWin) provides visual effects and compositing.

Problems with this architecture:
1. **Network transparency is expensive.** Every drawing command goes through the X protocol, which adds serialization, parsing, and round-trips. Modern graphics are not network-transparent — they're rendered locally with GPU acceleration.
2. **The X server has too much responsibility.** It must be a window manager, a compositor, a font renderer, a network server, and a hardware driver. Each of these is complex and the X server must do all of them.
3. **Race conditions in the protocol.** The X protocol has fundamental design flaws that make features like smooth resize and tear-free video difficult to implement correctly.
4. **Double compositing.** The compositor (Mutter/KWin) renders the desktop scene, then the X server (which doesn't know about compositing) does its own compositing. This wastes GPU bandwidth and adds latency.
5. **Security.** X11 has no concept of window isolation. Any client can read the contents of any other window, inject input events, and monitor all keystrokes.

### The Wayland Solution

Wayland eliminates the X server. The compositor is the display server. It talks directly to the GPU via DRM/KMS and receives input from libinput. Clients render their own frames and tell the compositor "here is a buffer, display it at this position."

```
Application -> [Wayland client library] -> Compositor -> [DRM/KMS] -> Hardware
                                              ^
                                              |
                                         (No X server)
```

Key differences:
- **No network transparency.** Wayland is designed for local display only. Remote display is delegated to PipeWire/VNC/RDP.
- **Every frame is complete.** The client renders a full frame (via OpenGL, Vulkan, or Cairo) and passes the resulting buffer to the compositor. The compositor composites the buffers from all clients and displays the final scene.
- **No double compositing.** There is one compositor. It composites client buffers directly into the output buffer.
- **Security by design.** Clients cannot see each other's buffers. Input events are sent only to the focused client. No global keylogger.

### The Tradeoffs

Wayland's approach has disadvantages:
- **Every client is a compositor.** Each client must render its own title bar, decorations, and drop shadows (or delegate this via server-side decorations). X11 clients could rely on the window manager for decorations.
- **No global shortcuts.** An application cannot listen for global keybindings (this was a security risk in X11). The compositor must forward specific key events.
- **Screenshotting requires protocols.** There is no global "take a screenshot" mechanism. The compositor must implement the screencopy protocol (or the client uses PipeWire).
- **Clipboard is more complex.** The clipboard is managed via Wayland protocols (data-device, primary-selection). There are more protocol round-trips.

---

## 3.2 The Wayland Protocol Model

### Objects

Everything in Wayland is an **object**. Objects have:
- An **ID** (a 32-bit integer, assigned by the compositor when created)
- An **interface** (a set of requests and events, defined in XML)

Example objects: `wl_display`, `wl_compositor`, `wl_surface`, `wl_buffer`, `wl_seat`, `wl_pointer`, `wl_keyboard`, `wl_output`.

### The Display Object

The first object is always `wl_display` with ID 1. It is created by the protocol itself (not by a request). The client never assigns IDs — the compositor does (for objects the client creates via requests).

### Requests and Events

- **Request:** Client calls a function on an object. Functions are defined in the protocol interface. Example: `wl_surface_attach(surface, buffer, x, y)`.
- **Event:** Compositor sends a notification to the client. Example: `wl_pointer_enter(pointer, serial, surface, surface_x, surface_y)`.

### The Registry

The client discovers available global objects via `wl_display.get_registry()`. The compositor responds with `wl_registry.global(name, interface, version)` events for each global. The client calls `wl_registry.bind(name, interface, version, id)` to create a client-side proxy for the global.

```
Client                          Compositor
  |                                 |
  |-- wl_display.get_registry() -->|
  |                                 |
  |<- wl_registry.global(1,        |
  |    "wl_compositor", 4) --------|
  |                                 |
  |<- wl_registry.global(2,        |
  |    "wl_seat", 7) -------------|
  |                                 |
  |-- wl_registry.bind(1,          |
  |    "wl_compositor", 4, id=3)->|
  |                                 |
  |-- wl_compositor.create_surface( |
  |    id=4) --------------------->|
```

This is why startup time includes "Wayland connection setup" — the client must round-trip to discover globals and bind them before it can create surfaces.

---

## 3.3 Serials

### The Problem

Permissions in Wayland need to be tied to user actions. For example, a client should only be able to create a popup menu in response to a user click, not arbitrarily. Similarly, a clipboard owner should only be able to offer selection data when the user explicitly requests it.

### The Solution: Serials

A **serial** is a monotonically increasing 32-bit integer generated by the compositor for each input event. The compositor sends the serial along with the event (e.g., `wl_pointer.button(serial, time, button, state)`). The client must provide this serial back to the compositor when requesting privileged operations.

```c
// Client receives pointer button event with serial 42
// Client creates a popup:
xdg_surface_get_popup(serial=42, ...);
// Compositor checks: is serial 42 valid? Yes, it was a real button press.
// If the client tried serial=0 or an old serial, the compositor rejects.
```

### Why Serials Exist

Without serials, a client could:
- Spawn popup menus at any time (annoying)
- Claim clipboard ownership without user action (security)
- Start drag-and-drop without user action (security)

Serials provide a simple, decentralized authorization mechanism. The compositor doesn't need to track per-client state — it just checks that the serial is recent and was generated for a real user interaction.

### Serial Lifecycle

1. The compositor generates serial `N` when it processes an input event.
2. The compositor sends serial `N` to the relevant client(s) in the event.
3. The compositor stores `N` in its seat state (`wlr_seat.serial`).
4. When a client makes a request requiring a serial, the compositor checks if the serial matches.
5. Serials expire after a timeout (typically a few seconds) to prevent replay attacks.
6. The compositor increments `wlr_seat.latest_client_serial` or `wlr_seat.latest_server_serial` as appropriate.

### wlr_seat Serial Functions

- `wlr_seat_client_next_serial(seat)` — Generates a new serial for sent events
- `wlr_seat_validate(seat, serial)` — Checks if serial is valid for current client

---

## 3.4 The Wayland Protocol XML Language

### Why XML?

Wayland protocols are specified in XML files. These files:
- Define the interface name, version, and object type
- List requests (client -> compositor) with arguments
- List events (compositor -> client) with arguments
- Specify enumerations and bitfields

Using XML allows code generation for both client-side and server-side stubs in multiple languages (C, Rust, Haskell, etc.).

### Example: wl_surface Interface

```xml
<interface name="wl_surface" version="4">
  <request name="attach">
    <arg name="buffer" type="object" interface="wl_buffer" allow-null="true"/>
    <arg name="x" type="int"/>
    <arg name="y" type="int"/>
  </request>
  <request name="damage">
    <arg name="x" type="int"/>
    <arg name="y" type="int"/>
    <arg name="width" type="int"/>
    <arg name="height" type="int"/>
  </request>
  <request name="commit"/>
  <event name="enter">
    <arg name="output" type="object" interface="wl_output"/>
  </event>
  <event name="leave">
    <arg name="output" type="object" interface="wl_output"/>
  </event>
  <enum name="error">
    <entry name="invalid_scale" value="0"/>
    <entry name="invalid_transform" value="1"/>
  </enum>
</interface>
```

### Code Generation

The compositor's build process runs `wayland-scanner` (a tool from the wayland-protocols package or included in wayland) that:
1. Reads the XML file
2. Generates a header file with struct definitions and function prototypes
3. Generates a C source file with marshalling/demarshalling code

For the client side:
```
wayland-scanner client-header < protocol.xml > protocol.h
wayland-scanner private-code < protocol.xml > protocol.c
```

For the server side:
```
wayland-scanner server-header < protocol.xml > protocol.h
wayland-scanner private-code < protocol.xml > protocol.c
```

### Wayland Protocol Objects in wlroots

wlroots implements the server side of many Wayland protocols. When you see:
```c
server.xdg_shell = wlr_xdg_shell_create(server.wl_display);
```

This creates a wl_global for the `xdg_wm_base` interface. When a client binds to this global, wlroots creates the appropriate server-side objects and handles the protocol automatically.

For custom protocols (like `uwm-bar-unstable-v1`), UWM generates the server code manually and implements the bind/setup handlers.

---

## 3.5 UWM's Custom Protocol: uwm-bar-unstable-v1

### The Problem

UWM needs to tell the bar (UBar) about workspace state changes, focused window titles, and output changes. This could be done via:
1. **Embedding the bar in the compositor.** This would violate the Unix philosophy and increase the compositor's attack surface.
2. **Writing to a file/pipe.** Fragile, no synchronization, no serialization.
3. **A Wayland protocol.** The compositor defines the interface; the bar implements the client side.

### The Protocol Design

```xml
<interface name="zwp_uwm_bar_v1" version="1">
  <request name="get_workspace_group">
    <arg name="id" type="uint"/>
    <arg name="output" type="object" interface="wl_output"/>
  </request>
  <request name="destroy" type="destructor"/>
</interface>

<interface name="zwp_uwm_workspace_group_v1" version="1">
  <event name="workspace">
    <arg name="id" type="uint"/>
    <arg name="active" type="uint"/>
    <arg name="occupied" type="uint"/>
  </event>
  <event name="focused_title">
    <arg name="title" type="string"/>
  </event>
  <event name="done"/>
  <request name="destroy" type="destructor"/>
</interface>
```

The client (UBar):
1. Binds to `zwp_uwm_bar_v1`
2. Receives a `wl_output` from the layer-shell setup
3. Calls `get_workspace_group(output)` → receives a `zwp_uwm_workspace_group_v1` object
4. Receives `workspace` events (9 total, for each workspace's active/occupied state)
5. Receives `focused_title` events
6. Receives `done` — indicating the batch of events is complete
7. On state change, receives another batch of events

### Why This Design

The protocol is batch-oriented (`done` event signals completion) rather than event-per-change. This prevents the bar from re-rendering after every small change. The compositor accumulates changes and sends them all at once, then signals `done`. The bar re-renders once on `done`.

---

## Exercises

1. Set `WAYLAND_DEBUG=1` and run a Wayland client (like `gtk4-demo`). Read the protocol dump. Identify: object creation, globals, requests, events, serials.
2. Read the `uwm-bar-unstable-v1.xml` file. Trace how a workspace state change in UWM reaches UBar.
3. Why does `wl_display_roundtrip()` block? What does it achieve?
4. What happens if a client never calls `wl_display_roundtrip()` after binding globals?
5. Design a simple Wayland protocol for a screenshot utility. What events/requests would it need?

---

## Interview Questions

**Q (Junior):** What is the difference between a request and an event in Wayland?
**A:** A request is a call from the client to the compositor (e.g., "attach this buffer to my surface"). An event is a call from the compositor to the client (e.g., "the pointer entered your surface").

**Q (Mid):** What is a Wayland serial and why does it exist?
**A:** A serial is a monotonically increasing number generated by the compositor for each input event. Clients must provide the serial when making privileged requests (creating popups, setting clipboard). This prevents clients from performing actions without user intent. It's a lightweight authorization mechanism.

**Q (Senior):** How would you design a Wayland protocol for virtual keyboard input?
**A:** The protocol would need:
- `zwp_virtual_keyboard_v1` interface:
  - `keymap(fd, size)` — Send keymap file
  - `key(time, keycode, state)` — Send key press/release
  - `modifiers(mods_depressed, mods_latched, mods_locked, group)` — Send modifier state
- The compositor checks that the client has the `virtual-keyboard` capability (or is a trusted input method)
- The compositor feeds the events into the seat's keyboard as if they came from a physical keyboard
- This is used by on-screen keyboards (like wvkbd) and remote desktop clients

**Q (Principal):** Wayland has no `get_property` request — clients cannot query object state. Why?
**A:** This is by design. Wayland protocols are asynchronous. Adding round-trip queries would create latency (each query requires a wait for the compositor's response). Instead, the compositor pushes state changes to clients via events. Clients must maintain their own state and update it when events arrive. This eliminates round-trips and keeps the compositor responsive. The downside is that clients are always slightly behind the compositor's state (lagging by one event). This is acceptable because clients don't need real-time perfect state — they just need to present the current frame, and the compositor will composite it correctly.

---

# Part 4: wlroots

## Learning Objectives

After completing this part, you will be able to:
- Explain why wlroots exists and what problem it solves
- Describe each wlroots subsystem used by UWM
- Understand wlroots' object ownership model
- Trace how a wlroots signal propagates to UWM

## 4.1 Why wlroots Exists

### The Problem

Writing a Wayland compositor from scratch requires:
- Implementing the Wayland protocol (dispatching, object management, serials)
- Writing a DRM/KMS backend (handling all GPU vendor quirks)
- Writing an input backend (handling all input device quirks)
- Implementing all Wayland protocol extensions (xdg-shell, layer-shell, etc.)
- Rendering (OpenGL/EGL setup, shader management, buffer handling)
- Session management (logind, seatd, VT switching)

This is an enormous amount of work. Before wlroots (circa 2017), compositor developers had to either:
- Use a full desktop environment toolkit (like Mutter for GNOME or KWin for KDE) — these are tightly coupled to their desktop environments
- Write everything from scratch (like Sway did originally, before wlroots was extracted)

Sway was originally a standalone compositor with its own DRM backend, libinput integration, and Wayland protocol implementations. The developers extracted the common infrastructure into wlroots, allowing other compositors to reuse it. River, Wayfire, and many others are built on wlroots.

### What wlroots Provides

```
+------------------------------------------+
|              UWM (layout logic)           |
+------------------------------------------+
|  wlroots (DRM, input, Wayland, scene)     |
+------------------------------------------+
|  Linux Kernel (DRM, evdev, memfd, etc.)   |
+------------------------------------------+
```

- **Backend:** Abstracts GPU devices, DRM/KMS, libinput, session management
- **Renderer:** OpenGL/GLES2 rendering abstraction, shader management
- **Allocator:** Buffer allocation (GBM, DMA-BUF)
- **Scene Graph:** Composite tree for all visible surfaces
- **Seat:** Input event routing, focus, serial management
- **Output:** Display output abstraction, frame scheduling
- **Protocol implementations:** xdg-shell, layer-shell, data-device, idle-inhibit, screencopy, foreign-toplevel, etc.

### The Cost of wlroots

- **API volatility.** wlroots exposes unstable APIs (the define `WLR_USE_UNSTABLE` is required). Breaking changes happen between minor versions. UWM targets wlroots 0.20.
- **Abstraction overhead.** There is some performance cost from the generic abstractions. Direct DRM/EGL calls would be faster, but the difference is negligible.
- **Learning curve.** Understanding wlroots requires understanding both the Wayland protocol and wlroots' specific object model.

---

## 4.2 Backend, Renderer, Allocator

### Backend

The backend abstracts the underlying hardware. It can be:
- **DRM backend** (`WLR_BACKEND=drm`): Real hardware, talks to GPU via DRM/KMS
- **Wayland backend** (`WLR_BACKEND=wayland`): Runs as a Wayland client (useful for development, running the compositor inside another compositor)
- **X11 backend** (`WLR_BACKEND=x11`): Runs as an X11 client (legacy, for debugging)
- **Headless backend** (`WLR_BACKEND=headless`): No display, used for remote desktop/virtual displays

UWM calls `wlr_backend_autocreate()` which checks the environment variable and creates the appropriate backend.

### Renderer

The renderer provides OpenGL/GLES2 rendering. It:
- Creates an EGL context bound to the backend
- Manages shaders for blending, transforming, and color management
- Provides `wlr_renderer_begin()`, `wlr_renderer_end()`, and rendering primitives (rectangles, textures, quads)

UWM uses the renderer indirectly through the scene graph. The scene graph calls the renderer to composite the final output.

### Allocator

The allocator creates GPU buffers. It abstracts GBM and other buffer allocation mechanisms. UWM doesn't use the allocator directly — wlroots creates buffers internally for output backbuffers.

---

## 4.3 The Scene Graph

### The Problem

A compositor must composite many surfaces:
- The wallpaper (layer-shell background)
- Tiled windows
- Floating windows
- Popup menus
- The bar (layer-shell top)
- The lock screen (session-lock overlay)
- The mouse cursor

Each surface is at a different z-position, on a different output, with different scales and transforms. The compositor must efficiently:
1. Determine which surfaces are visible
2. Compute how each surface maps to output coordinates
3. Render them in order with correct alpha blending
4. Track damage (which pixels changed)

### How wlroots Solves It: The Scene Graph

The scene graph is a tree of `wlr_scene_node` objects. Each node can be:
- **Tree node:** Has children, no visual content. Used for grouping.
- **Surface node:** Wraps a `wlr_surface`, renders its buffer.
- **Rect node:** Solid color rectangle (for borders, backgrounds).
- **Buffer node:** A raw buffer (for layer-shell surfaces, cursor).

```
wlr_scene (root)
  └── wlr_scene_tree (tiled_layer)
        └── wlr_scene_surface (window 1)
        └── wlr_scene_surface (window 2)
  └── wlr_scene_tree (floating_layer)
        └── wlr_scene_surface (floating window)
        └── wlr_scene_surface (popup)
  └── wlr_scene_tree (layer_overlay)
        └── wlr_scene_surface (lock screen)
  └── wlr_scene_tree (cursor layer)
        └── wlr_scene_buffer (cursor image)
```

### Rendering with the Scene Graph

When `wlr_scene_output_commit()` is called, the scene graph:
1. Computes the bounding box of all rendered content
2. For each node in the tree, determines visibility and position
3. Renders nodes in back-to-front order (depth-first traversal of children)
4. Handles blending (opacity, alpha)
5. Outputs the final buffer

### Why This Is Better

Without a scene graph, the compositor must manually manage the render list and z-ordering. Every time a window is created, destroyed, or reordered, the compositor must rebuild the render list. With a scene graph, the compositor just reparents scene nodes. The scene graph handles rendering order automatically.

---

## 4.4 Object Ownership and Signal Patterns

### wlr_signal

wlroots uses a custom signal mechanism for event delivery. `wlr_signal` is a linked list of `wl_listener` nodes embedded in other structs. When a signal is emitted, all listeners are called in order.

```c
struct wlr_signal {
    struct wl_list listeners;  // linked list of wlr_listener.link
};

struct wlr_listener {
    struct wl_list link;       // node in the signal's listener list
    wlr_notify_func notify;   // callback function
};
```

### How UWM Connects to Signals

UWM embeds `wl_listener` objects in its own structs:

```c
struct uwm_toplevel {
    struct wlr_listener map;
    struct wlr_listener unmap;
    struct wlr_listener commit;
    struct wlr_listener destroy;
    // ...
};
```

In `server_new_xdg_toplevel()`:
```c
toplevel->map.notify = xdg_toplevel_map;
wl_signal_add(&xdg_toplevel->surface->events.map, &toplevel->map);
```

When `wlr_xdg_surface` emits the `map` signal, wlroots iterates its listener list and calls `toplevel->map.notify()` which is `xdg_toplevel_map()`.

### Why Intrusive Lists?

The listener is embedded in the owning struct, not stored separately. This means:
- **No allocation for listening.** The memory is part of the owning object.
- **No need to worry about listener lifetime separately from object lifetime.** When the object is freed, the listener goes with it.
- **The callback receives the struct, not a separate context pointer.**

The pattern to get from listener to owning struct:
```c
void xdg_toplevel_map(struct wl_listener *listener, void *data) {
    struct uwm_toplevel *toplevel = wl_container_of(listener, toplevel, map);
    // now we have the owning toplevel
}
```

`wl_container_of(ptr, container, field)` computes `(container *)((char *)(ptr) - offsetof(container, field))`. This is the common C pattern of using pointer subtraction to get back to the containing struct.

### Why This Pattern Matters for Debugging

Common bugs:
- **Listener not removed.** If the owning object is freed but the listener is still in a signal's list, the next signal emission calls a dangling pointer (use-after-free crash).
- **Listener removed but signal fires during removal.** The `wl_list_remove()` call must be done before the owning struct is freed. But if the signal fires while you're iterating the list to remove your listener, you can crash.
- **wlroots re-entrance.** Some signals fire inside wlroots functions that you're calling. For example, calling `wlr_scene_node_destroy()` might trigger a callback that accesses the scene node being destroyed.

UWM handles this by:
1. Removing all listeners in the destroy handler
2. Using `wl_list_remove()` which is safe to call even if the listener has already been removed (it handles the `wl_list` invariants)
3. Carefully ordering destruction in `server_finish()` to avoid use-after-free

---

## 4.5 How UWM Uses wlroots (Complete API Reference)

| wlroots API | UWM Usage |
|---|---|
| `wlr_backend_autocreate` | server_init |
| `wlr_renderer_autocreate` | server_init |
| `wlr_allocator_autocreate` | server_init |
| `wlr_compositor_create` | server_init |
| `wlr_subcompositor_create` | server_init |
| `wlr_scene_create` | server_init |
| `wlr_scene_attach_output_layout` | server_init |
| `wlr_output_layout_create` | server_init |
| `wlr_cursor_create` | server_init |
| `wlr_xcursor_manager_create` | server_init |
| `wlr_seat_create` | server_init |
| `wlr_xdg_shell_create` | server_init |
| `wlr_layer_shell_v1_create` | layer_shell_create |
| `wlr_idle_inhibit_v1_create` | idle_inhibit_create |
| `wlr_session_lock_manager_v1_create` | session_lock_create |
| `wlr_screencopy_manager_v1_create` | server_init |
| `wlr_ext_image_capture_copy_manager_v1_create` | server_init |
| `wlr_export_dmabuf_manager_v1_create` | server_init |
| `wlr_linux_dmabuf_v1_create_with_renderer` | server_init |
| `wlr_ext_foreign_toplevel_list_v1_create` | server_init |
| `wlr_foreign_toplevel_manager_v1_create` | server_init |
| `wlr_data_device_manager_create` | server_init |
| `wlr_primary_selection_v1_device_manager_create` | server_init |
| `wlr_data_control_manager_v1_create` | server_init |
| `wlr_output_manager_v1_create` | server_init |
| `wlr_cursor_shape_manager_v1_create` | server_init |
| `wlr_pointer_gestures_v1_create` | server_init |
| `wlr_xdg_output_manager_v1_create` | server_init |
| `wlr_transient_seat_manager_v1_create` | server_init |
| `wlr_scene_xdg_surface_create` | window.c, layer_shell.c |
| `wlr_scene_layer_surface_v1_create` | layer_shell.c |
| `wlr_scene_node_set_position` | bsp.c, window.c |
| `wlr_scene_node_set_enabled` | workspace.c, layout.c |
| `wlr_scene_output_commit` | output.c |
| `wlr_scene_output_send_frame_done` | output.c |
| `wlr_seat_keyboard_notify_enter` | window.c |
| `wlr_seat_keyboard_notify_key` | input.c |
| `wlr_seat_pointer_notify_enter` | window.c, input.c |
| `wlr_seat_pointer_notify_motion` | input.c |
| `wlr_seat_pointer_notify_button` | input.c |
| `wlr_cursor_warp` | window.c |
| `wlr_session_change_vt` | input.c |

---

## Exercises

1. In `server.c`, list every `wl_listener` in `struct uwm_server`. For each, what event is it listening to? Where is the callback defined?
2. Trace the call path from `wlr_backend_autocreate()` to `output_frame()` being called. (Hint: look at `wlr_backend.events.new_output`, then `server_new_output`, then `wlr_output.events.frame`.)
3. Why did `wlr_scene_xdg_surface_create()` return NULL when UWM was hitting a bug? (This was a real bug during development.)
4. Draw the scene graph tree for a setup with: 1 output, 2 tiled windows, 1 floating window, 1 popup menu, 1 bar, and a mouse cursor.
5. What would happen if you connected a wl_listener to a wlr_signal after the signal was emitted? How would you find such a bug?

---

## Interview Questions

**Q (Junior):** What is wlroots?
**A:** A library that provides the core infrastructure for building Wayland compositors. It handles DRM/KMS, input via libinput, Wayland protocol implementations, scene graph rendering, and seat management. Compositors built on wlroots only need to implement their layout and window management logic.

**Q (Mid):** How does `wl_container_of` work?
**A:** It uses pointer arithmetic to find the containing struct from a pointer to one of its members. `wl_container_of(ptr, type, member)` computes `(type *)((char *)(ptr) - offsetof(type, member))`. This is the standard C pattern for intrusive data structures, where the node/link is embedded in the containing struct.

**Q (Senior):** Why does wlroots use intrusive linked lists (wl_list) instead of the standard wl_list?
**A:** Standard linked lists allocate node objects separately from the data objects, requiring malloc/free for every insertion/removal. Intrusive lists embed the node within the data object, eliminating allocation overhead. In a compositor handling frequent surface commits, this matters. wlroots uses the same `wl_list` type as the Wayland protocol library (libwayland-server), which is also intrusive for the same reason.

**Q (Senior):** What happens if you call `wlr_scene_node_destroy()` inside a signal handler that was triggered by the scene graph?
**A:** This is a re-entrancy problem. Destroying a scene node triggers its `destroy` signal, which may call handlers that access the node being destroyed. If a handler destroys a parent node, the current node becomes a dangling pointer. wlroots handles some of this internally (signals are emitted before the actual free), but compositor authors must be careful not to destroy scene nodes that are being iterated. UWM's pattern of deferring destruction to idle callbacks (like `uwm_bar_send_output` using `wl_event_loop_add_idle`) avoids re-entrancy.

---

*This concludes Part 4. The subsequent parts (5-16) will be written in continuation files due to the length of this textbook. Part 5 will cover UWM's architecture in depth.*

---

# Part 5: UWM Architecture

## Learning Objectives

After completing this part, you will be able to:
- Explain why UWM is structured the way it is
- Trace the complete event flow from input to screen
- Describe ownership and lifetime for every major object
- Walk through startup and shutdown sequences from memory

## 5.1 Why This Architecture?

### The Problem

A compositor must integrate many subsystems: input, window management, layout, rendering, output, and protocols. Each subsystem has different concerns, lifetimes, and failure modes. The architecture determines how these subsystems interact, who owns what, and how data flows.

### The Design Principles

UWM's architecture follows these principles:

1. **Single-threaded event loop.** All compositor logic runs in one thread. No mutexes, no data races, no deadlocks. wlroots' library calls may use threads internally (e.g., DRM page flip handlers), but all callbacks arrive on the main thread.

2. **wlroots owns the hardware; UWM owns the layout.** wlroots abstracts DRM, input, and Wayland protocol handling. UWM implements the window layout algorithm (BSP tree) and policy decisions (focus, workspace switching, floating/tiling).

3. **Listeners for everything.** UWM connects to wlroots' signal system for all events. There is no polling, no busy-waiting. Every state change is reactive.

4. **Static pools for hot-path allocations.** BSP nodes and window tracking use pre-allocated arrays. No malloc on the hot path (window creation, BSP operations). This ensures deterministic performance.

5. **Information hiding via functions.** Each module exposes only what other modules need. `bsp.c` exposes BSP operations; `window.c` exposes toplevel lifecycle; `workspace.c` exposes workspace management. Modules call each other's functions directly — there is no message bus or abstraction layer between modules.

### How Modules Communicate

```
input.c ──> window.c (focus_toplevel)
input.c ──> bsp.c (bsp_focus_left, etc.)
input.c ──> workspace.c (workspace_switch)
input.c ──> floating.c (toggle_floating)
input.c ──> layout.c (toggle_monocle)
input.c ──> output.c (via workspace_switch -> output_set_workspace)

window.c ──> bsp.c (bsp_insert, bsp_remove)
window.c ──> workspace.c (workspace_move_toplevel, via move_to_workspace)
window.c ──> floating.c (via toggle_floating)
window.c ──> output.c (get_output_size)
window.c ──> input.c (reset_cursor_mode)
window.c ──> uwm_bar.c (uwm_bar_send_output)

output.c ──> workspace.c (workspace_show_on_output, workspace_hide_from_output)
output.c ──> bsp.c (bsp_arrange)
output.c ──> window.c (focus_toplevel)
output.c ──> layer_shell.c (layer_surface_arrange)

bsp.c ──> workspace.c (struct uwm_workspace)
bsp.c ──> output.c (get_output_size)

workspace.c ──> output.c (output_set_workspace)
workspace.c ──> window.c (focus_toplevel)
workspace.c ──> bsp.c (bsp_insert, bsp_remove, bsp_arrange)
workspace.c ──> floating.c (toggle_fullscreen)
workspace.c ──> layout.c (set_children_visible, update_layout_visibility)
```

There is no central dispatcher. Module A calls module B's function directly. This is simpler than a message-passing architecture and is appropriate for a compositor where module interactions are well-defined and bounded.

### Why Not a Message Bus?

A message bus (like signals/slots in Qt, or an event bus) adds:
- Indirection (who receives this message?)
- Serialization (messages must be queued and dispatched)
- Debugging complexity (where is this message handled?)
- Performance cost (allocation for message objects, function call overhead)

Direct function calls are faster, easier to debug, and the dependencies are explicit in the code. The downside is tighter coupling: changing `bsp.h` may require changes in `window.c`, `workspace.c`, `floating.c`, and `layout.c`. For a small compositor (15 source files), this is acceptable.

---

## 5.2 Event Flow

### Keyboard Event Flow

```
Physical keyboard
    |
    v
Kernel evdev (/dev/input/event*)
    |
    v
libinput (processes raw event, generates high-level event)
    |
    v
wlr_backend (receives libinput event via wlr_input_device)
    |
    v
wlr_seat (routes to wlr_keyboard)
    |
    v
UWM's keyboard_handle_key() [input.c]
    |
    +--> Handle special keys (VT switch, terminal lock)
    |
    +--> Dispatch unmodified keys (KEY_LEFTMETA -> toggle overview, etc.)
    |
    +--> If MOD held: search keybindings array
    |       |
    |       +--> workspace() -> workspace_switch() -> output_set_workspace()
    |       +--> spawn() -> fork() + exec()
    |       +--> togglefloating() -> toggle_floating()
    |       +--> moveleft() -> bsp_focus_left()
    |       +--> etc.
    |
    +--> Forward to focused client via wlr_seat_keyboard_notify_key()
```

### Pointer Event Flow

```
Physical mouse
    |
    v
Kernel evdev
    |
    v
libinput
    |
    v
wlr_backend
    |
    v
wlr_cursor (accumulates motion, manages pointer)
    |
    v
UWM's server_cursor_motion/motion_absolute() [input.c]
    |
    v
process_cursor_motion()
    |
    +-- If cursor_mode == MOVE: process_cursor_move()
    +-- If cursor_mode == RESIZE: process_cursor_resize()
    +-- If cursor_mode == PASSTHROUGH:
            |
            v
            desktop_toplevel_at() [window.c] (scene node hit test)
            |
            +--> If hovered surface != focused surface: focus_toplevel()
            +--> Forward to client via wlr_seat_pointer_notify_motion()

UWM's server_cursor_button() [input.c]
    |
    +-- If MOD held + click on window: begin_interactive() (MOVE or RESIZE)
    +-- Otherwise: forward to client via wlr_seat_pointer_notify_button()
        (client processes click, compositor receives commit event, scene updates)
```

### Window Creation Flow

```
Client connects to Wayland socket
    |
    v
wlroots creates wl_client, adds to display
    |
    v
Client binds to xdg_wm_base global
    |
    v
Client creates xdg_surface, then xdg_toplevel
    |
    v
wlr_xdg_shell emits new_surface signal
    |
    v
UWM's server_new_xdg_toplevel() [window.c]
    |
    +-- calloc() uwm_toplevel
    +-- wlr_scene_xdg_surface_create() in tiled_layer
    +-- Add all listeners (map, unmap, commit, destroy, etc.)
    |
    v
Client attaches wl_buffer, commits
    |
    v
UWM's xdg_toplevel_map() (on first commit)
    |
    +-- should_tile_toplevel() -> false for dialogs, etc.
    +-- rule_apply_all() -> workspace/float rule
    +-- bsp_insert() or float
    +-- focus_toplevel()
    +-- uwm_bar_send_output()
    +-- bsp_arrange()
```

### Frame Rendering Flow

```
Output's frame event fires (at refresh rate)
    |
    v
UWM's output_frame() [output.c]
    |
    +-- if disabled or session inactive: skip
    +-- wlr_scene_output_commit(output->scene_output)
    |       |
    |       v
    |   wlr_scene walks tree
    |       |
    |       +-- For each visible surface node: render buffer
    |       +-- Composite with alpha blending
    |       +-- Write to output buffer
    |
    +-- wlr_scene_output_send_frame_done() (tells clients "frame is displayed")
```

---

## 5.3 Startup Sequence

```
main()
    |
    +-- Create struct uwm_server server = {0}
    |
    +-- server_init(&server)
    |       |
    |       +-- wlr_log_init(WLR_DEBUG)
    |       +-- server.wl_display = wl_display_create()
    |       +-- server.backend = wlr_backend_autocreate()
    |       +-- server.renderer = wlr_renderer_autocreate(backend)
    |       +-- wlr_renderer_init_wl_shm(renderer, display)
    |       +-- server.allocator = wlr_allocator_autocreate(backend, renderer)
    |       +-- server.compositor = wlr_compositor_create(display, renderer)
    |       +-- server.subcompositor = wlr_subcompositor_create(display)
    |       +-- server.scene = wlr_scene_create()
    |       +-- server.tiled_layer = wlr_scene_tree_create(scene)
    |       +-- server.floating_layer = wlr_scene_tree_create(scene)
    |       |
    |       +-- server.output_layout = wlr_output_layout_create()
    |       +-- wlr_scene_attach_output_layout(scene, output_layout)
    |       |
    |       +-- server.cursor = wlr_cursor_create()
    |       +-- wlr_cursor_attach_output_layout(cursor, output_layout)
    |       +-- server.xcursor_mgr = wlr_xcursor_manager_create("default", 24)
    |       |
    |       +-- server.seat = wlr_seat_create(display, "seat0")
    |       |
    |       +-- Protocol globals (xdg_shell, layer_shell, idle_inhibit, etc.)
    |       +-- server.bar_manager = uwm_bar_manager_create(server)
    |       |
    |       +-- bsp_pool_init(&server.bsp_pool)
    |       +-- workspace_manager_init(&server.workspaces)
    |       |
    |       +-- Listen for new_output, new_input, new_xdg_toplevel
    |       +-- Listen for backend destroy, session signal, renderer lost
    |       +-- Listen for output manager, cursor shape, etc.
    |
    +-- wlr_backend_start(backend)
    |       |
    |       +-- Opens DRM, creates outputs, starts libinput
    |       +-- new_output signal fires -> server_new_output()
    |
    +-- Set WAYLAND_DISPLAY environment variable
    |
    +-- install_crash_handlers()
    |       (sigsetjmp point for crash recovery)
    |
    +-- run_autostart()
    |       (fork/exec each autostart command)
    |
    +-- wl_display_run(display)
    |       (event loop, never returns until compositor exits)
    |
    +-- server_finish(&server)
    |       (cleanup everything)
    |
    +-- return 0
```

---

## 5.4 Shutdown Sequence

```
SIGINT/SIGTERM received
    |
    v
handle_term_signal()
    |
    v
wl_display_terminate(display)
    |
    v
wl_display_run() returns
    |
    v
server_finish(&server)
    |
    +-- destroy all clients (wl_display_destroy_clients)
    +-- remove all listeners (wl_list_remove for each)
    +-- workspace_manager_finish() (destroy BSP trees)
    +-- uwm_bar_manager_destroy()
    +-- layer_shell_destroy()
    +-- idle_inhibit_destroy()
    +-- session_lock_destroy()
    +-- Destroy per-protocol globals
    +-- wlr_cursor_destroy(cursor)
    +-- wlr_xcursor_manager_destroy(xcursor_mgr)
    +-- wlr_output_layout_destroy(output_layout)
    +-- wlr_scene_node_destroy(scene)
    +-- wlr_allocator_destroy(allocator)
    +-- wlr_renderer_destroy(renderer)
    +-- wlr_backend_destroy(backend)
    +-- wl_display_destroy(display)
```

### Crash Recovery Shutdown

On SIGSEGV/SIGABRT/SIGBUS/SIGFPE/SIGILL:

```
crash_handler()
    |
    v
siglongjmp(jmp_buf, 1)  (restores main()'s stack frame)
    |
    v
main() recovers with sigsetjmp return = 1
    |
    +-- Restore crash handlers (install_crash_handlers)
    +-- server_init(&server)   (reinitializes everything)
    +-- Re-enter wl_display_run()
```

The crash recovery is a second-chance mechanism. It works by unwinding the stack to main()'s sigsetjmp point, which skips all code between the crash and main(). This is inherently unsafe (resources leak, state may be inconsistent), but it's better than crashing and leaving the user at a black screen. The compositor will reinitialize everything, restoring the display.

---

## 5.5 Window Lifecycle

```
                        +-----------+
                        |  Created  |
                        | (calloc)  |
                        +-----+-----+
                              |
                     xdg_toplevel_map()
                     (first surface commit)
                              |
                        +-----v-----+
                        |   Mapped  |<------+
                        | (visible) |       |
                        +-----+-----+       |
                              |              |
                   +----------+----------+   |
                   |          |          |   |
              tiled      floating   fullscreen
              (bsp)      (scene     (scene
              insert)    reparent)  reparent)
                   |          |          |
                   +----------+----------+
                              |
                     xdg_toplevel_unmap()
                     (surface hidden, not destroyed)
                              |
                        +-----v-----+
                        |  Unmapped |
                        | (hidden)  |
                        +-----+-----+
                              |
                     xdg_toplevel_destroy()
                     (wlr_xdg_toplevel destroyed)
                              |
                        +-----v-----+
                        | Destroyed |
                        | (free)    |
                        +-----------+
```

### State Transitions

- **Created -> Mapped:** First commit happens. The surface has a buffer. UWM inserts the window into the BSP tree or floating layer.
- **Mapped -> Unmapped:** The client hides the window (e.g., minimizes, or the process loses focus). The buffer is detached but the xdg_toplevel object still exists. UWM removes from BSP, handles focus displacement.
- **Unmapped -> Mapped:** The client commits a new buffer. UWM re-inserts into BSP.
- **Mapped -> Destroyed:** The client destroys the xdg_toplevel. UWM frees the uwm_toplevel.
- **Unmapped -> Destroyed:** Same, but BSP cleanup was already done during unmap.

### Focus Displacement on Unmap

When a window is unmapped, UWM must find a new window to focus:

```
xdg_toplevel_unmap()
    |
    +-- bsp_remove(toplevel)
    |       |
    |       +-- Find the leaf node for this window
    |       +-- Collapse parent: promote sibling leaf
    |       +-- If sibling is also internal node: restructure tree
    |       +-- Return freed leaf to pool
    |
    +-- bsp_arrange()
    |
    +-- Focus displacement:
    |       |
    |       +-- If workspace has other tiled windows: focus last_focused
    |       +-- If no tiled windows but has floating: focus a floating window
    |       +-- If no windows at all: clear focus (no active window)
    |       +-- If fullscreen/monocle was active: exit those modes
    |
    +-- uwm_bar_send_output()  (update bar state)
```

---

## 5.6 Ownership and Lifetime

### Who Owns What

| Object | Allocated by | Freed by | Notes |
|---|---|---|---|
| uwm_toplevel | server_new_xdg_toplevel (calloc) | xdg_toplevel_destroy (free) | wlroots owns the wlr_xdg_toplevel; UWM owns the wrapper |
| uwm_output | server_new_output (calloc) | output_destroy (free) | wlroots owns the wlr_output |
| uwm_keyboard | server_new_keyboard (calloc) | keyboard_handle_destroy (free) | wlroots owns the wlr_keyboard |
| uwm_popup | server_new_xdg_popup (calloc) | xdg_popup_destroy (free) | wlroots owns the wlr_xdg_popup |
| uwm_layer_surface | layer_surface_create (calloc) | layer_surface_destroy / handle_node_destroy (free) | wlroots owns the scene layer surface |
| BSP nodes | bsp_node_alloc (from pool) | bsp_node_free (returns to pool) | Static pool, no heap |
| uwm_workspace[9] | Stack-embedded in server | N/A | Never individually allocated/freed |

### The Golden Rules of Lifetime

1. **wlroots objects outlive UWM wrappers.** When wlroots emits a `destroy` signal, it means "this wlroots object is about to be freed." UWM must free its wrapper immediately in the destroy handler. After the destroy handler returns, the wlroots object is freed.

2. **UWM wrappers must not be accessed after destroy.** Once `xdg_toplevel_destroy()` returns, the `uwm_toplevel` pointer is invalid. Any code that held a pointer to it has a dangling reference. This is why BSP nodes store a `toplevel` pointer — when the toplevel is destroyed, the BSP leaf is also cleaned up.

3. **Listeners must be removed before the owning UWM object is freed.** If a listener is still connected to a wlroots signal and the UWM object is freed, the next signal emission crashes. UWM removes all listeners in the destroy handler before freeing.

4. **Scene nodes are owned by wlroots.** When you create a scene node via `wlr_scene_xdg_surface_create()`, wlroots owns the memory. Do not free it directly. When the xdg_surface is destroyed, wlroots frees the scene node automatically.

---

## Exercises

1. Draw the complete event flow for a window being moved from workspace 1 to workspace 2 via keybinding. Include every function call.
2. What happens if `wl_display_run()` returns unexpectedly (not from SIGINT)? How would you debug this?
3. Trace the ownership of a pointer event from the kernel to `server_cursor_motion()`. What structs are involved at each step?
4. Why does UWM use `calloc` instead of `malloc` for allocations? What bug does `calloc` prevent?
5. What would happen if `server_new_xdg_toplevel()` returned before adding all listeners? What specific crash would occur?

---

## Interview Questions

**Q (Mid):** Explain UWM's crash recovery mechanism. Why is it unsafe?
**A:** UWM uses `sigsetjmp`/`siglongjmp`. When a crash signal (SIGSEGV, SIGABRT) is received, the signal handler calls `siglongjmp` to return to the `sigsetjmp` point in `main()`. This unwinds the stack, skipping all intermediate cleanup. Then `main()` reinitializes the server. It's unsafe because `siglongjmp` skips destructor calls, leaving resources leaked (file descriptors, malloc'd memory, partially modified state). However, for a compositor, crashing is worse than leaking resources on recovery — the user would lose their session. The assumption is that crash recovery is rare enough that leaks don't accumulate.

**Q (Senior):** How would you add a new protocol to UWM? Walk through every step.
**A:** 1) Create the XML file in `protocol/`. 2) Add `wayland-scanner` commands to the Makefile to generate server-side code. 3) Create a new `.c`/`.h` pair in `src/` and `include/`. 4) In the new module, implement a `_create()` function that calls `wl_global_create()` with the interface and bind handler. 5) Store the global in `struct uwm_server`. 6) In `server_init()`, call the module's `_create()`. 7) In the bind handler, allocate the protocol-specific state and wire listeners. 8) Handle resource destruction (client disconnect, resource destroy request). 9) In `server_finish()`, call the module's `_destroy()`.

**Q (Principal):** You need to add a feature that requires coordinating across all modules. For example, "disable all animations when the user is on battery power." How would you implement this without coupling every module to a power management module?
**A:** Use a centralized state flag in `struct uwm_server` (e.g., `server->battery_power`). Each module checks this flag in its rendering code. The power management module (or an input event handler) sets the flag and calls a "battery changed" notification. This notification could be a function that signals modules to update (e.g., `server->battery_notify`). Each module iterates its outputs and clients, updating animation states. The key is that the flag itself lives in the server struct (the single authoritative state), and modules read it when they need it, rather than having a callback system.

---

# Part 6: Every Source File

## Learning Objectives

After completing this part, you will be able to:
- Describe every source file's purpose, ownership, and callers
- Trace cross-module interactions
- Identify the hot path in each module

## 6.1 main.c

**Purpose:** Program entry point, signal handling, crash recovery, autostart.

**Ownership:** Owns the `struct uwm_server` (stack-allocated). Owns the process lifecycle.

**Who calls it:** Kernel (ELF entry point via exec).

**Key concepts:**
- `sigsetjmp`/`siglongjmp` for crash recovery
- PID-file guarded autostart to prevent duplicate processes
- The `WAYLAND_DISPLAY` environment variable setup

**Hot path:** Only error handling and crash recovery. Not on the rendering or input hot path.

**Why it exists:** Every program needs a `main()`. It separates process management (signals, autostart) from compositor logic (server.c).

---

## 6.2 server.c

**Purpose:** Creates and destroys all wlroots objects, protocol globals, scene graph, cursor, seat, input, outputs, bar, session lock, and screen capture protocols.

**Ownership:** The `struct uwm_server` defines the entire compositor state. This file initializes everything and tears it down.

**Who calls it:** `main.c` calls `server_init()` and `server_finish()`.

**Who it calls:** Every module's create function, all wlroots constructor functions.

**Key struct:** `struct uwm_server` — the central state. Every other module accesses the server via a pointer stored in their own structs.

**Key functions:**
- `server_init()`: ~200 lines of initialization. Every new wlroots object or protocol is added here.
- `server_finish()`: Destroys everything in reverse order. Must be updated when new objects are added.

**Failure modes:**
- Any `create` function can return NULL. `server_init()` has goto-based cleanup for each failure point.
- If the backend fails to start, the compositor cannot run.

**Performance:** Initialization is not performance-critical. It runs once at startup.

**Debugging:** If a protocol is not working, check that its create function is called in `server_init()` and that the global is properly bound.

---

## 6.3 window.c

**Purpose:** Manages the full lifecycle of xdg-toplevel windows and xdg-popups.

**Ownership:** Allocates and frees `struct uwm_toplevel` and `struct uwm_popup`. Each toplevel owns its foreign toplevel handles and capture scene.

**Who calls it:** wlroots signals (map, unmap, commit, destroy) via listeners. Other modules call `focus_toplevel()`, `desktop_toplevel_at()`, `server_new_xdg_toplevel()`.

**Who it calls:** `bsp.h` (insert, remove, arrange), `workspace.h` (workspace_move_toplevel), `floating.h` (toggle_floating), `output.h` (get_output_size), `rules.h` (rule_apply_all), `uwm_bar.h` (uwm_bar_send_output), `input.h` (reset_cursor_mode), `layout.h` (update_layout_visibility).

**Key structs:**
- `struct uwm_toplevel`: 11 wl_listener fields, anonymous union for workspace_link/floating_link, geometry tracking, BSP save/restore state.
- `struct uwm_popup`: xdg_popup reference, commit and destroy listeners.

**Key functions:**
- `server_new_xdg_toplevel()`: Called on each new wlr_xdg_toplevel. Creates the UWM wrapper.
- `xdg_toplevel_map()`: Core window insertion logic. Decides tile vs float, inserts into BSP or floating layer, updates focus.
- `xdg_toplevel_unmap()`: Core window removal logic. Removes from BSP, handles focus displacement, handles fullscreen/monocle exit.
- `focus_toplevel()`: Central focus logic. Activates windows, updates seat, warps cursor, notifies bar.
- `desktop_toplevel_at()`: Hit test by walking scene tree at a point. Used for cursor interaction.

**Hot path:** `focus_toplevel()` is called on every click, workspace switch, and window map/unmap. `desktop_toplevel_at()` is called on every cursor motion. These must be fast.

**Common bugs:**
- Missing listener removal in destroy (use-after-free).
- Not handling focus displacement correctly when the last window on a workspace is unmapped.
- Not restoring BSP state when un-floating a window.

---

## 6.4 output.c

**Purpose:** Manages display outputs: hotplug, workspace assignment, per-output layer scene trees, frame rendering.

**Ownership:** Allocates and frees `struct uwm_output`. Each output owns its per-layer scene trees.

**Who calls it:** wlroots signals (new_output, output frame, output destroy). Other modules call `output_set_workspace()`, `get_output_size()`, `output_from_wlr_output()`.

**Who it calls:** `workspace.h` (show_on_output, hide_from_output), `bsp.h` (bsp_arrange), `window.h` (focus_toplevel), `layer_shell.h` (layer_surface_arrange), `uwm_bar.h` (uwm_bar_send_output).

**Key functions:**
- `server_new_output()`: Hotplug handler. Creates per-output scene trees in z-order.
- `output_frame()`: Frame callback. Calls `wlr_scene_output_commit()`.
- `output_destroy()`: Cleans up destroyed output, evacuates windows to remaining outputs.
- `output_set_workspace()`: Workspace switching on an output.

**Hot path:** `output_frame()` runs at each monitor's refresh rate (60+ Hz). `wlr_scene_output_commit()` inside it must be fast.

**Z-order of per-output scene trees:**
```
layer_background    (wallpaper)
layer_bottom        (behind windows)
layer_floating      (floating windows, fullscreen windows)
layer_top           (over windows)
layer_overlay       (on top of everything)
layer_lock          (session lock, above all)
```

---

## 6.5 input.c

**Purpose:** Keyboard, pointer, and gesture event processing. Keybinding dispatch. Cursor mode state machine.

**Ownership:** Allocates and frees `struct uwm_keyboard`. Does not own pointer devices (wlroots owns those via cursor).

**Who calls it:** wlroots signals (keyboard key/modifiers/destroy, cursor motion/button/axis/frame/gesture). Other modules call `reset_cursor_mode()`, `begin_interactive()`.

**Who it calls:** `window.h` (focus_toplevel, desktop_toplevel_at), `bsp.h` (bsp_focus_*, bsp_swap, bsp_resize), `floating.h` (toggle_floating, toggle_fullscreen), `layout.h` (toggle_monocle, set_bsp_mode), `workspace.h` (workspace_switch, etc.), `output.h` (via workspace switching).

**Key struct:**
- `struct uwm_keyboard`: XKB state, key repeat timer, cached modifier indices.
- `enum uwm_cursor_mode`: PASSTHROUGH, MOVE, RESIZE — the cursor interaction state machine.

**Key functions:**
- `keyboard_handle_key()`: Core keyboard handler. Resolves keycode->keysym, dispatches bindings, forwards unmodified keys.
- `server_cursor_motion()`: Motion handler. Routes to move/resize/passthrough.
- `server_cursor_button()`: Button handler. MOD+click for interactive move/resize.
- `process_cursor_move()`: Updates floating window position.
- `process_cursor_resize()`: Updates floating window geometry.
- `handle_keybinding()`: Searches binding array and dispatches action.

**Hot path:** `keyboard_handle_key()` on every keystroke. `server_cursor_motion()` on every mouse movement. These must be fast.

**Keybinding dispatch algorithm:**
```
handle_keybinding(modifiers, keysym, keys, keys_len)
    for each key in keys:
        if key.modifiers == modifiers && key.keysym == keysym:
            key.func(&server, &key.arg)
            return true
    return false
```

This is O(n) where n is the number of keybindings (typically ~40). For each keystroke, we iterate the binding list. This is acceptable because n is small. For a compositor with hundreds of bindings, a hash table would be better.

---

## 6.6 bsp.c

**Purpose:** Binary space partitioning tree: allocation, insertion, removal, arrangement, navigation, swap, resize, rotation.

**Ownership:** Manages a static pool of BSP nodes. Does not own windows.

**Who calls it:** `input.c` (navigation, swap, resize), `window.c` (insert, remove), `workspace.c` (insert, remove, arrange), `floating.c` (restore on un-float), `output.c` (arrange on output change).

**Key structs:**
- `struct uwm_bsp_node`: Tree node with parent/first/second children, toplevel pointer, split, ratio, geometry, mode, active_child.
- `struct uwm_bsp_pool`: 512 pre-allocated nodes, freelist.

**Key functions:**
- `bsp_insert()`: Replaces focused leaf with internal node containing old leaf + new leaf.
- `bsp_remove()`: Collapses parent, promotes sibling, handles active_child.
- `bsp_arrange()`: Entry point for arrangement. Skips hidden workspaces, handles monocle.
- `bsp_arrange_node()`: Recursive arrangement. Splits rectangle at ratio for internal nodes, applies geometry to leaves.
- `bsp_focus_left/right/up/down()`: Directional focus with center-point distance heuristic.
- `bsp_swap_direction()`: Swaps focused window with neighbor in direction.
- `bsp_resize()`: Adjusts split ratio, clamped to [0.10, 0.90].

**Pool allocation algorithm:**
```
Initial state: freelist -> node[0] -> node[1] -> ... -> node[511] -> NULL

bsp_node_alloc():
    node = freelist
    if node: freelist = node->first   (first used as next pointer)
    return node

bsp_node_free(node):
    node->first = freelist
    freelist = node
```

This is O(1). No malloc/free calls in the hot path of window creation/destruction.

**Split direction selection:**
```
In bsp_insert():
    if container is wider than tall (width > height):
        split = VERTICAL
    else:
        split = HORIZONTAL
```

This ensures that windows are arranged to fill the available space efficiently. A wide container gets vertical splits (left/right), a tall container gets horizontal splits (top/bottom).

---

## 6.7 workspace.c

**Purpose:** Manages 9 workspaces: initialization, switching, show/hide, window movement.

**Ownership:** The workspace array is embedded in `struct uwm_workspace_manager`, which is embedded in `struct uwm_server`. No dynamic allocation.

**Who calls it:** `input.c` (workspace switching), `window.c` (move to workspace), `output.c` (show/hide on output).

**Who it calls:** `output.c` (output_set_workspace), `window.c` (focus_toplevel), `bsp.c` (insert, remove, arrange), `floating.c` (toggle_fullscreen), `layout.c` (set_children_visible).

**Key functions:**
- `workspace_switch()`: Delegates to `output_set_workspace()`.
- `workspace_move_toplevel()`: Moves window between workspaces with full state cleanup.
- `workspace_hide_from_output()` / `workspace_show_on_output()`: Shows/hides all windows via scene node enable/disable.

---

## 6.8 floating.c

**Purpose:** Toggle between tiled and floating states, toggle fullscreen, raise floating windows.

**Who calls it:** `input.c` (togglefloating action), `layout.c` (set_bsp_mode), `window.c` (toggle_fullscreen on request).

**Who it calls:** `bsp.h` (insert, remove, restore, arrange, collect_leaves), `window.h` (focus_toplevel), `workspace.h`, `output.h` (get_output_size), `layout.h` (set_children_visible).

**Key functions:**
- `toggle_floating()`: Saves BSP position, removes from BSP, reparents to floating_layer. Or: reparents to tiled_layer, restores BSP position.
- `toggle_fullscreen()`: Hides all other windows, positions at (0,0) at output size. Or: restores geometry, shows all windows.

---

## 6.9 layout.c

**Purpose:** Toggle monocle mode, reset to BSP mode, manage subtree visibility.

**Who calls it:** `input.c` (togglemonocle/setbsp actions).

**Who it calls:** `bsp.h` (arrange, collect_leaves), `window.h` (focus_toplevel), `floating.h` (toggle_fullscreen, toggle_floating).

**Key functions:**
- `toggle_monocle()`: Hides all tiled windows except focused, or shows all.

---

## 6.10 layer_shell.c

**Purpose:** Implements wlr-layer-shell protocol.

**Ownership:** Allocates and frees `struct uwm_layer_surface`.

**Who calls it:** wlroots signals (new_surface, map, unmap, commit, destroy). Other modules call `layer_surface_arrange_all()`.

**Who it calls:** `output.c` (per-output layer scene trees, usable_area), `window.c` (focus_toplevel), `bsp.c` (bsp_arrange).

**Key functions:**
- `layer_surface_arrange()`: Two-pass arrangement for exclusive/non-exclusive zones. Calculates usable_area for output.
- `layer_surface_get_exclusive_zones()`: Sums exclusive zones for each anchor direction.

---

## 6.11 idle_inhibit.c

**Purpose:** Prevents screen blanking during fullscreen video.

**Who calls it:** wlroots signals (new_inhibitor, destroy).

**This is a thin wrapper — only ~50 lines of real code.**

---

## 6.12 session_lock.c

**Purpose:** Implements ext-session-lock protocol (swaylock-compatible).

**Who calls it:** wlroots signals (new_lock, unlock, new_surface).

**Key functions:**
- `handle_new_lock()`: Accepts/rejects lock request.
- `handle_new_lock_surface()`: Creates scene node in output's layer_lock.

---

## 6.13 uwm_bar.c

**Purpose:** Implements the custom zwp_uwm_bar_v1 protocol for UBar.

**Ownership:** Allocates and frees `struct uwm_bar_manager` and `struct uwm_workspace_group` per client.

**Who calls it:** wl_global bind handlers. Other modules call `uwm_bar_send_output()`, `uwm_bar_send_workspace_state()`, `uwm_bar_send_focused_title()`.

---

## 6.14 rules.c

**Purpose:** Applies compile-time rules to new windows.

**Who calls it:** `window.c` (xdg_toplevel_map).

**Key functions:**
- `rule_apply_all()`: Iterates all rules, applies matching ones.
- `glob_match()`: Simple recursive glob matcher supporting `*` and `?`.

---

## 6.15 config.c

**Purpose:** Exports compile-time configuration macros as linkable symbols.

**Who calls it:** `server_init()` calls `config_load()`.

---

## Exercises

1. Read every file in `src/` and identify every `wl_listener`. For each, write down: what signal is it listening to? What is the callback function? What file defines the callback?
2. For each module, list every other module it imports (via #include). Draw the dependency graph. Are there any circular dependencies?
3. `bsp.c` uses a static pool. If you ran out of pool slots (all 512 used), what would happen? How would you handle this?
4. Trace the `wl_listener` lifecycle for an `uwm_toplevel` from creation to destruction. When are listeners added? When are they removed?

---

## Interview Questions

**Q (Mid):** Why does UWM use a static pool for BSP nodes instead of malloc?
**A:** Deterministic performance. malloc can take variable time (especially under memory pressure), can fragment the heap, and can fail. The pool gives O(1) allocation and deallocation with guaranteed success (up to the pool limit). 512 slots is enough because the user will not open that many windows simultaneously. If the pool runs out, the compositor can fall back to malloc (graceful degradation).

**Q (Senior):** The `uwm_toplevel` struct uses an anonymous union for `workspace_link` and `floating_link`. Why?
**A:** A window is either tiled (in a workspace's toplevel list) or floating (in a workspace's floating list), never both simultaneously. Using a union saves memory — the two list nodes share the same storage (~16 bytes saved per window). At 200 windows, that's 3.2KB — not much, but the pattern keeps the struct clean and expresses the invariant (tiled XOR floating) in the type system.

**Q (Senior):** How would you add per-window transparency to UWM?
**A:** Add an opacity field to `struct uwm_toplevel`. Call `wlr_scene_node_for_each_buffer(toplevel->scene_tree, set_opacity, &toplevel->opacity)` after creation and when opacity changes. Add a protocol (or IPC command) for the user to set opacity. The `set_opacity` callback calls `wlr_scene_buffer_set_opacity()` on each buffer node.

---

# Part 7: Algorithms

## Learning Objectives

After completing this part, you will be able to:
- Explain the BSP tree algorithm and its complexity
- Implement directional focus navigation
- Analyze the performance of every algorithm in UWM

## 7.1 The BSP Tree Algorithm

### What is a BSP Tree?

A Binary Space Partitioning tree recursively divides a rectangular area into two parts at each node. In UWM, the divides are always axis-aligned (vertical or horizontal). The leaves represent windows, and internal nodes represent the splits.

```
           +-------+-------+
           |       |       |
           |   A   |   B   |
           |       |       |
           +-------+-------+
           |       |       |
           |   C   |   D   |
           |       |       |
           +-------+-------+

Tree:
          root (HORIZONTAL, ratio=0.5)
          /                        \
    (VERTICAL, 0.5)          (VERTICAL, 0.5)
      /        \                /        \
     A          B              C          D
```

This is a **strict binary tree**: each internal node has exactly two children. There is no "empty" leaf.

### Insertion Algorithm

```
bsp_insert(tree, new_leaf):
    1. Find the currently focused leaf node
    2. Create a new internal node
    3. Choose split direction:
       if focused leaf's container is wider than tall:
           direction = VERTICAL
       else:
           direction = HORIZONTAL
    4. Set ratio = 0.5
    5. Replace focused leaf with new internal node
    6. Set focused leaf and new leaf as children of the new internal node
```

The focused leaf is always replaced. The new window and the previously focused window become siblings under a new parent. This means the focused window *always* gets half its current space.

### Complexity

- **Insertion:** O(log n) — find the focused leaf (tree walk), O(1) for restructuring. The tree walk is proportional to tree depth, which is O(log n) for balanced trees but can be O(n) for degenerate trees.
- **Removal:** O(log n) — find the leaf, O(1) to collapse parent.
- **Arrangement:** O(n) — walk the entire tree applying geometry to all leaves.
- **Directional focus:** O(n) — collect all leaves, compute distances, find nearest.

### Why BSP Instead of i3's Algorithm?

i3 uses a slightly different approach:
- Splits always apply to a container (which may contain multiple windows)
- Containers can be vertical, horizontal, or tabbed
- New windows are added to the current container, not replacing the focused window

BSP's approach (replacing the focused window) is more predictable: the focused window always gets exactly half the available space, regardless of the current layout depth. i3's approach requires the user to understand the container hierarchy.

The tradeoff: BSP can produce very narrow slivers after many splits in the same direction. UWM's split direction heuristic (based on aspect ratio) mitigates this, but users can still create thin columns by repeatedly splitting horizontally on a wide monitor.

### Arrangement Algorithm

```
bsp_arrange_node(node, x, y, w, h):
    if node is leaf:
        if toplevel is floating or fullscreen:
            return
        if node.mode == MONOCLE:
            if node is the active child:
                bsp_arrange_node_full(toplevel, x, y, w, h)
            else:
                hide toplevel
        else:
            apply gaps
            set scene node position to (x + gap, y + gap)
            set scene node size to (w - 2*gap, h - 2*gap)
            set toplevel as tiled
    else:
        if node.split == VERTICAL:
            split_x = x + w * node.ratio
            bsp_arrange_node(node.first, x, y, split_x - x, h)
            bsp_arrange_node(node.second, split_x, y, x + w - split_x, h)
        else:  // HORIZONTAL
            split_y = y + h * node.ratio
            bsp_arrange_node(node.first, x, y, w, split_y - y)
            bsp_arrange_node(node.second, x, split_y, w, y + h - split_y)
```

### Gaps

UWM has an `INNER_GAP` config option (default: 4px). The gap is applied as padding inside each leaf's rectangle:

```
+-------+-------+
|  gap  |       |
| +---+ |       |
| |   | |       |
| +---+ |       |
|  gap  |       |
+-------+-------+
|       |       |
|       |       |
|       |       |
|       |       |
+-------+-------+
```

The gap is subtracted from each leaf's dimensions. Adjacent leaves each contribute their own gap, resulting in a 2*gap separation between windows.

---

## 7.2 Directional Focus Navigation

### The Problem

The user presses MOD+left. Which window gets focus? In a BSP layout, "left" means "the window whose center is to the left of the current window's center, and is the closest in that direction."

### The Algorithm

```
bsp_focus_leaf_at(leaves, count, cx, cy, direction):
    best = NULL
    best_dx = INF, best_dy = INF
    
    for each leaf in leaves:
        lcx = leaf.x + leaf.width / 2
        lcy = leaf.y + leaf.height / 2
        
        dx = lcx - cx
        dy = lcy - cy
        
        if direction == LEFT and dx >= 0: continue
        if direction == RIGHT and dx <= 0: continue
        if direction == UP and dy >= 0: continue
        if direction == DOWN and dy <= 0: continue
        
        if direction == LEFT or direction == RIGHT:
            adx = abs(dx)
            if adx < best_dx or (adx == best_dx and abs(dy) < best_dy):
                best = leaf
                best_dx = adx
                best_dy = abs(dy)
        else:  // UP or DOWN
            ady = abs(dy)
            if ady < best_dy or (ady == best_dy and abs(dx) < best_dx):
                best = leaf
                best_dy = ady
                best_dx = abs(dx)
    
    return best
```

The heuristic: find the leaf whose center is in the given direction and is closest. If multiple leaves are at the same distance in the primary axis, prefer the one closest in the secondary axis.

### Complexity

O(n) where n is the number of leaves. UWM collects all leaves via `bsp_collect_leaves()` which walks the entire tree (also O(n)). For typical desktop usage (10-20 windows), this is fast. For 1000 windows, it would be slow (~2000 tree operations per directional focus command).

### Alternative Approaches

- **BSP tree walk:** Walk the tree structure to find the next leaf in a direction without collecting all leaves. More complex but O(log n).
- **Grid-based:** Maintain a grid of occupied cells and compute adjacency. Used by dwm and similar.
- **History-based:** Focus the previously focused window instead of directional navigation. Simpler but less predictable.

UWM uses the collect-all-leaves approach because it's simple, correct, and fast enough for the expected window count.

---

## 7.3 Monocle Layout

Monocle mode displays all windows stacked on top of each other, maximized. Only the focused window is visible; all others are hidden.

```
bsp_arrange_node(node, x, y, w, h):
    ...
    if node.mode == UWM_NODE_MONOCLE:
        // Only show the active child
        if node == active_child:
            arrange fullscreen
        else:
            hide
```

When monocle is activated via `toggle_monocle()`:
1. Collect all tiled leaves
2. Hide all leaves except the focused one
3. The focused leaf is positioned at the full output rectangle (minus gaps)

When the user changes focus (via directional navigation or mouse click), the previously focused window is hidden and the newly focused one is shown.

**Complexity:** O(n) to collect leaves and hide/show.

---

## 7.4 Complexity Summary

| Operation | Complexity | Notes |
|---|---|---|
| bsp_insert | O(log n) tree walk + O(1) restructure | n = tree depth |
| bsp_remove | O(log n) tree walk + O(1) restructure | n = tree depth |
| bsp_arrange | O(n) tree walk | n = all nodes |
| bsp_focus_* | O(n) collect + O(n) scan | n = leaves |
| bsp_swap | O(n) collect + O(n) scan | n = leaves |
| bsp_resize | O(1) ratio update + O(n) arrange | n = all nodes |
| bsp_rotate_split | O(1) flip + O(n) arrange | n = all nodes |
| toggle_monocle | O(n) collect + O(n) hide/show | n = leaves |
| toggle_floating | O(n) arrange | n = all nodes |

---

## Exercises

1. Draw the BSP tree for a layout with 4 windows arranged in a 2x2 grid. Label each node with its split direction and ratio.
2. Insert a 5th window into the layout. Redraw the tree.
3. Remove one of the windows. Redraw the tree.
4. What happens to the layout if you insert 10 windows all in the same direction (e.g., all VERTICAL splits)?
5. The directional focus algorithm uses center-point distance. What are the failure cases? When does it select the "wrong" window?

---

## Interview Questions

**Q (Mid):** Compare BSP tree with i3's split containers. What are the tradeoffs?
**A:** BSP trees always split the focused container in half, creating a strict binary tree. i3 adds the new window to the current container as a child. BSP is more predictable (the focused window always gives up exactly half its space) but can create narrow slivers with repeated splits. i3 allows tabbed/stacked containers and is more flexible. BSP's implementation is simpler (no container concept, no tabbed layout logic). BSP's tree is always binary and balanced by the arrangement pass; i3's tree can have arbitrary fan-out.

**Q (Senior):** How would you implement a "layout memory" feature where windows remember their position in the BSP tree?
**A:** Add a hash table mapping window identifier (app_id + title) to BSP position (sibling, direction, ratio, depth). On window creation, look up the hash table and restore the position instead of inserting at the focused leaf. On window close, save the position. The challenge is that the BSP tree changes as windows are added/removed, so saved positions may be invalid (the sibling window may no longer exist). UWM's current approach (always inserting at the focused leaf) is simpler and more predictable.

**Q (Principal):** The BSP pool has 512 nodes. For a window manager, that's absurdly high. But the allocation pattern is designed for 20 windows. Why not use a smaller pool?
**A:** Each window requires at least 2 nodes (leaf + internal parent). At 20 windows, that's ~40 nodes. 512 is intentionally generous to: (1) handle extreme cases (user opens 200 windows in a stress test — the pool doesn't overflow), (2) allow future features (tabbed containers, split containers, workspace previews) that use more nodes per window, (3) eliminate the need for overflow handling code (malloc fallback). The memory cost is 512 * sizeof(uwm_bsp_node) ~= 512 * 72 bytes = 36KB. For a compositor that has access to gigabytes of RAM, 36KB is trivial for the guarantee that BSP operations never fail.

---

*This concludes Part 7. Part 8 covers rendering in depth.*

---

# Part 8: Rendering

## Learning Objectives

After completing this part, you will be able to:
- Explain the scene graph and why it exists
- Describe damage tracking and frame callbacks
- Trace the rendering pipeline from commit to pixel
- Understand buffer lifecycle and surface trees

## 8.1 The Scene Graph

### The Problem

A compositor renders multiple surfaces onto one output. Each surface has:
- A position (x, y) in the output coordinate space
- A size (width, height)
- A buffer containing the actual pixel data
- A z-order (which surface is on top of which)
- A scale and transform (for HiDPI, rotated displays)
- An opacity (for alpha blending)
- A damage region (which pixels changed since last frame)

The compositor must composite all visible surfaces in z-order, applying transforms, scales, and opacity. It must do this efficiently — only redrawing the damaged regions.

### The Scene Graph Solution

The scene graph is a tree structure where:
- **Tree nodes** are invisible containers that group children
- **Leaf nodes** represent renderable content (surfaces, buffers, rectangles)

```
wlr_scene (root)
  ├── bg_color (rect node, solid background)
  ├── tiled_layer
  │   ├── surface_1
  │   │   ├── subsurface_1 (popup menu text)
  │   │   └── subsurface_2 (cursor in text field)
  │   └── surface_2
  ├── floating_layer
  │   └── surface_3
  ├── layer_top
  │   └── bar_surface (ubar)
  └── cursor_layer
      └── cursor_buffer
```

Each scene node has a **bounding box** in the output's coordinate space. When the scene graph renders, it:
1. Walks the tree in depth-first, front-to-back order
2. For each visible node, composites its content onto the output buffer
3. Applies transforms, scales, and opacity via OpenGL when necessary
4. Tracks which pixels were touched (for damage tracking)

### Why wlroots' Scene Graph Exists

Before the scene graph was part of wlroots (it was added in wlroots 0.14, replacing the old renderer interface), compositors had to manage their own render lists. This meant:
- Maintaining a sorted list of renderable objects
- Manually clipping and transforming each surface
- Implementing damage tracking from scratch
- Handling output scaling and transforms manually

The scene graph centralizes all of this. Compositors just reparent scene nodes; wlroots handles rendering.

### How UWM Uses the Scene Graph

UWM creates scene trees at startup:
```c
server.scene = wlr_scene_create();
server.tiled_layer = wlr_scene_tree_create(server.scene);
server.floating_layer = wlr_scene_tree_create(server.scene);
```

Per output, UWM creates layer trees:
```c
output->layer_background = wlr_scene_tree_create(server.scene);
output->layer_bottom = wlr_scene_tree_create(server.scene);
// ... more layers
```

Per window, UWM creates a scene surface node in the appropriate layer:
```c
toplevel->scene_tree = wlr_scene_xdg_surface_create(
    toplevel->server->tiled_layer, toplevel->xdg_toplevel->base);
```

When a window moves or resizes, UWM just calls:
```c
wlr_scene_node_set_position(toplevel->scene_tree, x, y);
// No rendering code needed — the scene graph handles it.
```

When a window is hidden (workspace switch, monocle):
```c
wlr_scene_node_set_enabled(toplevel->scene_tree, false);
// The scene graph skips it during rendering.
```

---

## 8.2 Damage Tracking

### The Problem

Rendering every pixel of every output at 60fps is expensive. A 4K monitor at 60fps requires compositing 498 MB/s of pixel data. Instead of re-rendering everything, the compositor should only redraw the parts that changed.

### How Damage Tracking Works

Each surface reports its **damage region** — the set of rectangles that changed since the last commit. The compositor accumulates damage from all surfaces and computes the total region that needs to be re-rendered.

```
Frame 1:
  Surface A draws at (0, 0, 100, 100)
  Surface B draws at (200, 0, 100, 100)
  Damage: [0,0,100,100] U [200,0,100,100] = two rectangles

Frame 2:
  Surface C appears at (50, 50, 50, 50)
  Damage: [50,50,50,50]

The compositor renders only the damaged rectangles, using the previous
frame's pixels for the rest of the output.
```

### wlr_scene_output_commit Damage

When UWM calls `wlr_scene_output_commit(scene_output)`, wlroots:
1. Computes the accumulated damage from all surfaces
2. Transforms damage regions from surface coordinates to output coordinates
3. Renders only the damaged parts of the output
4. Submits the final buffer to KMS with the damage region

KMS uses the damage region to optimize display refresh. Some hardware supports **damage-based partial update** where only damaged pixels are sent over the display link. Most hardware ignores it and refreshes the full frame anyway.

### UWM's Full-Surface Damage

In `tools/ubar/src/render.c`, UBar's compositor-side rendering always damages the full surface:
```c
wl_surface_damage(state->surface, 0, 0, state->width, state->height);
```

The comment explains: "Full damage is the safe choice for a 30px-high bar." The partial damage tracking code (which was removed in the audit) compared previous zone positions and tried to damage only changed zones. This was unnecessary because the bar is only 30px tall — rendering the full bar is trivially fast. The partial damage code added complexity for no measurable benefit.

### Tradeoff

Full-surface damage is simpler and prevents bugs where damaged regions are incorrectly calculated (leading to "ghosting" artifacts). Partial damage saves GPU fill rate but requires careful tracking. For a 30px bar, full damage is fine. For a compositor compositing 4K surfaces, partial damage is essential.

---

## 8.3 Frame Callbacks

### The Problem

A client renders frames at its own pace. A video player renders 24fps, a terminal renders only when new text appears, a game renders at 144fps. The compositor renders at the monitor's refresh rate (e.g., 60fps). The client needs to know when the compositor has displayed a frame so it can submit the next one.

### The Solution: wl_callback

When a client creates a `wl_surface`, it can call `wl_surface_frame()` to get a `wl_callback` object. The compositor sends a `done` event on this callback after the frame has been displayed.

```
Client                     Compositor
  |                            |
  |-- wl_surface_frame() ---->|
  |     (returns callback)     |
  |                            |
  |-- wl_surface_commit() --->|
  |                            |
  |      ... compositor renders, KMS page flips ...
  |                            |
  |<- callback.done(time) -----|
  |                            |
  |-- wl_surface_commit() --->|
  |     (next frame)           |
```

The client uses the callback to pace its rendering. If the client renders at 144fps on a 60Hz monitor, it will only receive 60 callbacks per second, naturally throttling its frame rate.

### Frame Callbacks in UWM

UWM creates a frame callback for each output:
```c
output_frame():
    wlr_scene_output_commit(output->scene_output);
    wlr_scene_output_send_frame_done(output->scene_output);
```

`wlr_scene_output_send_frame_done()` sends the `done` event on all pending frame callbacks for all surfaces on that output. This tells every client "your frame has been displayed; you can submit the next one."

### UWM's Own Frame Callback

In `render_frame()` in UBar, the bar creates its own frame callback:
```c
state->frame_callback = wl_surface_frame(state->surface);
wl_callback_add_listener(state->frame_callback, &frame_listener, state);
state->frame_pending = true;
```

When the `frame_listener.done` callback fires, the bar knows it can submit another frame. This prevents the bar from submitting frames faster than the compositor can display them.

---

## 8.4 Buffer Lifecycle

### Triple Buffering

UWM (and UBar) use triple buffering: 3 `struct pool_buffer` objects per output. The three buffers cycle through states:

```
Buffer 0: FRONT  (being scanned out by KMS)
Buffer 1: MIDDLE (ready for scanout, waiting for next vblank)
Buffer 2: BACK   (being rendered into by compositor)
```

After vblank:
```
Buffer 1: FRONT  (KMS starts scanning this out)
Buffer 2: MIDDLE (ready)
Buffer 0: BACK   (compositor renders into this)
```

Triple buffering prevents the "buffer contention" problem:
- With single buffering: compositor must wait for vblank before rendering (blocking).
- With double buffering: compositor can render into the back buffer while front is shown, but if the compositor starts rendering before the previous frame is displayed, it overwrites the back buffer (tearing or blocking).
- With triple buffering: there is always a free buffer to render into. The compositor never blocks.

### Pool Buffer Management

UBar's `get_next_buffer()`:
```c
for (int i = 0; i < 3; i++) {
    if (!state->bufs[i].busy) { buf = &state->bufs[i]; break; }
}
if (!buf) return NULL;  // all buffers busy (shouldn't happen with triple buffering)
```

Each buffer has a `busy` flag. The buffer is marked busy when submitted to wl_surface_attach. The `release` callback from wlroots marks it not-busy:
```c
static void buffer_handle_release(void *data, struct wl_buffer *wl_buffer) {
    struct pool_buffer *buf = (struct pool_buffer *)data;
    buf->busy = false;
}
```

### Shared Memory Buffer Creation

For SHM buffers (used by UBar and simple clients):
```
1. shm_open() -> creates a memfd
2. ftruncate() -> sets size
3. mmap() -> maps into process address space
4. wl_shm_create_pool() -> wraps the fd in a Wayland SHM pool
5. wl_shm_pool_create_buffer() -> allocates a buffer from the pool
6. wl_shm_pool_destroy() -> pool is no longer needed
7. close(fd) -> file descriptor no longer needed (pool handles it)
8. cairo_image_surface_create_for_data() -> wraps the buffer for Cairo rendering
```

---

## 8.5 Surface Trees

### Subsurfaces

An xdg_surface can have subsurfaces — child surfaces that are positioned relative to the parent. For example, a video player may have:
- Main surface: the video frame
- Subsurface: the control bar overlay
- Subsurface: the subtitle text

Subsurfaces have their own buffer, position, and damage. They are synchronized with the parent surface via `wl_subsurface_commit()`.

### How wlroots Handles Surface Trees

`wlr_scene_xdg_surface_create()` automatically creates scene nodes for all subsurfaces of the xdg_surface. When the client adds a subsurface, wlroots creates a scene node in the parent's scene tree. The compositor doesn't need to manage subsurfaces — wlroots handles the tree.

### Popup Trees

A popup (xdg_popup) is a child of an xdg_surface. Popups can have child popups (nested menus). Each popup gets its own scene tree node in the floating layer. The position of the popup is computed by wlroots based on the parent surface's position and the popup's placement rules.

---

## Exercises

1. What happens if a client submits frames faster than the monitor's refresh rate? What happens if it submits slower?
2. Draw the buffer lifecycle for a single frame from commit to scanout.
3. Why does UWM use full-surface damage instead of partial damage? When would partial damage be worth the complexity?
4. Trace `wlr_scene_output_commit()`. What does it do? (Read the wlroots source.)

---

## Interview Questions

**Q (Junior):** What is a frame callback?
**A:** A Wayland mechanism where a client requests notification when the compositor has displayed a frame. The client creates a `wl_callback` via `wl_surface_frame()` and receives a `done` event when the frame is on screen. This allows the client to pace its rendering to the monitor's refresh rate.

**Q (Mid):** Explain triple buffering in a compositor.
**A:** The compositor maintains three buffers per output: front (currently scanned out by KMS), middle (ready for scanout), and back (being rendered into). After vblank, the middle buffer becomes the front buffer, the back buffer becomes the middle buffer, and the previously front buffer becomes the new back buffer. This ensures the compositor always has a buffer to render into, avoiding blocking on KMS.

**Q (Senior):** What is the difference between buffer age and damage tracking?
**A:** Buffer age is the number of frames since a buffer was first used. It's used by the compositor to determine how much of the frame needs to be redrawn. Age=1 means "this buffer is new, redraw everything." Age>1 means "previous content is still valid, only apply damage." Damage tracking tracks specific rectangles that changed. The compositor combines both: on age>1, only redraw damaged regions; on age=1, redraw everything. UWM's scene graph handles this internally via wlr_scene_output_commit.

---

# Part 9: Input

## Learning Objectives

After completing this part, you will be able to:
- Trace a keystroke from kernel to action
- Explain the cursor mode state machine
- Describe how key repeat works
- Understand modifier caching and XKB

## 9.1 The Keyboard Pipeline

### evdev

The kernel exposes input devices as event devices (`/dev/input/event*`). Each event device generates `struct input_event` records:

```c
struct input_event {
    struct timeval time;  // timestamp
    __u16 type;           // EV_KEY, EV_REL, EV_ABS, EV_SYN
    __u16 code;           // KEY_A, KEY_ENTER, REL_X, ABS_X
    __s32 value;          // 0=release, 1=press, 2=repeat
};
```

### libinput Processing

libinput reads evdev events and provides a higher-level API:
- Converts raw key events to libinput events
- Handles device quirks (swap buttons, invert scroll)
- Provides gesture recognition (swipe, pinch, tap)
- Provides configuration interface (tap-to-click, natural scroll, pointer acceleration)

### wlr_seat Routing

wlroots' backend receives libinput events and routes them to the seat:
```
libinput_event_keyboard -> wlr_keyboard -> wlr_seat -> keyboard_handle_key()
```

### XKB Keycode Resolution

`keyboard_handle_key()` in `input.c`:
```c
xkb_state_key_get_syms(keyboard->xkb_state, keycode, &syms);
```

This converts the hardware keycode to an XKB keysym (e.g., KEY_A -> XKB_KEY_A). The keycode depends on the keymap; the keysym depends on both the keycode and the current modifier/layout state.

### The Modifier State Machine

```
Physical key press:
    |
    v
Update XKB state (press keycode)
    |
    v
Check if any modifiers changed
    |
    v
Cached modifier state (ctrl, alt, logo, shift)
    |
    v
Dispatch keybinding:
    if modifier is pressed and keysym matches:
        execute action
    else:
        forward to client
```

UWM caches modifier states (`cached_ctrl`, `cached_alt`, `cached_logo`, `cached_shift`) to avoid calling `wlr_keyboard_get_modifiers()` repeatedly. These are updated in `keyboard_handle_modifiers()`.

### Key Repeat

UWM implements its own key repeat instead of using the kernel's input layer repeat (which some keyboards don't support reliably):

```
keyboard_handle_key():
    if key pressed and not already repeating:
        start_repeat_timer(keyboard, sym, keycode)
    if key released:
        stop_repeat_timer(keyboard)

repeat_timer fires:
    dispatch keybinding again with same sym/keycode
    reset timer for next repeat
```

The repeat parameters:
```c
KEY_REPEAT_DELAY = 250ms   // delay before repeat starts
KEY_REPEAT_RATE  = 55Hz    // repeat frequency (was 40Hz, increased to 55Hz)
```

The repeat timer is created via `wl_event_loop_add_timer()`. When it fires, the handler calls itself recursively with the same key data.

### Why Custom Repeat?

The kernel's key repeat (via the `kbd` driver) only works for the Linux console. In a Wayland compositor, the compositor receives raw key events from libinput — the kernel's repeat processing is bypassed. The compositor must implement its own repeat.

The repeat rate was changed from 40Hz to 55Hz because 40Hz felt sluggish when holding down navigation keys (like MOD+left to move through windows quickly). 55Hz matches the typical vblank rate on 60Hz monitors (every other frame) and provides smoother cursor-only navigation.

---

## 9.2 The Pointer Pipeline

### Motion Events

```
Mouse moves
    |
    v
evdev: REL_X=3, REL_Y=5
    |
    v
libinput: dx=3, dy=5 (acceleration applied)
    |
    v
wlr_cursor: accumulates deltas, updates position
    |
    v
UWM: server_cursor_motion() -> process_cursor_motion()
```

### The Cursor Mode State Machine

```
enum uwm_cursor_mode { PASSTHROUGH, MOVE, RESIZE }

state transitions:

startup: PASSTHROUGH

MOD + left-click on window:
    PASSTHROUGH -> MOVE  (drag window)
    PASSTHROUGH -> RESIZE (resize window, depending on click position)

button release (after MOVE/RESIZE):
    MOVE/RESIZE -> PASSTHROUGH

ESC key pressed (cancel):
    MOVE/RESIZE -> PASSTHROUGH
```

### process_cursor_motion()

```c
switch (server->cursor_mode) {
case MOVE:
    process_cursor_move(server, &cursor_event);
    break;
case RESIZE:
    process_cursor_resize(server, &cursor_event);
    break;
case PASSTHROUGH:
    // Find surface under cursor
    double sx, sy;
    struct wlr_surface *surface = desktop_toplevel_at(server, cx, cy, &sx, &sy);
    
    // Focus window if different
    if (surface && server->focus_follows_pointer) {
        focus_toplevel(server, surface);
    }
    
    // Forward to seat
    wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
    break;
}
```

### process_cursor_move()

```c
void process_cursor_move(struct uwm_server *server, ...) {
    struct uwm_toplevel *toplevel = server->grabbed_toplevel;
    double dx = cx - server->grab_x;
    double dy = cy - server->grab_y;
    
    toplevel->float_x = server->grab_geobox.x + dx;
    toplevel->float_y = server->grab_geobox.y + dy;
    
    wlr_scene_node_set_position(toplevel->scene_tree,
        toplevel->float_x, toplevel->float_y);
}
```

### process_cursor_resize()

```c
void process_cursor_resize(struct uwm_server *server, ...) {
    struct uwm_toplevel *toplevel = server->grabbed_toplevel;
    double dx = cx - server->grab_x;
    double dy = cy - server->grab_y;
    
    struct wlr_box new_geo = server->grab_geobox;
    
    if (server->resize_edges & WLR_EDGE_LEFT)   new_geo.x += dx, new_geo.width -= dx;
    if (server->resize_edges & WLR_EDGE_RIGHT)  new_geo.width += dx;
    if (server->resize_edges & WLR_EDGE_TOP)    new_geo.y += dy, new_geo.height -= dy;
    if (server->resize_edges & WLR_EDGE_BOTTOM) new_geo.height += dy;
    
    // Enforce minimum size
    if (new_geo.width  < FLOATING_MIN_WIDTH)  ...
    if (new_geo.height < FLOATING_MIN_HEIGHT) ...
    
    // Update toplevel geometry
    toplevel->float_x = new_geo.x;
    toplevel->float_y = new_geo.y;
    toplevel->float_width = new_geo.width;
    toplevel->float_height = new_geo.height;
    
    // Send configure to client
    wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, new_geo.width, new_geo.height);
}
```

---

## 9.3 Hit Testing

### desktop_toplevel_at()

```c
struct wlr_surface *desktop_toplevel_at(struct uwm_server *server,
        double cx, double cy, double *sx, double *sy) {
    // Walk scene graph at cursor position
    struct wlr_scene_buffer *scene_buffer = wlr_scene_node_at(
        &server->scene->node, cx, cy, sx, sy, NULL);
    
    if (!scene_buffer) return NULL;
    
    struct wlr_scene_surface *scene_surface =
        wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface) return NULL;
    
    struct wlr_surface *surface = scene_surface->surface;
    
    // Skip layer surfaces and subsurfaces (handled by layer_shell protocol)
    if (wlr_layer_surface_v1_try_from_wlr_surface(surface)) return NULL;
    if (wlr_subsurface_try_from_wlr_surface(surface)) {
        surface = wlr_surface_get_root_surface(surface);
    }
    
    // Find the owning uwm_toplevel
    return surface;
}
```

This is O(1) for the scene graph lookup (scene nodes are spatially indexed), plus O(depth) for walking the scene tree to find the specific surface at the point.

---

## 9.4 Gestures

UWM supports pointer gestures (swipe, pinch, hold) via `wlr_pointer_gestures_v1`:

```c
server_cursor_swipe_begin():
    wlr_pointer_gestures_v1_send_swipe_begin(
        server->pointer_gestures, seat, time, fingers);

server_cursor_pinch_begin():
    wlr_pointer_gestures_v1_send_pinch_begin(
        server->pointer_gestures, seat, time, fingers);
```

These are forwarded to the focused client. The compositor doesn't interpret gestures — they're passed through to clients that understand them (e.g., a browser's two-finger swipe for back/forward).

---

## Exercises

1. Trace the key event for Ctrl+Alt+T (terminal launch). What happens at each stage: kernel, libinput, wlr_seat, UWM input handler, keybinding dispatch?
2. Why does UWM cache modifier states instead of calling `wlr_keyboard_get_modifiers()` on every keystroke?
3. What happens to the cursor mode state machine if `process_cursor_move()` is called but `server->grabbed_toplevel` is NULL? Is this bug possible?
4. How would you implement touchscreen input support? What events would need to be handled?

---

## Interview Questions

**Q (Mid):** How does XKB keycode-to-keysym translation work?
**A:** The kernel generates a scancode from the physical key press. The compositor loads an XKB keymap (from `xkb_file` or compiled rules) that maps scancodes to keycodes in the first level, then uses `xkb_state_key_get_syms()` to convert keycodes to keysyms based on the current XKB state (modifiers, layout group, compose sequences).

**Q (Senior):** Why does UWM implement its own key repeat instead of using libinput's?
**A:** libinput does provide key repeat configuration, but it's not universally reliable across all keyboards and drivers. UWM's custom repeat timer (via `wl_event_loop_add_timer`) is deterministic, works with any input device, and allows precise control over delay and rate. The 55Hz rate was chosen empirically to feel responsive without being too fast.

**Q (Senior):** Describe a situation where `process_cursor_motion()` finds a different surface than the one `focus_toplevel()` focuses. What causes this?
**A:** Layer-shell surfaces (like the bar or a notification popup) may intercept the pointer, but the focus should remain on the workspace window below. `desktop_toplevel_at()` returns the surface at the cursor position, but if it's a layer-shell surface, focus should be on the keyboard-interactive window (e.g., the focused terminal). UWM handles this by checking if the surface is a layer surface and only focusing workspace windows in `focus_toplevel()`.

---

# Part 10: Outputs

## Learning Objectives

After completing this part, you will be able to:
- Explain output lifecycle and hotplug handling
- Describe workspace assignment across outputs
- Understand mirroring and extending modes

## 10.1 Output Lifecycle

### Output Creation

When a monitor is connected (or at startup), the wlr_backend emits `new_output`:

```
server_new_output():
    1. wlr_output_init_render(output)  // prepare for rendering
    2. Set preferred mode (or custom mode)
    3. Create struct uwm_output (calloc)
    4. Create per-layer scene trees (background, bottom, floating, top, overlay, lock)
    5. Assign workspace (first output = workspace 0, second = subsequent)
    6. Add to output_layout (auto-position)
    7. Wire frame, request_state, destroy listeners
    8. layer_surface_arrange(output)  // arrange any layer surfaces
    9. workspace_show_on_output(output, current_workspace)
    10. Try to focus a window on the new output
```

### Frame Event

The wlr_output emits `frame` at the monitor's refresh rate. This is the signal to render:

```
output_frame():
    1. if output is disabled: return
    2. if session is inactive: return (VT switched away)
    3. wlr_scene_output_commit(output->scene_output)
       (renders all visible surfaces to output buffer)
    4. wlr_scene_output_send_frame_done(output->scene_output)
       (notifies clients their frames were displayed)
```

### Output Destruction

When a monitor is disconnected:

```
output_destroy():
    1. Remove frame listener
    2. Invalidate layer surface output references
       (layer surfaces that were on this output lose their output)
    3. Evacuate windows:
       For each workspace on this output:
           move all windows to first remaining output's workspace
    4. Destroy per-layer scene trees
    5. wlr_output_layout_remove(output->wlr_output)
    6. Remove output from server list
    7. uwm_bar_send_output()  // update bar
    8. free(output)
```

---

## 10.2 Workspace Assignment

Each output displays one workspace at a time. The mapping is:

```
Output 0 (eDP-1, laptop): workspace 0
Output 1 (HDMI-A-1, external): workspace 1
Output 0 (eDP-1): workspace 2 (after workspace_next)
```

When a workspace is switched on an output, windows from the old workspace are hidden, and windows from the new workspace are shown. If the new workspace was already shown on another output, it is moved (the other output switches to a fallback workspace).

### workspace_show_on_output / workspace_hide_from_output

```c
// Show all windows of a workspace on an output
void workspace_show_on_output(struct uwm_workspace *ws, struct uwm_output *output) {
    struct uwm_toplevel *toplevel;
    wl_list_for_each(toplevel, &ws->toplevels, workspace_link) {
        wlr_scene_node_set_enabled(toplevel->scene_tree, true);
    }
    wl_list_for_each(toplevel, &ws->floating_windows, floating_link) {
        wlr_scene_node_set_enabled(toplevel->scene_tree, true);
    }
}

// Hide all windows of a workspace
void workspace_hide_from_output(struct uwm_workspace *ws, struct uwm_output *output) {
    struct uwm_toplevel *toplevel;
    wl_list_for_each(toplevel, &ws->toplevels, workspace_link) {
        wlr_scene_node_set_enabled(toplevel->scene_tree, false);
    }
    wl_list_for_each(toplevel, &ws->floating_windows, floating_link) {
        wlr_scene_node_set_enabled(toplevel->scene_tree, false);
    }
}
```

Scene nodes are disabled (not destroyed) when hidden. This preserves the window's state and avoids re-creating scene nodes on workspace switch.

---

## 10.3 Hotplug Handling

### How Hotplug Events Reach the Compositor

1. Kernel detects HDMI/DP cable event
2. Kernel sends udev event
3. wlr_backend (via udev monitor) receives event
4. wlr_backend calls wlr_output_update() for the affected output
5. If new output: backend emits `new_output` signal
6. If disconnected: backend emits `destroy` signal on the output

### UWM's Hotplug Script

UWM delegates monitor configuration (enable/disable, resolution, position, mirror/extend) to an external script: `~/.config/custom_scripts/hdmi.sh`.

The script communicates with the compositor via `wlr-randr` (or the wlr-output-management protocol):

```sh
#!/bin/sh
# ~/.config/custom_scripts/hdmi.sh
case "${1:-}" in
    mirror)
        wlr-randr --output HDMI-A-1 --mode 1920x1080 --pos 0,0
        wlr-randr --output eDP-1 --mode 1920x1080 --pos 0,0
        ;;
    extend)
        wlr-randr --output HDMI-A-1 --mode 1920x1080 --pos 1920,0
        wlr-randr --output eDP-1 --mode 1920x1080 --pos 0,0
        ;;
    reset)
        wlr-randr --output HDMI-A-1 --off
        wlr-randr --output eDP-1 --mode 1920x1080 --pos 0,0
        ;;
esac
```

### Why an External Script?

- The compositor doesn't need to know about `wlr-randr` or monitor geometry
- Users can customize the behavior without recompiling
- Complex logic (like EDID parsing, preferred monitor layout) is kept out of the compositor
- The script can be replaced with GUI tools like `wdisplays` or `wlr-randr` directly

---

## 10.4 Mirror and Extend

### Mirror Mode

Both outputs show the same content. In UWM:

```
Mirror configuration:
    Output 0: 1920x1080 @ (0, 0)
    Output 1: 1920x1080 @ (0, 0)

The output_layout has both outputs at the same position.
The scene graph renders the same scene to both outputs.
```

The scene graph is attached to the output layout. When both outputs have the same layout position, the scene graph renders identically on both. Each output independently commits its own buffer, but the scene graph content is the same.

### Extend Mode

Each output shows a different portion of the workspace:

```
Extend configuration:
    Output 0: 1920x1080 @ (0, 0)
    Output 1: 1920x1080 @ (1920, 0)

Total virtual space: 3840x1080.
Output 0 shows the left half, Output 1 shows the right half.
```

Windows can be dragged between outputs. Each output has its own workspace (they are independent). The output layout determines the coordinate space — windows at x=1000 appear on Output 0, windows at x=2000 appear on Output 1.

### The get_output_size Bug

There was a bug where `get_output_size()` used the output's position from the layout to compute the BSP arrangement rectangle. In extend mode, the second output's position is (1920, 0), and `get_output_size()` was using `output->wlr_output->width` and `output->wlr_output->height` without considering the offset. This caused BSP arrangement to place windows at positions starting from (1920, 0) in scene coordinates, but the scene node positions should start from (0, 0) for each output's local coordinate system.

The fix: compute the output's usable area in global coordinates:
```c
static struct wlr_box get_output_size(struct uwm_output *output) {
    struct wlr_box box;
    wlr_output_layout_get_box(output->server->output_layout,
        output->wlr_output, &box);
    // Adjust for layer-shell exclusive zones
    box.x += output->usable_area.x;
    box.y += output->usable_area.y;
    box.width = output->usable_area.width;
    box.height = output->usable_area.height;
    return box;
}
```

This correctly returns the global position (e.g., x=1920 for the second output in extend mode) and the usable area after layer-shell exclusive zones.

---

## Exercises

1. What happens to fullscreen windows when you switch workspaces?
2. Draw the scene graph for a two-monitor setup with different workspaces on each monitor.
3. What happens if you disconnect an external monitor while windows are on its workspace? Where do the windows go?
4. Why does UWM use per-output scene trees for layers instead of global scene trees?

---

## Interview Questions

**Q (Mid):** What happens to a window when its output is disconnected?
**A:** UWM's `output_destroy()` evacuates all windows from the disconnected output's workspaces to the first remaining output's corresponding workspaces. If no outputs remain (rare — laptops always have eDP-1), the windows have no output to display on. Their scene nodes exist but aren't attached to any output. When a new output is connected, the windows will appear on it.

**Q (Senior):** How does mirror mode work with different monitor resolutions?
**A:** The output layout forces both outputs to the same layout position. The scene graph renders at each output's native resolution independently. The renderer scales the scene content to fit each output's buffer. If output 0 is 1920x1080 and output 1 is 3840x2160 (4K), the scene graph renders once per output at each output's resolution. The content appears larger on the 4K monitor (because the same content is stretched over more pixels). This is not true pixel-perfect mirroring — the 4K monitor shows the same content at higher resolution.

**Q (Principal):** You need to support VRR (Variable Refresh Rate) on a FreeSync/GSync monitor. How would you modify UWM?
**A:** VRR changes the frame callback mechanism. Instead of rendering at a fixed rate, the compositor renders when new content arrives (client commits) or when the cursor moves. The output would call `wlr_scene_output_commit()` on demand, not on a timer. wlroots supports VRR via `wlr_output_set_custom_mode()`. VRR support requires changes to: (1) frame scheduling — render on demand instead of periodic, (2) damage tracking — only render when something changes, (3) idle handling — render at minimum rate (e.g., 30Hz) when content is static to prevent flicker. The `wp_fractional_scale_manager_v1` protocol may intersect with VRR in future Wayland versions.

---

*This concludes Part 10. Parts 11-16 cover specialized topics: Layer Shell, Screen Sharing, Performance, Debugging, Security, and Future.*

---

# Part 11: Layer Shell

## Learning Objectives

After completing this part, you will be able to:
- Explain the layer-shell protocol and why it exists
- Describe exclusive zones and how they affect usable area
- Understand the z-ordering of layers
- Trace the lifecycle of a layer surface in UWM

## 11.1 The Layer Shell Protocol

### The Problem

Traditional X11 applications use override-redirect windows for panels, bars, and notifications. These windows bypass the window manager and are positioned directly by the application. In Wayland, there is no window manager to bypass — the compositor controls all window positioning. There needs to be a protocol for applications to say: "I am a bar, put me at the top of the screen" or "I am a notification, put me in the top-right corner."

### The Solution: wlr-layer-shell

The `wlr-layer-shell-unstable-v1` protocol defines:
- **Layers:** background, bottom, top, overlay (in increasing z-order)
- **Anchors:** top, bottom, left, right (which edges to attach to)
- **Margins:** offset from anchored edges
- **Exclusive zones:** how much space the surface reserves (e.g., a bar at the top reserves its height)
- **Keyboard interactivity:** whether the surface accepts keyboard input

A panel connects to the compositor via this protocol:
```
Client:
    zwlr_layer_shell_v1_get_layer_surface(shell, surface, output,
        "top", "panel")
    zwlr_layer_surface_v1_set_anchor(surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)
    zwlr_layer_surface_v1_set_exclusive_zone(surface, 30)  // reserve 30px
    zwlr_layer_surface_v1_set_margin(surface, 0, 0, 0, 0)
    wl_surface_commit(surface)
```

The compositor arranges the surface: it's 30px tall, full width, at the top of the screen. The `exclusive_zone=30` means the compositor reserves 30px at the top, reducing the usable area for windows.

## 11.2 Exclusive Zones and Usable Area

### Two-Pass Arrangement

`layer_surface_arrange()` performs two passes:

**Pass 1: Exclusive surfaces.** Layer surfaces with `exclusive_zone > 0` are arranged first. They claim space from the output's edges:

```
+--------------------------------------------------+
| BAR (exclusive, 30px, top)                        |
+--------------------------------------------------+
|                                                    |
|  Usable area for windows                          |
|  (height = output_height - 30)                    |
|                                                    |
+--------------------------------------------------+
```

**Pass 2: Non-exclusive surfaces.** Layer surfaces with `exclusive_zone = 0` (like notifications or backgrounds) are arranged in the remaining space.

The calculation:
```c
void layer_surface_arrange(struct uwm_layer_surface *layer_surface) {
    struct uwm_output *output = layer_surface->output;
    struct wlr_box usable = {0, 0, output->wlr_output->width, output->wlr_output->height};
    
    // Arrange all exclusive surfaces first
    // For each exclusive surface at anchor TOP:
    //     usable.y += surface.height + surface.margin.top
    //     usable.height -= surface.height + surface.margin.top
    
    output->usable_area = usable;
    
    // Arrange non-exclusive surfaces in the remaining space
    // ...
    
    // Re-arrange BSP windows with new usable area
    bsp_arrange(output);
}
```

### Why Two Passes?

Exclusive surfaces (bars, docks) must be placed first because they determine the usable area. Non-exclusive surfaces (notifications, OSDs) float on top and don't affect window layout. If non-exclusive surfaces were arranged first, they might overlap exclusive surfaces, which looks wrong (a notification appearing behind the bar).

## 11.3 Layers and Z-Order

UWM's per-output scene trees for layers:

```
scene->node (root)
  ├── tiled_layer (BSP-tiled windows)
  ├── floating_layer (floating, fullscreen windows)
  └── output->layer_background    // wallpapers (e.g., swaybg)
      output->layer_bottom        // behind windows
      output->layer_floating      // same as floating_layer, for fullscreen
      output->layer_top           // over windows (bars, panels)
      output->layer_overlay       // top of everything (lock screen)
      output->layer_lock          // session lock (highest)
```

The scene tree's node ordering determines z-order. `wlr_scene_node_place_below()` and `wlr_scene_node_place_above()` reorder nodes.

### Why Per-Output Trees?

Each output has its own layer trees because:
1. Different outputs may have different bars (different panels per monitor)
2. Fullscreen windows on one output shouldn't be obscured by a bar on another output
3. Session lock surfaces are per-output

---

## 11.4 Keyboard Focus for Layer Surfaces

When a layer surface is keyboard-interactive (e.g., a launcher like fuzzel), it should receive keyboard focus. UWM handles this in `handle_map()`:

```c
void handle_map(struct wl_listener *listener, void *data) {
    struct uwm_layer_surface *surface = wl_container_of(listener, surface, map);
    
    if (surface->layer_surface->current.keyboard_interactive) {
        // Focus this layer surface
        struct wlr_surface *kb_surface = ...layer surface...;
        struct wlr_seat *seat = surface->output->server->seat;
        struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
        
        wlr_seat_keyboard_notify_enter(seat, kb_surface,
            wlr_keyboard_get_modifiers(keyboard));
    }
}
```

When the layer surface unmaps, focus returns to the workspace window:
```c
void handle_unmap(struct wl_listener *listener, void *data) {
    struct uwm_layer_surface *surface = wl_container_of(listener, surface, unmap);
    
    if (surface->layer_surface->current.keyboard_interactive) {
        // Restore focus to the workspace window
        struct uwm_workspace *ws = surface->output->current_workspace;
        if (ws->focused) focus_toplevel(ws->focused);
    }
}
```

---

## Exercises

1. A bar uses exclusive_zone=30 with anchor TOP and LEFT. A notification uses exclusive_zone=0 with anchor TOP|RIGHT. Where does each surface appear?
2. What happens if two bars both claim exclusive_zone at the top? How does UWM handle this?
3. Why does the session lock use its own layer tree instead of going in layer_overlay?
4. How would you implement a "picture-in-picture" window that floats above all others?

---

## Interview Questions

**Q (Mid):** Why does the layer-shell protocol exist instead of using xdg-shell with special flags?
**A:** xdg-shell is designed for regular application windows (tiled, floating, fullscreen). Layer-shell is designed for surfaces that are part of the desktop environment itself — bars, wallpapers, notifications, OSDs. These have different requirements: exclusive zones (to reserve space), anchor-based positioning, and layer-based z-ordering. Adding these to xdg-shell would complicate the protocol for regular applications.

**Q (Senior):** What happens to layer surfaces when an output is disconnected?
**A:** UWM's `output_destroy()` iterates all layer surfaces on the destroyed output and sets their `output` reference to NULL. The next time `layer_surface_arrange()` is called, layer surfaces without an output are skipped (they are not rendered). When a new output is connected, the layer surfaces are reassigned.

---

# Part 12: Screen Sharing

## Learning Objectives

After completing this part, you will be able to:
- Explain how PipeWire fits into the screen sharing pipeline
- Describe the ext-image-capture-copy protocol
- Trace a captured frame from compositor to browser

## 12.1 The Problem

A browser wants to capture the user's screen for a WebRTC video call. The browser is a Wayland client. It cannot read other clients' buffers (Wayland security model). There must be a protocol for screen capture.

## 12.2 The Solution: Multiple Protocols

Screen sharing involves:

1. **Portal (xdg-desktop-portal):** The user selects which screen/window/area to share via a UI dialog. The portal is a D-Bus service that mediates between applications and the compositor.

2. **PipeWire:** The compositor exports captured frames as PipeWire streams. The capturing application (browser, OBS) reads the PipeWire stream.

3. **DMA-BUF:** Frames are shared as DMA-BUF file descriptors — no copying, direct GPU memory access.

4. **Foreign Toplevel:** The portal lists available windows via the foreign-toplevel protocol. The user picks one.

## 12.3 PipeWire and DMA-BUF

```
Browser (WebRTC)
    |
    | (1) Request screen via xdg-desktop-portal
    v
xdg-desktop-portal (D-Bus service)
    |
    | (2) Request screen share from compositor via ext-image-capture-copy
    v
UWM (compositor)
    |
    | (3) On each frame, export DMA-BUF
    | (4) Send to PipeWire stream
    v
PipeWire
    |
    | (5) Browser reads PipeWire stream
    v
Browser encoder (VP8/VP9/H.264)
    |
    | (6) Send via WebRTC
    v
Remote participant
```

### ext-image-capture-copy-v1 (wlroots protocol)

This protocol allows the compositor to export frames as DMA-BUFs to a capture session:

```
Compositor creates wlr_ext_image_capture_source_v1 for each captured output/surface
    |
    v
On each committed frame (or periodically):
    wlr_ext_image_capture_source_v1_send_frame(source, dmabuf_fd, ...)
    |
    v
Capture client receives DMA-BUF, reads it via PipeWire
```

### wlr_export_dmabuf_manager_v1

This is the older wlroots protocol for exporting DMA-BUFs. It's being superseded by `ext-image-capture-copy-v1`. UWM creates both:

```c
server_init():
    server.screencopy_manager = wlr_screencopy_manager_v1_create(display);
    server.ext_image_copy_capture_manager =
        wlr_ext_image_capture_copy_manager_v1_create(display);
    server.export_dmabuf_manager =
        wlr_export_dmabuf_manager_v1_create(display);
```

### Foreign Toplevel for Window Selection

The `ext-foreign-toplevel-list-v1` protocol allows the portal to enumerate windows:

```
UWM creates wlr_ext_foreign_toplevel_handle_v1 for each window
    |
    v
When a window is created/unmapped/title_changed:
    wlr_ext_foreign_toplevel_handle_v1_update_state()
    wlr_ext_foreign_toplevel_handle_v1_destroy()
```

The portal (xdg-desktop-portal-wlr) uses this to show the user a list of windows to share.

### Per-Window Capture Support in UWM

```c
// In server_new_xdg_toplevel():
handle_new_ext_foreign_toplevel(...):
    // Create ext_foreign_toplevel handle for screen sharing enumeration
    toplevel->ext_foreign_toplevel =
        wlr_ext_foreign_toplevel_handle_v1_create(server);

// In handle_new_ext_image_capture_session():
handle_new_foreign_toplevel_capture_request(...):
    // Create a per-window capture scene for this toplevel
    toplevel->image_capture_scene = wlr_scene_create();
    // Add toplevel to this scene
    wlr_scene_node_reparent(toplevel->scene_tree, toplevel->image_capture_scene);
```

### How OBS Works

OBS Studio (Open Broadcaster Software) uses:
1. **PipeWire capture source:** OBS reads frames via PipeWire's screencast portal
2. **Wayland capture:** Uses `ext-image-capture-source-v1` or `wlr-screencopy-v1` directly
3. **Compositor output:** The compositor exports frames as DMA-BUFs
4. **Encoding:** OBS encodes frames with x264/NVENC/AMDENC into the output format

OBS effectively becomes a capture client of the compositor, receiving frames at the screen's refresh rate.

---

## Exercises

1. What is the difference between `wlr_screencopy_manager_v1` and `wlr_ext_image_capture_copy_manager_v1`?
2. Why does screen sharing use DMA-BUF instead of shared memory?
3. How would you implement virtual output for remote desktop? (Hint: headless backend + pipewire)

---

## Interview Questions

**Q (Mid):** Why can't a Wayland client just read another client's buffer directly?
**A:** Wayland's security model prevents clients from accessing each other's buffers. Each client's buffers are either in its own address space (SHM) or in GPU memory that only the compositor can access (DMA-BUF). A client only sees its own surfaces and the input events directed to it. Screen capture requires explicit protocols (screencopy, ext-image-capture-copy) where the compositor exports the frame.

**Q (Senior):** How does xdg-desktop-portal choose which compositor protocol to use?
**A:** xdg-desktop-portal-wlr (the wlroots backend) checks which protocols the compositor implements. It tries ext-image-capture-copy first, falls back to wlr-screencopy, then to wlr-export-dmabuf. The portal also checks for ext-foreign-toplevel-list for window enumeration. If none of these are available, screen sharing fails.

---

# Part 13: Performance

## Learning Objectives

After completing this part, you will be able to:
- Profile a Wayland compositor with perf
- Identify hot paths and cold paths
- Explain each optimization in UWM

## 13.1 Profiling Methodology

### The Problem

"How do I know what's slow?" Without measurements, optimization is guesswork.

### The Tools

**perf** — Linux profiler. Samples the program counter at a rate (e.g., 1000 Hz). Shows where CPU time is spent.
```bash
perf record -g -p $(pidof uwm) -- sleep 10
perf report
```

**Flamegraphs** — Visual representation of perf data. Each rectangle is a function call; width is CPU time. Stacked to show call chains.
```bash
perf script | stackcollapse-perf.pl | flamegraph.pl > flame.svg
```

**Cachegrind (valgrind)** — Cache simulation. Shows L1/L2/LLC misses, branch mispredictions.
```bash
valgrind --tool=cachegrind uwm
```

**strace** — System call tracing. Shows syscall count and latency.
```bash
strace -c -p $(pidof uwm) -- sleep 10
```

## 13.2 Hot Paths and Cold Paths

### Hot Paths (execute at high frequency)

```
Path                          Frequency      | What to optimize
---------------------------------------------+------------------
cursor_motion -> hit test     60-1000 Hz     | Scene graph walk, O(output count)
output_frame -> commit        60-144 Hz      | Scene graph rendering
keyboard_handle_key           up to 55 Hz    | Keybinding dispatch, O(bindings)
bsp_arrange                   per resize     | Tree walk, O(nodes)
layer_surface_arrange         per layer change | Two passes, O(surfaces)
```

### Cold Paths (execute rarely)

```
Path                          Frequency      | Don't optimize
---------------------------------------------+------------------
server_new_xdg_toplevel       per window     | Alloc, listener setup
workspace_switch              per switch     | Show/hide windows
output_hotplug                rare           | Output creation
config_load                   once           | Startup
```

### The Optimization Rule

**Don't optimize cold paths.** `server_new_xdg_toplevel` runs once per window creation. Even if it took 1ms, that's unnoticeable. Optimizing it would add code complexity for zero user-visible benefit.

**Optimize hot paths only.** `cursor_motion` runs at 144Hz. Every microsecond saved means more time for rendering and lower latency.

## 13.3 UWM's Optimizations

### Pool Allocation (bsp_pool)

Instead of malloc/free for BSP nodes:
- O(1) allocation from freelist
- No heap fragmentation
- Deterministic timing
- All memory is contiguous (better cache locality)

Cost: 36KB of always-allocated memory. Worth it.

### Scene Graph (wlroots)

Instead of manual render list management:
- Spatially indexed node lookup (O(1) for hit testing)
- Automatic clipping and damage tracking
- Efficient buffer management (reuse, release)
- The scene graph handles surface trees and popups

### Intrusive Linked Lists (wl_list)

Instead of dynamically allocated lists:
- No allocation on insert/remove
- Node is part of the owning struct
- Better cache locality (node data is close to list pointers)

### Bit-field Flags (uwm_toplevel)

Instead of multiple bool fields:
```c
struct uwm_toplevel {
    bool floating:1;
    bool fullscreen:1;
    bool is_transient:1;
    bool saved_floating:1;
    bool bsp_saved:1;
    bool bsp_saved_is_second:1;
};
```

Six booleans in 1 byte instead of 6 bytes. Saves 5 bytes per toplevel. At 200 windows: 1KB saved. Minor, but the pattern expresses the intent.

### Cached Modifier Masks

Instead of calling `wlr_keyboard_get_modifiers()` on every keystroke:
```c
struct uwm_keyboard {
    bool cached_ctrl;
    bool cached_alt;
    bool cached_logo;
    bool cached_shift;
};
```

Updated in `keyboard_handle_modifiers()`. Avoids a function call and bitmask operation on every key event.

### No Partial Damage in UBar

The original UBar code attempted partial damage tracking (compare previous zone positions, damage only changed zones). This was removed because:
- The bar is 30px tall — rendering the full bar is trivially fast
- Partial damage tracking added complexity and memory (prev_zones array)
- The comparison loop was itself more expensive than the rendering

After removal: simpler code, same performance.

## 13.4 Memory Pressure Behavior

### The Problem

Under extreme memory pressure (swapping, OOM), malloc can:
- Take milliseconds (page fault to expand the heap)
- Return NULL (out of memory)
- Trigger the OOM killer (process killed)

A compositor must never call malloc on the hot path.

### UWM's Strategy

1. **Pre-allocate everything.** BSP pool (512 nodes), toplevel tracking (via workspace lists, no per-window heap allocation in the hot path).
2. **No hidden allocations.** wlroots' scene graph allocates internally, but scene nodes are created at window creation time (cold path), not during rendering.
3. **Stack allocation for temporary data.** Small arrays for cursor event processing are on the stack.
4. **Static pools for UBar's data collection.** UBar's data.c uses no heap allocation in the hot path (the timer callbacks write to pre-allocated state struct fields).

### The Crash Recovery Safety Net

If malloc returns NULL despite these precautions:
```c
// every allocation is checked:
struct uwm_toplevel *toplevel = calloc(1, sizeof(*toplevel));
if (!toplevel) {
    wlr_log(WLR_ERROR, "calloc failed");
    return;
}
```

The compositor skips the allocation and returns. The window is not created, but the compositor survives. This is acceptable — the user can retry.

---

## 13.5 Cache Locality

### The Problem

CPU caches are fast (L1: ~1ns, L2: ~4ns, L3: ~10ns). RAM is slow (~100ns). Programs that access memory sequentially benefit from prefetching and cache line reuse. Programs that jump between unrelated memory addresses suffer cache misses.

### UWM's Cache-Friendly Patterns

**BSP pool nodes are contiguous.** All 512 nodes are in a single array. Walking the tree accesses nodes sequentially in memory, maximizing cache line utilization.

**Workspace arrays are compact.** 9 workspaces in a fixed array, each with a few fields. Accessing `workspaces[i]` is a direct indexed lookup.

**Scene nodes are wlroots-managed.** wlroots arranges scene graph nodes in a tree. The scene graph rendering walks depth-first, which has good spatial locality for rendering.

### Cache-Unfriendly Patterns

**wl_list iteration.** Linked lists have poor cache locality — each node could be anywhere in memory. UWM's lists (toplevels, outputs, keyboards) are typically short (< 100 elements), so the impact is minimal.

**BSP directional focus.** `bsp_focus_left()` collects all leaves into an array (allocated on the stack via `alloca` or variable-length array) and iterates them. This is two passes (collect + scan). A single-pass algorithm would be faster but more complex.

---

## Exercises

1. Run `perf top -p $(pidof uwm)` while using the compositor. What functions use the most CPU?
2. Identify three more hot paths in UWM that could benefit from optimization.
3. Calculate the memory savings of using bit-fields for `uwm_toplevel` flags (6 bools) vs separate bool fields. At 512 toplevels, how much memory is saved?
4. Why is `bsp_arrange()` O(n) but still acceptable for 20 windows?

---

## Interview Questions

**Q (Mid):** What is a hot path? Give an example from UWM.
**A:** A hot path is code that executes frequently (at every input event, every frame). In UWM, `server_cursor_motion()` is a hot path — it runs at the pointer's report rate (60-1000 Hz). Every instruction in this function is executed millions of times per hour. `bsp_arrange()` is a cold path — it runs only when windows are added, removed, or resized.

**Q (Senior):** Why does UWM use alloca instead of malloc in some places?
**A:** UWM doesn't use `alloca` in the main compositor. In UBar's data.c, stack allocation is used for temporary structs. The tradeoff: stack allocation is faster (no heap management overhead) and doesn't fragment the heap, but stack space is limited (~8MB default on Linux). Deep recursion or large allocations could overflow the stack. UBar's allocations are small and bounded.

**Q (Principal):** Design a benchmark to compare UWM's performance under memory pressure against Sway's.
**A:** 1) Boot a VM with limited RAM (e.g., 512MB). 2) Start UWM/Sway. 3) Open 20 windows with varied content (terminals, browsers, editors). 4) Fill remaining RAM with `stress-ng --vm 2 --vm-bytes 80%`. 5) Measure: frame latency (via `wlr-randr --dump`), input latency (high-speed camera on keystroke-to-pixel), frame drops. 6) The compositor that maintains consistent frame timing and responsive input under swapping is the better performer. 7) Repeat with OOM conditions (`stress-ng --vm 3 --vm-bytes 120%`). The compositor that survives (doesn't get OOM-killed) wins.

---

# Part 14: Debugging

## Learning Objectives

After completing this part, you will be able to:
- Debug a compositor crash using GDB and ASAN
- Identify use-after-free and memory leaks
- Use WAYLAND_DEBUG for protocol debugging
- Apply the debugging methodology that solved UWM's real bugs

## 14.1 Debugging Philosophy

### The First Rule

**Reproduce the bug.** If you can't reproduce it, you can't fix it. If it's intermittent, look for data races (but there are none in UWM's single-threaded model) or memory corruption (write past buffer bounds).

### The Second Rule

**Isolate the component.** Which module is involved? Input? BSP? Output? Window? Layer shell? If the bug occurs when you switch workspaces, it's in workspace.c or output.c. If it occurs on keypress, it's in input.c.

### The Third Rule

**Read the code.** Before adding print statements or running GDB, read the relevant code path. Understand what *should* happen. Then figure out what *is* happening.

### The Fourth Rule

**Check the wlroots source.** If a wlroots API call behaves unexpectedly, read wlroots' implementation. Most bugs are not in wlroots, but understanding wlroots' behavior is essential.

## 14.2 Tools

### ASAN (Address Sanitizer)

Compile with `make ASAN=1`:
```makefile
ASAN ?= 0
ifeq ($(ASAN),1)
    OPT_FLAGS = -fsanitize=address -O0 -g3
    LD_FLAGS  = -lasan
else
    OPT_FLAGS = -O2 -DNDEBUG -march=native
endif
```

ASAN detects:
- Buffer overflows (heap, stack, global)
- Use-after-free
- Double-free
- Memory leaks (on exit)

If ASAN catches a bug, it prints a stack trace with the exact file/line of the error and the allocation/deallocation site.

### Valgrind

```bash
valgrind --leak-check=full --track-origins=yes ./uwm
```

Slower than ASAN (10-50x slowdown) but:
- Detects uninitialized memory reads
- Detects use-after-free (better stack traces than ASAN in some cases)
- Detects memory leaks

### GDB

```bash
gdb --args ./uwm -s "foot"
```

Useful commands:
```
break xdg_toplevel_map     # break on function
watch *0x7fffffff1234     # watch for memory modification
continue                   # continue execution
bt                         # backtrace
frame 3                    # examine frame 3
info locals                # show local variables
p toplevel->xdg_toplevel   # print expression
```

### WAYLAND_DEBUG

```bash
WAYLAND_DEBUG=1 ./uwm  # Server-side protocol dump
WAYLAND_DEBUG=1 foot   # Client-side protocol dump
```

Shows every Wayland message sent/received. Useful for understanding protocol interactions.

### strace

```bash
strace -e poll,read,write -p $(pidof uwm)  # Show I/O events
strace -e eventfd2,timerfd_create -p $(pidof uwm)  # Show timer/event FDs
```

## 14.3 Common Bugs and Solutions

### Bug 1: Fullscreen Bar Visibility During Workspace Switch

**Symptoms:** After switching workspaces on a monitor showing a fullscreen window, the bar appears briefly or the bar's layer-shell surface is visible.

**Root cause:** `workspace_hide_from_output` and `workspace_show_on_output` did not properly handle the `layer_top` and `layer_overlay` scene trees. When a fullscreen window was active, the bar's layer surface (which has a higher z-order than the top layer) was still being shown/hidden in the wrong order.

**Debugging:** Added `wlr_scene_node_set_enabled` calls for `layer_top` and `layer_overlay` in `workspace_show_on_output` and `workspace_hide_from_output`.

**Fix:** `workspace_hide_from_output` now explicitly hides `layer_top` and `layer_overlay`. `workspace_show_on_output` restores them if not in fullscreen mode.

### Bug 2: Monocle Exits When Only One Tiled Window Remains

**Symptoms:** In monocle mode, closing tiled windows until only one remains causes monocle to exit (all windows become visible).

**Root cause:** `xdg_toplevel_unmap` checks `workspace->monocle && count <= 1` and calls `toggle_monocle()` to exit. The intent was that monocle with one window is the same as BSP with one window (no stacking needed), but the check was too eager — it exited even when there were floating windows on the same workspace.

**Debugging:** Set a breakpoint on `toggle_monocle`, observed the call stack when closing a window. Found the check in `xdg_toplevel_unmap`.

**Fix:** `toggle_monocle` is only called when `count <= 1 && workspace->monocle`, but the check now also verifies that floating windows don't need monocle mode.

### Bug 3: Popup Menus Off-Screen

**Symptoms:** Popup menus (right-click context menus) appear partially or fully off-screen.

**Root cause:** `wlr_xdg_popup_unconstrain_from_box` was not called, so popups were constrained to their parent surface's coordinate space instead of the output's.

**Fix:** Added call to `wlr_xdg_popup_unconstrain_from_box` in `server_new_xdg_popup()`:
```c
struct wlr_box output_box;
wlr_output_layout_get_box(server->output_layout,
    output->wlr_output, &output_box);
wlr_xdg_popup_unconstrain_from_box(popup->xdg_popup, &output_box);
```

### Bug 4: KEY_REPEAT_RATE Too Low

**Symptoms:** Holding down navigation keys (MOD+arrow) felt sluggish. Windows moved slowly.

**Root cause:** KEY_REPEAT_RATE was 40Hz (25ms). At 40Hz, repeating keys fire every 25ms, meaning the user could move at most 40 windows per second. With complex BSP trees, this felt slow.

**Fix:** Changed to 55Hz (18ms). 55Hz is just under the typical 60Hz vblank rate, so each repeat fires on alternating frames. The timing is tight enough to feel responsive without wasting frames.

### Bug 5: Multi-Monitor Coordinate Bug

**Symptoms:** In extend mode, BSP arrangement on the second monitor was incorrect — windows appeared at the wrong position or size.

**Root cause:** `get_output_size()` returned `output->wlr_output->width/height` without considering the output's position in the layout. In extend mode with output 1 at (1920, 0), the BSP arrangement placed windows at scene coordinates starting from (0, 0) instead of (1920, 0).

**Fix:** Use `wlr_output_layout_get_box()` to get the output's layout position and add it to the usable area coordinates.

### Bug 6: Use-After-Free in UBar's Audio Monitor

**Symptoms:** Rare crash in UBar during shutdown.

**Root cause:** `audio_monitor_thread` allocated `struct audio_monitor_state` on the stack and passed pointers to it to PulseAudio callbacks. When the thread exited and the stack frame was reclaimed, the callbacks could fire into freed memory.

**Fix:** Heap-allocate `ams` via `calloc` and free it at the end of the thread function.

### Bug 7: Thread Leak in UBar

**Symptoms:** UBar process grows slowly over time.

**Root cause:** `data_stop_monitors` used `pthread_detach` and a 200ms sleep, hoping threads would exit. If they took longer, they leaked.

**Fix:** Store `pthread_t` values and call `pthread_join` to wait for clean exit.

---

## 14.4 Debugging Checklist

When a bug occurs:

1. **Is it reproducible?** If not, suspect memory corruption, UAF, or hardware issues.
2. **Enable ASAN.** Build with `make ASAN=1` and reproduce. If ASAN catches it, fix the root cause.
3. **Check WAYLAND_DEBUG.** Look for unexpected protocol messages.
4. **Read the stack trace.** Use GDB to get a backtrace at the crash point.
5. **Trace the event.** Follow the event from input/commit to the crash point, reading the code.
6. **Check the wlroots source.** If a wlroots API behaves unexpectedly, read the wlroots implementation.
7. **Check the wl_listener lifetime.** Is the listener still connected when the signal fires? Is the owning object still valid?
8. **Check for NULL.** Every alloc is checked, but are you checking for NULL from other functions?
9. **Check the TODOs.** `input.c`, `window.c`, `server.c` contain TODO comments for edge cases.
10. **Ask: what changed?** If the bug appeared after a change, revert the change and verify it goes away.

---

## Exercises

1. Reproduce the UBar use-after-free bug (before the fix): modify `data.c` to revert the stack-to-heap change, then run under ASAN.
2. Use `WAYLAND_DEBUG=server` while running UWM and opening a terminal. Identify every protocol message exchanged.
3. Set a breakpoint on `bsp_insert`. Walk through the function with a window being created.
4. Simulate the multi-monitor coordinate bug by temporarily reverting the `get_output_size` fix. What happens?
5. Use `perf record` on UWM for 30 seconds while doing typical tasks. Generate a flamegraph. Identify the hottest functions.

---

## Interview Questions

**Q (Junior):** What is ASAN and when would you use it?
**A:** Address Sanitizer is a compiler instrumentation tool that detects memory errors: buffer overflows, use-after-free, double-free, and memory leaks. You use it during development and debugging, compiling with `-fsanitize=address`. It's too slow for production use (2-5x slowdown) but invaluable for catching memory bugs.

**Q (Mid):** How would you debug a rendering glitch (e.g., a window appears black instead of showing its content)?
**A:** (1) Check if the window's buffer is valid — does the client commit a buffer? Use `WAYLAND_DEBUG=server` to see buffer attaches. (2) Check if the scene node is positioned correctly — print its x/y/width/height. (3) Check if the scene node is disabled — `wlr_scene_node_get_enabled`. (4) Check if the renderer works — render a test rectangle. (5) Check if KMS page flips succeed — add a frame callback listener.

**Q (Senior):** A user reports that UWM crashes every 3 hours under heavy load. You cannot reproduce it. How do you debug it?
**A:** (1) Build with ASAN and give the user the ASAN binary. Ask them to run it and send the crash log. (2) If ASAN doesn't catch it, build with debug symbols and ask for a core dump (`ulimit -c unlimited`). Analyze the core dump with GDB. (3) If still not reproducible, add logging around suspected areas (memory pressure handlers, seat events, output hotplug). Use `wlr_log` with timestamps. (4) Check for patterns: does it happen during specific operations (workspace switch, fullscreen toggle, window close)? (5) If it's a race condition (unlikely in single-threaded compositor but possible in wlroots internal threads), use ThreadSanitizer.

---

*This concludes Part 14. Part 15 covers security and Part 16 covers future developments.*

---

# Part 15: Security

## Learning Objectives

After completing this part, you will be able to:
- Explain the Wayland security model and how it differs from X11
- Describe capabilities, sandboxing, and portal architecture
- Understand PipeWire permissions
- Identify security-sensitive code in UWM

## 15.1 The Wayland Security Model

### The X11 Problem

X11 was designed for a time when all applications were trusted. Key security failures:

1. **No window isolation.** Any X11 client can read the contents of any window using `XGetImage()`. A keylogger can monitor all keystrokes by grabbing the keyboard. A screenshot tool can capture the entire screen. This is why `xeyes` follows your cursor — it reads the cursor position from the X server.

2. **Global state manipulation.** Any client can change the focus, resize windows, inject events, and read all input. There is no way to sandbox an X11 application.

3. **No clipboard isolation.** Any X11 client can monitor clipboard contents and selection changes in real time.

### The Wayland Solution

Wayland restricts clients to:
- **Their own buffers.** A client only sees what it draws. It cannot read other clients' buffers.
- **Input events directed to them.** A client receives keyboard events only when focused. It cannot globally monitor input.
- **Explicit protocols for sharing.** Screenshotting requires the compositor's participation via `wlr-screencopy` or `ext-image-capture-copy`.

### What This Means for Compositors

The compositor is the **trusted computing base** (TCB). It has access to:
- All client buffers (for compositing)
- All input events (for routing)
- The DRM device (for display)
- Shared memory FDs (for client buffers)

A compromised compositor can read all screen contents, capture all input, and display arbitrary content. This is why compositors must be minimal and auditable.

## 15.2 Capabilities and Sandboxing

### Wayland Capabilities

The `wl_seat` global sends `capabilities` events indicating what input devices are available:
```c
enum wl_seat_capability {
    WL_SEAT_CAPABILITY_POINTER    = 1,
    WL_SEAT_CAPABILITY_KEYBOARD   = 2,
    WL_SEAT_CAPABILITY_TOUCH      = 4,
};
```

Clients can request only the capabilities they need. A background music player doesn't need keyboard input. A screensaver might need only pointer input (to detect user activity).

### Protocol-Specific Capabilities

Some protocols add their own capabilities:
- `zwp_input_method_v1` — The client acts as an input method (IME)
- `zwp_virtual_keyboard_v1` — The client can inject keyboard events
- `ext_session_lock_v1` — The client can lock the session

These are privileged protocols. UWM allows any client to use them (there is no capability check). In a more security-conscious compositor, these would be restricted to specific clients.

### Sandboxing (Flatpak, Snap)

Sandboxed applications (Flatpak) use:
- **D-Bus portals** (xdg-desktop-portal) for file access, screenshotting, clipboard
- **PipeWire** for screen capture (doesn't require direct compositor protocol access)
- **wlr-layer-shell** for notifications (with compositor approval)

The portal acts as an intermediary: the sandboxed app asks the portal for a service, and the portal shows a permission dialog to the user.

## 15.3 Why Wayland Is Safer Than X11

| Feature | X11 | Wayland |
|---|---|---|
| Window contents | Any client can read | Only compositor sees all buffers |
| Keylogging | Any client can grab keyboard | Only focused client receives keys |
| Screenshot | Any client can capture | Requires explicit protocol |
| Event injection | Any client can inject | Only compositor generates events |
| Clipboard monitoring | Any client can read | Only active selection owner |
| Remote access | Built-in (X11 forwarding) | Delegated to PipeWire/VNC |

## 15.4 PipeWire Permissions

PipeWire has its own security model:
- **Access control** is managed by WirePlumber (or other session manager)
- **Flatpak apps** require the `pipewire` socket permission and the `screencast` portal permission
- **User approval** is handled by xdg-desktop-portal's permission dialog

The compositor exports frames as PipeWire streams. The compositor decides which frames to export (full screen, single window, specific region). The compositor does not need to verify "who" is reading — that's the portal's job.

## 15.5 Security-Sensitive Code in UWM

### Data Forwarding

UWM forwards input events to the focused client:
```c
wlr_seat_keyboard_notify_key(seat, ...);
wlr_seat_pointer_notify_motion(seat, ...);
```

UWM must ensure it only forwards events to the correct client. The seat implementation (wlroots) enforces this: events are sent only to the surface that currently has keyboard/pointer focus.

### Shared Memory FDs

Clients pass memfd FDs for SHM buffers:
```c
struct wl_shm_pool *pool = wl_shm_create_pool(state->shm, fd, size);
struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, ...);
```

The compositor mmap's the FD and reads/writes client pixel data. A malicious client could:
- Provide a tiny FD that's smaller than the claimed size → compositor reads beyond the FD boundary (SIGBUS if the FD is sparse, or reads stale data). wlroots handles this by checking the FD size.
- Modify the buffer after committing → the compositor sees partial content. This is fine — the compositor composites whatever is in the buffer.
- Close the FD after passing it → the compositor still has the FD via the dup in wayland-server.

### DMA-BUF

For DMA-BUFs, the client provides a set of FD handles. The compositor imports them into GL textures. A malicious client could provide:
- Invalid FDs → import fails gracefully
- FDs to sensitive GPU memory → DMA-BUF export should only export buffers the client owns, but kernel bugs in this area have existed
- FDs that change size → wlroots validates buffer parameters

### Fork/Exec

UWM calls `fork()` and `exec()` for autostart and keybinding actions. Security considerations:
- **Close all FDs** in the child process before exec (set `O_CLOEXEC` on all compositor FDs)
- **Do not trust environment variables** — the child inherits UWM's environment
- **Signal handling** — the child inherits signal handlers; reset them to defaults before exec

UWM's `spawn_cmd()` handles this:
```c
static void spawn_cmd(const char *cmd) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child: reset signal handlers, exec
        restore_crash_handlers();  // reset to SIG_DFL
        execlp("sh", "sh", "-c", cmd, NULL);
        _exit(EXIT_FAILURE);
    }
}
```

---

## Exercises

1. What prevents a malicious client from reading another client's SHM buffer?
2. How does Flatpak's screencast portal interact with UWM? What protocols are involved?
3. Why doesn't UWM implement capability checks for privileged protocols?

---

## Interview Questions

**Q (Mid):** How does Wayland prevent keylogging?
**A:** Wayland only sends keyboard events to the focused surface. The compositor determines focus. A background application cannot receive keyboard events. There is no `XGrabKeyboard` equivalent. Input methods must use the input-method protocol, which the compositor mediates.

**Q (Senior):** What security implications does VT switching have for a Wayland compositor?
**A:** When the user switches VTs (Ctrl+Alt+F2), the compositor's session is deactivated. The kernel revokes DRM and evdev access. The compositor must stop rendering and release all GPU resources (`handle_session_active()` with `active=false`). When the user switches back, the compositor re-acquires resources. A buggy compositor might continue rendering to DRM after session deactivation, causing a "GPU is in use by another process" error.

---

# Part 16: Future

## Learning Objectives

After completing this part, you will be able to:
- Describe the upcoming Wayland protocols and features
- Understand how UWM would need to change to support them
- Evaluate whether a new protocol is worth implementing

## 16.1 Fractional Scaling

### The Problem

HiDPI monitors (e.g., 4K at 14") need >1x scaling for readability. Integer scaling (2x, 3x) works but makes content too large on some monitors. 1.5x or 1.25x would be ideal.

### The Solution (Still Evolving)

`wp-fractional-scale-v1` allows clients to opt into non-integer scaling. The compositor tells each surface its fractional scale factor. The client renders at that scale and the compositor doesn't upscale (avoiding blur).

### Impact on UWM

UWM would need to:
1. Implement `wp_fractional_scale_manager_v1` global
2. Track per-surface fractional scale
3. Pass scale to `wlr_scene_output_set_scale()` or per-surface scale
4. Handle the interaction with layer-shell (bars need to scale too)

This is straightforward with wlroots — wlroots provides `wlr_fractional_scale_manager_v1`

## 16.2 Cursor Shape Protocol

### The Problem

Clients need to change the cursor icon (pointer, hand, text, etc.). In X11, this was done via `XDefineCursor()`. In Wayland, this requires the compositor's participation.

### The Current State

`wlr_cursor_shape_manager_v1` (already implemented in wlroots 0.20) allows clients to request cursor shapes:
```c
// Client:
wp_cursor_shape_device_v1_set_shape(shape_device, serial, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
```

### How UWM Uses It

UWM creates the global in `server_init()`:
```c
server.cursor_shape_manager = wlr_cursor_shape_manager_v1_create(display);
```

The `handle_cursor_shape_request()` callback updates the cursor image. This is already implemented.

## 16.3 XDG Activation

### The Problem

When you click a link in a messenger app, the browser should open and be focused (activated). The messenger needs a way to request activation of another window.

### The Solution: xdg-activation-v1

```xml
<request name="activate">
  <arg name="token" type="string"/>
  <arg name="surface" type="object" interface="wl_surface"/>
</request>
```

The requesting client gets a token from the compositor (generated from a user interaction serial) and passes it to the new client. The new client presents the token to the compositor, which activates its surface.

### Impact on UWM

UWM would implement `xdg_activation_v1` and handle the `activate` request by calling `focus_toplevel()`.

## 16.4 Text Input

### The Problem

Typing in East Asian languages (Chinese, Japanese, Korean) requires an Input Method Editor (IME). The IME is a separate process that receives keystrokes and outputs composed text.

### The Protocol

`zwp_input_method_v1` / `zwp_text_input_v3`:
- The compositor routes keystrokes to the IME (instead of the focused client) when an IME is active
- The IME sends composed text back to the compositor
- The compositor forwards the text to the focused client

This is complex to implement correctly. Sway has a dedicated input-method module.

### Impact on UWM

UWM does not implement text input. A third-party IME like `ibus` or `fcitx` communicates with clients directly via D-Bus, bypassing the compositor's input-method protocol. This works for most use cases but doesn't support on-screen keyboards (wvkbd) or advanced IME features.

## 16.5 XWayland

### The Problem

Many legacy applications (and some modern ones) are X11-only. They cannot run directly on Wayland.

### The Solution: XWayland

XWayland is an X server that runs as a Wayland client. It creates a root window that is an X11 desktop, and X11 applications run inside it. XWayland translates X11 protocol requests into Wayland surfaces and buffers.

### Impact on UWM

Adding XWayland requires:
1. Starting `Xwayland` as a child process
2. Implementing `wlr_xwayland` integration
3. Handling X11 window management (setting WM_NAME, _NET_WM_STATE, etc.)
4. XWayland surface management (creating scene nodes for XWayland windows)

This is significant work. UWM currently does not support XWayland.

## 16.6 HDR and Color Management

### The Problem

Modern monitors support HDR (High Dynamic Range) — wider color gamut (BT.2020), higher brightness (1000+ nits), and 10-bit color depth. Wayland was designed for SDR (Standard Dynamic Range, sRGB, 8-bit).

### The Future

- `wp_color_management_v1` — Color management protocol (still in development)
- `wp_hdr_metadata_v1` — HDR metadata protocol (still in development)
- wlroots will need to support HDR framebuffers (FP16, 10-bit)
- The scene graph will need color space management

UWM, as a minimalist compositor, will not implement HDR until wlroots provides a straightforward API.

## 16.7 Explicit Sync

### The Problem

Currently, buffer synchronization between the compositor and clients is implicit:
1. Client renders into buffer
2. Client commits buffer
3. Compositor assumes rendering is complete
4. Compositor composites

If the client is still rendering when the compositor composites, the frame shows partial content (tearing). Explicit sync allows the client to signal "rendering is complete" before the compositor uses the buffer.

### The Protocol

`wp_linux_explicit_synchronization_v1`:
- Client creates `wp_linux_buffer_release_v1` with an eventfd
- Client renders into buffer
- Client signals eventfd (rendering complete)
- Compositor waits on eventfd before compositing

### Impact on UWM

wlroots supports explicit sync. UWM would need to opt-in when creating buffers. The impact is minimal — wlroots handles the synchronization.

## 16.8 How to Evaluate a New Protocol

Before implementing a new protocol, ask:

1. **Does wlroots support it?** If yes, it's likely straightforward (create global, add listeners).
2. **Does the protocol add value?** If only one client (e.g., a niche bar) uses it, is it worth the maintenance burden?
3. **Can it be delegated to an external tool?** If the feature can be implemented via fork/exec or an external process using existing protocols, it should be.
4. **What's the security impact?** Does the protocol expose client data? Can it be used for privilege escalation?
5. **What's the performance impact?** Does the protocol fire events on the hot path? Can it cause frame drops?

---

## Exercises

1. Design a protocol for virtual desktops (workspaces) that a client could use to enumerate and switch workspaces. What events and requests would it need?
2. How would UWM need to change to support HDR? What wlroots APIs would be used?
3. Research `wp_fractional_scale_v1`. How does it differ from `wl_output.scale`? Why is it needed?

---

## Interview Questions

**Q (Mid):** Why does fractional scaling exist when `wl_output` already has a `scale` factor?
**A:** `wl_output.scale` is an integer (1, 2, 3). Clients typically render at integer scales (1x, 2x) even though the compositor can then scale down to e.g. 1.5x. This wastes GPU memory (rendering at 2x then downscaling) and looks bad (bilinear filtering on the downscale). Fractional scaling lets the client render at exactly the right scale (e.g., 1.5x), saving GPU memory and producing sharper output.

**Q (Senior):** Should UWM implement XWayland? What are the tradeoffs?
**A:** XWayland adds ~3000 lines of code (based on Sway's implementation), ongoing maintenance (XWayland quirks, window manager compatibility), and attack surface (an entire X server running as a child process). The benefit: users can run X11 applications (GIMP, older games, proprietary software). For a minimalist compositor, XWayland is optional — users who need X11 can use Sway. UWM intentionally does not support XWayland to keep the codebase small and auditable.

---

# Appendix A: Glossary

**ASAN** — Address Sanitizer. A compiler instrumentation tool for detecting memory errors.

**BSP** — Binary Space Partitioning. A tree-based layout algorithm that recursively divides screen space.

**CRTC** — Cathode Ray Tube Controller. The display controller in a GPU that generates pixel streams.

**DMA-BUF** — Direct Memory Access Buffer. A buffer that can be shared across kernel subsystems and processes via file descriptors.

**DRM** — Direct Rendering Manager. Linux kernel subsystem for GPU access.

**EDID** — Extended Display Identification Data. Data from a monitor describing its capabilities (resolutions, timings).

**EGL** — Embedded-System Graphics Library. Interface between OpenGL and the native windowing system.

**evdev** — Event device. Linux kernel interface for input devices.

**FD** — File descriptor. An integer referencing an open file, socket, or other I/O object.

**GBM** — Generic Buffer Manager. API for allocating GPU buffers that can be used with EGL and KMS.

**GEM** — Graphics Execution Manager. DRM memory manager for buffer objects.

**KMS** — Kernel Mode Setting. DRM subsystem for configuring display pipelines.

**libinput** — Library for processing input events from evdev devices.

**libseat** — Library for seat management (device access arbitration).

**MMU** — Memory Management Unit. Hardware that translates virtual addresses to physical addresses.

**PipeWire** — Multimedia framework for audio and video routing, used for screen capture.

**Scene Graph** — Tree structure representing all visible surfaces in a compositor.

**seat** — The set of input and output devices assigned to a user session.

**serial** — Monotonically increasing number used for authorization in Wayland protocols.

**SHM** — Shared Memory. Wayland buffer type backed by a memfd.

**TLB** — Translation Lookaside Buffer. Cache for virtual-to-physical address translations.

**wlroots** — Library for building Wayland compositors.

**XKB** — X Keyboard Extension. Keyboard configuration and keycode-to-keysym mapping library.

---

# Appendix B: Suggested Reading

## Books
- "Computer Systems: A Programmer's Perspective" (Bryant & O'Hallaron) — Virtual memory, caching, assembly
- "Linux Kernel Development" (Love) — Process management, memory management, VFS
- "Advanced Programming in the UNIX Environment" (Stevens) — File descriptors, signals, IPC
- "The Linux Programming Interface" (Kerrisk) — Comprehensive Linux API reference

## Wayland
- [Wayland Documentation](https://wayland.freedesktop.org/docs/html/) — Official protocol documentation
- [Wayland Book](https://wayland-book.com/) — Tutorial for Wayland programming
- [wlroots source code](https://gitlab.freedesktop.org/wlroots/wlroots) — Reference implementation

## Graphics
- [drm-howto](https://github.com/ascent12/drm-howto) — Tutorial for DRM/KMS programming
- [Linux Graphics Drivers](https://docs.kernel.org/gpu/) — Kernel DRM documentation
- [Understanding linux-dmabuf](https://wayland.app/protocols/linux-dmabuf-v1) — DMA-BUF protocol

## Debugging
- [Perf Wiki](https://perf.wiki.kernel.org/) — Linux profiling
- [Valgrind Manual](https://valgrind.org/docs/manual/manual.html) — Memory debugging
- GDB Documentation — Debugger usage

---

# Appendix C: UWM File Reference Cheat Sheet

## Header Files

| Header | Declares | Included by |
|---|---|---|
| server.h | `struct uwm_server`, `enum uwm_cursor_mode` | All .c files |
| window.h | `struct uwm_toplevel`, `struct uwm_popup` | input.c, output.c, bsp.c, workspace.c, floating.c, layout.c |
| output.h | `struct uwm_output` | server.c, window.c, input.c, bsp.c, workspace.c, floating.c, layer_shell.c |
| input.h | `struct uwm_keyboard`, action function prototypes | server.c, window.c |
| bsp.h | `struct uwm_bsp_node`, `struct uwm_bsp_pool` | server.c, window.c, workspace.c, floating.c, layout.c, input.c |
| workspace.h | `struct uwm_workspace`, `struct uwm_workspace_manager` | server.c, window.c, output.c, input.c, floating.c, layout.c |
| floating.h | `toggle_floating`, `toggle_fullscreen`, `raise_floating` | input.c, window.c, layout.c, workspace.c |
| layout.h | `toggle_monocle`, `set_bsp_mode` | input.c, window.c, workspace.c, floating.c |
| layer_shell.h | `struct uwm_layer_surface`, `struct uwm_layer_shell` | server.c, output.c |
| idle_inhibit.h | `struct uwm_idle_inhibitor`, `struct uwm_idle_inhibit` | server.c |
| session_lock.h | `struct uwm_session_lock` | server.c, output.c |
| uwm_bar.h | `struct uwm_bar_manager`, `struct uwm_workspace_group` | server.c, window.c, output.c |
| rules.h | `rule_apply_all` | window.c |
| config.h | `struct uwm_config`, keybinding arrays, action types | server.c, input.c, main.c, rules.c |

## Source Files Quick Reference

| File | Lines | Purpose | Key Functions |
|---|---|---|---|
| main.c | ~120 | Entry, crash recovery, autostart | main(), crash_handler(), run_autostart(), spawn_cmd() |
| server.c | ~550 | Server bootstrap | server_init(), server_finish(), handle_session_active(), handle_new_output(), handle_new_input() |
| window.c | ~800 | Window lifecycle | server_new_xdg_toplevel(), xdg_toplevel_map/unmap/commit/destroy(), focus_toplevel(), desktop_toplevel_at() |
| output.c | ~400 | Output management | server_new_output(), output_frame(), output_destroy(), output_set_workspace() |
| input.c | ~800 | Input processing | keyboard_handle_key(), server_cursor_motion/button(), process_cursor_move/resize(), handle_keybinding() |
| bsp.c | ~450 | BSP tree | bsp_insert(), bsp_remove(), bsp_arrange(), bsp_focus_left/right/up/down() |
| workspace.c | ~250 | Workspace manager | workspace_switch(), workspace_move_toplevel(), workspace_show/hide_on_output() |
| floating.c | ~150 | Float/fullscreen | toggle_floating(), toggle_fullscreen(), raise_floating() |
| layout.c | ~100 | Layout toggles | toggle_monocle(), set_bsp_mode(), update_layout_visibility() |
| layer_shell.c | ~250 | Layer-shell protocol | layer_surface_create(), layer_surface_arrange(), handle_map/unmap() |
| idle_inhibit.c | ~50 | Idle inhibit | idle_inhibit_create(), handle_new_inhibitor() |
| session_lock.c | ~100 | Session lock | handle_new_lock(), handle_new_lock_surface(), handle_lock_unlock() |
| uwm_bar.c | ~200 | Custom bar protocol | uwm_bar_manager_create(), uwm_bar_send_all(), uwm_bar_send_output() |
| rules.c | ~100 | Window rules | rule_apply_all(), glob_match() |
| config.c | ~50 | Config loader | config_load() |

---

# Appendix D: Wayland Protocol XML for uwm-bar-unstable-v1

```xml
<?xml version="1.0" encoding="UTF-8"?>
<protocol name="uwm_bar_unstable_v1">
  <interface name="zwp_uwm_bar_v1" version="1">
    <description>
      Manager for UWM workspace groups. A client binds to this global
      to receive workspace state events.
    </description>

    <request name="get_workspace_group">
      <arg name="id" type="uint"/>
      <arg name="output" type="object" interface="wl_output"/>
      <arg name="group" type="new_id" interface="zwp_uwm_workspace_group_v1"/>
    </request>

    <request name="destroy" type="destructor"/>
  </interface>

  <interface name="zwp_uwm_workspace_group_v1" version="1">
    <description>
      Represents the workspace state for one output. The compositor
      sends workspace events for all workspaces, then a done event.
    </description>

    <event name="workspace">
      <arg name="id" type="uint"/>
      <arg name="active" type="uint"/>
      <arg name="occupied" type="uint"/>
    </event>

    <event name="focused_title">
      <arg name="title" type="string"/>
    </event>

    <event name="done"/>

    <request name="destroy" type="destructor"/>
  </interface>
</protocol>
```

---

*End of UWM Textbook. Total: 16 parts covering the full stack from OS fundamentals through future developments.*

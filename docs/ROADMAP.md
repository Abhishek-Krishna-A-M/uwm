[✓] TinyWL boots
[✓] Files split
[✓] Workspace manager

PHASE 1 — Workspace Foundation
------------------------------
[x] Windows belong to workspaces
[x] Workspace switching
[x] Workspace visibility
[x] Move window between workspaces
[x] Per-workspace focus tracking
[x] Per-workspace layout state

PHASE 2 — BSP Core
------------------
[x] BSP node structure
[x] BSP insertion
[x] BSP split H
[x] BSP split V
[x] BSP layout calculation
[x] BSP tree destruction
[x] Focus traversal
[x] Window swap
[x] Window reorder
[x] Resize BSP splits
[x] Focus follows pointer
[x] Focus follows pointer toggle
[x] Warp pointer to focused window (optional)

PHASE 3 — Window States
-----------------------
[ ] Floating windows
[ ] Floating move
[ ] Floating resize
[ ] Pointer move
[ ] Pointer resize
[ ] Fullscreen
[ ] Monocle
[ ] Tabbed
[ ] Toggle tiled/floating
[ ] Toggle monocle
[ ] Toggle tabbed
[ ] Restore previous state
[ ] Floating layer ordering

PHASE 4 — Rules System
----------------------
[ ] Rule engine
[ ] Match by app_id
[ ] Match by title
[ ] Workspace assignment rules
[ ] Floating rules
[ ] Opacity rules
[ ] Fullscreen rules
[ ] Config-driven rules

PHASE 5 — Multi Output
----------------------
[ ] Output manager
[ ] Output detection
[ ] Workspace per output
[ ] Move window between outputs
[ ] HDMI extend
[ ] HDMI mirror
[ ] Output hotplug handling
[ ] Output layout configuration
[ ] Primary output selection

PHASE 6 — Visual Layer
----------------------
[ ] Focused opacity
[ ] Unfocused opacity
[ ] Per-rule opacity
[ ] Border rendering
[ ] Tab title rendering
[ ] Workspace indicators

PHASE 7 — Configuration
-----------------------
[ ] ~/.config/uwm/config
[ ] Keybind configuration
[ ] Rule configuration
[ ] Output configuration
[ ] Startup commands
[ ] Reload configuration
[ ] Config validation

PHASE 8 — IPC
-------------
[ ] Unix socket server
[ ] IPC command parser
[ ] Event subscription
[ ] Runtime state queries
[ ] IPC events

PHASE 9 — uwmctl
----------------
[ ] uwmctl workspace
[ ] uwmctl output
[ ] uwmctl focus
[ ] uwmctl move
[ ] uwmctl reload
[ ] uwmctl query
[ ] uwmctl monitor

PHASE 10 — Bar
--------------
[ ] uwm-bar protocol
[ ] Workspace display
[ ] Focused window title
[ ] Layout indicator
[ ] Output indicator
[ ] IPC integration

PHASE 11 — Wallpaper
--------------------
[ ] Wallpaper manager
[ ] Per-output wallpaper
[ ] Wallpaper reload
[ ] Wallpaper scaling modes

PHASE 12 — Screen Sharing
-------------------------
[ ] xdg-desktop-portal support
[ ] PipeWire integration
[ ] Screen capture
[ ] Window capture
[ ] Region capture
[ ] Multi-monitor capture

PHASE 13 — XWayland
-------------------
[ ] Optional build flag
[ ] XWayland startup
[ ] X11 window handling
[ ] X11 focus handling
[ ] X11 fullscreen
[ ] X11 rules integration

PHASE 14 — Optimization
-----------------------
[ ] Damage tracking audit
[ ] Render path audit
[ ] Input latency audit
[ ] Wakeup reduction
[ ] Allocation audit
[ ] Scene graph audit
[ ] Memory profiling
[ ] Startup profiling

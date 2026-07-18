/* UWM compile-time configuration — default values.
 * Copy this to config.h and edit config.h to override. */

/* appearance */
#define BORDERPX 2
#define RESIZEFACTOR 0.05f
#define FLOATING_MIN_WIDTH 100
#define FLOATING_MIN_HEIGHT 60
#define FLOATING_DEFAULT_WIDTH_RATIO 0.60f
#define FLOATING_DEFAULT_HEIGHT_RATIO 0.75f
#define FLOATING_CREATE_MIN_WIDTH 200
#define FLOATING_CREATE_MIN_HEIGHT 150
#define INNER_GAP 5

/* input */
#define FOCUS_FOLLOWS_POINTER true
#define KEY_REPEAT_DELAY 250
#define KEY_REPEAT_RATE 55
#define TAP_TO_CLICK true
#define NATURAL_SCROLL true
#define ACCEL_PROFILE 0.75
#define POINTER_SPEED 0.2

#define MOD WLR_MODIFIER_LOGO

/* spawnable command argv arrays (NULL-terminated) */
#define TERM        "footclient", NULL
#define LAUNCHER    "rofi", "-nowm", "-show", "drun", "-display-drun", "󰀻 Apps:", NULL
#define RUN         "rofi", "-nowm", "-show", "run", "-display-run", " Run:", NULL
#define SCREENSHOT    "sh", "-c", "grim -g \"$(slurp)\" - | tee ~/Pictures/Screenshots/$(date +%Y%m%d_%H%M%S).png | wl-copy", NULL
#define SCREENSHOT_FULL "grim", NULL
#define SCREENSHOT_CLIP "sh", "-c", "grim -g \"$(slurp)\" - | wl-copy", NULL
#define FILEMGR     "foot", "-e", "lf", NULL
#define FINDFILE    "sh", "-c", "file=$(cd ~ && fd --type f --hidden --follow --exclude .git --exclude .cache --exclude .local/share --exclude node_modules | rofi -dmenu -nowm -i -p '󰈞 Find File:') && [ -n \"$file\" ] && footclient -e env FILE=\"$HOME/$file\" sh -c 'cd \"$(dirname \"$(realpath \"$FILE\")\")\" && nvim \"$(realpath \"$FILE\")\" && exec $SHELL'", NULL
#define POWERMENU   "sh", "-c", "~/.config/custom_scripts/powermenu.sh", NULL
#define WINSWITCH   "rofi", "-nowm", "-show", "window", "-display-window", "󱂬 Switch:", "-theme-str", "window { width: 40%; } listview { lines: 8; }", NULL
#define HDMI_SCRIPT "sh", "-c", "~/.config/custom_scripts/hdmi.sh", NULL
#define VOLUP       "sh", "-c", "wpctl set-volume @DEFAULT_AUDIO_SINK@ 0.05+", NULL
#define VOLDOWN     "sh", "-c", "wpctl set-volume @DEFAULT_AUDIO_SINK@ 0.05-", NULL
#define VOLMUTE     "sh", "-c", "wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle", NULL
#define BRUP        "sh", "-c", "brightnessctl set +10%", NULL
#define BRDOWN      "sh", "-c", "brightnessctl set 10%-", NULL
#define REFRESH_BAR "sh", "-c", "killall -9 ubar 2>/dev/null; sleep 0.3; setsid ubar >/dev/null 2>&1 &", NULL
#define UBROWSER    "ubrowser", NULL
#define RUN_ACTIONS "sh", "-c", "~/.config/custom_scripts/run_actions.sh", NULL

/* key binding list helper */
#define KEY(m, s, f, a) { m, s, f, a },

/* modifier-based key bindings (require MOD to be held) */
#define KEYS \
	KEY(MOD, XKB_KEY_Return, spawn, { .argv = term }) \
	KEY(MOD, XKB_KEY_r, spawn, { .argv = launcher }) \
	KEY(MOD, XKB_KEY_Print, spawn, { .argv = screenshot_full }) \
	KEY(MOD, XKB_KEY_h, moveleft, {0}) \
	KEY(MOD, XKB_KEY_Left, moveleft, {0}) \
	KEY(MOD, XKB_KEY_j, movedown, {0}) \
	KEY(MOD, XKB_KEY_Down, movedown, {0}) \
	KEY(MOD, XKB_KEY_k, moveup, {0}) \
	KEY(MOD, XKB_KEY_Up, moveup, {0}) \
	KEY(MOD, XKB_KEY_l, moveright, {0}) \
	KEY(MOD, XKB_KEY_Right, moveright, {0}) \
	KEY(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_h, swapleft, {0}) \
	KEY(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_Left, swapleft, {0}) \
	KEY(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_j, swapdown, {0}) \
	KEY(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_Down, swapdown, {0}) \
	KEY(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_k, swapup, {0}) \
	KEY(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_Up, swapup, {0}) \
	KEY(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_l, swapright, {0}) \
	KEY(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_Right, swapright, {0}) \
	KEY(MOD | WLR_MODIFIER_ALT, XKB_KEY_h, resizeleft, {0}) \
	KEY(MOD | WLR_MODIFIER_ALT, XKB_KEY_Left, resizeleft, {0}) \
	KEY(MOD | WLR_MODIFIER_ALT, XKB_KEY_j, resizedown, {0}) \
	KEY(MOD | WLR_MODIFIER_ALT, XKB_KEY_Down, resizedown, {0}) \
	KEY(MOD | WLR_MODIFIER_ALT, XKB_KEY_k, resizeup, {0}) \
	KEY(MOD | WLR_MODIFIER_ALT, XKB_KEY_Up, resizeup, {0}) \
	KEY(MOD | WLR_MODIFIER_ALT, XKB_KEY_l, resizeright, {0}) \
	KEY(MOD | WLR_MODIFIER_ALT, XKB_KEY_Right, resizeright, {0}) \
	KEY(MOD | WLR_MODIFIER_SHIFT | WLR_MODIFIER_ALT, XKB_KEY_h, resizeshleft, {0}) \
	KEY(MOD | WLR_MODIFIER_SHIFT | WLR_MODIFIER_ALT, XKB_KEY_Left, resizeshleft, {0}) \
	KEY(MOD | WLR_MODIFIER_SHIFT | WLR_MODIFIER_ALT, XKB_KEY_j, resizeshdown, {0}) \
	KEY(MOD | WLR_MODIFIER_SHIFT | WLR_MODIFIER_ALT, XKB_KEY_Down, resizeshdown, {0}) \
	KEY(MOD | WLR_MODIFIER_SHIFT | WLR_MODIFIER_ALT, XKB_KEY_k, resizeshup, {0}) \
	KEY(MOD | WLR_MODIFIER_SHIFT | WLR_MODIFIER_ALT, XKB_KEY_Up, resizeshup, {0}) \
	KEY(MOD | WLR_MODIFIER_SHIFT | WLR_MODIFIER_ALT, XKB_KEY_l, resizeshright, {0}) \
	KEY(MOD | WLR_MODIFIER_SHIFT | WLR_MODIFIER_ALT, XKB_KEY_Right, resizeshright, {0}) \
	KEY(MOD, XKB_KEY_1, workspace, { .i = 0 }) \
	KEY(MOD, XKB_KEY_2, workspace, { .i = 1 }) \
	KEY(MOD, XKB_KEY_3, workspace, { .i = 2 }) \
	KEY(MOD, XKB_KEY_4, workspace, { .i = 3 }) \
	KEY(MOD, XKB_KEY_5, workspace, { .i = 4 }) \
	KEY(MOD, XKB_KEY_6, workspace, { .i = 5 }) \
	KEY(MOD, XKB_KEY_7, workspace, { .i = 6 }) \
	KEY(MOD, XKB_KEY_8, workspace, { .i = 7 }) \
	KEY(MOD, XKB_KEY_9, workspace, { .i = 8 }) \
	KEY(MOD, XKB_KEY_KP_1, workspace, { .i = 0 }) \
	KEY(MOD, XKB_KEY_KP_2, workspace, { .i = 1 }) \
	KEY(MOD, XKB_KEY_KP_3, workspace, { .i = 2 }) \
	KEY(MOD, XKB_KEY_KP_4, workspace, { .i = 3 }) \
	KEY(MOD, XKB_KEY_KP_5, workspace, { .i = 4 }) \
	KEY(MOD, XKB_KEY_KP_6, workspace, { .i = 5 }) \
	KEY(MOD, XKB_KEY_KP_7, workspace, { .i = 6 }) \
	KEY(MOD, XKB_KEY_KP_8, workspace, { .i = 7 }) \
	KEY(MOD, XKB_KEY_KP_9, workspace, { .i = 8 }) \
	KEY(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_1, movetows, { .i = 0 }) \
	KEY(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_2, movetows, { .i = 1 }) \
	KEY(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_3, movetows, { .i = 2 }) \
	KEY(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_4, movetows, { .i = 3 }) \
	KEY(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_5, movetows, { .i = 4 }) \
	KEY(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_6, movetows, { .i = 5 }) \
	KEY(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_7, movetows, { .i = 6 }) \
	KEY(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_8, movetows, { .i = 7 }) \
	KEY(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_9, movetows, { .i = 8 }) \
	KEY(MOD, XKB_KEY_Tab, workspaceprev, {0}) \
	KEY(MOD, XKB_KEY_bracketleft, workspacedec, {0}) \
	KEY(MOD, XKB_KEY_bracketright, workspaceinc, {0}) \
	KEY(MOD, XKB_KEY_f, togglefullscreen, {0}) \
	KEY(MOD, XKB_KEY_s, togglefloating, {0}) \
	KEY(MOD, XKB_KEY_m, togglemonocle, {0}) \
	KEY(MOD, XKB_KEY_t, setbsp, {0}) \
	KEY(MOD, XKB_KEY_c, cyclefocus, {0}) \
	KEY(MOD, XKB_KEY_w, closewindow, {0}) \
	KEY(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_w, forceclose, {0}) \
	KEY(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_r, rotatesplit, {0}) \
	KEY(MOD, XKB_KEY_e, spawn, { .argv = run }) \
	KEY(MOD, XKB_KEY_space, spawn, { .argv = winswitch }) \
	KEY(MOD | WLR_MODIFIER_ALT, XKB_KEY_space, spawn, { .argv = hdmi_script }) \
	KEY(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_f, spawn, { .argv = filemgr }) \
	KEY(MOD | WLR_MODIFIER_ALT, XKB_KEY_f, spawn, { .argv = findfile }) \
	KEY(MOD | WLR_MODIFIER_ALT, XKB_KEY_x, spawn, { .argv = powermenu }) \
	KEY(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_s, spawn, { .argv = screenshot_clip }) \
	KEY(MOD | WLR_MODIFIER_ALT, XKB_KEY_r, spawn, { .argv = refresh_bar }) \
	KEY(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_b, spawn, { .argv = ubrowser }) \
	KEY(MOD | WLR_MODIFIER_ALT, XKB_KEY_b, spawn, { .argv = run_actions }) \
	KEY(MOD | WLR_MODIFIER_ALT, XKB_KEY_q, quit, {0})

/* unmodified key bindings (no modifier required) */
#define KEYS_UNMODIFIED \
	KEY(0, XKB_KEY_XF86AudioRaiseVolume, spawn, { .argv = volup }) \
	KEY(0, XKB_KEY_XF86AudioLowerVolume, spawn, { .argv = voldown }) \
	KEY(0, XKB_KEY_XF86AudioMute, spawn, { .argv = volmute }) \
	KEY(0, XKB_KEY_XF86MonBrightnessUp, spawn, { .argv = brup }) \
	KEY(0, XKB_KEY_XF86MonBrightnessDown, spawn, { .argv = brdown }) \
	KEY(0, XKB_KEY_Print, spawn, { .argv = screenshot })

/* startup — NULL-terminated list of shell commands.
 * Each entry is executed via sh -c after WAYLAND_DISPLAY is set.
 * PipeWire is started here because runit has no user service manager.
 * Portals are started here for the same reason. */
#define AUTOSTART \
	"foot --server", \
	"ubar", \
	"swaybg -i ~/Pictures/artix-wallpaper.png -m fill", \
	"/usr/lib/xdg-desktop-portal -r 2>/dev/null || /usr/libexec/xdg-desktop-portal -r 2>/dev/null || true", \
	"runsvdir ~/.local/share/runit/service", \
	NULL

/* compile-time rules */
#define RULE(appid, _title, ws, fl, fs) \
	{ .app_id = appid, .title = _title, .workspace = ws, \
	  .set_floating = fl, .set_fullscreen = fs },
/* Float portal file picker dialogs to prevent BSP tiling from breaking
 * popup positioning and file selection. */
#define RULES \
	RULE("xdg-desktop-portal-gtk", NULL, -1, true, false) \
	RULE("xdg-desktop-portal", NULL, -1, true, false) \
	RULE("org.freedesktop.impl.portal.desktop.gtk", NULL, -1, true, false) \
	RULE("org.gtk.Portal", NULL, -1, true, false)

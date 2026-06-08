# UWM Keybindings Reference

Keybindings are registered via `river_seat_v1.add_binding()` at startup and dispatched through the internal command table.

## Modifier Convention

| Symbol | Key |
|--------|-----|
| `$mod` | Super/Windows (Mod4) |
| `$alt` | Alt (Mod1) |
| `S` | Shift |

This follows the Sway/bspwm convention. `$mod` is the primary modifier throughout.

---

## Session & Lock

| Binding | Action | Notes |
|---------|--------|-------|
| `$mod + S + p` | Lock screen | `swaylock` equivalent |
| `$mod + $alt + q` | Quit UWM | `exit` / `quit` |
| `$mod + $alt + r` | Reload config | `reload` |
| `$mod + Escape` | Reload config | Alternative |

---

## Terminal

| Binding | Action |
|---------|--------|
| `$mod + Return` | Spawn terminal (`foot`) |
| `$mod + $alt + Return` | Spawn terminal (alternate) |

---

## Launchers

| Binding | Action |
|---------|--------|
| `$mod + r` | Application launcher (`rofi -show drun`) |
| `$mod + S + r` | Command runner (`rofi -show run`) |
| `$mod + Space` | Window switcher (`rofi -show window`) |

---

## File Management

| Binding | Action |
|---------|--------|
| `$mod + $alt + f` | Find file (fd + rofi + nvim) |
| `$mod + S + f` | File manager (`lf` in terminal) |

---

## Window Management

| Binding | Action | bspwm equivalent |
|---------|--------|------------------|
| `$mod + w` | Close focused window | `bspc node -c` |
| `$mod + S + w` | Kill focused window | `bspc node -k` |

---

## Focus Movement (Vim Keys)

| Binding | Direction | bspwm equivalent |
|---------|-----------|------------------|
| `$mod + h` | Focus left (west) | `bspc node -f west` |
| `$mod + j` | Focus down (south) | `bspc node -f south` |
| `$mod + k` | Focus up (north) | `bspc node -f north` |
| `$mod + l` | Focus right (east) | `bspc node -f east` |
| `$mod + c` | Focus next window | `bspc node -f next.local` |
| `$mod + i` | Focus next (alt) | `bspc wm -h off; bspc node newer -f` |
| `$mod + S + c` | Focus prev window | `bspc node -f prev.local` |
| `$mod + o` | Focus prev (alt) | `bspc wm -h off; bspc node older -f` |

---

## Move / Swap Windows

| Binding | Action | bspwm equivalent |
|---------|--------|------------------|
| `$mod + S + h` | Move window left | `bspc node -s west` |
| `$mod + S + j` | Move window down | `bspc node -s south` |
| `$mod + S + k` | Move window up | `bspc node -s north` |
| `$mod + S + l` | Move window right | `bspc node -s east` |
| `$mod + g` | Swap with biggest | `bspc node -s biggest.window` |

---

## Floating Window Movement (Arrow Keys)

| Binding | Action |
|---------|--------|
| `$mod + Left` | Move floating window left 20px |
| `$mod + Down` | Move floating window down 20px |
| `$mod + Up` | Move floating window up 20px |
| `$mod + Right` | Move floating window right 20px |

---

## Layout / States

| Binding | Action | bspwm equivalent |
|---------|--------|------------------|
| `$mod + m` | Toggle tabbed/split layout | `bspc desktop -l next` |
| `$mod + s` | Toggle floating | `bspc node -t floating` |
| `$mod + t` | Force tiled | `bspc node -t tiled` |
| `$mod + f` | Toggle fullscreen | `bspc node -t fullscreen` |
| `$mod + S + t` | Toggle split orientation | `layout toggle splitv/splith` |

---

## Resize

| Binding | Action | bspwm equivalent |
|---------|--------|------------------|
| `$mod + $alt + h` | Shrink width | `bspc node -z left -20 0` |
| `$mod + $alt + j` | Grow height | `bspc node -z bottom 0 20` |
| `$mod + $alt + k` | Shrink height | `bspc node -z top 0 -20` |
| `$mod + $alt + l` | Grow width | `bspc node -z right 20 0` |
| `$mod + $alt + S + h` | Grow width (inward) | `bspc node -z right -20 0` |
| `$mod + $alt + S + j` | Shrink height (inward) | `bspc node -z top 0 20` |
| `$mod + $alt + S + k` | Grow height (inward) | `bspc node -z bottom 0 -20` |
| `$mod + $alt + S + l` | Shrink width (inward) | `bspc node -z left 20 0` |

---

## Workspace Navigation

| Binding | Action | bspwm equivalent |
|---------|--------|------------------|
| `$mod + bracketleft` (`[`) | Previous workspace | `bspc desktop -f prev.local` |
| `$mod + bracketright` (`]`) | Next workspace | `bspc desktop -f next.local` |
| `$mod + grave` `` ` `` | Toggle last workspace | `bspc desktop -f last` |
| `$mod + Tab` | Toggle last workspace (alt) | `bspc desktop -f last` |

### Workspace Numbers (1–5)

| Binding | Action |
|---------|--------|
| `$mod + 1` | Go to workspace 1 |
| `$mod + 2` | Go to workspace 2 |
| `$mod + 3` | Go to workspace 3 |
| `$mod + 4` | Go to workspace 4 |
| `$mod + 5` | Go to workspace 5 |
| `$mod + S + 1` | Move window to workspace 1 |
| `$mod + S + 2` | Move window to workspace 2 |
| `$mod + S + 3` | Move window to workspace 3 |
| `$mod + S + 4` | Move window to workspace 4 |
| `$mod + S + 5` | Move window to workspace 5 |

---

## Media / Hardware Keys

| Binding | Action |
|---------|--------|
| `XF86AudioRaiseVolume` | Volume +5% (`pactl`) |
| `XF86AudioLowerVolume` | Volume -5% (`pactl`) |
| `XF86AudioMute` | Toggle mute (`pactl`) |
| `XF86MonBrightnessUp` | Brightness +10% (`brightnessctl`) |
| `XF86MonBrightnessDown` | Brightness -10% (`brightnessctl`) |

---

## Screenshot

| Binding | Action |
|---------|--------|
| `Print` | Region → file + clipboard (`grim + slurp`) |
| `$mod + Print` | Full screen → file (`grim`) |
| `$mod + S + s` | Region → clipboard only (`grim + slurp + wl-copy`) |

---

## Application Shortcuts

| Binding | Action |
|---------|--------|
| `$mod + S + b` | Browser (`ubrowser`) |
| `$mod + $alt + b` | Actions menu |
| `$mod + $alt + x` | Power menu |
| `$mod + $alt + Space` | Display mode |

---

## Notes

- These bindings are designed to match bspwm/sxhkd and Sway conventions for muscle-memory compatibility.
- The `$mod` key is **Super/Windows (Mod4)** — consistent across both configs.
- All bindings are registered at UWM startup via `river_seat_v1.add_binding()`.
- Unbound keys pass through to the focused window.
- `XF86*` keys (media/hardware) are usually physical keys on the keyboard.

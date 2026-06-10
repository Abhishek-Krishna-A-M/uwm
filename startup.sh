#!/bin/sh
# UWM startup script
# Launch session services before starting the compositor

# Start PipeWire audio
pipewire &
wireplumber &

# Start notification daemon
mako &

# Set wallpaper
swaybg -i ~/Pictures/artix-wallpaper.png &

# Start status bar
uwm-bar &

# Start compositor
exec uwm

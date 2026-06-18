#!/bin/sh
# UWM launcher — session creation only.
# Display-dependent services: config.def.h AUTOSTART
# Session daemons (pipewire, portals): user service manager

export XDG_CURRENT_DESKTOP=wlroots

exec dbus-run-session uwm

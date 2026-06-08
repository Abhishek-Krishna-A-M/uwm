#ifndef UWM_REGISTRY_H
#define UWM_REGISTRY_H

#include <wayland-client.h>
#include <stdbool.h>

void setup_registry_listener(struct wl_registry *registry);
bool verify_river_globals(void);

#endif

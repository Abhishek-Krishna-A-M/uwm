#ifndef UWM_REGISTRY_H
#define UWM_REGISTRY_H

#include <wayland-client.h>
#include <stdbool.h>

void setup_registry_listener(struct wl_registry *registry);
bool verify_river_globals(void);
struct river_window_manager_v1 *registry_get_window_manager(void);
struct river_xkb_bindings_v1 *registry_get_xkb_bindings(void);

#endif

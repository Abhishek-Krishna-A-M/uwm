#include <stdio.h>
#include <string.h>
#include "registry.h"
#include "river-window-management-v1-protocol.h"
#include "river-xkb-bindings-v1-protocol.h"

// Static local tracking registers for v0.1 boundaries
static struct river_window_manager_v1 *window_manager = NULL;
static struct river_xkb_bindings_v1 *xkb_bindings = NULL;

static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t id, const char *interface, uint32_t version) {
    (void)data;

    if (strcmp(interface, "river_window_manager_v1") == 0) {
        printf("[uwm] Discovered global interface: %s (v%d)\n", interface, version);
        window_manager = wl_registry_bind(registry, id, &river_window_manager_v1_interface, 4);
    } else if (strcmp(interface, "river_xkb_bindings_v1") == 0) {
        printf("[uwm] Discovered global interface: %s (v%d)\n", interface, version);
        xkb_bindings = wl_registry_bind(registry, id, &river_xkb_bindings_v1_interface, 1);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t id) {
    (void)data;
    (void)registry;
    (void)id;
    // Log removal states if tracking objects get torn down mid-session
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove
};

void setup_registry_listener(struct wl_registry *registry) {
    wl_registry_add_listener(registry, &registry_listener, NULL);
}

bool verify_river_globals(void) {
    return (window_manager != NULL && xkb_bindings != NULL);
}

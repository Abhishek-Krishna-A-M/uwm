#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <wayland-client.h>
#include "registry.h"
#include "wm.h"

int main(void) {
    printf("[uwm] Booting up systems layer...\n");

    // 1. Connect to the Wayland display server socket
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "[uwm] CRITICAL: Failed to connect to Wayland display socket.\n");
        return EXIT_FAILURE;
    }
    printf("[uwm] Successfully hooked into WAYLAND_DISPLAY.\n");

    // 2. Fetch the central registry object
    struct wl_registry *registry = wl_display_get_registry(display);
    if (!registry) {
        fprintf(stderr, "[uwm] CRITICAL: Failed to get Wayland display registry.\n");
        wl_display_disconnect(display);
        return EXIT_FAILURE;
    }

    // 3. Attach our global registry listener
    setup_registry_listener(registry);

    // 4. Trigger synchronous roundtrip to force global discovery execution
    printf("[uwm] Synchronizing registry globals...\n");
    wl_display_roundtrip(display);

    // 5. Verify that River components exist on the display instance
    if (!verify_river_globals()) {
        fprintf(stderr, "[uwm] CRITICAL: Required River compositor protocols not found.\n");
        wl_registry_destroy(registry);
        wl_display_disconnect(display);
        return EXIT_FAILURE;
    }

    // 6. Enter our low-overhead execution loop
    printf("[uwm] V0.1 interface online. Intercepting compositor channels...\n");
    while (true) {
        if (wl_display_dispatch(display) == -1) {
            fprintf(stderr, "[uwm] Runtime failure inside display dispatch matrix.\n");
            break;
        }
    }

    // Cleanup sequence
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    printf("[uwm] Lifecycle cleanly terminated.\n");
    return EXIT_SUCCESS;
}

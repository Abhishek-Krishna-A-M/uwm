#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <wayland-client.h>
#include "registry.h"
#include "wm.h"

int main(void) {
    printf("[uwm] Booting up systems layer...\n");

    // Automatically reap zombie child processes (crucial for forking terminals/apps)
    signal(SIGCHLD, SIG_IGN);

    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "[uwm] CRITICAL: Failed to connect to Wayland display socket.\n");
        return EXIT_FAILURE;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    setup_registry_listener(registry);

    printf("[uwm] Synchronizing registry globals...\n");
    if (wl_display_roundtrip(display) < 0) {
        fprintf(stderr, "[uwm] CRITICAL: Wayland socket roundtrip failed.\n");
        return EXIT_FAILURE;
    }

    if (!verify_river_globals()) {
        fprintf(stderr, "[uwm] CRITICAL: Required River compositor protocols not found.\n");
        return EXIT_FAILURE;
    }

    // Initialize the core window management lifecycle
    wm_init();

    printf("[uwm] V0.1 interface online. Intercepting compositor channels...\n");
    while (true) {
        if (wl_display_dispatch(display) == -1) {
            fprintf(stderr, "[uwm] Runtime failure inside display dispatch matrix.\n");
            break;
        }
    }

    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    return EXIT_SUCCESS;
}

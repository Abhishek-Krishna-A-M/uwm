#include <stdio.h>
#include "wm.h"
#include "registry.h"
#include "seat.h"
#include "river-window-management-v1-protocol.h"

static void handle_window(void *data, struct river_window_manager_v1 *wm, struct river_window_v1 *window) {
    (void)data; (void)wm; (void)window;
}

static void handle_output(void *data, struct river_window_manager_v1 *wm, struct river_output_v1 *output) {
    (void)data; (void)wm; (void)output;
}

// River pushes the seat object directly to us here
static void handle_seat(void *data, struct river_window_manager_v1 *wm, struct river_seat_v1 *seat) {
    (void)data; (void)wm;
    seat_handle_add(seat);
}

static void handle_manage_start(void *data, struct river_window_manager_v1 *wm) {
    (void)data; (void)wm;
}

static void handle_render_start(void *data, struct river_window_manager_v1 *wm) {
    (void)data; (void)wm;
}

// Added the missing child interface handlers to prevent Wayland segfaults
static const struct river_window_manager_v1_listener wm_listener = {
    .window = handle_window,
    .output = handle_output,
    .seat = handle_seat,
    .manage_start = handle_manage_start,
    .render_start = handle_render_start
};

void wm_init(void) {
    struct river_window_manager_v1 *wm = registry_get_window_manager();
    if (wm) {
        river_window_manager_v1_add_listener(wm, &wm_listener, NULL);
        printf("[uwm] Layout matrix initialized. Listening for geometry cycles...\n");
    }
}

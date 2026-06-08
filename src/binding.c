#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include "binding.h"
#include "registry.h"
#include "river-xkb-bindings-v1-protocol.h"

// Mod4 (Super) is bit 6, which equals 64 in River's modifier bitmask table
#define UWM_MOD_SUPER 64 

// Define this globally so the compiler can recognize the type
struct uwm_binding {
    struct river_xkb_binding_v1 *obj;
};

// Listeners must be defined at the top level
static void handle_pressed(void *data, struct river_xkb_binding_v1 *obj) {
    (void)data; (void)obj;
    printf("[uwm] Super+Return pressed! Forking terminal...\n");
    
    if (fork() == 0) {
        execlp("st", "st", (char *)NULL);
        exit(0);
    }
}

static void handle_released(void *data, struct river_xkb_binding_v1 *obj) {
    (void)data; (void)obj;
}

static const struct river_xkb_binding_v1_listener binding_listener = {
    .pressed = handle_pressed,
    .released = handle_released
};

// Now accepts the correct river_seat_v1 pointer
void binding_register_defaults(struct river_seat_v1 *seat) {
    struct river_xkb_bindings_v1 *xkb_bindings = registry_get_xkb_bindings();
    if (!xkb_bindings) {
        fprintf(stderr, "[uwm] Error: XKB protocol not initialized.\n");
        return;
    }

    printf("[uwm] Registering global binding: Super+Return -> st\n");

    struct uwm_binding *b = malloc(sizeof(struct uwm_binding));

    b->obj = river_xkb_bindings_v1_get_xkb_binding(
        xkb_bindings,
        seat,
        XKB_KEY_Return,
        UWM_MOD_SUPER
    );

    river_xkb_binding_v1_add_listener(b->obj, &binding_listener, b);
    river_xkb_binding_v1_enable(b->obj);
}

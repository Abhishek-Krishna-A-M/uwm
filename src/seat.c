#include <stdio.h>
#include "seat.h"
#include "binding.h"

void seat_handle_add(struct river_seat_v1 *seat) {
    printf("[uwm] Initializing River seat context.\n");
    binding_register_defaults(seat);
}

#ifndef TRANSIENT_H
#define TRANSIENT_H
#include "server.h"
struct uwm_transient_entry {
	struct wl_list link;
	struct wlr_seat *seat;
	struct wl_listener seat_destroy;
};
void handle_transient_seat_create(struct wl_listener *listener, void *data);
#endif

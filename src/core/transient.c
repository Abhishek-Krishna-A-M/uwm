#include "transient.h"
#include "server.h"
#include <wlr/types/wlr_transient_seat_v1.h>
#include <wlr/types/wlr_seat.h>
#include <inttypes.h>

static void handle_transient_destroy(struct wl_listener *listener, void *data) {
	struct uwm_transient_entry *e = wl_container_of(listener, e, seat_destroy);
	wl_list_remove(&e->link);
	wl_list_remove(&e->seat_destroy.link);
	free(e);
}

void handle_transient_seat_create(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, transient_seat_create);
	struct wlr_transient_seat_v1 *transient_seat = data;
	static uint64_t i;
	char name[64];
	snprintf(name, sizeof(name), "transient-%" PRIx64, i++);
	struct wlr_seat *new_seat = wlr_seat_create(server->wl_display, name);
	if (new_seat) {
		wlr_seat_set_capabilities(new_seat, WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);
		wlr_transient_seat_v1_ready(transient_seat, new_seat);
		struct uwm_transient_entry *e = calloc(1, sizeof(*e));
		if (e) {
			e->seat = new_seat;
			e->seat_destroy.notify = handle_transient_destroy;
			wl_signal_add(&new_seat->events.destroy, &e->seat_destroy);
			wl_list_insert(&server->transient_seats, &e->link);
		}
	} else {
		wlr_transient_seat_v1_deny(transient_seat);
	}
}

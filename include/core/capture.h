#ifndef CAPTURE_H
#define CAPTURE_H

#include "server.h"

void handle_new_foreign_toplevel_capture_request(struct wl_listener *listener, void *data);
void handle_new_capture_session(struct wl_listener *listener, void *data);

#endif

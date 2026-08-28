#ifndef OUTPUT_MANAGER_H
#define OUTPUT_MANAGER_H
#include "server.h"
void handle_output_manager_apply(struct wl_listener *listener, void *data);
void handle_output_manager_test(struct wl_listener *listener, void *data);
void handle_cursor_shape_request(struct wl_listener *listener, void *data);
#endif

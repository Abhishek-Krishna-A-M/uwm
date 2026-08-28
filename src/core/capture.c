#include "capture.h"
#include "window.h"
#include "output.h"
#include <wlr/util/log.h>
#include <wlr/types/wlr_output.h>

void handle_new_foreign_toplevel_capture_request(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, new_foreign_toplevel_capture_request);
	struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request *request = data;
	struct uwm_toplevel *toplevel = request->toplevel_handle->data;

	if (!toplevel)
		return;

	if (!toplevel->image_capture_scene) {
		toplevel->image_capture_scene = wlr_scene_create();
		toplevel->image_capture_scene->restack_xwayland_surfaces = false;
		struct wlr_surface *surf = toplevel_surface(toplevel);
		if (toplevel->type == UWM_TOPLEVEL_XDG && toplevel->xdg_toplevel)
			wlr_scene_xdg_surface_create(&toplevel->image_capture_scene->tree, toplevel->xdg_toplevel->base);
		else if (surf)
			wlr_scene_surface_create(&toplevel->image_capture_scene->tree, surf);
	}

	struct wlr_ext_image_capture_source_v1 *source =
		wlr_ext_image_capture_source_v1_create_with_scene_node(
			&toplevel->image_capture_scene->tree.node,
			wl_display_get_event_loop(server->wl_display),
			server->allocator,
			server->renderer);
	if (source) {
		wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request_accept(
			request, source);
	} else {
		wlr_log(WLR_ERROR, "Failed to create capture source for toplevel");
	}
}

void handle_new_capture_session(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, new_capture_session);
	struct wlr_ext_image_copy_capture_session_v1 *session = data;
	wlr_log(WLR_INFO, "New ext-image-copy-capture session created for source %p",
		(void *)session->source);
	struct uwm_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		wlr_output_schedule_frame(output->wlr_output);
	}
}

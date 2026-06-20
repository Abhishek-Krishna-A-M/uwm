#ifndef SERVER_H
#define SERVER_H

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_transient_seat_v1.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include "config.h"
#include "workspace.h"
#include "layer_shell.h"
#include "idle_inhibit.h"
#include "session_lock.h"

/* Forward declaration to break circular dependency */
struct uwm_toplevel;

enum uwm_cursor_mode {
	UWM_CURSOR_PASSTHROUGH,
	UWM_CURSOR_MOVE,
	UWM_CURSOR_RESIZE,
};

struct uwm_server {
	struct wl_display *wl_display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;
	struct wlr_scene_tree *tiled_layer;
	struct wlr_scene_tree *floating_layer;
	struct wlr_scene_output_layout *scene_layout;
	struct uwm_bsp_pool bsp_pool;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;
	struct wl_list toplevels;

	struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager;
	struct wl_listener new_toplevel_decoration;

	struct wlr_server_decoration_manager *server_decoration_manager;

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;
	struct wl_listener cursor_swipe_begin;
	struct wl_listener cursor_swipe_update;
	struct wl_listener cursor_swipe_end;
	struct wl_listener cursor_pinch_begin;
	struct wl_listener cursor_pinch_update;
	struct wl_listener cursor_pinch_end;
	struct wl_listener cursor_hold_begin;
	struct wl_listener cursor_hold_end;

	struct wlr_seat *seat;
	struct wlr_pointer_gestures_v1 *pointer_gestures;
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener pointer_focus_change;
	struct wl_listener request_set_selection;
	struct wl_listener request_set_primary_selection;
	struct wl_list keyboards;
	enum uwm_cursor_mode cursor_mode;
	struct uwm_toplevel *grabbed_toplevel;
	double grab_x, grab_y;
	struct wlr_box grab_geobox;
	uint32_t resize_edges;
	uint32_t last_button_serial;

	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct wl_listener new_output;
	struct wl_listener output_layout_change;

	struct uwm_output *active_output;          /* output with keyboard focus */

	struct uwm_workspace_manager workspaces;
	struct wlr_session *session;
	struct uwm_config config;
	struct wl_listener renderer_lost;
	bool locked;                              /* session lock active */

	/* Layer shell support */
	struct uwm_layer_shell layer_shell;
	struct wl_list layer_surfaces;

	/* Idle inhibitor support */
	struct uwm_idle_inhibit idle_inhibit;
	struct wl_list idle_inhibitors;

	/* Screencopy & screen sharing support */
	struct wlr_screencopy_manager_v1 *screencopy_manager;
	struct wlr_ext_image_copy_capture_manager_v1 *ext_image_copy_capture_manager;
	struct wlr_ext_output_image_capture_source_manager_v1 *output_capture_source_manager;
	struct wlr_ext_foreign_toplevel_list_v1 *foreign_toplevel_list;
	struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_manager;
	struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1 *foreign_toplevel_capture_source_manager;
	struct wl_listener new_foreign_toplevel_capture_request;
	struct wl_listener new_capture_session;
	struct wlr_output_manager_v1 *output_manager_v1;
	struct wl_listener output_manager_apply;
	struct wl_listener output_manager_test;
	struct wlr_export_dmabuf_manager_v1 *export_dmabuf_manager;
	struct wlr_linux_dmabuf_v1 *linux_dmabuf_v1;

	/* Transient seat protocol support */
	struct wlr_transient_seat_manager_v1 *transient_seat_manager;
	struct wl_listener transient_seat_create;

	/* UWM bar protocol */
	struct uwm_bar_manager *bar_manager;
	struct wl_event_source *bar_idle_source;

	/* Session lock protocol */
	struct uwm_session_lock session_lock;
};

bool server_init(struct uwm_server *server);
void server_finish(struct uwm_server *server);

#endif /* SERVER_H */

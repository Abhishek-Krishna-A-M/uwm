#include <stdbool.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_ext_data_control_v1.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include "server.h"
#include "input.h"
#include "output.h"
#include "window.h"
#include "layer_shell.h"
#include "idle_inhibit.h"
#include "uwm_bar.h"

static int handle_term_signal(int signo, void *data) {
	struct uwm_server *server = data;
	wlr_log(WLR_INFO, "Received signal %d, terminating compositor", signo);
	wl_display_terminate(server->wl_display);
	return 0;
}

static void handle_renderer_lost(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, renderer_lost);
}

static void handle_session_active(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, session_active);

	if (server->session->active) {
		struct uwm_output *output;
		wl_list_for_each(output, &server->outputs, link) {
			wlr_output_schedule_frame(output->wlr_output);
		}
	}
}

/* Crash-recovery state for session signal emissions.
 * We save the listener list at startup so we can rebuild it after
 * a crash in wl_signal_emit_mutable (the libinput handler).  This
 * avoids accessing the dangling cursor/end markers that
 * wl_signal_emit_mutable leaves on the stack after siglongjmp. */
static struct wl_listener *g_saved_listeners[64];
static int g_n_saved;
sigjmp_buf g_crash_jmpbuf;
volatile sig_atomic_t g_crash_jmpbuf_valid;

void uwm_save_session_listeners(struct uwm_server *server) {
	struct wl_signal *sig = &server->session->events.active;
	struct wl_list *head = &sig->listener_list;
	g_n_saved = 0;
	struct wl_list *pos = head->next;
	while (pos != head && g_n_saved < 64) {
		struct wl_listener *l = wl_container_of(pos, l, link);
		g_saved_listeners[g_n_saved++] = l;
		pos = pos->next;
	}
}

void uwm_rebuild_session_listeners(struct uwm_server *server) {
	struct wl_signal *sig = &server->session->events.active;
	struct wl_list *head = &sig->listener_list;
	wl_list_init(head);
	for (int i = 0; i < g_n_saved; i++) {
		wl_list_insert(head->prev, &g_saved_listeners[i]->link);
	}
}

void uwm_call_session_active(struct uwm_server *server) {
	handle_session_active(&server->session_active, NULL);
}

/* Sentinel placed at the HEAD of the session signal list.
 * It fires FIRST, before all other listeners (DRM, libinput, our handler).
 * We call handle_session_active here so it runs even if a later
 * listener (libinput) crashes and kills the process.
 * Crash protection: main() wraps wl_display_run with sigsetjmp. */
static struct wl_listener g_session_active_sentinel;
static struct uwm_server *g_sentinel_server;

static void handle_session_active_sentinel(struct wl_listener *listener, void *data) {
	/* Call our handler NOW, before the DRM/libinput handlers fire.
	 * Crash recovery in main() catches any fatal signal and
	 * rebuilds the listener list + calls our handler. */
	handle_session_active(&g_sentinel_server->session_active, data);
}

static void handle_new_foreign_toplevel_capture_request(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, new_foreign_toplevel_capture_request);
	struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request *request = data;
	struct uwm_toplevel *toplevel = request->toplevel_handle->data;

	if (!toplevel)
		return;

	/* Lazily create the per-window capture scene on first request */
	if (!toplevel->image_capture_scene) {
		toplevel->image_capture_scene = wlr_scene_create();
		toplevel->image_capture_scene->restack_xwayland_surfaces = false;
		wlr_scene_xdg_surface_create(&toplevel->image_capture_scene->tree,
			toplevel->xdg_toplevel->base);
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

static void handle_new_capture_session(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, new_capture_session);
	struct wlr_ext_image_copy_capture_session_v1 *session = data;
	wlr_log(WLR_INFO, "New ext-image-copy-capture session created for source %p",
		(void *)session->source);
	struct uwm_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		wlr_output_schedule_frame(output->wlr_output);
	}
}

static void handle_transient_seat_create(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, transient_seat_create);
	struct wlr_transient_seat_v1 *transient_seat = data;
	static uint64_t i;
	char name[64];
	snprintf(name, sizeof(name), "transient-%" PRIx64, i++);
	struct wlr_seat *new_seat = wlr_seat_create(server->wl_display, name);
	if (new_seat) {
		wlr_seat_set_capabilities(new_seat, WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);
		wlr_transient_seat_v1_ready(transient_seat, new_seat);
	} else {
		wlr_transient_seat_v1_deny(transient_seat);
	}
}

static void handle_output_manager_apply(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, output_manager_apply);
	struct wlr_output_configuration_v1 *config = data;

	struct wlr_output_configuration_head_v1 *config_head;
	wl_list_for_each(config_head, &config->heads, link) {
		struct wlr_output *wlr_output = config_head->state.output;
		struct wlr_output_state state;
		wlr_output_state_init(&state);
		wlr_output_head_v1_state_apply(&config_head->state, &state);
		wlr_output_commit_state(wlr_output, &state);
		wlr_output_state_finish(&state);
	}

	wl_list_for_each(config_head, &config->heads, link) {
		struct wlr_output *wlr_output = config_head->state.output;
		wlr_output_layout_add(server->output_layout, wlr_output,
			config_head->state.x, config_head->state.y);
	}

	wlr_output_manager_v1_set_configuration(server->output_manager_v1, config);
	wlr_output_configuration_v1_send_succeeded(config);
}

static void handle_cursor_shape_request(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, cursor_shape_request);
	struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;

	if (event->device_type != WLR_CURSOR_SHAPE_MANAGER_V1_DEVICE_TYPE_POINTER) {
		return;
	}

	struct wlr_seat_client *focused_client =
		server->seat->pointer_state.focused_client;
	if (focused_client != event->seat_client) {
		return;
	}

	const char *name = wlr_cursor_shape_v1_name(event->shape);
	if (name) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, name);
	}
}

static void handle_output_manager_test(struct wl_listener *listener, void *data) {
	struct uwm_server *server = wl_container_of(listener, server, output_manager_test);
	struct wlr_output_configuration_v1 *config = data;

	struct wlr_output_configuration_head_v1 *config_head;
	bool ok = true;
	wl_list_for_each(config_head, &config->heads, link) {
		struct wlr_output *wlr_output = config_head->state.output;
		struct wlr_output_state state;
		wlr_output_state_init(&state);
		wlr_output_head_v1_state_apply(&config_head->state, &state);
		if (!wlr_output_test_state(wlr_output, &state)) {
			ok = false;
			wlr_output_state_finish(&state);
			break;
		}
		wlr_output_state_finish(&state);
	}

	if (ok)
		wlr_output_configuration_v1_send_succeeded(config);
	else
		wlr_output_configuration_v1_send_failed(config);
}

bool server_init(struct uwm_server *server) {
	/* Set up signal handling with SA_RESTART to prevent signals from
	 * interrupting blocking syscalls (crucial during VT switch).
	 * SIGCHLD: prevent zombie processes from fork() calls.
	 * SIGPIPE: prevent broken pipes (e.g. from PipeWire dying on VT switch)
	 *          from killing the compositor.
	 * SIGHUP: logind may revoke the controlling terminal during VT switch,
	 *         which sends SIGHUP to the foreground process group. Ignore it;
	 *         wlroots handles session pause/resume via libseat/logind.
	 * SIGTERM: logind sends SIGTERM when disabling a seat during VT switch.
	 *          Ignore it — the event loop handles SIGINT for clean exit. */
	struct sigaction sa_ign = { .sa_handler = SIG_IGN, .sa_flags = SA_RESTART };
	sigaction(SIGCHLD, &sa_ign, NULL);
	sigaction(SIGPIPE, &sa_ign, NULL);
	sigaction(SIGHUP, &sa_ign, NULL);
	sigaction(SIGTERM, &sa_ign, NULL);
	config_load(&server->config);

	/* The Wayland display is managed by libwayland. It handles accepting
	 * clients from the Unix socket, managing Wayland globals, and so on. */
	server->wl_display = wl_display_create();

	/* Initialize all wl_lists early so that cleanup paths (goto err) can
	 * safely iterate or wl_list_remove on them regardless of how far init
	 * got before failure. */
	wl_list_init(&server->layer_surfaces);
	wl_list_init(&server->idle_inhibitors);
	wl_list_init(&server->outputs);
	wl_list_init(&server->toplevels);
	wl_list_init(&server->keyboards);

	/* The backend is a wlroots feature which abstracts the underlying input and
	 * output hardware. The autocreate option will choose the most suitable
	 * backend based on the current environment, such as opening an X11 window
	 * if an X11 server is running. */
	server->backend = wlr_backend_autocreate(wl_display_get_event_loop(server->wl_display), &server->session);
	if (server->backend == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_backend");
		wlr_log(WLR_ERROR, "If running on a TTY, ensure a session manager is available:");
		wlr_log(WLR_ERROR, "  - seatd (recommended): https://git.sr.ht/~kennylevinsen/seatd");
		wlr_log(WLR_ERROR, "  - elogind: https://github.com/elogind/elogind");
		wl_display_destroy(server->wl_display);
		return false;
	}
	if (server->session == NULL) {
		wlr_log(WLR_ERROR, "No session backend available. VT switching will not work.");
		wlr_log(WLR_ERROR, "Install seatd or elogind for session management.");
	} else {
		g_sentinel_server = server;

		server->session_active.notify = handle_session_active;
		wl_signal_add(&server->session->events.active, &server->session_active);

		/* Insert a sentinel at the HEAD of the signal list.
		 * This fires BEFORE all other listeners (DRM, libinput, and
		 * our main handler). The sentinel calls our handler early
		 * so it runs even if a later listener crashes. */
		g_session_active_sentinel.notify = handle_session_active_sentinel;
		wl_list_insert(&server->session->events.active.listener_list,
			&g_session_active_sentinel.link);

		/* Save listener list for crash recovery in main().
		 * Must happen after ALL session listeners are registered. */
		uwm_save_session_listeners(server);
	}

	/* Autocreates a renderer, either Pixman, GLES2 or Vulkan for us. The user
	 * can also specify a renderer using the WLR_RENDERER env var.
	 * The renderer is responsible for defining the various pixel formats it
	 * supports for shared memory, this configures that for clients. */
	server->renderer = wlr_renderer_autocreate(server->backend);
	if (server->renderer == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_renderer");
		wlr_backend_destroy(server->backend);
		if (server->session)
			wlr_session_destroy(server->session);
		wl_display_destroy(server->wl_display);
		return false;
	}

	/* Initialize wl_shm only (not init_wl_display, which would also create
	 * a linux-dmabuf global without scene feedback). We create the single
	 * linux-dmabuf ourselves later and wire it to the scene so that
	 * xdg-desktop-portal-wlr gets proper DMA-BUF format/modifier info for
	 * screen capture. */
	wlr_renderer_init_wl_shm(server->renderer, server->wl_display);

	server->renderer_lost.notify = handle_renderer_lost;
	wl_signal_add(&server->renderer->events.lost, &server->renderer_lost);

	/* Autocreates an allocator for us.
	 * The allocator is the bridge between the renderer and the backend. It
	 * handles the buffer creation, allowing wlroots to render onto the screen */
	server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
	if (server->allocator == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_allocator");
		wl_list_remove(&server->renderer_lost.link);
		wlr_renderer_destroy(server->renderer);
		wlr_backend_destroy(server->backend);
		if (server->session)
			wlr_session_destroy(server->session);
		wl_display_destroy(server->wl_display);
		return false;
	}

	/* This creates some hands-off wlroots interfaces. The compositor is
	 * necessary for clients to allocate surfaces, the subcompositor allows to
	 * assign the role of subsurfaces to surfaces and the data device manager
	 * handles the clipboard. Each of these wlroots interfaces has room for you
	 * to dig your fingers in and play with their behavior if you want. Note that
	 * the clients cannot set the selection directly without compositor approval,
	 * see the handling of the request_set_selection event below.*/
	server->compositor = wlr_compositor_create(server->wl_display, 5, server->renderer);
	wlr_subcompositor_create(server->wl_display);
	wlr_data_device_manager_create(server->wl_display);
	wlr_primary_selection_v1_device_manager_create(server->wl_display);
	wlr_data_control_manager_v1_create(server->wl_display);
	wlr_ext_data_control_manager_v1_create(server->wl_display, 1);

	/* Transient seat protocol: allows clipboard helpers and similar to request
	 * a separate seat so their keyboard focus doesn't interfere with the main
	 * seat. The wlr_seat they get is independent — they can request keyboard
	 * focus on it without stealing it from the main seat. */
	server->transient_seat_manager = wlr_transient_seat_manager_v1_create(server->wl_display);
	if (server->transient_seat_manager) {
		server->transient_seat_create.notify = handle_transient_seat_create;
		wl_signal_add(&server->transient_seat_manager->events.create_seat,
			&server->transient_seat_create);
	}

	/* Handle SIGINT via the Wayland event loop so we can cleanly
	 * shut down (destroy clients, release DRM master, etc.).
	 * SIGTERM is ignored (see above) to prevent logind from killing
	 * the compositor during VT switch — use Ctrl+C (SIGINT) to exit. */
	struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
	wl_event_loop_add_signal(loop, SIGINT, handle_term_signal, server);

	/* Creates an output layout, which a wlroots utility for working with an
	 * arrangement of screens in a physical layout. */
	server->output_layout = wlr_output_layout_create(server->wl_display);

	/* Listen for output layout changes (position updates) */
	server->output_layout_change.notify = handle_output_layout_change;
	wl_signal_add(&server->output_layout->events.change,
		&server->output_layout_change);

	/* Configure a listener to be notified when new outputs are available on the
	 * backend. */
	wl_list_init(&server->outputs);
	server->active_output = NULL;
	server->new_output.notify = server_new_output;
	wl_signal_add(&server->backend->events.new_output, &server->new_output);

	/* Create a scene graph. This is a wlroots abstraction that handles all
	 * rendering and damage tracking. All the compositor author needs to do
	 * is add things that should be rendered to the scene graph at the proper
	 * positions and then call wlr_scene_output_commit() to render a frame if
	 * necessary.
	 */
	server->scene = wlr_scene_create();
	/* Scene graph z-order (bottom to top):
	 * [tiled_layer] -> [floating_layer] -> [output layout (windows)] ->
	 * layer_top -> layer_overlay
	 *
	 * scene_layout (output layout) is created first, then tiled/floating
	 * layers are raised above it. Per-output layer trees (background,
	 * bottom, top, overlay) are created and positioned in server_new_output(). */
	server->scene_layout = wlr_scene_attach_output_layout(server->scene, server->output_layout);
	server->tiled_layer = wlr_scene_tree_create(&server->scene->tree);
	server->floating_layer = wlr_scene_tree_create(&server->scene->tree);

	/* Raise tiled and floating layers above scene_layout so windows
	 * render on top of the output layout's background content */
	wlr_scene_node_raise_to_top(&server->tiled_layer->node);
	wlr_scene_node_raise_to_top(&server->floating_layer->node);

	/* Create the linux-dmabuf protocol and wire it to the scene graph.
	 * This single global (v5, with scene feedback) is what
	 * xdg-desktop-portal-wlr needs for PipeWire-based screen capture.
	 * Must be called after scene creation — the scene must exist before
	 * we can wire the dmabuf feedback to it. */
	if (wlr_renderer_get_texture_formats(server->renderer, WLR_BUFFER_CAP_DMABUF)) {
		server->linux_dmabuf_v1 = wlr_linux_dmabuf_v1_create_with_renderer(
			server->wl_display, 5, server->renderer);
		wlr_scene_set_linux_dmabuf_v1(server->scene, server->linux_dmabuf_v1);
	}

	/* Set up xdg-shell version 3. The xdg-shell is a Wayland protocol which is
	 * used for application windows. For more detail on shells, refer to
	 * https://drewdevault.com/2018/07/29/Wayland-shells.html.
	 */
	server->xdg_shell = wlr_xdg_shell_create(server->wl_display, 3);
	server->new_xdg_toplevel.notify = server_new_xdg_toplevel;
	wl_signal_add(&server->xdg_shell->events.new_toplevel, &server->new_xdg_toplevel);
	server->new_xdg_popup.notify = server_new_xdg_popup;
	wl_signal_add(&server->xdg_shell->events.new_popup, &server->new_xdg_popup);

	/* Set up xdg-decoration protocol to request no decorations from clients.
	 * This tells clients not to draw their own title bars or borders. */
	server->xdg_decoration_manager = wlr_xdg_decoration_manager_v1_create(
		server->wl_display);
	if (server->xdg_decoration_manager) {
		server->new_toplevel_decoration.notify = server_new_toplevel_decoration;
		wl_signal_add(&server->xdg_decoration_manager->events.new_toplevel_decoration,
			&server->new_toplevel_decoration);
	}

	/* Set up KDE server-decoration protocol. Some clients (e.g. GTK3) use
	 * org_kde_kwin_server_decoration as their primary decoration negotiation
	 * mechanism instead of xdg-decoration. Without this, GTK3 apps may
	 * default to client-side decorations. */
	server->server_decoration_manager = wlr_server_decoration_manager_create(
		server->wl_display);
	if (server->server_decoration_manager) {
		wlr_server_decoration_manager_set_default_mode(
			server->server_decoration_manager,
			WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
	}

	/* Set up output management and xdg-output protocol. These allow clients
	 * like slurp and grim to query the output layout (position, size, scale). */
	server->output_manager_v1 = wlr_output_manager_v1_create(server->wl_display);
	server->output_manager_apply.notify = handle_output_manager_apply;
	wl_signal_add(&server->output_manager_v1->events.apply,
		&server->output_manager_apply);
	server->output_manager_test.notify = handle_output_manager_test;
	wl_signal_add(&server->output_manager_v1->events.test,
		&server->output_manager_test);
	if (!wlr_xdg_output_manager_v1_create(server->wl_display, server->output_layout)) {
		wlr_log(WLR_ERROR, "Failed to create xdg-output manager");
	}

	/* Creates a cursor, which is a wlroots utility for tracking the cursor
	 * image shown on screen.
	 */
	server->cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server->cursor, server->output_layout);

	/* Creates an xcursor manager, another wlroots utility which loads up
	 * Xcursor themes to source cursor images from and makes sure that cursor
	 * images are available at all scale factors on the screen (necessary for
	 * HiDPI support). */
	server->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
	wlr_xcursor_manager_load(server->cursor_mgr, 1);
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");

	/* wlr_cursor *only* displays an image on screen. It does not move around
	 * when the pointer moves. However, we can attach input devices to it, and
	 * it will generate aggregate events for all of them. In these events, we
	 * can choose how we want to process them, forwarding them to clients and
	 * moving the cursor around. More detail on this process is described in
	 * https://drewdevault.com/2018/07/17/Input-handling-in-wlroots.html. */
	server->cursor_mode = UWM_CURSOR_PASSTHROUGH;
	server->cursor_motion.notify = server_cursor_motion;
	wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);
	server->cursor_motion_absolute.notify = server_cursor_motion_absolute;
	wl_signal_add(&server->cursor->events.motion_absolute, &server->cursor_motion_absolute);
	server->cursor_button.notify = server_cursor_button;
	wl_signal_add(&server->cursor->events.button, &server->cursor_button);
	server->cursor_axis.notify = server_cursor_axis;
	wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);
	server->cursor_frame.notify = server_cursor_frame;
	wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);

	server->cursor_swipe_begin.notify = server_cursor_swipe_begin;
	wl_signal_add(&server->cursor->events.swipe_begin, &server->cursor_swipe_begin);
	server->cursor_swipe_update.notify = server_cursor_swipe_update;
	wl_signal_add(&server->cursor->events.swipe_update, &server->cursor_swipe_update);
	server->cursor_swipe_end.notify = server_cursor_swipe_end;
	wl_signal_add(&server->cursor->events.swipe_end, &server->cursor_swipe_end);
	server->cursor_pinch_begin.notify = server_cursor_pinch_begin;
	wl_signal_add(&server->cursor->events.pinch_begin, &server->cursor_pinch_begin);
	server->cursor_pinch_update.notify = server_cursor_pinch_update;
	wl_signal_add(&server->cursor->events.pinch_update, &server->cursor_pinch_update);
	server->cursor_pinch_end.notify = server_cursor_pinch_end;
	wl_signal_add(&server->cursor->events.pinch_end, &server->cursor_pinch_end);
	server->cursor_hold_begin.notify = server_cursor_hold_begin;
	wl_signal_add(&server->cursor->events.hold_begin, &server->cursor_hold_begin);
	server->cursor_hold_end.notify = server_cursor_hold_end;
	wl_signal_add(&server->cursor->events.hold_end, &server->cursor_hold_end);

	/* Configures a seat, which is a single "seat" at which a user sits and
	 * operates the computer. This conceptually includes up to one keyboard,
	 * pointer, touch, and drawing tablet device. We also rig up a listener to
	 * let us know when new input devices are available on the backend.
	 */
	server->new_input.notify = server_new_input;
	wl_signal_add(&server->backend->events.new_input, &server->new_input);
	server->seat = wlr_seat_create(server->wl_display, "seat0");
	server->request_cursor.notify = seat_request_cursor;
	wl_signal_add(&server->seat->events.request_set_cursor, &server->request_cursor);
	server->pointer_focus_change.notify = seat_pointer_focus_change;
	wl_signal_add(&server->seat->pointer_state.events.focus_change, &server->pointer_focus_change);
	server->request_set_selection.notify = seat_request_set_selection;
	wl_signal_add(&server->seat->events.request_set_selection, &server->request_set_selection);
	server->request_set_primary_selection.notify = seat_request_set_primary_selection;
	wl_signal_add(&server->seat->events.request_set_primary_selection, &server->request_set_primary_selection);

	/* Create pointer gestures protocol for touchpad pinch/ swipe/hold support */
	server->pointer_gestures = wlr_pointer_gestures_v1_create(server->wl_display);

	/* Create cursor-shape-v1 manager for server-side cursor shapes.
	 * This allows clients (e.g. foot, fuzzel) to set cursor images
	 * via the cursor-shape-v1 protocol instead of uploading a surface.
	 * Version 2 adds DND_ASK and ALL_RESIZE shapes. */
	server->cursor_shape_manager = wlr_cursor_shape_manager_v1_create(
		server->wl_display, 2);
	if (server->cursor_shape_manager) {
		server->cursor_shape_request.notify = handle_cursor_shape_request;
		wl_signal_add(&server->cursor_shape_manager->events.request_set_shape,
			&server->cursor_shape_request);
	}

	bsp_pool_init(&server->bsp_pool);
	workspace_manager_init(&server->workspaces);

	for (uint32_t i = 0; i < UWM_WORKSPACE_COUNT; i++)
		server->workspaces.workspaces[i].focus_follows_pointer
			= server->config.focus_follows_pointer;

	/* Initialize layer shell protocol */
	if (!layer_shell_create(server)) {
		wlr_log(WLR_ERROR, "Failed to create layer shell");
		goto err;
	}

	/* Initialize UWM bar protocol */
	if (!uwm_bar_manager_create(server)) {
		wlr_log(WLR_ERROR, "Failed to create UWM bar manager");
		goto err;
	}

	/* Initialize idle inhibit protocol */
	if (!idle_inhibit_create(server)) {
		wlr_log(WLR_ERROR, "Failed to create idle inhibit");
		goto err;
	}

	/* Initialize session lock protocol */
	if (!session_lock_create(server)) {
		wlr_log(WLR_ERROR, "Failed to create session lock");
		goto err;
	}

	/* Initialize screencopy protocol */
	server->screencopy_manager = wlr_screencopy_manager_v1_create(server->wl_display);
	if (!server->screencopy_manager) {
		wlr_log(WLR_ERROR, "Failed to create screencopy manager");
		goto err;
	}

	/* Initialize ext-image-copy-capture protocol for per-window screen sharing.
	 * This is the modern replacement for wlr-screencopy, used by
	 * xdg-desktop-portal-wlr for window-level capture. Output capture
	 * uses the legacy screencopy path as primary (more reliable in
	 * wlroots 0.20.1), but per-window capture requires this protocol. */
	server->ext_image_copy_capture_manager =
		wlr_ext_image_copy_capture_manager_v1_create(server->wl_display, 1);
	if (!server->ext_image_copy_capture_manager) {
		wlr_log(WLR_ERROR, "Failed to create ext image copy capture manager");
		goto err;
	}
	server->new_capture_session.notify = handle_new_capture_session;
	wl_signal_add(&server->ext_image_copy_capture_manager->events.new_session,
		&server->new_capture_session);

	/* Initialize export-dmabuf protocol for direct DMA-BUF export.
	 * Used by capture tools and OBS for zero-copy frame access. */
	server->export_dmabuf_manager = wlr_export_dmabuf_manager_v1_create(server->wl_display);
	if (!server->export_dmabuf_manager) {
		wlr_log(WLR_ERROR, "Failed to create export dmabuf manager");
		goto err;
	}

	/* Output capture source manager — lets portal/clients select a specific
	 * output to capture. Required for xdg-desktop-portal-wlr monitor selection. */
	server->output_capture_source_manager =
		wlr_ext_output_image_capture_source_manager_v1_create(server->wl_display, 1);
	if (!server->output_capture_source_manager) {
		wlr_log(WLR_ERROR, "Failed to create ext output image capture source manager");
	}

	/* Foreign toplevel list — advertises windows to portal/clients so they
	 * can select a specific window to capture. Required for window sharing. */
	server->foreign_toplevel_list =
		wlr_ext_foreign_toplevel_list_v1_create(server->wl_display, 1);
	if (!server->foreign_toplevel_list) {
		wlr_log(WLR_ERROR, "Failed to create ext foreign toplevel list");
	}

	/* Legacy foreign toplevel management — used by wlrctl and other tools */
	server->foreign_toplevel_manager =
		wlr_foreign_toplevel_manager_v1_create(server->wl_display);
	if (!server->foreign_toplevel_manager) {
		wlr_log(WLR_ERROR, "Failed to create foreign toplevel manager");
	}

	/* Foreign toplevel image capture source — lets portal/clients select a
	 * specific window for per-window screen sharing. */
	server->foreign_toplevel_capture_source_manager =
		wlr_ext_foreign_toplevel_image_capture_source_manager_v1_create(server->wl_display, 1);
	if (server->foreign_toplevel_capture_source_manager) {
		server->new_foreign_toplevel_capture_request.notify =
			handle_new_foreign_toplevel_capture_request;
		wl_signal_add(
			&server->foreign_toplevel_capture_source_manager->events.new_request,
			&server->new_foreign_toplevel_capture_request);
	} else {
		wlr_log(WLR_ERROR, "Failed to create ext foreign toplevel image capture source manager");
	}

	return true;

err:
	uwm_bar_manager_destroy(server);
	session_lock_destroy(server);
	idle_inhibit_destroy(server);
	layer_shell_destroy(server);

	config_finish(&server->config);
	workspace_manager_finish(&server->workspaces, &server->bsp_pool);

	if (server->scene)
		wlr_scene_node_destroy(&server->scene->tree.node);
	if (server->cursor_mgr)
		wlr_xcursor_manager_destroy(server->cursor_mgr);
	if (server->cursor)
		wlr_cursor_destroy(server->cursor);
	if (server->allocator)
		wlr_allocator_destroy(server->allocator);
	if (server->renderer)
		wlr_renderer_destroy(server->renderer);
	if (server->backend)
		wlr_backend_destroy(server->backend);
	if (server->session)
		wlr_session_destroy(server->session);
	wl_display_destroy(server->wl_display);
	return false;
}

void server_finish(struct uwm_server *server) {
	wlr_log(WLR_INFO, "SERVER_FINISH BEGIN");

	/* Once wl_display_run returns, we destroy all clients then shut down the server. */
	wl_display_destroy_clients(server->wl_display);

	/* Clean up layer shell, idle inhibit, session lock, screencopy, and bar */
	layer_shell_destroy(server);
	idle_inhibit_destroy(server);
	session_lock_destroy(server);
	uwm_bar_manager_destroy(server);

	config_finish(&server->config);
	workspace_manager_finish(&server->workspaces, &server->bsp_pool);

	wl_list_remove(&server->new_xdg_toplevel.link);
	wl_list_remove(&server->new_xdg_popup.link);
	if (server->xdg_decoration_manager) {
		wl_list_remove(&server->new_toplevel_decoration.link);
	}
	if (server->transient_seat_manager) {
		wl_list_remove(&server->transient_seat_create.link);
	}
	if (server->foreign_toplevel_capture_source_manager) {
		wl_list_remove(&server->new_foreign_toplevel_capture_request.link);
	}
	if (server->ext_image_copy_capture_manager) {
		wl_list_remove(&server->new_capture_session.link);
	}

	wl_list_remove(&server->cursor_motion.link);
	wl_list_remove(&server->cursor_motion_absolute.link);
	wl_list_remove(&server->cursor_button.link);
	wl_list_remove(&server->cursor_axis.link);
	wl_list_remove(&server->cursor_frame.link);
	wl_list_remove(&server->cursor_swipe_begin.link);
	wl_list_remove(&server->cursor_swipe_update.link);
	wl_list_remove(&server->cursor_swipe_end.link);
	wl_list_remove(&server->cursor_pinch_begin.link);
	wl_list_remove(&server->cursor_pinch_update.link);
	wl_list_remove(&server->cursor_pinch_end.link);
	wl_list_remove(&server->cursor_hold_begin.link);
	wl_list_remove(&server->cursor_hold_end.link);

	if (server->cursor_shape_manager) {
		wl_list_remove(&server->cursor_shape_request.link);
	}

	wl_list_remove(&server->new_input.link);
	wl_list_remove(&server->request_cursor.link);
	wl_list_remove(&server->pointer_focus_change.link);
	wl_list_remove(&server->request_set_selection.link);
	wl_list_remove(&server->request_set_primary_selection.link);

	wl_list_remove(&server->new_output.link);
	wl_list_remove(&server->output_layout_change.link);
	wl_list_remove(&server->output_manager_apply.link);
	wl_list_remove(&server->output_manager_test.link);

	wl_list_remove(&server->renderer_lost.link);

	if (server->session) {
		wl_list_remove(&g_session_active_sentinel.link);
		wl_list_remove(&server->session_active.link);
	}

	/* Destroy backend before scene so output destroy handlers
	 * can safely access per-output scene nodes. */
	wlr_backend_destroy(server->backend);
	wlr_scene_node_destroy(&server->scene->tree.node);
	wlr_xcursor_manager_destroy(server->cursor_mgr);
	wlr_cursor_destroy(server->cursor);
	wlr_allocator_destroy(server->allocator);
	wlr_renderer_destroy(server->renderer);
	if (server->session)
		wlr_session_destroy(server->session);
	wl_display_destroy(server->wl_display);
	wlr_log(WLR_INFO, "SERVER_FINISH END");
}

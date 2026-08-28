#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>
#include <libinput.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include "input.h"
#include "config.h"
#include "window.h"
#include "bsp.h"
#include "floating.h"
#include "layout.h"
#include "server.h"
#include "output.h"

/* ========== Global server pointer for action functions ========== */
struct uwm_server *uwm_server;

/* ========== Workspace/arrangement helpers ========== */
static struct uwm_workspace *current_ws(void)
{
	if (uwm_server->active_output && uwm_server->active_output->current_workspace < UWM_WORKSPACE_COUNT)
		return &uwm_server->workspaces.workspaces[uwm_server->active_output->current_workspace];
	return &uwm_server->workspaces.workspaces[uwm_server->workspaces.current];
}

static void bsp_arrange_current_workspace(void)
{
	struct uwm_workspace *ws = current_ws();
	int x, y, w, h;
	get_output_size(ws, &x, &y, &w, &h);
	bsp_arrange(ws, x, y, w, h, uwm_server->config.inner_gap);
}

/* ========== Action functions — called via keys[] dispatch ========== */

void spawn(const union arg *arg)
{
	if (fork() == 0) {
		setsid();
		execvp((char *)arg->argv[0], (char **)arg->argv);
		_exit(1);
	}
}

void quit(const union arg *arg)
{
	(void)arg;
	wl_display_terminate(uwm_server->wl_display);
}

void closewindow(const union arg *arg)
{
	(void)arg;
	struct uwm_toplevel *focused = current_ws()->focused;
	if (focused) toplevel_send_close(focused);
}

void forceclose(const union arg *arg)
{
	(void)arg;
	struct uwm_toplevel *focused = current_ws()->focused;
	if (!focused) return;
	struct wlr_surface *surf = toplevel_surface(focused);
	if (surf) {
		struct wl_resource *res = surf->resource;
		/* for xdg, resource is xdg surface resource; for xwayland, wl_surface resource */
		if (res) {
			struct wl_client *client = wl_resource_get_client(res);
			if (client) wl_client_destroy(client);
		}
	}
}

/* focus movement — also moves floating windows */
static void focus_move(struct uwm_workspace *ws, int dx, int dy,
		struct uwm_toplevel *(*bsp_fn)(struct uwm_workspace *))
{
	if (ws->fullscreen_window)
		return;
	struct uwm_toplevel *focused = ws->focused;
	if (focused && focused->floating) {
		focused->float_x += dx;
		focused->float_y += dy;
		wlr_scene_node_set_position(&focused->scene_tree->node,
			focused->float_x, focused->float_y);
	} else {
		struct uwm_toplevel *target = bsp_fn(ws);
		if (target)
			focus_toplevel(target);
	}
}

void moveleft(const union arg *arg)  { (void)arg; focus_move(current_ws(), -20, 0, bsp_focus_left); }
void moveright(const union arg *arg) { (void)arg; focus_move(current_ws(), 20, 0, bsp_focus_right); }
void moveup(const union arg *arg)    { (void)arg; focus_move(current_ws(), 0, -20, bsp_focus_up); }
void movedown(const union arg *arg)  { (void)arg; focus_move(current_ws(), 0, 20, bsp_focus_down); }

/* BSP swap */
static void swap_dir(struct uwm_workspace *ws, int dir)
{
	bsp_swap_direction(ws, ws->focused, dir);
	bsp_arrange_current_workspace();
}

void swapleft(const union arg *arg)  { (void)arg; swap_dir(current_ws(), 0); }
void swapright(const union arg *arg) { (void)arg; swap_dir(current_ws(), 1); }
void swapup(const union arg *arg)    { (void)arg; swap_dir(current_ws(), 2); }
void swapdown(const union arg *arg)  { (void)arg; swap_dir(current_ws(), 3); }

/* resize — tiled (BSP ratio) or floating */
static void resize_float(struct uwm_toplevel *focused, int dx, int dy, int dw, int dh)
{
	if (!focused || !focused->floating)
		return;
	focused->float_x += dx;
	focused->float_y += dy;
	focused->float_width += dw;
	focused->float_height += dh;
	if (focused->float_width < floating_min_width)
		focused->float_width = floating_min_width;
	if (focused->float_height < floating_min_height)
		focused->float_height = floating_min_height;
	wlr_scene_node_set_position(&focused->scene_tree->node,
		focused->float_x, focused->float_y);
	toplevel_set_size(focused, focused->float_width, focused->float_height);
}

static void resize_tiled(float delta)
{
	struct uwm_workspace *ws = current_ws();
	bsp_resize(ws, ws->focused, delta);
	bsp_arrange_current_workspace();
}

void resizeleft(const union arg *arg)
{
	(void)arg;
	struct uwm_toplevel *focused = current_ws()->focused;
	if (focused && focused->floating)
		resize_float(focused, -20, 0, 20, 0);
	else
		resize_tiled(-resizefactor);
}

void resizeright(const union arg *arg)
{
	(void)arg;
	struct uwm_toplevel *focused = current_ws()->focused;
	if (focused && focused->floating)
		resize_float(focused, 0, 0, 20, 0);
	else
		resize_tiled(resizefactor);
}

void resizeup(const union arg *arg)
{
	(void)arg;
	struct uwm_toplevel *focused = current_ws()->focused;
	if (focused && focused->floating)
		resize_float(focused, 0, -20, 0, 20);
	else
		resize_tiled(resizefactor);
}

void resizedown(const union arg *arg)
{
	(void)arg;
	struct uwm_toplevel *focused = current_ws()->focused;
	if (focused && focused->floating)
		resize_float(focused, 0, 0, 0, 20);
	else
		resize_tiled(-resizefactor);
}

/* floating shrink (Shift+Alt+arrow) */
void resizeshleft(const union arg *arg)
{
	(void)arg;
	resize_float(current_ws()->focused, 20, 0, -20, 0);
}

void resizeshright(const union arg *arg)
{
	(void)arg;
	resize_float(current_ws()->focused, 0, 0, -20, 0);
}

void resizeshup(const union arg *arg)
{
	(void)arg;
	resize_float(current_ws()->focused, 0, 20, 0, -20);
}

void resizeshdown(const union arg *arg)
{
	(void)arg;
	resize_float(current_ws()->focused, 0, 0, 0, -20);
}

/* workspace switching */
void workspace(const union arg *arg)
{
	uint32_t ws = (uint32_t)arg->i;
	if (ws < UWM_WORKSPACE_COUNT)
		workspace_switch(uwm_server, ws);
}

void movetows(const union arg *arg)
{
	struct uwm_toplevel *focused = current_ws()->focused;
	if (!focused)
		return;
	uint32_t ws = (uint32_t)arg->i;
	if (ws < UWM_WORKSPACE_COUNT) {
		workspace_move_toplevel(focused, ws);
		workspace_switch(uwm_server, ws);
		focus_toplevel(focused);
	}
}

void workspaceinc(const union arg *arg)
{
	(void)arg;
	workspace_next(uwm_server);
}

void workspacedec(const union arg *arg)
{
	(void)arg;
	workspace_prev(uwm_server);
}

void workspaceprev(const union arg *arg)
{
	(void)arg;
	if (uwm_server->workspaces.last != uwm_server->workspaces.current)
		workspace_switch(uwm_server, uwm_server->workspaces.last);
}

void workspaceinc_active(const union arg *arg)
{
	(void)arg;
	workspace_next_active(uwm_server);
}

void workspacedec_active(const union arg *arg)
{
	(void)arg;
	workspace_prev_active(uwm_server);
}

/* layout toggles */
void togglefloating(const union arg *arg)
{
	(void)arg;
	toggle_floating(current_ws()->focused);
}

void togglefullscreen(const union arg *arg)
{
	(void)arg;
	toggle_fullscreen(current_ws()->focused);
}

void togglemonocle(const union arg *arg)
{
	(void)arg;
	toggle_monocle(current_ws());
}

void setbsp(const union arg *arg)
{
	(void)arg;
	set_bsp_mode(current_ws());
}

void cyclefocus(const union arg *arg)
{
	(void)arg;
	workspace_cycle_next(uwm_server);
}

void rotatesplit(const union arg *arg)
{
	(void)arg;
	bsp_rotate_focused_split(current_ws());
	bsp_arrange_current_workspace();
}


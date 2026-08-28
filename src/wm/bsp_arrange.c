#include <stdlib.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "bsp.h"
#include "workspace.h"
#include "window.h"
#include "server.h"
#include "output.h"
#include "layout.h"

static void bsp_node_apply_geometry(struct uwm_bsp_node *node)
{
	if (node->x != node->toplevel->scene_tree->node.x
			|| node->y != node->toplevel->scene_tree->node.y)
		wlr_scene_node_set_position(
			&node->toplevel->scene_tree->node, node->x, node->y);
	struct wlr_box geo = toplevel_geometry(node->toplevel);
	if (node->width != geo.width || node->height != geo.height
#if WLR_HAS_XWAYLAND
			|| (node->toplevel->type == UWM_TOPLEVEL_XWAYLAND && node->toplevel->xwayland_surface
				&& (node->toplevel->xwayland_surface->x != node->x || node->toplevel->xwayland_surface->y != node->y))
#endif
	) {
#if WLR_HAS_XWAYLAND
		if (node->toplevel->type == UWM_TOPLEVEL_XWAYLAND && node->toplevel->xwayland_surface) {
			wlr_xwayland_surface_configure(node->toplevel->xwayland_surface,
				node->x, node->y, node->width, node->height);
		} else
#endif
		{
			toplevel_set_size(node->toplevel, node->width, node->height);
		}
		if (node->toplevel->type == UWM_TOPLEVEL_XDG && node->toplevel->xdg_toplevel)
			wlr_xdg_toplevel_set_tiled(node->toplevel->xdg_toplevel,
				WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
	}
}

static void bsp_arrange_node_full(
	struct uwm_bsp_node *node, int x, int y, int width, int height)
{
	if (width <= 0 || height <= 0)
		return;

	node->x = x;
	node->y = y;
	node->width = width;
	node->height = height;

	if (node->first == NULL) {
		if (node->toplevel && !node->toplevel->floating
			&& !node->toplevel->fullscreen) {
			if (!node->toplevel->workspace->monocle)
				bsp_node_apply_geometry(node);
		}
		return;
	}

	bsp_arrange_node_full(node->first, x, y, width, height);
	bsp_arrange_node_full(node->second, x, y, width, height);
}

static void bsp_arrange_node(
	struct uwm_bsp_node *node, int x, int y, int width, int height, int gap)
{
	if (width <= 0 || height <= 0)
		return;

	node->x = x;
	node->y = y;
	node->width = width;
	node->height = height;

	if (node->first == NULL) {
		if (node->toplevel && !node->toplevel->floating
			&& !node->toplevel->fullscreen) {
			if (!node->toplevel->workspace->monocle)
				bsp_node_apply_geometry(node);
		}
		return;
	}

	switch (node->mode) {
	case UWM_NODE_MONOCLE:
		if (node->active_child) {
			bsp_arrange_node_full(node->active_child, x, y, width, height);
		} else {
			bsp_arrange_node_full(node->first, x, y, width, height);
		}
		update_layout_visibility(node);
		break;
	case UWM_NODE_BSP:
	default:
		if (node->split == UWM_SPLIT_VERTICAL) {
			int first_w = (int)((width - gap) * node->ratio);
			if (first_w < 1) first_w = 1;
			int second_w = width - gap - first_w;
			if (second_w < 1) { second_w = 1; first_w = width - gap - 1; }
			if (first_w + second_w + gap != width) {
				first_w = (width - gap) / 2;
				second_w = (width - gap) - first_w;
				if (first_w < 1) first_w = 1;
				if (second_w < 1) second_w = 1;
			}
			bsp_arrange_node(node->first, x, y, first_w, height, gap);
			bsp_arrange_node(node->second, x + first_w + gap, y, second_w, height, gap);
		} else {
			int first_h = (int)((height - gap) * node->ratio);
			if (first_h < 1) first_h = 1;
			int second_h = height - gap - first_h;
			if (second_h < 1) { second_h = 1; first_h = height - gap - 1; }
			if (first_h + second_h + gap != height) {
				first_h = (height - gap) / 2;
				second_h = (height - gap) - first_h;
				if (first_h < 1) first_h = 1;
				if (second_h < 1) second_h = 1;
			}
			bsp_arrange_node(node->first, x, y, width, first_h, gap);
			bsp_arrange_node(node->second, x, y + first_h + gap, width, second_h, gap);
		}
		break;
	}
}

void bsp_arrange(struct uwm_workspace *workspace, int x, int y, int width, int height, int gap)
{
	if (!workspace->output)
		return;

	int ogap = workspace->output->server->config.outer_gap;
	x += ogap;
	y += ogap;
	width -= 2*ogap;
	height -= 2*ogap;

	if (width < 1) width = 1;
	if (height < 1) height = 1;

	if (workspace->root == NULL) {
		struct uwm_toplevel *tl;
		wl_list_for_each(tl, &workspace->floating_windows, floating_link) {
			wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
			wlr_scene_node_set_position(&tl->scene_tree->node, tl->float_x, tl->float_y);
		}
		workspace_update_borders(workspace);
		return;
	}

	if (workspace->monocle) {
		struct uwm_toplevel *active_tiled = NULL;
		if (workspace->focused && !workspace->focused->floating && !workspace->focused->fullscreen) {
			active_tiled = workspace->focused;
		} else if (workspace->last_focused && !workspace->last_focused->floating && !workspace->last_focused->fullscreen) {
			active_tiled = workspace->last_focused;
		} else {
			struct uwm_toplevel *tl;
			wl_list_for_each(tl, &workspace->toplevels, workspace_link) {
				if (!tl->floating && !tl->fullscreen) {
					active_tiled = tl;
					break;
				}
			}
		}

		struct uwm_toplevel *tl;
		wl_list_for_each(tl, &workspace->toplevels, workspace_link) {
			if (tl->floating || tl->fullscreen)
				continue;
			if (tl == active_tiled) {
				wlr_scene_node_set_position(&tl->scene_tree->node, x, y);
				struct wlr_box g = toplevel_geometry(tl);
				if (g.width != width || g.height != height)
					toplevel_set_size(tl, width, height);
				wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
			} else {
				wlr_scene_node_set_enabled(&tl->scene_tree->node, false);
			}
		}
		bsp_arrange_node(workspace->root, x, y, width, height, gap);
		if (active_tiled) {
			wlr_scene_node_set_position(&active_tiled->scene_tree->node, x, y);
			wlr_scene_node_set_enabled(&active_tiled->scene_tree->node, true);
		}

		/* Floating windows remain enabled, untouched at their floating geometry */
		wl_list_for_each(tl, &workspace->floating_windows, floating_link) {
			wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
			wlr_scene_node_set_position(&tl->scene_tree->node, tl->float_x, tl->float_y);
		}
	} else {
		bsp_arrange_node(workspace->root, x, y, width, height, gap);
		struct uwm_toplevel *tl;
		wl_list_for_each(tl, &workspace->floating_windows, floating_link) {
			wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
			wlr_scene_node_set_position(&tl->scene_tree->node, tl->float_x, tl->float_y);
		}
	}
	workspace_update_borders(workspace);
}

void get_output_size(struct uwm_workspace *ws,
		int *x, int *y, int *w, int *h)
{
	struct uwm_output *output = ws->output;
	if (output) {
		*x = output->lx + output->usable_area.x;
		*y = output->ly + output->usable_area.y;
		*w = output->usable_area.width;
		*h = output->usable_area.height;
	} else {
		struct uwm_server *server = ws->focused
			? ws->focused->server : NULL;
		if (server) {
			struct uwm_output *first = output_first(server);
			if (first) {
				*x = first->lx + first->usable_area.x;
				*y = first->ly + first->usable_area.y;
				*w = first->usable_area.width;
				*h = first->usable_area.height;
				return;
			}
		}
		*x = 0; *y = 0; *w = 0; *h = 0;
	}
}

void bsp_arrange_workspace(struct uwm_workspace *workspace)
{
	int x, y, w, h;
	get_output_size(workspace, &x, &y, &w, &h);
	struct uwm_output *output = workspace->output;
	int gap = output ? output->server->config.inner_gap : 0;
	bsp_arrange(workspace, x, y, w, h, gap);
}

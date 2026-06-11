#include <stdlib.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "floating.h"
#include "bsp.h"
#include "window.h"
#include "workspace.h"
#include "server.h"

void raise_floating(struct uwm_toplevel *window)
{
	if (!window || !window->floating)
		return;
	wl_list_remove(&window->floating_link);
	wl_list_insert(&window->workspace->floating_windows,
		&window->floating_link);
	wlr_scene_node_raise_to_top(&window->scene_tree->node);
}

void toggle_floating(struct uwm_toplevel *window)
{
	if (!window)
		return;
	struct uwm_workspace *ws = window->workspace;
	int out_w, out_h;
	get_output_size(window->server, &out_w, &out_h);

	if (window->fullscreen)
		return;

	if (!window->floating) {
		int float_w = (int)(out_w * 0.75f);
		int float_h = (int)(out_h * 0.75f);
		if (float_w < 200) float_w = 200;
		if (float_h < 150) float_h = 150;
		window->float_width = float_w;
		window->float_height = float_h;
		window->float_x = (out_w - float_w) / 2;
		window->float_y = (out_h - float_h) / 2;

		if (ws->root) {
			struct uwm_bsp_node *leaf = bsp_find_leaf(ws->root, window);
			if (leaf && leaf->parent) {
				struct uwm_bsp_node *parent = leaf->parent;
				window->bsp_saved = true;
				window->bsp_saved_split = parent->split;
				window->bsp_saved_ratio = parent->ratio;
				window->bsp_saved_mode = parent->mode;
				window->bsp_saved_is_second = (parent->second == leaf);
				window->bsp_saved_sibling = (parent->second == leaf)
					? parent->first->toplevel
					: parent->second->toplevel;
			} else {
				window->bsp_saved = false;
				window->bsp_saved_sibling = NULL;
			}
		} else {
			window->bsp_saved = false;
			window->bsp_saved_sibling = NULL;
		}

		bsp_remove(ws, window);
		window->floating = true;

		wl_list_remove(&window->floating_link);
		wl_list_insert(&ws->floating_windows, &window->floating_link);

		wlr_scene_node_reparent(
			&window->scene_tree->node,
			window->server->floating_layer);
		wlr_scene_node_set_position(&window->scene_tree->node,
			window->float_x, window->float_y);
		wlr_xdg_toplevel_set_size(window->xdg_toplevel,
			window->float_width, window->float_height);

		raise_floating(window);
	} else {
		window->floating = false;
		wl_list_remove(&window->floating_link);
		wl_list_init(&window->floating_link);

		wlr_scene_node_reparent(
			&window->scene_tree->node,
			window->server->tiled_layer);

		bsp_restore(ws, window);
	}

	bsp_arrange(ws, out_w, out_h, window->server->config.inner_gap);

	if (ws->focused == window)
		focus_toplevel(window);
}

void toggle_fullscreen(struct uwm_toplevel *window)
{
	if (!window)
		return;
	struct uwm_workspace *ws = window->workspace;
	int out_w, out_h;
	get_output_size(window->server, &out_w, &out_h);

	if (!window->fullscreen) {
		window->saved_floating = window->floating;

		wlr_scene_node_reparent(
			&window->scene_tree->node,
			window->server->floating_layer);

		if (window->floating) {
			window->saved_x = window->float_x;
			window->saved_y = window->float_y;
			window->saved_width = window->float_width;
			window->saved_height = window->float_height;
			wl_list_remove(&window->floating_link);
			wl_list_init(&window->floating_link);
			window->floating = false;
		} else {
			struct uwm_bsp_node *leaf = bsp_find_leaf(ws->root, window);
			if (leaf) {
				window->saved_x = leaf->x;
				window->saved_y = leaf->y;
				window->saved_width = leaf->width;
				window->saved_height = leaf->height;
			}
		}

		ws->fullscreen_window = window;
		window->fullscreen = true;

		wlr_scene_node_set_position(&window->scene_tree->node, 0, 0);
		wlr_xdg_toplevel_set_size(window->xdg_toplevel, out_w, out_h);

		struct uwm_toplevel *tl, *tmp;
		wl_list_for_each_safe(tl, tmp, &ws->toplevels, workspace_link) {
			if (tl != window)
				wlr_scene_node_set_enabled(&tl->scene_tree->node, false);
		}
		wl_list_for_each_safe(tl, tmp, &ws->floating_windows, floating_link) {
			if (tl != window)
				wlr_scene_node_set_enabled(&tl->scene_tree->node, false);
		}
		wlr_scene_node_raise_to_top(&window->scene_tree->node);

	} else {
		window->fullscreen = false;
		ws->fullscreen_window = NULL;

		struct uwm_toplevel *tl, *tmp;
		wl_list_for_each_safe(tl, tmp, &ws->toplevels, workspace_link) {
			wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
		}
		wl_list_for_each_safe(tl, tmp, &ws->floating_windows, floating_link) {
			wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
		}

		if (window->saved_floating) {
			window->floating = true;
			window->float_x = window->saved_x;
			window->float_y = window->saved_y;
			window->float_width = window->saved_width;
			window->float_height = window->saved_height;
			wl_list_remove(&window->floating_link);
			wl_list_insert(&ws->floating_windows, &window->floating_link);
wlr_scene_node_reparent(
			&window->scene_tree->node,
			window->server->floating_layer);
			wlr_scene_node_set_position(&window->scene_tree->node,
				window->float_x, window->float_y);
			wlr_xdg_toplevel_set_size(window->xdg_toplevel,
				window->float_width, window->float_height);
			raise_floating(window);
		} else {
			wlr_scene_node_reparent(
				&window->scene_tree->node,
				window->server->tiled_layer);
		}

		bsp_arrange(ws, out_w, out_h, window->server->config.inner_gap);

		if (ws->focused == window)
			focus_toplevel(window);
	}
}



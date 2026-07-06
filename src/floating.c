#include <stdlib.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "floating.h"
#include "bsp.h"
#include "window.h"
#include "workspace.h"
#include "output.h"
#include "server.h"
#include "config.h"
#include "layout.h"

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
	int out_x, out_y, out_w, out_h;
	get_output_size(ws, &out_x, &out_y, &out_w, &out_h);

	if (window->fullscreen)
		return;

	if (!window->floating) {
		int float_w = (int)(out_w * floating_default_width_ratio);
		int float_h = (int)(out_h * floating_default_height_ratio);
		if (float_w < floating_create_min_width) float_w = floating_create_min_width;
		if (float_h < floating_create_min_height) float_h = floating_create_min_height;
		window->float_width = float_w;
		window->float_height = float_h;
		window->float_x = (out_w - float_w) / 2;
		window->float_y = (out_h - float_h) / 2;

		enum uwm_split saved_sibling_split = UWM_SPLIT_VERTICAL;
		struct uwm_bsp_node *saved_sibling_node = NULL;

		if (ws->root) {
			struct uwm_bsp_node *leaf = bsp_find_leaf(ws->root, window);
			if (leaf && leaf->parent) {
				struct uwm_bsp_node *parent = leaf->parent;
				window->bsp_saved = true;
				window->bsp_saved_split = parent->split;
				window->bsp_saved_ratio = parent->ratio;
				window->bsp_saved_mode = parent->mode;
				window->bsp_saved_is_second = (parent->second == leaf);

				struct uwm_bsp_node *sibling = (parent->second == leaf)
					? parent->first : parent->second;

				if (sibling->toplevel) {
					window->bsp_saved_sibling = sibling->toplevel;
					window->bsp_saved_depth = 0;
				} else {
					saved_sibling_node = sibling;
					saved_sibling_split = sibling->split;

					struct uwm_bsp_node *leaves[UWM_MAX_WINDOWS];
					int count = 0;
					bsp_collect_leaves(sibling, leaves, &count, UWM_MAX_WINDOWS);
					if (count > 0) {
						window->bsp_saved_sibling = leaves[0]->toplevel;
						int depth = 0;
						struct uwm_bsp_node *n = leaves[0];
						while (n && n != sibling) {
							n = n->parent;
							depth++;
						}
						window->bsp_saved_depth = depth;
					} else {
						window->bsp_saved_sibling = NULL;
						window->bsp_saved_depth = 0;
					}
				}
			} else {
				window->bsp_saved = false;
				window->bsp_saved_sibling = NULL;
				window->bsp_saved_depth = 0;
			}
		} else {
			window->bsp_saved = false;
			window->bsp_saved_sibling = NULL;
			window->bsp_saved_depth = 0;
		}

		bsp_remove(ws, window);

		if (saved_sibling_node)
			saved_sibling_node->split = saved_sibling_split;
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

		if (ws->monocle) {
			int count = 0;
			struct uwm_toplevel *tl;
			wl_list_for_each(tl, &ws->toplevels, workspace_link) {
				count++;
			}
			if (count <= 1) {
				ws->monocle = false;
				if (ws->root)
					set_children_visible(ws->root, true);
			}
		}
	} else {
		window->floating = false;
		wl_list_remove(&window->floating_link);
		wl_list_init(&window->workspace_link);
		wl_list_insert(&ws->toplevels, &window->workspace_link);

		wlr_scene_node_reparent(
			&window->scene_tree->node,
			window->server->tiled_layer);

		bsp_restore(ws, window);
	}

	bsp_arrange(ws, out_x, out_y, out_w, out_h, window->server->config.inner_gap);

	if (ws->focused == window)
		focus_toplevel(window);
}

void toggle_fullscreen(struct uwm_toplevel *window)
{
	if (!window)
		return;
	struct uwm_workspace *ws = window->workspace;
	struct uwm_output *output = ws->output;

	if (!window->fullscreen) {
		/* Hide other windows and layers FIRST so there is no visual
		 * flicker while we reparent and resize the fullscreen window. */
		struct uwm_toplevel *tl;
		wl_list_for_each(tl, &ws->toplevels, workspace_link) {
			if (tl != window)
				wlr_scene_node_set_enabled(&tl->scene_tree->node, false);
		}
		wl_list_for_each(tl, &ws->floating_windows, floating_link) {
			if (tl != window)
				wlr_scene_node_set_enabled(&tl->scene_tree->node, false);
		}
		if (output) {
			wlr_scene_node_set_enabled(&output->layer_top->node, false);
			wlr_scene_node_set_enabled(&output->layer_overlay->node, false);
		}

		window->saved_floating = window->floating;

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

		wlr_scene_node_reparent(
			&window->scene_tree->node,
			window->server->floating_layer);

		int fs_w = output ? output->wlr_output->width : 0;
		int fs_h = output ? output->wlr_output->height : 0;
		wlr_scene_node_set_position(&window->scene_tree->node, 0, 0);
		wlr_xdg_toplevel_set_fullscreen(window->xdg_toplevel, true);
		wlr_xdg_toplevel_set_size(window->xdg_toplevel, fs_w, fs_h);

		wlr_scene_node_raise_to_top(&window->scene_tree->node);

	} else {
		window->fullscreen = false;
		ws->fullscreen_window = NULL;
		wlr_xdg_toplevel_set_fullscreen(window->xdg_toplevel, false);

		/* Restore geometry BEFORE showing other windows so the
		 * scene doesn't render the fullscreen window at full size
		 * overlapping other windows underneath (causes flicker with
		 * transparent windows). */
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
		} else {
			wlr_scene_node_reparent(
				&window->scene_tree->node,
				window->server->tiled_layer);
		}

		struct uwm_toplevel *tl;
		wl_list_for_each(tl, &ws->toplevels, workspace_link) {
			wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
		}
		wl_list_for_each(tl, &ws->floating_windows, floating_link) {
			wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
		}
		if (output) {
			wlr_scene_node_set_enabled(&output->layer_top->node, true);
			wlr_scene_node_set_enabled(&output->layer_overlay->node, true);
		}

		if (window->saved_floating)
			raise_floating(window);

		int out_x, out_y, out_w, out_h;
		get_output_size(ws, &out_x, &out_y, &out_w, &out_h);
		bsp_arrange(ws, out_x, out_y, out_w, out_h, window->server->config.inner_gap);

		if (ws->focused == window)
			focus_toplevel(window);
	}
}

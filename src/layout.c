#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_scene.h>
#include "layout.h"
#include "bsp.h"
#include "floating.h"
#include "window.h"
#include "workspace.h"
#include "server.h"

void update_layout_visibility(struct uwm_bsp_node *node)
{
	if (!node || !node->first)
		return;

	struct uwm_bsp_node *active = node->active_child;

	struct uwm_bsp_node *leaves[UWM_MAX_WINDOWS];
	int count = 0;
	bsp_collect_leaves(node, leaves, &count, UWM_MAX_WINDOWS);

	for (int i = 0; i < count; i++) {
		struct uwm_bsp_node *leaf = leaves[i];
		if (!leaf->toplevel)
			continue;
		if (leaf->toplevel->fullscreen)
			continue;
		bool visible = (leaf == active);
		wlr_scene_node_set_enabled(
			&leaf->toplevel->scene_tree->node, visible);
	}
}

void set_children_visible(struct uwm_bsp_node *node, bool visible)
{
	if (!node)
		return;
	if (node->first == NULL) {
		if (node->toplevel)
			wlr_scene_node_set_enabled(
				&node->toplevel->scene_tree->node, visible);
		return;
	}
	set_children_visible(node->first, visible);
	set_children_visible(node->second, visible);
}

void toggle_tabbed(struct uwm_workspace *workspace)
{
	if (!workspace || !workspace->focused)
		return;

	struct uwm_bsp_node *leaf = bsp_find_leaf(
		workspace->root, workspace->focused);
	if (!leaf)
		return;

	struct uwm_bsp_node *container = bsp_find_tabbed_parent(leaf);
	if (!container)
		container = leaf->parent;
	if (!container)
		return;

	if (container->mode == UWM_NODE_TABBED) {
		container->mode = UWM_NODE_BSP;
		container->active_child = NULL;
		set_children_visible(container, true);
	} else {
		container->mode = UWM_NODE_TABBED;
		container->active_child = leaf;

		struct uwm_toplevel *focused = workspace->focused;
		bsp_arrange_workspace(workspace);
		update_layout_visibility(container);

		if (focused)
			focus_toplevel(focused);
		return;
	}

	bsp_arrange_workspace(workspace);
}

void toggle_monocle(struct uwm_workspace *workspace)
{
	if (!workspace || !workspace->focused)
		return;

	if (workspace->monocle) {
		workspace->monocle = false;

		if (workspace->root)
			set_children_visible(workspace->root, true);
		bsp_arrange_workspace(workspace);
	} else {
		workspace->monocle = true;
		if (!workspace->root)
			return;

		struct uwm_toplevel *focused = workspace->focused;
		bsp_arrange_workspace(workspace);

		if (focused)
			focus_toplevel(focused);
	}
}

void set_bsp_mode(struct uwm_workspace *workspace)
{
	if (!workspace)
		return;

	struct uwm_toplevel *focused = workspace->focused;
	if (!focused)
		return;

	if (focused->fullscreen)
		toggle_fullscreen(focused);

	if (focused->floating)
		toggle_floating(focused);

	if (workspace->monocle)
		toggle_monocle(workspace);
}

void cycle_layout_child(struct uwm_workspace *workspace)
{
	if (!workspace || !workspace->focused)
		return;

	struct uwm_bsp_node *leaf = bsp_find_leaf(
		workspace->root, workspace->focused);
	if (!leaf)
		return;

	struct uwm_bsp_node *container = bsp_find_tabbed_parent(leaf);
	if (!container || container->first == NULL)
		return;

	struct uwm_bsp_node *leaves[UWM_MAX_WINDOWS];
	int count = 0;
	bsp_collect_leaves(container, leaves, &count, UWM_MAX_WINDOWS);
	if (count < 2)
		return;

	int current = -1;
	for (int i = 0; i < count; i++) {
		if (leaves[i] == container->active_child) {
			current = i;
			break;
		}
	}

	int next = (current + 1) % count;
	container->active_child = leaves[next];

	update_layout_visibility(container);

	if (leaves[next]->toplevel)
		focus_toplevel(leaves[next]->toplevel);
}

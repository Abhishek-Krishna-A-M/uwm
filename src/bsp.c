#include <stdlib.h>
#include <math.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "bsp.h"
#include "workspace.h"
#include "window.h"
#include "server.h"
#include "output.h"
#include "layout.h"

void bsp_pool_init(struct uwm_bsp_pool *pool)
{
	pool->freelist = NULL;
	pool->count = 0;
	for (int i = 0; i < BSP_POOL_SIZE; i++) {
		pool->nodes[i].first = pool->freelist;
		pool->freelist = &pool->nodes[i];
	}
}

struct uwm_bsp_node *bsp_node_alloc(struct uwm_bsp_pool *pool)
{
	if (!pool->freelist)
		return NULL;
	struct uwm_bsp_node *node = pool->freelist;
	pool->freelist = node->first;
	node->first = NULL;
	node->second = NULL;
	node->parent = NULL;
	node->toplevel = NULL;
	node->ratio = 0.5f;
	node->mode = UWM_NODE_BSP;
	node->active_child = NULL;
	node->x = node->y = node->width = node->height = 0;
	return node;
}

void bsp_node_free(struct uwm_bsp_pool *pool, struct uwm_bsp_node *node)
{
	if (!node)
		return;
	node->first = pool->freelist;
	pool->freelist = node;
}

static struct uwm_bsp_node *bsp_node_create(struct uwm_toplevel *toplevel)
{
	struct uwm_bsp_node *node = bsp_node_alloc(&toplevel->server->bsp_pool);
	if (!node)
		return NULL;
	node->toplevel = toplevel;
	return node;
}

static struct uwm_bsp_node *bsp_internal_create(
	enum uwm_split split,
	struct uwm_bsp_node *first,
	struct uwm_bsp_node *second,
	struct uwm_bsp_pool *pool)
{
	struct uwm_bsp_node *node = bsp_node_alloc(pool);
	if (!node)
		return NULL;
	node->split = split;
	node->first = first;
	node->second = second;
	return node;
}

struct uwm_bsp_node *bsp_find_leaf(
	struct uwm_bsp_node *node, struct uwm_toplevel *toplevel)
{
	if (node == NULL)
		return NULL;
	if (node->toplevel == toplevel)
		return node;
	if (node->first) {
		struct uwm_bsp_node *found = bsp_find_leaf(node->first, toplevel);
		if (found)
			return found;
	}
	if (node->second) {
		struct uwm_bsp_node *found = bsp_find_leaf(node->second, toplevel);
		if (found)
			return found;
	}
	return NULL;
}

static struct uwm_bsp_node *bsp_find_focused_leaf(struct uwm_workspace *ws)
{
	if (ws->root == NULL || ws->focused == NULL)
		return NULL;
	return bsp_find_leaf(ws->root, ws->focused);
}

static struct uwm_bsp_node *bsp_first_leaf(struct uwm_bsp_node *node)
{
	if (node == NULL)
		return NULL;
	while (node->first)
		node = node->first;
	return node;
}

static enum uwm_split bsp_choose_split(struct uwm_bsp_node *leaf)
{
	if (leaf->width >= leaf->height)
		return UWM_SPLIT_VERTICAL;
	return UWM_SPLIT_HORIZONTAL;
}

static void bsp_node_apply_geometry(struct uwm_bsp_node *node)
{
	if (node->x != node->toplevel->scene_tree->node.x
			|| node->y != node->toplevel->scene_tree->node.y)
		wlr_scene_node_set_position(
			&node->toplevel->scene_tree->node, node->x, node->y);
	if (node->width != node->toplevel->xdg_toplevel->base->current.geometry.width
			|| node->height != node->toplevel->xdg_toplevel->base->current.geometry.height) {
		wlr_xdg_toplevel_set_size(
			node->toplevel->xdg_toplevel, node->width, node->height);
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
			bsp_node_apply_geometry(node);
		}
		return;
	}

	switch (node->mode) {
	case UWM_NODE_TABBED: {
		bsp_arrange_node_full(node->first, x, y, width, height);
		bsp_arrange_node_full(node->second, x, y, width, height);
		update_layout_visibility(node);
		break;
	}
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
	if (workspace->root == NULL)
		return;

	/* Skip arrangement for hidden workspaces — they'll be arranged
	 * when shown via output_set_workspace() / bsp_arrange_workspace(). */
	if (!workspace->output)
		return;

	if (width < 1) width = 1;
	if (height < 1) height = 1;

	if (workspace->monocle && workspace->focused) {
		struct uwm_toplevel *tl;
		wl_list_for_each(tl, &workspace->toplevels, workspace_link) {
			if (tl != workspace->focused && !tl->floating && !tl->fullscreen)
				wlr_scene_node_set_enabled(&tl->scene_tree->node, false);
		}
		bsp_arrange_node(workspace->root, x, y, width, height, gap);
		wlr_scene_node_set_position(
			&workspace->focused->scene_tree->node, x, y);
		wlr_xdg_toplevel_set_size(
			workspace->focused->xdg_toplevel, width, height);
		wlr_scene_node_set_enabled(
			&workspace->focused->scene_tree->node, true);
	} else {
		bsp_arrange_node(workspace->root, x, y, width, height, gap);
	}
}

void bsp_destroy(struct uwm_bsp_node *node, struct uwm_bsp_pool *pool)
{
	if (node == NULL)
		return;
	if (node->first)
		bsp_destroy(node->first, pool);
	if (node->second)
		bsp_destroy(node->second, pool);
	bsp_node_free(pool, node);
}

struct uwm_bsp_node *bsp_insert(
	struct uwm_workspace *workspace,
	struct uwm_toplevel *toplevel)
{
	if (workspace->root == NULL) {
		workspace->root = bsp_node_create(toplevel);
		if (!workspace->root)
			return NULL;
		workspace->tree_gen++;
		return workspace->root;
	}

	struct uwm_bsp_node *focused_leaf = bsp_find_focused_leaf(workspace);
	if (focused_leaf == NULL)
		focused_leaf = bsp_first_leaf(workspace->root);

	enum uwm_split split = bsp_choose_split(focused_leaf);

	struct uwm_bsp_node *new_leaf = bsp_node_create(toplevel);
	if (!new_leaf)
		return NULL;

	enum uwm_node_mode node_mode = UWM_NODE_BSP;
	struct uwm_bsp_node *tabbed_parent = bsp_find_tabbed_parent(focused_leaf);
	if (tabbed_parent)
		node_mode = tabbed_parent->mode;

	struct uwm_bsp_node *internal = bsp_internal_create(
		split, focused_leaf, new_leaf, &toplevel->server->bsp_pool);
	if (!internal) {
		bsp_node_free(&toplevel->server->bsp_pool, new_leaf);
		return NULL;
	}
	internal->mode = node_mode;
	if (node_mode != UWM_NODE_BSP && tabbed_parent)
		internal->active_child = focused_leaf;

	internal->parent = focused_leaf->parent;

	focused_leaf->parent = internal;
	new_leaf->parent = internal;

	if (internal->parent == NULL) {
		workspace->root = internal;
	} else if (internal->parent->first == focused_leaf) {
		internal->parent->first = internal;
	} else {
		internal->parent->second = internal;
	}

	workspace->tree_gen++;
	return new_leaf;
}

static void bsp_fix_active_child(struct uwm_bsp_node *node,
	struct uwm_bsp_node *removed_leaf,
	struct uwm_bsp_node *replacement)
{
	if (!node)
		return;
	if (node->mode != UWM_NODE_TABBED
		&& node->mode != UWM_NODE_MONOCLE)
		return;
	if (node->active_child == removed_leaf) {
		if (replacement)
			node->active_child = replacement;
		else
			node->active_child = NULL;
	}
}

void bsp_restore(struct uwm_workspace *workspace, struct uwm_toplevel *toplevel)
{
	if (!toplevel->bsp_saved || !toplevel->bsp_saved_sibling) {
		bsp_insert(workspace, toplevel);
		return;
	}

	struct uwm_bsp_node *sibling = bsp_find_leaf(
		workspace->root, toplevel->bsp_saved_sibling);
	if (!sibling) {
		bsp_insert(workspace, toplevel);
		return;
	}

	struct uwm_bsp_node *new_leaf = bsp_node_create(toplevel);
	if (!new_leaf)
		return;

	struct uwm_bsp_node *sib_parent = sibling->parent;
	bool sib_is_first = (sib_parent && sib_parent->first == sibling);

	struct uwm_bsp_pool *pool = &toplevel->server->bsp_pool;
	struct uwm_bsp_node *internal;
	if (toplevel->bsp_saved_is_second) {
		internal = bsp_internal_create(
			toplevel->bsp_saved_split, sibling, new_leaf, pool);
	} else {
		internal = bsp_internal_create(
			toplevel->bsp_saved_split, new_leaf, sibling, pool);
	}
	if (!internal) {
		bsp_node_free(pool, new_leaf);
		return;
	}
	internal->ratio = toplevel->bsp_saved_ratio;
	internal->mode = toplevel->bsp_saved_mode;
	if (toplevel->bsp_saved_mode != UWM_NODE_BSP)
		internal->active_child = sibling;

	if (sib_parent == NULL) {
		workspace->root = NULL;
	} else if (sib_is_first) {
		sib_parent->first = NULL;
	} else {
		sib_parent->second = NULL;
	}

	internal->parent = sib_parent;
	new_leaf->parent = internal;
	sibling->parent = internal;

	if (sib_parent == NULL) {
		workspace->root = internal;
	} else if (sib_is_first) {
		sib_parent->first = internal;
	} else {
		sib_parent->second = internal;
	}

	workspace->tree_gen++;
}

void bsp_remove(struct uwm_workspace *workspace, struct uwm_toplevel *toplevel)
{
	if (workspace->root == NULL)
		return;

	struct uwm_bsp_node *leaf = bsp_find_leaf(workspace->root, toplevel);
	if (leaf == NULL)
		return;

	workspace->tree_gen++;

	struct uwm_bsp_node *parent = leaf->parent;
	struct uwm_bsp_pool *pool = &toplevel->server->bsp_pool;

	if (parent == NULL) {
		workspace->root = NULL;
		bsp_node_free(pool, leaf);
		return;
	}

	struct uwm_bsp_node *grandparent = parent->parent;
	struct uwm_bsp_node *sibling =
		(parent->first == leaf) ? parent->second : parent->first;

	bsp_fix_active_child(parent, leaf, sibling);

	struct uwm_bsp_node *cur = grandparent;
	while (cur) {
		bsp_fix_active_child(cur, leaf, sibling);
		cur = cur->parent;
	}

	sibling->parent = grandparent;

	if (grandparent == NULL) {
		workspace->root = sibling;
	} else if (grandparent->first == parent) {
		grandparent->first = sibling;
	} else {
		grandparent->second = sibling;
	}

	bsp_node_free(pool, parent);
	bsp_node_free(pool, leaf);
}

void bsp_collect_leaves(
	struct uwm_bsp_node *node,
	struct uwm_bsp_node **leaves, int *count, int capacity)
{
	if (node == NULL || *count >= capacity)
		return;
	if (node->first == NULL) {
		leaves[*count] = node;
		(*count)++;
		return;
	}
	bsp_collect_leaves(node->first, leaves, count, capacity);
	bsp_collect_leaves(node->second, leaves, count, capacity);
}

struct uwm_bsp_node *bsp_find_tabbed_parent(
	struct uwm_bsp_node *leaf)
{
	if (leaf == NULL) return NULL;
	struct uwm_bsp_node *node = leaf->parent;
	while (node) {
		if (node->mode == UWM_NODE_TABBED
			|| node->mode == UWM_NODE_MONOCLE) {
			return node;
		}
		node = node->parent;
	}
	return NULL;
}

void get_output_size(struct uwm_workspace *ws,
		int *x, int *y, int *w, int *h)
{
	struct uwm_output *output = ws->output;
	if (output) {
		*x = output->usable_area.x;
		*y = output->usable_area.y;
		*w = output->usable_area.width;
		*h = output->usable_area.height;
	} else {
		/* Fallback: use first available output */
		struct uwm_server *server = ws->focused
			? ws->focused->server : NULL;
		if (server) {
			struct uwm_output *first = output_first(server);
			if (first) {
				*x = first->usable_area.x;
				*y = first->usable_area.y;
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

static struct uwm_bsp_node *bsp_nearest_in_direction(
	struct uwm_workspace *workspace,
	struct uwm_bsp_node *focused_leaf,
	int dir)
{
	float fcx = focused_leaf->x + focused_leaf->width / 2.0f;
	float fcy = focused_leaf->y + focused_leaf->height / 2.0f;
	float f_left = (float)focused_leaf->x;
	float f_right = f_left + focused_leaf->width;
	float f_top = (float)focused_leaf->y;
	float f_bottom = f_top + focused_leaf->height;

	int count = 0;
	struct uwm_bsp_node *leaves[UWM_MAX_WINDOWS];
	bsp_collect_leaves(workspace->root, leaves, &count, UWM_MAX_WINDOWS);

	struct uwm_bsp_node *best = NULL;
	float best_dist = INFINITY;

	for (int i = 0; i < count; i++) {
		struct uwm_bsp_node *leaf = leaves[i];
		if (leaf == focused_leaf)
			continue;

		bool in_dir = false;
		switch (dir) {
		case 0:
			in_dir = (leaf->x + leaf->width) <= f_left;
			break;
		case 1:
			in_dir = (float)leaf->x >= f_right;
			break;
		case 2:
			in_dir = (leaf->y + leaf->height) <= f_top;
			break;
		case 3:
			in_dir = (float)leaf->y >= f_bottom;
			break;
		}
		if (!in_dir)
			continue;

		float lcx = leaf->x + leaf->width / 2.0f;
		float lcy = leaf->y + leaf->height / 2.0f;
		float dx = fabsf(fcx - lcx);
		float dy = fabsf(fcy - lcy);
		float dist;

		if (dir == 0 || dir == 1)
			dist = dx + dy * 0.3f;
		else
			dist = dy + dx * 0.3f;

		if (dist < best_dist) {
			best_dist = dist;
			best = leaf;
		}
	}

	return best;
}

struct uwm_toplevel *bsp_focus_left(struct uwm_workspace *workspace)
{
	if (workspace->root == NULL || workspace->focused == NULL)
		return NULL;
	struct uwm_bsp_node *focused_leaf = bsp_find_focused_leaf(workspace);
	if (focused_leaf == NULL)
		return NULL;
	struct uwm_bsp_node *best = bsp_nearest_in_direction(
		workspace, focused_leaf, 0);
	return best ? best->toplevel : NULL;
}

struct uwm_toplevel *bsp_focus_right(struct uwm_workspace *workspace)
{
	if (workspace->root == NULL || workspace->focused == NULL)
		return NULL;
	struct uwm_bsp_node *focused_leaf = bsp_find_focused_leaf(workspace);
	if (focused_leaf == NULL)
		return NULL;
	struct uwm_bsp_node *best = bsp_nearest_in_direction(
		workspace, focused_leaf, 1);
	return best ? best->toplevel : NULL;
}

struct uwm_toplevel *bsp_focus_up(struct uwm_workspace *workspace)
{
	if (workspace->root == NULL || workspace->focused == NULL)
		return NULL;
	struct uwm_bsp_node *focused_leaf = bsp_find_focused_leaf(workspace);
	if (focused_leaf == NULL)
		return NULL;
	struct uwm_bsp_node *best = bsp_nearest_in_direction(
		workspace, focused_leaf, 2);
	return best ? best->toplevel : NULL;
}

struct uwm_toplevel *bsp_focus_down(struct uwm_workspace *workspace)
{
	if (workspace->root == NULL || workspace->focused == NULL)
		return NULL;
	struct uwm_bsp_node *focused_leaf = bsp_find_focused_leaf(workspace);
	if (focused_leaf == NULL)
		return NULL;
	struct uwm_bsp_node *best = bsp_nearest_in_direction(
		workspace, focused_leaf, 3);
	return best ? best->toplevel : NULL;
}

static void bsp_swap(
	struct uwm_workspace *workspace,
	struct uwm_toplevel *a,
	struct uwm_toplevel *b)
{
	struct uwm_bsp_node *leaf_a = bsp_find_leaf(workspace->root, a);
	struct uwm_bsp_node *leaf_b = bsp_find_leaf(workspace->root, b);
	if (leaf_a == NULL || leaf_b == NULL)
		return;

	struct uwm_toplevel *tmp = leaf_a->toplevel;
	leaf_a->toplevel = leaf_b->toplevel;
	leaf_b->toplevel = tmp;
}

void bsp_swap_direction(
	struct uwm_workspace *workspace,
	struct uwm_toplevel *focused,
	int direction)
{
	if (workspace->root == NULL || focused == NULL)
		return;

	struct uwm_bsp_node *focused_leaf = bsp_find_leaf(workspace->root, focused);
	if (focused_leaf == NULL)
		return;

	struct uwm_bsp_node *target = bsp_nearest_in_direction(
		workspace, focused_leaf, direction);
	if (target == NULL)
		return;

	struct uwm_toplevel *tmp = focused_leaf->toplevel;
	focused_leaf->toplevel = target->toplevel;
	target->toplevel = tmp;
}

void bsp_resize(
	struct uwm_workspace *workspace,
	struct uwm_toplevel *focused,
	float delta)
{
	if (workspace->root == NULL || focused == NULL)
		return;

	struct uwm_bsp_node *leaf = bsp_find_leaf(workspace->root, focused);
	if (leaf == NULL || leaf->parent == NULL)
		return;

	struct uwm_bsp_node *parent = leaf->parent;
	parent->ratio += delta;

	if (parent->ratio < 0.10f)
		parent->ratio = 0.10f;
	if (parent->ratio > 0.90f)
		parent->ratio = 0.90f;
}

void bsp_rotate_split(struct uwm_bsp_node *node)
{
	if (node == NULL || node->first == NULL)
		return;

	if (node->split == UWM_SPLIT_VERTICAL)
		node->split = UWM_SPLIT_HORIZONTAL;
	else
		node->split = UWM_SPLIT_VERTICAL;
}

void bsp_rotate_focused_split(struct uwm_workspace *workspace)
{
	if (workspace->root == NULL || workspace->focused == NULL)
		return;

	struct uwm_bsp_node *leaf = bsp_find_leaf(workspace->root, workspace->focused);
	if (leaf == NULL || leaf->parent == NULL)
		return;

	bsp_rotate_split(leaf->parent);
}

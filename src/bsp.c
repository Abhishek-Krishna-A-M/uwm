#include <stdlib.h>
#include <math.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "bsp.h"
#include "workspace.h"
#include "window.h"
#include "server.h"

static struct uwm_bsp_node *bsp_node_create(struct uwm_toplevel *toplevel)
{
	struct uwm_bsp_node *node = calloc(1, sizeof(*node));
	node->toplevel = toplevel;
	node->ratio = 0.5f;
	return node;
}

static struct uwm_bsp_node *bsp_internal_create(
	enum uwm_split split,
	struct uwm_bsp_node *first,
	struct uwm_bsp_node *second)
{
	struct uwm_bsp_node *node = calloc(1, sizeof(*node));
	node->split = split;
	node->ratio = 0.5f;
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
	wlr_scene_node_set_position(
		&node->toplevel->scene_tree->node, node->x, node->y);
	wlr_xdg_toplevel_set_size(
		node->toplevel->xdg_toplevel, node->width, node->height);
}

static void bsp_arrange_node(
	struct uwm_bsp_node *node, int x, int y, int width, int height)
{
	node->x = x;
	node->y = y;
	node->width = width;
	node->height = height;

	if (node->first == NULL) {
		bsp_node_apply_geometry(node);
		return;
	}

	if (node->split == UWM_SPLIT_VERTICAL) {
		int first_w = (int)(width * node->ratio);
		if (first_w < 1) first_w = 1;
		int second_w = width - first_w;
		if (second_w < 1) { second_w = 1; first_w = width - 1; }
		bsp_arrange_node(node->first, x, y, first_w, height);
		bsp_arrange_node(node->second, x + first_w, y, second_w, height);
	} else {
		int first_h = (int)(height * node->ratio);
		if (first_h < 1) first_h = 1;
		int second_h = height - first_h;
		if (second_h < 1) { second_h = 1; first_h = height - 1; }
		bsp_arrange_node(node->first, x, y, width, first_h);
		bsp_arrange_node(node->second, x, y + first_h, width, second_h);
	}
}

void bsp_arrange(struct uwm_workspace *workspace, int width, int height)
{
	if (workspace->root == NULL)
		return;
	if (width < 1) width = 1;
	if (height < 1) height = 1;
	bsp_arrange_node(workspace->root, 0, 0, width, height);
}

void bsp_destroy(struct uwm_bsp_node *node)
{
	if (node == NULL)
		return;
	if (node->first)
		bsp_destroy(node->first);
	if (node->second)
		bsp_destroy(node->second);
	free(node);
}

struct uwm_bsp_node *bsp_insert(
	struct uwm_workspace *workspace,
	struct uwm_toplevel *toplevel)
{
	if (workspace->root == NULL) {
		workspace->root = bsp_node_create(toplevel);
		return workspace->root;
	}

	struct uwm_bsp_node *focused_leaf = bsp_find_focused_leaf(workspace);
	if (focused_leaf == NULL)
		focused_leaf = bsp_first_leaf(workspace->root);

	enum uwm_split split = bsp_choose_split(focused_leaf);

	struct uwm_bsp_node *new_leaf = bsp_node_create(toplevel);

	struct uwm_bsp_node *internal = bsp_internal_create(
		split, focused_leaf, new_leaf);

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

	return new_leaf;
}

void bsp_remove(struct uwm_workspace *workspace, struct uwm_toplevel *toplevel)
{
	if (workspace->root == NULL)
		return;

	struct uwm_bsp_node *leaf = bsp_find_leaf(workspace->root, toplevel);
	if (leaf == NULL)
		return;

	struct uwm_bsp_node *parent = leaf->parent;

	if (parent == NULL) {
		workspace->root = NULL;
		free(leaf);
		return;
	}

	struct uwm_bsp_node *grandparent = parent->parent;
	struct uwm_bsp_node *sibling =
		(parent->first == leaf) ? parent->second : parent->first;

	sibling->parent = grandparent;

	if (grandparent == NULL) {
		workspace->root = sibling;
	} else if (grandparent->first == parent) {
		grandparent->first = sibling;
	} else {
		grandparent->second = sibling;
	}

	free(parent);
	free(leaf);
}

static void bsp_collect_leaves(
	struct uwm_bsp_node *node,
	struct uwm_bsp_node **leaves, int *count)
{
	if (node == NULL)
		return;
	if (node->first == NULL) {
		leaves[*count] = node;
		(*count)++;
		return;
	}
	bsp_collect_leaves(node->first, leaves, count);
	bsp_collect_leaves(node->second, leaves, count);
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
	struct uwm_bsp_node *leaves[256];
	bsp_collect_leaves(workspace->root, leaves, &count);

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

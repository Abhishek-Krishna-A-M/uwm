#include <stdlib.h>
#include <math.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "bsp.h"
#include "workspace.h"
#include "window.h"
#include "server.h"
#include "output.h"
#include "layout.h"

static struct uwm_bsp_node *bsp_find_focused_leaf(struct uwm_workspace *ws)
{
	if (ws->root == NULL || ws->focused == NULL)
		return NULL;
	return bsp_find_leaf(ws->root, ws->focused);
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

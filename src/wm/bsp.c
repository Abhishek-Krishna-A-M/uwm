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
	if (!pool->freelist) {
		wlr_log(WLR_ERROR, "BSP node pool exhausted (%d nodes)",
			BSP_POOL_SIZE);
		return NULL;
	}
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
	if (node == NULL || toplevel == NULL)
		return NULL;
	/* O(1) fast path via cached leaf */
	if (toplevel->bsp_leaf && toplevel->bsp_leaf->toplevel == toplevel) {
		/* verify leaf is still under this root (walk up) */
		struct uwm_bsp_node *cur = toplevel->bsp_leaf;
		while (cur && cur != node) cur = cur->parent;
		if (cur == node) return toplevel->bsp_leaf;
		/* also allow leaf == node itself */
		if (toplevel->bsp_leaf == node) return toplevel->bsp_leaf;
	}
	if (node->toplevel == toplevel)
		return node;
	if (node->first) {
		struct uwm_bsp_node *found = bsp_find_leaf(node->first, toplevel);
		if (found) {
			toplevel->bsp_leaf = found;
			return found;
		}
	}
	if (node->second) {
		struct uwm_bsp_node *found = bsp_find_leaf(node->second, toplevel);
		if (found) {
			toplevel->bsp_leaf = found;
			return found;
		}
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
		toplevel->bsp_leaf = workspace->root;
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

	struct uwm_bsp_node *internal = bsp_internal_create(
		split, focused_leaf, new_leaf, &toplevel->server->bsp_pool);
	if (!internal) {
		bsp_node_free(&toplevel->server->bsp_pool, new_leaf);
		return NULL;
	}

	internal->parent = focused_leaf->parent;

	focused_leaf->parent = internal;
	new_leaf->parent = internal;
	toplevel->bsp_leaf = new_leaf;

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
	if (node->mode != UWM_NODE_MONOCLE)
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

	for (int i = 0; i < toplevel->bsp_saved_depth && sibling->parent; i++)
		sibling = sibling->parent;

	/* If the parent split matches the saved split, the sibling was
	 * re-split in the same orientation after the window was floated.
	 * Go up one more level to avoid nesting inside the new split. */
	if (sibling->parent
			&& sibling->parent->split == toplevel->bsp_saved_split)
		sibling = sibling->parent;

	struct uwm_bsp_node *new_leaf = bsp_node_create(toplevel);
	if (!new_leaf)
		return;
	toplevel->bsp_leaf = new_leaf;

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
	toplevel->bsp_leaf = NULL;

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

	if (sibling->first)
		sibling->split = parent->split;

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
		if (node->mode == UWM_NODE_MONOCLE) {
			return node;
		}
		node = node->parent;
	}
	return NULL;
}

#ifndef BSP_H
#define BSP_H

#include <stdbool.h>
#include <wayland-server-core.h>

struct uwm_workspace;
struct uwm_toplevel;
struct uwm_server;
struct wlr_scene_tree;

#define BSP_POOL_SIZE 512

struct uwm_bsp_pool;

enum uwm_split {
	UWM_SPLIT_VERTICAL,
	UWM_SPLIT_HORIZONTAL,
};

enum uwm_node_mode {
	UWM_NODE_BSP,
	UWM_NODE_MONOCLE,
};

struct uwm_bsp_node {
	struct uwm_bsp_node *parent;
	struct uwm_bsp_node *first;
	struct uwm_bsp_node *second;
	struct uwm_toplevel *toplevel;
	enum uwm_split split;
	float ratio;
	int x, y, width, height;
	enum uwm_node_mode mode;
	struct uwm_bsp_node *active_child;
};

struct uwm_bsp_pool {
	struct uwm_bsp_node nodes[BSP_POOL_SIZE];
	struct uwm_bsp_node *freelist;
	int count;
};

void bsp_pool_init(struct uwm_bsp_pool *pool);
struct uwm_bsp_node *bsp_node_alloc(struct uwm_bsp_pool *pool);
void bsp_node_free(struct uwm_bsp_pool *pool, struct uwm_bsp_node *node);

struct uwm_bsp_node *bsp_insert(
	struct uwm_workspace *workspace,
	struct uwm_toplevel *toplevel);

void bsp_remove(
	struct uwm_workspace *workspace,
	struct uwm_toplevel *toplevel);

void bsp_restore(
	struct uwm_workspace *workspace,
	struct uwm_toplevel *toplevel);

void bsp_arrange(
	struct uwm_workspace *workspace,
	int x, int y, int width, int height, int gap);

void bsp_destroy(
	struct uwm_bsp_node *node,
	struct uwm_bsp_pool *pool);

struct uwm_toplevel *bsp_focus_left(
	struct uwm_workspace *workspace);

struct uwm_toplevel *bsp_focus_right(
	struct uwm_workspace *workspace);

struct uwm_toplevel *bsp_focus_up(
	struct uwm_workspace *workspace);

struct uwm_toplevel *bsp_focus_down(
	struct uwm_workspace *workspace);

void bsp_swap_direction(
	struct uwm_workspace *workspace,
	struct uwm_toplevel *focused,
	int direction);

void bsp_resize(
	struct uwm_workspace *workspace,
	struct uwm_toplevel *focused,
	float delta);

struct uwm_bsp_node *bsp_find_leaf(
	struct uwm_bsp_node *root,
	struct uwm_toplevel *toplevel);

void bsp_rotate_split(
	struct uwm_bsp_node *node);

void bsp_rotate_focused_split(
	struct uwm_workspace *workspace);

void bsp_collect_leaves(
	struct uwm_bsp_node *node,
	struct uwm_bsp_node **leaves, int *count, int capacity);

struct uwm_bsp_node *bsp_find_tabbed_parent(
	struct uwm_bsp_node *leaf);

void bsp_arrange_workspace(
	struct uwm_workspace *workspace);

void get_output_size(
	struct uwm_workspace *workspace,
	int *x, int *y, int *width, int *height);

#endif

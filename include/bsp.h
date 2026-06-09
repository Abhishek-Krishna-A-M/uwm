#ifndef BSP_H
#define BSP_H

#include <stdbool.h>

struct uwm_workspace;
struct uwm_toplevel;
struct uwm_server;

enum uwm_split {
	UWM_SPLIT_VERTICAL,
	UWM_SPLIT_HORIZONTAL,
};

struct uwm_bsp_node {
	struct uwm_bsp_node *parent;
	struct uwm_bsp_node *first;
	struct uwm_bsp_node *second;
	struct uwm_toplevel *toplevel;
	enum uwm_split split;
	float ratio;
	int x, y, width, height;
};

struct uwm_bsp_node *bsp_insert(
	struct uwm_workspace *workspace,
	struct uwm_toplevel *toplevel);

void bsp_remove(
	struct uwm_workspace *workspace,
	struct uwm_toplevel *toplevel);

void bsp_arrange(
	struct uwm_workspace *workspace,
	int width, int height);

void bsp_destroy(
	struct uwm_bsp_node *node);

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

#endif

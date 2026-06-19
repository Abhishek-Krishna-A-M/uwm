#ifndef LAYOUT_H
#define LAYOUT_H

#include <stdbool.h>
#include "bsp.h"

struct uwm_workspace;
struct uwm_toplevel;

void toggle_monocle(struct uwm_workspace *workspace);
void set_bsp_mode(struct uwm_workspace *workspace);

void update_layout_visibility(struct uwm_bsp_node *node);
void set_children_visible(struct uwm_bsp_node *node, bool visible);

#endif

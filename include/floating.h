#ifndef FLOATING_H
#define FLOATING_H

#include <stdint.h>
#include <stdbool.h>

struct uwm_toplevel;
struct uwm_workspace;
struct uwm_server;

void toggle_floating(struct uwm_toplevel *window);
void toggle_fullscreen(struct uwm_toplevel *window);
void raise_floating(struct uwm_toplevel *window);

#endif

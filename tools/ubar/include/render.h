#ifndef UBAR_RENDER_H
#define UBAR_RENDER_H

#include "ubar.h"

void render_frame(State *state);
struct pool_buffer *get_next_buffer(State *state, uint32_t width, uint32_t height);

#endif

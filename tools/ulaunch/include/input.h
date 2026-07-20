#ifndef ULAUNCH_INPUT_H
#define ULAUNCH_INPUT_H

#include <wayland-client.h>

void input_init(struct wl_keyboard *keyboard);
void input_repeat_fire(void);

#endif

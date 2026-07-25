#ifndef GESTURE_H
#define GESTURE_H

#include <stdint.h>
#include <stdbool.h>

struct uwm_server;

struct uwm_gesture {
	bool active;
	uint32_t fingers;
	double dx;
	double dy;
};

void gesture_reset(struct uwm_gesture *g);
bool gesture_swipe_begin(struct uwm_gesture *g, uint32_t fingers);
void gesture_swipe_update(struct uwm_gesture *g, double dx, double dy);
bool gesture_swipe_end(struct uwm_gesture *g, struct uwm_server *server, bool cancelled);

#endif

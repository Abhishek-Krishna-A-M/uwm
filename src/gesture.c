#include <math.h>
#include "gesture.h"
#include "server.h"
#include "workspace.h"
#include "layout.h"
#include "config.h"

#define GESTURE_THRESHOLD 50.0

void gesture_reset(struct uwm_gesture *g)
{
	*g = (struct uwm_gesture){0};
}

bool gesture_swipe_begin(struct uwm_gesture *g, uint32_t fingers)
{
	gesture_reset(g);
	g->fingers = fingers;
	g->active = (fingers == 3);
	return g->active;
}

void gesture_swipe_update(struct uwm_gesture *g, double dx, double dy)
{
	if (!g->active)
		return;

	g->dx += dx;
	g->dy += dy;
}

bool gesture_swipe_end(struct uwm_gesture *g, struct uwm_server *server, bool cancelled)
{
	if (!g->active || cancelled || g->fingers != 3) {
		gesture_reset(g);
		return false;
	}

	if (fabs(g->dx) < GESTURE_THRESHOLD && fabs(g->dy) < GESTURE_THRESHOLD) {
		gesture_reset(g);
		return false;
	}

	struct uwm_workspace *ws = workspace_current(server);

	if (fabs(g->dx) > fabs(g->dy)) {
		if (g->dx > 0)
			workspace_next(server);
		else
			workspace_prev(server);
	} else {
		if (g->dy > 0)
			set_bsp_mode(ws);
		else
			toggle_monocle(ws);
	}

	gesture_reset(g);
	return true;
}

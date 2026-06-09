#ifndef WORKSPACE_H
#define WORKSPACE_H

#include <stdint.h>

#define UWM_WORKSPACE_COUNT 9

struct uwm_workspace{
	uint32_t id;
};

struct uwm_workspace_manager{
	struct uwm_workspace workspaces[UWM_WORKSPACE_COUNT];
	uint32_t current;
};

void workspace_manager_init(struct uwm_workspace_manager *wm);
void workspace_switch(struct uwm_workspace_manager *wm, uint32_t workspace);
#endif

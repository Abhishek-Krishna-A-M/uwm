#include "workspace.h"
#include <stdint.h>

void workspace_manager_init(struct uwm_workspace_manager *wm)
{
	for (uint32_t i=0; i<UWM_WORKSPACE_COUNT; i++) {
		wm->workspaces[i].id=i;
	}
	wm->current=0;
}

void workspace_switch(struct uwm_workspace_manager *wm, uint32_t workspace)
{
	if(workspace>=UWM_WORKSPACE_COUNT){
		return;
	}
	wm->current = workspace;
}

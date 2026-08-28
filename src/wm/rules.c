#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_output_layout.h>
#include "config.h"
#include "window.h"
#include "server.h"
#include "workspace.h"
#include "bsp.h"
#include "floating.h"
#include "rules.h"

static bool glob_match(const char *pattern, const char *string)
{
	if (!pattern || !*pattern)
		return true;
	if (!string)
		return false;
	/* linear O(n+m) glob without exponential recursion */
	const char *p = pattern, *s = string;
	const char *star = NULL;
	const char *ss = s;
	while (*s) {
		if (*p == '?' || *p == *s) {
			p++; s++;
			continue;
		}
		if (*p == '*') {
			star = p++;
			ss = s;
			continue;
		}
		if (star) {
			p = star + 1;
			s = ++ss;
			continue;
		}
		return false;
	}
	while (*p == '*') p++;
	return *p == '\0';
}

static bool rule_matches(struct uwm_rule *rule, const char *app_id, const char *title)
{
	if (rule->app_id && !glob_match(rule->app_id, app_id))
		return false;
	if (rule->title && !glob_match(rule->title, title))
		return false;
	return true;
}

static void apply_rule(struct uwm_config *config, struct uwm_rule *rule,
		struct uwm_toplevel *toplevel)
{
	if (rule->workspace > 0) {
		uint32_t target = (uint32_t)(rule->workspace - 1);
		if (target < UWM_WORKSPACE_COUNT
				&& target != toplevel->workspace->id) {
			struct uwm_workspace *old_ws = toplevel->workspace;
			struct uwm_workspace *new_ws =
				&toplevel->server->workspaces.workspaces[target];

			/* fix BSP desync: remove from old BSP before moving */
			if (!toplevel->floating && !toplevel->fullscreen && old_ws->root) {
				bsp_remove(old_ws, toplevel);
			}
			wl_list_remove(&toplevel->workspace_link);
			wl_list_insert(&new_ws->toplevels, &toplevel->workspace_link);
			toplevel->workspace = new_ws;
			if (!toplevel->floating && !toplevel->fullscreen && new_ws->root) {
				/* will be inserted by caller (map) if needed; if already tiled, re-insert */
				if (old_ws->root)
					bsp_insert(new_ws, toplevel);
			}

			if (toplevel->server->workspaces.current != target) {
				wlr_scene_node_set_enabled(
					&toplevel->scene_tree->node, false);
			}
		}
	}

	if (rule->set_floating && !toplevel->floating) {
		int out_x, out_y, out_w, out_h;
		get_output_size(toplevel->workspace, &out_x, &out_y, &out_w, &out_h);

		toplevel->float_width = (int)(out_w * floating_default_width_ratio);
		toplevel->float_height = (int)(out_h * floating_default_height_ratio);
		if (toplevel->float_width < floating_create_min_width)
			toplevel->float_width = floating_create_min_width;
		if (toplevel->float_height < floating_create_min_height)
			toplevel->float_height = floating_create_min_height;
		toplevel->float_x = (out_w - toplevel->float_width) / 2;
		toplevel->float_y = (out_h - toplevel->float_height) / 2;

		wl_list_remove(&toplevel->workspace_link);
		wl_list_init(&toplevel->workspace_link);

		toplevel->floating = true;
		wl_list_insert(&toplevel->workspace->floating_windows,
			&toplevel->floating_link);
		wlr_scene_node_reparent(&toplevel->scene_tree->node,
			toplevel->server->floating_layer);
		wlr_scene_node_set_position(&toplevel->scene_tree->node,
			toplevel->float_x, toplevel->float_y);
		toplevel_set_size(toplevel, toplevel->float_width, toplevel->float_height);
	}

	if (rule->set_fullscreen && !toplevel->fullscreen) {
		if (!toplevel->floating) {
			bsp_insert(toplevel->workspace, toplevel);
		}
		toggle_fullscreen(toplevel);
	}
}

void rule_apply_all(struct uwm_config *config, struct uwm_toplevel *toplevel)
{
	const char *app_id = toplevel_app_id(toplevel);
	const char *title = toplevel_title(toplevel);

	for (int i = 0; i < config->rule_count; i++) {
		struct uwm_rule *rule = &config->rules[i];
		if (rule_matches(rule, app_id, title)) {
			apply_rule(config, rule, toplevel);
		}
	}
}

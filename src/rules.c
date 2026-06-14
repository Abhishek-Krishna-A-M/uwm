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

static void set_opacity_cb(struct wlr_scene_buffer *buffer,
		int sx, int sy, void *data)
{
	float *opacity = data;
	wlr_scene_buffer_set_opacity(buffer, *opacity);
	(void)sx;
	(void)sy;
}

static bool glob_match(const char *pattern, const char *string)
{
	if (!pattern || !*pattern)
		return true;
	if (!string)
		return false;

	while (*pattern && *string) {
		if (*pattern == '*') {
			pattern++;
			if (!*pattern) return true;
			while (*string) {
				if (glob_match(pattern, string))
					return true;
				string++;
			}
			return false;
		}
		if (*pattern == '?' || *pattern == *string) {
			pattern++;
			string++;
			continue;
		}
		return false;
	}
	while (*pattern == '*') pattern++;
	return *pattern == '\0' && *string == '\0';
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
			struct uwm_workspace *new_ws =
				&toplevel->server->workspaces.workspaces[target];

			wl_list_remove(&toplevel->workspace_link);
			wl_list_insert(&new_ws->toplevels, &toplevel->workspace_link);
			toplevel->workspace = new_ws;

			if (toplevel->server->workspaces.current != target) {
				wlr_scene_node_set_enabled(
					&toplevel->scene_tree->node, false);
			}
		}
	}

	if (rule->set_floating && !toplevel->floating) {
		int out_w, out_h;
		get_output_size(toplevel->server, &out_w, &out_h);

		toplevel->float_width = (int)(out_w * 0.60f);
		toplevel->float_height = (int)(out_h * 0.75f);
		if (toplevel->float_width < 200)
			toplevel->float_width = 200;
		if (toplevel->float_height < 150)
			toplevel->float_height = 150;
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
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
			toplevel->float_width, toplevel->float_height);
	}

	if (rule->set_fullscreen && !toplevel->fullscreen) {
		if (!toplevel->floating) {
			bsp_insert(toplevel->workspace, toplevel);
		}
		toggle_fullscreen(toplevel);
	}

	if (rule->has_opacity) {
		float op = rule->opacity;
		wlr_scene_node_for_each_buffer(
			&toplevel->scene_tree->node,
			set_opacity_cb, &op);
	}
}

void rule_apply_all(struct uwm_config *config, struct uwm_toplevel *toplevel)
{
	const char *app_id = toplevel->xdg_toplevel->app_id;
	const char *title = toplevel->xdg_toplevel->title;

	for (int i = 0; i < config->rule_count; i++) {
		struct uwm_rule *rule = &config->rules[i];
		if (rule_matches(rule, app_id, title)) {
			apply_rule(config, rule, toplevel);
		}
	}
}

#include <stdlib.h>
#include <string.h>
#include <drm_fourcc.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/drm_format_set.h>
#include <pango/pangocairo.h>
#include "layout.h"
#include "bsp.h"
#include "window.h"
#include "workspace.h"
#include "server.h"

void update_layout_visibility(struct uwm_bsp_node *node)
{
	if (!node || !node->first)
		return;

	struct uwm_bsp_node *active = node->active_child;

	struct uwm_bsp_node *leaves[256];
	int count = 0;
	bsp_collect_leaves(node, leaves, &count, 256);

	for (int i = 0; i < count; i++) {
		struct uwm_bsp_node *leaf = leaves[i];
		if (!leaf->toplevel)
			continue;
		if (leaf->toplevel->fullscreen)
			continue;
		bool visible = (leaf == active);
		wlr_scene_node_set_enabled(
			&leaf->toplevel->scene_tree->node, visible);
	}
}

static void set_children_visible(struct uwm_bsp_node *node, bool visible)
{
	if (!node)
		return;
	if (node->first == NULL) {
		if (node->toplevel)
			wlr_scene_node_set_enabled(
				&node->toplevel->scene_tree->node, visible);
		return;
	}
	set_children_visible(node->first, visible);
	set_children_visible(node->second, visible);
}

void destroy_tab_bar(struct uwm_bsp_node *node)
{
	if (!node || !node->deco_tree)
		return;
	wlr_scene_node_destroy(&node->deco_tree->node);
	node->deco_tree = NULL;
	if (node->tab_bar_buf) {
		wlr_buffer_drop(node->tab_bar_buf);
		node->tab_bar_buf = NULL;
	}
}

void update_tab_bar(struct uwm_bsp_node *node)
{
	if (!node || node->mode != UWM_NODE_TABBED)
		return;

	struct uwm_bsp_node *leaves[256];
	int count = 0;
	bsp_collect_leaves(node, leaves, &count, 256);

	if (count == 0)
		return;

	struct uwm_server *server = NULL;
	if (node->toplevel)
		server = node->toplevel->server;
	else if (leaves[0] && leaves[0]->toplevel)
		server = leaves[0]->toplevel->server;
	if (!server)
		return;

	if (node->deco_tree && node->last_tab_count == count
			&& node->last_active_child == node->active_child)
		return;

	node->last_tab_count = count;
	node->last_active_child = node->active_child;

	node->last_tab_count = count;
	node->last_active_child = node->active_child;

	if (node->deco_tree)
		destroy_tab_bar(node);

	node->deco_tree = wlr_scene_tree_create(&server->scene->tree);
	if (!node->deco_tree)
		return;

	int tab_h = 24;
	int tab_w = node->width;
	if (tab_w < 1) tab_w = 1;

	struct uwm_bsp_node *active = node->active_child;

	cairo_surface_t *cr_surf = cairo_image_surface_create(
		CAIRO_FORMAT_ARGB32, tab_w, tab_h);
	cairo_t *cr = cairo_create(cr_surf);

	cairo_set_source_rgba(cr, 0.15, 0.15, 0.15, 1.0);
	cairo_rectangle(cr, 0, 0, tab_w, tab_h);
	cairo_fill(cr);

	int tab_width = tab_w / count;
	if (tab_width < 1) tab_width = 1;

	for (int i = 0; i < count; i++) {
		struct uwm_bsp_node *leaf = leaves[i];
		if (!leaf->toplevel)
			continue;

		int w = (i == count - 1) ? (tab_w - i * tab_width) : tab_width;
		int x = i * tab_width;

		if (leaf == active)
			cairo_set_source_rgba(cr, 0.3, 0.5, 0.8, 1.0);
		else
			cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 1.0);
		cairo_rectangle(cr, x, 1, w, tab_h - 2);
		cairo_fill(cr);

		int text_w = w - 4;
		if (text_w < 4)
			continue;

		const char *title =
			leaf->toplevel->xdg_toplevel->title ?
			leaf->toplevel->xdg_toplevel->title :
			(leaf->toplevel->xdg_toplevel->app_id ?
				leaf->toplevel->xdg_toplevel->app_id : "untitled");

		PangoLayout *layout = pango_cairo_create_layout(cr);
		PangoFontDescription *fd = pango_font_description_from_string("sans 14");
		pango_layout_set_font_description(layout, fd);
		pango_font_description_free(fd);
		pango_layout_set_text(layout, title, -1);
		pango_layout_set_width(layout, text_w * PANGO_SCALE);
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

		cairo_set_source_rgba(cr, 1, 1, 1, 1);
		cairo_move_to(cr, x + 2, 2);
		pango_cairo_show_layout(cr, layout);

		g_object_unref(layout);
	}

	cairo_destroy(cr);

	const struct wlr_drm_format *buffer_fmt = NULL;
	const struct wlr_drm_format_set *tex_formats =
		wlr_renderer_get_texture_formats(server->renderer,
			server->allocator->buffer_caps);
	if (tex_formats)
		buffer_fmt = wlr_drm_format_set_get(tex_formats, DRM_FORMAT_ARGB8888);

	if (buffer_fmt) {
		struct wlr_buffer *buf = wlr_allocator_create_buffer(
			server->allocator, tab_w, tab_h, buffer_fmt);
		if (buf) {
			void *data;
			uint32_t fmt;
			size_t stride;
			if (wlr_buffer_begin_data_ptr_access(buf,
				WLR_BUFFER_DATA_PTR_ACCESS_WRITE,
				&data, &fmt, &stride))
			{
				unsigned char *src = cairo_image_surface_get_data(cr_surf);
				int src_stride = cairo_image_surface_get_stride(cr_surf);
				for (int y = 0; y < tab_h; y++)
					memcpy((unsigned char *)data + y * stride,
						src + y * src_stride,
						tab_w * 4);
				wlr_buffer_end_data_ptr_access(buf);
			}

			struct wlr_scene_buffer *sbuf = wlr_scene_buffer_create(
				node->deco_tree, buf);
			if (sbuf)
				wlr_scene_node_set_position(&sbuf->node,
					node->x, node->y);
			node->tab_bar_buf = buf;
		}
	}

	cairo_surface_destroy(cr_surf);
}

void toggle_tabbed(struct uwm_workspace *workspace)
{
	if (!workspace || !workspace->focused)
		return;

	struct uwm_bsp_node *leaf = bsp_find_leaf(
		workspace->root, workspace->focused);
	if (!leaf)
		return;

	struct uwm_bsp_node *container = bsp_find_tabbed_parent(leaf);
	if (!container)
		container = leaf->parent;
	if (!container)
		return;

	if (container->mode == UWM_NODE_TABBED) {
		container->mode = UWM_NODE_BSP;
		container->active_child = NULL;
		destroy_tab_bar(container);
		set_children_visible(container, true);
	} else {
		container->mode = UWM_NODE_TABBED;
		container->active_child = leaf;

		struct uwm_toplevel *focused = workspace->focused;
		bsp_arrange_workspace(workspace);
		update_layout_visibility(container);

		if (focused)
			focus_toplevel(focused);
		return;
	}

	bsp_arrange_workspace(workspace);
}

void toggle_monocle(struct uwm_workspace *workspace)
{
	if (!workspace || !workspace->focused)
		return;

	if (workspace->monocle) {
		workspace->monocle = false;

		if (workspace->root)
			set_children_visible(workspace->root, true);
		bsp_arrange_workspace(workspace);
	} else {
		workspace->monocle = true;
		if (!workspace->root)
			return;

		struct uwm_toplevel *focused = workspace->focused;
		bsp_arrange_workspace(workspace);

		if (focused)
			focus_toplevel(focused);
	}
}

void set_bsp_mode(struct uwm_workspace *workspace)
{
	if (!workspace || !workspace->focused)
		return;

	struct uwm_bsp_node *leaf = bsp_find_leaf(
		workspace->root, workspace->focused);
	if (!leaf)
		return;

	struct uwm_bsp_node *container = bsp_find_tabbed_parent(leaf);
	if (!container)
		container = leaf->parent;
	if (!container || container->mode == UWM_NODE_BSP)
		return;

	container->mode = UWM_NODE_BSP;
	container->active_child = NULL;

	if (container->deco_tree)
		destroy_tab_bar(container);

	set_children_visible(container, true);
	bsp_arrange_workspace(workspace);
}

void cycle_layout_child(struct uwm_workspace *workspace)
{
	if (!workspace || !workspace->focused)
		return;

	struct uwm_bsp_node *leaf = bsp_find_leaf(
		workspace->root, workspace->focused);
	if (!leaf)
		return;

	struct uwm_bsp_node *container = bsp_find_tabbed_parent(leaf);
	if (!container || container->first == NULL)
		return;

	struct uwm_bsp_node *leaves[256];
	int count = 0;
	bsp_collect_leaves(container, leaves, &count, 256);
	if (count < 2)
		return;

	int current = -1;
	for (int i = 0; i < count; i++) {
		if (leaves[i] == container->active_child) {
			current = i;
			break;
		}
	}

	int next = (current + 1) % count;
	container->active_child = leaves[next];

	update_layout_visibility(container);
	update_tab_bar(container);

	if (leaves[next]->toplevel)
		focus_toplevel(leaves[next]->toplevel);
}

#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pango/pango.h>

#if __has_include("../config.h")
#include "../config.h"
#endif
/* fallback if config.h not found or macros not defined — mirrors rofi config.rasi */
#ifndef ULAUNCH_BG
#define ULAUNCH_BG "#000409"
#endif
#ifndef ULAUNCH_FG
#define ULAUNCH_FG "#ffffff"
#endif
#ifndef ULAUNCH_HIGHLIGHT_BG
#define ULAUNCH_HIGHLIGHT_BG "#ffffff"
#endif
#ifndef ULAUNCH_HIGHLIGHT_FG
#define ULAUNCH_HIGHLIGHT_FG "#000409"
#endif
#ifndef ULAUNCH_PROMPT_COLOR
#define ULAUNCH_PROMPT_COLOR "#ffffff"
#endif
#ifndef ULAUNCH_BORDER_COLOR
#define ULAUNCH_BORDER_COLOR "#ffffff"
#endif
#ifndef ULAUNCH_SEPARATOR_COLOR
#define ULAUNCH_SEPARATOR_COLOR "#ffffff"
#endif
#ifndef ULAUNCH_BORDER_WIDTH
#define ULAUNCH_BORDER_WIDTH 2
#endif
#ifndef ULAUNCH_BORDER_RADIUS
#define ULAUNCH_BORDER_RADIUS 0
#endif
#ifndef ULAUNCH_PADDING
#define ULAUNCH_PADDING 12
#endif
#ifndef ULAUNCH_ITEM_PADDING
#define ULAUNCH_ITEM_PADDING 6
#endif
#ifndef ULAUNCH_LINE_SPACING
#define ULAUNCH_LINE_SPACING 4
#endif
#ifndef ULAUNCH_WIDTH_PCT
#define ULAUNCH_WIDTH_PCT 28
#endif
#ifndef ULAUNCH_MAX_ITEMS
#define ULAUNCH_MAX_ITEMS 5
#endif
#ifndef ULAUNCH_FONT
#define ULAUNCH_FONT "monospace 12"
#endif
#ifndef ULAUNCH_PROMPT
#define ULAUNCH_PROMPT "> "
#endif

static uint32_t parse_hex(const char *s) {
	if (*s == '#') s++;
	size_t len = strlen(s);
	unsigned int r, g, b, a = 0xFF;
	if (len == 8) {
		if (sscanf(s, "%02x%02x%02x%02x", &r, &g, &b, &a) != 4) return 0;
		return (a << 24) | (r << 16) | (g << 8) | b;
	}
	if (sscanf(s, "%02x%02x%02x", &r, &g, &b) != 3) return 0;
	return (0xFF << 24) | (r << 16) | (g << 8) | b;
}

int theme_load(Theme *t, const char *path) {
	/* defaults come from config.h which now mirrors ~/dotfiles/config/rofi/config.rasi:
	 * window { width 28%; padding 12px; border 2px; }
	 * listview { lines 5; spacing 4px; }
	 * element { padding 6px; } */
	*t = (Theme){
		.bg = parse_hex(ULAUNCH_BG),
		.fg = parse_hex(ULAUNCH_FG),
		.highlight_bg = parse_hex(ULAUNCH_HIGHLIGHT_BG),
		.highlight_fg = parse_hex(ULAUNCH_HIGHLIGHT_FG),
		.prompt_color = parse_hex(ULAUNCH_PROMPT_COLOR),
		.border_color = parse_hex(ULAUNCH_BORDER_COLOR),
		.separator_color = parse_hex(ULAUNCH_SEPARATOR_COLOR),
		.border_width = ULAUNCH_BORDER_WIDTH,
		.border_radius = ULAUNCH_BORDER_RADIUS,
		.padding = ULAUNCH_PADDING,
		.item_padding = ULAUNCH_ITEM_PADDING,
		.line_spacing = ULAUNCH_LINE_SPACING,
		.width_pct = ULAUNCH_WIDTH_PCT,
		.max_items = ULAUNCH_MAX_ITEMS,
	};
	/* apply separate separator alpha if config defines it and hex had no AA */
#ifdef ULAUNCH_SEPARATOR_ALPHA
	if (((t->separator_color >> 24) & 0xFF) == 0xFF)
		t->separator_color = (ULAUNCH_SEPARATOR_ALPHA << 24) | (t->separator_color & 0x00FFFFFF);
#endif
	/* rofi has no separator; keep ulaunch separator subtle if still opaque */
	if (t->separator_color == 0xFFFFFFFF)
		t->separator_color = (0x14 << 24) | 0xFFFFFF;
	strcpy(t->font, ULAUNCH_FONT);
	strcpy(t->prompt, ULAUNCH_PROMPT);

	FILE *f = fopen(path, "r");
	if (!f) goto done;

	char line[256];
	while (fgets(line, sizeof(line), f)) {
		char *eq = strchr(line, '=');
		if (!eq) continue;
		*eq++ = '\0';
		char *key = line;
		char *val = eq;
		size_t klen = strlen(key);
		while (klen > 0 && (key[klen-1] == ' ' || key[klen-1] == '\t')) key[--klen] = '\0';
		while (*val == ' ' || *val == '\t') val++;
		size_t vlen = strlen(val);
		while (vlen > 0 && (val[vlen-1] == '\n' || val[vlen-1] == '\r' || val[vlen-1] == ' ' || val[vlen-1] == '\t')) val[--vlen] = '\0';

		if (strcmp(key, "bg") == 0) t->bg = parse_hex(val);
		else if (strcmp(key, "fg") == 0) t->fg = parse_hex(val);
		else if (strcmp(key, "highlight-bg") == 0) t->highlight_bg = parse_hex(val);
		else if (strcmp(key, "highlight-fg") == 0) t->highlight_fg = parse_hex(val);
		else if (strcmp(key, "prompt-color") == 0) t->prompt_color = parse_hex(val);
		else if (strcmp(key, "border-color") == 0) t->border_color = parse_hex(val);
		else if (strcmp(key, "separator-color") == 0) t->separator_color = parse_hex(val);
		else if (strcmp(key, "border-width") == 0) t->border_width = atoi(val);
		else if (strcmp(key, "border-radius") == 0) t->border_radius = atoi(val);
		else if (strcmp(key, "padding") == 0) t->padding = atoi(val);
		else if (strcmp(key, "item-padding") == 0) t->item_padding = atoi(val);
		else if (strcmp(key, "line-spacing") == 0 || strcmp(key, "spacing") == 0) t->line_spacing = atoi(val);
		else if (strcmp(key, "width-pct") == 0) t->width_pct = atoi(val);
		else if (strcmp(key, "max-items") == 0) t->max_items = atoi(val);
		else if (strcmp(key, "prompt") == 0) {
			snprintf(t->prompt, THEME_PROMPT_MAX, "%s", val);
		} else if (strcmp(key, "font") == 0) {
			snprintf(t->font, THEME_FONT_MAX, "%s", val);
		}
	}
	fclose(f);

done:
	if (t->font_desc)
		pango_font_description_free(t->font_desc);
	t->font_desc = pango_font_description_from_string(t->font);
	return 0;
}

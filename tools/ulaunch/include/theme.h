#ifndef ULAUNCH_THEME_H
#define ULAUNCH_THEME_H

#include <stdint.h>
#include <pango/pango.h>

#define THEME_FONT_MAX 64
#define THEME_PROMPT_MAX 32

typedef struct {
	char font[THEME_FONT_MAX];
	PangoFontDescription *font_desc;
	uint32_t bg;
	uint32_t fg;
	uint32_t highlight_bg;
	uint32_t highlight_fg;
	uint32_t prompt_color;
	uint32_t border_color;
	int border_width;
	int padding;
	int width_pct;
	int max_items;
	char prompt[THEME_PROMPT_MAX];
} Theme;

int theme_load(Theme *t, const char *path);

#endif

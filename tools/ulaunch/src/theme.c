#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pango/pango.h>

static uint32_t parse_hex(const char *s) {
	if (*s == '#') s++;
	unsigned int r, g, b;
	if (sscanf(s, "%02x%02x%02x", &r, &g, &b) != 3) return 0;
	return (0xFF << 24) | (r << 16) | (g << 8) | b;
}

int theme_load(Theme *t, const char *path) {
	*t = (Theme){
		.bg = parse_hex("#000409"),
		.fg = parse_hex("#ffffff"),
		.highlight_bg = parse_hex("#ffffff"),
		.highlight_fg = parse_hex("#000409"),
		.prompt_color = parse_hex("#ffffff"),
		.border_color = parse_hex("#ffffff"),
		.border_width = 1,
		.padding = 12,
		.width_pct = 28,
		.max_items = 5,
	};
	strcpy(t->font, "monospace 12");
	strcpy(t->prompt, "> ");

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
		else if (strcmp(key, "border-width") == 0) t->border_width = atoi(val);
		else if (strcmp(key, "padding") == 0) t->padding = atoi(val);
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

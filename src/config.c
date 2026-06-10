#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "config.h"

static bool parse_bool(const char *value, bool *out)
{
	if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0
			|| strcmp(value, "yes") == 0) {
		*out = true;
		return true;
	}
	if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0
			|| strcmp(value, "no") == 0) {
		*out = false;
		return true;
	}
	return false;
}

static bool parse_int(const char *value, int *out)
{
	char *end;
	long n = strtol(value, &end, 10);
	if (*end != '\0')
		return false;
	*out = (int)n;
	return true;
}

static void apply_config_line(struct uwm_config *config, const char *key, const char *value)
{
	if (strcmp(key, "focus_follows_pointer") == 0)
		parse_bool(value, &config->focus_follows_pointer);
	else if (strcmp(key, "key_repeat_delay") == 0)
		parse_int(value, &config->key_repeat_delay);
	else if (strcmp(key, "key_repeat_rate") == 0)
		parse_int(value, &config->key_repeat_rate);
	else if (strcmp(key, "tap_to_click") == 0)
		parse_bool(value, &config->tap_to_click);
	else if (strcmp(key, "natural_scroll") == 0)
		parse_bool(value, &config->natural_scroll);
	else if (strcmp(key, "accel_profile") == 0)
		parse_int(value, &config->accel_profile);
}

void config_load(struct uwm_config *config)
{
	*config = (struct uwm_config){
		.focus_follows_pointer = true,
		.key_repeat_delay = 250,
		.key_repeat_rate = 40,
		.tap_to_click = true,
		.natural_scroll = true,
		.accel_profile = 0,
	};

	const char *home = getenv("HOME");
	if (!home)
		return;

	char path[4096];
	int n = snprintf(path, sizeof(path), "%s/.config/uwm/config", home);
	if (n < 0 || (size_t)n >= sizeof(path))
		return;

	FILE *f = fopen(path, "r");
	if (!f)
		return;

	char line[1024];
	while (fgets(line, sizeof(line), f)) {
		char *p = line;
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '#' || *p == '\n' || *p == '\0')
			continue;
		char *eq = strchr(p, '=');
		if (!eq)
			continue;
		*eq = '\0';
		char *key = p;
		char *value = eq + 1;
		while (value && (*value == ' ' || *value == '\t'))
			value++;
		char *nl = strchr(value, '\n');
		if (nl)
			*nl = '\0';
		char *end = value + strlen(value) - 1;
		while (end >= value && (*end == ' ' || *end == '\t'))
			*end-- = '\0';
		apply_config_line(config, key, value);
	}
	fclose(f);
}

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

static bool parse_float(const char *value, float *out)
{
	char *end;
	float f = strtof(value, &end);
	if (*end != '\0')
		return false;
	*out = f;
	return true;
}

static void trim(char *p)
{
	if (!*p) return;
	size_t len = strlen(p);
	char *end = p + len - 1;
	while (end >= p && (*end == ' ' || *end == '\t')) *end-- = '\0';
}

static void parse_rule_value(struct uwm_rule *rule, const char *value)
{
	char buf[1024];
	strncpy(buf, value, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	char *tok = buf;
	while (tok && *tok) {
		char *comma = strchr(tok, ',');
		if (comma) *comma = '\0';

		char *colon = strchr(tok, ':');
		if (colon) {
			*colon = '\0';
			char *k = tok;
			char *v = colon + 1;
			trim(k);
			trim(v);
			if (!*v) v = NULL;

			if (strcmp(k, "app_id") == 0 && v) {
				free(rule->app_id);
				rule->app_id = strdup(v);
			} else if (strcmp(k, "title") == 0 && v) {
				free(rule->title);
				rule->title = strdup(v);
			} else if (strcmp(k, "workspace") == 0 && v) {
				parse_int(v, &rule->workspace);
			} else if (strcmp(k, "floating") == 0 && v) {
				parse_bool(v, &rule->set_floating);
			} else if (strcmp(k, "fullscreen") == 0 && v) {
				parse_bool(v, &rule->set_fullscreen);
			} else if (strcmp(k, "opacity") == 0 && v) {
				if (parse_float(v, &rule->opacity)) {
					rule->has_opacity = true;
				}
			}
		}

		if (comma)
			tok = comma + 1;
		else
			break;
	}
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
	else if (strcmp(key, "inner_gap") == 0)
		parse_int(value, &config->inner_gap);
	else if (strcmp(key, "rule") == 0) {
		if (config->rule_count >= UWM_MAX_RULES)
			return;
		struct uwm_rule *rule = &config->rules[config->rule_count];
		memset(rule, 0, sizeof(*rule));
		parse_rule_value(rule, value);
		config->rule_count++;
	}
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
		.inner_gap = 5,
		.rule_count = 0,
	};

	const char *home = getenv("HOME");
	if (!home) return;

	char path[4096];
	int n = snprintf(path, sizeof(path), "%s/.config/uwm/config", home);
	if (n < 0 || (size_t)n >= sizeof(path)) return;

	FILE *f = fopen(path, "r");
	if (!f) return;

	char line[1024];
	while (fgets(line, sizeof(line), f)) {
		char *p = line;
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '#' || *p == '\n' || *p == '\0') continue;

		char *eq = strchr(p, '=');
		if (!eq) continue;
		*eq = '\0';
		char *key = p;
		char *value = eq + 1;
		while (value && (*value == ' ' || *value == '\t')) value++;
		char *nl = strchr(value, '\n');
		if (nl) *nl = '\0';
		trim(value);

		apply_config_line(config, key, value);
	}
	fclose(f);
}

void config_finish(struct uwm_config *config)
{
	for (int i = 0; i < config->rule_count; i++) {
		free(config->rules[i].app_id);
		free(config->rules[i].title);
	}
	config->rule_count = 0;
}

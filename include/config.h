#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

#define UWM_MAX_RULES 64

struct uwm_rule {
	char *app_id;
	char *title;
	int workspace;
	bool set_floating;
	bool set_fullscreen;
	bool has_opacity;
	float opacity;
};

struct uwm_config {
	bool focus_follows_pointer;
	int key_repeat_delay;
	int key_repeat_rate;
	bool tap_to_click;
	bool natural_scroll;
	int accel_profile;
	int inner_gap;
	int rule_count;
	struct uwm_rule rules[UWM_MAX_RULES];
};

void config_load(struct uwm_config *config);
void config_finish(struct uwm_config *config);

#endif

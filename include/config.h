#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

struct uwm_config {
	bool focus_follows_pointer;
	int key_repeat_delay;
	int key_repeat_rate;
	bool tap_to_click;
	bool natural_scroll;
	int accel_profile;
};

void config_load(struct uwm_config *config);

#endif

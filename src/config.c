#include "config.h"
#include <stddef.h>
#include <wlr/types/wlr_keyboard.h>

/* ========== Include user configuration ==========
 * config.def.h provides defaults, config.h overrides them.
 * Order matters: config.h can #undef and redefine macros. */
#include "../config.def.h"
#include "../config.h"

/* ========== Exported configuration globals ========== */

/* spawnable command argv arrays */
const char *term[] = { TERM };
const char *launcher[] = { LAUNCHER };
const char *run[] = { RUN };
const char *screenshot[] = { SCREENSHOT };
const char *screenshot_full[] = { SCREENSHOT_FULL };
const char *screenshot_clip[] = { SCREENSHOT_CLIP };
const char *filemgr[] = { FILEMGR };
const char *findfile[] = { FINDFILE };
const char *powermenu[] = { POWERMENU };
const char *winswitch[] = { WINSWITCH };
const char *hdmi_script[] = { HDMI_SCRIPT };
const char *volup[] = { VOLUP };
const char *voldown[] = { VOLDOWN };
const char *volmute[] = { VOLMUTE };
const char *brup[] = { BRUP };
const char *brdown[] = { BRDOWN };

/* key binding arrays */
const struct key keys[] = { KEYS };
const size_t keys_len = sizeof(keys) / sizeof(keys[0]);

const struct key keys_unmodified[] = { KEYS_UNMODIFIED };
const size_t keys_unmodified_len = sizeof(keys_unmodified) / sizeof(keys_unmodified[0]);

/* autostart commands */
const char *const autostart[] = { AUTOSTART };

/* scalar settings */
const int borderpx = BORDERPX;
const float resizefactor = RESIZEFACTOR;
const int floating_min_width = FLOATING_MIN_WIDTH;
const int floating_min_height = FLOATING_MIN_HEIGHT;
const int floating_create_min_width = FLOATING_CREATE_MIN_WIDTH;
const int floating_create_min_height = FLOATING_CREATE_MIN_HEIGHT;
const float floating_default_width_ratio = FLOATING_DEFAULT_WIDTH_RATIO;
const float floating_default_height_ratio = FLOATING_DEFAULT_HEIGHT_RATIO;
const float unfocus_dim = UNFOCUS_DIM;

/* compile-time rules */
static const struct uwm_rule builtin_rules[] = { RULES };
static const int builtin_rule_count =
	sizeof(builtin_rules) / sizeof(builtin_rules[0]);

void config_load(struct uwm_config *config)
{
	*config = (struct uwm_config){
		.focus_follows_pointer = FOCUS_FOLLOWS_POINTER,
		.key_repeat_delay = KEY_REPEAT_DELAY,
		.key_repeat_rate = KEY_REPEAT_RATE,
		.tap_to_click = TAP_TO_CLICK,
		.natural_scroll = NATURAL_SCROLL,
		.accel_profile = ACCEL_PROFILE,
		.inner_gap = INNER_GAP,
		.rule_count = 0,
	};

	int n = builtin_rule_count;
	if (n > UWM_MAX_RULES)
		n = UWM_MAX_RULES;
	for (int i = 0; i < n; i++)
		config->rules[i] = builtin_rules[i];
	config->rule_count = n;
}

void config_finish(struct uwm_config *config)
{
	/* compile-time config has no heap-allocated data — nothing to free */
	(void)config;
}

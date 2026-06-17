#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

#define UWM_WORKSPACE_COUNT 9
#define UWM_MAX_RULES 64
#define UWM_MAX_WINDOWS 256
#define LENGTH(X) (sizeof X / sizeof X[0])

union arg {
	const char **argv;
	int i;
	unsigned int ui;
	float f;
	const void *v;
};

struct key {
	uint32_t mod;
	xkb_keysym_t keysym;
	void (*func)(const union arg *);
	const union arg arg;
};

struct uwm_rule {
	const char *app_id;
	const char *title;
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

extern const char *term[];
extern const char *launcher[];
extern const char *const autostart[];

extern const struct key keys[];
extern const size_t keys_len;
extern const struct key keys_unmodified[];
extern const size_t keys_unmodified_len;

extern const int borderpx;
extern const float resizefactor;
extern const int floating_min_width;
extern const int floating_min_height;
extern const int floating_create_min_width;
extern const int floating_create_min_height;
extern const float floating_default_width_ratio;
extern const float floating_default_height_ratio;
extern const float unfocus_dim;

extern const char *run[];
extern const char *screenshot[];
extern const char *screenshot_full[];
extern const char *screenshot_clip[];
extern const char *filemgr[];
extern const char *findfile[];
extern const char *powermenu[];
extern const char *winswitch[];
extern const char *hdmi_script[];
extern const char *volup[];
extern const char *voldown[];
extern const char *volmute[];
extern const char *brup[];
extern const char *brdown[];

/* action functions — defined in input.c, referenced from config.def.h keys[] */
void spawn(const union arg *arg);
void quit(const union arg *arg);
void closewindow(const union arg *arg);
void forceclose(const union arg *arg);
void moveleft(const union arg *arg);
void moveright(const union arg *arg);
void moveup(const union arg *arg);
void movedown(const union arg *arg);
void swapleft(const union arg *arg);
void swapright(const union arg *arg);
void swapup(const union arg *arg);
void swapdown(const union arg *arg);
void resizeleft(const union arg *arg);
void resizeright(const union arg *arg);
void resizeup(const union arg *arg);
void resizedown(const union arg *arg);
void resizeshleft(const union arg *arg);
void resizeshright(const union arg *arg);
void resizeshup(const union arg *arg);
void resizeshdown(const union arg *arg);
void workspace(const union arg *arg);
void movetows(const union arg *arg);
void workspaceinc(const union arg *arg);
void workspacedec(const union arg *arg);
void workspaceprev(const union arg *arg);
void togglefloating(const union arg *arg);
void togglefullscreen(const union arg *arg);
void togglemonocle(const union arg *arg);
void setbsp(const union arg *arg);
void cyclefocus(const union arg *arg);
void rotatesplit(const union arg *arg);

void config_load(struct uwm_config *config);
void config_finish(struct uwm_config *config);

#endif

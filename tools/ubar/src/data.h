#ifndef UBAR_DATA_H
#define UBAR_DATA_H

#include "ubar.h"
#include <pthread.h>

extern int g_vol_pct;
extern bool g_muted;
extern char g_net_name[128];
extern char g_net_speed[128];
extern char g_net_iface[32];
extern pthread_mutex_t g_data_mutex;

void data_init_fast(State *state);
void data_start_monitors(State *state);
void data_update_clock(State *state);
void data_update_cpu(State *state);
void data_update_temp(State *state);
void data_update_memory(State *state);
void data_update_battery(State *state);
void data_update_hdmi(State *state);
void data_update_locks(State *state);
bool data_update_volume(void);
void data_update_network(void);
void data_sync_to_state(State *state);
void data_update_all_timer(State *state);

#endif

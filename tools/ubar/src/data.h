#ifndef UBAR_DATA_H
#define UBAR_DATA_H

#include "ubar.h"
#include <pthread.h>

void data_init_fast(State *state);
void data_start_monitors(State *state);
void data_stop_monitors(State *state);

bool data_update_clock(State *state);
bool data_update_cpu(State *state);
bool data_update_temp(State *state);
bool data_update_memory(State *state);
bool data_update_battery_hardware(State *state);
bool data_update_hdmi_hardware(State *state);
bool data_update_locks_hardware(State *state);
void data_sync_to_state(State *state);
bool data_update_all_timer(State *state);
bool data_update_slow_timer(State *state);

#endif

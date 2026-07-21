#include "ulaunch.h"
#include "mode_dmenu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static void grow_arrays(void) {
	state.cap_entries *= 2;
	state.entries = realloc(state.entries, sizeof(char *) * state.cap_entries);
	state.filtered = realloc(state.filtered, sizeof(int) * state.cap_entries);
	state.scores = realloc(state.scores, sizeof(float) * state.cap_entries);
}

void dmenu_pump(void) {
	char buf[4096];
	int n = read(STDIN_FILENO, buf, sizeof(buf));
	if (n < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) return;
		state.dmenu_stdin_done = true;
		return;
	}
	if (n == 0) {
		if (state.dmenu_buf_len > 0) {
			state.dmenu_buf[state.dmenu_buf_len] = '\0';
			state.dmenu_buf_len = 0;
			if (state.n_entries >= state.cap_entries) grow_arrays();
			state.entries[state.n_entries++] = strdup(state.dmenu_buf);
		}
		state.dmenu_stdin_done = true;
		return;
	}
	for (int i = 0; i < n; i++) {
		if (buf[i] == '\n') {
			if (state.dmenu_buf_len > 0) {
				state.dmenu_buf[state.dmenu_buf_len] = '\0';
				state.dmenu_buf_len = 0;
				if (state.n_entries >= state.cap_entries) grow_arrays();
				state.entries[state.n_entries++] = strdup(state.dmenu_buf);
			}
		} else if (state.dmenu_buf_len < (int)sizeof(state.dmenu_buf) - 1) {
			state.dmenu_buf[state.dmenu_buf_len++] = buf[i];
		}
	}
}

int mode_dmenu(void) {
	int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
	if (flags < 0 || fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0)
		return 1;

	state.cap_entries = ENTRIES_INIT;
	state.entries = malloc(sizeof(char *) * state.cap_entries);
	if (!state.entries) return 1;

	int init_cap = state.cap_entries > MAX_SCORE_RESULTS
		? state.cap_entries : MAX_SCORE_RESULTS;
	state.filtered = malloc(sizeof(int) * init_cap);
	if (!state.filtered) return 1;
	state.scores = malloc(sizeof(float) * init_cap);
	if (!state.scores) return 1;

	state.dmenu_buf_len = 0;
	state.dmenu_stdin_done = false;
	dmenu_pump();
	return 0;
}

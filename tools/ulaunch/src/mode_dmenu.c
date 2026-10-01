#include "ulaunch.h"
#include "mode_dmenu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/* Read size for the stdin entry stream. A bulk producer (rg --files, compgen
 * -c, fd) emits hundreds of KB to a few MB; at 4 KB per read that is hundreds
 * of poll wakeups and hundreds of full re-filters before the launcher settles.
 * 64 KB matches the default pipe capacity, so a burst arrives in a handful of
 * reads. */
#define DMENU_READ_CHUNK 65536

/* Grow the entry array and keep the filter's arrays in step. filter.c owns
 * filtered/scores, so they are resized through filter_reserve() rather than
 * directly. */
static bool grow_arrays(void) {
	size_t new_cap = (size_t)state.cap_entries * 2;
	char **e = realloc(state.entries, sizeof(char *) * new_cap);
	if (!e) return false;
	state.entries = e;

	if (!filter_reserve((int)new_cap)) return false;

	state.cap_entries = (int)new_cap;
	return true;
}

/* Append one input line as an entry. Returns false on OOM. */
static bool push_entry(const char *line) {
	if (state.n_entries >= state.cap_entries && !grow_arrays())
		return false;
	state.entries[state.n_entries] = strdup(line);
	if (!state.entries[state.n_entries])
		return false;
	state.n_entries++;
	/* n_entries can reach cap_entries exactly, so make sure the filter
	 * arrays can hold it before filter_update() indexes them. */
	if (!filter_reserve(state.n_entries))
		return false;
	return true;
}

int dmenu_pump(void) {
	char buf[DMENU_READ_CHUNK];
	int before = state.n_entries;

	int n = read(STDIN_FILENO, buf, sizeof(buf));
	if (n < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
		state.dmenu_stdin_done = true;
		return 0;
	}
	if (n == 0) {
		if (state.dmenu_buf_len > 0) {
			state.dmenu_buf[state.dmenu_buf_len] = '\0';
			state.dmenu_buf_len = 0;
			push_entry(state.dmenu_buf);
		}
		state.dmenu_stdin_done = true;
		return state.n_entries - before;
	}
	for (int i = 0; i < n; i++) {
		if (buf[i] == '\n') {
			if (state.dmenu_buf_len > 0) {
				state.dmenu_buf[state.dmenu_buf_len] = '\0';
				state.dmenu_buf_len = 0;
				push_entry(state.dmenu_buf);
			}
		} else if (state.dmenu_buf_len < (int)sizeof(state.dmenu_buf) - 1) {
			state.dmenu_buf[state.dmenu_buf_len++] = buf[i];
		}
	}
	return state.n_entries - before;
}

int mode_dmenu(void) {
	int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
	if (flags < 0 || fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0)
		return 1;

	state.cap_entries = ENTRIES_INIT;
	state.entries = malloc(sizeof(char *) * state.cap_entries);
	if (!state.entries) return 1;
	if (!filter_reserve(ENTRIES_INIT)) return 1;

	state.dmenu_buf_len = 0;
	state.dmenu_stdin_done = false;
	dmenu_pump();
	return 0;
}

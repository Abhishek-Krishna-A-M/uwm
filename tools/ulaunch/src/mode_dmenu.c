#include "ulaunch.h"
#include "mode_dmenu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int mode_dmenu(void) {
	if (isatty(STDIN_FILENO)) {
		fprintf(stderr, "ulaunch: dmenu mode expects piped input\n");
		fprintf(stderr, "  example: printf 'foo\\nbar\\n' | %s -d\n",
			getenv("_") ? getenv("_") : "ulaunch");
		return 1;
	}

	char *line = NULL;
	size_t len = 0;
	ssize_t nread;

	state.cap_entries = ENTRIES_INIT;
	state.entries = malloc(sizeof(char *) * state.cap_entries);
	if (!state.entries) return 1;

	while ((nread = getline(&line, &len, stdin)) != -1) {
		if (nread > 0 && line[nread - 1] == '\n')
			line[nread - 1] = '\0';

		if (state.n_entries >= state.cap_entries) {
			state.cap_entries *= 2;
			char **tmp = realloc(state.entries, sizeof(char *) * state.cap_entries);
			if (!tmp) { free(line); return 1; }
			state.entries = tmp;
		}

		state.entries[state.n_entries] = strdup(line);
		if (!state.entries[state.n_entries]) { free(line); return 1; }
		state.n_entries++;
	}
	free(line);

	if (state.n_entries == 0) return 1;

	state.filtered = malloc(sizeof(int) * state.n_entries);
	if (!state.filtered) return 1;
	state.scores = malloc(sizeof(float) * state.n_entries);
	if (!state.scores) return 1;

	filter_update();
	return 0;
}

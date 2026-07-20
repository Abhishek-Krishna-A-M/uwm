#include "ulaunch.h"
#include "mode_drun.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>


#define MAX_DIRS 16
#define BUF_LEN 1024

static int parse_desktop_file(const char *path) {
	FILE *f = fopen(path, "r");
	if (!f) return -1;

	char line[BUF_LEN];
	char name[BUF_LEN] = {0};
	char exec_raw[BUF_LEN] = {0};
	bool nodisplay = false;
	bool in_desktop_entry = false;

	while (fgets(line, sizeof(line), f)) {
		size_t len = strlen(line);
		while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
			line[--len] = '\0';

		if (len == 0) continue;

		if (line[0] == '[') {
			in_desktop_entry = (strcmp(line, "[Desktop Entry]") == 0);
			continue;
		}

		if (!in_desktop_entry) continue;

		if (strncmp(line, "NoDisplay=", 10) == 0 && strcmp(line + 10, "true") == 0) {
			nodisplay = true;
		}

		if (strncmp(line, "Name=", 5) == 0) {
			snprintf(name, sizeof(name), "%s", line + 5);
		}

		if (strncmp(line, "Exec=", 5) == 0) {
			snprintf(exec_raw, sizeof(exec_raw), "%s", line + 5);
		}
	}
	fclose(f);

	if (nodisplay || name[0] == '\0' || exec_raw[0] == '\0') return -1;

	/* expand % fields in Exec line */
	char expanded[BUF_LEN] = {0};
	char *src = exec_raw;
	char *dst = expanded;
	while (*src && (size_t)(dst - expanded) < sizeof(expanded) - 1) {
		if (*src == '%' && *(src + 1)) {
			switch (*(src + 1)) {
			case '%': *dst++ = '%'; src += 2; break;
			case 'f': case 'F': case 'u': case 'U':
			case 'i': case 'c': case 'k':
				src += 2; break;
			default:
				*dst++ = *src++;
			}
		} else {
			*dst++ = *src++;
		}
	}
	*dst = '\0';

	/* trim trailing whitespace */
	char *end = expanded + strlen(expanded) - 1;
	while (end >= expanded && (*end == ' ' || *end == '\t')) *end-- = '\0';

	if (state.n_entries >= state.cap_entries) {
		state.cap_entries *= 2;
		state.entries = realloc(state.entries, sizeof(char *) * state.cap_entries);
		state.exec_cmds = realloc(state.exec_cmds, sizeof(char *) * state.cap_entries);
		if (!state.entries || !state.exec_cmds) return -1;
	}

	state.entries[state.n_entries] = strdup(name);
	state.exec_cmds[state.n_entries] = strdup(expanded);
	if (!state.entries[state.n_entries] || !state.exec_cmds[state.n_entries])
		return -1;
	state.n_entries++;
	return 0;
}

static int cmp_drun(const void *a, const void *b) {
	int ia = *(const int *)a;
	int ib = *(const int *)b;
	return strcasecmp(state.entries[ia], state.entries[ib]);
}

static void scan_dir(const char *dir) {
	DIR *d = opendir(dir);
	if (!d) return;

	struct dirent *e;
	while ((e = readdir(d))) {
		size_t len = strlen(e->d_name);
		if (len < 8 || strcmp(e->d_name + len - 8, ".desktop") != 0)
			continue;

		char path[1024];
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		parse_desktop_file(path);
	}
	closedir(d);
}

int mode_drun(void) {
	state.cap_entries = ENTRIES_INIT;
	state.entries = malloc(sizeof(char *) * state.cap_entries);
	state.exec_cmds = malloc(sizeof(char *) * state.cap_entries);
	if (!state.entries || !state.exec_cmds) return 1;

	const char *data_dirs = getenv("XDG_DATA_DIRS");
	if (!data_dirs) data_dirs = "/usr/local/share:/usr/share";

	char dirs_buf[4096];
	strncpy(dirs_buf, data_dirs, sizeof(dirs_buf) - 1);
	dirs_buf[sizeof(dirs_buf) - 1] = '\0';

	char *save;
	char *d = strtok_r(dirs_buf, ":", &save);
	while (d && state.n_entries < 4096) {
		char appdir[1024];
		snprintf(appdir, sizeof(appdir), "%s/applications", d);
		scan_dir(appdir);
		d = strtok_r(NULL, ":", &save);
	}

	/* user local dir */
	const char *home = getenv("HOME");
	if (home) {
		char appdir[1024];
		snprintf(appdir, sizeof(appdir), "%s/.local/share/applications", home);
		scan_dir(appdir);
	}

	if (state.n_entries == 0) return 1;

	/* sort entries alphabetically by name */
	int *order = malloc(sizeof(int) * state.n_entries);
	if (!order) return 1;
	for (int i = 0; i < state.n_entries; i++) order[i] = i;
	qsort(order, state.n_entries, sizeof(int), cmp_drun);

	char **sorted_entries = malloc(sizeof(char *) * state.n_entries);
	char **sorted_execs = malloc(sizeof(char *) * state.n_entries);
	if (!sorted_entries || !sorted_execs) {
		free(order); free(sorted_entries); free(sorted_execs); return 1;
	}
	for (int i = 0; i < state.n_entries; i++) {
		sorted_entries[i] = state.entries[order[i]];
		sorted_execs[i] = state.exec_cmds[order[i]];
	}
	free(state.entries);
	free(state.exec_cmds);
	state.entries = sorted_entries;
	state.exec_cmds = sorted_execs;
	free(order);

	state.filtered = malloc(sizeof(int) * state.n_entries);
	if (!state.filtered) return 1;
	state.scores = malloc(sizeof(float) * state.n_entries);
	if (!state.scores) return 1;
	state.hits = calloc(state.n_entries, sizeof(int));
	if (!state.hits) return 1;

	filter_update();
	return 0;
}

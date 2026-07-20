#include "ulaunch.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>

static float score_match(const char *needle, const char *haystack, int nhits) {
	int nlen = strlen(needle);
	int hlen = strlen(haystack);

	if (nlen == 0) return 1.0;
	if (nlen > hlen) return 0.0;

	float score = 0.0;
	int prev = -2;
	int ni = 0;
	int first = -1;

	for (int hi = 0; hi < hlen && ni < nlen; hi++) {
		if (tolower((unsigned char)needle[ni]) == tolower((unsigned char)haystack[hi])) {
			if (first < 0) first = hi;

			score += 1.0;

			if (hi == prev + 1)
				score += 0.5;

			if (hi > 0) {
				char p = haystack[hi - 1];
				if (p == '-' || p == '_' || p == '.' || p == ' ' || p == '/' || p == '\\')
					score += 0.8;
				else if (isupper((unsigned char)haystack[hi]) && islower((unsigned char)p))
					score += 0.6;
			}

			prev = hi;
			ni++;
		}
	}

	if (ni < nlen) return 0.0;

	if (first == 0) score += 0.5;

	score = score / (nlen + 1);

	if (nhits > 0)
		score *= 1.0 + 0.3 * log10f((float)(nhits + 1));

	return score;
}

static int cmp_score(const void *a, const void *b) {
	int ia = *(const int *)a;
	int ib = *(const int *)b;
	if (state.scores[ia] > state.scores[ib]) return -1;
	if (state.scores[ia] < state.scores[ib]) return 1;
	return 0;
}

void filter_update(void) {
	state.n_filtered = 0;

	if (state.input_len == 0) {
		state.n_filtered = state.n_entries;
		for (int i = 0; i < state.n_entries; i++) {
			state.filtered[i] = i;
			state.scores[i] = 0.0;
		}
		return;
	}

	for (int i = 0; i < state.n_entries && state.n_filtered < MAX_SCORE_RESULTS; i++) {
		float s = score_match(state.input, state.entries[i],
			state.hits ? state.hits[i] : 0);
		if (s > 0.0) {
			state.filtered[state.n_filtered] = i;
			state.scores[state.n_filtered] = s;
			state.n_filtered++;
		}
	}

	if (state.n_filtered > 1) {
		int order[MAX_SCORE_RESULTS];
		for (int i = 0; i < state.n_filtered; i++) order[i] = i;
		qsort(order, state.n_filtered, sizeof(int), cmp_score);
		int old_filtered[MAX_SCORE_RESULTS];
		float old_scores[MAX_SCORE_RESULTS];
		memcpy(old_filtered, state.filtered, state.n_filtered * sizeof(int));
		memcpy(old_scores, state.scores, state.n_filtered * sizeof(float));
		for (int i = 0; i < state.n_filtered; i++) {
			state.filtered[i] = old_filtered[order[i]];
			state.scores[i] = old_scores[order[i]];
		}
	}

	if (state.cursor >= state.n_filtered)
		state.cursor = state.n_filtered > 0 ? state.n_filtered - 1 : 0;
}

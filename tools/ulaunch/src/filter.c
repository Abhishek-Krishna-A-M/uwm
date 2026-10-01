#include "ulaunch.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>

/* Result list capacity. This bounds what the user can scroll through, NOT how
 * many entries are scored or kept as incremental-filter candidates — those are
 * bounded by the number of entries and live in separate arrays. */
#undef MAX_SCORE_RESULTS
#define MAX_SCORE_RESULTS 4096

/* Arrays owned by this file. All of them are sized >= filter_cap, which is
 * always >= max(n_entries, MAX_SCORE_RESULTS).
 *
 *   cand[]      every entry matching the current query, in discovery order.
 *               This is the set the next appended keystroke re-scores.
 *   cand_score  parallel scores for cand[].
 *   order[]     sort permutation over cand[], heap-allocated (it is as large
 *               as the entry list, so it must not live on the stack).
 *   state.filtered / state.scores
 *               the top MAX_SCORE_RESULTS of cand[], sorted best-first.
 *               These are what render.c draws and emit() selects from. */
static int *cand;
static float *cand_score;
static int *order;
static int filter_cap;

static const float *sort_scores;

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
	if (sort_scores[ia] > sort_scores[ib]) return -1;
	if (sort_scores[ia] < sort_scores[ib]) return 1;
	return 0;
}

/* Grow every array so it can hold `need` entries, never below
 * MAX_SCORE_RESULTS. Returns false on allocation failure. */
bool filter_reserve(int need) {
	int want = need > MAX_SCORE_RESULTS ? need : MAX_SCORE_RESULTS;
	if (filter_cap >= want) return true;
	if (filter_cap > 0 && filter_cap * 2 > want) want = filter_cap * 2;

	int *c = realloc(cand, sizeof(int) * (size_t)want);
	if (!c) return false;
	cand = c;

	float *cs = realloc(cand_score, sizeof(float) * (size_t)want);
	if (!cs) return false;
	cand_score = cs;

	int *o = realloc(order, sizeof(int) * (size_t)want);
	if (!o) return false;
	order = o;

	int *f = realloc(state.filtered, sizeof(int) * (size_t)want);
	if (!f) return false;
	state.filtered = f;

	float *fs = realloc(state.scores, sizeof(float) * (size_t)want);
	if (!fs) return false;
	state.scores = fs;

	filter_cap = want;
	return true;
}

void filter_fini(void) {
	free(cand);
	free(cand_score);
	free(order);
	free(state.filtered);
	free(state.scores);
	cand = NULL;
	cand_score = NULL;
	order = NULL;
	state.filtered = NULL;
	state.scores = NULL;
	filter_cap = 0;
}

/* Incremental filtering.
 *
 * score_match() requires the needle to be a subsequence of the haystack, so an
 * entry that fails a query of length N can never match a query beginning with
 * the same N characters. When the user simply appends one character we only
 * have to re-score the entries that survived the previous keystroke instead of
 * the entire list — for the Run (compgen -c) and Find File (fd) entry lists
 * that is the difference between a few thousand and tens of thousands of scans
 * per keystroke.
 *
 * Anything that shortens or rewrites the query (backspace, ctrl+u, ctrl+w,
 * paste) forces a full rescan. The incremental path is a pure optimisation on
 * top of the same scoring function, so results are identical either way.
 *
 * Note the candidate set is *uncapped*: it keeps every match, and only the
 * visible result list is truncated (after sorting, so the truncation keeps the
 * best matches rather than the first ones found). Capping the candidate set
 * would silently drop matches that a later keystroke could have promoted. */
void filter_update(void) {
	int n_cand = 0;

	if (state.input_len == 0) {
		/* Empty query: everything matches, no scoring needed. */
		if (state.n_entries > filter_cap && !filter_reserve(state.n_entries))
			return;
		int n = state.n_entries < filter_cap ? state.n_entries : filter_cap;
		for (int i = 0; i < n; i++) {
			cand[i] = i;
			cand_score[i] = 0.0f;
		}
		n_cand = n;
	} else {
		/* Appending one character to the previous query can only ever
		 * shrink the match set, so rescore just the survivors. */
		bool appended = state.prev_input_len == state.input_len - 1
			&& memcmp(state.prev_input, state.input, (size_t)state.prev_input_len) == 0
			&& state.n_cand > 0;

		if (appended) {
			for (int i = 0; i < state.n_cand; i++) {
				int idx = cand[i];
				float s = score_match(state.input, state.entries[idx],
					state.hits ? state.hits[idx] : 0);
				if (s > 0.0) {
					cand[n_cand] = idx;
					cand_score[n_cand] = s;
					n_cand++;
				}
			}
		} else {
			if (state.n_entries > filter_cap && !filter_reserve(state.n_entries))
				return;
			int n = state.n_entries < filter_cap ? state.n_entries : filter_cap;
			for (int i = 0; i < n; i++) {
				float s = score_match(state.input, state.entries[i],
					state.hits ? state.hits[i] : 0);
				if (s > 0.0) {
					cand[n_cand] = i;
					cand_score[n_cand] = s;
					n_cand++;
				}
			}
		}
	}

	state.n_cand = n_cand;

	/* Sort best-first, then expose at most MAX_SCORE_RESULTS. Sorting
	 * before truncating is what keeps the visible list the *best* matches
	 * rather than the first MAX_SCORE_RESULTS in index order. */
	if (n_cand > 1) {
		for (int i = 0; i < n_cand; i++) order[i] = i;
		sort_scores = cand_score;
		qsort(order, (size_t)n_cand, sizeof(int), cmp_score);

		int *tmp_i = malloc(sizeof(int) * (size_t)n_cand);
		float *tmp_f = malloc(sizeof(float) * (size_t)n_cand);
		if (tmp_i && tmp_f) {
			for (int i = 0; i < n_cand; i++) tmp_i[i] = cand[order[i]];
			for (int i = 0; i < n_cand; i++) tmp_f[i] = cand_score[order[i]];
			memcpy(cand, tmp_i, sizeof(int) * (size_t)n_cand);
			memcpy(cand_score, tmp_f, sizeof(float) * (size_t)n_cand);
		}
		/* If the temporaries failed to allocate we simply keep the
		 * unsorted order; correctness of the match set is unaffected. */
		free(tmp_i);
		free(tmp_f);
	}

	int n_visible = n_cand > MAX_SCORE_RESULTS ? MAX_SCORE_RESULTS : n_cand;
	memcpy(state.filtered, cand, sizeof(int) * (size_t)n_visible);
	memcpy(state.scores, cand_score, sizeof(float) * (size_t)n_visible);
	state.n_filtered = n_visible;

	memcpy(state.prev_input, state.input, (size_t)state.input_len + 1);
	state.prev_input_len = state.input_len;

	if (state.cursor >= state.n_filtered)
		state.cursor = state.n_filtered > 0 ? state.n_filtered - 1 : 0;
}

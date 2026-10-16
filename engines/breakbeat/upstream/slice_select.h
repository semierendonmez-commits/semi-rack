#ifndef SLICE_SELECT_H
#define SLICE_SELECT_H

#include <stdint.h>

/* Inputs to a single trigger-tick slice decision. */
typedef struct {
    int   current_slice;    /* 0..7 */
    int   beat_position;    /* 0..7, where this tick lands in a 4/4 bar */
    float complexity;       /* 0..1 */
    float anchor;           /* 0..1 */
    float roll;             /* 0..1 */
    int   phrase_bars;      /* 0=Off, 2, 4, 8, 16 */
    float fill;             /* 0..1; only used when phrase_bars > 0 */
    int   bar_in_phrase;    /* 0..(phrase_bars-1); ignored if phrase_bars == 0 */
} slice_inputs_t;

/* Random source: returns float in [0, 1). Implementation provided by caller
 * (production = libc rand(); tests = deterministic seeded PRNG). */
typedef float (*slice_rand_fn)(void *ctx);

/* Picks the next slice index (0..7) given inputs and a random source. */
int slice_select_next(const slice_inputs_t *in, slice_rand_fn rand_fn, void *rand_ctx);

/* Compute per-slice swap-probability multiplier. Exposed for testing. */
float slice_select_weight_at(int slice_idx, float anchor);

/* Apply phrase modulation to params. Exposed for testing. */
void slice_select_apply_phrase(const slice_inputs_t *in,
                                float *out_complexity,
                                float *out_anchor,
                                float *out_roll);

/* Escape-hatch probability constant (5%). Exposed so tests can reason about it. */
#define SLICE_SELECT_ESCAPE_P 0.05f

#endif

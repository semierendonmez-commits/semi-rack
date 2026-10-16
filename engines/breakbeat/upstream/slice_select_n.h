/*
 * slice_select_n — schwung-breakbeat's slice selection generalised to
 * 8/16/32 slices and to anchor weights placed by musical time.
 *
 * Derived from third_party/schwung-breakbeat/slice_select.c (MIT). With
 * num_slices = 8 and loop_bars = 1 it makes exactly the same decisions with
 * exactly the same random draws as slice_select_next(); tests/EngineTest.cpp
 * checks this against the unmodified original.
 */
#ifndef SLICE_SELECT_N_H
#define SLICE_SELECT_N_H

#include "slice_select.h"

/* SEMI port: the extern "C" block is removed so the module can compile this file as C++
 * inside namespace bbeat (one binary holds ~35 engines; no global C symbols). */

typedef struct {
    int   num_slices;     /* 8, 16 or 32 */
    int   current_slice;  /* 0..num_slices-1 */
    int   beat_position;  /* natural slice for this trigger, 0..num_slices-1 */
    float loop_bars;      /* musical length of the loop, e.g. 0.25 .. 8 */
    float complexity;     /* 0..1 */
    float anchor;         /* 0..1 */
    float roll;           /* 0..1 */
    int   is_fill;        /* this trigger falls in a fill step */
    float fill;           /* 0..1, only used when is_fill */
} slice_n_inputs_t;

/* Anchor weight of a slice, located by where it starts inside its bar:
 * slices starting on an eighth note use the original 8-step weight table
 * (kick on 1, snare on 3 locked), slices between eighths are ghost-note
 * territory and swap freely. */
float slice_n_weight (int slice_idx, int num_slices, float loop_bars, float anchor);

int slice_n_next (const slice_n_inputs_t *in, slice_rand_fn rand_fn, void *rand_ctx);


#endif

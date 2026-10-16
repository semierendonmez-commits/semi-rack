#include "slice_select_n.h"
#include <math.h>

float slice_n_weight (int idx, int n, float loop_bars, float anchor)
{
    static const float locked[8] = { 0.0f, 0.5f, 1.0f, 0.7f, 0.0f, 0.5f, 1.0f, 1.2f };
    static const float between = 1.1f;
    if (n < 1) n = 8;
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    if (anchor < 0.0f) anchor = 0.0f;
    if (anchor > 1.0f) anchor = 1.0f;

    /* position of this slice's start within its bar, in eighth notes */
    double eighths = (double) idx * (double) loop_bars * 8.0 / (double) n;
    double inBar = fmod (eighths, 8.0);
    double nearest = floor (inBar + 0.5);
    float w = between;
    if (fabs (inBar - nearest) < 1.0e-6)
        w = locked[((int) nearest) & 7];
    return (1.0f - anchor) + w * anchor;
}

int slice_n_next (const slice_n_inputs_t *in, slice_rand_fn rand_fn, void *rand_ctx)
{
    const int n = in->num_slices > 0 ? in->num_slices : 8;
    const int step = n >= 8 ? n / 8 : 1;   /* one "original eighth" in slices */

    float c = in->complexity, a = in->anchor, ro = in->roll;
    if (in->is_fill)
    {
        float f = in->fill;
        if (f < 0.0f) f = 0.0f;
        if (f > 1.0f) f = 1.0f;
        c = c + (1.0f - c) * f;
        a = a * (1.0f - f);
        ro = ro * (1.0f - f);
    }

    float r = rand_fn (rand_ctx);
    if (r < (1.0f - ro))
    {
        /* MOVE branch */
        float p_swap = c * slice_n_weight (in->beat_position, n, in->loop_bars, a);
        if (rand_fn (rand_ctx) < p_swap)
        {
            int j = (int) (rand_fn (rand_ctx) * (float) n);
            if (j < 0) j = 0;
            if (j > n - 1) j = n - 1;
            return j;
        }
        return in->beat_position;
    }

    /* STAY branch */
    if (rand_fn (rand_ctx) < SLICE_SELECT_ESCAPE_P)
    {
        int j = 2 + (int) (rand_fn (rand_ctx) * 3.0f);
        if (j < 2) j = 2;
        if (j > 4) j = 4;
        return (in->current_slice + j * step) % n;
    }
    {
        float w_here = slice_n_weight (in->current_slice, n, in->loop_bars, a);
        float p_repeat = 1.0f - w_here;
        if (rand_fn (rand_ctx) < p_repeat)
            return in->current_slice;
        int dir = (rand_fn (rand_ctx) < 0.5f) ? step : n - step;
        return (in->current_slice + dir) % n;
    }
}

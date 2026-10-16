/*
 * FLOATER's bridge to CloudSeed (charlesvestal/schwung-cloudseed, an exact C port of
 * Ghost Note Audio's CloudSeedCore, MIT). The module source is included unchanged; this
 * file only renames its Schwung entry point and calls its own instance functions with
 * float buffers instead of Move's int16 blocks. It runs at the module's own 48 kHz.
 */
#define move_audio_fx_init_v2 floater_cloudseed_schwung_entry
#include "cloudseed.c"
#include "floater_cloudseed.h"

void* fl_cs_create (void)
{
    return v2_create_instance (NULL, NULL);
}

void fl_cs_destroy (void* p)
{
    v2_destroy_instance (p);
}

void fl_cs_set (void* p, const fl_cs_params* q)
{
    cloudseed_instance_t* inst = (cloudseed_instance_t*) p;
    if (! inst) return;
    inst->size = q->size;
    inst->decay = q->decay;
    inst->predelay = q->predelay;
    inst->diffusion = q->diffusion;
    inst->low_cut = q->low_cut;
    inst->high_cut = q->high_cut;
    inst->mod_amount = q->mod_amount;
    inst->mod_rate = q->mod_rate;
    inst->cross_seed = q->cross_seed;
    v2_apply_parameters (inst);
}

void fl_cs_process (void* p, const float* inL, const float* inR, float* outL, float* outR, int frames)
{
    cloudseed_instance_t* inst = (cloudseed_instance_t*) p;
    if (! inst || ! inst->channel_l || ! inst->channel_r) return;
    float l[BUFFER_SIZE], r[BUFFER_SIZE];
    int offset = 0;
    while (offset < frames)
    {
        int chunk = frames - offset;
        if (chunk > BUFFER_SIZE) chunk = BUFFER_SIZE;
        memcpy (l, inL + offset, sizeof (float) * (size_t) chunk);
        memcpy (r, inR + offset, sizeof (float) * (size_t) chunk);
        channel_process (inst->channel_l, l, outL + offset, chunk);
        channel_process (inst->channel_r, r, outR + offset, chunk);
        offset += chunk;
    }
}

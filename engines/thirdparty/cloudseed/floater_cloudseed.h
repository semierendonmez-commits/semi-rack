/* FLOATER's C interface to CloudSeed (see floater_cloudseed.c). All parameters 0..1. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float size, decay, predelay, diffusion, low_cut, high_cut, mod_amount, mod_rate, cross_seed;
} fl_cs_params;

void* fl_cs_create (void);
void  fl_cs_destroy (void* instance);
void  fl_cs_set (void* instance, const fl_cs_params* params);
void  fl_cs_process (void* instance, const float* inL, const float* inR, float* outL, float* outR, int frames);   /* wet only, at 48 kHz */

#ifdef __cplusplus
}
#endif

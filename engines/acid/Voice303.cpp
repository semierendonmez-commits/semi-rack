// Host glue for schwung-303.
//
// third_party/schwung-303/dsp/plugin.cpp is compiled UNCHANGED as part of this
// translation unit, so its static P303 instance type and v2_* entry points
// are reachable here. Only the exported init symbol is renamed from outside.
//
// Two departures from the module's own render_block, both forced by the DAW:
//   - the engine and drive run at the host's sample rate instead of 44.1 kHz,
//   - output stays float instead of being clipped to int16.
// The signal path is otherwise exactly v2_render_block's.

//
// SEMI Family: plugin.cpp lives in engines/acid/schwung303 (unchanged). Its two global type names are
// renamed from here (P303, namespace drive) so they cannot meet another schwung module's copy in the
// shared binary; Open303's former global helpers now live in namespace rosic (see thirdparty/open303).
// Voice303 is wrapped in namespace acid.
#define move_plugin_init_v2 acid_303_move_plugin_init_v2
#define P303 acid_P303
#define drive acid_drive
namespace rosic {}
using namespace rosic;  // linToExp / linToLin, global upstream
#include "schwung303/plugin.cpp"
#undef drive
#undef move_plugin_init_v2

#include "Voice303.h"

namespace acid {

static P303* asP (void* p) { return static_cast<P303*> (p); }

Voice303::Voice303()
{
    instance = v2_create ("", nullptr);
}

Voice303::~Voice303()
{
    v2_destroy (instance);
}

void Voice303::prepare (double sampleRate)
{
    auto* p = asP (instance);
    p->engine.setSampleRate (sampleRate);
    p->drive_fx.prepare (sampleRate);
    p->engine.allNotesOff();
}

void Voice303::setParam (const char* key, float value)
{
    char buf[32];
    std::snprintf (buf, sizeof (buf), "%.6f", static_cast<double> (value));
    v2_set_param (instance, key, buf);
}

void Voice303::setParamInt (const char* key, int value)
{
    char buf[16];
    std::snprintf (buf, sizeof (buf), "%d", value);
    v2_set_param (instance, key, buf);
}

void Voice303::midi (const uint8_t* msg, int len)
{
    v2_on_midi (instance, msg, len, MOVE_MIDI_SOURCE_INTERNAL);
}

void Voice303::allNotesOff()
{
    asP (instance)->engine.allNotesOff();
}

void Voice303::render (float* out, int n)
{
    auto* p = asP (instance);
    for (int i = 0; i < n; ++i)
        out[i] = static_cast<float> (p->engine.getSample());
    p->drive_fx.process (out, n, p->n_drive, p->n_drive_mix);
}

const char* const Voice303::floatKeys[Voice303::numFloat] = {
    "cutoff", "resonance", "env_mod", "decay", "accent", "volume", "tuning",
    "normal_decay", "accent_decay", "feedback_hpf", "soft_attack",
    "slide_time", "tanh_shaper_drive", "drive", "drive_mix",
};

void Voice303::setFloat (int index, float fv)
{
    auto* p = asP (instance);
    switch (index)  // mirrors v2_set_param line by line
    {
        case 0:  p->n_cutoff    = fv; apply_cutoff (p);    break;
        case 1:  p->n_resonance = fv; apply_resonance (p); break;
        case 2:  p->n_env_mod   = fv; apply_env_mod (p);   break;
        case 3:  p->n_decay     = fv; apply_decay (p);     break;
        case 4:  p->n_accent    = fv; apply_accent (p);    break;
        case 5:  p->n_volume    = fv; apply_volume (p);    break;
        case 6:  p->n_tuning    = fv; apply_tuning (p);    break;
        case 7:  p->n_normal_decay      = fv; if (p->devil_mod_switch) apply_devil_mods (p); break;
        case 8:  p->n_accent_decay      = fv; if (p->devil_mod_switch) apply_devil_mods (p); break;
        case 9:  p->n_feedback_hpf      = fv; if (p->devil_mod_switch) apply_devil_mods (p); break;
        case 10: p->n_soft_attack       = fv; if (p->devil_mod_switch) apply_devil_mods (p); break;
        case 11: p->n_slide_time        = fv; if (p->devil_mod_switch) apply_devil_mods (p); break;
        case 12: p->n_tanh_shaper_drive = fv; if (p->devil_mod_switch) apply_devil_mods (p); break;
        case 13: p->n_drive     = fv; break;
        case 14: p->n_drive_mix = fv; break;
        default: break;
    }
}

} // namespace acid

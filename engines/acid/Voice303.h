// One schwung-303 instance (Open303 + Devilfish + Soft/RAT drive), driven
// through the module's own plugin_api_v2 entry points for MIDI and params,
// rendered in float at the host's sample rate.
// SEMI Family: vendored from semierendonmez-commits/acid Source/engine; only change: wrapped in namespace acid
// (one plugin binary links ~35 engines).
#pragma once
#include <cstdint>
#include <memory>

namespace acid {

class Voice303
{
public:
    Voice303();
    ~Voice303();

    void prepare (double sampleRate);

    // Same keys as the module's set_param ("cutoff", "devil_mod_switch", …).
    void setParam (const char* key, float value);
    void setParamInt (const char* key, int value);

    // Fast path for modulation: same assignments as the module's set_param,
    // without the string round trip. Index order = Voice303::floatKeys.
    static constexpr int numFloat = 15;
    static const char* const floatKeys[numFloat];
    void setFloat (int index, float value);

    // Raw MIDI as the module's on_midi receives it.
    void midi (const uint8_t* msg, int len);
    void allNotesOff();

    // Mono output, `n` samples, already through the drive stage.
    void render (float* out, int n);


private:
    void* instance = nullptr;
};

} // namespace acid

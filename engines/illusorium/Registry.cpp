// Processing order of the rack. Pages group the modules on screen; the chain
// runs spectral / pitch work first, then time, then perception, then the
// spatial modules (which create the final stereo image), then loudness.
#include "Module.h"

namespace il
{
ModulePtr makePhantom(); ModulePtr makeTartini(); ModulePtr makeRepitch(); ModulePtr makeConstSpectrum();
ModulePtr makeShepard(); ModulePtr makeTritone(); ModulePtr makeScramble();
ModulePtr makeRisset(); ModulePtr makeSpeechToSong(); ModulePtr makePhantomWords(); ModulePtr makeContinuity();
ModulePtr makeDiscontinuity(); ModulePtr makeStream();
ModulePtr makeHuggins(); ModulePtr makeBinaural(); ModulePtr makeOctave(); ModulePtr makeDichotic(); ModulePtr makeFranssen();
ModulePtr makeHaas(); ModulePtr makeDoppler(); ModulePtr makeHrtf(); ModulePtr makeHolo();
ModulePtr makeZwicker(); ModulePtr makeYanny(); ModulePtr makeMask(); ModulePtr makeLoudness();

using Factory = ModulePtr (*)();
static const Factory order[] = {
    makePhantom, makeTartini, makeRepitch, makeConstSpectrum, makeShepard, makeTritone, makeScramble,
    makeZwicker,
    makeRisset, makeSpeechToSong, makePhantomWords, makeContinuity, makeDiscontinuity, makeStream,
    makeYanny, makeMask,
    makeHuggins, makeBinaural, makeOctave, makeDichotic, makeFranssen, makeHaas, makeDoppler, makeHrtf, makeHolo,
    makeLoudness,
};

int numModules() { return (int) (sizeof (order) / sizeof (order[0])); }

namespace
{
    struct Infos
    {
        std::vector<ModulePtr> protos;
        Infos() { for (auto f : order) protos.push_back (f()); }
    };
    const Infos& infos() { static Infos i; return i; }
}

const MInfo& moduleInfo (int index) { return infos().protos[(size_t) std::min (std::max (index, 0), numModules() - 1)]->info(); }
ModulePtr createModule (int index) { return order[std::min (std::max (index, 0), numModules() - 1)](); }
int pageOf (int index) { return moduleInfo (index).page; }
} // namespace il

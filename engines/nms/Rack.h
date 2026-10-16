// SEMI Family port: + outValue () (read-only accessor); otherwise unchanged.
// NMS - the rack: modules in panel order, patch cables, and the channel
// chains that normal the unpatched inputs (see Module.h). Every input sums
// its cables; processing is per sample with a one-sample delay on every
// connection, so any patch (feedback included) is well defined. Edits happen
// on the message thread under the processor's lock.
#pragma once
#include "Module.h"

namespace nms
{
struct Cable { int src = 0, out = 0, dst = 0, in = 0, colour = 0; };

class Rack
{
public:
    std::vector<std::shared_ptr<Module>> mods;
    std::vector<Cable> cables;
    std::vector<Cable> normals;       // computed by compile(): the channel chains
    std::array<Module*, kLanes> laneSrc {};   // first instrument of each lane (LINK, LISTEN)
    int nextId = 1;

    std::shared_ptr<Module> make (const std::string& type, int forceId = 0);
    void insert (std::shared_ptr<Module>, int pos = -1);
    void remove (int id);
    void clear();
    Module* find (int id) const;
    Module* findType (const std::string& type) const;
    int index (int id) const;
    int count (const std::string& type) const;
    bool canAdd (const std::string& type) const;
    int freeChannel() const;          // the first mixer channel without a module

    bool connect (const Cable&);
    void disconnect (int dst, int in);
    void disconnectOut (int src, int out);
    void removeCable (size_t i);
    std::vector<size_t> cablesInto (int dst, int in) const;

    void compile();
    void prepare (double sampleRate);
    void reset();
    void process (Ctx&, float* L, float* R, int n);

    float outLevel (int modIndex, int out) const;
    // SEMI Family port: the last value of an output (for the module's per-channel TRIG jacks)
    float outValue (int modIndex, int out) const
    {
        if (modIndex < 0 || modIndex >= (int) outOff.size()) return 0.0f;
        const size_t i = (size_t) (outOff[(size_t) modIndex] + out);
        return i < outPrev.size() ? outPrev[i] : 0.0f;
    }
    double sampleRate() const { return sr; }

private:
    double sr = 48000.0;
    std::vector<float> outPrev, outCur, inBuf;
    std::vector<int> outOff, inOff;
    struct Wire { int s, d; };
    std::vector<Wire> wires;
    std::unique_ptr<std::atomic<float>[]> level;
    size_t numOuts = 0;
    int outIdx = -1;
};

struct FactoryPatch { const char* name; const char* category; void (*build) (Rack&); int patterns; uint32_t seed; };
const std::vector<FactoryPatch>& factoryPatches();
void buildPatch (Rack&, int index);
} // namespace nms

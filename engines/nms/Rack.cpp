#include "Rack.h"

namespace nms
{
std::shared_ptr<Module> Rack::make (const std::string& type, int forceId)
{
    std::shared_ptr<Module> m (Registry::create (type).release());
    if (! m) return nullptr;
    m->id = forceId > 0 ? forceId : nextId;
    nextId = std::max (nextId, m->id + 1);
    m->prepare (sr);
    m->reset();
    return m;
}

void Rack::insert (std::shared_ptr<Module> m, int pos)
{
    if (! m) return;
    if (pos < 0 || pos > (int) mods.size()) mods.push_back (m);
    else mods.insert (mods.begin() + pos, m);
    compile();
}

void Rack::remove (int id)
{
    cables.erase (std::remove_if (cables.begin(), cables.end(), [id] (const Cable& c) { return c.src == id || c.dst == id; }), cables.end());
    mods.erase (std::remove_if (mods.begin(), mods.end(), [id] (const std::shared_ptr<Module>& m) { return m->id == id; }), mods.end());
    compile();
}

void Rack::clear() { mods.clear(); cables.clear(); nextId = 1; compile(); }
Module* Rack::find (int id) const { for (auto& m : mods) if (m->id == id) return m.get(); return nullptr; }
Module* Rack::findType (const std::string& t) const { for (auto& m : mods) if (m->spec.id == t) return m.get(); return nullptr; }
int Rack::index (int id) const { for (size_t i = 0; i < mods.size(); ++i) if (mods[i]->id == id) return (int) i; return -1; }
int Rack::count (const std::string& type) const { int n = 0; for (auto& m : mods) if (m->spec.id == type) ++n; return n; }
bool Rack::canAdd (const std::string& type) const
{
    auto* t = Registry::find (type);
    return t != nullptr && (! t->unique || count (type) == 0) && mods.size() < 40;
}
int Rack::freeChannel() const
{
    bool used[kChannels] {};
    for (auto& m : mods) if (m->spec.chained) { const int c = m->channel(); if (c >= 0 && c < kChannels) used[c] = true; }
    for (int c = 0; c < kChannels; ++c) if (! used[c]) return c;
    return 0;
}

bool Rack::connect (const Cable& c)
{
    auto* s = find (c.src); auto* d = find (c.dst);
    if (! s || ! d || c.out < 0 || c.out >= (int) s->spec.outs.size() || c.in < 0 || c.in >= (int) d->spec.ins.size()) return false;
    for (auto& x : cables) if (x.src == c.src && x.out == c.out && x.dst == c.dst && x.in == c.in) return false;
    cables.push_back (c);
    compile();
    return true;
}
void Rack::disconnect (int dst, int in)
{
    cables.erase (std::remove_if (cables.begin(), cables.end(), [=] (const Cable& c) { return c.dst == dst && c.in == in; }), cables.end());
    compile();
}
void Rack::disconnectOut (int src, int out)
{
    cables.erase (std::remove_if (cables.begin(), cables.end(), [=] (const Cable& c) { return c.src == src && c.out == out; }), cables.end());
    compile();
}
void Rack::removeCable (size_t i) { if (i < cables.size()) { cables.erase (cables.begin() + (long) i); compile(); } }
std::vector<size_t> Rack::cablesInto (int dst, int in) const
{
    std::vector<size_t> r;
    for (size_t i = 0; i < cables.size(); ++i) if (cables[i].dst == dst && cables[i].in == in) r.push_back (i);
    return r;
}

void Rack::compile()
{
    outOff.assign (mods.size(), 0); inOff.assign (mods.size(), 0);
    size_t no = 0, ni = 0;
    outIdx = -1;
    for (size_t i = 0; i < mods.size(); ++i)
    {
        outOff[i] = (int) no; inOff[i] = (int) ni;
        no += mods[i]->spec.outs.size(); ni += mods[i]->spec.ins.size();
        mods[i]->patched = 0;
        if (mods[i]->isOutput() && outIdx < 0) outIdx = (int) i;
    }
    for (auto& c : cables) if (auto* d = find (c.dst)) if (c.in < 64) d->patched |= (uint64_t) 1 << c.in;

    // ---- channel chains --------------------------------------------------
    normals.clear();
    auto isPatchedIn = [] (const Module* m, int in) { return m->isPatched (in); };
    auto addNorm = [this] (const std::vector<Module*>& srcs, const char* outKey, Module* dst, int in)
    {
        for (auto* s : srcs) { const int o = s->spec.out (outKey); if (o >= 0) normals.push_back ({ s->id, o, dst->id, in, -1 }); }
    };
    // lanes 0..7 feed mixer channels; lanes 8 / 9 start at the mixer's SEND A / B and end at its returns
    std::vector<Module*> group[kLanes];
    Module* lastInstr[kLanes] {};
    Module* mixer = nullptr;
    for (auto& mp : mods) if (mp->isOutput()) { mixer = mp.get(); break; }
    laneSrc.fill (nullptr);
    for (auto& mp : mods)
        if (mp->spec.chained && mp->spec.cat != Cat::Fx)
        { const int l = std::max (0, std::min (kLanes - 1, mp->channel())); if (laneSrc[(size_t) l] == nullptr) laneSrc[(size_t) l] = mp.get(); }
    for (auto& mp : mods)
    {
        auto* m = mp.get();
        if (! m->spec.chained) continue;
        const int ch = std::max (0, std::min (kLanes - 1, m->channel()));
        if (m->spec.cat == Cat::Fx)
        {
            const auto& ins = m->spec.ins;
            const bool fromSend = ch >= kChannels && group[ch].empty() && mixer != nullptr;
            for (int i = 0; i < (int) ins.size(); ++i)
            {
                if (isPatchedIn (m, i)) continue;
                const Norm nr = ins[(size_t) i].norm;
                if (nr == Norm::ChainL || nr == Norm::ChainR)
                {
                    if (nr == Norm::ChainR && isPatchedIn (m, 0)) { for (auto& c : cables) if (c.dst == m->id && c.in == 0) normals.push_back ({ c.src, c.out, m->id, i, -1 }); continue; }
                    if (fromSend)
                    {
                        const char* key = ch == kChannels ? (nr == Norm::ChainL ? "sal" : "sar") : (nr == Norm::ChainL ? "sbl" : "sbr");
                        const int o = mixer->spec.out (key);
                        if (o >= 0) normals.push_back ({ mixer->id, o, m->id, i, -1 });
                    }
                    else addNorm (group[ch], nr == Norm::ChainL ? "l" : "r", m, i);
                }
                else if (nr == Norm::ChainTrig)
                {
                    const int li = m->spec.param ("lch");
                    const int listen = li >= 0 ? (int) m->getPlain (li) - 1 : -1;     // 0 = CHAIN
                    Module* src = listen >= 0 ? laneSrc[(size_t) listen] : lastInstr[ch];
                    if (src != nullptr && src != m) addNorm ({ src }, "trig", m, i);
                }
            }
            group[ch].assign (1, m);
        }
        else { group[ch].push_back (m); lastInstr[ch] = m; }
    }
    for (auto& mp : mods)
    {
        auto* m = mp.get();
        const auto& ins = m->spec.ins;
        for (int i = 0; i < (int) ins.size(); ++i)
        {
            const Norm nr = ins[(size_t) i].norm;
            if (isPatchedIn (m, i)) continue;
            int lane = -1; bool right = false;
            if (nr == Norm::ChannelL || nr == Norm::ChannelR) { lane = i / 2; right = nr == Norm::ChannelR; }
            else if (nr == Norm::ReturnL || nr == Norm::ReturnR) { lane = kChannels + (i - 2 * kChannels) / 2; right = nr == Norm::ReturnR; }
            if (lane < 0 || lane >= kLanes) continue;
            if (right && isPatchedIn (m, i - 1)) { for (auto& c : cables) if (c.dst == m->id && c.in == i - 1) normals.push_back ({ c.src, c.out, m->id, i, -1 }); continue; }
            addNorm (group[lane], right ? "r" : "l", m, i);
        }
    }

    std::vector<float> prev (no, 0.0f);
    outPrev.swap (prev);
    outCur.assign (no, 0.0f);
    inBuf.assign (ni + 1, 0.0f);
    if (no != numOuts || ! level) { level.reset (new std::atomic<float>[no + 1]); for (size_t i = 0; i <= no; ++i) level[i].store (0.0f); numOuts = no; }
    wires.clear();
    for (auto* list : { &cables, &normals })
        for (auto& c : *list)
        {
            const int si = index (c.src), di = index (c.dst);
            if (si < 0 || di < 0) continue;
            wires.push_back ({ outOff[(size_t) si] + c.out, inOff[(size_t) di] + c.in });
        }
}

void Rack::prepare (double s)
{
    sr = s;
    for (auto& m : mods) { m->prepare (s); m->reset(); }
    compile();
}

void Rack::reset() { for (auto& m : mods) m->reset(); std::fill (outPrev.begin(), outPrev.end(), 0.0f); }

float Rack::outLevel (int mi, int out) const
{
    if (mi < 0 || mi >= (int) outOff.size() || ! level) return 0.0f;
    const size_t i = (size_t) (outOff[(size_t) mi] + out);
    return i < numOuts ? level[i].load (std::memory_order_relaxed) : 0.0f;
}

void Rack::process (Ctx& ctx, float* L, float* R, int n)
{
    const size_t nm = mods.size();
    int done = 0;
    while (done < n)
    {
        const int len = std::min (32, n - done);
        ctx.lanes = &laneSrc;
        for (auto& m : mods) m->block (ctx);
        for (int k = 0; k < len; ++k)
        {
            std::fill (inBuf.begin(), inBuf.end(), 0.0f);
            for (const auto& w : wires) inBuf[(size_t) w.d] += outPrev[(size_t) w.s];
            for (size_t i = 0; i < nm; ++i)
            {
                auto* m = mods[i].get();
                float* o = outCur.data() + outOff[i];
                const size_t no = m->spec.outs.size();
                m->tick (inBuf.data() + inOff[i], o);
                for (size_t j = 0; j < no; ++j)
                    if (! std::isfinite (o[j]) || std::abs (o[j]) > 1.0e4f) { for (size_t q = 0; q < no; ++q) o[q] = 0.0f; m->reset(); break; }
            }
            outPrev.swap (outCur);
            float l = 0, r = 0;
            if (outIdx >= 0) { l = mods[(size_t) outIdx]->mainL; r = mods[(size_t) outIdx]->mainR; }
            if (! std::isfinite (l) || ! std::isfinite (r)) { l = r = 0; mods[(size_t) outIdx]->reset(); }
            L[done + k] = l; R[done + k] = r;
        }
        for (size_t i = 0; i < numOuts; ++i)
        {
            const float v = std::abs (outPrev[i]), old = level[i].load (std::memory_order_relaxed);
            level[i].store (v > old ? v : old * 0.92f, std::memory_order_relaxed);
        }
        ctx.pos16 += ctx.pos16PerSample * len;
        ctx.sample += len;
        done += len;
    }
}
} // namespace nms

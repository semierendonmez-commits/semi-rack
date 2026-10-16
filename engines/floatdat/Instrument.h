// SEMI port: FLOATDAT's processor (Source/PluginProcessor.*) without JUCE, MIDI, presets on disk or video.
// IKD (ryoz_ikedar_ln) + FLT (FLOATER, engines/floater: the plug-in's FLT layer is an older FLOATER v0.4
// copy; here it is the current engine and patch layer, a superset) + VTX (Vortessa) + JDAT, played by the
// Pinball 2 Markov sequencer, fed by a DataBlob. The plug-in's parameters are a plain struct (Settings);
// its MIDI lane notes become lane gates (start offset + length) for the module; MIDI in becomes fireLane().
// FLT's MUTATE / DATA PATCH / data feed, done on the message thread in the plug-in, run at the start of the
// next block here (Case is allocation-free, see engines/floater/Case.h).
#pragma once

#include "DataBlob.h"
#include "IkdEngine.h"
#include "JdatEngine.h"
#include "Pinball.h"
#include "Vtx.h"
#include "floater/Case.h"
#include "floater/Engine.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace fdat
{
namespace pb = fdat_pinball;

// lane destinations (the plug-in's ids::dests)
enum Dest { dMidi, dIkdStep, dIkdMutate, dFltHit, dFltBurst, dFltMutate, dFltPatch, dFltUnflip, dVtxKick, dVtxTone, dVtxGhost,
            dJdatTick, dJdatClear, dDataStep, dDataJump, dDataRegion, dVisCut, dVisFlash, dAllTick, dNumDests };
const std::vector<std::string>& destNames();
const std::vector<std::string>& trigNames();
const std::vector<std::string>& engineClockNames();   // own grid pinball
const std::vector<std::string>& fltClockNames();      // own pinball both
const std::vector<std::string>& fltMapNames();
const std::vector<std::string>& feedNames();          // off, on load, on move
const std::vector<std::string>& divNames();
const std::vector<std::string>& autoNames();
const std::vector<std::string>& sourceNames();
const std::vector<std::string>& interpNames();
const std::vector<std::string>& writeNames();
const std::vector<std::string>& loopNames();
const std::vector<std::string>& scanNames();
const std::vector<std::string>& stepSizeNames();
const std::vector<std::string>& vtxFeedNames();
const std::vector<std::string>& factoryNames();
const std::vector<std::string>& sceneNames();
const std::vector<std::string>& visAutoNames();

struct Settings
{
    // ---- global
    bool run = true;
    float bpm = 120.0f;
    float master = -3.0f;          // dB (-60 = off)
    bool clip = true, limit = true, dcBlock = true;
    float lvlIkd = 0.0f, lvlFlt = -2.0f, lvlVtx = -4.0f, lvlJdat = -3.0f;
    int velDepth = 100;
    // ---- pinball
    float pbMult = 1.0f, pbSwing = 0.0f, pbLen = 1.0f;
    int pbStart = 1;
    bool pbOut = true;
    struct Step { int trig = 0, vel = 100, prob = 100; float dur = 5.0f, dly = 0.0f; int dprob = 100; };
    std::array<Step, pb::kSteps> steps {};
    struct Lane { int note = 36; bool choke = true; int dest = 0; };
    std::array<Lane, pb::kLanes> lanes {};
    // ---- IKD
    int ikdClk = 2, ikdDiv = 3;
    float ikdSwing = 12.0f;
    bool ikdHold = false;
    int ikdAuto = 0, ikdChance = 50;
    float ikdWave = 0.0f, ikdSpec = 0.0f, ikdSine = -6.0f;
    int ikdFeed = 2;
    bool ikdFeedSnd = false;
    // ---- FLT
    int fltClk = 2, fltFeed = 1;
    bool fltFeedSnd = true, fltFeedModel = false;
    int fltVel = 70, fltBurst = 3, fltMap = 0;
    // ---- VTX (dB levels, the rest as the plug-in's ranges)
    float vtxKickLvl = -3.0f, vtxToneLvl = -6.0f, vtxGhostLvl = -9.0f, vtxDriftLvl = -60.0f;
    bool vtxKickOwn = false;
    float vtxKickRate = 8.0f, vtxKickProb = 0.7f, vtxKickPitch = 60.0f, vtxKickDecay = 1.0f, vtxKickDrive = 0.3f, vtxKickRev = 0.5f, vtxKickPre = 80.0f;
    bool vtxToneOwn = false;
    float vtxToneProb = 0.6f, vtxToneFreq = 800.0f, vtxToneFb = 0.7f, vtxToneCollapse = 0.3f, vtxToneMut = 0.4f, vtxToneCrush = 0.3f;
    float vtxGSens = 0.2f, vtxGFb = 0.55f, vtxGPitch = 640.0f, vtxGDetune = 0.3f, vtxGRate = 0.3f, vtxGBright = 0.5f, vtxGSelf = 0.2f, vtxGSat = 0.9f, vtxGInput = 0.5f;
    float vtxDriftFreq = 55.0f, vtxDriftDetune = 0.35f;
    int vtxFeed = 1;
    // ---- JDAT
    int jdatClk = 2;
    float jdatRate = 120.0f;
    int jdatDiv = 3;
    std::array<int, 4> jdatSrc { 3, 3, 3, 3 };
    bool jdatLink = true;
    int jdatClear = 40, jdatJump = 30;
    bool jdatHold = false;
    int jdatInterp = 1, jdatWrite = 1, jdatFeed = 1, jdatLoop = 0;
    std::array<float, 4> jdatLvl { 0.0f, 0.0f, 0.0f, -6.0f };
    // ---- data
    int dataAs = 0, dataScan = 0;
    float dataRate = 2.0f;
    int dataStep = 2;
    // ---- visual (kept by presets)
    int visScene = 8, visAuto = 2;
    bool visFlash = true;
    float visDensity = 0.7f;

    Settings();   // the lanes' default destinations and notes
};

struct Transport { bool playing = false; double bpm = 120.0, ppq = 0.0; bool host = false; };

// a lane fired (the plug-in's MIDI note): offset in the block, how long the note is, its velocity
struct LaneGate { int offset, lane, velocity; float durMs; };

// what happened, for the screens (the plug-in's VisEvent, reduced)
struct Telemetry
{
    std::array<uint32_t, pb::kLanes> laneHits {};
    std::array<int, pb::kLanes> laneVel {};
    std::array<uint32_t, pb::kSteps> stepFires {};
    std::array<uint32_t, flt::kLanes> fltLaneHits {};
    uint32_t sceneCuts = 0, flashes = 0, ikdSteps = 0, jdatTicks = 0, fltHits = 0, dataMoves = 0;
    float rmsIkd = 0, rmsFlt = 0, rmsJdat = 0, rmsVtx = 0, rmsOut = 0, peakOut = 0;
    bool playing = false;
    double ppq = 0.0, bpm = 120.0;
};

class Instrument
{
public:
    Instrument();
    void prepare (double sampleRate);
    void setSeed (uint32_t s);

    Settings S;
    fdat_ryoz::SharedState ikdShared;
    fdat_ryoz::Engine ikd { ikdShared };
    flt::Case fltCase;
    flt::Engine flt;
    fdat_jdat::Engine jdat;
    fdat_vtx::Engine vtx;
    pb::Sequencer pinball;
    std::array<bool, fdat_ryoz::kNumVoices> ikdLocks {};
    Telemetry tele;

    // data: the audio thread's blob (owned elsewhere; set at the start of a block)
    void setBlob (const fdat_pdata::DataBlob* b) { current = b; }
    const fdat_pdata::DataBlob* blob() const { return current; }
    uint64_t head = 0;

    // requests (audio thread, before process)
    void fireLaneNow (int lane, int velocity) { laneFireRequests |= 1u << lane; laneFireVel[(size_t) lane] = velocity; }
    void requestReset() { resetRequest = true; }
    void requestHalt() { haltRequest = true; }
    void requestJump (int step) { jumpRequest = step; }
    void requestHead (uint64_t bits) { headRequest = (int64_t) bits; }
    void requestFltPatch() { ++fltPatchRequests; }
    void requestFltMutate() { ++fltMutateRequests; }
    void requestFltNewPatch() { fltNewRequest = true; }

    // renders n samples (n <= kMaxBlock); gates of the lanes fired in it
    static constexpr int kMaxBlock = 512;
    void process (float* L, float* R, int n, const Transport& t);
    const LaneGate* gates() const { return laneGates.data(); }
    int numGates() const { return nGates; }

    // presets (the plug-in's ten)
    void loadFactory (int index);
    int factoryIndex = 0;

    // the pinball functions and the IKD / FLT helpers of the plug-in
    enum class Fn { RandomizeTrigs, ClearSteps, ClearMatrix, ClearAll, MatrixChain, MatrixDrunk, MatrixRandom, MatrixPingPong, MatrixJumps, RandomizeSteps };
    void pinballFunction (Fn f);
    enum class IkdRandom { Pattern, Sound, All };
    void ikdRandomise (IkdRandom what);
    void ikdMutate (int voice);
    void fltDataPatch (bool lanes, bool sound, bool models);

private:
    struct Block;
    void fireLane (int lane, int velocity, float durMs, int offset, bool midi, Block& blk);
    void moveHead (uint64_t newHead, bool feed);
    void feedEngines (bool onLoad);
    void fltHit (int lane, int velocity, int offset, int pbLane, bool burst);
    float accentFor (int velocity) const;
    float urand() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (float) (rs >> 8) * (1.0f / 16777216.0f); }
    int irand (int n) { return n <= 1 ? 0 : std::min (n - 1, (int) (urand() * (float) n)); }

    struct Symbols : fdat_jdat::SymbolSource
    {
        const fdat_pdata::DataBlob* blob = nullptr;
        uint64_t pos = 0;
        bool trit = true;
        float next() noexcept override;
    } symbols;

    const fdat_pdata::DataBlob* current = nullptr;
    double sr = 44100.0;
    int64_t sampleClock = 0;
    double freePpq = 0.0;
    bool wasPlaying = false;
    double headFrac = 0.0;
    uint64_t lastFedRow = ~0ull;
    uint32_t seenSerial = 0;
    uint32_t rs = 0x9E3779B9u;
    pb::Rand laneRng;

    uint32_t laneFireRequests = 0;
    std::array<int, pb::kLanes> laneFireVel {};
    bool resetRequest = false, haltRequest = false;
    int jumpRequest = -1;
    int64_t headRequest = -1;
    int fltPatchRequests = 0, fltMutateRequests = 0;
    bool fltNewRequest = false, fltFeedPending = false;

    struct PendingFire { int64_t at; int lane; int velocity; float durMs; bool midi; };
    std::array<PendingFire, 128> pending {};
    int numPending = 0;

    std::array<LaneGate, 256> laneGates {};
    int nGates = 0;

    int fltRobin = 0;
    struct FltBurst { int64_t at; int lane; int vel; };
    std::array<FltBurst, 64> fltBursts {};
    int numFltBursts = 0;
    struct FltHitNow { int offset, lane, vel; };
    std::array<FltHitNow, 128> fltHits {};
    int numFltHits = 0;

    std::array<fdat_vtx::Hit, 128> vtxHits {};
    int numVtxHits = 0;
    std::vector<float> buf;   // 9 x kMaxBlock
    float limEnv = 1.0f;
    float dcX[2] {}, dcY[2] {};
    float smIkd = 1, smFlt = 1, smJdat = 1, smVtx = 1, smMaster = 1;
};

} // namespace fdat

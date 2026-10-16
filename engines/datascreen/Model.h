// DATASCREEN model for Rack / MetaModule (semi-rack addition, namespace dsc).
//
// The plugin fills its Model once per frame on the message thread from three places: the
// shared-memory bus (sibling snapshots and events), MIDI, and the audio it sits on
// (Source/model/Ingest.cpp). Rack has no shared memory, so here:
//   * the audio is the IN L / R jacks (pushAudio per sample);
//   * the events are the TRIG (lane hits), STEP (a sibling's step CV), RST and BUS jacks;
//   * every section a scene reads (automata, grid, buffers, matrix, steps, lanes, reel, CV,
//     voices) is always "made by the model" from the data under the read head and the audio,
//     exactly as Ingest::synthesise does when no sibling sends one;
//   * the per-frame Motion (envelopes, histories) of Source/scenes/Scenes.cpp is folded in, so
//     the screen only draws a Frame and never keeps state of its own (stable frames).
// The analysis (1024-point FFT -> 48 log bands, flux onsets, kick) follows Ingest::analyse.
// Everything runs on the audio thread at the display rate (no allocation after construction).
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "DataBlob.h"

namespace dsc {

using pdata::DataBlob;

constexpr int kBands = 48, kWin = 1024, kSteps = 16, kStates = 17, kLanes = 8, kRows = 9;
constexpr int kHist = 64;                 // 1/32-note histories
constexpr int kSpecRows = 32, kSpecBands = 24;
constexpr int kTrail = 48;                // attractor trail (MetaModule path budget)
constexpr int kReel = 128, kScope = 128, kCvHist = 32;
constexpr int kNumScenes = 26;

// ---- scenes: names, variants, pools (Source/scenes/Scenes.cpp kInfo, Source/Params.h) ----------
struct SceneInfo {
	const char* name;
	int numVariants;
	const char* variants[3];
};
const SceneInfo& sceneInfo(int i);
constexpr int kNumPools = 6;
const char* poolName(int p);
bool inPool(int pool, int scene);
constexpr int kNumChanges = 9;
const char* changeName(int c);   // manual, every bar ... on onset, on BUS turn

inline uint32_t hash32(uint32_t x) noexcept {
	x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
	return x;
}
inline float hashf(uint32_t x) noexcept { return (float) (hash32(x) >> 8) * (1.0f / 16777216.0f); }

// ---- what one screen frame reads ------------------------------------------------------------
struct Frame {
	uint32_t serial = 0;
	// clock
	double beats = 0.0;
	float bpm = 120.f, beatPhase = 0.f, barPhase = 0.f;
	int bar = 0, sixteenth = 0;
	bool playing = false;
	// envelopes 0..1 (Motion)
	float level = 0.f, low = 0.f, mid = 0.f, high = 0.f, kick = 0.f, hit = 0.f, step = 0.f, flash = 0.f, energy = 0.f;
	float centroid = 0.f;
	uint8_t bands[kBands]{};
	int8_t wave[kScope]{}, waveL[64]{}, waveR[64]{};
	// histories, newest at histHead
	int histHead = 0;
	uint8_t laneHist[kHist]{};
	uint16_t rowHist[kHist]{};
	uint32_t gateHist[kHist]{};
	uint8_t spec[kSpecRows][kSpecBands]{};
	int specHead = 0;
	int8_t cvHist[8][kCvHist]{};
	int cvHead = 0;
	// sections
	uint64_t autoRows[kRows]{};
	uint8_t autoEnv[kRows]{};
	int autoStep = 0;
	uint64_t grid[32]{};
	uint32_t gates = 0;
	uint8_t gridCol[32]{};
	int cursor = -1;
	int8_t buf[3][64]{};
	uint8_t bufGate[3]{};
	float bufPhase = 0.f;
	uint8_t matrix[kStates][kStates]{};   // rows normalised to 100
	int stepCur = -1, lastFrom = -1, lastTo = -1;
	int8_t path[16]{};
	int pathN = 0;
	bool learned = false;
	int8_t trig[kSteps]{};
	uint8_t vel[kSteps]{}, prob[kSteps]{};
	uint16_t stepFlash = 0;
	uint16_t lanePat[kLanes]{}, laneFlip[kLanes]{};
	uint8_t laneLevel[kLanes]{};
	uint8_t laneFlash = 0;
	bool lanesLearned = false;
	uint8_t reel[kReel]{};
	float gene[4]{};
	float voices[8]{};
	int8_t cv[8]{};
	float attrX[kTrail]{}, attrY[kTrail]{}, attrZ[kTrail]{};
	int attrHead = 0;
	// data
	bool haveData = false;
	char name[40]{}, type[24]{}, note[40]{};
	uint64_t head = 0, nbits = 0, headByte = 0, fileSize = 0;
	float entropy = 0.f;
	uint8_t entMap[64]{};
	uint8_t bytes[256]{};
	uint64_t bits[8]{};                    // 512 bits from the head (bit 0 first)
	char str[4][32]{};
	uint8_t cdf[257]{};                    // the window's byte distribution, 0..255
	// picture
	int scene = 0, variant = 0;
	uint32_t cuts = 0;
	float density = 0.6f;
};

struct Settings {
	int scan = 3;            // 0 hold, 1 step, 2 run, 3 follow
	float scanRate = 8.f;    // rows of 64 bits per second (run)
	float react = 1.f;       // 0..2
	float speed = 1.f;       // 0..2
	bool freeze = false;     // the tape stops recording
};

class Model {
public:
	Model();
	void prepare(double sampleRate) { sr = sampleRate > 1000.0 ? sampleRate : 48000.0; }

	// audio thread, every sample
	void pushAudio(float l, float r) {
		ringL[wr & (kWin - 1)] = l;
		ringR[wr & (kWin - 1)] = r;
		wr++;
	}
	// events (audio thread)
	void laneHit(int lane, float vel);
	void stepEntered(int step);
	void resetPulse();
	void flash(float amount);
	void setBlob(const DataBlob* b);          // the blob outlives its use (Slot on the audio thread)
	void setHeadNorm(float x01);              // HEAD CV
	void setClock(double beats, double bpm, bool playing);
	void setEnergy(float e01) { busEnergy = e01; }

	// display rate: everything above becomes one Frame
	void update(Frame& f, float dt, const Settings& s);

	uint64_t head() const { return headBits; }
	bool onsetNow() const { return onset > 0.99f; }

private:
	void analyse(float dt);
	void advanceHead(float dt, const Settings& s);
	void histories(Frame& f);
	void synthesise(Frame& f, float dt, const Settings& s);
	void dataView(Frame& f);
	void attractor(float dt, const Settings& s);
	uint8_t byteAt(uint64_t i) const noexcept {
		return blob ? blob->byteAt(i) : (uint8_t) ((i * 2654435761u) >> 24);
	}
	int bitAt(uint64_t i) const noexcept { return blob ? blob->bit(i) : (int) ((i * 2654435761u >> 13) & 1u); }
	uint64_t word64(uint64_t pos) const noexcept {
		if (blob)
			return blob->word64(pos);
		uint64_t w = 0;
		for (int i = 0; i < 64; ++i)
			w |= (uint64_t) bitAt(pos + (uint64_t) i) << i;
		return w;
	}
	uint64_t headByte() const noexcept { return blob ? blob->byteForBit(headBits) : headBits / 8u; }

	double sr = 48000.0;
	// audio
	float ringL[kWin]{}, ringR[kWin]{};
	uint32_t wr = 0;
	std::vector<float> re, im, hann;
	std::vector<int> bitrev;
	float bands[kBands]{}, prevBands[kBands]{};
	float rms = 0.f, peak = 0.f, flux = 0.f, fluxAvg = 0.f, onset = 0.f, onsetEnv = 0.f;
	float bass = 0.f, midA = 0.f, highA = 0.f, centroid = 0.f, kickPulse = 0.f;
	// motion envelopes
	float level = 0.f, low = 0.f, mid = 0.f, high = 0.f, kick = 0.f, hit = 0.f, stepEnv = 0.f, flashEnv = 0.f, energy = 0.f;
	float envBands[kBands]{};
	float busEnergy = -1.f;
	// clock
	double beats = 0.0, bpm = 120.0;
	bool playing = false;
	int lastSix = -1;
	int64_t sixCount = 0;
	double lastHistBeat = -1.0;
	// events
	float laneFlash[kLanes]{};
	uint8_t laneAcc = 0, laneFlashAcc = 0;
	uint16_t stepFlashAcc = 0;
	float trigPulse = 0.f, stepPulse = 0.f;
	int step = -1, lastFrom = -1, lastTo = -1;
	int stepEventsPending = 0;
	int8_t pathBuf[16]{};
	int pathN = 0;
	float learned[kStates][kStates]{};
	float learnedTotal = 0.f;
	uint32_t laneBarNow[kLanes]{}, laneBarLast[kLanes]{};
	bool laneSeen = false;
	int laneBar = -1;
	uint32_t lastSeed = 0;
	// data
	const DataBlob* blob = nullptr;
	uint64_t headBits = 0;
	double scanAcc = 0.0;
	float headCv = -1.f;
	// made sections' memory
	uint64_t rows[kRows]{};
	uint64_t rowsBlock = ~0ull;
	uint32_t rowsSerial = ~0u;
	int rowsSix = -1;
	float autoEnv[kRows]{}, bufGate[3]{}, laneLevel[kLanes]{};
	float tape[256]{};
	int tapeWrite = 0;
	double tapeLastBeat = -1.0;
	float tapeAcc = 0.f;
	float cvState[8]{};
	float logistic = 0.37f;
	float voiceKick = 0.f, voiceBands[4]{};
	// attractor
	double lx = 0.1, ly = 0.0, lz = 20.0;
	float trailX[kTrail]{}, trailY[kTrail]{}, trailZ[kTrail]{};
	int trailHead = 0;
	// histories
	int histHead = 0;
	uint8_t laneHist[kHist]{};
	uint16_t rowHist[kHist]{};
	uint32_t gateHist[kHist]{};
	uint8_t spec[kSpecRows][kSpecBands]{};
	int specHead = 0;
	int8_t cvHist[8][kCvHist]{};
	int cvHead = 0;
	uint32_t gatesNow = 0;
	uint32_t serial = 0;
};

// ---- scene changes (the CHANGE / POOL / VARIANT controls) ---------------------------------------
struct Director {
	int scene = 0;
	uint32_t cuts = 0;
	int lastBar = -1;
	double lastOnsetBeat = -100.0;

	// a cut to `s` (manual, CV, BUS)
	void cutTo(int s);
	// next scene in the pool (the NEXT button, and the auto modes)
	void next(int pool, bool random, uint32_t salt);
	// once per frame: the automatic cuts. stepEvent / onsetEvent / turnEvent: since the last call.
	bool tick(const Frame& f, int change, int pool, bool stepEvent, bool onsetEvent, bool turnEvent);
	// variant knob: 0 auto, 1..3
	int variant(int knob) const;
};

} // namespace dsc

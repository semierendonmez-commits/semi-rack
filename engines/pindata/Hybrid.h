// Semi-rack addition (not in the plugin): PINDATA's processBlock (Source/PluginProcessor.cpp) as a
// JUCE-free engine, so the Rack / MetaModule module and the offline test drive the same code.
//
//   * the parameter tree becomes one plain Settings struct (defaults = the plugin's defaults);
//   * the host transport becomes bpm / ppq / playing handed in per block by the caller (the module
//     derives them from CLK or its own tempo);
//   * MIDI out becomes per-lane note state (noteOn / noteOffAt) the module turns into GATE / PITCH /
//     VEL; MIDI in becomes fireLane() requests (the module's FIRE input);
//   * the file loader becomes buildBlob() / textBlob() below (bytes, text, WAV audio decoded by the
//     caller; images are read as bytes);
//   * the visual events become counters and a short step path for the module's screens.
// Everything else (the reset points, swing, lane destinations, the data head, the three layers'
// clocks, the mix with DC block / limiter / clip) follows processBlock line by line.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "DataBlob.h"
#include "IkdEngine.h"
#include "IkdFactory.h"
#include "IngpEngine.h"
#include "JdatEngine.h"
#include "OriginalPresets.h"
#include "Pinball.h"

namespace pnd {

// ---------------------------------------------------------------- choice tables (Params.h)
enum Dest { dMidi, dIkdStep, dIkdMutate, dIngpTick, dIngpRule, dIngpSync, dJdatTick, dJdatClear,
            dDataStep, dDataJump, dDataRegion, dVisCut, dVisFlash, dAllTick, dNumDests };
inline const char* destName(int d) {
	static const char* const n[] = {"midi only", "ikd step", "ikd mutate", "ingp tick", "ingp rule", "ingp sync",
	                                 "jdat tick", "jdat clear", "data step", "data jump", "data region",
	                                 "vis cut", "vis flash", "all tick"};
	return n[std::clamp(d, 0, dNumDests - 1)];
}
constexpr double kDivBeats[] = {1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125};
constexpr int kAutoSteps[] = {0, 1, 2, 4, 8, 16, 32, 64};
constexpr int kLoopTicks[] = {0, 16, 32, 64, 128, 256};
constexpr uint64_t kStepBits[] = {1, 8, 64, 576, 2048, 8192};
constexpr int kDefaultDest[pinball::kLanes] = {dIkdStep, dIngpTick, dJdatTick, dIkdMutate, dIngpRule, dJdatClear, dDataStep, dVisCut};

inline float dbToGain(float db) { return db <= -59.9f ? 0.f : std::pow(10.f, db / 20.f); }

// ---------------------------------------------------------------- data (DataLoader.cpp without JUCE)
namespace pdata {

inline uint32_t nextSerial() {
	static std::atomic<uint32_t> serials{1};
	return serials.fetch_add(1);
}

// AUDIO: the caller decodes (mono, any rate); normalised, bits = sample above zero.
inline bool buildAudio(DataBlob& b, const std::vector<float>& mono, double rate, int channels) {
	if (mono.empty())
		return false;
	b.symbols = mono;
	float peak = 1.0e-6f;
	for (float s : b.symbols)
		peak = std::max(peak, std::fabs(s));
	for (auto& s : b.symbols)
		s /= peak;
	std::vector<uint8_t> bits(b.symbols.size());
	for (size_t i = 0; i < bits.size(); ++i)
		bits[i] = b.symbols[i] > 0.f ? 1 : 0;
	b.packBits(bits);
	b.used = ReadAs::Audio;
	b.audioRate = rate;
	b.audioChannels = channels;
	b.audioSeconds = rate > 0.0 ? (double) mono.size() / rate : 0.0;
	char buf[64];
	std::snprintf(buf, sizeof buf, "%.1f KHZ  %d CH  %.2f S", rate / 1000.0, channels, b.audioSeconds);
	b.kindNote = buf;
	return true;
}

inline bool isAudioType(const std::string& t) {
	return t == "WAV AUDIO" || t == "AIFF AUDIO" || t == "AIFF-C AUDIO" || t == "FLAC AUDIO" || t == "OGG STREAM" || t == "MP3 AUDIO";
}
inline bool isImageType(const std::string& t) { return t == "PNG IMAGE" || t == "JPEG IMAGE" || t == "GIF IMAGE"; }

// build(): bits / symbols / analysis from b.bytes. `audio` = the decoded samples when the caller
// could decode the file (WAV), else nullptr. Images are not decoded here: read as bytes.
inline void build(DataBlob& b, ReadAs requested, const std::vector<float>* audio = nullptr, double rate = 0.0, int channels = 0) {
	b.analyse();
	b.symbols.clear();
	b.kindNote.clear();
	b.imageW = b.imageH = b.rasterRows = 0;
	b.audioRate = b.audioSeconds = 0.0;
	b.audioChannels = 0;

	ReadAs mode = requested;
	if (mode == ReadAs::Auto) {
		if (isAudioType(b.typeName))
			mode = ReadAs::Audio;
		else if (isImageType(b.typeName))
			mode = ReadAs::Image;
		else if (b.typeName == "TEXT")
			mode = ReadAs::Text;
		else
			mode = ReadAs::Bytes;
	}
	if (mode == ReadAs::Audio && !(audio && buildAudio(b, *audio, rate, channels))) {
		b.kindNote = "NOT DECODABLE AS AUDIO - READ AS BYTES";
		mode = ReadAs::Bytes;
	}
	if (mode == ReadAs::Image) {
		b.kindNote = "IMAGE READ AS BYTES";
		mode = ReadAs::Bytes;
	}
	if (mode == ReadAs::Text) {
		b.bitsFromText();
		b.symbolsFromBytes();
		b.used = ReadAs::Text;
		if (b.kindNote.empty())
			b.kindNote = std::to_string(b.wordCount) + " WORDS  " + std::to_string(b.lineCount) + " LINES";
		if (b.nbits == 0) {
			b.bitsFromBytes();
			b.used = ReadAs::Bytes;
		}
	}
	else if (mode == ReadAs::Bytes) {
		b.bitsFromBytes();
		b.symbolsFromBytes();
		b.used = ReadAs::Bytes;
		if (b.kindNote.empty())
			b.kindNote = "8 BITS / BYTE  MSB FIRST";
	}
	b.serial = nextSerial();
}

inline void setBytes(DataBlob& b, const uint8_t* d, size_t n, size_t cap, const std::string& name, const std::string& path) {
	b.name = name;
	b.path = path;
	size_t dot = name.find_last_of('.');
	b.ext = dot == std::string::npos ? std::string() : name.substr(dot + 1);
	b.bytes.assign(d, d + std::min(n, cap));
	b.fileSize = n;
	b.truncated = n > cap;
}

inline void fromText(DataBlob& b, const char* text, const std::string& name, ReadAs requested) {
	std::string s = text ? text : "";
	if (s.empty())
		s = " ";
	setBytes(b, (const uint8_t*) s.data(), s.size(), s.size(), name, "");
	b.ext = "txt";
	build(b, requested == ReadAs::Auto ? ReadAs::Text : requested);
}

} // namespace pdata

using pdata::DataBlob;

// ---------------------------------------------------------------- settings
struct Settings {
	// global
	bool run = true;
	float masterDb = -3.f, lvlIkdDb = 0.f, lvlIngpDb = 0.f, lvlJdatDb = -3.f;
	bool clip = true, limit = true, dcBlock = true;
	int velDepth = 100;
	bool laneOut = true;               // "Output": lanes play their notes (GATE / PITCH / VEL)
	// pinball
	double mult = 1.0, swing = 0.0, patternBars = 1.0;
	int startStep = 1;
	std::array<int, pinball::kLanes> laneNote{}, laneDest{};
	std::array<bool, pinball::kLanes> laneChoke{};
	// IKD
	int ikdClk = 2, ikdDiv = 3, ikdAuto = 0, ikdChance = 50, ikdFeed = 2;
	float ikdSwing = 12.f, ikdWaveDb = 0.f, ikdSpecDb = 0.f, ikdSineDb = -6.f;
	bool ikdHold = false, ikdFeedSnd = false;
	// INGP
	int ingpClk = 2, ingpDiv = 3, ingpGens = 1, ingpWalk = 0, ingpFeed = 2;
	float ingpBpm = 140.f, ingpSmooth = 15.f, ingpTune = 0.f, ingpTilt = 0.f, ingpWidth = 1.f, ingpSwing = 0.f;
	bool ingpGen = true, ingpFeedBanks = false;
	// JDAT
	int jdatClk = 2, jdatDiv = 3, jdatClear = 40, jdatJump = 30, jdatInterp = 1, jdatWrite = 1, jdatFeed = 1, jdatLoop = 0;
	float jdatRate = 120.f;
	std::array<int, 4> jdatSrc{{3, 3, 3, 3}};
	std::array<float, 4> jdatLvlDb{{0.f, 0.f, 0.f, -6.f}};
	bool jdatLink = true, jdatHold = false;
	// data
	int readAs = 0, scan = 0, stepSize = 2;
	float scanRate = 2.f;

	Settings() {
		for (int l = 0; l < pinball::kLanes; ++l) {
			laneNote[(size_t) l] = 36 + l;
			laneDest[(size_t) l] = kDefaultDest[l];
			laneChoke[(size_t) l] = true;
		}
	}
};

struct Transport {
	bool playing = false;
	double bpm = 120.0;
	double ppq = 0.0;                  // at the first sample of the block
};

inline const char* factoryName(int i) {
	static const char* const n[] = {"pindata", "free originals", "markov drunk", "data rain", "region walk",
	                                "sine field", "barcode grid", "spectral pinball"};
	return n[std::clamp(i, 0, 7)];
}
constexpr int kNumFactory = 8;

// ---------------------------------------------------------------- the instrument
class Hybrid {
public:
	ryoz::SharedState ikdShared;
	ryoz::Engine ikd{ikdShared};
	ingp::Engine ingp;
	jdat::Engine jdat;
	pinball::Sequencer pinball;
	std::array<bool, ryoz::kNumVoices> ikdLocks{};

	// ---- telemetry (audio thread writes, the module copies into its snapshot) ----
	uint64_t head = 0;
	uint32_t stepFires = 0, laneHitsTotal = 0, ikdSteps = 0, ingpTicks = 0, jdatTicks = 0;
	uint32_t resets = 0, sceneCuts = 0, flashes = 0, dataMoves = 0, chokes = 0;
	std::array<uint32_t, pinball::kLanes> laneHits{};
	std::array<int, pinball::kLanes> laneVel{};
	int lastLane = -1;
	std::array<int8_t, 16> path{};    // last steps entered, oldest first
	int pathN = 0;
	int lastFrom = -1, lastTo = -1;    // states 0..16
	float rmsIkd = 0.f, rmsIngp = 0.f, rmsJdat = 0.f, rmsOut = 0.f, peakOut = 0.f;
	int ingpCaFlash = -1;
	uint32_t patternEvents = 0;        // pattern-length resets (not manual), for the BUS EVENT pulse

	// MIDI note state per lane ([makenote @repeatmode 1])
	std::array<int, pinball::kLanes> noteOn{};
	std::array<int64_t, pinball::kLanes> noteOffAt{};
	std::array<uint32_t, pinball::kLanes> noteSerial{};   // bumps on every note-on (retrigger)

	Hybrid() {
		noteOn.fill(-1);
		seed(0x5eed1234u);
	}

	void seed(uint32_t s) {
		pinball.seed(s);
		jdat.seed(s * 7u + 1u);
		laneRng.seed(s * 13u + 5u);
		fnRng.seed(s * 31u + 7u);
	}

	void prepare(double sampleRate) {
		sr = sampleRate > 0 ? sampleRate : 44100.0;
		ikd.prepare(sr);
		ingp.prepare(sr);
		jdat.prepare(sr);
		pinball.prepare(sr);
		noteOn.fill(-1);
		numPending = 0;
		wasPlaying = false;
		seenSerial = 0;      // feed the data again
		smInit = false;
	}

	// ---- requests (call on the audio thread, before process) ----
	void requestReset() { resetRequest = true; }
	void requestHalt() { haltRequest = true; }
	void requestJump(int step) { jumpRequest = step; }
	void requestLanes(uint32_t mask) { laneFireRequests |= mask; }
	void requestHead(uint64_t bits) { headRequest = (int64_t) bits; }
	void requestRefeed() { refeed = true; }
	void requestJdatClear() { jdatClearRequest = true; }

	// ---- functions menu (audio thread) ----
	enum class Fn { RandomizeTrigs, ClearSteps, ClearMatrix, ClearAll, MatrixChain, MatrixDrunk, MatrixRandom,
	                MatrixPingPong, MatrixJumps, RandomizeSteps };

	void pinballFunction(Fn f) {
		auto& r = fnRng;
		auto setRow = [this](int x, std::initializer_list<std::pair<int, float>> w) {
			for (int y = 0; y < pinball::kStates; ++y)
				pinball.matrix[(size_t) x][(size_t) y].store(0.f);
			for (auto& p : w)
				if (p.first >= 0 && p.first < pinball::kStates)
					pinball.matrix[(size_t) x][(size_t) p.first].store(p.second);
		};
		switch (f) {
			case Fn::RandomizeTrigs:
				for (auto& s : pinball.steps)
					s.trig = r.below(10);
				break;
			case Fn::ClearSteps:
				for (auto& s : pinball.steps)
					s = pinball::StepParams{};
				break;
			case Fn::ClearMatrix: pinball.clearMatrix(); break;
			case Fn::ClearAll:
				pinballFunction(Fn::ClearSteps);
				pinball.clearMatrix();
				break;
			case Fn::MatrixChain: pinball.setDefaultMatrix(); break;
			case Fn::MatrixDrunk:
				for (int x = 0; x < pinball::kStates; ++x) {
					const int s = std::max(1, x);
					setRow(x, {{s == 1 ? 2 : s - 1, 35.f}, {s + 1 > 16 ? 15 : s + 1, 65.f}, {s, x == 0 ? 0.f : 10.f}});
				}
				break;
			case Fn::MatrixPingPong:
				for (int x = 0; x < pinball::kStates; ++x)
					setRow(x, {{x < 16 ? x + 1 : 15, 100.f}});
				for (int x = 9; x < 16; ++x)
					pinball.matrix[(size_t) x][(size_t) (x - 1)].store(40.f);
				break;
			case Fn::MatrixJumps:
				for (int x = 0; x < pinball::kStates; ++x)
					setRow(x, {{x < 16 ? x + 1 : 1, 70.f}, {1 + r.below(16), 30.f}});
				break;
			case Fn::MatrixRandom:
				for (int x = 0; x < pinball::kStates; ++x) {
					for (int y = 0; y < pinball::kStates; ++y)
						pinball.matrix[(size_t) x][(size_t) y].store(0.f);
					const int k = 1 + r.below(3);
					for (int j = 0; j < k; ++j)
						pinball.matrix[(size_t) x][(size_t) (1 + r.below(16))].store((float) (20 + r.below(81)));
				}
				break;
			case Fn::RandomizeSteps:
				for (auto& s : pinball.steps) {
					s.velocity = 50 + r.below(78);
					s.trigProb = r.unit() < 0.6 ? 100 : 30 + r.below(70);
					s.duration = std::clamp(std::pow(2.0, r.below(5) - 1), 0.25, 64.0);
					s.delay = r.unit() < 0.75 ? 0.0 : r.below(4) * 0.5;
					s.delayProb = r.unit() < 0.5 ? 100 : 25 + r.below(75);
				}
				break;
		}
	}

	static uint64_t randomBits(pinball::Rand& r, int width, int n) {
		uint64_t w = 0;
		n = std::clamp(n, 0, width);
		int c = 0;
		while (c < n) {
			const uint64_t m = (uint64_t) 1 << r.below(width);
			if (!(w & m)) {
				w |= m;
				++c;
			}
		}
		return w;
	}
	static uint64_t randomDensity(pinball::Rand& r, int width, float density) {
		uint64_t w = 0;
		for (int i = 0; i < width; ++i)
			if (r.unit() < density)
				w |= (uint64_t) 1 << i;
		return w;
	}
	// n set bits spread evenly over `width` (a bar whose popcount is n)
	static uint64_t spreadBits(int width, int n) {
		uint64_t w = 0;
		n = std::clamp(n, 0, width);
		for (int k = 0; k < n; ++k)
			w |= (uint64_t) 1 << std::clamp((int) ((double) k * width / std::max(1, n)), 0, width - 1);
		return w;
	}

	enum class IkdRandom { Pattern, Sound, All };
	void ikdRandomise(IkdRandom what) {
		auto& r = fnRng;
		for (int v = 0; v < ryoz::kNumVoices; ++v) {
			if (ikdLocks[(size_t) v])
				continue;
			auto& vs = ikdShared.voices[(size_t) v];
			if (what != IkdRandom::Sound)
				vs.cells.store(randomDensity(r, ryoz::kCells, 0.15f + 0.45f * (float) r.unit()));
			if (what != IkdRandom::Pattern) {
				vs.pitch.store(randomBits(r, ryoz::kPitchBits, 2 + r.below(37)));
				vs.gain.store(randomBits(r, ryoz::kGainBits, 3 + r.below(14)));
				vs.decay.store(randomBits(r, ryoz::kDecayBits, r.below(25)));
			}
		}
	}
	void ikdMutate(int voice) {
		for (int v = 0; v < ryoz::kNumVoices; ++v)
			if ((voice < 0 && !ikdLocks[(size_t) v]) || v == voice)
				ikdShared.voices[(size_t) v].mutateRequests.fetch_add(1, std::memory_order_relaxed);
	}
	void ingpRandomRule() { ingp.rule.store(fnRng.next()); }
	void ingpRandomBanks() {
		auto& r = fnRng;
		const double root = 40.0 + r.unit() * 260.0;
		for (int v = 0; v < ingp::V; ++v) {
			const double hz = root * (v + 1) * (1.0 + (r.unit() - 0.5) * 0.02);
			ingp.freq[(size_t) v].store((float) ingp::Engine::hzToSlider(std::min(18000.0, hz)));
			ingp.amp[(size_t) v].store(std::clamp(0.55f / std::sqrt((float) v + 1.f) * (0.6f + 0.4f * (float) r.unit()), 0.03f, 0.6f));
			ingp.pan[(size_t) v].store(0.5f + ((float) r.unit() - 0.5f) * 0.8f);
		}
	}
	void ingpLoadOriginal(int index, Settings& s) {
		const auto& o = origpresets::all[std::clamp(index, 0, 3)];
		for (int v = 0; v < ingp::V; ++v) {
			ingp.freq[(size_t) v].store(o.freq[v]);
			ingp.amp[(size_t) v].store(o.amp[v]);
			ingp.pan[(size_t) v].store(o.pan[v]);
			ingp.len[(size_t) v].store(o.len[v]);
		}
		if (o.rule >= 0)
			ingp.rule.store((uint32_t) o.rule);
		s.ingpSmooth = o.smooth;
		s.ingpBpm = o.bpm;
	}
	void jdatRoll(float amount, Settings& s) {
		auto& r = fnRng;
		for (int i = 0; i < 4; ++i)
			if (r.unit() < amount * 0.7f)
				s.jdatSrc[(size_t) i] = 1 + r.below(3);
		s.jdatClear = std::clamp(s.jdatClear + (int) ((r.unit() - 0.5) * 80 * amount), 0, 100);
		s.jdatJump = std::clamp(s.jdatJump + (int) ((r.unit() - 0.5) * 80 * amount), 0, 100);
		if (r.unit() < amount)
			s.jdatInterp = r.below(3);
		if (r.unit() < amount * 0.5f)
			s.jdatWrite = r.below(4);
	}

	// The plugin's eight factory presets (loadFactory). Resets `s` to the defaults first, keeps the
	// data and read-as.
	void loadFactory(int index, Settings& s) {
		index = std::clamp(index, 0, kNumFactory - 1);
		const int readAs = s.readAs;
		s = Settings();
		s.readAs = readAs;

		for (int w = 0; w < ryoz::kMasterWords; ++w)
			ikdShared.master[(size_t) w].store(ryoz::factory::kMaster[w]);
		const auto baseMs = (float) ryoz::Engine::stepMsForMasterSum(ikdShared.masterSum());
		pinball::Rand rr;
		rr.seed((uint32_t) (index * 7919 + 13));
		for (int v = 0; v < ryoz::kNumVoices; ++v) {
			auto& vs = ikdShared.voices[(size_t) v];
			vs.pitch.store(ryoz::factory::kPitch[v]);
			vs.cells.store(ryoz::factory::kCells[v]);
			vs.gain.store(ryoz::factory::kGain[v]);
			vs.decay.store(ryoz::factory::kDecay[v]);
			vs.rule.store(ryoz::kVoiceSpecs[v].savedRule);
			vs.lastRule.store(ryoz::kVoiceSpecs[v].savedRule);
			vs.gateBaseMs.store(baseMs);
			vs.signs.store((uint64_t) rr.next() << 32 | rr.next());
		}
		ikdShared.loadSerial.fetch_add(1, std::memory_order_release);

		ingp.rule.store(ingp::kDefaultRule);
		ingpLoadOriginal(1, s);
		ingp.muteMask.store(0);

		pinball.setDefaultMatrix();
		struct S { int trig, vel, prob; float dur, dly; int dprob; };
		auto setSteps = [this](std::initializer_list<S> list) {
			int k = 0;
			for (auto& x : list) {
				auto& p = pinball.steps[(size_t) k];
				p.trig = x.trig;
				p.velocity = x.vel;
				p.trigProb = x.prob;
				p.duration = x.dur;
				p.delay = x.dly;
				p.delayProb = x.dprob;
				if (++k >= pinball::kSteps)
					break;
			}
		};
		for (auto& p : pinball.steps)
			p = pinball::StepParams{};
		auto w = [this](int x, int y, float v) { pinball.matrix[(size_t) x][(size_t) y].store(v); };

		switch (index) {
			case 0:
			default:
				setSteps({{1, 112, 100, 2, 0, 100}, {3, 90, 100, 1, 0, 100}, {2, 100, 100, 4, 0, 100}, {1, 80, 70, 1, 0, 100},
				          {3, 70, 100, 1, 0.5f, 50}, {5, 100, 60, 2, 0, 100}, {2, 96, 100, 2, 1, 40}, {1, 104, 100, 2, 0, 100},
				          {3, 120, 100, 1, 0, 100}, {4, 100, 50, 1, 0, 100}, {2, 88, 100, 8, 0, 100}, {6, 100, 30, 1, 0, 100},
				          {1, 100, 100, 3, 0, 100}, {9, 100, 100, 1, 0, 100}, {7, 100, 100, 1, 0, 100}, {8, 100, 100, 1, 0, 100}});
				w(4, 9, 40); w(8, 1, 30); w(12, 5, 35); w(16, 1, 60); w(16, 9, 40);
				s.patternBars = 4;
				s.swing = 0.1;
				break;
			case 1:
				s.ikdClk = 0; s.ingpClk = 0; s.jdatClk = 0;
				for (auto& d : s.laneDest)
					d = dMidi;
				s.ikdFeed = 0; s.ingpFeed = 1; s.jdatWrite = 0;
				setSteps({{1, 100, 100, 1, 0, 100}, {0, 100, 100, 1, 0, 100}, {2, 100, 100, 1, 0, 100}, {0, 100, 100, 1, 0, 100},
				          {1, 100, 100, 1, 0, 100}, {3, 100, 100, 1, 0, 100}, {2, 100, 100, 1, 0, 100}, {0, 100, 100, 1, 0, 100},
				          {1, 100, 100, 1, 0, 100}, {0, 100, 100, 1, 0, 100}, {2, 100, 100, 1, 0, 100}, {4, 100, 100, 1, 0, 100},
				          {1, 100, 100, 1, 0, 100}, {0, 100, 100, 1, 0, 100}, {2, 100, 100, 1, 0, 100}, {9, 100, 100, 1, 0, 100}});
				break;
			case 2:
				pinballFunction(Fn::MatrixDrunk);
				setSteps({{1, 110, 100, 1, 0, 100}, {3, 80, 90, 1, 0, 100}, {2, 100, 100, 3, 0, 100}, {3, 70, 80, 1, 0.5f, 60},
				          {1, 90, 100, 1, 0, 100}, {5, 100, 70, 1, 0, 100}, {2, 90, 100, 2, 0, 100}, {3, 110, 100, 1, 0, 100},
				          {4, 100, 60, 1, 0, 100}, {1, 100, 100, 2, 0, 100}, {3, 80, 100, 1, 0.25f, 50}, {2, 100, 100, 6, 0, 100},
				          {7, 100, 50, 1, 0, 100}, {1, 120, 100, 1, 0, 100}, {6, 100, 40, 1, 0, 100}, {8, 100, 100, 1, 0, 100}});
				s.patternBars = 8;
				s.swing = 0.2;
				break;
			case 3:
				s.scan = 2; s.scanRate = 6.f; s.jdatWrite = 3;
				s.jdatClk = 1; s.ingpClk = 1; s.ingpSmooth = 4;
				s.ingpGens = 3; s.ingpWalk = 1;
				setSteps({{1, 120, 100, 1, 0, 100}, {0, 100, 100, 1, 0, 100}, {1, 70, 60, 1, 0, 100}, {4, 100, 50, 1, 0, 100},
				          {1, 100, 100, 1, 0, 100}, {0, 100, 100, 1, 0, 100}, {8, 100, 40, 1, 0, 100}, {1, 90, 100, 1, 0.5f, 50},
				          {1, 120, 100, 1, 0, 100}, {0, 100, 100, 1, 0, 100}, {1, 70, 60, 1, 0, 100}, {4, 100, 50, 1, 0, 100},
				          {1, 100, 100, 1, 0, 100}, {9, 100, 100, 1, 0, 100}, {1, 90, 100, 2, 0, 100}, {8, 100, 100, 1, 0, 100}});
				break;
			case 4:
				s.scan = 3;
				pinballFunction(Fn::MatrixJumps);
				setSteps({{1, 110, 100, 2, 0, 100}, {2, 90, 100, 2, 0, 100}, {3, 100, 100, 1, 0, 100}, {1, 80, 100, 1, 0, 100},
				          {2, 70, 100, 4, 0, 100}, {3, 100, 100, 1, 0, 100}, {1, 96, 100, 2, 0.5f, 50}, {5, 104, 100, 1, 0, 100},
				          {2, 120, 100, 2, 0, 100}, {3, 100, 100, 1, 0, 100}, {1, 88, 100, 2, 0, 100}, {2, 100, 100, 1, 0, 100},
				          {3, 100, 100, 3, 0, 100}, {1, 100, 100, 1, 0, 100}, {9, 100, 100, 1, 0, 100}, {8, 100, 100, 1, 0, 100}});
				s.patternBars = 16;
				break;
			case 5:
				s.ikdWaveDb = -60.f; s.ikdSpecDb = -60.f; s.ikdSineDb = 0.f;
				s.ingpSmooth = 350.f; s.ingpWidth = 1.6f; s.lvlJdatDb = -20.f;
				s.ingpGens = 2;
				setSteps({{2, 100, 100, 8, 0, 100}, {0, 100, 100, 1, 0, 100}, {1, 70, 100, 6, 0, 100}, {0, 100, 100, 1, 0, 100},
				          {2, 90, 100, 12, 0, 100}, {5, 100, 50, 1, 0, 100}, {1, 60, 100, 4, 1, 50}, {0, 100, 100, 1, 0, 100},
				          {2, 110, 100, 16, 0, 100}, {0, 100, 100, 1, 0, 100}, {1, 80, 100, 8, 0, 100}, {0, 100, 100, 1, 0, 100},
				          {2, 100, 100, 6, 0, 100}, {7, 100, 60, 1, 0, 100}, {1, 90, 100, 4, 0, 100}, {8, 100, 100, 1, 0, 100}});
				s.mult = 2.0;
				s.patternBars = 8;
				break;
			case 6:
				s.ikdClk = 1; s.ingpClk = 1; s.jdatClk = 1;
				s.ikdSwing = 0.f;
				s.laneDest[0] = dVisFlash; s.laneDest[1] = dDataStep; s.laneDest[2] = dIkdMutate; s.laneDest[3] = dVisCut;
				setSteps({{1, 127, 100, 1, 0, 100}, {0, 100, 100, 1, 0, 100}, {0, 100, 100, 1, 0, 100}, {3, 100, 100, 1, 0, 100},
				          {0, 100, 100, 1, 0, 100}, {0, 100, 100, 1, 0, 100}, {2, 100, 100, 1, 0, 100}, {0, 100, 100, 1, 0, 100},
				          {1, 90, 100, 1, 0, 100}, {0, 100, 100, 1, 0, 100}, {3, 100, 100, 1, 0, 100}, {0, 100, 100, 1, 0, 100},
				          {0, 100, 100, 1, 0, 100}, {2, 100, 100, 1, 0, 100}, {0, 100, 100, 1, 0, 100}, {4, 100, 100, 1, 0, 100}});
				s.patternBars = 2;
				break;
			case 7:
				s.ikdWaveDb = -60.f; s.ikdSineDb = -60.f; s.ikdSpecDb = 3.f;
				s.lvlIngpDb = -12.f;
				s.ikdAuto = 3; s.ikdChance = 70;
				pinballFunction(Fn::MatrixRandom);
				pinballFunction(Fn::RandomizeTrigs);
				break;
		}
		refeed = true;
	}

	// ---- one block ----
	void process(const DataBlob* blob, float* outL, float* outR, int n, const Settings& s, const Transport& tr) {
		if (n <= 0)
			return;
		n = std::min(n, kMaxBlock);
		Block blk;
		blk.n = n;
		current = blob;

		const bool run = s.run;
		const double bpm = std::max(1.0, tr.bpm);
		const bool playing = tr.playing;
		const double ppq = tr.ppq;
		const double ppqPerSample = bpm / 60.0 / sr;
		const double ppqEnd = ppq + ppqPerSample * n;
		blk.sixteenth = 60.0 / bpm / 4.0 * sr;

		// ---- data: a new blob?
		if (blob != nullptr && blob->serial != seenSerial) {
			seenSerial = blob->serial;
			head = blob->firstLively();
			headFrac = 0.0;
			lastFedRow = ~0ull;
			symbols.blob = blob;
			symbols.pos = 0;
			feedEngines(true, s);
			dataMoves++;
		}
		if (headRequest >= 0) {
			const int64_t hr = headRequest;
			headRequest = -1;
			headFrac = 0.0;
			moveHead((uint64_t) hr, true, s);
			if (s.ikdFeed == 1 || s.ingpFeed == 1)
				feedEngines(true, s);
		}
		if (refeed) {
			refeed = false;
			feedEngines(true, s);
		}
		if (jdatClearRequest) {
			jdatClearRequest = false;
			jdat.clearBuffers();
		}

		// ---- FIRE input / lane requests ("MIDI in fires lanes" + audition)
		if (laneFireRequests != 0) {
			const uint32_t req = laneFireRequests;
			laneFireRequests = 0;
			for (int l = 0; l < pinball::kLanes; ++l)
				if (req & (1u << l))
					fireLane(l, 100, (float) (2.0 * blk.sixteenth / sr * 1000.0), 0, true, blk, s);
		}

		// ---- pinball
		const double mult = s.mult;
		const int startStep = std::clamp(s.startStep, 1, 16);
		const double patternBeats = std::max(0.5, s.patternBars) * 4.0;
		pinball::EventList<512> pev;

		std::array<int, 64> resetsAt{};
		int nResets = 0;
		if (playing && !wasPlaying)
			resetsAt[(size_t) nResets++] = 0;
		if (playing) {
			for (int64_t k = (int64_t) std::floor(ppq / patternBeats) + 1; (double) k * patternBeats < ppqEnd && nResets < 63; ++k) {
				const int off = std::clamp((int) std::floor(((double) k * patternBeats - ppq) / ppqPerSample), 0, n - 1);
				if (nResets == 0 || resetsAt[(size_t) nResets - 1] != off) {
					resetsAt[(size_t) nResets++] = off;
					patternEvents++;
				}
			}
			if (wasPlaying && std::fabs(std::fmod(ppq, patternBeats)) < 1.0e-9 && (nResets == 0 || resetsAt[0] != 0)) {
				for (int i = nResets; i > 0; --i)
					resetsAt[(size_t) i] = resetsAt[(size_t) i - 1];
				resetsAt[0] = 0;
				++nResets;
				patternEvents++;
			}
		}
		const bool manualReset = resetRequest;
		const bool manualHalt = haltRequest;
		resetRequest = haltRequest = false;

		if (!playing && wasPlaying) {
			pinball::EventList<512> tmp;
			pinball.halt(0, tmp);
			allNotesOff();
			numPending = 0;
		}

		pinball.setTiming(blk.sixteenth, mult);
		if (manualHalt)
			pinball.halt(0, pev);
		if (manualReset)
			pinball.reset(0, startStep, pev);
		if (jumpRequest >= 0) {
			pinball.jump(jumpRequest, 0, pev);
			jumpRequest = -1;
		}

		if (playing || manualReset || pinball.currentStep.load() >= 0) {
			int pos = 0, ri = 0;
			while (pos < n) {
				const int segEnd = ri < nResets ? resetsAt[(size_t) ri] : n;
				if (segEnd > pos) {
					const int before = pev.count;
					pinball.process(segEnd - pos, blk.sixteenth, mult, pev);
					for (int i = before; i < pev.count; ++i)
						pev.items[(size_t) i].offset += pos;
					pos = segEnd;
				}
				if (ri < nResets && resetsAt[(size_t) ri] == pos) {
					pinball.reset(pos, startStep, pev);
					++ri;
				}
				else if (segEnd >= n)
					break;
			}
		}

		const double swing = s.swing;
		const int scan = s.scan;
		for (int i = 0; i < pev.count; ++i) {
			const auto& e = pev.items[(size_t) i];
			switch (e.type) {
				case pinball::Event::Enter: {
					const int to = e.step + 1;
					lastFrom = lastTo;
					lastTo = to;
					pushPath(e.step);
					if (scan == 1)
						moveHead(head + kStepBits[std::clamp(s.stepSize, 0, 5)], true, s);
					if (scan == 3 && blob != nullptr && blob->nbits > 0)
						moveHead((blob->nbits / 16u * (uint64_t) std::max(0, (int) e.step)) & ~(uint64_t) 63, true, s);
					break;
				}
				case pinball::Event::Fire: stepFires++; break;
				case pinball::Event::Trig: {
					const double beat = ppq + ppqPerSample * e.offset;
					const auto g = (int64_t) std::floor(beat * 4.0 / mult + 1.0e-9);
					int64_t at = sampleClock + e.offset;
					if ((g & 1) && swing > 0.0)
						at += (int64_t) (swing * 0.5 * blk.sixteenth * mult);
					if (numPending < (int) pending.size())
						pending[(size_t) numPending++] = {at, e.lane, e.velocity, e.durMs, true};
					break;
				}
				case pinball::Event::Choke:
					for (int l = 0; l < pinball::kLanes; ++l)
						if ((e.chokeMask & (1u << l)) && s.laneChoke[(size_t) l] && noteOn[(size_t) l] >= 0)
							noteOn[(size_t) l] = -1;
					chokes++;
					break;
				case pinball::Event::Reset:
					allNotesOff();
					numPending = 0;
					resets++;
					lastTo = 0;
					break;
				case pinball::Event::Halt:
				case pinball::Event::Idle: break;
			}
		}

		for (int i = 0; i < numPending;) {
			auto& pf = pending[(size_t) i];
			if (pf.at < sampleClock + n) {
				fireLane(pf.lane, pf.velocity, pf.durMs, (int) std::clamp<int64_t>(pf.at - sampleClock, 0, n - 1), pf.midi, blk, s);
				pending[(size_t) i] = pending[(size_t) --numPending];
			}
			else
				++i;
		}

		// ---- data scan RUN
		if (scan == 2 && playing && blob != nullptr && blob->nbits > 0) {
			headFrac += s.scanRate * 64.0 * n / sr;
			if (headFrac >= 1.0) {
				const auto adv = (uint64_t) headFrac;
				headFrac -= (double) adv;
				const uint64_t nh = (head + adv) % blob->nbits;
				head = nh;
				if (nh / 64u != lastFedRow) {
					lastFedRow = nh / 64u;
					moveHead(nh, true, s);
				}
			}
		}

		// ---- INGP clock
		const int ingpClk = s.ingpClk;
		if (ingpClk != 2 && run) {
			const bool grid = ingpClk == 1;
			if (!grid || playing) {
				const double ibpm = grid ? bpm : (double) s.ingpBpm;
				const double ippqPerSample = ibpm / 60.0 / sr;
				const double ip0 = grid ? ppq : ingpFreePpq;
				const double ip1 = ip0 + ippqPerSample * n;
				const double tl = kDivBeats[std::clamp(s.ingpDiv, 0, 5)];
				const double isw = s.ingpSwing;
				for (int64_t k = (int64_t) std::floor(ip0 / tl) - 1; (double) k * tl <= ip1 + tl; ++k) {
					const double t = (double) k * tl + ((k & 1) ? isw * tl : 0.0);
					if (t < ip0 - 1e-9 || t >= ip1 - 1e-12)
						continue;
					blk.addIngp(std::clamp((int) std::floor((t - ip0) / ippqPerSample), 0, n - 1), 0, 1.f, -1.f, 0);
				}
				if (!grid)
					ingpFreePpq = ip1;
			}
		}
		if (ingpCloseAt >= 0 && ingpCloseAt < sampleClock + n) {
			blk.addIngp((int) std::clamp<int64_t>(ingpCloseAt - sampleClock, 0, n - 1), 3, 1.f, 0.f, 0);
			ingpCloseAt = -1;
		}

		// ---- render
		float* ikL = mix[0];
		float* ikR = mix[1];
		float* igL = mix[2];
		float* igR = mix[3];
		float* jdL = mix[4];
		float* jdR = mix[5];
		std::fill(igL, igL + n, 0.f);
		std::fill(igR, igR + n, 0.f);

		auto sortByOffset = [](auto* a, int cnt) {
			for (int i = 1; i < cnt; ++i)
				for (int j = i; j > 0 && a[j].offset < a[j - 1].offset; --j)
					std::swap(a[j], a[j - 1]);
		};
		sortByOffset(blk.ikdExt.data(), blk.nIkd);
		sortByOffset(blk.ingpEv.data(), blk.nIngp);
		sortByOffset(blk.jdatExt.data(), blk.nJdat);

		// IKD
		{
			ryoz::Engine::Controls c;
			const int clk = s.ikdClk;
			c.run = run;
			c.hostSync = clk == 1;
			c.external = clk == 2;
			c.divBeats = kDivBeats[std::clamp(s.ikdDiv, 0, 5)];
			c.swing = s.ikdSwing * 0.01;
			c.clip = false;
			c.hold = s.ikdHold;
			c.autoEvery = kAutoSteps[std::clamp(s.ikdAuto, 0, 7)];
			c.autoChance = s.ikdChance * 0.01f;
			const float g[3] = {dbToGain(s.ikdWaveDb), dbToGain(s.ikdSpecDb), dbToGain(s.ikdSineDb)};
			for (int v = 0; v < ryoz::kNumVoices; ++v) {
				c.voiceGain[(size_t) v] = g[v / 3];
				ikd.setLocked(v, ikdLocks[(size_t) v]);
			}
			c.ext = blk.ikdExt.data();
			c.numExt = blk.nIkd;
			ryoz::Engine::HostInfo h;
			h.valid = true;
			h.playing = playing;
			h.bpm = bpm;
			h.ppq = ppq;
			const int stepBefore = ikdShared.step.load();
			ikd.process(ikL, ikR, n, h, c);
			if (ikdShared.step.load() != stepBefore)
				ikdSteps++;
		}

		// INGP (segments between its events)
		{
			ingp::Engine::RenderParams rp;
			rp.smoothMs = s.ingpSmooth;
			rp.tuneRatio = std::pow(2.0, s.ingpTune / 12.0);
			rp.width = s.ingpWidth;
			rp.tilt = s.ingpTilt;
			rp.gain = 1.f;
			const int gens = s.ingpGen ? s.ingpGens : 0;
			const auto walk = (ingp::WalkArea) std::clamp(s.ingpWalk, 0, 1);
			int done = 0;
			for (int i = 0; i < blk.nIngp; ++i) {
				const auto& e = blk.ingpEv[(size_t) i];
				if (e.offset > done) {
					ingp.render(igL + done, igR + done, e.offset - done, rp);
					done = e.offset;
				}
				switch (e.kind) {
					case 0: {
						const auto res = ingp.tick(gens, walk, ++ingpTickIndex);
						ingp.startGateRamps(res.gatesOn, rp.smoothMs, e.accent);
						if (res.caIndex >= 0)
							ingpCaFlash = res.caIndex;
						ingpTicks++;
						if (e.durMs > 0.f)
							ingpCloseAt = sampleClock + e.offset + (int64_t) (e.durMs * 0.001 * sr);
						break;
					}
					case 1:
						ingp.walkCursor(e.moves, walk, ++ingpTickIndex);
						ingpCaFlash = ingp.cursorIdx.load();
						break;
					case 2: ingp.syncReset(); break;
					case 3:
						if (ingpClk == 2)
							ingp.closeGates(rp.smoothMs);
						break;
					default: break;
				}
			}
			if (done < n)
				ingp.render(igL + done, igR + done, n - done, rp);
		}

		// JDAT
		{
			jdat::Params jp;
			const int clk = s.jdatClk;
			jp.run = run;
			jp.sync = clk == 1;
			jp.external = clk == 2;
			jp.ext = blk.jdatExt.data();
			jp.numExt = blk.nJdat;
			jp.rateMs = s.jdatRate;
			jp.divBeats = kDivBeats[std::clamp(s.jdatDiv, 0, 5)];
			for (int i = 0; i < 4; ++i) {
				jp.src[(size_t) i] = std::clamp(s.jdatSrc[(size_t) i], 0, 3);
				jp.level[(size_t) i] = dbToGain(s.jdatLvlDb[(size_t) i]);
			}
			if (s.jdatLink)
				jp.src[2] = jp.src[0];
			jp.master = 1.f;
			jp.clip = false;
			jp.clearProb = s.jdatClear;
			jp.jumpProb = s.jdatJump;
			jp.hold = s.jdatHold;
			jp.interp = std::clamp(s.jdatInterp, 0, 2);
			jp.loopTicks = kLoopTicks[std::clamp(s.jdatLoop, 0, 5)];
			const int wm = s.jdatWrite;
			jp.writeMode = wm == 0 ? jdat::writeRandom : (wm == 2 ? jdat::writeDataScatter : jdat::writeDataSeq);
			symbols.trit = wm != 3;
			if (symbols.blob != blob)
				symbols.blob = blob;
			jp.symbols = &symbols;
			jdat::Transport t;
			t.valid = true;
			t.playing = playing;
			t.ppq = ppq;
			t.bpm = bpm;
			const auto ticksBefore = jdat.tickCount.load();
			jdat.process(jdL, jdR, n, jp, t, nullptr);
			if (jdat.tickCount.load() != ticksBefore)
				jdatTicks++;
		}

		// ---- mix
		const float tIk = dbToGain(s.lvlIkdDb), tIg = dbToGain(s.lvlIngpDb), tJd = dbToGain(s.lvlJdatDb), tM = dbToGain(s.masterDb);
		if (!smInit) {
			smIk = tIk; smIg = tIg; smJd = tJd; smM = tM;
			smInit = true;
		}
		const float smK = 1.f - std::exp(-1.f / (0.01f * (float) sr));   // ~30 ms to settle
		const float dcR = (float) std::exp(-2.0 * 3.14159265358979323846 * 8.0 / sr);
		const float rel = (float) std::exp(-1.0 / (0.12 * sr));
		double sIk = 0, sIg = 0, sJd = 0, sOut = 0;
		float pk = 0.f;
		for (int i = 0; i < n; ++i) {
			smIk += (tIk - smIk) * smK;
			smIg += (tIg - smIg) * smK;
			smJd += (tJd - smJd) * smK;
			smM += (tM - smM) * smK;
			const float il = ikL[i] * smIk, ir = ikR[i] * smIk, gl = igL[i] * smIg, gr = igR[i] * smIg, jl = jdL[i] * smJd, jr = jdR[i] * smJd;
			sIk += il * il + ir * ir;
			sIg += gl * gl + gr * gr;
			sJd += jl * jl + jr * jr;
			float l = (il + gl + jl) * smM, r = (ir + gr + jr) * smM;
			if (s.dcBlock) {
				const float yl = l - dcX[0] + dcR * dcY[0], yr = r - dcX[1] + dcR * dcY[1];
				dcX[0] = l;
				dcX[1] = r;
				dcY[0] = yl;
				dcY[1] = yr;
				l = yl;
				r = yr;
			}
			if (s.limit) {
				const float pkNow = std::max(std::fabs(l), std::fabs(r));
				const float want = pkNow > 0.98f ? 0.98f / pkNow : 1.f;
				limEnv = want < limEnv ? want : want + (limEnv - want) * rel;
				l *= limEnv;
				r *= limEnv;
			}
			if (s.clip) {
				l = std::clamp(l, -1.f, 1.f);
				r = std::clamp(r, -1.f, 1.f);
			}
			if (!std::isfinite(l) || !std::isfinite(r))
				l = r = 0.f;
			sOut += l * l + r * r;
			pk = std::max(pk, std::max(std::fabs(l), std::fabs(r)));
			outL[i] = l;
			outR[i] = r;
		}
		auto smooth = [n](float& a, double sum) {
			const float v = (float) std::sqrt(sum / std::max(1, 2 * n));
			a = std::max(v, a * 0.85f);
		};
		smooth(rmsIkd, sIk);
		smooth(rmsIngp, sIg);
		smooth(rmsJdat, sJd);
		smooth(rmsOut, sOut);
		peakOut = std::max(pk, peakOut * 0.9f);

		// note-offs, "Output" off
		if (!s.laneOut)
			allNotesOff();
		for (int l = 0; l < pinball::kLanes; ++l)
			if (noteOn[(size_t) l] >= 0 && noteOffAt[(size_t) l] < sampleClock + n)
				noteOn[(size_t) l] = -1;

		wasPlaying = playing;
		sampleClock += n;
	}

	int64_t now() const { return sampleClock; }
	double sampleRate() const { return sr; }

	static constexpr int kMaxBlock = 256;

private:
	struct Block {
		int n = 0;
		double sixteenth = 1.0;
		std::array<ryoz::Engine::Controls::ExtStep, 128> ikdExt{};
		int nIkd = 0;
		struct IngpEv { int offset; int kind; float accent; float durMs; int moves; };   // kind 0 tick, 1 walk, 2 sync, 3 close
		std::array<IngpEv, 160> ingpEv{};
		int nIngp = 0;
		std::array<jdat::ExtTick, 128> jdatExt{};
		int nJdat = 0;
		void addIkd(int o, double gateMs, float acc) {
			if (nIkd < (int) ikdExt.size())
				ikdExt[(size_t) nIkd++] = {o, gateMs, acc};
		}
		void addIngp(int o, int kind, float acc, float dur, int moves) {
			if (nIngp < (int) ingpEv.size())
				ingpEv[(size_t) nIngp++] = {o, kind, acc, dur, moves};
		}
		void addJdat(int o, float acc) {
			if (nJdat < (int) jdatExt.size())
				jdatExt[(size_t) nJdat++] = {o, acc};
		}
	};

	struct Symbols : jdat::SymbolSource {
		const DataBlob* blob = nullptr;
		uint64_t pos = 0;
		bool trit = true;
		float next() noexcept override {
			if (blob == nullptr || blob->symbols.empty())
				return 0.f;
			const uint64_t i = pos++;
			if (!trit)
				return blob->symbolAt(i);
			if (blob->used == pdata::ReadAs::Audio || blob->used == pdata::ReadAs::Image) {
				const float v = blob->symbolAt(i);
				return v > 0.33f ? 1.f : (v < -0.33f ? -1.f : 0.f);
			}
			return (float) ((int) (blob->byteAt(i) % 3u) - 1);
		}
	} symbols;

	void pushPath(int step) {
		if (pathN < (int) path.size())
			path[(size_t) pathN++] = (int8_t) step;
		else {
			for (size_t k = 1; k < path.size(); ++k)
				path[k - 1] = path[k];
			path[path.size() - 1] = (int8_t) step;
		}
	}

	void allNotesOff() { noteOn.fill(-1); }

	float accentFor(int velocity, const Settings& s) const {
		const float depth = s.velDepth * 0.01f;
		return std::clamp(1.f + depth * ((float) velocity / 100.f - 1.f), 0.f, 1.27f);
	}

	void moveHead(uint64_t newHead, bool feed, const Settings& s) {
		const auto* b = current;
		if (b == nullptr || b->nbits == 0)
			return;
		head = newHead % b->nbits;
		dataMoves++;
		if (feed)
			feedEngines(false, s);
	}

	// Load (onLoad) or move: write the data under the read head into every layer whose feed asks for it.
	void feedEngines(bool onLoad, const Settings& s) {
		const auto* b = current;
		if (b == nullptr || b->nbits == 0)
			return;
		const uint64_t h = head;
		auto wants = [onLoad](int f) { return onLoad ? f >= 1 : f == 2; };

		if (wants(s.ikdFeed)) {
			for (int v = 0; v < ryoz::kNumVoices; ++v) {
				if (ikdLocks[(size_t) v])
					continue;
				auto& vs = ikdShared.voices[(size_t) v];
				vs.cells.store(b->word64(h + (uint64_t) v * 64u), std::memory_order_relaxed);
				if (s.ikdFeedSnd) {
					const uint64_t base = b->byteForBit(h) + 1152u + (uint64_t) v * 3u;
					vs.pitch.store(spreadBits(ryoz::kPitchBits, 4 + b->byteAt(base) * 30 / 255), std::memory_order_relaxed);
					vs.gain.store(spreadBits(ryoz::kGainBits, 2 + b->byteAt(base + 1) * 14 / 255), std::memory_order_relaxed);
					vs.decay.store(spreadBits(ryoz::kDecayBits, b->byteAt(base + 2) * 25 / 255), std::memory_order_relaxed);
				}
			}
		}
		if (wants(s.ingpFeed)) {
			for (int i = 0; i < ingp::N; ++i)
				ingp.setCell(i, b->bit(h + (uint64_t) i));
			if (s.ingpFeedBanks) {
				const uint64_t base = b->byteForBit(h) + 256u;
				for (int v = 0; v < ingp::V; ++v) {
					ingp.freq[(size_t) v].store(0.35f + 0.5f * b->byteAt(base + (uint64_t) v) / 255.f);
					ingp.amp[(size_t) v].store(0.08f + 0.35f * b->byteAt(base + 32u + (uint64_t) v) / 255.f);
					ingp.pan[(size_t) v].store(b->byteAt(base + 64u + (uint64_t) v) / 255.f);
				}
			}
		}
		if (wants(s.jdatFeed) && !b->symbols.empty()) {
			symbols.blob = b;
			symbols.pos = (uint64_t) ((double) h / (double) b->nbits * (double) b->symbols.size());
			symbols.trit = s.jdatWrite != 3;
			jdat.seedBuffers(symbols);
		}
	}

	void fireLane(int lane, int velocity, float durMs, int offset, bool midi, Block& blk, const Settings& s) {
		lane = std::clamp(lane, 0, pinball::kLanes - 1);
		laneHits[(size_t) lane]++;
		laneHitsTotal++;
		laneVel[(size_t) lane] = velocity;
		lastLane = lane;

		if (midi && s.laneOut && velocity > 0) {
			noteOn[(size_t) lane] = s.laneNote[(size_t) lane];
			noteSerial[(size_t) lane]++;
			noteOffAt[(size_t) lane] = sampleClock + offset + std::max<int64_t>(1, (int64_t) (durMs * 0.001 * sr));
		}

		const float acc = accentFor(velocity, s);
		const auto* b = current;
		switch (s.laneDest[(size_t) lane]) {
			case dIkdStep: blk.addIkd(offset, durMs, acc); break;
			case dIkdMutate: {
				int cand[ryoz::kNumVoices], nc = 0;
				for (int v = 0; v < ryoz::kNumVoices; ++v)
					if (!ikdLocks[(size_t) v])
						cand[nc++] = v;
				if (nc > 0)
					ikdShared.voices[(size_t) cand[laneRng.below(nc)]].mutateRequests.fetch_add(1, std::memory_order_relaxed);
				break;
			}
			case dIngpTick: blk.addIngp(offset, 0, acc, durMs, 0); break;
			case dIngpRule: blk.addIngp(offset, 1, acc, durMs, 1 + velocity / 24); break;
			case dIngpSync: blk.addIngp(offset, 2, acc, durMs, 0); break;
			case dJdatTick: blk.addJdat(offset, acc); break;
			case dJdatClear: jdat.clearBuffers(); break;
			case dDataStep: moveHead(head + kStepBits[std::clamp(s.stepSize, 0, 5)], true, s); break;
			case dDataJump:
				if (b != nullptr && b->nbits > 0)
					moveHead((uint64_t) (laneRng.unit() * (double) b->nbits) & ~(uint64_t) 63, true, s);
				break;
			case dDataRegion:
				if (b != nullptr && b->nbits > 0) {
					const int st = std::max(0, pinball.currentStep.load());
					moveHead((b->nbits / 16u * (uint64_t) st) & ~(uint64_t) 63, true, s);
				}
				break;
			case dVisCut: sceneCuts++; break;
			case dVisFlash: flashes++; break;
			case dAllTick:
				blk.addIkd(offset, durMs, acc);
				blk.addIngp(offset, 0, acc, durMs, 0);
				blk.addJdat(offset, acc);
				break;
			default: break;
		}
	}

	double sr = 44100.0;
	int64_t sampleClock = 0;
	const DataBlob* current = nullptr;
	uint32_t seenSerial = 0;
	double ingpFreePpq = 0.0;
	bool wasPlaying = false;
	double headFrac = 0.0;
	uint64_t lastFedRow = ~0ull;
	pinball::Rand laneRng, fnRng;

	bool resetRequest = false, haltRequest = false, refeed = false, jdatClearRequest = false;
	int jumpRequest = -1;
	uint32_t laneFireRequests = 0;
	int64_t headRequest = -1;

	struct PendingFire { int64_t at; int lane; int velocity; float durMs; bool midi; };
	std::array<PendingFire, 128> pending{};
	int numPending = 0;

	int64_t ingpCloseAt = -1;
	uint32_t ingpTickIndex = 0;

	float mix[6][kMaxBlock]{};
	bool smInit = false;
	float smIk = 1.f, smIg = 1.f, smJd = 1.f, smM = 1.f;
	float limEnv = 1.f;
	float dcX[2]{}, dcY[2]{};
};

} // namespace pnd

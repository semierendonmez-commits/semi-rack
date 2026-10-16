// Call & Response generator — C++ port of the JavaScript engine the Call & Response plug-in runs
// (semierendonmez-commits/callresponse-plugin engine/crg_engine.js 0.96.2 + the STEPS part of
// crg_extend.js), which is Side Brain's C&R Generator Max for Live device (sidebrain.net), itself a
// port of Nordl3's Call-Response-Generator (MIT, see LICENSE.Nordl3.txt). The original JS is kept in
// js/ for reference.
//
// Ported function by function, same names, same order of decisions: styles, motifs, the 38 answer
// strategies, articulation (gate / tie / slide / accent and their clustering), endings + RESOLVE,
// DENSITY (with the silenced-step memory), OCT JUMP, CONTRAST, tie / slide settling, mutation,
// phrase modes (pass plans, interleaves, forms with fixed letter variants, vary / drift), dice,
// undo, the 'acid303' preset, and the native compile of one phrase cycle into note rows (swing,
// note lengths, ties joined) that the plug-in's sequencer plays.
//
// Not ported (plug-in / Live features outside the generator): HARMONY (FOLLOW + CHORD OUT, the
// chord progression of crg_extend.js), the Custom form editor, Live scale follow, MIDI clip export,
// per-parameter locks, MIDI channels / routing.
//
// JS -> C++ notes: Math.random -> a seeded xorshift (Rng), so phrases are not bit-identical to the
// plug-in's (they never were: Math.random is unseeded) but every probability is the same. A JS
// null scale degree is NaN here; where the JS adds to a possibly-null degree (null -> 0 in JS
// arithmetic) the port does the same through num(). No exceptions, no iostreams; generation
// allocates (std::vector / std::map), so it must not run on an audio thread.
#pragma once

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace crg {

// ---------------------------------------------------------------------------------- lists (DATA)
extern const std::vector<std::string> kKeys, kScales, kStyles, kStrategies, kModes, kMotifs, kArticulation,
    kGateFeels, kConnections, kSlides, kAccents, kEndings, kRegisters, kDensityPresets, kContours, kRepetitions,
    kMutationAmounts, kMutationTargets, kNotePools, kOctaves, kRates, kSyncModes, kPairTimings;
extern const double kRateBeats[6];  // '1/4','1/8','1/8t','1/16','1/16t','1/32'
constexpr int kMinSteps = 2, kMaxSteps = 64;

int indexOf(const std::vector<std::string>& list, const char* name);

// ---------------------------------------------------------------------------------- parameters
struct Params {
	// enums: indices into the lists above
	int style, responseStrategy, motifType, phraseMode, endingBehaviour, key, scale, rootOctave;
	int steps;  // 2..64 (the value, not an index)
	int registerProfile, densityPreset, contourPreset, repetitionPreset, mutationAmount, mutationTarget;
	int notePoolMode, articulationProfile, gateFeel, connectionFeel, slideProfile, accentProfile;
	int rate, syncMode, newPairTiming;
	// numbers (the device's ranges)
	double noteLengthPct, baseVelocity, accentDepth, swing, octaveJump, mutateAll, density, contrast, resolve,
	    driftCall, driftResponse;

	static Params defaults();  // DEFAULTS
	bool operator==(const Params& o) const;
	bool operator!=(const Params& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------------- phrases
struct Was {
	bool has = false;
	double degree = NAN, gate = 0.82;
	bool accent = false, slide = false, tie = false;
};

struct Step {
	bool active = false;
	double degree = NAN;  // NaN = JS null
	double gate = 0.82;
	bool accent = false, slide = false, tie = false;
	int oct = 0;          // OCT JUMP offset (octaves)
	bool hasRank = false;
	double rank = 0.0;    // DENSITY tie-break
	Was was;              // DENSITY memory of a silenced step
};

struct Phrase {
	std::vector<Step> s;
	std::vector<int> silenced;  // JS phrase.__silenced
	size_t size() const { return s.size(); }
	Step& operator[](size_t i) { return s[i]; }
	const Step& operator[](size_t i) const { return s[i]; }
};

struct State {
	Params p = Params::defaults();
	Phrase call, response;
	std::map<std::string, Phrase> variants[2];  // per lane: letter -> fixed variant (forms)
	bool hasVariants = false;
	int cycle = 0;
	int passIdx = 0;
};

// One compiled phrase cycle (nativeCompile): note rows on a slot grid of total = passes * steps.
struct Row {
	int slot, lane, pitch, vel;  // lane 0 call, 1 response
	double durTicks, swingTicks; // 480 ticks per beat
};

struct PassView {
	Phrase call, response;
	Phrase variant;
	bool hasVariant = false;
	int lane = 0;  // lane of the pass (forms)
};

struct Compiled {
	State snap;          // the state this cycle was compiled from (+ the fixed form variants it made)
	std::vector<Row> rows;
	std::vector<PassView> views;
	State next;          // the state the following cycle starts from (evolving modes)
	bool evolves = false;
	int steps = 8, total = 16;
	double ticks = 120.0;
	int sync = 0;        // 0 restart, 1 timeline, 2 free
};

// ---------------------------------------------------------------------------------- RNG
struct Rng {
	uint64_t s;
	explicit Rng(uint64_t seed = 1) : s(seed * 0x9E3779B97F4A7C15ull + 0x2545F4914F6CDD1Dull) {
		if (!s)
			s = 1;
	}
	double random() {  // Math.random(): [0, 1)
		s ^= s >> 12;
		s ^= s << 25;
		s ^= s >> 27;
		return (double) ((s * 0x2545F4914F6CDD1Dull) >> 11) * (1.0 / 9007199254740992.0);
	}
};

// ---------------------------------------------------------------------------------- engine
class Engine {
public:
	explicit Engine(uint64_t seed = 1) : rng(seed) {}

	State S;

	// whole-state operations (the device's handlers)
	void generate(int which);  // 0 both, 1 call, 2 response (generate())
	void applyParams(const Params& np);  // param() / setnum(): dispatch on what changed
	void mutate(int which);    // action mutatecall / mutateresponse / mutateboth
	void shift(int which, int dir);
	void dice();               // performDice: rerolls the dice menus, new pair
	void preset303();          // PRESETS.acid303
	void keepRhythm();         // random('keeprhythm')
	void keepPitch();          // random('keeppitch')
	void keepMotif();          // random('keepmotif')
	void pushUndo();
	bool undo();
	bool canUndo() const { return !undoStack.empty(); }

	Compiled compile(const State& snap);  // nativeCompile

	// pitch of a step as it plays (foldPitch(degreeToMidi(...))), -1 = none
	int stepPitch(const Phrase& ph, int i, const Params& p) const;

	Rng rng;

private:
	struct Cfg;
	struct SP;
	struct Plan;

	Cfg cfg() const;
	double random() { return rng.random(); }
	bool chance(double p) { return random() < p; }
	double rand(const std::vector<double>& a);
	int randInt(const std::vector<int>& a);
	std::vector<int> pickN(std::vector<int> a, int n);

	SP styleProfile(const Cfg& c) const;
	std::vector<int> notePool(const Cfg& c);
	struct Motif {
		int len;
		std::vector<bool> rhythm;
		std::vector<double> contour;  // may be shorter than len (JS slice)
	};
	Motif motifTemplate(const Cfg& c);
	struct Art {
		double gate;
		bool accent, slide, tie;
	};
	Art articulationForStep(int idx, bool active, const Cfg& c, int motifPos);
	void clusterHolds(Phrase& ph, int only, bool runs);
	void postProcessArticulation(Phrase& ph, const Cfg& c, int only);
	void normalisePhrase(Phrase& ph);
	void applyEnding(Phrase& ph, const Cfg& c, bool response);
	double snapToPool(double deg, const std::vector<int>& pool, const Cfg& c, bool nearestOnly);
	Phrase buildCall(const Cfg& c);
	Phrase buildResponse(const Phrase& call, const Cfg& c);
	void enforceTies(Phrase& ph, const Cfg& c);
	void forgetDensityMemory(Phrase& ph);
	void restoreDensity(Phrase& ph, const Cfg& c);
	Phrase mutatePhrase(const Phrase& ph, const Cfg& c, std::string target, int stepsOverride);
	void settlePhrase(Phrase& ph, const Cfg& c);
	void applyOctaveJump();
	void applyDensity();
	void resizePhrases();
	void reapplyArticulation(int fields);
	void reapplyEnding();
	void applyArtPreset();
	void clearVariants();
	void randEnum(int& field, int n);

	Plan passPlan(int mode) const;
	int laneForStep(const Plan& plan, int pass, int i, int steps) const;
	const Phrase& variantFor(const std::string& key, int lane, const Cfg& c);
	void endOfPass(const Plan& plan, const Cfg& c);
	struct Ev {
		int pitch, vel;
		double dur;
	};
	bool stepEvent(const Phrase& ph, int i, const Cfg& c, Ev& out);
	double swingOffset(int i, double stepLen) const;

	struct Undo {
		Params p;
		std::vector<Step> call, response;
	};
	std::vector<Undo> undoStack;
};

} // namespace crg

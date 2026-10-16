// Call & Response generator — C++ port of crg_engine.js (see CrgEngine.h). Section and function
// names follow the JS file; comments marked JS: quote the original's reasoning where it matters.
#include "CrgEngine.h"

#include <algorithm>
#include <cstring>
#include <set>

namespace crg {

// =============================================================================================
// 1. Constants (verbatim from the JS)
// =============================================================================================

const std::vector<std::string> kKeys = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

namespace {
struct ScaleDef {
	const char* name;
	std::vector<int> iv;
};
const std::vector<ScaleDef>& scaleDefs() {
	static const std::vector<ScaleDef> d = {
	    {"Chromatic", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
	    {"Major / Ionian", {0, 2, 4, 5, 7, 9, 11}},
	    {"Natural Minor / Aeolian", {0, 2, 3, 5, 7, 8, 10}},
	    {"Harmonic Minor", {0, 2, 3, 5, 7, 8, 11}},
	    {"Melodic Minor", {0, 2, 3, 5, 7, 9, 11}},
	    {"Dorian", {0, 2, 3, 5, 7, 9, 10}},
	    {"Phrygian", {0, 1, 3, 5, 7, 8, 10}},
	    {"Mixolydian", {0, 2, 4, 5, 7, 9, 10}},
	    {"Locrian", {0, 1, 3, 5, 6, 8, 10}},
	    {"Lydian", {0, 2, 4, 6, 7, 9, 11}},
	    {"Lydian Dominant", {0, 2, 4, 6, 7, 9, 10}},
	    {"Aeolian b5", {0, 2, 3, 5, 6, 8, 10}},
	    {"Dorian b2", {0, 1, 3, 5, 7, 9, 10}},
	    {"Mixolydian b6", {0, 2, 4, 5, 7, 8, 10}},
	    {"Major Pentatonic", {0, 2, 4, 7, 9}},
	    {"Minor Pentatonic", {0, 3, 5, 7, 10}},
	    {"Blues Minor", {0, 3, 5, 6, 7, 10}},
	    {"Blues Major", {0, 2, 3, 4, 7, 9}},
	    {"Phrygian Dominant", {0, 1, 4, 5, 7, 8, 10}},
	    {"Double Harmonic", {0, 1, 4, 5, 7, 8, 11}},
	    {"Hungarian Minor", {0, 2, 3, 6, 7, 8, 11}},
	    {"Romanian Minor", {0, 2, 3, 6, 7, 9, 10}},
	    {"Whole Tone", {0, 2, 4, 6, 8, 10}},
	    {"Diminished Half-Whole", {0, 1, 3, 4, 6, 7, 9, 10}},
	    {"Diminished Whole-Half", {0, 2, 3, 5, 6, 8, 9, 11}},
	    {"Persian", {0, 1, 4, 5, 6, 8, 11}},
	    {"Arabic", {0, 2, 4, 5, 6, 8, 10}},
	    {"Hirajoshi", {0, 2, 3, 7, 8}},
	    {"In Sen", {0, 1, 5, 7, 10}},
	    {"Japanese Pentatonic", {0, 1, 5, 7, 8}},
	    {"Kumoi", {0, 2, 3, 7, 9}},
	    {"Pelog-ish", {0, 1, 3, 7, 8}},
	    {"Egyptian Pentatonic", {0, 2, 5, 7, 10}},
	    {"Minor Triad only", {0, 3, 7}},
	    {"Power Fifths", {0, 7}},
	    {"Root + b7", {0, 10}},
	    {"Root + 5th + b7", {0, 7, 10}},
	    {"Root + 2nd + 5th", {0, 2, 7}},
	    {"Minor 7 arpeggio pool", {0, 3, 7, 10}},
	    {"Dominant 7 arpeggio pool", {0, 4, 7, 10}},
	    {"Diminished 7 arpeggio pool", {0, 3, 6, 9}},
	};
	return d;
}
std::vector<std::string> scaleNames() {
	std::vector<std::string> n;
	for (auto& d : scaleDefs())
		n.push_back(d.name);
	return n;
}
} // namespace

const std::vector<std::string> kScales = scaleNames();
const std::vector<std::string> kStyles = {"Classic Acid", "Raw Acid", "Squelchy Acid", "Dark Acid", "Hypno Acid", "Sparse Acid", "Busy Acid", "Acid Stabs", "Acid Runner", "Acid Question/Answer", "Driving Techno", "Hypnotic Techno", "Minimal Techno", "Raw Warehouse", "Tool Techno", "Dub Techno", "Industrial Techno", "Percussive Techno", "Loop Techno", "Broken Techno", "Jackin House", "Deep House", "Minimal House", "Classic House", "Tech House", "Bass House", "Shuffly House", "Organ House-ish", "House Stabline", "House Bounce", "90s Rave", "Dark Rave", "Rave Stab", "Electro Pulse", "Breaky Electro", "Detroit-ish", "Proto-Trance", "Hooverish Riff", "Motif Study", "Rooted Groove", "Repetitive Hook", "Wide Answer", "Tight Answer", "Tension Builder", "Resolver", "Call Lead / Response Support"};
const std::vector<std::string> kStrategies = {"Same rhythm + resolve", "Same rhythm + new contour", "Motif + simplify", "Motif + extend", "Motif + truncate", "Echo", "Answer upward", "Answer downward", "Higher answer", "Lower answer", "Invert contour", "Mirror contour loosely", "Copy rhythm / change notes", "Copy notes / change rhythm", "Transpose motif up", "Transpose motif down", "Response resolves to root", "Response resolves to 5th", "Response resolves lower", "Response resolves harder", "Repeat then break", "Tighter answer", "Sparser answer", "Busier answer", "Accent the answer", "Slide the answer", "Staccato answer", "Legato answer", "Delayed answer", "Half-phrase answer", "Fragment reply", "Tail answer", "Opposite register", "Same motif darker", "Same motif brighter", "Question then closure", "Hook then fill", "Call dominant / response tonic"};
const std::vector<std::string> kModes = {"Call only", "Response only", "Call then response", "Response then call", "Alternate half bars", "Alternate half bars, response first", "Alternate 2-step cells", "Alternate 1-step cells", "Call, call, response", "Call, response, response", "Call, varied call, response", "Call, response, varied response", "A / B / A / C", "A / A' / B / B'", "Call with response fill", "Mutating call, stable response", "Stable call, mutating response", "Both mutate slowly", "Call sparse, response busy", "Call busy, response sparse"};
const std::vector<std::string> kMotifs = {"Pulse", "Offbeat", "Syncopated", "Push", "Sparse", "Dense", "Rest-heavy", "Four-on-floor companion", "Stabbed", "Rolling", "Repeated note", "Stepwise rise", "Stepwise fall", "Rise then drop", "Drop then rise", "Leap then settle", "Pivot note", "Two-note hook", "Three-note hook", "Arp fragment", "Acid nibble", "Techno cell", "House bounce", "Rave stab figure", "Drone anchor", "Pedal point motif", "Call tag", "Answer tag"};
const std::vector<std::string> kArticulation = {"Custom", "Clean", "Punchy", "Rolling", "Acidic", "Hypnotic", "Choppy", "Fluid", "Stabbed", "Tense", "Released"};
const std::vector<std::string> kGateFeels = {"Pluck", "Stab", "Tight", "Short", "Medium", "Long", "Full", "Mixed", "Mixed short", "Wide mix", "Random within bounds"};
const std::vector<std::string> kConnections = {"Detached", "Semi-detached", "Legato", "Tied bias", "Mixed ties", "Connected runs"};
const std::vector<std::string> kSlides = {"None", "Rare", "Occasional", "Acid moderate", "Acid heavy", "Phrase-end slides", "Upward slides bias", "Downward slides bias"};
const std::vector<std::string> kAccents = {"None", "Light", "Moderate", "Heavy", "Offbeat accents", "Syncopated accents", "Downbeat accents", "Motif accents", "Random accents within motif"};
const std::vector<std::string> kEndings = {"Resolve to root", "Resolve to 5th", "Leave hanging", "Rise into next phrase", "Fall into next phrase", "Hard stop", "Fade energy", "Strong cadence", "Weak cadence", "Ambiguous ending", "Response more resolved than call", "Response same tension as call", "Response more tense than call"};
const std::vector<std::string> kRegisters = {"Narrow register", "Medium register", "Wide register"};
const std::vector<std::string> kDensityPresets = {"Very sparse", "Sparse", "Medium", "Busy", "Dense", "Very dense"};
const std::vector<std::string> kContours = {"Flat", "Slight rise", "Slight fall", "Arch", "Inverted arch", "Wandering", "Stepwise", "Leapy"};
const std::vector<std::string> kRepetitions = {"Very repetitive", "Repetitive", "Balanced", "Varied", "Highly varied"};
const std::vector<std::string> kMutationAmounts = {"Tiny", "Small", "Medium", "Strong", "Brutal"};
const std::vector<std::string> kMutationTargets = {"Rhythm only", "Pitch only", "Articulation only", "Ending only", "Motif only", "Whole phrase", "Response only", "Call only", "Call + response linked"};
const std::vector<std::string> kNotePools = {"Strict note pool", "Pool with spice notes", "Pool expands on response", "Pool contracts on response", "Root-heavy", "Root + 5th heavy", "Triad pool", "Seventh pool", "Scale-wide"};
const std::vector<std::string> kOctaves = {"-1", "0", "1", "2", "3", "4", "5"};
const std::vector<std::string> kRates = {"1/4", "1/8", "1/8t", "1/16", "1/16t", "1/32"};
const double kRateBeats[6] = {1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125};
const std::vector<std::string> kSyncModes = {"Restart", "Timeline", "Free"};
const std::vector<std::string> kPairTimings = {"Now", "Next Phrase"};

int indexOf(const std::vector<std::string>& list, const char* name) {
	for (size_t i = 0; i < list.size(); i++)
		if (list[i] == name)
			return (int) i;
	return 0;
}

Params Params::defaults() {
	Params p;
	p.style = indexOf(kStyles, "Motif Study");
	p.responseStrategy = indexOf(kStrategies, "Higher answer");
	p.motifType = indexOf(kMotifs, "Acid nibble");
	p.phraseMode = indexOf(kModes, "Call with response fill");
	p.endingBehaviour = indexOf(kEndings, "Resolve to root");
	p.key = indexOf(kKeys, "A");
	p.scale = indexOf(kScales, "Minor Pentatonic");
	p.rootOctave = indexOf(kOctaves, "1");
	p.steps = 8;
	p.registerProfile = indexOf(kRegisters, "Medium register");
	p.densityPreset = indexOf(kDensityPresets, "Medium");
	p.contourPreset = indexOf(kContours, "Slight rise");
	p.repetitionPreset = indexOf(kRepetitions, "Very repetitive");
	p.mutationAmount = indexOf(kMutationAmounts, "Small");
	p.mutationTarget = indexOf(kMutationTargets, "Call + response linked");
	p.notePoolMode = indexOf(kNotePools, "Root-heavy");
	p.articulationProfile = indexOf(kArticulation, "Custom");
	p.gateFeel = indexOf(kGateFeels, "Wide mix");
	p.connectionFeel = indexOf(kConnections, "Mixed ties");
	p.slideProfile = indexOf(kSlides, "Rare");
	p.accentProfile = indexOf(kAccents, "Moderate");
	p.rate = indexOf(kRates, "1/16");
	p.syncMode = indexOf(kSyncModes, "Restart");
	p.newPairTiming = indexOf(kPairTimings, "Now");
	p.noteLengthPct = 100;
	p.baseVelocity = 64;
	p.accentDepth = 65;
	p.swing = 5;
	p.octaveJump = 0;
	p.mutateAll = 0;
	p.density = 52;
	p.contrast = 50;
	p.resolve = 100;
	p.driftCall = 0;
	p.driftResponse = 0;
	return p;
}

bool Params::operator==(const Params& o) const {
	return style == o.style && responseStrategy == o.responseStrategy && motifType == o.motifType &&
	       phraseMode == o.phraseMode && endingBehaviour == o.endingBehaviour && key == o.key && scale == o.scale &&
	       rootOctave == o.rootOctave && steps == o.steps && registerProfile == o.registerProfile &&
	       densityPreset == o.densityPreset && contourPreset == o.contourPreset &&
	       repetitionPreset == o.repetitionPreset && mutationAmount == o.mutationAmount &&
	       mutationTarget == o.mutationTarget && notePoolMode == o.notePoolMode &&
	       articulationProfile == o.articulationProfile && gateFeel == o.gateFeel &&
	       connectionFeel == o.connectionFeel && slideProfile == o.slideProfile && accentProfile == o.accentProfile &&
	       rate == o.rate && syncMode == o.syncMode && newPairTiming == o.newPairTiming &&
	       noteLengthPct == o.noteLengthPct && baseVelocity == o.baseVelocity && accentDepth == o.accentDepth &&
	       swing == o.swing && octaveJump == o.octaveJump && mutateAll == o.mutateAll && density == o.density &&
	       contrast == o.contrast && resolve == o.resolve && driftCall == o.driftCall && driftResponse == o.driftResponse;
}

// =============================================================================================
// 2. Helpers
// =============================================================================================

namespace {

const double NUL = NAN;
inline bool isNull(double d) { return std::isnan(d); }
inline double num(double d) { return std::isnan(d) ? 0.0 : d; }  // JS: null + x == x
// JS Math.max(lo, Math.min(hi, v)) — NaN stays NaN
inline double clampJS(double v, double lo, double hi) {
	if (std::isnan(v))
		return v;
	return std::max(lo, std::min(hi, v));
}
inline bool has(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }
std::string lower(std::string s) {
	for (auto& c : s)
		c = (char) std::tolower((unsigned char) c);
	return s;
}
// JS (x % n + n) % n for doubles
inline double modJS(double a, double n) { return std::fmod(std::fmod(a, n) + n, n); }

// (densityValue: the JS falls back to it only when DENSITY is undefined, which cfg() never leaves it)
double repetitionValue(const std::string& n) {
	if (n == "Very repetitive") return 0.88;
	if (n == "Repetitive") return 0.72;
	if (n == "Balanced") return 0.5;
	if (n == "Varied") return 0.32;
	if (n == "Highly varied") return 0.18;
	return 0.5;
}
int contourBias(const std::string& n) {
	if (n == "Flat") return 0;
	if (n == "Slight rise") return 1;
	if (n == "Slight fall") return -1;
	if (n == "Arch") return 2;
	if (n == "Inverted arch") return -2;
	if (n == "Wandering") return 99;
	if (n == "Stepwise") return 100;
	if (n == "Leapy") return 101;
	return 0;
}

const double OFFBEAT_WEIGHTS[4] = {0.34, 0.40, 0.95, 0.46};

// STYLE_PRESETS: NaN = field absent (the JS `??` falls back to the keyword value)
struct StylePreset {
	const char* name;
	const char* cat;
	double leap, stableEnd, restBias, motifVar, responseTightness, rootBias, octaveBias, syncBias, offbeatBias,
	    sustainBias, choppyBias;
};
#define N_ NAN
const StylePreset STYLE_PRESETS[] = {
    // name                       cat      leap  stEnd rest  mVar  rTight root  oct   sync  offb  sust  chop
    {"Classic Acid",             "acid",   N_,   N_,   N_,   N_,   N_,    0.30, N_,   0.28, N_,   N_,   N_},
    {"Raw Acid",                 "acid",   N_,   N_,   N_,   N_,   N_,    0.24, N_,   N_,   N_,   N_,   0.24},
    {"Squelchy Acid",            "acid",   N_,   N_,   N_,   N_,   N_,    N_,   N_,   N_,   N_,   0.28, N_},
    {"Dark Acid",                "acid",   N_,   N_,   0.20, N_,   N_,    0.38, N_,   N_,   N_,   N_,   N_},
    {"Hypno Acid",               "acid",   N_,   N_,   N_,   N_,   N_,    N_,   N_,   N_,   N_,   0.42, N_},
    {"Sparse Acid",              "acid",   N_,   N_,   0.30, N_,   N_,    N_,   N_,   N_,   N_,   N_,   N_},
    {"Busy Acid",                "acid",   N_,   N_,   N_,   0.24, N_,    N_,   N_,   N_,   N_,   N_,   N_},
    {"Acid Stabs",               "acid",   N_,   N_,   N_,   N_,   N_,    N_,   N_,   N_,   N_,   N_,   0.46},
    {"Acid Runner",              "acid",   0.26, N_,   N_,   N_,   N_,    N_,   N_,   N_,   N_,   N_,   N_},
    {"Acid Question/Answer",     "acid",   N_,   N_,   N_,   N_,   0.78,  N_,   N_,   N_,   N_,   N_,   N_},
    {"Driving Techno",           "techno", N_,   N_,   N_,   N_,   N_,    0.38, N_,   N_,   N_,   N_,   0.34},
    {"Hypnotic Techno",          "techno", N_,   N_,   N_,   N_,   0.78,  N_,   N_,   N_,   N_,   0.28, N_},
    {"Minimal Techno",           "techno", N_,   N_,   0.34, N_,   N_,    N_,   N_,   N_,   N_,   N_,   0.40},
    {"Raw Warehouse",            "techno", 0.18, N_,   N_,   N_,   N_,    N_,   N_,   N_,   N_,   N_,   0.42},
    {"Tool Techno",              "techno", N_,   N_,   N_,   N_,   N_,    0.44, N_,   N_,   N_,   N_,   N_},
    {"Dub Techno",               "techno", N_,   N_,   0.32, N_,   N_,    N_,   N_,   N_,   N_,   0.36, N_},
    {"Industrial Techno",        "techno", 0.20, N_,   N_,   N_,   N_,    N_,   N_,   N_,   N_,   N_,   0.48},
    {"Percussive Techno",        "techno", N_,   N_,   N_,   N_,   N_,    N_,   N_,   N_,   N_,   N_,   0.44},
    {"Loop Techno",              "techno", N_,   N_,   N_,   N_,   0.84,  N_,   N_,   N_,   N_,   N_,   N_},
    {"Broken Techno",            "techno", 0.22, N_,   N_,   0.28, N_,    N_,   N_,   N_,   N_,   N_,   N_},
    {"Jackin House",             "house",  N_,   N_,   N_,   N_,   N_,    N_,   N_,   N_,   0.42, N_,   N_},
    {"Deep House",               "house",  N_,   N_,   N_,   N_,   N_,    N_,   N_,   N_,   0.36, 0.24, N_},
    {"Minimal House",            "house",  N_,   N_,   0.26, N_,   N_,    N_,   N_,   N_,   0.30, N_,   N_},
    {"Classic House",            "house",  N_,   N_,   N_,   N_,   N_,    N_,   N_,   N_,   0.38, N_,   N_},
    {"Tech House",               "house",  N_,   N_,   N_,   N_,   N_,    N_,   N_,   N_,   0.28, N_,   0.22},
    {"Bass House",               "house",  N_,   N_,   N_,   N_,   N_,    0.40, N_,   N_,   0.26, N_,   N_},
    {"Shuffly House",            "house",  N_,   N_,   N_,   N_,   N_,    N_,   N_,   N_,   0.48, 0.18, N_},
    {"Organ House-ish",          "house",  N_,   N_,   N_,   N_,   N_,    N_,   N_,   N_,   0.34, 0.30, N_},
    {"House Stabline",           "house",  N_,   N_,   N_,   N_,   N_,    N_,   N_,   N_,   0.30, N_,   0.34},
    {"House Bounce",             "house",  0.10, N_,   N_,   N_,   N_,    N_,   N_,   N_,   0.50, N_,   N_},
    {"90s Rave",                 "rave",   0.30, N_,   N_,   N_,   N_,    N_,   0.22, N_,   N_,   N_,   N_},
    {"Dark Rave",                "rave",   0.24, N_,   N_,   N_,   N_,    0.30, N_,   N_,   N_,   N_,   N_},
    {"Rave Stab",                "rave",   N_,   N_,   N_,   N_,   N_,    N_,   N_,   N_,   N_,   N_,   0.36},
    {"Electro Pulse",            "rave",   N_,   N_,   N_,   N_,   N_,    N_,   N_,   N_,   0.24, N_,   N_},
    {"Breaky Electro",           "rave",   N_,   N_,   N_,   0.26, N_,    N_,   N_,   N_,   N_,   N_,   N_},
    {"Detroit-ish",              "other",  N_,   N_,   N_,   N_,   N_,    0.34, N_,   N_,   N_,   0.20, N_},
    {"Proto-Trance",             "rave",   0.18, N_,   N_,   N_,   N_,    N_,   N_,   N_,   N_,   0.24, N_},
    {"Hooverish Riff",           "rave",   0.34, N_,   N_,   N_,   N_,    N_,   N_,   N_,   N_,   N_,   N_},
    {"Motif Study",              "other",  N_,   N_,   N_,   0.06, 0.88,  N_,   N_,   N_,   N_,   N_,   N_},
    {"Rooted Groove",            "other",  N_,   N_,   N_,   N_,   N_,    0.56, N_,   N_,   N_,   N_,   N_},
    {"Repetitive Hook",          "other",  N_,   N_,   N_,   0.10, N_,    N_,   N_,   N_,   N_,   N_,   N_},
    {"Wide Answer",              "other",  0.28, N_,   N_,   N_,   0.46,  N_,   N_,   N_,   N_,   N_,   N_},
    {"Tight Answer",             "other",  N_,   N_,   N_,   N_,   0.88,  N_,   N_,   N_,   N_,   N_,   N_},
    {"Tension Builder",          "other",  0.22, 0.40, N_,   N_,   N_,    N_,   N_,   N_,   N_,   N_,   N_},
    {"Resolver",                 "other",  N_,   0.90, N_,   N_,   0.78,  N_,   N_,   N_,   N_,   N_,   N_},
    {"Call Lead / Response Support", "other", N_, N_,  N_,   N_,   0.84,  N_,   N_,   N_,   N_,   N_,   N_},
};
#undef N_

const StylePreset* findPreset(const std::string& style) {
	for (const auto& p : STYLE_PRESETS)
		if (style == p.name)
			return &p;
	return nullptr;
}

std::string styleCategory(const std::string& style) {
	if (const StylePreset* p = findPreset(style))
		return p->cat;
	const std::string s = lower(style);
	if (has(s, "acid")) return "acid";
	if (has(s, "house")) return "house";
	if (has(s, "techno")) return "techno";
	if (has(s, "rave") || has(s, "electro") || has(s, "hoover")) return "rave";
	return "other";
}

const char* const RESTRICTIVE_POOLS[] = {"Strict note pool", "Triad pool", "Seventh pool", "Root-heavy", "Root + 5th heavy"};
bool restrictivePool(const std::string& m) {
	for (auto* r : RESTRICTIVE_POOLS)
		if (m == r)
			return true;
	return false;
}

// field masks for the articulation `only` lists
enum : int { F_GATE = 1, F_ACCENT = 2, F_SLIDE = 4, F_TIE = 8 };

struct ArtPreset {
	const char *name, *gate, *conn, *slide, *accent;
};
const ArtPreset ART_PRESETS[] = {
    {"Clean", "Medium", "Semi-detached", "None", "None"},
    {"Punchy", "Short", "Detached", "Rare", "Moderate"},
    {"Rolling", "Long", "Tied bias", "Occasional", "Light"},
    {"Acidic", "Mixed", "Legato", "Acid moderate", "Motif accents"},
    {"Hypnotic", "Long", "Connected runs", "Rare", "None"},
    {"Choppy", "Tight", "Detached", "None", "Moderate"},
    {"Fluid", "Long", "Legato", "Occasional", "Light"},
    {"Stabbed", "Tight", "Detached", "None", "Heavy"},
    {"Tense", "Short", "Semi-detached", "Rare", "Heavy"},
    {"Released", "Long", "Semi-detached", "Occasional", "Light"},
};

int foldPitch(int m) {
	while (m > 127)
		m -= 12;
	while (m < 0)
		m += 12;
	return m;
}

} // namespace

// The JS cfg(): names as strings, numbers normalised.
struct Engine::Cfg {
	std::string style, responseStrategy, motifType, phraseMode, endingBehaviour, key, rootOctave;
	std::vector<int> scale;
	int steps;
	std::string registerProfile, densityPreset, contourPreset, repetitionPreset, mutationAmount, mutationTarget,
	    notePoolMode, articulationProfile, gateFeel, connectionFeel, slideProfile, accentProfile;
	double noteLengthPct, densityTarget, contrast, resolve, octaveJump;
};

Engine::Cfg Engine::cfg() const {
	const Params& p = S.p;
	Cfg c;
	auto at = [](const std::vector<std::string>& l, int i) { return l[(size_t) std::max(0, std::min((int) l.size() - 1, i))]; };
	c.style = at(kStyles, p.style);
	c.responseStrategy = at(kStrategies, p.responseStrategy);
	c.motifType = at(kMotifs, p.motifType);
	c.phraseMode = at(kModes, p.phraseMode);
	c.endingBehaviour = at(kEndings, p.endingBehaviour);
	c.key = at(kKeys, p.key);
	c.scale = scaleDefs()[(size_t) std::max(0, std::min((int) scaleDefs().size() - 1, p.scale))].iv;
	c.rootOctave = at(kOctaves, p.rootOctave);
	c.steps = std::max(kMinSteps, std::min(kMaxSteps, p.steps));
	c.registerProfile = at(kRegisters, p.registerProfile);
	c.densityPreset = at(kDensityPresets, p.densityPreset);
	c.contourPreset = at(kContours, p.contourPreset);
	c.repetitionPreset = at(kRepetitions, p.repetitionPreset);
	c.mutationAmount = at(kMutationAmounts, p.mutationAmount);
	c.mutationTarget = at(kMutationTargets, p.mutationTarget);
	c.notePoolMode = at(kNotePools, p.notePoolMode);
	c.articulationProfile = at(kArticulation, p.articulationProfile);
	c.gateFeel = at(kGateFeels, p.gateFeel);
	c.connectionFeel = at(kConnections, p.connectionFeel);
	c.slideProfile = at(kSlides, p.slideProfile);
	c.accentProfile = at(kAccents, p.accentProfile);
	c.noteLengthPct = p.noteLengthPct;
	c.densityTarget = std::max(0.0, std::min(1.0, p.density / 100.0));
	c.contrast = std::max(0.0, std::min(1.0, p.contrast / 100.0));
	c.resolve = std::max(0.0, std::min(1.0, p.resolve / 100.0));
	c.octaveJump = std::max(0.0, std::min(1.0, p.octaveJump / 100.0));
	return c;
}

double Engine::rand(const std::vector<double>& a) {
	if (a.empty())
		return NUL;  // JS: undefined
	return a[(size_t) std::floor(random() * (double) a.size())];
}
int Engine::randInt(const std::vector<int>& a) {
	if (a.empty())
		return 0;
	return a[(size_t) std::floor(random() * (double) a.size())];
}
std::vector<int> Engine::pickN(std::vector<int> a, int n) {
	std::vector<int> out;
	while (!a.empty() && (int) out.size() < n) {
		const size_t i = (size_t) std::floor(random() * (double) a.size());
		out.push_back(a[i]);
		a.erase(a.begin() + (long) i);
	}
	return out;
}

namespace {
std::vector<double> filt(std::initializer_list<double> v, size_t len) {
	std::vector<double> o;
	for (double x : v)
		if (x < (double) len)
			o.push_back(x);
	return o;
}

int rootMidi(const std::string& rootOctave, const std::string& key) {
	static const char* octs[] = {"-1", "0", "1", "2", "3", "4", "5"};
	int base = 24;
	for (int i = 0; i < 7; i++)
		if (rootOctave == octs[i])
			base = i * 12;
	return base + indexOf(kKeys, key.c_str());
}
// degreeToMidi: NaN when the degree does not name a scale entry (JS s[1.5] is undefined)
double degreeToMidi(double deg, const std::vector<int>& s, const std::string& rootOctave, const std::string& key) {
	if (std::isnan(deg))
		deg = 0.0;  // JS: null -> 0 (callers guard null themselves)
	const double len = s.empty() ? 1.0 : (double) s.size();
	const double oct = std::floor(deg / len);
	const double idx = modJS(deg, len);
	if (idx != std::floor(idx) || s.empty())
		return NAN;
	return rootMidi(rootOctave, key) + s[(size_t) idx] + oct * 12.0;
}

double preferredStepActivation(const std::string& cat, int idx, double offbeatBias) {
	const double* base;
	static const double acid[4] = {0.82, 0.35, 0.72, 0.28}, techno[4] = {0.86, 0.18, 0.58, 0.22},
	                    house[4] = {0.68, 0.52, 0.60, 0.40}, rave[4] = {0.78, 0.42, 0.68, 0.38},
	                    other[4] = {0.7, 0.35, 0.6, 0.3};
	if (cat == "acid") base = acid;
	else if (cat == "techno") base = techno;
	else if (cat == "house") base = house;
	else if (cat == "rave") base = rave;
	else base = other;
	const double b = offbeatBias ? std::max(0.0, std::min(1.0, offbeatBias * 2.2)) : 0.0;
	const int i = idx % 4;
	return base[i] * (1 - b) + OFFBEAT_WEIGHTS[i] * b;
}

int fifthDegree(const std::vector<int>& scale) {
	if (scale.empty())
		return 0;
	int best = 0, bestD = 99;
	for (int i = 0; i < (int) scale.size(); i++) {
		const int d = std::abs((scale[(size_t) i] % 12) - 7);
		if (d < bestD) {
			bestD = d;
			best = i;
		}
	}
	return best;
}

int registerCeiling(const std::string& reg, const std::vector<int>& scale) {
	const int len = std::max(3, scale.empty() ? 7 : (int) scale.size());
	if (reg == "Narrow register") return len - 1;
	if (reg == "Medium register") return len + len / 2;
	return len * 2 + 2;
}

std::vector<int> stableDegreesForScale(size_t len) {
	std::vector<int> o;
	for (int x : {0, 2, 4, 6})
		if ((size_t) x < len)
			o.push_back(x);
	return o;
}

double phraseContour(const Phrase& ph) {
	std::vector<double> d;
	for (auto& s : ph.s)
		if (s.active && !isNull(s.degree))
			d.push_back(s.degree);
	if (d.size() < 2)
		return 0;
	return d.back() - d.front();
}

// JS: Object.keys(tally) on integer degrees iterates ascending (std::map order)
std::map<double, int> tallyOf(const Phrase& ph) {
	std::map<double, int> t;
	for (auto& x : ph.s)
		if (x.active && !isNull(x.degree))
			t[x.degree]++;
	return t;
}
// `common` as restoreDensity / applyDensity pick it
double commonDegree(const Phrase& ph) {
	const auto t = tallyOf(ph);
	double common = 0;
	for (auto& kv : t) {
		auto it = t.find(common);
		if (it == t.end() || kv.second > it->second)
			common = kv.first;
	}
	return common;
}
} // namespace

struct Engine::SP {
	double leap, stableEnd, restBias, motifVar, responseTightness, rootBias, octaveBias, syncBias, offbeatBias,
	    sustainBias, choppyBias;
};

Engine::SP Engine::styleProfile(const Cfg& c) const {
	const std::string style = lower(c.style);
	double leap = 0.18, stableEnd = 0.7;
	double restBias = 0.18, motifVar = 0.18, responseTightness = 0.5, rootBias = 0.22, octaveBias = 0.12;
	double syncBias = 0.18, offbeatBias = 0.14, sustainBias = 0.18, choppyBias = 0.14;
	const StylePreset* preset = findPreset(c.style);
	if (has(style, "acid")) { leap = 0.22; rootBias = 0.28; responseTightness = 0.66; syncBias = 0.26; sustainBias = 0.34; }
	if (has(style, "raw")) { motifVar += 0.08; choppyBias += 0.12; }
	if (has(style, "hypno")) { leap -= 0.08; stableEnd = 0.55; restBias = 0.1; responseTightness = 0.72; sustainBias += 0.16; }
	if (has(style, "minimal")) { restBias = 0.28; }
	if (has(style, "busy")) { restBias -= 0.06; }
	if (has(style, "sparse")) { restBias += 0.12; }
	if (has(style, "house")) { leap = 0.12; rootBias = 0.32; responseTightness = 0.62; offbeatBias = 0.3; syncBias = 0.2; sustainBias = 0.12; }
	if (has(style, "techno")) { leap = 0.14; rootBias = 0.35; restBias = 0.24; responseTightness = 0.7; choppyBias = 0.3; syncBias = 0.12; }
	if (has(style, "rave") || has(style, "electro") || has(style, "hoover")) { leap = 0.24; octaveBias = 0.2; offbeatBias = 0.22; }
	if (has(style, "dub")) { restBias = 0.3; stableEnd = 0.78; sustainBias = 0.22; }
	if (has(style, "industrial") || has(style, "warehouse")) { leap = 0.18; rootBias = 0.4; choppyBias = 0.28; }
	if (has(style, "motif study")) { motifVar = 0.08; responseTightness = 0.85; }
	if (has(style, "rooted groove")) { rootBias = 0.5; stableEnd = 0.8; }
	if (has(style, "repetitive hook")) { motifVar = 0.1; }
	if (preset) {
		auto pick = [](double pv, double v) { return std::isnan(pv) ? v : pv; };
		leap = pick(preset->leap, leap);
		stableEnd = pick(preset->stableEnd, stableEnd);
		restBias = pick(preset->restBias, restBias);
		motifVar = pick(preset->motifVar, motifVar);
		responseTightness = pick(preset->responseTightness, responseTightness);
		rootBias = pick(preset->rootBias, rootBias);
		octaveBias = pick(preset->octaveBias, octaveBias);
		syncBias = pick(preset->syncBias, syncBias);
		offbeatBias = pick(preset->offbeatBias, offbeatBias);
		sustainBias = pick(preset->sustainBias, sustainBias);
		choppyBias = pick(preset->choppyBias, choppyBias);
	}
	return {leap, stableEnd, restBias, motifVar, responseTightness, rootBias, octaveBias, syncBias, offbeatBias, sustainBias, choppyBias};
}

std::vector<int> Engine::notePool(const Cfg& c) {
	const int scaleLen = (int) c.scale.size();
	std::vector<int> pool;
	for (int i = 0; i < std::min(scaleLen, 7); i++)
		pool.push_back(i);
	auto f = [&](std::initializer_list<int> v) {
		std::vector<int> o;
		for (int x : v)
			if (x < scaleLen)
				o.push_back(x);
		return o;
	};
	const std::string& m = c.notePoolMode;
	if (m == "Strict note pool") pool = pickN(pool, std::min((int) pool.size(), 3));
	else if (m == "Pool with spice notes") {
		std::vector<int> a = pickN(pool, std::min((int) pool.size(), 4));
		a.push_back(std::min(scaleLen - 1, scaleLen / 2));
		std::vector<int> u;  // new Set: keeps the first occurrence order
		for (int x : a)
			if (std::find(u.begin(), u.end(), x) == u.end())
				u.push_back(x);
		pool = u;
	}
	else if (m == "Pool expands on response") pool = f({0, 1, 2, 4});
	else if (m == "Pool contracts on response") { pool.clear(); for (int i = 0; i < scaleLen; i++) pool.push_back(i); }
	else if (m == "Root-heavy") pool = f({0, 0, 0, 1, 2, 4});
	else if (m == "Root + 5th heavy") pool = f({0, 0, 4, 4, 1, 2});
	else if (m == "Triad pool") pool = f({0, 2, 4});
	else if (m == "Seventh pool") pool = f({0, 2, 4, 6});
	else if (m == "Scale-wide") { pool.clear(); for (int i = 0; i < scaleLen; i++) pool.push_back(i); }
	if (pool.empty())
		pool.push_back(0);
	return pool;
}

double Engine::snapToPool(double deg, const std::vector<int>& pool, const Cfg& c, bool nearestOnly) {
	const double len = std::max(1.0, (double) c.scale.size());
	const double oct = std::floor(deg / len);
	const double within = deg - oct * len;
	std::set<int> uniq(pool.begin(), pool.end());
	const bool weighted = pool.size() != uniq.size();
	if (weighted && !nearestOnly && chance(0.5))
		return std::fmod((double) randInt(pool), len) + oct * len;
	double best = pool[0], bestD = 99;
	for (int p : pool) {
		const double d = std::abs(std::fmod((double) p, len) - within);
		if (d < bestD) {
			bestD = d;
			best = std::fmod((double) p, len);
		}
	}
	return best + oct * len;
}

Engine::Motif Engine::motifTemplate(const Cfg& c) {
	const std::string t = lower(c.motifType);
	Motif m;
	m.len = c.steps <= 4 ? 2 : (c.steps <= 8 ? randInt({2, 3, 4}) : randInt({3, 4}));
	const int len = m.len;
	m.rhythm.assign((size_t) len, false);
	auto R = [&](auto fn) {
		for (int i = 0; i < len; i++)
			m.rhythm[(size_t) i] = fn(i);
	};
	if (has(t, "pulse")) R([](int) { return true; });
	else if (has(t, "offbeat")) R([](int i) { return i % 2 == 1; });
	else if (has(t, "sync")) R([&](int i) { return chance(i == 0 ? 0.78 : 0.55); });
	else if (has(t, "sparse") || has(t, "rest")) R([&](int) { return chance(0.45); });
	else if (has(t, "dense") || has(t, "rolling")) R([&](int) { return chance(0.82); });
	else if (has(t, "stab")) R([&](int i) { return i == 0 || chance(0.25); });
	else if (has(t, "push")) R([&](int i) { return i == len - 1 || chance(0.35); });
	else if (has(t, "four-on-floor")) R([](int i) { return i == 0; });
	else if (has(t, "house bounce")) R([](int i) { return i == 0 || i % 2 == 1; });
	else if (has(t, "drone")) R([](int i) { return i == 0; });
	else if (has(t, "pedal")) R([](int i) { return i % 2 == 0; });
	else if (has(t, "tag")) R([&](int i) { return i >= len - 2; });
	else R([&](int) { return chance(0.65); });
	if (std::none_of(m.rhythm.begin(), m.rhythm.end(), [](bool b) { return b; }))
		m.rhythm[0] = true;

	auto slice = [&](std::initializer_list<double> v) {
		std::vector<double> o(v);
		if ((int) o.size() > len)
			o.resize((size_t) len);
		return o;
	};
	std::vector<double>& ct = m.contour;
	if (has(t, "repeated")) ct.assign((size_t) len, 0.0);
	else if (has(t, "stepwise rise")) { for (int i = 0; i < len; i++) ct.push_back(i); }
	else if (has(t, "stepwise fall")) { for (int i = 0; i < len; i++) ct.push_back(-i); }
	else if (has(t, "rise then drop")) ct = slice({0, 1, 2, 0, -1});
	else if (has(t, "drop then rise")) ct = slice({0, -1, -2, 0, 1});
	else if (has(t, "leap")) { const double r = rand({2, 3, 4}); ct = slice({0, r, 1, 0}); }
	else if (has(t, "pivot")) ct = slice({0, 1, 0, -1});
	else if (has(t, "two-note")) ct = slice({0, 1, 0, 1});
	else if (has(t, "three-note")) ct = slice({0, 1, 2, 1});
	else if (has(t, "arp")) ct = slice({0, 2, 4, 2});
	else if (has(t, "acid nibble")) ct = slice({0, 0, 1, 0, -1});
	else if (has(t, "techno cell")) ct = slice({0, 0, 0, 1});
	else if (has(t, "house bounce")) ct = slice({0, 2, 0, 1});
	else if (has(t, "drone")) ct.assign((size_t) len, 0.0);
	else if (has(t, "pedal")) ct = slice({0, 3, 0, 4});
	else if (has(t, "push")) ct = slice({0, 0, 1, 2});
	else if (has(t, "four-on-floor")) ct = slice({0, 0, 0, 1});
	else if (has(t, "call tag")) { ct = {0, 1}; for (int i = 0; i < std::max(0, len - 2); i++) ct.push_back(0); if ((int) ct.size() > len) ct.resize((size_t) len); }
	else if (has(t, "answer tag")) { ct = {0, -1}; for (int i = 0; i < std::max(0, len - 2); i++) ct.push_back(0); if ((int) ct.size() > len) ct.resize((size_t) len); }
	else { for (int i = 0; i < len; i++) ct.push_back(rand({-1, 0, 1})); }
	return m;
}

Engine::Art Engine::articulationForStep(int idx, bool active, const Cfg& c, int motifPos) {
	const SP sp = styleProfile(c);
	const std::string cat = styleCategory(c.style);
	double gate = 0.82;
	bool accent = false, slide = false, tie = false;
	const std::string &gateFeel = c.gateFeel, &conn = c.connectionFeel, &slideP = c.slideProfile, &accentP = c.accentProfile;

	static const double mixed[10] = {0.54, 0.25, 0.15, 0.54, 0.25, 0.85, 0.54, 0.25, 0.54, 0.15};
	static const double mixedShort[10] = {0.10, 0.25, 0.06, 0.15, 0.25, 0.10, 0.54, 0.06, 0.15, 0.10};
	static const double wideMix[10] = {0.06, 0.54, 0.15, 0.95, 0.25, 0.10, 0.85, 0.25, 0.54, 0.06};
	if (gateFeel == "Pluck") gate = 0.06;
	else if (gateFeel == "Stab") gate = 0.10;
	else if (gateFeel == "Tight") gate = 0.15;
	else if (gateFeel == "Short") gate = 0.25;
	else if (gateFeel == "Medium") gate = 0.54;
	else if (gateFeel == "Long") gate = 0.85;
	else if (gateFeel == "Full") gate = 0.95;
	else if (gateFeel == "Mixed") gate = mixed[idx % 10];
	else if (gateFeel == "Mixed short") gate = mixedShort[idx % 10];
	else if (gateFeel == "Wide mix") gate = wideMix[idx % 10];
	else if (gateFeel == "Random within bounds") gate = 0.20 + random() * 0.70;

	if (cat == "acid" && idx % 4 == 3 && chance(sp.sustainBias)) gate *= 1.15;
	else if (cat == "techno" && idx % 4 == 0 && chance(sp.choppyBias)) gate *= 0.85;
	else if (cat == "house" && idx % 2 == 1 && chance(sp.offbeatBias)) gate *= 1.12;

	if (conn == "Detached") gate = std::min(gate, 0.45);
	else if (conn == "Semi-detached") gate = std::min(gate, 0.62);
	else if (conn == "Legato") tie = chance(0.45);
	else if (conn == "Tied bias") tie = chance(0.30);
	else if (conn == "Mixed ties") tie = chance(0.18);
	else if (conn == "Connected runs") tie = chance(0.28);

	if (slideP == "None") slide = false;
	else if (slideP == "Rare") slide = chance(0.06);
	else if (slideP == "Occasional") slide = chance(0.16);
	else if (slideP == "Acid moderate") slide = chance(0.28);
	else if (slideP == "Acid heavy") slide = chance(0.42);
	else if (slideP == "Phrase-end slides") slide = chance(motifPos >= 2 ? 0.32 : 0.04);
	else if (slideP == "Upward slides bias") slide = chance(0.30);
	else if (slideP == "Downward slides bias") slide = chance(0.30);

	if (accentP == "None") accent = false;
	else if (accentP == "Light") accent = chance(0.14);
	else if (accentP == "Moderate") accent = chance(0.24);
	else if (accentP == "Heavy") accent = chance(0.38);
	else if (accentP == "Offbeat accents") accent = idx % 4 == 2;
	else if (accentP == "Syncopated accents") accent = idx % 2 == 1 && chance(0.5);
	else if (accentP == "Downbeat accents") accent = idx % 4 == 0 || idx == 0;
	else if (accentP == "Motif accents") accent = motifPos == 0 || chance(0.08);
	else if (accentP == "Random accents within motif") accent = motifPos > 0 && chance(0.40);

	if (cat == "acid" && active) tie = tie || chance(0.06);
	if (!active) {
		accent = false;
		slide = false;
		tie = false;
	}
	return {gate, accent, slide, tie};
}

void Engine::clusterHolds(Phrase& ph, int only, bool runs) {
	auto kindOn = [&](int k) { return !only || (only & k); };
	std::vector<int> live;
	for (int i = 0; i < (int) ph.size(); i++)
		if (ph[(size_t) i].active)
			live.push_back(i);
	if (live.size() < 4)
		return;
	std::vector<int> held;
	for (int i : live)
		if ((kindOn(F_TIE) && ph[(size_t) i].tie) || (kindOn(F_SLIDE) && ph[(size_t) i].slide))
			held.push_back(i);
	const int want = (int) held.size();
	if (!want)
		return;
	std::vector<int> kinds;  // F_TIE or F_SLIDE
	for (int i : held)
		kinds.push_back((kindOn(F_TIE) && ph[(size_t) i].tie) ? F_TIE : F_SLIDE);
	for (int i : held) {
		if (kindOn(F_TIE)) ph[(size_t) i].tie = false;
		if (kindOn(F_SLIDE)) ph[(size_t) i].slide = false;
	}
	auto liveAt = [&](int k) { return (k >= 0 && k < (int) live.size()) ? live[(size_t) k] : -1; };
	auto holds = [&](int i) { return i >= 0 && (ph[(size_t) i].tie || ph[(size_t) i].slide); };
	int placed = 0, guard = 0;
	while (placed < want && guard++ < 40) {
		const int runLen = std::min(runs ? randInt({2, 2, 3, 3, 4}) : randInt({1, 1, 1, 2}), want - placed);
		const int startAt = (int) std::floor(random() * std::max(1, (int) live.size() - runLen));
		bool clash = false;
		for (int k = 0; k < runLen; k++) {
			const int idx = liveAt(startAt + k);
			if (idx < 0 || holds(idx)) {
				clash = true;
				break;
			}
			const int before = liveAt(startAt - 1), after = liveAt(startAt + runLen);
			if (holds(before)) clash = true;
			if (holds(after)) clash = true;
		}
		if (clash)
			continue;
		for (int k = 0; k < runLen; k++) {
			const int idx = liveAt(startAt + k);
			const int kind = placed < (int) kinds.size() ? kinds[(size_t) placed] : F_TIE;
			if (kind == F_TIE) ph[(size_t) idx].tie = true;
			else ph[(size_t) idx].slide = true;
			placed++;
		}
	}
}

void Engine::postProcessArticulation(Phrase& ph, const Cfg& c, int only) {
	auto touches = [&](int f) { return !only || (only & f); };
	if (touches(F_TIE) || touches(F_SLIDE))
		clusterHolds(ph, only, c.connectionFeel == "Connected runs");
	const int n = (int) ph.size();

	for (int i = 0; i < n - 1; i++) {
		Step& s = ph[(size_t) i];
		if (!touches(F_SLIDE) || !s.active || !s.slide)
			continue;
		int j = i + 1;
		while (j < n && !ph[(size_t) j].active)
			j++;
		if (j >= n) {
			s.slide = false;
			continue;
		}
		if (ph[(size_t) j].degree == s.degree) {
			const double ceilDeg = registerCeiling(c.registerProfile, c.scale);
			const double sd = num(s.degree);  // JS compares null as 0
			const double dir = (sd >= ceilDeg) ? -1 : (sd <= 0 ? 1 : rand({-1, 1}));
			ph[(size_t) j].degree = clampJS(num(s.degree) + dir, 0, ceilDeg);
			if (ph[(size_t) j].degree == s.degree)
				ph[(size_t) j].degree = clampJS(num(s.degree) - dir, 0, ceilDeg);
		}
	}

	for (int i = 0; i < n - 1; i++) {
		Step& s = ph[(size_t) i];
		if (!touches(F_TIE) || !s.active || !s.tie)
			continue;
		int j = i + 1;
		while (j < n && !ph[(size_t) j].active)
			j++;
		if (j >= n) {
			s.tie = false;
			continue;
		}
		if (s.slide) {
			s.tie = false;
			continue;
		}
		if (!(ph[(size_t) j].degree == s.degree))
			ph[(size_t) j].degree = s.degree;
	}

	const std::string cat = styleCategory(c.style);
	const int dirBias = c.slideProfile == "Upward slides bias" ? 1 : c.slideProfile == "Downward slides bias" ? -1 : 0;
	if (dirBias) {
		for (int i = 0; i < n - 1; i++) {
			Step &s = ph[(size_t) i], &nx = ph[(size_t) i + 1];
			if (!s.active || !s.slide || !nx.active || isNull(nx.degree) || isNull(s.degree))
				continue;
			const double moving = nx.degree - s.degree;
			const int sign = moving > 0 ? 1 : moving < 0 ? -1 : 0;
			if (moving == 0 || sign != dirBias)
				nx.degree = clampJS(s.degree + dirBias * rand({1, 1, 2}), 0, 11);
		}
	}

	if (cat == "acid") {
		for (int i = 0; i < n; i++) {
			Step& s = ph[(size_t) i];
			if (!s.active || isNull(s.degree))
				continue;
			if (touches(F_ACCENT) && s.accent && !(i % 4 == 0 || s.degree == 0) && chance(0.4)) {
				s.accent = false;
				for (int k = 0; k < n; k++) {
					Step& o = ph[(size_t) k];
					if (o.active && !o.accent && (k % 4 == 0 || o.degree == 0)) {
						o.accent = true;
						break;
					}
				}
			}
			if (touches(F_TIE) && i < n - 1 && ph[(size_t) i + 1].active) {
				const double nd = isNull(ph[(size_t) i + 1].degree) ? s.degree : ph[(size_t) i + 1].degree;
				const double diff = std::abs(nd - s.degree);
				if (diff <= 2 && chance(0.10)) {
					s.tie = true;
					s.gate = std::max(s.gate ? s.gate : 0.8, 1.05);
				}
			}
		}
		int slides = 0;
		for (auto& s : ph.s)
			slides += s.slide;
		const int maxSlides = (int) std::ceil(n / 3.0);
		if (slides > maxSlides) {
			for (int i = n - 1; i >= 0 && slides > maxSlides; i--)
				if (ph[(size_t) i].slide) {
					ph[(size_t) i].slide = false;
					slides--;
				}
		}
	}
	else if (cat == "techno") {
		for (int i = 0; i < n; i++) {
			Step& s = ph[(size_t) i];
			if (touches(F_ACCENT) && s.active && s.accent && i % 4 != 0 && chance(0.25)) {
				s.accent = false;
				for (int k = 0; k < n; k += 4)
					if (ph[(size_t) k].active && !ph[(size_t) k].accent) {
						ph[(size_t) k].accent = true;
						break;
					}
			}
		}
	}
	else if (cat == "house") {
		for (int i = 0; i < n; i++) {
			Step& s = ph[(size_t) i];
			if (touches(F_ACCENT) && s.active && s.accent && i % 2 == 0 && chance(0.3)) {
				s.accent = false;
				for (int k = 1; k < n; k += 2)
					if (ph[(size_t) k].active && !ph[(size_t) k].accent) {
						ph[(size_t) k].accent = true;
						break;
					}
			}
		}
	}
}

void Engine::normalisePhrase(Phrase& ph) {
	for (auto& s : ph.s) {
		if (s.active && isNull(s.degree)) {
			s.active = false;
			s.accent = s.slide = s.tie = false;
		}
		if (!s.active)
			s.degree = NUL;
	}
	if (!ph.s.empty() && std::none_of(ph.s.begin(), ph.s.end(), [](const Step& s) { return s.active; })) {
		ph.s.back().active = true;
		ph.s.back().degree = 0;
	}
}

void Engine::applyEnding(Phrase& ph, const Cfg& c, bool response) {
	const int lastIdx = (int) ph.size() - 1;
	if (lastIdx < 0)
		return;
	Step& last = ph[(size_t) lastIdx];
	const double before = last.degree;
	if (!last.active) {
		last.active = true;
		last.degree = 0;
	}
	const std::string& end = c.endingBehaviour;
	const size_t L = c.scale.size();
	auto prevDeg = [&]() { return lastIdx - 1 >= 0 ? ph[(size_t) lastIdx - 1].degree : NUL; };
	if (has(end, "Resolve to root")) last.degree = 0;
	else if (has(end, "Resolve to 5th")) last.degree = std::min(4.0, (double) L - 1);
	else if (has(end, "Rise into")) { const double p = prevDeg(); last.degree = clampJS((isNull(p) || p == 0 ? 0 : p) + 1, 0, 10); }
	else if (has(end, "Fall into")) { const double p = prevDeg(); last.degree = clampJS((isNull(p) || p == 0 ? 1 : p) - 1, 0, 10); }
	else if (has(end, "Hard stop")) { last.gate = 0.35; last.tie = false; }
	else if (has(end, "Fade energy")) { last.accent = false; last.gate = 0.55; }
	else if (has(end, "Strong cadence")) last.degree = rand(filt({0, 0, 0, 2, 4}, L));
	else if (has(end, "Weak cadence")) last.degree = rand(filt({1, 3, 5}, L));
	else if (has(end, "Ambiguous")) last.degree = rand(filt({1, 2, 5}, L));

	if (response && has(end, "more resolved")) last.degree = rand(filt({0, 0, 2, 4}, L));
	if (response && has(end, "more tense")) last.degree = rand(filt({1, 3, 5}, L));

	// RESOLVE macro
	const std::vector<double> stableTones = filt({0, 2, 4}, L), tenseTones = filt({1, 3, 5, 6}, L);
	const std::string le = lower(end);
	const bool explicitEnd = has(le, "root") || has(le, "5th") || has(le, "cadence") || has(le, "resolved") || has(le, "tense");
	const bool apply = response || chance(0.5);
	if (apply && !stableTones.empty() && !tenseTones.empty()) {
		if (explicitEnd) {
			if (!chance(c.resolve))
				last.degree = before;
		}
		else {
			last.degree = chance(c.resolve) ? rand(stableTones) : rand(tenseTones);
		}
	}
}

// =============================================================================================
// 3. Phrase construction
// =============================================================================================

Phrase Engine::buildCall(const Cfg& c) {
	const SP sp = styleProfile(c);
	const std::string cat = styleCategory(c.style);
	const Motif motif = motifTemplate(c);
	const std::vector<int> pool = notePool(c);
	const int steps = c.steps;
	const size_t L = c.scale.size();
	Phrase out;
	const double densTarget = 0.20 + c.densityTarget * 0.74;
	const double repTarget = repetitionValue(c.repetitionPreset);
	const int contourMode = contourBias(c.contourPreset);

	const double ceilNow = registerCeiling(c.registerProfile, c.scale);
	double center = 0;
	if (c.registerProfile == "Narrow register") center = 0;
	else if (c.registerProfile == "Medium register") center = std::floor(random() * std::max(1.0, ceilNow * 0.25));
	else center = std::floor(random() * std::max(1.0, ceilNow * 0.5));
	if (contourBias(c.contourPreset) < 0)
		center += (c.registerProfile == "Narrow register") ? 2 : 3;

	std::vector<Step> motifSteps;
	double prevDeg = center + rand({0, 0, 1, -1});
	for (int m = 0; m < motif.len; m++) {
		bool active = motif.rhythm[(size_t) m];
		if (chance(sp.restBias + (1 - densTarget) * 0.25))
			active = active && chance(0.68);
		double deg = prevDeg;
		if (contourMode != 0 && m == 0 && chance(sp.rootBias))
			deg = rand(filt({0, 0, 1, 2}, L));
		else {
			const double contour = m < (int) motif.contour.size() ? motif.contour[(size_t) m] : 0.0;
			if (contourMode == 0) deg = prevDeg;
			else if (contourMode == 99 && m > 0) deg = prevDeg + rand({-3, -2, -1, 1, 2, 3});
			else if (contourMode == 100 && m > 0) deg = prevDeg + rand({-1, 0, 1});
			else if (contourMode == 101 && m > 0 && chance(0.35)) deg = prevDeg + rand({-3, -2, 2, 3});
			else if (contourMode == 1) deg = prevDeg + 1 + std::max(0.0, contour) + rand({-1, 0, 0, 0});
			else if (contourMode == -1) deg = prevDeg - 1 + std::min(0.0, contour) + rand({0, 0, 0, 1});
			else if (contourMode == 2) deg = prevDeg + (m < (int) std::ceil(motif.len / 2.0) ? 1 : -1) * 1.5 + rand({0, 0, 0, 1});
			else if (contourMode == -2) deg = prevDeg + (m < (int) std::ceil(motif.len / 2.0) ? -1 : 1) * 1.5 + rand({-1, 0, 0, 0});
			else if (chance(0.65)) deg = prevDeg + contour + rand({-1, 0, 0, 1});
			else deg = randInt(pool) + rand({0, 0, std::max(0.0, std::floor(center / 2))});
		}
		if (contourMode != 0) {
			const double leapScale = c.registerProfile == "Wide register" ? 1.6 : c.registerProfile == "Narrow register" ? 0.5 : 1;
			if (chance(sp.leap * (contourMode == 101 ? 1.45 : 1) * leapScale))
				deg += c.registerProfile == "Wide register" ? rand({-4, -3, 3, 4}) : rand({-3, 3});
			if (cat == "acid" && chance(sp.syncBias)) deg += rand({-1, 0, 1});
			if (cat == "house" && chance(sp.offbeatBias) && m % 2 == 1) deg += rand({0, 1});
		}
		if (cat == "techno" && chance(sp.rootBias * 0.4)) deg = rand(filt({0, 0, 2, 4}, L));
		deg = clampJS(deg, 0, registerCeiling(c.registerProfile, c.scale));
		if (restrictivePool(c.notePoolMode) && !pool.empty() && chance(0.8))
			deg = snapToPool(deg, pool, c, contourMode == 0);
		prevDeg = deg;
		const Art art = articulationForStep(m, active, c, m);
		Step st;
		st.active = active;
		st.degree = active ? deg : NUL;
		st.gate = art.gate;
		st.accent = art.accent;
		st.slide = art.slide;
		st.tie = art.tie;
		motifSteps.push_back(st);
	}
	if (std::none_of(motifSteps.begin(), motifSteps.end(), [](const Step& s) { return s.active; })) {
		motifSteps[0].active = true;
		motifSteps[0].degree = 0;
	}

	for (int i = 0; i < steps; i++) {
		const int m = i % motif.len;
		const int block = i / motif.len;
		Step step = motifSteps[(size_t) m];
		if (block > 0) {
			if (chance(repTarget))
				step = motifSteps[(size_t) m];
			else {
				if (contourMode != 0 && step.active && !isNull(step.degree)) {
					const double d = c.repetitionPreset == "Highly varied" ? rand({-3, -2, -1, 0, 1, 2, 3}) : rand({-2, -1, 0, 1, 2});
					step.degree = clampJS(step.degree + d, 0, registerCeiling(c.registerProfile, c.scale));
				}
				if (chance(sp.motifVar + (1 - repTarget) * 0.25))
					step.active = !step.active ? chance(0.38) : step.active;
			}
			if (contourMode != 0 && i >= (int) std::floor(steps * 0.75) && step.active && chance(sp.stableEnd))
				step.degree = clampJS(num(step.degree) + rand({-1, 0, 1}), 0, 10);
		}
		const double styleAct = preferredStepActivation(cat, i, sp.offbeatBias);
		const double dens = std::min(0.98, std::max(0.05, (densTarget * 0.6) + (styleAct * 0.4)));
		if (chance(1 - dens))
			step.active = step.active && chance(dens);
		if (contourMode != 0 && step.active && chance(sp.rootBias * 0.4) && i % std::max(2, motif.len) == 0)
			step.degree = rand(filt({0, 0, 2, 4}, L));
		if (cat == "house" && step.active && i % 2 == 1 && chance(sp.offbeatBias))
			step.gate = std::max(step.gate ? step.gate : 0.8, 0.95);
		if (!step.active)
			step.degree = NUL;
		out.s.push_back(step);
	}

	// CONTOUR shape, after the repetition clone
	auto applyContourShape = [&](Phrase& ph) {
		if (steps < 2)
			return;
		if (contourMode == 0) {
			const auto tally = tallyOf(ph);
			if (tally.empty())
				return;
			double best = tally.begin()->first;
			int bestN = tally.begin()->second;
			for (auto& kv : tally)
				if (kv.second > bestN) {
					best = kv.first;
					bestN = kv.second;
				}
			for (auto& s : ph.s)
				if (s.active && !isNull(s.degree))
					s.degree = best;
			return;
		}
		auto bend = [&](double t) -> double {
			if (contourMode == 1) return t * 4;
			if (contourMode == -1) return (1 - t) * 4;
			if (contourMode == 2) return std::sin(t * M_PI) * 4;
			return (1 - std::sin(t * M_PI)) * 4;
		};
		if (contourMode != 1 && contourMode != -1 && contourMode != 2 && contourMode != -2)
			return;
		const double ceilDeg = registerCeiling(c.registerProfile, c.scale);
		for (size_t i = 0; i < ph.size(); i++) {
			if (!ph[i].active || isNull(ph[i].degree))
				continue;
			ph[i].degree = clampJS(std::round(ph[i].degree + bend((double) i / (ph.size() - 1))), 0, ceilDeg);
		}
	};

	if (c.repetitionPreset == "Very repetitive" || c.repetitionPreset == "Repetitive") {
		const int cloneLen = std::min(motif.len, steps / 2);
		for (int i = cloneLen; i < steps && cloneLen > 0; i++) {
			if (chance(c.repetitionPreset == "Very repetitive" ? 0.9 : 0.72)) {
				const Step src = out[(size_t) (i % cloneLen)];
				const Art a = articulationForStep(i, src.active, c, i % motif.len);
				Step st = src;
				st.gate = a.gate;
				st.accent = a.accent;
				st.slide = a.slide;
				st.tie = a.tie;
				out[(size_t) i] = st;
			}
		}
	}

	applyContourShape(out);
	applyEnding(out, c, false);
	postProcessArticulation(out, c, 0);
	normalisePhrase(out);
	return out;
}

Phrase Engine::buildResponse(const Phrase& callIn, const Cfg& c) {
	const int steps = c.steps;
	// guard (the JS assumes call.length == steps): repeat / cut the call to the phrase length
	Phrase callR;
	if ((int) callIn.size() != steps && !callIn.s.empty())
		for (int i = 0; i < steps; i++)
			callR.s.push_back(callIn[(size_t) (i % (int) callIn.size())]);
	const Phrase& call = callR.s.empty() ? callIn : callR;
	if (call.s.empty())
		return Phrase();
	Phrase out;
	out.s = call.s;  // call.map(s => ({...s}))
	const std::string& rt = c.responseStrategy;
	SP sp = styleProfile(c);
	const size_t L = c.scale.size();
	const int motifLen = std::max(2, std::min(4, steps / 2));
	std::vector<Step> motif(call.s.begin(), call.s.begin() + std::min((size_t) motifLen, call.size()));
	const int mlen = (int) motif.size();
	std::vector<double> activeDegrees;
	for (auto& s : call.s)
		if (s.active && !isNull(s.degree))
			activeDegrees.push_back(s.degree);
	double avgDeg = 0;
	if (!activeDegrees.empty()) {
		for (double d : activeDegrees)
			avgDeg += d;
		avgDeg /= activeDegrees.size();
	}

	std::vector<int> respPool = notePool(c);
	if (c.notePoolMode == "Pool expands on response") {
		respPool.clear();
		for (int i = 0; i < std::min((int) L, 7); i++)
			respPool.push_back(i);
	}
	else if (c.notePoolMode == "Pool contracts on response")
		respPool = stableDegreesForScale(L);

	const double callContour = phraseContour(call);
	const std::vector<int> stable = stableDegreesForScale(L);
	std::vector<double> stableD(stable.begin(), stable.end());
	if (stableD.empty())
		stableD = {0};
	std::vector<bool> copyRhythm;
	for (auto& s : call.s)
		copyRhythm.push_back(s.active);

	// CONTRAST macro
	sp.responseTightness = clampJS(1 - c.contrast, 0.04, 0.96);
	sp.motifVar = clampJS(sp.motifVar + c.contrast * 0.35, 0, 0.9);
	sp.leap = clampJS(sp.leap + c.contrast * 0.25, 0, 0.9);

	const double lenD = std::max(1.0, (double) L);
	auto mapMotif = [&](auto fn) {
		for (int i = 0; i < steps; i++) {
			const Step& base = motif[(size_t) (i % mlen)];
			out[(size_t) i] = base;
			if (out[(size_t) i].active && !isNull(out[(size_t) i].degree)) {
				double d = fn(base.degree, i);
				if (chance(sp.responseTightness * 0.28) && !respPool.empty())
					d = randInt(respPool) + std::floor(d / lenD) * lenD;
				out[(size_t) i].degree = clampJS(d, 0, 10);
			}
			else
				out[(size_t) i].degree = NUL;
		}
	};
	auto copyFeel = [&](int i) {
		out[(size_t) i].gate = call[(size_t) i].gate;
		out[(size_t) i].slide = call[(size_t) i].slide;
		out[(size_t) i].tie = call[(size_t) i].tie;
		out[(size_t) i].accent = call[(size_t) i].accent;
	};
	auto jitter = [&](double d, int) { return d + rand({-1, 0, 1}); };

	if (rt == "Same rhythm + resolve") {
		for (int i = 0; i < steps; i++) {
			out[(size_t) i].active = copyRhythm[(size_t) i];
			out[(size_t) i].degree = out[(size_t) i].active ? clampJS(num(call[(size_t) i].degree) + rand({-1, 0, 0, 1}), 0, 10) : NUL;
			if (out[(size_t) i].active) copyFeel(i);
		}
	}
	else if (rt == "Same rhythm + new contour") {
		const double dir = callContour >= 0 ? -1 : 1;
		for (int i = 0; i < steps; i++) {
			out[(size_t) i].active = copyRhythm[(size_t) i];
			out[(size_t) i].degree = out[(size_t) i].active
			                             ? clampJS(num(call[(size_t) i].degree) + dir * std::floor(i / std::max(2.0, steps / 4.0)) + rand({-1, 0, 1}), 0, 10)
			                             : NUL;
			if (out[(size_t) i].active) copyFeel(i);
		}
	}
	else if (rt == "Motif + simplify") {
		mapMotif([&](double d, int) { return d + rand({-1, 0, 0, 1}); });
		for (int i = 0; i < steps; i++) {
			out[(size_t) i].active = out[(size_t) i].active && chance(i % 2 == 0 ? 1 : 0.35);
			if (!out[(size_t) i].active) out[(size_t) i].degree = NUL;
		}
	}
	else if (rt == "Motif + extend") {
		mapMotif(jitter);
		for (int i = 1; i < steps; i++) {
			if (!out[(size_t) i].active && out[(size_t) i - 1].active && chance(0.42)) {
				out[(size_t) i].active = true;
				out[(size_t) i].degree = out[(size_t) i - 1].degree;
				out[(size_t) i].tie = true;
				out[(size_t) i].gate = std::max(out[(size_t) i].gate ? out[(size_t) i].gate : 0.8, 1.0);
			}
		}
	}
	else if (rt == "Motif + truncate") {
		mapMotif(jitter);
		for (int i = (int) std::floor(steps * 0.65); i < steps; i++)
			if (chance(0.65)) {
				out[(size_t) i].active = false;
				out[(size_t) i].degree = NUL;
			}
	}
	else if (rt == "Echo" || rt == "Delayed answer") {
		for (int i = 0; i < steps; i++) {
			const Step& src = call[(size_t) ((i - 2 + steps) % steps)];
			out[(size_t) i].active = src.active;
			out[(size_t) i].degree = out[(size_t) i].active ? clampJS(num(src.degree) + rand({-1, 0, 1}), 0, 10) : NUL;
		}
		if (rt == "Delayed answer") {
			for (int i = steps - 1; i > 0; i--)
				out[(size_t) i] = out[(size_t) i - 1];
			Step z;
			z.active = false;
			z.degree = NUL;
			z.gate = 0.6;
			out[0] = z;
		}
	}
	else if (rt == "Answer upward" || rt == "Higher answer") mapMotif([&](double d, int) { return d + rand({1, 2, 3}); });
	else if (rt == "Answer downward" || rt == "Lower answer") mapMotif([&](double d, int) { return d - rand({1, 1, 2}); });
	else if (rt == "Invert contour" || rt == "Mirror contour loosely")
		mapMotif([&](double d, int) { return std::round(avgDeg - (d - avgDeg)) + rand({-1, 0, 1}); });
	else if (rt == "Copy rhythm / change notes") {
		for (int i = 0; i < steps; i++) {
			out[(size_t) i].active = copyRhythm[(size_t) i];
			out[(size_t) i].degree = out[(size_t) i].active ? clampJS(randInt(respPool) + rand({0, 0, 2}), 0, 10) : NUL;
		}
	}
	else if (rt == "Copy notes / change rhythm") {
		for (int i = 0; i < steps; i++) {
			out[(size_t) i].degree = call[(size_t) i].degree;
			out[(size_t) i].active = chance(call[(size_t) i].active ? 0.68 : 0.22);
			if (!out[(size_t) i].active) out[(size_t) i].degree = NUL;
		}
	}
	else if (rt == "Transpose motif up") mapMotif([&](double d, int) { return d + rand({2, 3, 4}); });
	else if (rt == "Transpose motif down") mapMotif([&](double d, int) { return d - rand({2, 2, 3}); });
	else if (rt == "Response resolves to root") {
		mapMotif(jitter);
		for (int i = std::max(0, steps - 3); i < steps; i++)
			if (out[(size_t) i].active) out[(size_t) i].degree = rand(filt({0, 0, 2}, L));
	}
	else if (rt == "Response resolves to 5th") {
		mapMotif(jitter);
		for (int i = std::max(0, steps - 3); i < steps; i++)
			if (out[(size_t) i].active) out[(size_t) i].degree = rand(filt({4, 2, 0}, L));
	}
	else if (rt == "Response resolves lower") {
		mapMotif([&](double d, int) { return d - rand({1, 2}); });
		for (int i = std::max(0, steps - 2); i < steps; i++)
			if (out[(size_t) i].active) out[(size_t) i].degree = clampJS(num(out[(size_t) i].degree) - 1, 0, 10);
	}
	else if (rt == "Response resolves harder") {
		mapMotif(jitter);
		for (int i = std::max(0, steps - 4); i < steps; i++)
			if (out[(size_t) i].active) {
				out[(size_t) i].degree = rand(stableD);
				out[(size_t) i].accent = true;
			}
	}
	else if (rt == "Repeat then break") {
		mapMotif(jitter);
		for (int i = 0; i < steps - 2; i++)
			if (chance(0.72)) out[(size_t) i] = call[(size_t) i];
		if (steps >= 2 && out[(size_t) steps - 2].active)
			out[(size_t) steps - 2].degree = clampJS(num(out[(size_t) steps - 2].degree) + rand({2, 3, -2}), 0, 10);
		if (out[(size_t) steps - 1].active) out[(size_t) steps - 1].degree = rand(stableD);
	}
	else if (rt == "Tighter answer") {
		mapMotif(jitter);
		for (int i = 0; i < steps; i++)
			if (out[(size_t) i].active) {
				out[(size_t) i].gate = 0.48 + (i % 2 == 0 ? 0.04 : 0);
				out[(size_t) i].accent = chance(0.3);
			}
	}
	else if (rt == "Sparser answer") {
		mapMotif(jitter);
		for (int i = 0; i < steps; i++) {
			out[(size_t) i].active = out[(size_t) i].active && chance(i % 2 == 0 ? 0.85 : 0.22);
			if (!out[(size_t) i].active) out[(size_t) i].degree = NUL;
		}
	}
	else if (rt == "Busier answer") {
		mapMotif(jitter);
		for (int i = 0; i < steps; i++)
			if (!out[(size_t) i].active && chance(0.3)) {
				out[(size_t) i].active = true;
				const double pd = out[(size_t) std::max(0, i - 1)].degree;
				out[(size_t) i].degree = isNull(pd) ? (double) randInt(respPool) : pd;
			}
	}
	else if (rt == "Accent the answer") {
		mapMotif(jitter);
		for (int i = 0; i < steps; i++)
			out[(size_t) i].accent = out[(size_t) i].active && (i % 2 == 0 || chance(0.22));
	}
	else if (rt == "Slide the answer") {
		mapMotif(jitter);
		for (int i = 0; i < steps; i++) {
			out[(size_t) i].slide = out[(size_t) i].active && chance(0.34);
			out[(size_t) i].gate = std::max(out[(size_t) i].gate ? out[(size_t) i].gate : 0.8, 1.0);
		}
	}
	else if (rt == "Staccato answer") {
		mapMotif(jitter);
		for (int i = 0; i < steps; i++) {
			out[(size_t) i].gate = 0.42;
			out[(size_t) i].tie = false;
		}
	}
	else if (rt == "Legato answer") {
		mapMotif(jitter);
		for (int i = 0; i < steps; i++) {
			out[(size_t) i].gate = 1.08;
			out[(size_t) i].tie = out[(size_t) i].active && chance(0.18);
		}
	}
	else if (rt == "Half-phrase answer") {
		mapMotif(jitter);
		for (int i = 0; i < steps / 2; i++) {
			out[(size_t) i].active = false;
			out[(size_t) i].degree = NUL;
		}
	}
	else if (rt == "Fragment reply" || rt == "Tail answer") {
		const int start = rt == "Tail answer" ? steps / 2 : steps / 3;
		for (int i = 0; i < steps; i++) {
			const Step& src = call[(size_t) ((start + i) % steps)];
			out[(size_t) i].active = src.active && chance(0.88);
			out[(size_t) i].degree = out[(size_t) i].active ? clampJS(num(src.degree) + rand({-1, 0, 1}), 0, 10) : NUL;
		}
	}
	else if (rt == "Opposite register") mapMotif([&](double d, int) { return d + (c.registerProfile == "Wide register" ? -3 : 3); });
	else if (rt == "Same motif darker") mapMotif([&](double d, int) { return d - 1; });
	else if (rt == "Same motif brighter") mapMotif([&](double d, int) { return d + 1; });
	else if (rt == "Question then closure") {
		mapMotif(jitter);
		std::vector<double> qEnd = filt({1, 3, 5}, L);
		if (qEnd.empty())
			qEnd = {1};
		if (steps >= 2 && out[(size_t) steps - 2].active) out[(size_t) steps - 2].degree = rand(qEnd);
		if (out[(size_t) steps - 1].active) out[(size_t) steps - 1].degree = rand(stableD);
	}
	else if (rt == "Hook then fill") {
		for (int i = 0; i < steps; i++) {
			if (i < steps / 2)
				out[(size_t) i] = call[(size_t) i];
			else {
				const Step& src = motif[(size_t) (i % mlen)];
				out[(size_t) i] = src;
				if (out[(size_t) i].active && !isNull(out[(size_t) i].degree))
					out[(size_t) i].degree = clampJS(src.degree + rand({-2, -1, 1, 2}), 0, 10);
			}
		}
	}
	else if (rt == "Call dominant / response tonic") {
		mapMotif(jitter);
		for (int i = 0; i < steps; i++) {
			if (!out[(size_t) i].active) continue;
			out[(size_t) i].degree = chance(0.78) ? 0 : rand(filt({0, 0, 2}, L));
		}
	}
	else
		mapMotif(jitter);

	for (int i = 0; i < steps; i++) {
		if (out[(size_t) i].active && call[(size_t) i].active && chance(sp.responseTightness))
			out[(size_t) i].degree = clampJS(std::round((num(out[(size_t) i].degree) + num(call[(size_t) i].degree)) / 2), 0, 10);
		const Art a = articulationForStep(i, out[(size_t) i].active, c, i % mlen);
		// {...out[i], ...a, gate: out[i].gate ?? a.gate}: the gate stays, the rest re-rolls
		out[(size_t) i].accent = a.accent;
		out[(size_t) i].slide = a.slide;
		out[(size_t) i].tie = a.tie;
		if (!out[(size_t) i].active) out[(size_t) i].degree = NUL;
	}

	// CONTRAST, second half
	if (c.contrast > 0.5) {
		const double p = (c.contrast - 0.5) * 2 * 0.4;
		for (int i = 0; i < steps; i++) {
			if (!chance(p)) continue;
			if (out[(size_t) i].active) {
				out[(size_t) i].active = false;
				out[(size_t) i].degree = NUL;
			}
			else {
				out[(size_t) i].active = true;
				const double nd = out[(size_t) std::max(0, i - 1)].degree;
				out[(size_t) i].degree = !isNull(nd) ? nd : (double) randInt(respPool);
			}
		}
	}
	else if (c.contrast < 0.5) {
		const double p = (0.5 - c.contrast) * 2 * 0.6;
		for (int i = 0; i < steps; i++) {
			if (!chance(p)) continue;
			if (out[(size_t) i].active == call[(size_t) i].active) continue;
			out[(size_t) i].active = call[(size_t) i].active;
			if (out[(size_t) i].active && isNull(out[(size_t) i].degree)) out[(size_t) i].degree = call[(size_t) i].degree;
			if (!out[(size_t) i].active) out[(size_t) i].degree = NUL;
		}
	}

	if ((c.notePoolMode == "Pool contracts on response" || c.notePoolMode == "Pool expands on response") && !respPool.empty()) {
		for (int i = 0; i < steps; i++) {
			if (!out[(size_t) i].active || isNull(out[(size_t) i].degree)) continue;
			if (!chance(0.75)) continue;
			const double oct = std::floor(out[(size_t) i].degree / lenD), within = out[(size_t) i].degree - oct * lenD;
			double best = respPool[0], bd = 99;
			for (int k : respPool) {
				const double d = std::abs(std::fmod((double) k, lenD) - within);
				if (d < bd) {
					bd = d;
					best = std::fmod((double) k, lenD);
				}
			}
			out[(size_t) i].degree = clampJS(best + oct * lenD, 0, 11);
		}
	}

	applyEnding(out, c, true);
	if (rt != "Same rhythm + resolve" && rt != "Same rhythm + new contour")
		postProcessArticulation(out, c, 0);
	normalisePhrase(out);
	return out;
}

int Engine::stepPitch(const Phrase& ph, int i, const Params& p) const {
	if (i < 0 || i >= (int) ph.size())
		return -1;
	const Step& s = ph[(size_t) i];
	if (!s.active || isNull(s.degree))
		return -1;
	const auto& sc = scaleDefs()[(size_t) std::max(0, std::min((int) scaleDefs().size() - 1, p.scale))].iv;
	const double m = degreeToMidi(s.degree + s.oct * (double) sc.size(), sc, kOctaves[(size_t) std::max(0, std::min(6, p.rootOctave))],
	                              kKeys[(size_t) std::max(0, std::min(11, p.key))]);
	if (!std::isfinite(m))
		return -1;
	return foldPitch((int) m);
}

void Engine::enforceTies(Phrase& ph, const Cfg& c) {
	auto pitchOf = [&](const Step& s) -> double {
		if (!s.active || isNull(s.degree))
			return NUL;
		const double m = degreeToMidi(s.degree + s.oct * (double) c.scale.size(), c.scale, c.rootOctave, c.key);
		return std::isfinite(m) ? (double) foldPitch((int) m) : NUL;
	};
	auto same = [](double a, double b) { return (std::isnan(a) && std::isnan(b)) || a == b; };  // JS null === null
	const int n = (int) ph.size();
	auto nextActive = [&](int i) {
		int j = i + 1;
		while (j < n && !ph[(size_t) j].active)
			j++;
		return j < n ? j : -1;
	};
	for (int i = 0; i < n - 1; i++) {
		Step& s = ph[(size_t) i];
		if (!s.active)
			continue;
		if (s.tie && s.slide)
			s.tie = false;
		const int j = nextActive(i);
		if (j < 0) {
			s.tie = false;
			s.slide = false;
			continue;
		}
		if (s.tie) {
			ph[(size_t) j].degree = s.degree;
			ph[(size_t) j].oct = s.oct;
			continue;
		}
		if (s.slide) {
			if (j != i + 1) {
				s.slide = false;
				continue;
			}
			if (!same(pitchOf(ph[(size_t) j]), pitchOf(s)))
				continue;
			const double ceilDeg = registerCeiling(c.registerProfile, c.scale);
			ph[(size_t) j].oct = s.oct;
			const double up = clampJS(num(s.degree) + 1, 0, ceilDeg), dn = clampJS(num(s.degree) - 1, 0, ceilDeg);
			ph[(size_t) j].degree = (up != s.degree) ? up : dn;
			if (same(pitchOf(ph[(size_t) j]), pitchOf(s)))
				s.slide = false;
		}
	}
}

void Engine::forgetDensityMemory(Phrase& ph) {
	for (auto& s : ph.s) {
		s.was = Was();
		s.hasRank = true;
		s.rank = random();
	}
	ph.silenced.clear();
}

void Engine::restoreDensity(Phrase& ph, const Cfg& c) {
	const int want = std::max(1, std::min(c.steps, (int) std::lround(c.steps * (0.12 + c.densityTarget * 0.88))));
	const std::string cat0 = styleCategory(c.style);
	const SP sp0 = styleProfile(c);
	const int n = (int) ph.size();
	for (auto& s : ph.s)
		if (!s.hasRank) {
			s.hasRank = true;
			s.rank = random();
		}
	auto wt = [&](int i) { return preferredStepActivation(cat0, i, sp0.offbeatBias) + ph[(size_t) i].rank * 0.15; };
	int live = 0;
	for (auto& s : ph.s)
		live += s.active;
	int guard = 0;
	while (live < want && guard++ < 200) {
		std::vector<int> off;
		for (int i = 0; i < n; i++)
			if (!ph[(size_t) i].active)
				off.push_back(i);
		if (off.empty())
			break;
		std::stable_sort(off.begin(), off.end(), [&](int a, int b) { return wt(b) < wt(a); });
		const int i = off[0];
		ph[(size_t) i].active = true;
		if (isNull(ph[(size_t) i].degree)) {
			const double common = commonDegree(ph);
			const double nd = (i > 0 && !isNull(ph[(size_t) i - 1].degree)) ? ph[(size_t) i - 1].degree
			                  : ((i < n - 1 && !isNull(ph[(size_t) i + 1].degree)) ? ph[(size_t) i + 1].degree : common);
			ph[(size_t) i].degree = nd;
		}
		const Art a = articulationForStep(i, true, c, i % 4);
		ph[(size_t) i].gate = a.gate;
		ph[(size_t) i].accent = a.accent;
		ph[(size_t) i].slide = a.slide;
		ph[(size_t) i].tie = a.tie;
		live++;
	}
	while (live > want && guard++ < 200) {
		std::vector<int> on;
		for (int i = 0; i < n; i++)
			if (ph[(size_t) i].active)
				on.push_back(i);
		if (on.size() <= 1)
			break;
		std::stable_sort(on.begin(), on.end(), [&](int a, int b) { return wt(a) < wt(b); });
		const int i = on[0];
		ph[(size_t) i].active = false;
		ph[(size_t) i].degree = NUL;
		live--;
	}
}

Phrase Engine::mutatePhrase(const Phrase& phrase, const Cfg& c, std::string target, int stepsOverride) {
	if (target == "Call + response linked")
		target = "Whole phrase";
	Phrase out = phrase;
	out.silenced.clear();  // phrase.map() does not copy __silenced
	const int n = (int) out.size();
	int amt;
	if (stepsOverride > 0)
		amt = std::max(1, std::min(n, stepsOverride));
	else {
		const std::string& a = c.mutationAmount;
		amt = a == "Tiny" ? 1 : a == "Small" ? 2 : a == "Medium" ? 3 : a == "Strong" ? 5 : a == "Brutal" ? std::max(4, (int) std::floor(n * 0.7)) : 3;
	}
	std::vector<int> all;
	for (int i = 0; i < n; i++)
		all.push_back(i);
	const std::vector<int> idxs = pickN(all, amt);
	for (int i : idxs) {
		Step& s = out[(size_t) i];
		if (target == "Rhythm only" || target == "Whole phrase" || target == "Call only" || target == "Response only")
			if (chance(0.7)) s.active = !s.active;
		if ((target == "Pitch only" || target == "Whole phrase" || target == "Call only" || target == "Response only") && s.active)
			s.degree = clampJS(num(s.degree) + rand({-2, -1, 1, 2}), 0, 10);
		if (target == "Articulation only" || target == "Whole phrase") {
			const Art a = articulationForStep(i, s.active, c, i % 4);
			s.gate = a.gate;
			s.accent = a.accent;
			s.slide = a.slide;
			s.tie = a.tie;
		}
		if (target == "Ending only" && i == n - 1)
			applyEnding(out, c, false);
		if (target == "Motif only") {
			const int src = i % std::max(2, n / 4);
			out[(size_t) i] = out[(size_t) src];
		}
		if (!out[(size_t) i].active)
			out[(size_t) i].degree = NUL;
	}
	restoreDensity(out, c);
	settlePhrase(out, c);
	return out;
}

void Engine::settlePhrase(Phrase& ph, const Cfg& c) {
	enforceTies(ph, c);
	normalisePhrase(ph);
}

// =============================================================================================
// 4. Whole-state operations
// =============================================================================================

void Engine::clearVariants() {
	S.variants[0].clear();
	S.variants[1].clear();
	S.hasVariants = false;
}

void Engine::applyOctaveJump() {
	const Cfg c = cfg();
	const double p = c.octaveJump;
	for (Phrase* ph : {&S.call, &S.response}) {
		std::vector<int> live;
		for (int i = 0; i < (int) ph->size(); i++) {
			(*ph)[(size_t) i].oct = 0;
			if ((*ph)[(size_t) i].active)
				live.push_back(i);
		}
		const int want = (int) std::lround(live.size() * p * 0.6);
		for (int i : pickN(live, want))
			(*ph)[(size_t) i].oct = chance(0.5) ? 1 : -1;
		settlePhrase(*ph, c);
	}
}

void Engine::applyDensity() {
	clearVariants();
	const Cfg c = cfg();
	const std::string cat = styleCategory(c.style);
	const int base = std::max(1, std::min(c.steps, (int) std::lround(c.steps * (0.12 + c.densityTarget * 0.88))));
	double bias[2] = {1, 1};
	if (c.phraseMode == "Call sparse, response busy") { bias[0] = 0.6; bias[1] = 1.4; }
	else if (c.phraseMode == "Call busy, response sparse") { bias[0] = 1.4; bias[1] = 0.6; }
	const SP sp0 = styleProfile(c);

	for (int lane = 0; lane < 2; lane++) {
		Phrase& p = lane == 0 ? S.call : S.response;
		if (p.s.empty())
			continue;
		const int want = std::max(1, std::min(c.steps, (int) std::lround(base * bias[lane])));
		const int last = (int) p.size() - 1;
		std::vector<int> live, dead;
		for (int i = 0; i < (int) p.size(); i++)
			(p[(size_t) i].active ? live : dead).push_back(i);
		for (auto& s : p.s)
			if (!s.hasRank) {
				s.hasRank = true;
				s.rank = random();
			}
		auto weight = [&](int i) { return preferredStepActivation(cat, i, sp0.offbeatBias) + p[(size_t) i].rank * 0.15; };
		std::stable_sort(dead.begin(), dead.end(), [&](int a, int b) { return weight(b) < weight(a); });
		std::stable_sort(live.begin(), live.end(), [&](int a, int b) { return weight(a) < weight(b); });

		while ((int) live.size() < want && (!p.silenced.empty() || !dead.empty())) {
			int i;
			if (!p.silenced.empty()) {
				i = p.silenced.back();
				p.silenced.pop_back();
			}
			else {
				i = dead.front();
				dead.erase(dead.begin());
			}
			if (i < 0 || i > last || p[(size_t) i].active)
				continue;
			auto di = std::find(dead.begin(), dead.end(), i);
			if (di != dead.end())
				dead.erase(di);
			Step& s = p[(size_t) i];
			s.active = true;
			auto si = std::find(p.silenced.begin(), p.silenced.end(), i);
			if (si != p.silenced.end())
				p.silenced.erase(si);
			if (s.was.has) {
				s.degree = s.was.degree;
				s.gate = s.was.gate;
				s.accent = s.was.accent;
				s.slide = s.was.slide;
				s.tie = s.was.tie;
				s.was = Was();
			}
			else {
				if (isNull(s.degree)) {
					const double common = commonDegree(p);
					s.degree = (i > 0 && !isNull(p[(size_t) i - 1].degree)) ? p[(size_t) i - 1].degree
					           : ((i < last && !isNull(p[(size_t) i + 1].degree)) ? p[(size_t) i + 1].degree : common);
				}
				const Art a = articulationForStep(i, true, c, i % std::max(2, c.steps / 2));
				s.gate = a.gate;
				s.accent = a.accent;
				s.slide = a.slide;
				s.tie = a.tie;
			}
			live.push_back(i);
		}
		while ((int) live.size() > want && live.size() > 1) {
			const int i = live.front();
			live.erase(live.begin());
			if (i == last) {
				live.push_back(i);
				if ((int) live.size() <= want)
					break;
				continue;
			}
			Step& s = p[(size_t) i];
			if (!isNull(s.degree)) {
				s.was.has = true;
				s.was.degree = s.degree;
				s.was.gate = s.gate;
				s.was.accent = s.accent;
				s.was.slide = s.slide;
				s.was.tie = s.tie;
			}
			p.silenced.push_back(i);
			s.active = false;
			s.degree = NUL;
			s.accent = s.slide = s.tie = false;
		}
		settlePhrase(p, c);
	}
}

void Engine::resizePhrases() {
	clearVariants();
	const int want = std::max(kMinSteps, std::min(kMaxSteps, S.p.steps));
	for (Phrase* p : {&S.call, &S.response}) {
		if (p->s.empty())
			continue;
		const int old = (int) p->size();
		if (want == old)
			continue;
		Phrase out;
		for (int i = 0; i < want; i++)
			out.s.push_back((*p)[(size_t) (i % old)]);
		*p = out;
	}
}

void Engine::reapplyArticulation(int fields) {
	const Cfg c = cfg();
	for (Phrase* p : {&S.call, &S.response}) {
		if (p->s.empty())
			continue;
		int motifPos = 0;
		for (int i = 0; i < (int) p->size(); i++) {
			Step& s = (*p)[(size_t) i];
			if (!s.active)
				continue;
			const Art a = articulationForStep(i, true, c, motifPos);
			if (fields & F_GATE) s.gate = a.gate;
			if (fields & F_ACCENT) s.accent = a.accent;
			if (fields & F_SLIDE) s.slide = a.slide;
			if (fields & F_TIE) s.tie = a.tie;
			motifPos = (motifPos + 1) % 4;
		}
		postProcessArticulation(*p, c, fields);
		settlePhrase(*p, c);
	}
}

void Engine::reapplyEnding() {
	const Cfg c = cfg();
	if (!S.call.s.empty()) {
		applyEnding(S.call, c, false);
		normalisePhrase(S.call);
	}
	if (!S.response.s.empty()) {
		applyEnding(S.response, c, true);
		normalisePhrase(S.response);
	}
}

void Engine::applyArtPreset() {
	const std::string& name = kArticulation[(size_t) S.p.articulationProfile];
	for (const auto& ap : ART_PRESETS)
		if (name == ap.name) {
			S.p.gateFeel = indexOf(kGateFeels, ap.gate);
			S.p.connectionFeel = indexOf(kConnections, ap.conn);
			S.p.slideProfile = indexOf(kSlides, ap.slide);
			S.p.accentProfile = indexOf(kAccents, ap.accent);
		}
}

void Engine::generate(int which) {
	const Cfg c = cfg();
	if (which == 0 || which == 1)
		S.call = buildCall(c);
	if (S.call.s.empty())
		S.call = buildCall(c);
	if (which == 0 || which == 2)
		S.response = buildResponse(S.call, c);
	if (S.response.s.empty())
		S.response = buildResponse(S.call, c);
	clearVariants();
	forgetDensityMemory(S.call);
	forgetDensityMemory(S.response);
	applyDensity();
	applyOctaveJump();
	enforceTies(S.call, c);
	enforceTies(S.response, c);
	if (c.responseStrategy == "Call dominant / response tonic") {
		const int fifth = fifthDegree(c.scale);
		for (auto& s : S.call.s) {
			if (!s.active || isNull(s.degree))
				continue;
			if (chance(0.78))
				s.degree = fifth;
		}
	}
	if (c.responseStrategy == "Same rhythm + resolve" || c.responseStrategy == "Same rhythm + new contour") {
		for (size_t i = 0; i < S.response.size() && i < S.call.size(); i++) {
			Step &cs = S.call[i], &rs = S.response[i];
			if (cs.active && !rs.active) {
				rs.active = true;
				if (isNull(rs.degree))
					rs.degree = cs.degree;
			}
			else if (!cs.active && rs.active) {
				rs.active = false;
				rs.degree = NUL;
			}
			if (!cs.active)
				continue;
			rs.gate = cs.gate;
			rs.tie = cs.tie;
			rs.accent = cs.accent;
			rs.slide = cs.slide;
		}
	}
}

// param() / setnum(): what a change requires (REGEN / RESPONSE / ENDING / ARTIC / DENSITY ...)
void Engine::applyParams(const Params& np) {
	const Params old = S.p;
	if (np == old)
		return;
	S.p = np;
	bool regenBoth = old.style != np.style || old.motifType != np.motifType || old.scale != np.scale ||
	                 old.registerProfile != np.registerProfile || old.densityPreset != np.densityPreset ||
	                 old.contourPreset != np.contourPreset || old.repetitionPreset != np.repetitionPreset ||
	                 old.notePoolMode != np.notePoolMode || old.contrast != np.contrast;
	if (old.steps != np.steps) {
		if (regenBoth)
			;  // rebuilt at the new length below
		else
			resizePhrases();
	}
	if (old.mutateAll != np.mutateAll) {  // one knob for both lanes
		S.p.driftCall = np.mutateAll;
		S.p.driftResponse = np.mutateAll;
	}
	if (old.articulationProfile != np.articulationProfile)
		applyArtPreset();  // sets the four menus; they are not "custom" edits
	else if (old.gateFeel != np.gateFeel || old.connectionFeel != np.connectionFeel || old.slideProfile != np.slideProfile ||
	         old.accentProfile != np.accentProfile)
		S.p.articulationProfile = 0;  // markArtCustom

	if (regenBoth) {
		generate(0);
		return;
	}
	if (old.responseStrategy != np.responseStrategy)
		generate(2);
	if (old.articulationProfile != np.articulationProfile)
		reapplyArticulation(F_GATE | F_ACCENT | F_SLIDE | F_TIE);
	else {
		int f = 0;
		if (old.gateFeel != np.gateFeel) f |= F_GATE;
		if (old.connectionFeel != np.connectionFeel) f |= F_GATE | F_TIE;
		if (old.slideProfile != np.slideProfile) f |= F_SLIDE;
		if (old.accentProfile != np.accentProfile) f |= F_ACCENT;
		if (f)
			reapplyArticulation(f);
	}
	if (old.endingBehaviour != np.endingBehaviour || old.resolve != np.resolve)
		reapplyEnding();
	if (old.density != np.density)
		applyDensity();
	if (old.octaveJump != np.octaveJump)
		applyOctaveJump();
	if ((int) S.call.size() != std::max(kMinSteps, std::min(kMaxSteps, S.p.steps)))
		generate(0);  // regenerateIfShapeChanged
}

void Engine::mutate(int which) {
	const Cfg c = cfg();
	const std::string& t = c.mutationTarget;
	if (which == 1 || which == 0) {
		const std::string tc = which == 1 ? ((t == "Response only" || t == "Call + response linked") ? "Whole phrase" : t)
		                                  : ((t == "Call only" || t == "Response only" || t == "Call + response linked") ? "Whole phrase" : t);
		S.call = mutatePhrase(S.call, c, tc, 0);
	}
	if (which == 2 || which == 0) {
		const std::string tr = which == 2 ? ((t == "Call only" || t == "Call + response linked") ? "Whole phrase" : t)
		                                  : ((t == "Call only" || t == "Response only" || t == "Call + response linked") ? "Whole phrase" : t);
		S.response = mutatePhrase(S.response, c, tr, 0);
	}
}

void Engine::shift(int which, int dir) {
	auto sh = [&](Phrase& p) {
		const int n = (int) p.size();
		if (n < 2)
			return;
		Phrase out;
		for (int i = 0; i < n; i++)
			out.s.push_back(p[(size_t) ((((i + dir) % n) + n) % n)]);
		p = out;
	};
	if (which == 0 || which == 1) sh(S.call);
	if (which == 0 || which == 2) sh(S.response);
}

void Engine::randEnum(int& field, int n) { field = (int) std::floor(random() * n); }

void Engine::dice() {
	pushUndo();
	randEnum(S.p.style, (int) kStyles.size());
	randEnum(S.p.motifType, (int) kMotifs.size());
	randEnum(S.p.responseStrategy, (int) kStrategies.size());
	randEnum(S.p.phraseMode, (int) kModes.size());
	randEnum(S.p.endingBehaviour, (int) kEndings.size());
	randEnum(S.p.registerProfile, (int) kRegisters.size());
	randEnum(S.p.contourPreset, (int) kContours.size());
	randEnum(S.p.repetitionPreset, (int) kRepetitions.size());
	randEnum(S.p.notePoolMode, (int) kNotePools.size());
	randEnum(S.p.articulationProfile, (int) kArticulation.size());
	randEnum(S.p.gateFeel, (int) kGateFeels.size());
	randEnum(S.p.connectionFeel, (int) kConnections.size());
	randEnum(S.p.slideProfile, (int) kSlides.size());
	randEnum(S.p.accentProfile, (int) kAccents.size());
	generate(0);
}

void Engine::preset303() {
	pushUndo();
	Params& p = S.p;
	p.style = indexOf(kStyles, "Motif Study");
	p.motifType = indexOf(kMotifs, "Acid nibble");
	p.responseStrategy = indexOf(kStrategies, "Same rhythm + resolve");
	p.phraseMode = indexOf(kModes, "Call then response");
	p.endingBehaviour = indexOf(kEndings, "Resolve to root");
	p.key = indexOf(kKeys, "A");
	p.scale = indexOf(kScales, "Minor Pentatonic");
	p.rootOctave = indexOf(kOctaves, "2");
	p.steps = 16;
	p.rate = indexOf(kRates, "1/16");
	p.registerProfile = indexOf(kRegisters, "Medium register");
	p.contourPreset = indexOf(kContours, "Flat");
	p.repetitionPreset = indexOf(kRepetitions, "Very repetitive");
	p.notePoolMode = indexOf(kNotePools, "Root-heavy");
	p.mutationAmount = indexOf(kMutationAmounts, "Small");
	p.articulationProfile = indexOf(kArticulation, "Custom");
	p.gateFeel = indexOf(kGateFeels, "Mixed");
	p.connectionFeel = indexOf(kConnections, "Mixed ties");
	p.slideProfile = indexOf(kSlides, "Occasional");
	p.accentProfile = indexOf(kAccents, "Heavy");
	p.noteLengthPct = 100;
	p.swing = 0;
	p.baseVelocity = 70;
	p.accentDepth = 75;
	p.density = 90;
	p.resolve = 70;
	p.octaveJump = 40;
	p.driftCall = p.driftResponse = p.mutateAll = 0;
	generate(0);
}

void Engine::keepRhythm() {
	const Cfg c = cfg();
	for (auto& s : S.call.s)
		s.degree = s.active ? clampJS(num(s.degree) + rand({-2, -1, 1, 2}), 0, 10) : NUL;
	S.call.silenced.clear();
	normalisePhrase(S.call);
	S.response = buildResponse(S.call, c);
}

void Engine::keepPitch() {
	const Cfg c = cfg();
	for (auto& s : S.call.s)
		s.active = chance(s.active ? 0.72 : 0.28);
	S.call.silenced.clear();
	normalisePhrase(S.call);
	S.response = buildResponse(S.call, c);
}

void Engine::keepMotif() {
	const Cfg c = cfg();
	const size_t ml = std::min<size_t>(4, S.call.size());
	if (!ml) {
		generate(0);
		return;
	}
	std::vector<Step> motif(S.call.s.begin(), S.call.s.begin() + (long) ml);
	Phrase out;
	for (int i = 0; i < c.steps; i++) {
		Step s = motif[(size_t) i % ml];
		const Art a = articulationForStep(i, s.active, c, i % (int) ml);
		s.gate = a.gate;
		s.accent = a.accent;
		s.slide = a.slide;
		s.tie = a.tie;
		out.s.push_back(s);
	}
	normalisePhrase(out);
	S.call = out;
	S.response = buildResponse(S.call, c);
}

void Engine::pushUndo() {
	undoStack.push_back({S.p, S.call.s, S.response.s});
	if (undoStack.size() > 24)
		undoStack.erase(undoStack.begin());
}

bool Engine::undo() {
	if (undoStack.empty())
		return false;
	const Undo u = undoStack.back();
	undoStack.pop_back();
	S.p = u.p;
	S.call.s = u.call;
	S.call.silenced.clear();
	S.response.s = u.response;
	S.response.silenced.clear();
	return true;
}

// =============================================================================================
// 5. Phrase modes, forms, drift
// =============================================================================================

struct Engine::Plan {
	std::vector<int> seq;   // 0 call, 1 response
	int interleave = -1;    // -1 none, 0 half, n cells
	bool flip = false;
	std::vector<bool> vary;
	int varyEvery = 1;
	std::vector<std::string> form;
	bool hasSeq() const { return interleave < 0; }
};

Engine::Plan Engine::passPlan(int mode) const {
	Plan p;
	const std::string m = kModes[(size_t) std::max(0, std::min((int) kModes.size() - 1, mode))];
	const int C = 0, R = 1;
	if (m == "Call only") p.seq = {C};
	else if (m == "Response only") p.seq = {R};
	else if (m == "Call then response") p.seq = {C, R};
	else if (m == "Response then call") p.seq = {R, C};
	else if (m == "Alternate half bars") p.interleave = 0;
	else if (m == "Alternate half bars, response first") { p.interleave = 0; p.flip = true; }
	else if (m == "Alternate 2-step cells") p.interleave = 2;
	else if (m == "Alternate 1-step cells") p.interleave = 1;
	else if (m == "Call, call, response") p.seq = {C, C, R};
	else if (m == "Call, response, response") p.seq = {C, R, R};
	else if (m == "Call, varied call, response") { p.seq = {C, C, R}; p.vary = {false, true, false}; }
	else if (m == "Call, response, varied response") { p.seq = {C, R, R}; p.vary = {false, false, true}; }
	else if (m == "A / B / A / C") { p.seq = {C, R, C, R}; p.form = {"A", "B", "A", "C"}; }
	else if (m == "A / A' / B / B'") { p.seq = {C, C, R, R}; p.form = {"A", "A'", "B", "B'"}; }
	else if (m == "Call with response fill") p.seq = {C, C, C, R};
	else if (m == "Mutating call, stable response") { p.seq = {C, R}; p.vary = {true, false}; }
	else if (m == "Stable call, mutating response") { p.seq = {C, R}; p.vary = {false, true}; }
	else if (m == "Both mutate slowly") { p.seq = {C, R}; p.vary = {true, true}; p.varyEvery = 4; }
	else p.seq = {C, R};
	return p;
}

int Engine::laneForStep(const Plan& plan, int pass, int i, int steps) const {
	if (!plan.hasSeq()) {
		const int cell = plan.interleave == 0 ? std::max(1, steps / 2) : plan.interleave;
		const bool first = (i / cell) % 2 == 0;
		return (first != plan.flip) ? 0 : 1;
	}
	return plan.seq[(size_t) (pass % (int) plan.seq.size())];
}

const Phrase& Engine::variantFor(const std::string& key, int lane, const Cfg& c) {
	if (key == "A")
		return lane == 0 ? S.call : S.response;
	auto& store = S.variants[lane];
	auto it = store.find(key);
	if (it == store.end()) {
		const Phrase& base = lane == 0 ? S.call : S.response;
		const int steps = S.p.steps ? S.p.steps : 8;
		const int amt = key.find('\'') != std::string::npos ? std::max(1, (int) std::lround(steps * 0.12))
		                                                    : std::max(2, (int) std::lround(steps * 0.35));
		S.hasVariants = true;
		it = store.emplace(key, mutatePhrase(base, c, "Whole phrase", amt)).first;
	}
	return it->second;
}

void Engine::endOfPass(const Plan& plan, const Cfg& c) {
	auto modeVaries = [&](int lane) {
		if (plan.vary.empty())
			return false;
		for (size_t i = 0; i < plan.vary.size(); i++) {
			if (!plan.vary[i])
				continue;
			const int l = plan.hasSeq() ? plan.seq[i] : 0;
			if (l == lane)
				return true;
		}
		return false;
	};
	const int nSteps = S.p.steps ? S.p.steps : 8;
	const int nextLane = laneForStep(plan, S.passIdx + 1, 0, nSteps);
	const bool interleaved = !plan.hasSeq();
	const double drift[2] = {S.p.driftCall, S.p.driftResponse};
	for (int lane = 0; lane < 2; lane++) {
		const double amt = std::max(0.0, std::min(1.0, drift[lane] / 100.0));
		if (amt <= 0 || modeVaries(lane))
			continue;
		if (!interleaved && nextLane != lane)
			continue;
		const int steps = std::max(1, (int) std::lround(amt * nSteps * 0.30));
		Phrase& ph = lane == 0 ? S.call : S.response;
		ph = mutatePhrase(ph, c, "Whole phrase", steps);
	}
	if (!plan.form.empty())
		return;
	if (!plan.vary.empty()) {
		const int every = plan.varyEvery ? plan.varyEvery : 1;
		if (S.passIdx % every == 0) {
			const int idx = S.passIdx % (int) plan.vary.size();
			if (plan.vary[(size_t) idx]) {
				const int lane = plan.hasSeq() ? plan.seq[(size_t) idx] : 0;
				Phrase& ph = lane == 0 ? S.call : S.response;
				ph = mutatePhrase(ph, c, "Whole phrase", 0);
			}
		}
	}
}

// =============================================================================================
// 6. Note rendering + native compile
// =============================================================================================

double Engine::swingOffset(int i, double stepLen) const {
	const double sw = S.p.swing;
	if (!sw || (i % 2) == 0)
		return 0;
	return stepLen * (sw / 100) * 0.5;
}

bool Engine::stepEvent(const Phrase& ph, int i, const Cfg& c, Ev& out) {
	if (i < 0 || i >= (int) ph.size())
		return false;
	const Step& s = ph[(size_t) i];
	if (!s.active || isNull(s.degree))
		return false;
	const double pm = degreeToMidi(s.degree + s.oct * (double) c.scale.size(), c.scale, c.rootOctave, c.key);
	if (!std::isfinite(pm))
		return false;
	const int pitch = foldPitch((int) pm);
	const double base = std::max(1.0, std::min(127.0, S.p.baseVelocity ? S.p.baseVelocity : 92.0));
	const double depth = std::max(0.0, std::min(100.0, S.p.accentDepth)) / 100.0;
	const int vel = s.accent ? (int) std::lround(std::max(1.0, std::min(127.0, base + (127 - base) * depth))) : (int) base;
	const double gateVal = (s.gate ? s.gate : 0.82) * (c.noteLengthPct / 100.0);
	int nextPitch = -1;
	if (i + 1 < (int) ph.size()) {
		const Step& nx = ph[(size_t) i + 1];
		if (nx.active && !isNull(nx.degree)) {
			const double nm = degreeToMidi(nx.degree + nx.oct * (double) c.scale.size(), c.scale, c.rootOctave, c.key);
			if (std::isfinite(nm))
				nextPitch = foldPitch((int) nm);
		}
	}
	double dur = gateVal;
	if (s.tie)
		dur = 1.0 + random() * 0.10;
	if (s.slide && nextPitch >= 0 && nextPitch != pitch)
		dur = std::max(dur, 1.05);
	if (!s.slide && !s.tie)
		dur = std::min(dur, 0.95);
	out.pitch = pitch;
	out.vel = vel;
	out.dur = std::max(0.08, dur);
	return true;
}

Compiled Engine::compile(const State& snap) {
	const State saved = S;
	S = snap;
	Compiled out;
	const Cfg c = cfg();
	const Plan plan = passPlan(S.p.phraseMode);
	const int passes = plan.hasSeq() ? (int) plan.seq.size() : 1;
	const int steps = c.steps;
	const double ticks = kRateBeats[std::max(0, std::min(5, S.p.rate))] * 480.0;
	const int cycle = snap.cycle;
	out.evolves = !plan.vary.empty() || S.p.driftCall > 0 || S.p.driftResponse > 0;

	struct Pend {
		int slot, lane, pitch, vel, lastStep;
		double duration, swing, start;
	};
	for (int pass = 0; pass < passes; pass++) {
		const int formLane = laneForStep(plan, pass, 0, steps);
		PassView view;
		view.call = S.call;
		view.response = S.response;
		view.lane = formLane;
		const Phrase* variant = nullptr;
		if (!plan.form.empty()) {
			variant = &variantFor(plan.form[(size_t) pass], formLane, c);
			view.variant = *variant;
			view.hasVariant = true;
		}
		out.views.push_back(view);
		std::vector<Pend> events;
		for (int i = 0; i < steps; i++) {
			const int lane = laneForStep(plan, pass, i, steps);
			const Phrase& phrase = variant ? *variant : (lane == 0 ? S.call : S.response);
			Ev ev;
			if (!stepEvent(phrase, i, c, ev))
				continue;
			const double sw = swingOffset(i, ticks), start = i * ticks + sw;
			if (!events.empty()) {
				Pend& prev = events.back();
				if (prev.lastStep == i - 1 && prev.lane == lane && i >= 1 && phrase[(size_t) i - 1].tie && prev.pitch == ev.pitch) {
					prev.duration = std::max(1.0, start + ev.dur * ticks - prev.start);
					prev.lastStep = i;
					continue;
				}
			}
			events.push_back({pass * steps + i, lane, ev.pitch, ev.vel, i, std::max(1.0, ev.dur * ticks), sw, start});
		}
		for (auto& e : events)
			out.rows.push_back({e.slot, e.lane, e.pitch, e.vel, e.duration, e.swing});
		if (out.evolves) {
			S.passIdx = cycle * passes + pass;
			endOfPass(plan, c);
		}
	}
	out.next = S;
	out.next.cycle = cycle + 1;
	out.steps = steps;
	out.total = passes * steps;
	out.ticks = ticks;
	out.sync = std::max(0, std::min(2, S.p.syncMode));
	out.snap = snap;
	if (!plan.form.empty()) {
		// fixed letter variants persist (snap.variants = packVariants())
		out.snap.variants[0] = out.next.variants[0];
		out.snap.variants[1] = out.next.variants[1];
		out.snap.hasVariants = out.next.hasVariants;
	}
	S = saved;
	return out;
}

} // namespace crg

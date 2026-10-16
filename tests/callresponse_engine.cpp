// Offline check of the C++ port of the Call & Response generator (engines/callresponse) against the
// ORIGINAL JavaScript engine: for each configuration in tests/callresponse_configs.json both are run
// N times and compared on the statistics in tests/callresponse_js_stats.txt (written by
// tests/callresponse_js_stats.js under node). Plus invariants. Not part of the plugin build.
//   g++ -std=c++17 -O2 -Iengines tests/callresponse_engine.cpp engines/callresponse/CrgEngine.cpp -o build/crg_test
//   ./build/crg_test tests/callresponse_configs.json tests/callresponse_js_stats.txt
#include "callresponse/CrgEngine.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace crg;

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

struct Config {
	std::string name, preset;
	std::vector<std::pair<std::string, std::string>> over;
};

// minimal reader for the regular JSON of callresponse_configs.json
static std::vector<Config> readConfigs(const std::string& path) {
	std::ifstream f(path);
	std::stringstream ss;
	ss << f.rdbuf();
	const std::string s = ss.str();
	std::vector<Config> out;
	size_t i = 0;
	auto skip = [&]() { while (i < s.size() && std::strchr(" \t\r\n,:", s[i])) i++; };
	auto str = [&]() {
		std::string r;
		i++;  // "
		while (i < s.size() && s[i] != '"') r += s[i++];
		i++;
		return r;
	};
	auto value = [&]() {
		skip();
		if (s[i] == '"') return str();
		std::string r;
		while (i < s.size() && (std::isdigit((unsigned char) s[i]) || s[i] == '.' || s[i] == '-')) r += s[i++];
		return r;
	};
	while (i < s.size()) {
		if (s[i] != '{') { i++; continue; }
		i++;
		Config c;
		for (;;) {
			skip();
			if (s[i] == '}') { i++; break; }
			const std::string k = str();
			skip();
			if (k == "over") {
				i++;  // {
				for (;;) {
					skip();
					if (s[i] == '}') { i++; break; }
					const std::string kk = str();
					c.over.push_back({kk, value()});
				}
			}
			else {
				const std::string v = value();
				if (k == "name") c.name = v;
				else if (k == "preset") c.preset = v;
			}
		}
		out.push_back(c);
	}
	return out;
}

static void setParam(Params& p, const std::string& k, const std::string& v) {
	struct E { const char* key; int Params::*f; const std::vector<std::string>* list; };
	static const E enums[] = {
	    {"style", &Params::style, &kStyles}, {"responseStrategy", &Params::responseStrategy, &kStrategies},
	    {"motifType", &Params::motifType, &kMotifs}, {"phraseMode", &Params::phraseMode, &kModes},
	    {"endingBehaviour", &Params::endingBehaviour, &kEndings}, {"key", &Params::key, &kKeys},
	    {"scale", &Params::scale, &kScales}, {"rootOctave", &Params::rootOctave, &kOctaves},
	    {"registerProfile", &Params::registerProfile, &kRegisters}, {"densityPreset", &Params::densityPreset, &kDensityPresets},
	    {"contourPreset", &Params::contourPreset, &kContours}, {"repetitionPreset", &Params::repetitionPreset, &kRepetitions},
	    {"mutationAmount", &Params::mutationAmount, &kMutationAmounts}, {"mutationTarget", &Params::mutationTarget, &kMutationTargets},
	    {"notePoolMode", &Params::notePoolMode, &kNotePools}, {"articulationProfile", &Params::articulationProfile, &kArticulation},
	    {"gateFeel", &Params::gateFeel, &kGateFeels}, {"connectionFeel", &Params::connectionFeel, &kConnections},
	    {"slideProfile", &Params::slideProfile, &kSlides}, {"accentProfile", &Params::accentProfile, &kAccents},
	    {"rate", &Params::rate, &kRates}, {"syncMode", &Params::syncMode, &kSyncModes}};
	for (auto& e : enums)
		if (k == e.key) { p.*(e.f) = indexOf(*e.list, v.c_str()); return; }
	struct D { const char* key; double Params::*f; };
	static const D nums[] = {{"noteLengthPct", &Params::noteLengthPct}, {"baseVelocity", &Params::baseVelocity},
	                         {"accentDepth", &Params::accentDepth}, {"swing", &Params::swing}, {"octaveJump", &Params::octaveJump},
	                         {"mutateAll", &Params::mutateAll}, {"density", &Params::density}, {"contrast", &Params::contrast},
	                         {"resolve", &Params::resolve}, {"driftCall", &Params::driftCall}, {"driftResponse", &Params::driftResponse}};
	for (auto& d : nums)
		if (k == d.key) { p.*(d.f) = std::stod(v); return; }
	if (k == "steps") p.steps = std::stoi(v);
}

int main(int argc, char** argv) {
	const std::string cfgPath = argc > 1 ? argv[1] : "tests/callresponse_configs.json";
	const std::string jsPath = argc > 2 ? argv[2] : "tests/callresponse_js_stats.txt";
	const auto configs = readConfigs(cfgPath);
	CHECK(configs.size() >= 8, "configs read (%zu)", configs.size());

	std::map<std::string, std::vector<double>> js;
	{
		std::ifstream f(jsPath);
		std::string line;
		while (std::getline(f, line)) {
			std::istringstream ls(line);
			std::string name;
			ls >> name;
			std::vector<double> v;
			double x;
			while (ls >> x) v.push_back(x);
			js[name] = v;
		}
	}
	CHECK(!js.empty(), "JS stats read from %s", jsPath.c_str());

	static const char* statNames[] = {"notes/slot", "mean pitch", "pitch sd", "accented", "dur/step", "call share", "active", "ties", "slides"};
	// tolerances: absolute for the fractions, semitones for pitch
	static const double tol[] = {0.06, 2.0, 1.2, 0.05, 0.08, 0.06, 0.06, 0.05, 0.03};
	Engine e(12345);
	const int N = 400;
	for (const auto& cfg : configs) {
		double rows = 0, slots = 0, pitch = 0, pitch2 = 0, acc = 0, dur = 0, call = 0, ties = 0, slides = 0, act = 0, steps = 0;
		bool finite = true, inRange = true;
		for (int k = 0; k < N; k++) {
			Params p = Params::defaults();
			if (cfg.preset == "acid303") {
				e.S.p = p;
				e.preset303();
				p = e.S.p;
			}
			for (auto& kv : cfg.over) setParam(p, kv.first, kv.second);
			e.S = State();
			e.S.p = p;
			e.generate(0);
			for (const Phrase* ph : {&e.S.call, &e.S.response})
				for (const Step& s : ph->s) {
					steps++;
					if (s.active) {
						act++;
						ties += s.tie;
						slides += s.slide;
						if (std::isnan(s.degree)) finite = false;
					}
				}
			const Compiled c = e.compile(e.S);
			slots += c.total;
			const double base = p.baseVelocity;
			for (const Row& r : c.rows) {
				rows++;
				pitch += r.pitch;
				pitch2 += (double) r.pitch * r.pitch;
				acc += r.vel > base;
				dur += r.durTicks / c.ticks;
				call += r.lane == 0;
				inRange = inRange && r.pitch >= 0 && r.pitch < 128 && r.vel >= 1 && r.vel <= 127 && r.slot >= 0 &&
				          r.slot < c.total && r.durTicks >= 1 && std::isfinite(r.durTicks);
			}
		}
		CHECK(finite && inRange, "%s: rows in range, active steps pitched", cfg.name.c_str());
		const double m = pitch / std::max(1.0, rows);
		const double mine[9] = {rows / slots, m, std::sqrt(std::max(0.0, pitch2 / std::max(1.0, rows) - m * m)), acc / std::max(1.0, rows),
		                        dur / std::max(1.0, rows), call / std::max(1.0, rows), act / steps, ties / std::max(1.0, act), slides / std::max(1.0, act)};
		auto it = js.find(cfg.name);
		if (it == js.end() || it->second.size() < 9) {
			CHECK(false, "no JS stats for %s", cfg.name.c_str());
			continue;
		}
		std::printf("%-26s", cfg.name.c_str());
		for (int s = 0; s < 9; s++) {
			const double d = std::abs(mine[s] - it->second[(size_t) s]);
			std::printf(" %s%.3f/%.3f", d > tol[s] ? "!" : "", mine[s], it->second[(size_t) s]);
			CHECK(d <= tol[s], "%s %s: C++ %.4f vs JS %.4f", cfg.name.c_str(), statNames[s], mine[s], it->second[(size_t) s]);
		}
		std::printf("\n");
	}

	// behaviour: mutate keeps the note count (DENSITY owns it), undo restores, forms replay letters
	{
		Engine g(7);
		g.S.p.steps = 16;
		g.generate(0);
		int before = 0;
		for (auto& s : g.S.call.s) before += s.active;
		g.pushUndo();
		const std::vector<Step> saved = g.S.call.s;
		g.mutate(1);
		int after = 0;
		for (auto& s : g.S.call.s) after += s.active;
		CHECK(after == before, "mutate keeps the count (%d -> %d)", before, after);
		CHECK(g.undo(), "undo");
		bool same = g.S.call.size() == saved.size();
		for (size_t i = 0; same && i < saved.size(); i++) same = saved[i].active == g.S.call[i].active;
		CHECK(same, "undo restores the call");

		Params p = g.S.p;
		p.density = 100;
		g.applyParams(p);
		int full = 0;
		for (auto& s : g.S.call.s) full += s.active;
		CHECK(full == 16, "density 100 fills every step (%d)", full);
		p.density = 0;
		g.applyParams(p);
		int sparse = 0;
		for (auto& s : g.S.call.s) sparse += s.active;
		CHECK(sparse <= 3, "density 0 is sparse (%d)", sparse);
		p.density = 100;
		g.applyParams(p);
		int back = 0;
		for (auto& s : g.S.call.s) back += s.active;
		CHECK(back == 16, "density back up restores (%d)", back);

		p.phraseMode = indexOf(kModes, "A / B / A / C");
		g.applyParams(p);
		const Compiled c1 = g.compile(g.S);
		CHECK(c1.total == 4 * 16 && c1.views.size() == 4, "form compiles four passes");
		// A (pass 0) and A (pass 2) play the same notes
		std::vector<int> a0, a2;
		for (auto& r : c1.rows) {
			if (r.slot < 16) a0.push_back(r.pitch * 1000 + r.slot);
			if (r.slot >= 32 && r.slot < 48) a2.push_back(r.pitch * 1000 + r.slot - 32);
		}
		CHECK(a0 == a2 && !a0.empty(), "form letter A replays");
		p.phraseMode = indexOf(kModes, "Both mutate slowly");
		p.driftCall = 50;
		g.applyParams(p);
		const Compiled c2 = g.compile(g.S);
		CHECK(c2.evolves && c2.next.cycle == 1, "drift evolves into a next cycle");
		g.dice();
		CHECK(!g.S.call.s.empty() && !g.S.response.s.empty(), "dice gives a pair");
	}

	std::printf("callresponse: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}

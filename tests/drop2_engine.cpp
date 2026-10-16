// Offline check of the vendored DROP II engine with the module's demo drop. Not part of the plugin build.
//   g++ -std=c++17 -O2 -Iengines tests/drop2_engine.cpp engines/drop2/Transform.cpp -o build/drop2_test && ./build/drop2_test
#include "drop2/Preview.h"
#include "drop2/Transform.h"
#include <cmath>
#include <cstdio>

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static dr2::Clip demoDrums() {
	dr2::Clip c;
	for (int bar = 0; bar < 4; ++bar) {
		const double o = bar * 4.0;
		c.notes.push_back({o, 0.25, 36, 1.0f});
		c.notes.push_back({o + 2.5, 0.25, 36, 0.9f});
		if (bar % 2 == 1) c.notes.push_back({o + 3.25, 0.25, 36, 0.7f});
		c.notes.push_back({o + 1.0, 0.25, 38, 1.0f});
		c.notes.push_back({o + 3.0, 0.25, 38, 1.0f});
		c.notes.push_back({o + 1.75, 0.25, 38, 0.3f});
		for (int k = 0; k < 8; ++k) c.notes.push_back({o + k * 0.5, 0.125, (k % 4 == 3) ? 46 : 42, 0.6f});
	}
	c.sort();
	return c;
}
static dr2::Clip demoBass() {
	dr2::Clip c;
	const int line[] = {31, 31, 34, 29};
	for (int bar = 0; bar < 4; ++bar) {
		const double o = bar * 4.0;
		c.notes.push_back({o, 1.5, line[bar], 0.9f});
		c.notes.push_back({o + 1.75, 0.5, line[bar] + 12, 0.8f});
		c.notes.push_back({o + 2.4, 1.0, line[bar], 0.85f});
	}
	return c;
}

int main() {
	const auto d = demoDrums(), b = demoBass();
	const auto roles = dr2::detectRoles(d);
	CHECK(roles.kick == 36 && roles.snare == 38, "GM roles %d %d", roles.kick, roles.snare);

	// learned (non-GM) pitches: the module records kick 36 / snare 38 / hat 42, still GM
	dr2::Settings s;
	s.identity = 1.f;
	auto r = dr2::generate(d, b, s, 1);
	CHECK(r.identity > 0.99f, "identity 1 should give drop 1 back (%.3f)", r.identity);

	s.identity = 0.f;
	s.halftime = s.flip = s.split = true;
	s.respInterval = 3;
	r = dr2::generate(d, b, s, 7);
	CHECK(r.identity < 0.8f, "identity 0 with all moves should depart (%.3f)", r.identity);
	CHECK(!r.bassB.notes.empty(), "split gives a response voice");
	CHECK(!r.bassA.notes.empty(), "call voice kept");
	int changed = 0;
	for (auto& n : r.drums.notes) changed += n.tag != dr2::Same;
	CHECK(changed > 0, "drums changed");
	bool inRange = true;
	for (auto& n : r.bassA.notes) inRange = inRange && n.pitch >= 0 && n.pitch < 128 && std::isfinite(n.start) && n.start >= 0 && n.start < 16.0;
	CHECK(inRange, "bass notes in range");

	// variants differ (the module's seeds)
	s.identity = 0.3f;
	int distinct = 0;
	float prev = -1.f;
	for (int v = 0; v < 4; v++) {
		auto rv = dr2::generate(d, b, s, 1u * 31u + (unsigned) v * 7919u + 1u);
		if (std::abs(rv.identity - prev) > 1e-4f) distinct++;
		prev = rv.identity;
	}
	CHECK(distinct >= 2, "variants should differ (%d)", distinct);

	// identity monotonic on average
	float hiSum = 0, loSum = 0;
	for (unsigned seed = 1; seed < 40; seed++) {
		s.identity = 0.9f; hiSum += dr2::generate(d, b, s, seed).identity;
		s.identity = 0.1f; loSum += dr2::generate(d, b, s, seed).identity;
	}
	CHECK(hiSum > loSum, "higher IDENTITY keeps more of drop 1 (%.2f vs %.2f)", hiSum / 39, loSum / 39);

	// preview kit: finite, bounded, non-silent
	dr2::Preview kit;
	kit.prepare(48000.0);
	kit.noteOn(dr2::Preview::Drums, dr2::Role::Kick, 36, 1.f);
	kit.noteOn(dr2::Preview::BassA, dr2::Role::Perc, 31, 0.9f);
	double peak = 0, energy = 0;
	bool finite = true;
	for (int i = 0; i < 48000; i++) {
		float l = 0, rr = 0;
		kit.render(&l, &rr, 0, 1, 0.7f);
		finite = finite && std::isfinite(l);
		peak = std::max(peak, (double) std::abs(l));
		energy += l * l;
		if (i == 20000) kit.noteOff(dr2::Preview::BassA, 31);
	}
	CHECK(finite, "kit finite");
	CHECK(peak <= 1.0 && energy > 10.0, "kit bounded and audible (peak %.2f energy %.1f)", peak, energy);

	std::printf("drop2: %d checks, %d failed (identity at 0 = %.2f, all moves)\n", checks, fails, r.identity);
	return fails ? 1 : 0;
}

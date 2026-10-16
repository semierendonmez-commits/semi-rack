// Offline check of the DUBPLATE arrangement core as the Rack module drives it (one
// Sequencer::step per 16th). Not part of the plugin build.
//   g++ -std=c++17 -O2 -Iengines tests/dubplate_engine.cpp engines/dubplate/*.cpp -o build/dubplate_test
//   ./build/dubplate_test
#include "dubplate/GenreTemplates.h"
#include "dubplate/PatternGenerator.h"
#include "dubplate/Random.h"
#include "dubplate/Sequencer.h"
#include <cmath>
#include <cstdio>

using namespace dubplate;

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static int laneOf(const Arrangement& a, Role r) {
	for (int i = 0; i < (int) a.lanes.size(); i++)
		if (a.lanes[(size_t) i].role == r) return i;
	return -1;
}

int main() {
	// juce::Random reference values (JUCE's 48-bit LCG): Random(1).nextInt() sequence
	{
		Random r(1);
		const int a = r.nextInt(), b = r.nextInt();
		// seed1 = (1 * 0x5deece66d + 11) & 2^48-1 = 0x5deece678 -> >> 16 = 0x5deec
		CHECK(a == 0x5deec, "Random first value %x", a);
		CHECK(b != a, "Random advances");
		Random f(7);
		bool inRange = true;
		for (int i = 0; i < 1000; i++) { const float x = f.nextFloat(); inRange = inRange && x >= 0.f && x < 1.f; }
		CHECK(inRange, "nextFloat in [0, 1)");
	}

	for (int g = 0; g < (int) Genre::numGenres; g++) {
		Arrangement a = GenreTemplates::makeTemplate((Genre) g);
		CHECK(!a.sections.empty() && !a.lanes.empty(), "%s template has sections and lanes", genreName((Genre) g));
		CHECK(a.bpm >= genreTempoRange((Genre) g).start && a.bpm <= genreTempoRange((Genre) g).end, "%s tempo %.0f in range", genreName((Genre) g), a.bpm);
		for (auto& l : a.lanes) CHECK(l.cells.size() == a.sections.size(), "cells per section");

		Sequencer seq;
		seq.setArrangement(&a);
		Macros m;
		m.energy = 0.5f;
		Hit hits[Sequencer::maxHits];
		const long long total = (long long) a.totalBars() * 16;
		std::vector<int> perLane(a.lanes.size(), 0);
		std::vector<int> perLaneDrop(a.lanes.size(), 0), perLaneIntro(a.lanes.size(), 0);
		bool sane = true;
		int fillHits = 0;
		for (long long k = 0; k < total; k++) {
			const int n = seq.step(k, m, hits, Sequencer::maxHits);
			const int si = seq.sectionAt(k);
			for (int i = 0; i < n; i++) {
				const Hit& h = hits[i];
				sane = sane && h.lane >= 0 && h.lane < (int) a.lanes.size() && h.vel > 0.f && h.vel <= 1.f && h.delay >= 0.f && h.delay < 1.f
				       && std::isfinite(h.semis) && h.slice < 16;
				perLane[(size_t) h.lane]++;
				if (si >= 0 && a.sections[(size_t) si].type == SectionType::Drop) perLaneDrop[(size_t) h.lane]++;
				if (si >= 0 && a.sections[(size_t) si].type == SectionType::Intro) perLaneIntro[(size_t) h.lane]++;
				if (h.delay == 0.5f || h.reverse) fillHits++;
			}
		}
		CHECK(sane, "%s hits are well formed", genreName((Genre) g));
		const int kick = laneOf(a, Role::Kick);
		CHECK(kick >= 0 && perLane[(size_t) kick] > a.totalBars(), "%s kick plays (%d hits / %d bars)", genreName((Genre) g), kick >= 0 ? perLane[(size_t) kick] : 0, a.totalBars());
		// DJ-friendly intro: no bass-bus hits in the intro
		bool introDrumsOnly = true;
		for (size_t i = 0; i < a.lanes.size(); i++)
			if (roleInfo(a.lanes[i].role).bus == Bus::Bass && perLaneIntro[i] > 0) introDrumsOnly = false;
		CHECK(introDrumsOnly, "%s intro is drums only", genreName((Genre) g));
		// the drop has bass
		int dropBass = 0;
		for (size_t i = 0; i < a.lanes.size(); i++)
			if (roleInfo(a.lanes[i].role).bus == Bus::Bass) dropBass += perLaneDrop[i];
		CHECK(dropBass > 0, "%s drop has bass (%d)", genreName((Genre) g), dropBass);
		CHECK(fillHits > 0, "%s has fills / rolls (%d)", genreName((Genre) g), fillHits);

		// ENERGY macro low mutes upper layers: fewer hits overall
		Macros low = m;
		low.energy = 0.05f;
		int nLow = 0, nMid = 0;
		for (long long k = 0; k < total; k++) {
			nLow += seq.step(k, low, hits, Sequencer::maxHits);
			nMid += seq.step(k, m, hits, Sequencer::maxHits);
		}
		CHECK(nLow < nMid, "%s low energy thins the arrangement (%d < %d)", genreName((Genre) g), nLow, nMid);

		// jump: request section 2 during bar 1 with a 4-bar quantise -> from bar 4 on the
		// arrangement plays section 2 from its start
		seq.resetPosition();
		for (long long k = 0; k < 20; k++) seq.step(k, m, hits, Sequencer::maxHits);
		seq.requestJump(2, 4);
		long long before = -1;
		seq.sectionAt(63, &before);
		for (long long k = 20; k <= 64; k++) seq.step(k, m, hits, Sequencer::maxHits);
		CHECK(before == 63, "no jump before the 4-bar line (step %lld)", before);
		long long arrStep = -1;
		const int siAfter = seq.sectionAt(64, &arrStep);
		CHECK(siAfter == 2 && arrStep == (long long) a.sectionStartBar(2) * 16, "%s jump lands on section 2 (section %d step %lld)", genreName((Genre) g), siAfter, arrStep);
		seq.resetPosition();
	}

	// DnB: BASS 2 (variation of the reese, drop-2 only) plays only in the second drop
	{
		Arrangement a = GenreTemplates::makeTemplate(Genre::DnB);
		Sequencer seq;
		seq.setArrangement(&a);
		Macros m;
		Hit hits[Sequencer::maxHits];
		const int b2 = laneOf(a, Role::Bass2), reese = laneOf(a, Role::Reese);
		int b2First = 0, b2Second = 0, reeseSecond = 0, drops = 0, lastSec = -1;
		for (long long k = 0; k < (long long) a.totalBars() * 16; k++) {
			const int si = seq.sectionAt(k);
			if (si != lastSec && si >= 0 && a.sections[(size_t) si].type == SectionType::Drop) drops++;
			lastSec = si;
			const int n = seq.step(k, m, hits, Sequencer::maxHits);
			for (int i = 0; i < n; i++) {
				if (hits[i].lane == b2) (drops >= 2 ? b2Second : b2First)++;
				if (hits[i].lane == reese && drops >= 2 && a.sections[(size_t) si].type == SectionType::Drop) reeseSecond++;
			}
		}
		CHECK(b2 >= 0 && b2First == 0 && b2Second > 0, "BASS 2 only in drop 2 (first %d, second %d)", b2First, b2Second);
		CHECK(reeseSecond == 0, "the reese gives way to BASS 2 in drop 2 (%d)", reeseSecond);
	}

	// Jungle: breaks give slices 0..15 in order on a plain bar
	{
		Arrangement a = GenreTemplates::makeTemplate(Genre::Jungle);
		Sequencer seq;
		seq.setArrangement(&a);
		Macros m;
		m.chaos = 0.f;
		a.fill.probability = 0.f;
		Hit hits[Sequencer::maxHits];
		const int brk = laneOf(a, Role::Break);
		const int drop = 2, start = a.sectionStartBar(drop) * 16;
		bool ordered = true;
		int seen = 0;
		for (long long k = start; k < start + 16; k++) {
			const int n = seq.step(k, m, hits, Sequencer::maxHits);
			for (int i = 0; i < n; i++)
				if (hits[i].lane == brk) { ordered = ordered && hits[i].slice == (int) (k - start); seen++; }
		}
		CHECK(seen == 16 && ordered, "break slices in order on the drop's first bar (%d)", seen);

		// generators keep the model valid
		PatternGenerator::generateForm(a, 99, false);
		PatternGenerator::mutate(a, 0.8f, 5);
		PatternGenerator::generateDrums(a, 0.7f, 3);
		PatternGenerator::generateBassline(a, 0.7f, 4);
		seq.setArrangement(&a);
		bool cellsOk = true;
		for (auto& l : a.lanes) cellsOk = cellsOk && l.cells.size() == a.sections.size();
		CHECK(cellsOk && a.totalBars() >= 64, "form / mutate keep cells per section (%d bars)", a.totalBars());
		Arrangement v = a;
		PatternGenerator::generateForm(v, 99, true);
		CHECK(v.sections.size() == 6 && v.sections[1].type == SectionType::Drop, "VIP form starts with the drop");
	}

	std::printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}

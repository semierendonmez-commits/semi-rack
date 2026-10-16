// Offline check of the DATASCREEN model as the Rack module drives it (engines/datascreen: audio per
// sample, events, one update per display frame, the scene director). Not part of the plugin build:
//   g++ -std=c++17 -O2 -Iengines tests/datascreen_engine.cpp engines/datascreen/Model.cpp
//       -o build/datascreen_test && ./build/datascreen_test
#include "datascreen/DataLoad.h"
#include "datascreen/Model.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

namespace {
constexpr double SR = 48000.0;
constexpr int kPerFrame = 2400;   // 20 Hz, as on MetaModule

bool finiteFrame(const dsc::Frame& f) {
	const float v[] = {f.level, f.low, f.mid, f.high, f.kick, f.hit, f.step, f.flash, f.energy, f.centroid, f.bufPhase};
	for (float x : v)
		if (!std::isfinite(x) || x < 0.f)
			return false;
	for (int i = 0; i < dsc::kTrail; i++)
		if (!std::isfinite(f.attrX[i]) || !std::isfinite(f.attrY[i]) || !std::isfinite(f.attrZ[i]) || std::fabs(f.attrX[i]) > 10.f)
			return false;
	return true;
}

// one second of a kick-ish pulse train at 120 BPM (plus a high tone), clocked 16ths
struct Run {
	dsc::Model m;
	dsc::Settings s;
	dsc::Frame f;
	double beats = 0.0;
	long n = 0;
	int onsets = 0, kicks = 0;
	void frames(int count, bool audio, bool hits) {
		for (int k = 0; k < count; k++) {
			for (int i = 0; i < kPerFrame; i++, n++) {
				const double t = n / SR;
				const double beatT = std::fmod(t, 0.5);
				float v = 0.f;
				if (audio)
					v = (float) (0.8 * std::sin(2.0 * M_PI * 55.0 * t) * std::exp(-beatT * 14.0) + 0.05 * std::sin(2.0 * M_PI * 3520.0 * t));
				m.pushAudio(v, v);
			}
			beats = n / SR * 2.0;
			m.setClock(beats, 120.0, true);
			if (hits && k % 2 == 0)
				m.laneHit(k / 2 % 8, 0.9f);
			if (hits && k % 3 == 0)
				m.stepEntered((k / 3) % 16);
			m.update(f, (float) kPerFrame / (float) SR, s);
			onsets += m.onsetNow();
			kicks += f.kick > 0.9f;
			if (!finiteFrame(f)) {
				CHECK(false, "non-finite frame at %d", k);
				return;
			}
		}
	}
};
} // namespace

int main() {
	// ---- silence: everything stays quiet and finite, the sections are still made
	{
		Run r;
		r.frames(40, false, false);
		CHECK(r.f.level < 0.01f, "silence level %f", r.f.level);
		CHECK(r.onsets == 0, "silence onsets %d", r.onsets);
		int rowsOn = 0;
		for (int v = 0; v < dsc::kRows; v++)
			rowsOn += r.f.autoRows[v] != 0;
		CHECK(rowsOn == dsc::kRows, "automata made from the hash data (%d rows)", rowsOn);
		CHECK(!r.f.haveData && std::strcmp(r.f.name, "NO FILE") == 0, "no file");
		for (int i = 0; i < dsc::kStates; i++) {
			int sum = 0;
			for (int j = 0; j < dsc::kStates; j++)
				sum += r.f.matrix[i][j];
			CHECK(sum >= 97 && sum <= 103, "matrix row %d sums to %d", i, sum);
		}
	}
	// ---- audio + events: bands, onsets, kicks, learned walk and lanes, histories, reel
	{
		Run r;
		r.frames(200, true, true);   // 10 s
		int bandMax = 0, lowB = 0, highB = 0;
		for (int b = 0; b < dsc::kBands; b++) {
			bandMax = std::max(bandMax, (int) r.f.bands[b]);
			if (b < 8) lowB = std::max(lowB, (int) r.f.bands[b]);
			if (b > 40) highB = std::max(highB, (int) r.f.bands[b]);
		}
		std::printf("audio: level %.3f low %.3f high %.3f bandMax %d onsets %d kicks %d centroid %.0f Hz\n", r.f.level, r.f.low,
		            r.f.high, bandMax, r.onsets, r.kicks, r.f.centroid);
		CHECK(r.f.level > 0.02f, "level %f", r.f.level);
		CHECK(bandMax > 100, "bands alive %d", bandMax);
		CHECK(lowB > 60, "55 Hz in the low bands (%d)", lowB);
		CHECK(r.onsets >= 8, "onsets on the 2 Hz pulses (%d in 10 s)", r.onsets);
		CHECK(r.kicks > 0, "kick envelope fires");
		CHECK(r.f.learned, "the walk is learned from the steps");
		CHECK(r.f.lastTo >= 1 && r.f.lastTo <= 16, "last state %d", r.f.lastTo);
		CHECK(r.f.lanesLearned, "lanes learned from the hits");
		int pat = 0;
		for (int l = 0; l < dsc::kLanes; l++)
			pat += __builtin_popcount(r.f.lanePat[l]);
		CHECK(pat > 0, "lane patterns from hits (%d steps)", pat);
		int reel = 0;
		for (int i = 0; i < dsc::kReel; i++)
			reel += r.f.reel[i] > 20;
		CHECK(reel > 20, "the tape recorded peaks (%d of %d)", reel, dsc::kReel);
		int spec = 0;
		for (int i = 0; i < dsc::kSpecRows; i++)
			for (int k = 0; k < dsc::kSpecBands; k++)
				spec += r.f.spec[i][k] > 40;
		CHECK(spec > 20, "waterfall history filled (%d)", spec);
		CHECK(r.f.bar == (int) std::floor(r.beats / 4.0), "bar %d", r.f.bar);
		// follow scan: one row per 16th
		CHECK(r.f.head > 0, "follow scan moved the head (%llu)", (unsigned long long) r.f.head);
	}
	// ---- a file: text read as bits, strings found, head CV
	{
		Run r;
		auto blob = std::make_unique<dsc::DataBlob>();
		const char* text = "The data screen reads any file. Microsound and datamatics: barcode, matrix, field, scan, numbers.\n"
		                   "Pinball walks sixteen steps; the lanes fire; the reel records two bars of peaks.\n";
		dsc::pdata::fromText(*blob, text, "notes.txt", dsc::pdata::ReadAs::Auto);
		r.m.setBlob(blob.get());
		r.frames(4, true, false);
		CHECK(r.f.haveData && std::strcmp(r.f.type, "TEXT") == 0, "type %s", r.f.type);
		CHECK(r.f.str[0][0] != 0, "strings at the head");
		CHECK(r.f.entropy > 3.f && r.f.entropy < 5.5f, "text entropy %f", r.f.entropy);
		r.m.setHeadNorm(0.5f);
		r.frames(1, true, false);
		CHECK(r.f.head > 0 && r.f.head < r.f.nbits && r.f.head % 64 == 0, "head CV 0.5 -> %llu of %llu", (unsigned long long) r.f.head,
		      (unsigned long long) r.f.nbits);
		CHECK(r.f.flash < 0.5f, "load flash has decayed");
		r.m.setBlob(nullptr);
		r.frames(1, false, false);
		CHECK(!r.f.haveData, "cleared");
	}
	// ---- the director: every-bar cuts stay in the pool, manual cuts, variants in range
	{
		Run r;
		dsc::Director dir;
		int cuts = 0, outOfPool = 0;
		for (int k = 0; k < 400; k++) {   // 20 s = 10 bars at 120
			r.frames(1, false, false);
			if (dir.tick(r.f, 1, 2, false, false, false))
				cuts++;
			if (!dsc::inPool(2, dir.scene) && cuts > 0)
				outOfPool++;
			const int v = dir.variant(0);
			CHECK(v >= 0 && v < dsc::sceneInfo(dir.scene).numVariants, "variant %d", v);
		}
		std::printf("director: %d cuts in %d bars (every bar, films pool), scene %d %s\n", cuts, r.f.bar, dir.scene, dsc::sceneInfo(dir.scene).name);
		CHECK(cuts >= 8 && cuts <= 11, "one cut per bar (%d)", cuts);
		CHECK(outOfPool == 0, "cuts stayed in the films pool");
		dir.cutTo(25);
		CHECK(dir.scene == 25 && dir.variant(3) == 1, "manual cut to GHOST, variant 3 clamps to 2 variants");
		dir.cutTo(-1);
		CHECK(dir.scene == 25, "cutTo wraps (-1 -> 25)");
		int random = 0;
		for (int k = 0; k < 40; k++) {
			const int before = dir.scene;
			dir.next(0, true, (uint32_t) k);
			random += dir.scene != before;
		}
		CHECK(random == 40, "random next never repeats the scene (%d)", random);
		for (int i = 0; i < dsc::kNumScenes; i++)
			CHECK(dsc::sceneInfo(i).numVariants >= 2 && dsc::sceneInfo(i).numVariants <= 3, "scene %d variants", i);
	}
	std::printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}

// Offline test of the BLACKBOX cable tape (engines/blackbox). Not part of the plugin build.
//   g++ -std=c++17 -O2 -Iengines/blackbox tests/blackbox_engine.cpp engines/blackbox/CableTape.cpp -o build/blackbox_engine && build/blackbox_engine
#include "CableTape.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static int checks = 0, failures = 0;
#define CHECK(cond, ...)                                                   \
	do {                                                                   \
		++checks;                                                          \
		if (!(cond)) {                                                     \
			++failures;                                                    \
			std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond);    \
			std::printf(__VA_ARGS__);                                      \
			std::printf("\n");                                             \
		}                                                                  \
	} while (0)

using namespace bbxcv;

constexpr float kSr = 48000.f;
constexpr float kDt = 1.f / kSr;
constexpr double kBpm = 172.0;

// the "patch": an EPOCH-like BUS + four CVs + four gates, as functions of time (seconds)
static void signals(double t, float* v) {
	const double bars = t * kBpm / 60.0 / 4.0;
	v[LPos] = (float) (10.0 * std::fmod(bars, 32.0) / 32.0);
	v[LEnergy] = (float) (5.0 + 4.0 * std::sin(t * 0.7));
	v[LScene] = 0.5f * (float) ((int) (t / 4.0) % 6);
	const int bar = (int) std::floor(bars);
	const double sinceBar = (bars - bar) * 4.0 * 60.0 / kBpm;
	v[LEvent] = (bar % 4 == 0 && bar > 0 && sinceBar < 0.001) ? (bar % 8 == 0 ? 5.f : 2.5f) : 0.f;
	v[LCv1] = (float) (5.0 + 4.5 * std::sin(t * 2.0 * M_PI * 0.5));
	v[LCv2] = (float) ((int) (t * 4.0) % 5);          // stepped
	v[LCv3] = (float) (-3.0 + 0.5 * t);               // ramp, negative at first
	v[LCv4] = 0.f;
	const double six = t * kBpm / 60.0 * 4.0;         // 16ths
	v[LGate1] = (six - std::floor(six)) < 0.5 ? 10.f : 0.f;
	v[LGate2] = ((int) six % 8) == 0 ? 7.f : 0.f;
	v[LGate3] = t > 3.0 && t < 3.0006 ? 10.f : 0.f;   // a 0.6 ms trigger: still >= 1 ms on replay
	v[LGate4] = 0.f;
}

int main() {
	// ---- record 20 s
	Tape tape(1 << 16);
	Recorder rec(tape);
	const int n = (int) (20.0 * kSr);
	int gate1Edges = 0, pulses = 0;
	float prevG = 0.f, prevE = 0.f;
	for (int i = 0; i < n; i++) {
		float v[kLanes];
		signals(i * (double) kDt, v);
		if (v[LGate1] > 1.f && prevG <= 1.f)
			gate1Edges++;
		if (v[LEvent] > 1.f && prevE <= 1.f)
			pulses++;
		prevG = v[LGate1];
		prevE = v[LEvent];
		if (i == n / 4)
			rec.mark();
		rec.sample(v, true, kDt);
	}
	const uint64_t used = tape.end() - tape.begin();
	CHECK(rec.now() >= 19990 && rec.now() <= 20010, "tape time %u ms", (unsigned) rec.now());
	CHECK(used > 1000 && used < 40000, "records %llu", (unsigned long long) used);
	CHECK(rec.marks() == 1, "marks %d", rec.marks());
	std::printf("20 s -> %llu records (%llu KB), %d gate-1 hits, %d turns\n", (unsigned long long) used,
	            (unsigned long long) (used * sizeof(Rec) / 1024), gate1Edges, pulses);

	// ---- replay from 5 s: CV1 within the deadband / rate error, gates and pulses where they were
	{
		Player pl(tape);
		pl.start(5000, 1024);
		double maxErr = 0.0, maxErrStep = 0.0;
		int hits = 0, replayPulses = 0, gate3High = 0;
		float pg = pl.out(LGate1), pe = 0.f;
		bool ok = true;
		const int m = (int) (10.0 * kSr);
		for (int i = 0; i < m && ok; i++) {
			ok = pl.sample(kDt, rec.now());
			const double t = 5.0 + (i + 1) * (double) kDt;
			float v[kLanes];
			signals(t, v);
			maxErr = std::max(maxErr, (double) std::fabs(pl.out(LCv1) - v[LCv1]));
			if (std::fabs(t * 4.0 - std::round(t * 4.0)) > 0.01)
				maxErrStep = std::max(maxErrStep, (double) std::fabs(pl.out(LCv2) - v[LCv2]));
			const float g = pl.out(LGate1), e = pl.out(LEvent);
			if (g > 1.f && pg <= 1.f)
				hits++;
			if (e > 1.f && pe <= 1.f)
				replayPulses++;
			if (pl.out(LGate3) > 1.f)
				gate3High++;
			pg = g;
			pe = e;
			for (int l = 0; l < kLanes; l++)
				if (!std::isfinite(pl.out(l)))
					ok = false;
		}
		const int wantHits = (int) std::round(10.0 * kBpm / 60.0 * 4.0);
		CHECK(ok, "player ran out early / non-finite");
		CHECK(maxErr < 0.1, "CV1 replay error %.4f V (slope 14 V/s x ~5 ms hold + 5 mV)", maxErr);
		CHECK(maxErrStep < 0.01, "stepped CV2 error %.4f V", maxErrStep);
		CHECK(std::abs(hits - wantHits) <= 1, "gate 1 replay hits %d want %d", hits, wantHits);
		CHECK(replayPulses >= 2, "replayed turns %d", replayPulses);
		(void) gate3High;   // gate 3 fired at 3 s, before the replay window
	}

	// ---- the short trigger at 3 s is replayed >= 1 ms
	{
		Player pl(tape);
		pl.start(2900, 1024);
		int high = 0;
		for (int i = 0; i < (int) (0.3 * kSr); i++) {
			pl.sample(kDt, rec.now());
			if (pl.out(LGate3) > 1.f)
				high++;
		}
		CHECK(high >= 40 && high <= 100, "short trigger replayed for %d samples", high);
	}

	// ---- recall: the state at 12.3 s
	{
		Player pl(tape);
		pl.start(12300, 1024);
		float v[kLanes];
		signals(12.3, v);
		CHECK(std::fabs(pl.state.value[LScene] - v[LScene]) < 0.01f, "scene %f want %f", pl.state.value[LScene], v[LScene]);
		CHECK(std::fabs(pl.state.value[LEnergy] - v[LEnergy]) < 0.05f, "energy %f want %f", pl.state.value[LEnergy], v[LEnergy]);
		CHECK(std::fabs(pl.state.value[LCv3] - v[LCv3]) < 0.05f, "cv3 %f want %f", pl.state.value[LCv3], v[LCv3]);
	}

	// ---- a small ring rolls: memory stays bounded, a recall after the roll still knows every lane
	{
		Tape small(2048);
		Recorder r2(small);
		r2.settings.keyEvery = 256;
		for (int i = 0; i < (int) (20.0 * kSr); i++) {
			float v[kLanes];
			signals(i * (double) kDt, v);
			r2.sample(v, true, kDt);
		}
		CHECK(small.end() > small.capacity(), "rolled (%llu written)", (unsigned long long) small.end());
		CHECK(small.end() - small.begin() == small.capacity(), "full ring");
		const uint32_t first = small.at(small.begin()).t;
		CHECK(first > 1000, "oldest record at %u ms", (unsigned) first);
		LaneState st;
		stateBefore(small, small.end() - 10, st, 256);
		bool all = true;
		for (int l = 0; l < kLanes; l++)
			all = all && st.known[l];
		CHECK(all, "every lane known after the roll");
		std::vector<Rec> copy;
		CHECK(small.copyOut(copy) && copy.size() + 1 >= small.capacity(), "copy %zu", copy.size());
		bool ordered = true;
		for (size_t i = 1; i < copy.size(); i++)
			ordered = ordered && copy[i].t >= copy[i - 1].t;
		CHECK(ordered, "copy in time order");
	}

	// ---- not armed: tape time stops, nothing is written
	{
		const uint64_t e = tape.end();
		const uint32_t t = rec.now();
		float v[kLanes];
		signals(1.0, v);
		for (int i = 0; i < 48000; i++)
			rec.sample(v, false, kDt);
		CHECK(tape.end() == e && rec.now() == t, "paused");
	}

	// ---- the timeline view
	{
		View v;
		buildView(tape, 0, rec.now(), 1024, v);
		int ev = 0, bars = 0, marks = 0, g1 = 0;
		for (int c = 0; c < kViewCols; c++) {
			ev += v.event[c] ? 1 : 0;
			bars += v.bar[c] ? 1 : 0;
			marks += v.mark[c];
			g1 += v.gates[c] & 1;
		}
		CHECK(v.any && ev >= 2 && bars >= 10 && marks == 1 && g1 > 80, "view ev %d bars %d marks %d g1 %d", ev, bars, marks, g1);
		CHECK(v.scene[0] == 0 && v.scene[kViewCols - 1] >= 0, "scenes %d %d", v.scene[0], v.scene[kViewCols - 1]);
	}

	// ---- MIDI export
	{
		std::vector<Rec> copy;
		CHECK(tape.copyOut(copy), "copy");
		MidiOptions opt;
		MidiResult r = exportMidi(copy, opt);
		CHECK(r.bytes.size() > 100 && !std::memcmp(r.bytes.data(), "MThd", 4), "SMF header");
		CHECK(r.tempoFromBus && std::fabs(r.bpm - kBpm) < 1.0, "tempo %.2f from bus %d", r.bpm, (int) r.tempoFromBus);
		size_t pos = 14;
		int tracks = 0;
		bool chunksOk = true;
		while (pos + 8 <= r.bytes.size()) {
			chunksOk = chunksOk && !std::memcmp(&r.bytes[pos], "MTrk", 4);
			const uint32_t len = (uint32_t) r.bytes[pos + 4] << 24 | (uint32_t) r.bytes[pos + 5] << 16 | (uint32_t) r.bytes[pos + 6] << 8 | r.bytes[pos + 7];
			pos += 8 + len;
			tracks++;
		}
		CHECK(chunksOk && pos == r.bytes.size() && tracks == r.tracks, "chunks ok %d, %d tracks (%d)", (int) chunksOk, tracks, r.tracks);
		CHECK(r.tracks >= 4, "tracks %d (conductor, BUS, lanes)", r.tracks);
		CHECK(r.notes >= gate1Edges + pulses, "notes %d (>= %d gate-1 + turns)", r.notes, gate1Edges + pulses);
		if (FILE* f = std::fopen("build/blackbox_test.mid", "wb")) {
			std::fwrite(r.bytes.data(), 1, r.bytes.size(), f);
			std::fclose(f);
		}
		std::printf("MIDI: %zu bytes, %d tracks, %d notes, %d CCs @ %.1f BPM\n", r.bytes.size(), r.tracks, r.notes, r.ccs, r.bpm);

		MidiOptions w;
		w.t0 = 8000;
		w.t1 = 9000;
		MidiResult rw = exportMidi(copy, w);
		CHECK(rw.notes > 0 && rw.notes < r.notes / 5, "window notes %d", rw.notes);
	}

	tape.clear();
	rec.reset();
	CHECK(tape.end() == 0 && rec.now() == 0, "cleared");

	std::printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

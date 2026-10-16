// Offline check of the vendored QUANTA engine as the Rack module drives it (32-sample blocks,
// clock -> ppq, gate -> voice slots, the arrangement through qn::Player). Not part of the plugin build.
//   g++ -std=c++17 -O2 -Iengines tests/quanta_engine.cpp engines/quanta/Particles.cpp engines/quanta/Arrange.cpp -o build/quanta_test
//   ./build/quanta_test            (add -DMETAMODULE to check the MetaModule particle budget)
#include "quanta/Arrange.h"
#include "quanta/Particles.h"
#include <chrono>
#include <cmath>
#include <cstdio>

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

constexpr double SR = 48000.0;
constexpr int B = 32;

struct Stats { bool finite = true; float peak = 0.f; double rms = 0.0; int maxActive = 0; double ms = 0.0; };

// voice: free-running slot 0 at hz (when hz > 0); arrangement on when arr != nullptr
static Stats run(qn::Engine& e, qn::Player& pl, const qn::Scene& synth, double hz, const qn::Arrangement* arr,
                 double seconds, double bpm = 120.0, int fxMode = qn::FxNone) {
	Stats st;
	float Lb[B], Rb[B];
	double ppq = 0.0, sum = 0.0;
	const long total = (long) (SR * seconds);
	if (hz > 0)
		e.noteOn(0, hz, 0.8f);
	const auto t0 = std::chrono::steady_clock::now();
	static const qn::Arrangement empty;
	for (long n = 0; n + B <= total; n += B) {
		qn::Engine::Block eb;
		eb.synth = &synth;
		eb.bpm = bpm;
		eb.ppq = ppq;
		eb.sampleRate = SR;
		eb.outDb = -6.f;
		qn::ManualFx man;
		if (fxMode != qn::FxNone) {
			man.mode = fxMode;
			man.sliceBeats = 0.25;
			man.progress = (float) std::fmod(ppq, 1.0);
			man.trigger = n == 0;
		}
		pl.render(e, Lb, Rb, B, eb, arr ? *arr : empty, arr != nullptr, 0.f, true, man);
		ppq += B * bpm / 60.0 / SR;
		for (int i = 0; i < B; i++) {
			if (!std::isfinite(Lb[i]) || !std::isfinite(Rb[i]))
				st.finite = false;
			st.peak = std::max(st.peak, std::max(std::abs(Lb[i]), std::abs(Rb[i])));
			sum += 0.5 * ((double) Lb[i] * Lb[i] + (double) Rb[i] * Rb[i]);
		}
		st.maxActive = std::max(st.maxActive, e.activeParticles.load());
	}
	st.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	st.rms = std::sqrt(sum / (double) total);
	return st;
}

static double pct(const Stats& s, double seconds) { return s.ms / (seconds * 10.0); }

int main() {
	// ---- every particle type, free-running voice at C4
	static const char* types[] = {"GRAIN", "GLISSON", "GRAINLET", "TRAINLET", "PULSAR"};
	for (int t = 0; t < qn::kNumTypes; t++) {
		qn::Engine e;
		e.prepare(SR);
		qn::Player pl;
		qn::Scene s;
		s.type = t;
		s.density = 80.f;
		s.durMs = 30.f;
		s.glide = 7.f;
		const Stats st = run(e, pl, s, 261.63, nullptr, 2.0);
		CHECK(st.finite, "%s not finite", types[t]);
		CHECK(st.rms > 1e-3, "%s silent (rms %g)", types[t], st.rms);
		CHECK(st.peak <= 0.97f, "%s over the -0.3 dBFS ceiling (%g)", types[t], st.peak);
		std::printf("%-9s peak %.3f rms %.4f particles<=%d  cost %.2f %%\n", types[t], st.peak, st.rms, st.maxActive, pct(st, 2.0));
	}

	// ---- a dense cloud at the particle budget (the worst case for the per-sample loop)
	{
		qn::Engine e;
		e.prepare(SR);
		qn::Player pl;
		qn::Scene s = qn::factoryScene(qn::BCloud);
		s.density = 2000.f;
		s.durMs = 200.f;
		const Stats st = run(e, pl, s, 440.0, nullptr, 2.0);
		CHECK(st.finite && st.rms > 1e-3, "dense cloud");
		CHECK(st.maxActive <= qn::Engine::kMaxParticles, "particle budget %d", st.maxActive);
		std::printf("dense cloud: particles<=%d (budget %d) cost %.2f %%\n", st.maxActive, qn::Engine::kMaxParticles, pct(st, 2.0));
	}

	// ---- the arrangement: every form generates, plays and stays bounded
	static const char* forms[] = {"ACCUMULATE", "ALTERNATE", "PERMUTE", "CODEX", "MARKOV", "ARC"};
	for (int f = 0; f < qn::kNumForms; f++) {
		qn::GenParams g;
		g.form = f;
		g.lengthBars = 32;
		g.blockBars = 2;
		g.seed = 7;
		const qn::Arrangement a = qn::generate(g);
		CHECK(!a.blocks.empty(), "%s: no blocks", forms[f]);
		CHECK(std::abs(a.totalBeats() - 128.0) < 1e-6, "%s: %g beats", forms[f], a.totalBeats());
		const qn::Arrangement b = qn::generate(g);
		CHECK(a.toString() == b.toString(), "%s: not deterministic", forms[f]);
		const qn::Arrangement c = qn::Arrangement::fromString(a.toString());
		CHECK(c.blocks.size() == a.blocks.size() && c.toString() == a.toString(), "%s: string round trip", forms[f]);
		qn::Engine e;
		e.prepare(SR);
		qn::Player pl;
		qn::Scene s;
		const Stats st = run(e, pl, s, 0.0, &a, 8.0, 140.0);
		CHECK(st.finite, "%s not finite", forms[f]);
		CHECK(st.rms > 1e-4, "%s silent", forms[f]);
		CHECK(st.peak <= 0.97f, "%s peak %g", forms[f], st.peak);
		std::printf("%-10s %3zu blocks  peak %.3f rms %.4f particles<=%d cost %.2f %%\n", forms[f], a.blocks.size(), st.peak, st.rms,
		            st.maxActive, pct(st, 8.0));
	}

	// ---- perform fx on top of a running arrangement
	{
		qn::GenParams g;
		g.form = qn::Codex;
		g.lengthBars = 16;
		const qn::Arrangement a = qn::generate(g);
		for (int fx = qn::FxRepeat; fx < qn::kNumFx; fx++) {
			qn::Engine e;
			e.prepare(SR);
			qn::Player pl;
			qn::Scene s;
			const Stats st = run(e, pl, s, 0.0, &a, 3.0, 128.0, fx);
			CHECK(st.finite && st.peak <= 0.97f, "fx %d: finite %d peak %g", fx, st.finite, st.peak);
		}
	}

	// ---- a sample as the waveform
	{
		qn::Engine e;
		e.prepare(SR);
		auto sd = std::make_shared<qn::SampleData>();
		sd->sr = SR;
		sd->data.resize(48000);
		for (size_t i = 0; i < sd->data.size(); i++)
			sd->data[i] = (float) std::sin(2.0 * M_PI * 261.63 * (double) i / SR);
		e.setSample(sd);
		qn::Player pl;
		qn::Scene s;
		s.type = qn::Grain;
		s.wave = qn::Sample;
		const Stats st = run(e, pl, s, 261.63, nullptr, 1.0);
		CHECK(st.finite && st.rms > 1e-3, "sample wave: rms %g", st.rms);
	}

	// ---- setSlotHz moves new particles only, no restart
	{
		qn::Engine e;
		e.prepare(SR);
		qn::Player pl;
		qn::Scene s;
		s.type = qn::Grain;
		s.density = 200.f;
		run(e, pl, s, 220.0, nullptr, 0.2);
		e.setSlotHz(0, 880.0);
		const int w0 = e.eventW.load();
		float Lb[B], Rb[B];
		qn::Engine::Block eb;
		eb.synth = &s;
		eb.sampleRate = SR;
		for (int i = 0; i < 100; i++)
			e.process(Lb, Rb, B, eb);
		const int w1 = e.eventW.load();
		CHECK(w1 != w0, "no spawns after setSlotHz");
		CHECK(std::abs(e.events[(size_t) ((w1 - 1 + qn::Engine::kEvents) % qn::Engine::kEvents)].hz - 880.f) < 1.f, "setSlotHz pitch");
		CHECK(e.slotActive(0), "voice restarted / stopped");
	}

	std::printf("%d checks, %d failures\n", checks, fails);
	return fails ? 1 : 0;
}

// Smoke test of the three modules (FLOATER, FLOATDAT, SPLICEBUS) against the built plugin.so and libRack:
// construct, run 4 s at 48 kHz (own clock, then CLK), check the audio is finite, bounded and not silent, the
// gates fire, and the patch JSON round-trips. Not part of the plugin build.
//   sh tests/floater_modules_smoke.sh <Rack-SDK>
#include <rack.hpp>
#include <cmath>
#include <cstdio>
using namespace rack;
extern Model* modelFloater;
extern Model* modelFloatdat;
extern Model* modelSplicebus;

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; std::printf("FAIL %s  ", #c); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

struct Res { double rms = 0; float peak = 0; bool finite = true; int gateEdges = 0; };

static Res run(Module* m, double seconds, int clkIn, int audioIn, int nGates, int firstGate) {
	Module::ProcessArgs a;
	a.sampleRate = 48000.f;
	a.sampleTime = 1.f / 48000.f;
	Res r;
	double sum = 0;
	const int n = (int) (seconds * 48000);
	std::vector<bool> was(nGates, false);
	for (int i = 0; i < n; i++) {
		if (clkIn >= 0) {
			m->inputs[clkIn].channels = 1;   // setChannels() cannot connect a port
			m->inputs[clkIn].setVoltage((i / 3000) % 2 ? 0.f : 10.f);   // 1/16 at 240 BPM-ish (16 Hz)
		}
		if (audioIn >= 0) {
			m->inputs[audioIn].channels = 1;
			m->inputs[audioIn].setVoltage(4.f * std::sin(i * 0.01f) + ((i % 12000) < 400 ? 3.f * std::sin(i * 0.3f) : 0.f));
		}
		a.frame = i;
		m->process(a);
		const float l = m->outputs[0].getVoltage(), rr = m->outputs[1].getVoltage();
		if (!std::isfinite(l) || !std::isfinite(rr)) r.finite = false;
		r.peak = std::max(r.peak, std::max(std::fabs(l), std::fabs(rr)));
		sum += l * l + rr * rr;
		for (int g = 0; g < nGates; g++) {
			const bool on = m->outputs[firstGate + g].getVoltage() > 5.f;
			if (on && !was[g]) r.gateEdges++;
			was[g] = on;
		}
	}
	r.rms = std::sqrt(sum / (2.0 * n));
	return r;
}

static void roundTrip(Module* m, const char* name) {
	json_t* j = m->dataToJson();
	m->dataFromJson(j);
	json_t* k = m->dataToJson();
	char* a = json_dumps(j, JSON_SORT_KEYS);
	char* b = json_dumps(k, JSON_SORT_KEYS);
	CHECK(a && b && !std::strcmp(a, b), "%s JSON round trip", name);
	std::free(a); std::free(b);
	json_decref(j); json_decref(k);
}

int main() {
	Module::SampleRateChangeEvent e;
	e.sampleRate = 48000.f;
	e.sampleTime = 1.f / 48000.f;
	{
		Module* m = modelFloater->createModule();
		m->onSampleRateChange(e);
		Res r = run(m, 4.0, -1, -1, 8, 2);
		std::printf("FLOATER own clock: rms %.2f V peak %.2f V, %d gates\n", r.rms, r.peak, r.gateEdges);
		CHECK(r.finite && r.peak < 7.5f && r.rms > 0.1 && r.gateEdges > 20, "FLOATER own clock");
		Res c = run(m, 4.0, 0, -1, 8, 2);
		std::printf("FLOATER on CLK: rms %.2f V, %d gates\n", c.rms, c.gateEdges);
		CHECK(c.finite && c.rms > 0.1 && c.gateEdges > 20, "FLOATER on CLK");
		m->params[m->params.size() - 3].setValue(1.f);   // NEW PATCH
		run(m, 0.1, -1, -1, 0, 0);
		m->params[m->params.size() - 3].setValue(0.f);
		Res n = run(m, 2.0, -1, -1, 8, 2);
		CHECK(n.finite && n.peak < 7.5f, "FLOATER after NEW PATCH");
		roundTrip(m, "FLOATER");
		delete m;
	}
	{
		Module* m = modelFloatdat->createModule();
		m->onSampleRateChange(e);
		Res r = run(m, 4.0, -1, -1, 8, 2);
		std::printf("FLOATDAT own clock: rms %.2f V peak %.2f V, %d gates\n", r.rms, r.peak, r.gateEdges);
		CHECK(r.finite && r.peak <= 5.01f && r.rms > 0.1 && r.gateEdges > 10, "FLOATDAT own clock");
		Res c = run(m, 4.0, 0, -1, 8, 2);
		std::printf("FLOATDAT on CLK: rms %.2f V, %d gates\n", c.rms, c.gateEdges);
		CHECK(c.finite && c.rms > 0.1 && c.gateEdges > 10, "FLOATDAT on CLK");
		roundTrip(m, "FLOATDAT");
		delete m;
	}
	{
		Module* m = modelSplicebus->createModule();
		m->onSampleRateChange(e);
		Res r = run(m, 4.0, -1, 0, 0, 0);
		std::printf("SPLICEBUS stopped (a wire): rms %.2f V\n", r.rms);
		CHECK(r.finite && r.rms > 1.0, "SPLICEBUS wire");
		Res c = run(m, 16.0, 3, 0, 2, 2);
		std::printf("SPLICEBUS on CLK: rms %.2f V, %d break / turn gates\n", c.rms, c.gateEdges);
		CHECK(c.finite && c.rms > 1.0 && c.peak < 15.f && c.gateEdges > 0, "SPLICEBUS on CLK");
		roundTrip(m, "SPLICEBUS");
		delete m;
	}
	std::printf("%d failures\n", failures);
	return failures ? 1 : 0;
}

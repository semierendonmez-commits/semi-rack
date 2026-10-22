// Smoke test of EVERY module in the plugin against the built plugin.so and libRack (no GUI).
// Each module runs in its own forked process so a crash is reported, not fatal.
//   sh tests/all_modules_smoke.sh <Rack-SDK> [Slug ...]
//
// Per module:
//   A  unpatched, 6 s                      (does it make sound / gates on its own?)
//   B  patched, 12 s: CLK 1/16 @172 BPM, RST once, RUN high, audio drum loop into audio inputs,
//      gates on trigger inputs, slow V/OCT sweep, BUS cable (POS/ENERGY/SCENE/EVENT) into BUS inputs
//   R  randomize params, 4 s; reset, 1 s
//   S  44.1 kHz and 96 kHz, 2 s each
//   F  file: every string field named *path*/*file* in dataToJson is pointed at a test WAV, 4 s
//   J  dataToJson -> dataFromJson -> dataToJson must be identical
// Checks: every output finite, |V| <= 12 V; at least one output alive in A or B.
// Reports: per-output rms/peak/edges, CPU µs per sample (desktop) in B.
#include <rack.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace rack;

extern Plugin* pluginInstance;
void init(Plugin* p);
void smokeMakeEngine(); // tests/all_modules_engine.cpp

namespace {

const char* kWavPath = "build/smoke_loop.wav";

std::string lower(std::string s) {
	for (auto& c : s)
		c = (char) std::tolower((unsigned char) c);
	return s;
}
bool has(const std::string& s, const char* k) { return s.find(k) != std::string::npos; }

enum Kind { None, Clock, Reset, Run, Audio, Gate, Pitch, Bus, Cv };

Kind classify(const std::string& rawName, const std::string& rawDesc) {
	std::string n = lower(rawName), d = lower(rawDesc);
	std::string s = n + " " + d;
	if (has(n, "bus") || has(s, "family bus") || has(s, "bus cable")) return Bus;
	if (has(n, "clock") || n == "clk" || has(n, "clk")) return Clock;
	if (has(n, "reset") || n == "rst") return Reset;
	if (n == "run" || has(n, "run gate") || has(n, "run (") || n.rfind("run", 0) == 0) return Run;
	if (has(n, "v/oct") || has(n, "voct") || has(n, "pitch") || has(n, "transpose") || has(n, "root")) return Pitch;
	if (has(n, "left") || has(n, "right") || has(n, "audio") || n == "in" || has(n, "in l") || has(n, "in r") ||
	    has(n, "input") || has(n, "sidechain") || has(n, "excite") || has(n, "break") || n == "sc")
		return Audio;
	if (has(n, "gate") || has(n, "trig") || has(n, "strike") || has(n, "hit") || has(n, "kick") || has(n, "snare") ||
	    has(n, "hat") || has(n, "fire") || has(n, "lane") || has(n, "next") || has(n, "mutate") || has(n, "new"))
		return Gate;
	return Cv;
}

struct OutStat {
	double sum = 0;
	float peak = 0;
	int edges = 0;
	bool hi = false;
	bool finite = true;
	float minV = 1e9f, maxV = -1e9f;
	size_t n = 0;
};

struct RunResult {
	std::vector<OutStat> outs;
	double usPerSample = 0;
	bool finite = true;
	float worst = 0;
};

float drumLoop(long i, float sr) {
	// 172 BPM: kick on 1 and 2.5, snare on 2 and 4, hats every 8th
	double t = i / sr;
	double beat = t * 172.0 / 60.0;
	double b = std::fmod(beat, 4.0);
	auto env = [](double x, double k) { return x >= 0 && x < 1 ? std::exp(-k * x) : 0.0; };
	double kick = env(b, 18) + env(b - 1.5, 18);
	double snare = env(b - 1.0, 14) + env(b - 3.0, 14);
	double hat = env(std::fmod(beat, 0.5), 60);
	double s = 0.9 * kick * std::sin(2 * M_PI * (50 + 120 * env(b, 30)) * t) +
	           0.5 * snare * (((i * 1103515245u + 12345u) >> 16 & 0x7fff) / 16384.0 - 1.0) +
	           0.2 * hat * (((i * 214013u + 2531011u) >> 16 & 0x7fff) / 16384.0 - 1.0);
	return (float) (4.0 * s);
}

void writeWav(const char* path) {
	const int sr = 48000, frames = sr * 4, ch = 2;
	FILE* f = std::fopen(path, "wb");
	if (!f) return;
	auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	std::fwrite("RIFF", 1, 4, f); w32(36 + frames * ch * 2); std::fwrite("WAVE", 1, 4, f);
	std::fwrite("fmt ", 1, 4, f); w32(16); w16(1); w16(ch); w32(sr); w32(sr * ch * 2); w16(ch * 2); w16(16);
	std::fwrite("data", 1, 4, f); w32(frames * ch * 2);
	for (int i = 0; i < frames; i++) {
		float v = drumLoop(i, sr) / 5.f;
		int16_t s = (int16_t) std::max(-32767.f, std::min(32767.f, v * 30000.f));
		std::fwrite(&s, 2, 1, f); std::fwrite(&s, 2, 1, f);
	}
	std::fclose(f);
}

RunResult run(Module* m, float sr, double seconds, bool patched, long& frame) {
	Module::ProcessArgs a;
	a.sampleRate = sr;
	a.sampleTime = 1.f / sr;
	RunResult r;
	r.outs.resize(m->outputs.size());
	std::vector<Kind> kinds(m->inputs.size(), None);
	for (size_t i = 0; i < m->inputs.size(); i++) {
		PortInfo* pi = i < m->inputInfos.size() ? m->inputInfos[i] : nullptr;
		kinds[i] = pi ? classify(pi->name, pi->description) : None;
		m->inputs[i].channels = 0;
		m->inputs[i].setVoltage(0.f);
	}
	for (auto& o : m->outputs)
		o.channels = 1; // as if a cable were connected
	const long n = (long) (seconds * sr);
	const double sixteenth = 60.0 / 172.0 / 4.0;
	auto t0 = std::chrono::steady_clock::now();
	for (long k = 0; k < n; k++, frame++) {
		const double t = k / (double) sr;
		if (patched) {
			const bool clk = std::fmod(t, sixteenth) < sixteenth * 0.5;
			const double bars = t * 172.0 / 60.0 / 4.0;
			for (size_t i = 0; i < m->inputs.size(); i++) {
				Input& in = m->inputs[i];
				switch (kinds[i]) {
					case Clock: in.channels = 1; in.setVoltage(clk ? 10.f : 0.f); break;
					case Reset: in.channels = 1; in.setVoltage(t < 0.002 ? 10.f : 0.f); break;
					case Run: in.channels = 1; in.setVoltage(10.f); break;
					case Audio: in.channels = 1; in.setVoltage(drumLoop(frame, sr)); break;
					case Gate: in.channels = 1; in.setVoltage(std::fmod(t, sixteenth * 4) < 0.01 ? 10.f : 0.f); break;
					case Pitch: in.channels = 1; in.setVoltage((float) (0.5 * std::sin(t * 0.7))); break;
					case Bus:
						in.channels = 4;
						in.setVoltage((float) (10.0 * std::fmod(bars, 32.0) / 32.0), 0);
						in.setVoltage((float) (5.0 + 5.0 * std::sin(t * 0.3)), 1);
						in.setVoltage((float) (0.5 * ((int) (bars / 8) % 4)), 2);
						in.setVoltage(std::fmod(bars, 4.0) < 0.004 ? 5.f : 0.f, 3);
						break;
					default: break;
				}
			}
		}
		a.frame = frame;
		m->process(a);
		for (size_t o = 0; o < m->outputs.size(); o++) {
			Output& out = m->outputs[o];
			int chs = std::max(1, out.getChannels());
			for (int c = 0; c < chs; c++) {
				float v = out.getVoltage(c);
				OutStat& s = r.outs[o];
				if (!std::isfinite(v)) { s.finite = false; r.finite = false; continue; }
				if (c == 0) {
					s.sum += (double) v * v;
					s.n++;
					bool hi = v > 1.f;
					if (hi && !s.hi) s.edges++;
					s.hi = hi;
				}
				s.peak = std::max(s.peak, std::fabs(v));
				s.minV = std::min(s.minV, v);
				s.maxV = std::max(s.maxV, v);
				r.worst = std::max(r.worst, std::fabs(v));
			}
		}
	}
	auto t1 = std::chrono::steady_clock::now();
	r.usPerSample = std::chrono::duration<double, std::micro>(t1 - t0).count() / std::max<long>(1, n);
	return r;
}

bool alive(const OutStat& s) { return s.n && (std::sqrt(s.sum / s.n) > 0.005 || s.edges > 0 || s.maxV - s.minV > 0.05f); }

int setPaths(json_t* j, const char* path) {
	int n = 0;
	if (json_is_object(j)) {
		const char* key;
		json_t* v;
		json_object_foreach(j, key, v) {
			std::string k = lower(key);
			if (json_is_string(v) && (has(k, "path") || has(k, "file") || has(k, "wav") || has(k, "sample"))) {
				json_object_set_new(j, key, json_string(path));
				n++;
			}
			else
				n += setPaths(v, path);
		}
	}
	else if (json_is_array(j)) {
		size_t i;
		json_t* v;
		json_array_foreach(j, i, v) n += setPaths(v, path);
	}
	return n;
}

int testModel(Model* model) {
	int fails = 0;
	auto fail = [&](const char* what) { std::printf("  FAIL %s\n", what); fails++; };
	Module* m = model->createModule();
	Module::SampleRateChangeEvent e;
	e.sampleRate = 48000.f;
	e.sampleTime = 1.f / 48000.f;
	m->onSampleRateChange(e);
	long frame = 0;

	RunResult A = run(m, 48000.f, 6.0, false, frame);
	RunResult B = run(m, 48000.f, 12.0, true, frame);

	std::printf("  outputs (A unpatched | B patched):\n");
	bool anyAlive = false;
	for (size_t o = 0; o < m->outputs.size(); o++) {
		std::string nm = o < m->outputInfos.size() && m->outputInfos[o] ? m->outputInfos[o]->name : "?";
		const OutStat &a = A.outs[o], &b = B.outs[o];
		bool al = alive(a) || alive(b);
		anyAlive |= al;
		std::printf("    %-28.28s A rms %5.2f pk %5.2f e %4d | B rms %5.2f pk %5.2f e %4d %s\n", nm.c_str(),
		            a.n ? std::sqrt(a.sum / a.n) : 0.0, a.peak, a.edges, b.n ? std::sqrt(b.sum / b.n) : 0.0, b.peak,
		            b.edges, al ? "" : " <- dead");
	}
	if (!A.finite || !B.finite) fail("non-finite output (A/B)");
	if (A.worst > 12.f || B.worst > 12.f) { std::printf("  worst |V| %.2f\n", std::max(A.worst, B.worst)); fail("output above 12 V"); }
	if (!m->outputs.empty() && !anyAlive) fail("no output moved in A or B");

	// R: randomize + reset
	Module::RandomizeEvent rz;
	m->onRandomize(rz);
	RunResult R = run(m, 48000.f, 4.0, true, frame);
	if (!R.finite || R.worst > 12.f) fail("after randomize: non-finite or > 12 V");
	Module::ResetEvent re;
	m->onReset(re);
	RunResult R2 = run(m, 48000.f, 1.0, true, frame);
	if (!R2.finite) fail("after reset: non-finite");

	// S: sample rates
	for (float sr : {44100.f, 96000.f}) {
		e.sampleRate = sr;
		e.sampleTime = 1.f / sr;
		m->onSampleRateChange(e);
		RunResult S = run(m, sr, 2.0, true, frame);
		if (!S.finite || S.worst > 12.f) { std::printf("  at %.0f Hz\n", sr); fail("sample rate: non-finite or > 12 V"); }
	}
	e.sampleRate = 48000.f;
	e.sampleTime = 1.f / 48000.f;
	m->onSampleRateChange(e);

	// F: file paths
	json_t* d = m->dataToJson();
	int nPaths = d ? setPaths(d, kWavPath) : 0;
	if (nPaths) {
		m->dataFromJson(d);
		RunResult F = run(m, 48000.f, 4.0, true, frame);
		bool al = false;
		for (auto& s : F.outs) al |= alive(s);
		std::printf("  file: %d path field(s) -> test WAV; outputs %s\n", nPaths, al ? "alive" : "silent");
		if (!F.finite || F.worst > 12.f) fail("with WAV loaded: non-finite or > 12 V");
	}
	if (d) json_decref(d);

	// J: JSON round trip
	json_t* j1 = m->dataToJson();
	if (j1) {
		m->dataFromJson(j1);
		json_t* j2 = m->dataToJson();
		char* s1 = json_dumps(j1, JSON_SORT_KEYS | JSON_COMPACT);
		char* s2 = json_dumps(j2, JSON_SORT_KEYS | JSON_COMPACT);
		if (!s1 || !s2 || std::strcmp(s1, s2)) {
			size_t k = 0;
			while (s1 && s2 && s1[k] && s1[k] == s2[k]) k++;
			size_t from = k > 40 ? k - 40 : 0;
			std::printf("  json differs after round trip at byte %zu:\n    %.80s\n    %.80s\n", k, s1 ? s1 + from : "", s2 ? s2 + from : "");
			fail("JSON round trip");
		}
		std::free(s1); std::free(s2);
		json_decref(j1); json_decref(j2);
	}

	std::printf("  cpu: %.2f us/sample unpatched, %.2f us/sample patched (%.1f %% of one core at 48 kHz)\n",
	            A.usPerSample, B.usPerSample, B.usPerSample * 48000.0 / 1e4);
	delete m;
	return fails;
}

} // namespace

int main(int argc, char** argv) {
	writeWav(kWavPath);
	// A bare context + engine: ParamQuantity::setImmediateValue (randomize / reset) goes through APP->engine.
	smokeMakeEngine();
	Plugin* p = new Plugin;
	p->path = ".";
	init(p);
	int total = 0, crashed = 0, failed = 0;
	std::vector<std::string> bad;
	for (Model* model : p->models) {
		if (argc > 1) {
			bool want = false;
			for (int i = 1; i < argc; i++) want |= model->slug == argv[i];
			if (!want) continue;
		}
		total++;
		std::printf("== %s\n", model->slug.c_str());
		std::fflush(stdout);
		pid_t pid = fork();
		if (pid == 0) {
			alarm(240);
			int f = testModel(model);
			std::fflush(stdout);
			_exit(f ? 1 : 0);
		}
		int st = 0;
		waitpid(pid, &st, 0);
		if (WIFSIGNALED(st)) {
			std::printf("  CRASH signal %d\n", WTERMSIG(st));
			crashed++;
			bad.push_back(model->slug + " (crash)");
		}
		else if (WEXITSTATUS(st)) {
			failed++;
			bad.push_back(model->slug);
		}
	}
	std::printf("\n%d modules, %d failed, %d crashed\n", total, failed, crashed);
	for (auto& b : bad) std::printf("  %s\n", b.c_str());
	return (failed || crashed) ? 1 : 0;
}

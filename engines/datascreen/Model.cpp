// DATASCREEN model for Rack / MetaModule — see Model.h. Ported from Source/model/Ingest.cpp
// (analyse, synthesise) and Source/scenes/Scenes.cpp (Motion::advance, prepareScenes) without JUCE.
#include "Model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace dsc {

namespace {
constexpr double kPi = 3.14159265358979323846;
inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int popc(uint64_t v) { return __builtin_popcountll(v); }

// elementary rules the made automata step with (Ingest.cpp kRules)
const int kRules[10] = {30, 45, 54, 60, 90, 102, 110, 126, 150, 182};

uint64_t generation(uint64_t row, int rule) {
	uint64_t out = 0;
	for (int i = 0; i < 64; ++i) {
		const int l = (int) ((row >> ((i + 63) & 63)) & 1u), c = (int) ((row >> i) & 1u), r = (int) ((row >> ((i + 1) & 63)) & 1u);
		if ((rule >> (l << 2 | c << 1 | r)) & 1)
			out |= 1ull << i;
	}
	return out;
}

void copyStr(char* dst, size_t n, const std::string& s) {
	std::snprintf(dst, n, "%s", s.c_str());
}

const SceneInfo kInfo[kNumScenes] = {
	{"BARCODE", 3, {"bands", "split", "layers"}},
	{"MATRIX", 3, {"fan", "spectrum", "triple"}},
	{"SPACE", 3, {"cloud", "tunnel", "sphere"}},
	{"NETWORK", 3, {"web", "contour", "markov"}},
	{"SCATTER", 3, {"file", "delay map", "density"}},
	{"FIELD", 3, {"field + roll", "automata", "wall"}},
	{"SCAN", 3, {"columns", "buffers", "rain"}},
	{"NUMBERS", 3, {"time", "hex wall", "clock"}},
	{"TEST PATTERN", 3, {"vertical", "horizontal", "checker"}},
	{"SPECTRA", 3, {"waterfall", "lissajous", "radial"}},
	{"TEXT", 3, {"typing", "word", "dump"}},
	{"SEQUENCER", 3, {"lanes", "ring", "matrix"}},
	{"TUNNEL", 2, {"corridor", "rush", ""}},
	{"INTERFERENCE", 3, {"rings", "hourglass", "sources"}},
	{"CODEX", 2, {"blocks", "columns", ""}},
	{"SUPERPOSITION", 2, {"snake", "blocks", ""}},
	{"TILES", 2, {"wall", "readings", ""}},
	{"BITMAP", 2, {"smear", "pull", ""}},
	{"TICKER", 2, {"wall", "board", ""}},
	{"HORIZON", 2, {"plane", "ceiling", ""}},
	{"CASE", 2, {"rack", "focus", ""}},
	{"REEL", 2, {"tape", "genes", ""}},
	{"ORBIT", 2, {"rings", "binary", ""}},
	{"ATTRACTOR", 2, {"lorenz", "phase", ""}},
	{"VORTEX", 2, {"spiral", "bytes", ""}},
	{"GHOST", 2, {"net", "nodes", ""}},
};
} // namespace

const SceneInfo& sceneInfo(int i) { return kInfo[((i % kNumScenes) + kNumScenes) % kNumScenes]; }

const char* poolName(int p) {
	static const char* const n[kNumPools] = {"All 26", "Data 1-12", "Films 13-20", "Case 21-26", "Dark only", "Bright only"};
	return n[std::clamp(p, 0, kNumPools - 1)];
}

bool inPool(int pool, int s) {
	switch (pool) {
		case 1: return s < 12;
		case 2: return s >= 12 && s < 20;
		case 3: return s >= 20;
		case 4: return s != 8 && s != 14 && s != 15 && s != 17;   // the white-field scenes out
		case 5: return s == 8 || s == 14 || s == 15 || s == 17 || s == 10;
		default: return true;
	}
}

const char* changeName(int c) {
	static const char* const n[kNumChanges] = {"Manual", "Every bar", "Every 2 bars", "Every 4 bars", "Every 8 bars",
	                                           "On step", "Random", "On onset", "On BUS turn"};
	return n[std::clamp(c, 0, kNumChanges - 1)];
}

// ================================================================ Model

Model::Model() {
	re.assign(kWin, 0.0f);
	im.assign(kWin, 0.0f);
	hann.resize(kWin);
	bitrev.resize(kWin);
	for (int i = 0; i < kWin; ++i) {
		hann[(size_t) i] = (float) (0.5 - 0.5 * std::cos(2.0 * kPi * i / (kWin - 1)));
		int r = 0;
		for (int b = 0; b < 10; ++b)
			if (i & (1 << b))
				r |= 1 << (9 - b);
		bitrev[(size_t) i] = r;
	}
	for (int v = 0; v < kRows; ++v)
		rows[v] = 1ull << (v * 7);
}

void Model::laneHit(int lane, float vel) {
	if (lane < 0 || lane >= kLanes)
		return;
	laneFlash[lane] = 1.0f;
	laneAcc |= (uint8_t) (1u << lane);
	laneFlashAcc |= (uint8_t) (1u << lane);
	trigPulse = std::max(trigPulse, clampf(vel, 0.2f, 1.0f));
	laneSeen = true;
	if (lastSix >= 0)
		laneBarNow[lane] |= 1u << (lastSix & 15);
}

void Model::stepEntered(int s) {
	if (s < 0 || s >= kSteps)
		return;
	const int to = s + 1;   // state 0 = before step 1 (Pinball 2)
	const int from = lastTo < 0 ? 0 : lastTo;
	lastFrom = from;
	lastTo = to;
	step = s;
	learned[from][to] += 1.0f;
	learnedTotal += 1.0f;
	if (pathN < 16)
		pathBuf[pathN++] = (int8_t) s;
	else {
		std::memmove(pathBuf, pathBuf + 1, 15);
		pathBuf[15] = (int8_t) s;
	}
	stepFlashAcc |= (uint16_t) (1u << s);
	stepPulse = 1.0f;
	lastSeed = lastSeed * 1664525u + 1013904223u + (uint32_t) s;
	stepEventsPending++;
}

void Model::resetPulse() {
	lastTo = -1;
	lastFrom = -1;
	flashEnv = std::max(flashEnv, 0.9f);
}

void Model::flash(float a) { flashEnv = std::max(flashEnv, clamp01(a)); }

void Model::setBlob(const DataBlob* b) {
	blob = b;
	headBits = b ? b->firstLively() : 0;
	rowsSerial = ~0u;
	flashEnv = std::max(flashEnv, 0.8f);
}

void Model::setHeadNorm(float x01) { headCv = x01; }

void Model::setClock(double b, double tempo, bool play) {
	beats = b;
	bpm = std::clamp(tempo, 20.0, 400.0);
	playing = play;
}

// ---- audio analysis (Ingest::analyse) ------------------------------------------------------
void Model::analyse(float dt) {
	double sum = 0.0;
	float pk = 0.f;
	for (int i = 0; i < kWin; ++i) {
		const uint32_t idx = (wr + (uint32_t) i) & (kWin - 1);
		const float s = 0.5f * (ringL[idx] + ringR[idx]);
		sum += (double) s * s;
		pk = std::max(pk, std::max(std::fabs(ringL[idx]), std::fabs(ringR[idx])));
		re[(size_t) bitrev[(size_t) i]] = s * hann[(size_t) i];
		im[(size_t) bitrev[(size_t) i]] = 0.f;
	}
	// radix-2 FFT, in place (bit-reversed above)
	for (int len = 2; len <= kWin; len <<= 1) {
		const double ang = -2.0 * kPi / len;
		const float wr0 = (float) std::cos(ang), wi0 = (float) std::sin(ang);
		for (int i = 0; i < kWin; i += len) {
			float cr = 1.f, ci = 0.f;
			for (int k = 0; k < len / 2; ++k) {
				const size_t a = (size_t) (i + k), b = (size_t) (i + k + len / 2);
				const float tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
				re[b] = re[a] - tr;
				im[b] = im[a] - ti;
				re[a] += tr;
				im[a] += ti;
				const float nr = cr * wr0 - ci * wi0;
				ci = cr * wi0 + ci * wr0;
				cr = nr;
			}
		}
	}
	rms = rms * 0.6f + 0.4f * (float) std::sqrt(sum / kWin);
	peak = std::max(pk, peak * 0.92f);

	float fl = 0.f;
	double cNum = 0.0, cDen = 0.0;
	for (int b = 0; b < kBands; ++b) {
		const double f0 = 40.0 * std::pow(16000.0 / 40.0, (double) b / kBands);
		const double f1 = 40.0 * std::pow(16000.0 / 40.0, (double) (b + 1) / kBands);
		const int k0 = std::clamp((int) (f0 / sr * kWin), 1, kWin / 2 - 1);
		const int k1 = std::clamp((int) (f1 / sr * kWin) + 1, k0 + 1, kWin / 2);
		double mx = 0.0;
		for (int kk = k0; kk < k1; ++kk)
			mx = std::max(mx, std::sqrt((double) re[(size_t) kk] * re[(size_t) kk] + (double) im[(size_t) kk] * im[(size_t) kk]));
		const double amp = mx / (kWin * 0.25);
		cNum += amp * std::sqrt(f0 * f1);
		cDen += amp;
		const float db = (float) (20.0 * std::log10(std::max(amp, 1.0e-5)));
		const float v = clamp01((db + 78.0f) / 66.0f);
		bands[b] = v > bands[b] ? v : bands[b] * 0.86f + v * 0.14f;
		fl += std::max(0.0f, v - prevBands[b]);
		prevBands[b] = v;
	}
	centroid = cDen > 1.0e-9 ? (float) (cNum / cDen) : 0.f;
	flux = fl / kBands;
	fluxAvg = fluxAvg * 0.95f + flux * 0.05f;
	onset = (flux > fluxAvg * 2.2f + 0.012f && flux > 0.02f) ? 1.f : 0.f;
	float b0 = 0, b1 = 0, b2 = 0;
	for (int b = 0; b < kBands; ++b)
		(b < 12 ? b0 : (b < 32 ? b1 : b2)) += bands[b];
	bass = bass * 0.7f + 0.3f * b0 / 12.f;
	midA = midA * 0.7f + 0.3f * b1 / 20.f;
	highA = highA * 0.7f + 0.3f * b2 / 16.f;
	onsetEnv = std::max(onset, onsetEnv * 0.9f);
	kickPulse *= std::exp(-dt * 12.f);
	if (onset > 0.99f && bass > 0.45f)
		kickPulse = 1.f;   // a low onset is a kick
}

// ---- the read head --------------------------------------------------------------------------
void Model::advanceHead(float dt, const Settings& s) {
	const uint64_t nb = blob && blob->nbits ? blob->nbits : (1ull << 20);
	const int six = (int) (std::floor(beats * 4.0));
	if (headCv >= 0.f) {
		headBits = ((uint64_t) ((double) clamp01(headCv) * (double) (nb - 1)) / 64u) * 64u;
	}
	else {
		switch (s.scan) {
			case 1:   // step: a row per step entered
				headBits += 64u * (uint64_t) stepEventsPending;
				break;
			case 2:   // run
				scanAcc += (double) dt * (double) s.scanRate * 64.0;
				if (scanAcc >= 64.0) {
					const uint64_t rowsN = (uint64_t) (scanAcc / 64.0);
					headBits += rowsN * 64u;
					scanAcc -= (double) rowsN * 64.0;
				}
				break;
			case 3:   // follow: a row per 16th while the clock plays
				if (playing && lastSix >= 0 && six > lastSix)
					headBits += 64u * (uint64_t) std::min(16, six - lastSix);
				break;
			default: break;
		}
		headBits %= nb;
	}
	if (six != lastSix) {
		if (lastSix >= 0 && six > lastSix)
			sixCount += six - lastSix;
		lastSix = six;
	}
	stepEventsPending = 0;
}

// ---- 1/32-note histories (prepareScenes) -----------------------------------------------------
void Model::histories(Frame& f) {
	const double unit = 0.125;
	if (lastHistBeat < 0.0 || beats < lastHistBeat - 1.0)
		lastHistBeat = std::floor(beats / unit) * unit;
	int guard = 0;
	while (beats >= lastHistBeat + unit && guard++ < 64) {
		lastHistBeat += unit;
		histHead = (histHead + 1) % kHist;
		gateHist[histHead] = gatesNow;
		uint16_t rw = 0;
		const int col = (int) (sixCount & 63);
		for (int v = 0; v < kRows; ++v)
			if ((rows[v] >> col) & 1u)
				rw |= (uint16_t) (1u << v);
		rowHist[histHead] = rw;
		laneHist[histHead] = laneAcc;
		laneAcc = 0;
		specHead = (specHead + 1) % kSpecRows;
		for (int k = 0; k < kSpecBands; ++k)
			spec[specHead][k] = (uint8_t) std::clamp((int) (0.5f * (envBands[2 * k] + envBands[2 * k + 1]) * 255.f), 0, 255);
		if ((histHead & 1) == 0) {
			cvHead = (cvHead + 1) % kCvHist;
			for (int i = 0; i < 8; ++i)
				cvHist[i][cvHead] = (int8_t) std::clamp((int) (cvState[i] * 127.f), -127, 127);
		}
	}
	f.histHead = histHead;
	std::memcpy(f.laneHist, laneHist, sizeof laneHist);
	std::memcpy(f.rowHist, rowHist, sizeof rowHist);
	std::memcpy(f.gateHist, gateHist, sizeof gateHist);
	std::memcpy(f.spec, spec, sizeof spec);
	f.specHead = specHead;
	std::memcpy(f.cvHist, cvHist, sizeof cvHist);
	f.cvHead = cvHead;
}

// ---- the sections nobody sends (Ingest::synthesise) --------------------------------------------
void Model::synthesise(Frame& f, float dt, const Settings& s) {
	const uint64_t hb = headByte();
	const int six = (int) (sixCount & 0x7fffffff);
	const uint32_t ser = blob ? blob->serial : 0u;

	// automata: nine 64-cell rows seeded from the data, one generation every 16th
	{
		const uint64_t block = headBits / 576u;
		if (block != rowsBlock || ser != rowsSerial) {
			rowsBlock = block;
			rowsSerial = ser;
			for (int v = 0; v < kRows; ++v) {
				rows[v] = word64(block * 576u + (uint64_t) v * 64u) ^ (uint64_t) lastSeed * (0x9E3779B97F4A7C15ull + (uint64_t) v);
				if (rows[v] == 0)
					rows[v] = 1ull << (v * 7);
			}
		}
		if (six != rowsSix) {
			rowsSix = six;
			const int v = (int) (hash32((uint32_t) six * 2654435761u) % kRows);
			const int rule = kRules[hash32((uint32_t) six + (uint32_t) v * 31u) % 10u];
			if ((six & 3) == 0)
				rows[v] = generation(rows[v], rule);
		}
		f.autoStep = six & 63;
		for (int v = 0; v < kRows; ++v) {
			f.autoRows[v] = rows[v];
			const float band = bands[std::clamp(4 + v * 5, 0, kBands - 1)];
			const bool on = (rows[v] >> (f.autoStep & 63)) & 1u;
			autoEnv[v] = on ? std::max(autoEnv[v] * 0.9f, 0.25f + 0.75f * band) : autoEnv[v] * std::exp(-dt * 6.f);
			f.autoEnv[v] = (uint8_t) (clamp01(autoEnv[v]) * 255.f);
		}
	}
	// grid: 32 x 64 from the data, the column walking with the 16ths
	{
		const uint64_t base = (headBits / 64u) * 64u;
		uint32_t gates = 0;
		for (int r = 0; r < 32; ++r) {
			f.grid[r] = word64(base + (uint64_t) r * 64u);
			f.gridCol[r] = (uint8_t) ((six + r * 3) & 63);
			if ((f.grid[r] >> f.gridCol[r]) & 1u)
				gates |= 1u << r;
		}
		f.gates = gatesNow = gates;
		f.cursor = (six & 31) * 64 + (six & 63);
	}
	// buffers: three of 512 trits from the bytes (decimated to 64 for the screen)
	{
		for (int b = 0; b < 3; ++b) {
			const uint64_t off = hb + (uint64_t) b * 512u;
			for (int i = 0; i < 64; ++i) {
				int acc = 0;
				for (int k = 0; k < 8; ++k)
					acc += ((int) (byteAt(off + (uint64_t) (i * 8 + k)) % 3u) - 1);
				f.buf[b][i] = (int8_t) std::clamp(acc * 127 / 8, -127, 127);
			}
			const float g = std::max(laneFlash[b], bands[8 + b * 14]);
			bufGate[b] = std::max(g, bufGate[b] * std::exp(-dt * 8.f));
			f.bufGate[b] = (uint8_t) (clamp01(bufGate[b]) * 255.f);
		}
		f.bufPhase = (float) std::fmod(beats * 0.5, 1.0);
	}
	// matrix: the walk learned from the steps, else read from the bytes
	{
		f.learned = learnedTotal > 2.f;
		if (f.learned) {
			for (int i = 0; i < kStates; ++i) {
				float sum = 0.f;
				for (int j = 0; j < kStates; ++j) {
					learned[i][j] *= std::exp(-dt * 0.02f);
					sum += learned[i][j];
				}
				for (int j = 0; j < kStates; ++j)
					f.matrix[i][j] = (uint8_t) (sum > 0.f ? learned[i][j] * 100.f / sum : (j == (i % kSteps) + 1 ? 100 : 0));
			}
		}
		else {
			const uint64_t base = (hb / 289u) * 289u;
			for (int i = 0; i < kStates; ++i) {
				float w[kStates], sum = 0.f;
				for (int j = 0; j < kStates; ++j) {
					const int v = byteAt(base + (uint64_t) (i * kStates + j));
					w[j] = j == 0 ? 0.f : (v > 170 ? (float) (v - 170) : 0.f);
					sum += w[j];
				}
				if (sum <= 0.f) {
					w[(i % kSteps) + 1] = 100.f;
					sum = 100.f;
				}
				for (int j = 0; j < kStates; ++j)
					f.matrix[i][j] = (uint8_t) std::lround(w[j] * 100.f / sum);
			}
		}
		f.stepCur = step;
		f.lastFrom = lastFrom;
		f.lastTo = lastTo;
		f.pathN = pathN;
		std::memcpy(f.path, pathBuf, sizeof pathBuf);
	}
	// steps: from the bytes
	for (int i = 0; i < kSteps; ++i) {
		const uint64_t o = hb + 1024u + (uint64_t) i * 6u;
		const int t = byteAt(o) % 10;
		f.trig[i] = (int8_t) (t == 9 ? -1 : t);
		f.vel[i] = (uint8_t) (40 + byteAt(o + 1) % 88);
		f.prob[i] = (uint8_t) (30 + byteAt(o + 2) % 71);
	}
	// lanes: learned from the hits on the 16th grid, else from the bits
	{
		const int bar = (int) std::floor(beats / 4.0);
		if (bar != laneBar) {
			std::memcpy(laneBarLast, laneBarNow, sizeof laneBarNow);
			std::memset(laneBarNow, 0, sizeof laneBarNow);
			laneBar = bar;
		}
		f.lanesLearned = laneSeen;
		for (int l = 0; l < kLanes; ++l) {
			if (laneSeen) {
				f.lanePat[l] = (uint16_t) (laneBarLast[l] | laneBarNow[l]);
				f.laneFlip[l] = (uint16_t) ((laneBarLast[l] ^ laneBarNow[l]) & laneBarNow[l]);
			}
			else {
				f.lanePat[l] = (uint16_t) (word64((headBits / 64u) * 64u + (uint64_t) l * 16u) & 0xffffu);
				f.laneFlip[l] = 0;
			}
			laneLevel[l] = std::max(laneFlash[l], laneLevel[l] * std::exp(-dt * 5.f));
			f.laneLevel[l] = (uint8_t) (clamp01(laneLevel[l]) * 255.f);
		}
	}
	// reel: our own tape — the channel's peaks over the last two bars
	{
		const double unit = 8.0 / 256.0;
		tapeAcc = std::max(tapeAcc, peak);
		if (tapeLastBeat < 0.0 || beats < tapeLastBeat - 4.0)
			tapeLastBeat = beats;
		int guard = 0;
		while (beats >= tapeLastBeat + unit && guard++ < 256) {
			tapeLastBeat += unit;
			if (!s.freeze) {
				tape[tapeWrite] = clamp01(tapeAcc);
				tapeWrite = (tapeWrite + 1) & 255;
			}
			tapeAcc = 0.f;
		}
		for (int i = 0; i < kReel; ++i)
			f.reel[i] = (uint8_t) (std::max(tape[(tapeWrite + 2 * i) & 255], tape[(tapeWrite + 2 * i + 1) & 255]) * 255.f);
		const float barPh = (float) (beats / 4.0 - std::floor(beats / 4.0));
		for (int g = 0; g < 4; ++g) {
			const int splice = (int) (hash32((uint32_t) laneBar * 131u + (uint32_t) g * 17u + lastSeed) % 8u);
			f.gene[g] = std::fmod((float) splice / 8.f + barPh / 8.f * (1.f + (float) g * 0.25f), 1.f);
		}
	}
	// CV: eight slow sources out of the audio and the clock
	{
		auto& z = cvState;
		const double beatPh = beats - std::floor(beats);
		z[0] += (rms * 6.f - 1.f - z[0]) * (1.f - std::exp(-dt * 1.5f));
		if ((six & 3) == 0)
			z[1] = bands[hash32((uint32_t) six) % kBands] * 2.f - 1.f;
		z[2] = (float) std::fabs(std::sin(beats * kPi * 0.5)) * 2.f - 1.f;
		z[3] = (float) std::sin(beatPh * 2.0 * kPi);
		z[4] = std::max(onsetEnv * 2.f - 1.f, z[4] - dt * 3.f);
		logistic = (3.6f + 0.39f * clamp01(highA * 2.f)) * logistic * (1.f - logistic);
		if (logistic <= 0.f || logistic >= 1.f)
			logistic = 0.37f;
		z[5] += (logistic * 2.f - 1.f - z[5]) * (1.f - std::exp(-dt * 4.f));
		const float target = hashf((uint32_t) (beats * 2.0) * 977u) * 2.f - 1.f;
		z[6] += (target - z[6]) * (1.f - std::exp(-dt * 2.f));
		z[7] = (float) byteAt(hb + 800u + (uint64_t) (six & 15)) / 127.5f - 1.f;
		for (int i = 0; i < 8; ++i) {
			z[i] = clampf(z[i], -1.f, 1.f);
			f.cv[i] = (int8_t) (z[i] * 127.f);
		}
	}
	// voices: the kick, the tone, the ghost net's four nodes
	{
		voiceKick = std::max(kickPulse, voiceKick * std::exp(-dt * 5.f));
		f.voices[0] = voiceKick;
		f.voices[1] = centroid > 0.f ? centroid : 220.f;
		f.voices[2] = midA;
		for (int k = 0; k < 4; ++k) {
			float sum = 0.f;
			for (int b = k * 12; b < k * 12 + 12; ++b)
				sum += bands[b];
			voiceBands[k] = std::max(sum / 12.f, voiceBands[k] * std::exp(-dt * 3.f));
			f.voices[3 + k] = voiceBands[k];
		}
		f.voices[7] = blob ? (float) (blob->entropy / 8.0) : 0.5f;
	}
}

// ---- the file under the head ---------------------------------------------------------------
void Model::dataView(Frame& f) {
	f.haveData = blob != nullptr;
	f.head = headBits;
	f.headByte = headByte();
	if (blob) {
		copyStr(f.name, sizeof f.name, blob->name);
		copyStr(f.type, sizeof f.type, blob->typeName);
		copyStr(f.note, sizeof f.note, blob->kindNote);
		f.nbits = blob->nbits;
		f.fileSize = blob->fileSize;
		f.entropy = (float) blob->entropy;
		const size_t nb = blob->blockEntropy.size();
		for (int k = 0; k < 64; ++k)
			f.entMap[k] = nb ? (uint8_t) (clamp01(blob->blockEntropy[(size_t) k * nb / 64u]) * 255.f) : 0;
	}
	else {
		std::snprintf(f.name, sizeof f.name, "NO FILE");
		std::snprintf(f.type, sizeof f.type, "HASH");
		f.note[0] = 0;
		f.nbits = 1ull << 20;
		f.fileSize = 0;
		f.entropy = 8.f;
		for (int k = 0; k < 64; ++k)
			f.entMap[k] = (uint8_t) (200 + (hash32((uint32_t) k) & 55));
	}
	const uint64_t hb = f.headByte;
	uint32_t h[256]{};
	for (int i = 0; i < 256; ++i) {
		f.bytes[i] = byteAt(hb + (uint64_t) i);
		h[f.bytes[i]]++;
	}
	uint32_t acc = 0;
	f.cdf[0] = 0;
	for (int k = 0; k < 256; ++k) {
		acc += h[k];
		f.cdf[k + 1] = (uint8_t) std::min<uint32_t>(255u, acc);
	}
	for (int r = 0; r < 8; ++r)
		f.bits[r] = word64(headBits + (uint64_t) r * 64u);
	for (int k = 0; k < 4; ++k)
		f.str[k][0] = 0;
	if (blob && !blob->strings.empty()) {
		const auto& S = blob->strings;
		auto it = std::lower_bound(S.begin(), S.end(), hb, [](const pdata::FoundString& s, uint64_t o) { return s.offset < o; });
		size_t i = (size_t) (it - S.begin());
		for (int k = 0; k < 4; ++k)
			copyStr(f.str[k], sizeof f.str[k], S[(i + (size_t) k) % S.size()].text);
	}
}

// ---- the Lorenz attractor (ScenesCase attractor): rho by the file's entropy, time by the sound ---
void Model::attractor(float dt, const Settings& s) {
	const double rho = 24.0 + 8.0 * (blob ? blob->entropy / 8.0 : 0.5);
	const double h = 0.0035 * (0.4 + 2.0 * level) * std::max(0.1f, s.speed);
	const int n = std::max(1, (int) std::lround(dt * 240.f));
	for (int i = 0; i < n; ++i) {
		const double dx = 10.0 * (ly - lx), dy = lx * (rho - lz) - ly, dz = lx * ly - 8.0 / 3.0 * lz;
		lx += dx * h;
		ly += dy * h;
		lz += dz * h;
	}
	if (!std::isfinite(lx) || !std::isfinite(ly) || !std::isfinite(lz) || std::fabs(lx) > 200.0) {
		lx = 0.1;
		ly = 0.0;
		lz = 20.0;
	}
	trailHead = (trailHead + 1) % kTrail;
	trailX[trailHead] = (float) (lx / 25.0);
	trailY[trailHead] = (float) (ly / 30.0);
	trailZ[trailHead] = (float) ((lz - 25.0) / 25.0);
}

// ---- once per display frame ----------------------------------------------------------------
void Model::update(Frame& f, float dt, const Settings& s) {
	dt = clampf(dt, 0.0005f, 0.1f);
	analyse(dt);
	advanceHead(dt, s);

	// motion envelopes (Motion::advance)
	auto follow = [dt](float& e, float target, float attack, float release) {
		const float tau = target > e ? attack : release;
		e += (target - e) * (1.f - std::exp(-dt / tau));
	};
	const float rk = clampf(s.react, 0.f, 3.f);
	follow(level, clamp01(rms * 3.5f * rk), 0.02f, 0.25f);
	float lo = 0, mi = 0, hi = 0;
	for (int i = 0; i < kBands; ++i) {
		follow(envBands[i], clamp01(bands[i] * (0.4f + 0.6f * rk)), 0.015f, 0.18f);
		(i < 12 ? lo : (i < 32 ? mi : hi)) += envBands[i];
	}
	follow(low, lo / 12.f, 0.02f, 0.2f);
	follow(mid, mi / 20.f, 0.02f, 0.2f);
	follow(high, hi / 16.f, 0.02f, 0.2f);
	const float eighth = (float) (30.0 / bpm);
	const float kickNow = std::max(onset > 0.98f ? 1.f : 0.f, kickPulse > 0.98f ? 1.f : 0.f);
	kick = std::max(kick * std::exp(-dt / (eighth * 0.5f)), kickNow * std::min(1.f, rk));
	hit = std::max(hit * std::exp(-dt / (eighth * 0.4f)), trigPulse);
	stepEnv = std::max(stepEnv * std::exp(-dt / (eighth * 0.5f)), stepPulse);
	trigPulse = 0.f;
	stepPulse = 0.f;
	if (busEnergy >= 0.f)
		follow(energy, clamp01(busEnergy), 0.05f, 0.4f);
	else
		energy = level;

	synthesise(f, dt, s);
	histories(f);
	attractor(dt, s);
	dataView(f);

	// the clock and the envelopes
	f.serial = ++serial;
	f.beats = beats;
	f.bpm = (float) bpm;
	f.playing = playing;
	f.bar = (int) std::floor(beats / 4.0);
	f.beatPhase = (float) (beats - std::floor(beats));
	f.barPhase = (float) (beats / 4.0 - std::floor(beats / 4.0));
	f.sixteenth = std::clamp((int) (f.barPhase * 16.f), 0, 15);
	f.level = level;
	f.low = low;
	f.mid = mid;
	f.high = high;
	f.kick = kick;
	f.hit = hit;
	f.step = stepEnv;
	f.flash = flashEnv;
	f.energy = energy;
	f.centroid = centroid;
	for (int i = 0; i < kBands; ++i)
		f.bands[i] = (uint8_t) (clamp01(envBands[i]) * 255.f);
	for (int i = 0; i < kScope; ++i) {
		const uint32_t idx = (wr + (uint32_t) (i * (kWin / kScope))) & (kWin - 1);
		f.wave[i] = (int8_t) std::clamp((int) (0.5f * (ringL[idx] + ringR[idx]) * 127.f), -127, 127);
	}
	for (int i = 0; i < 64; ++i) {
		const uint32_t idx = (wr + (uint32_t) (kWin - 256 + i * 4)) & (kWin - 1);
		f.waveL[i] = (int8_t) std::clamp((int) (ringL[idx] * 127.f), -127, 127);
		f.waveR[i] = (int8_t) std::clamp((int) (ringR[idx] * 127.f), -127, 127);
	}
	f.stepFlash = stepFlashAcc;
	f.laneFlash = laneFlashAcc;
	stepFlashAcc = 0;
	laneFlashAcc = 0;
	std::memcpy(f.attrX, trailX, sizeof trailX);
	std::memcpy(f.attrY, trailY, sizeof trailY);
	std::memcpy(f.attrZ, trailZ, sizeof trailZ);
	f.attrHead = trailHead;

	// decays
	for (int l = 0; l < kLanes; ++l)
		laneFlash[l] *= std::exp(-dt * 10.f);
	flashEnv *= std::exp(-dt * 14.f);
}

// ================================================================ Director

void Director::cutTo(int s) {
	s = ((s % kNumScenes) + kNumScenes) % kNumScenes;
	if (s != scene) {
		scene = s;
		cuts++;
	}
}

void Director::next(int pool, bool random, uint32_t salt) {
	int cand[kNumScenes], n = 0;
	for (int i = 0; i < kNumScenes; ++i)
		if (inPool(pool, i))
			cand[n++] = i;
	if (n == 0)
		return;
	int pick;
	if (random) {
		pick = cand[hash32(salt * 2654435761u + cuts * 977u) % (uint32_t) n];
		if (pick == scene && n > 1)
			pick = cand[(std::find(cand, cand + n, pick) - cand + 1) % n];
	}
	else {
		pick = cand[0];
		for (int k = 0; k < n; ++k)
			if (cand[k] > scene) {
				pick = cand[k];
				break;
			}
	}
	scene = pick;
	cuts++;
}

bool Director::tick(const Frame& f, int change, int pool, bool stepEvent, bool onsetEvent, bool turnEvent) {
	const uint32_t before = cuts;
	const bool newBar = f.bar != lastBar && lastBar != -1 && f.playing;
	lastBar = f.bar;
	switch (change) {
		case 1: case 2: case 3: case 4: {
			const int every = 1 << (change - 1);
			if (newBar && (f.bar % every) == 0)
				next(pool, false, (uint32_t) f.bar);
			break;
		}
		case 5:
			if (stepEvent)
				next(pool, false, (uint32_t) f.serial);
			break;
		case 6:   // random: a third of the bars, to a random scene
			if (newBar && hash32((uint32_t) f.bar * 7919u) % 3u == 0)
				next(pool, true, (uint32_t) f.bar);
			break;
		case 7:   // on onset, at most once a beat
			if (onsetEvent && f.beats - lastOnsetBeat >= 1.0) {
				lastOnsetBeat = f.beats;
				next(pool, true, (uint32_t) f.serial);
			}
			break;
		case 8:
			if (turnEvent)
				next(pool, false, (uint32_t) f.bar);
			break;
		default: break;
	}
	return cuts != before;
}

int Director::variant(int knob) const {
	const int nv = sceneInfo(scene).numVariants;
	if (knob <= 0)
		return (int) (hash32(cuts * 7u + (uint32_t) scene) % (uint32_t) nv);
	return std::min(knob - 1, nv - 1);
}

} // namespace dsc

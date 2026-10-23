// FLOATDAT — Rack / MetaModule port of FLOATDAT 2.0 (IKEDAR x FLOATER x VORTESSA x JDAT x Pinball 2; engines:
// engines/floatdat (IKD, VTX, JDAT, Pinball, DataBlob, the processor as fdat::Instrument) and engines/floater
// (FLOATER v0.7 + its patch layer; the plug-in carried a v0.4 copy, see engines/floatdat/sources.mk)).
//
// Any file is the material: LOAD (menu) reads it as bytes / text / audio (WAV) into bits and symbols; a read
// head feeds IKD's automata, FLOATER's lanes and sound (DATA PATCH), VTX's tuning and JDAT's buffers. The
// Pinball 2 Markov sequencer fires eight lanes; each lane does something inside (its DESTINATION) and comes out
// as a gate (the plug-in's MIDI notes: T1..T8, 10 V for the note's duration).
// Panel: the four layers' levels, MASTER, BPM, TIME MULT, SWING, PATTERN, SCAN RATE, RESET, DATA PATCH,
// MUTATE (FLT), SCENE, RUN. Screens: VISUAL (datamatic scenes of the file under the head, cut and flashed by
// the lanes), PINBALL (the 17 x 17 matrix and the ball), DATA (file, type, entropy, head).
// Menu-only: every page of the plug-in (the 16 steps, lane destinations / mutes, matrix shapes, IKD, FLT,
// VTX, JDAT, DATA, the mix switches, the ten presets). Dropped: the full-screen video output, the CASE view,
// 17 of the 22 scenes (five are drawn: barcode, matrix, scan, numbers, cells), palettes, MIDI (gates and the
// LANES input instead), user preset files, the file bytes inside the patch (the path is saved), images as
// images (read as bytes), audio formats other than WAV.
// Position: CLK = 1/16 notes (RST = bar 1) as the plug-in's host clock, else its internal clock at BPM while RUN.

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/Sample.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/FloatdatLayout.hpp"
#include "floatdat/Data.h"
#include "floatdat/Instrument.h"

#include <cstring>

namespace L = semi::layout::floatdat;
using semi::Screen;
namespace pb = fdat_pinball;
using fdat_pdata::DataBlob;

namespace {

constexpr int kBlock = 64;
constexpr float kVolts = 5.f;
#ifdef METAMODULE
constexpr size_t kMaxFileBytes = 256u * 1024u;       // bits + symbols: ~1.5 MB in RAM
constexpr size_t kMaxWavFrames = 48000u * 10u;
#else
constexpr size_t kMaxFileBytes = 2u * 1024u * 1024u;
constexpr size_t kMaxWavFrames = 48000u * 60u;
#endif
constexpr int kScenes = 5;
const char* kSceneNames[kScenes] = {"BARCODE", "MATRIX", "SCAN", "NUMBERS", "CELLS"};

struct Snapshot {
	int scene = 0;
	bool flash = false, playing = false, loaded = false;
	uint64_t rows[24]{};
	float sym[64]{};
	uint8_t bytes[24]{};
	uint64_t headByte = 0;
	char found[28] = "";
	char name[30] = "", type[22] = "", note[34] = "";
	float entropy = 0.f, headFrac = 0.f;
	uint64_t size = 0;
	uint32_t crc = 0;
	uint64_t cells[9]{};
	int ikdStep = -1;
	float matrix[pb::kStates][pb::kStates]{};
	int step = -1;
	uint8_t stepFlash[pb::kSteps]{}, laneFlash[pb::kLanes]{};
	float rms[5]{};
	float bpm = 120.f;
	double ppq = 0.0;
};

} // namespace

struct Floatdat : Module {
	enum ParamId { IKD_PARAM, FLT_PARAM, VTX_PARAM, JDAT_PARAM, MASTER_PARAM, BPM_PARAM, MULT_PARAM, SWING_PARAM, PATLEN_PARAM, RATE_PARAM,
	               RESET_PARAM, DPATCH_PARAM, MUTATE_PARAM, SCENE_PARAM, RUN_PARAM, PARAMS_LEN };
	enum InputId { CLK_INPUT, RST_INPUT, RUN_INPUT, LANES_INPUT, RESET_INPUT, DPATCH_INPUT, SCENE_INPUT, MULT_INPUT, INPUTS_LEN };
	enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, ENUMS(GATE_OUTPUT, 8), OUTPUTS_LEN };
	enum LightId { ENUMS(LANE_LIGHT, 8), RUN_LIGHT, LOAD_LIGHT, LIGHTS_LEN };

	fdat::Instrument inst;
	semi::Slot<DataBlob> slot;
	semi::Loader<DataBlob> loader;
	std::string filePath;           // what is loaded ("" = an example text)
	int example = 1;
	std::atomic<int64_t> savedHead{-1};

	// requests from the menu (applied at a block start)
	std::atomic<int> reqPreset{-1}, reqFn{-1}, reqIkd{-1};
	std::atomic<bool> reqFltNew{false}, reqPull{true};

	dsp::BooleanTrigger resetBtn, dpatchBtn, mutBtn, sceneBtn, runBtn;
	dsp::SchmittTrigger clkTrig, rstTrig, resetIn, dpatchIn, sceneIn, laneIn[8];

	// transport
	int64_t step = -1;
	float sinceClk = 0.f, period = 0.f;
	bool clkSeen = false;

	// blocks
	float outBuf[2][kBlock]{};
	int pos = 0;
	fdat::LaneGate gates[256];
	int numGates = 0;
	int gateLeft[8]{};
	float sampleRate = 44100.f;

	// visuals
	int scene = 0;
	int flashFrames = 0;
	uint32_t seenCuts = 0, seenFlashes = 0, seenSteps[pb::kSteps]{}, seenLanes[pb::kLanes]{};
	int stepFlash[pb::kSteps]{}, laneFlash[pb::kLanes]{};
	int64_t lastBar = -1;
	float lastFlashTime = 1e9f;
	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;

	~Floatdat() override {
		// background jobs first (and wait for a run in flight): they write into this module
		loader.stopAndWait();
	}

	Floatdat() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(IKD_PARAM, -60.f, 6.f, 0.f, "CELLULA level", " dB");
		configParam(FLT_PARAM, -60.f, 6.f, -2.f, "FLT (FLOATER) level", " dB");
		configParam(VTX_PARAM, -60.f, 6.f, -4.f, "VTX (Vortessa) level", " dB");
		configParam(JDAT_PARAM, -60.f, 6.f, -3.f, "DATUM level", " dB");
		configParam(MASTER_PARAM, -60.f, 6.f, -3.f, "Master", " dB");
		configParam(BPM_PARAM, 20.f, 300.f, 120.f, "Tempo (without CLK)", " BPM");
		configParam(MULT_PARAM, 0.25f, 4.f, 1.f, "Pinball time multiplier", "x");
		configParam(SWING_PARAM, 0.f, 1.f, 0.f, "Pinball swing", " %", 0.f, 100.f);
		configParam(PATLEN_PARAM, 0.5f, 64.f, 1.f, "Pattern length (the ball goes home)", " bars");
		configParam(RATE_PARAM, 0.1f, 64.f, 2.f, "Scan rate (SCAN = run)", " rows/s");
		configButton(RESET_PARAM, "Pinball reset");
		configButton(DPATCH_PARAM, "FLT DATA PATCH (the data under the head becomes FLOATER's patch)");
		configButton(MUTATE_PARAM, "FLT MUTATE");
		configButton(SCENE_PARAM, "Next scene");
		configButton(RUN_PARAM, "Run (internal clock)");
		configInput(CLK_INPUT, "Clock (1/16 notes)");
		configInput(RST_INPUT, "Reset (next clock = bar 1; the ball goes home)");
		configInput(RUN_INPUT, "Run gate");
		configInput(LANES_INPUT, "Lanes: poly channel n fires lane n (mono: lane 1)");
		configInput(RESET_INPUT, "Pinball reset trigger");
		configInput(DPATCH_INPUT, "DATA PATCH trigger");
		configInput(SCENE_INPUT, "Next scene trigger");
		configInput(MULT_INPUT, "Time multiplier CV (x2 per 2.5 V)");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		for (int i = 0; i < 8; i++) {
			configOutput(GATE_OUTPUT + i, string::f("Lane T%d gate (the note, 10 V)", i + 1));
			configLight(LANE_LIGHT + i, string::f("T%d", i + 1));
		}
		inst.setSeed((uint32_t) random::u32());
		inst.prepare(44100.0);
		loader.attach(this, &slot, [this](const std::string& path, DataBlob& b) { return buildBlob(path, b); });
		loadExample(1);
	}

	// ---- data -------------------------------------------------------------------------------

	// off the audio thread: the file as bytes; WAV decoded for READ AS AUDIO / AUTO
	bool buildBlob(const std::string& path, DataBlob& b) {
		std::vector<uint8_t> bytes;
		if (!semi::readFileBytes(path, bytes, kMaxFileBytes))
			return false;
		const auto as = (fdat_pdata::ReadAs) inst.S.dataAs;
		const bool wav = bytes.size() > 12 && !std::memcmp(bytes.data(), "RIFF", 4) && !std::memcmp(bytes.data() + 8, "WAVE", 4);
		std::vector<float> mono;
		double rate = 0.0;
		int ch = 0;
		if (wav && (as == fdat_pdata::ReadAs::Auto || as == fdat_pdata::ReadAs::Audio)) {
			semi::Sample s;
			if (semi::readWav(path, s, kMaxWavFrames)) {
				mono.resize(s.frames);
				for (size_t i = 0; i < s.frames; i++)
					mono[i] = s.mono(i);
				rate = s.rate;
				ch = s.channels;
			}
		}
		FILE* f = std::fopen(path.c_str(), "rb");
		uint64_t size = 0;
		if (f) {
			std::fseek(f, 0, SEEK_END);
			size = (uint64_t) std::max(0L, std::ftell(f));
			std::fclose(f);
		}
		return fdat_pdata::fromBytes(b, std::move(bytes), semi::fileName(path), path, size, as, mono.empty() ? nullptr : &mono, rate, ch);
	}

	void loadFile(const std::string& path) {
		filePath = path;
		loader.request(path);
	}

	void loadExample(int i) {
		example = clamp(i, 1, 9);
		filePath.clear();
		auto* b = new DataBlob;
		const auto as = (fdat_pdata::ReadAs) inst.S.dataAs;
		fdat_pdata::fromText(*b, fdat_pdata::exampleText(example), fdat_pdata::exampleName(example), as == fdat_pdata::ReadAs::Auto ? fdat_pdata::ReadAs::Text : as);
		slot.collect();
		slot.offer(b);
	}

	void reinterpret() {
		if (filePath.empty())
			loadExample(example);
		else
			loadFile(filePath);
	}

	// ---- panel <-> settings -------------------------------------------------------------------

	void pullKnobs() {
		auto& S = inst.S;
		params[IKD_PARAM].setValue(S.lvlIkd);
		params[FLT_PARAM].setValue(S.lvlFlt);
		params[VTX_PARAM].setValue(S.lvlVtx);
		params[JDAT_PARAM].setValue(S.lvlJdat);
		params[MASTER_PARAM].setValue(S.master);
		params[BPM_PARAM].setValue(S.bpm);
		params[MULT_PARAM].setValue(S.pbMult);
		params[SWING_PARAM].setValue(S.pbSwing);
		params[PATLEN_PARAM].setValue(S.pbLen);
		params[RATE_PARAM].setValue(S.dataRate);
	}

	void pushKnobs() {
		auto& S = inst.S;
		S.lvlIkd = params[IKD_PARAM].getValue();
		S.lvlFlt = params[FLT_PARAM].getValue();
		S.lvlVtx = params[VTX_PARAM].getValue();
		S.lvlJdat = params[JDAT_PARAM].getValue();
		S.master = params[MASTER_PARAM].getValue();
		S.bpm = params[BPM_PARAM].getValue();
		S.pbSwing = params[SWING_PARAM].getValue();
		S.pbLen = std::round(params[PATLEN_PARAM].getValue() * 2.f) * 0.5f;
		S.dataRate = params[RATE_PARAM].getValue();
		S.pbMult = clamp(params[MULT_PARAM].getValue() * std::pow(2.f, inputs[MULT_INPUT].getVoltage() / 2.5f), 0.25f, 4.f);
	}

	// ---- Rack ---------------------------------------------------------------------------------

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		sampleRate = e.sampleRate;
		inst.prepare(e.sampleRate);
		pos = 0;
		numGates = 0;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		reqPreset = 0;
	}

	json_t* dataToJson() override;
	void dataFromJson(json_t* root) override;

	// ---- the block ------------------------------------------------------------------------------

	bool clockPosition(float dt, double& ppq, double& bpm) {
		sinceClk += dt;
		if (clkTrig.process(inputs[CLK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (clkSeen)
				period = sinceClk;
			clkSeen = true;
			step++;
			sinceClk = 0.f;
		}
		if (!inputs[CLK_INPUT].isConnected()) {
			clkSeen = false;
			period = 0.f;
			return false;
		}
		if (period > 0.f && sinceClk > 2.5f * period) {
			clkSeen = false;
			period = 0.f;
		}
		if (step < 0 || period <= 0.f)
			return false;
		bpm = 60.0 / (4.0 * period);
		ppq = (step + std::min(sinceClk / period, 0.999f)) * 0.25;
		return true;
	}

	void runBlock(const ProcessArgs& args) {
		if (slot.poll())
			inst.setBlob(slot.active);
		const int64_t sh = savedHead.exchange(-1);
		if (sh >= 0)
			inst.requestHead((uint64_t) sh);
		const int pr = reqPreset.exchange(-1);
		if (pr >= 0) {
			inst.loadFactory(pr);
			reqPull = true;
		}
		const int fn = reqFn.exchange(-1);
		if (fn >= 0)
			inst.pinballFunction((fdat::Instrument::Fn) fn);
		const int ik = reqIkd.exchange(-1);
		if (ik >= 0) {
			if (ik < 3)
				inst.ikdRandomise((fdat::Instrument::IkdRandom) ik);
			else
				inst.ikdMutate(-1);
		}
		if (reqFltNew.exchange(false))
			inst.requestFltNewPatch();
		if (reqPull.exchange(false))
			pullKnobs();
		pushKnobs();
		if (inputs[RUN_INPUT].isConnected())
			inst.S.run = inputs[RUN_INPUT].getVoltage() > 1.f;

		fdat::Transport t;
		double ppq = 0.0, bpm = 120.0;
		if (inputs[CLK_INPUT].isConnected()) {
			t.host = true;
			t.playing = clockPosition(args.sampleTime * kBlock, ppq, bpm) && inst.S.run;
			t.ppq = ppq;
			t.bpm = bpm;
		}
		else
			clockPosition(args.sampleTime * kBlock, ppq, bpm);
		inst.process(outBuf[0], outBuf[1], kBlock, t);
		numGates = std::min(inst.numGates(), 256);
		for (int k = 0; k < numGates; k++)
			gates[k] = inst.gates()[k];
	}

	void process(const ProcessArgs& args) override {
		if (rstTrig.process(inputs[RST_INPUT].getVoltage(), 0.1f, 1.f)) {
			step = -1;
			inst.requestReset();
		}
		if (resetBtn.process(params[RESET_PARAM].getValue() > 0.f) | resetIn.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f))
			inst.requestReset();
		if (dpatchBtn.process(params[DPATCH_PARAM].getValue() > 0.f) | dpatchIn.process(inputs[DPATCH_INPUT].getVoltage(), 0.1f, 1.f))
			inst.requestFltPatch();
		if (mutBtn.process(params[MUTATE_PARAM].getValue() > 0.f))
			inst.requestFltMutate();
		if (sceneBtn.process(params[SCENE_PARAM].getValue() > 0.f) | sceneIn.process(inputs[SCENE_INPUT].getVoltage(), 0.1f, 1.f))
			scene = (scene + 1) % kScenes;
		if (runBtn.process(params[RUN_PARAM].getValue() > 0.f))
			inst.S.run = !inst.S.run;
		const int lc = inputs[LANES_INPUT].getChannels();
		for (int c = 0; c < std::min(lc, 8); c++)
			if (laneIn[c].process(inputs[LANES_INPUT].getVoltage(c), 0.1f, 1.f))
				inst.fireLaneNow(c, 100);

		outputs[OUT_L_OUTPUT].setVoltage(outBuf[0][pos] * kVolts);
		outputs[OUT_R_OUTPUT].setVoltage(outBuf[1][pos] * kVolts);
		for (int k = 0; k < numGates; k++)
			if (gates[k].offset == pos)
				gateLeft[gates[k].lane] = gates[k].velocity > 0 ? std::max(1, (int) (gates[k].durMs * 0.001f * args.sampleRate)) : 0;
		for (int i = 0; i < 8; i++) {
			outputs[GATE_OUTPUT + i].setVoltage(gateLeft[i] > 0 ? 10.f : 0.f);
			if (gateLeft[i] > 0)
				gateLeft[i]--;
		}
		if (++pos >= kBlock) {
			pos = 0;
			runBlock(args);
		}
		if (ui.tick(args.sampleTime))
			publish(args.sampleTime);
	}

	void publish(float dt) {
		const auto& T = inst.tele;
		Snapshot& s = snap.writeSlot();
		// scene cuts and flashes from the lanes, the auto cut
		if (T.sceneCuts != seenCuts) {
			seenCuts = T.sceneCuts;
			scene = inst.S.visAuto == 4 ? (int) (random::u32() % kScenes) : (scene + 1) % kScenes;
		}
		const int64_t bar = (int64_t) std::floor(T.ppq / 4.0);
		if (T.playing && bar != lastBar) {
			if ((inst.S.visAuto == 1) || (inst.S.visAuto == 2 && bar % 4 == 0))
				scene = (scene + 1) % kScenes;
			lastBar = bar;
		}
		lastFlashTime += 1.f / semi::platform::kUiRateHz;
		if (T.flashes != seenFlashes) {
			seenFlashes = T.flashes;
			// the strobe limit (3 a second), as the plug-in's default
			if (inst.S.visFlash && lastFlashTime > 0.33f) {
				flashFrames = 2;
				lastFlashTime = 0.f;
			}
		}
		s.flash = flashFrames > 0;
		if (flashFrames > 0)
			flashFrames--;
		s.scene = scene;
		s.playing = T.playing;
		s.bpm = (float) T.bpm;
		s.ppq = T.ppq;
		for (int l = 0; l < pb::kLanes; l++) {
			if (T.laneHits[(size_t) l] != seenLanes[l]) {
				seenLanes[l] = T.laneHits[(size_t) l];
				laneFlash[l] = 3;
			}
			s.laneFlash[l] = (uint8_t) std::max(0, laneFlash[l]);
			lights[LANE_LIGHT + l].setBrightness(laneFlash[l] > 0 ? 1.f : 0.f);
			if (laneFlash[l] > 0)
				laneFlash[l]--;
		}
		for (int st = 0; st < pb::kSteps; st++) {
			if (T.stepFires[(size_t) st] != seenSteps[st]) {
				seenSteps[st] = T.stepFires[(size_t) st];
				stepFlash[st] = 3;
				if (inst.S.visAuto == 3 && st == 0)
					scene = (scene + 1) % kScenes;
			}
			s.stepFlash[st] = (uint8_t) std::max(0, stepFlash[st]);
			if (stepFlash[st] > 0)
				stepFlash[st]--;
		}
		lights[RUN_LIGHT].setBrightness(inst.S.run ? 1.f : 0.f);
		s.step = inst.pinball.currentStep.load(std::memory_order_relaxed);
		for (int x = 0; x < pb::kStates; x++)
			for (int y = 0; y < pb::kStates; y++)
				s.matrix[x][y] = inst.pinball.matrix[(size_t) x][(size_t) y].load(std::memory_order_relaxed);
		s.rms[0] = T.rmsIkd;
		s.rms[1] = T.rmsFlt;
		s.rms[2] = T.rmsVtx;
		s.rms[3] = T.rmsJdat;
		s.rms[4] = T.rmsOut;
		for (int v = 0; v < 9; v++)
			s.cells[v] = inst.ikdShared.voices[(size_t) v].cells.load(std::memory_order_relaxed);
		s.ikdStep = inst.ikdShared.step.load(std::memory_order_relaxed);

		const DataBlob* b = inst.blob();
		s.loaded = b != nullptr && b->nbits > 0;
		lights[LOAD_LIGHT].setBrightness(loader.busy ? 1.f : (loader.failed ? 0.4f : 0.f));
		if (s.loaded) {
			const uint64_t h = inst.head;
			const uint64_t row0 = (h / 64u) * 64u;
			for (int r = 0; r < 24; r++)
				s.rows[r] = b->word64(row0 + (uint64_t) r * 64u);
			const uint64_t sp = b->symbols.empty() ? 0 : (uint64_t) ((double) h / (double) b->nbits * (double) b->symbols.size());
			for (int k = 0; k < 64; k++)
				s.sym[k] = b->symbolAt(sp + (uint64_t) k);
			s.headByte = b->byteForBit(h);
			for (int k = 0; k < 24; k++)
				s.bytes[k] = b->byteAt(s.headByte + (uint64_t) k);
			s.found[0] = 0;
			for (const auto& fs : b->strings)
				if (fs.offset >= s.headByte) {
					std::snprintf(s.found, sizeof s.found, "%s", fs.text.c_str());
					break;
				}
			std::snprintf(s.name, sizeof s.name, "%s", b->name.c_str());
			std::snprintf(s.type, sizeof s.type, "%s", b->typeName.c_str());
			std::snprintf(s.note, sizeof s.note, "%s", b->kindNote.c_str());
			s.entropy = (float) b->entropy;
			s.size = b->fileSize;
			s.crc = b->crc32;
			s.headFrac = (float) ((double) h / (double) b->nbits);
		}
		snap.publish();
		(void) dt;
	}
};

json_t* Floatdat::dataToJson() {
	const auto& S = inst.S;
	json_t* root = json_object();
	json_object_set_new(root, "file", json_string(filePath.c_str()));
	json_object_set_new(root, "example", json_integer(example));
	json_object_set_new(root, "head", json_integer((json_int_t) inst.head));
	json_object_set_new(root, "preset", json_integer(inst.factoryIndex));
	json_object_set_new(root, "scene", json_integer(scene));
	// the plug-in's parameters
	json_t* p = json_object();
	auto I = [&](const char* k, int v) { json_object_set_new(p, k, json_integer(v)); };
	auto F = [&](const char* k, float v) { json_object_set_new(p, k, json_real(v)); };
	auto B = [&](const char* k, bool v) { json_object_set_new(p, k, json_boolean(v)); };
	B("run", S.run); B("clip", S.clip); B("limit", S.limit); B("dcblock", S.dcBlock); I("veldepth", S.velDepth); B("pb_out", S.pbOut);
	I("pb_start", S.pbStart);
	I("ikd_clk", S.ikdClk); I("ikd_div", S.ikdDiv); F("ikd_swing", S.ikdSwing); B("ikd_hold", S.ikdHold); I("ikd_auto", S.ikdAuto);
	I("ikd_chance", S.ikdChance); F("ikd_wave", S.ikdWave); F("ikd_spec", S.ikdSpec); F("ikd_sine", S.ikdSine); I("ikd_feed", S.ikdFeed); B("ikd_feedsnd", S.ikdFeedSnd);
	I("flt_clk", S.fltClk); I("flt_feed", S.fltFeed); B("flt_feedsnd", S.fltFeedSnd); B("flt_feedmodel", S.fltFeedModel); I("flt_vel", S.fltVel);
	I("flt_burst", S.fltBurst); I("flt_map", S.fltMap);
	F("vtx_kick_lvl", S.vtxKickLvl); F("vtx_tone_lvl", S.vtxToneLvl); F("vtx_ghost_lvl", S.vtxGhostLvl); F("vtx_drift_lvl", S.vtxDriftLvl);
	B("vtx_kick_own", S.vtxKickOwn); F("vtx_kick_rate", S.vtxKickRate); F("vtx_kick_prob", S.vtxKickProb); F("vtx_kick_pitch", S.vtxKickPitch);
	F("vtx_kick_decay", S.vtxKickDecay); F("vtx_kick_drive", S.vtxKickDrive); F("vtx_kick_rev", S.vtxKickRev); F("vtx_kick_pre", S.vtxKickPre);
	B("vtx_tone_own", S.vtxToneOwn); F("vtx_tone_prob", S.vtxToneProb); F("vtx_tone_freq", S.vtxToneFreq); F("vtx_tone_fb", S.vtxToneFb);
	F("vtx_tone_collapse", S.vtxToneCollapse); F("vtx_tone_mut", S.vtxToneMut); F("vtx_tone_crush", S.vtxToneCrush);
	F("vtx_g_sens", S.vtxGSens); F("vtx_g_fb", S.vtxGFb); F("vtx_g_pitch", S.vtxGPitch); F("vtx_g_detune", S.vtxGDetune); F("vtx_g_rate", S.vtxGRate);
	F("vtx_g_bright", S.vtxGBright); F("vtx_g_self", S.vtxGSelf); F("vtx_g_sat", S.vtxGSat); F("vtx_g_input", S.vtxGInput);
	F("vtx_drift_freq", S.vtxDriftFreq); F("vtx_drift_detune", S.vtxDriftDetune); I("vtx_feed", S.vtxFeed);
	I("jdat_clk", S.jdatClk); F("jdat_rate", S.jdatRate); I("jdat_div", S.jdatDiv); B("jdat_link", S.jdatLink); I("jdat_clear", S.jdatClear);
	I("jdat_jump", S.jdatJump); B("jdat_hold", S.jdatHold); I("jdat_interp", S.jdatInterp); I("jdat_write", S.jdatWrite); I("jdat_feed", S.jdatFeed);
	I("jdat_loop", S.jdatLoop);
	for (int i = 0; i < 4; i++) {
		I(string::f("jdat_src%d", i).c_str(), S.jdatSrc[(size_t) i]);
		F(string::f("jdat_lvl%d", i).c_str(), S.jdatLvl[(size_t) i]);
	}
	I("data_as", S.dataAs); I("data_scan", S.dataScan); I("data_step", S.dataStep); I("vis_auto", S.visAuto); B("vis_flash", S.visFlash);
	json_object_set_new(root, "params", p);
	json_t* steps = json_array();
	for (const auto& st : S.steps) {
		json_t* a = json_array();
		for (float v : {(float) st.trig, (float) st.vel, (float) st.prob, st.dur, st.dly, (float) st.dprob})
			json_array_append_new(a, json_real(v));
		json_array_append_new(steps, a);
	}
	json_object_set_new(root, "steps", steps);
	json_t* lanes = json_array();
	for (const auto& ln : S.lanes) {
		json_t* a = json_array();
		json_array_append_new(a, json_integer(ln.note));
		json_array_append_new(a, json_boolean(ln.choke));
		json_array_append_new(a, json_integer(ln.dest));
		json_array_append_new(lanes, a);
	}
	json_object_set_new(root, "lanes", lanes);
	json_t* mx = json_array();
	for (int x = 0; x < pb::kStates; x++)
		for (int y = 0; y < pb::kStates; y++)
			json_array_append_new(mx, json_real(inst.pinball.matrix[(size_t) x][(size_t) y].load()));
	json_object_set_new(root, "matrix", mx);
	json_t* ik = json_array();
	for (int v = 0; v < fdat_ryoz::kNumVoices; v++) {
		const auto& vs = inst.ikdShared.voices[(size_t) v];
		json_t* a = json_array();
		for (uint64_t w : {vs.pitch.load(), vs.cells.load(), vs.gain.load(), vs.decay.load(), vs.signs.load()})
			json_array_append_new(a, json_string(string::f("%016llx", (unsigned long long) w).c_str()));
		json_array_append_new(a, json_integer(vs.rule.load()));
		json_array_append_new(a, json_real(vs.gateBaseMs.load()));
		json_array_append_new(a, json_boolean(inst.ikdLocks[(size_t) v]));
		json_array_append_new(ik, a);
	}
	json_object_set_new(root, "ikd", ik);
	json_t* mw = json_array();
	for (int w = 0; w < fdat_ryoz::kMasterWords; w++)
		json_array_append_new(mw, json_string(string::f("%016llx", (unsigned long long) inst.ikdShared.master[(size_t) w].load()).c_str()));
	json_object_set_new(root, "ikdMaster", mw);
	// FLOATER's whole patch
	json_t* fv = json_object();
	for (const auto& kv : inst.fltCase.snapshotValues())
		json_object_set_new(fv, kv.first.c_str(), json_real(kv.second));
	json_object_set_new(root, "flt", fv);
	return root;
}

void Floatdat::dataFromJson(json_t* root) {
	auto& S = inst.S;
	if (json_t* p = json_object_get(root, "params")) {
		auto I = [&](const char* k, int& v) { if (json_t* x = json_object_get(p, k)) v = (int) json_integer_value(x); };
		auto F = [&](const char* k, float& v) { if (json_t* x = json_object_get(p, k)) v = (float) json_number_value(x); };
		auto B = [&](const char* k, bool& v) { if (json_t* x = json_object_get(p, k)) v = json_boolean_value(x); };
		B("run", S.run); B("clip", S.clip); B("limit", S.limit); B("dcblock", S.dcBlock); I("veldepth", S.velDepth); B("pb_out", S.pbOut);
		I("pb_start", S.pbStart);
		I("ikd_clk", S.ikdClk); I("ikd_div", S.ikdDiv); F("ikd_swing", S.ikdSwing); B("ikd_hold", S.ikdHold); I("ikd_auto", S.ikdAuto);
		I("ikd_chance", S.ikdChance); F("ikd_wave", S.ikdWave); F("ikd_spec", S.ikdSpec); F("ikd_sine", S.ikdSine); I("ikd_feed", S.ikdFeed); B("ikd_feedsnd", S.ikdFeedSnd);
		I("flt_clk", S.fltClk); I("flt_feed", S.fltFeed); B("flt_feedsnd", S.fltFeedSnd); B("flt_feedmodel", S.fltFeedModel); I("flt_vel", S.fltVel);
		I("flt_burst", S.fltBurst); I("flt_map", S.fltMap);
		F("vtx_kick_lvl", S.vtxKickLvl); F("vtx_tone_lvl", S.vtxToneLvl); F("vtx_ghost_lvl", S.vtxGhostLvl); F("vtx_drift_lvl", S.vtxDriftLvl);
		B("vtx_kick_own", S.vtxKickOwn); F("vtx_kick_rate", S.vtxKickRate); F("vtx_kick_prob", S.vtxKickProb); F("vtx_kick_pitch", S.vtxKickPitch);
		F("vtx_kick_decay", S.vtxKickDecay); F("vtx_kick_drive", S.vtxKickDrive); F("vtx_kick_rev", S.vtxKickRev); F("vtx_kick_pre", S.vtxKickPre);
		B("vtx_tone_own", S.vtxToneOwn); F("vtx_tone_prob", S.vtxToneProb); F("vtx_tone_freq", S.vtxToneFreq); F("vtx_tone_fb", S.vtxToneFb);
		F("vtx_tone_collapse", S.vtxToneCollapse); F("vtx_tone_mut", S.vtxToneMut); F("vtx_tone_crush", S.vtxToneCrush);
		F("vtx_g_sens", S.vtxGSens); F("vtx_g_fb", S.vtxGFb); F("vtx_g_pitch", S.vtxGPitch); F("vtx_g_detune", S.vtxGDetune); F("vtx_g_rate", S.vtxGRate);
		F("vtx_g_bright", S.vtxGBright); F("vtx_g_self", S.vtxGSelf); F("vtx_g_sat", S.vtxGSat); F("vtx_g_input", S.vtxGInput);
		F("vtx_drift_freq", S.vtxDriftFreq); F("vtx_drift_detune", S.vtxDriftDetune); I("vtx_feed", S.vtxFeed);
		I("jdat_clk", S.jdatClk); F("jdat_rate", S.jdatRate); I("jdat_div", S.jdatDiv); B("jdat_link", S.jdatLink); I("jdat_clear", S.jdatClear);
		I("jdat_jump", S.jdatJump); B("jdat_hold", S.jdatHold); I("jdat_interp", S.jdatInterp); I("jdat_write", S.jdatWrite); I("jdat_feed", S.jdatFeed);
		I("jdat_loop", S.jdatLoop);
		for (int i = 0; i < 4; i++) {
			I(string::f("jdat_src%d", i).c_str(), S.jdatSrc[(size_t) i]);
			F(string::f("jdat_lvl%d", i).c_str(), S.jdatLvl[(size_t) i]);
		}
		I("data_as", S.dataAs); I("data_scan", S.dataScan); I("data_step", S.dataStep); I("vis_auto", S.visAuto); B("vis_flash", S.visFlash);
	}
	if (json_t* steps = json_object_get(root, "steps")) {
		size_t i;
		json_t* a;
		json_array_foreach(steps, i, a) {
			if (i >= (size_t) pb::kSteps || json_array_size(a) < 6)
				continue;
			auto v = [&](size_t k) { return (float) json_number_value(json_array_get(a, k)); };
			S.steps[i] = {(int) v(0), (int) v(1), (int) v(2), v(3), v(4), (int) v(5)};
		}
	}
	if (json_t* lanes = json_object_get(root, "lanes")) {
		size_t i;
		json_t* a;
		json_array_foreach(lanes, i, a) {
			if (i >= (size_t) pb::kLanes || json_array_size(a) < 3)
				continue;
			S.lanes[i].note = (int) json_integer_value(json_array_get(a, 0));
			S.lanes[i].choke = json_boolean_value(json_array_get(a, 1));
			S.lanes[i].dest = clamp((int) json_integer_value(json_array_get(a, 2)), 0, fdat::dNumDests - 1);
		}
	}
	if (json_t* mx = json_object_get(root, "matrix"))
		for (int k = 0; k < pb::kStates * pb::kStates && k < (int) json_array_size(mx); k++)
			inst.pinball.matrix[(size_t) (k / pb::kStates)][(size_t) (k % pb::kStates)].store((float) json_number_value(json_array_get(mx, (size_t) k)));
	auto hex = [](json_t* s) { return (uint64_t) std::strtoull(json_string_value(s) ? json_string_value(s) : "0", nullptr, 16); };
	if (json_t* ik = json_object_get(root, "ikd")) {
		size_t v;
		json_t* a;
		json_array_foreach(ik, v, a) {
			if (v >= (size_t) fdat_ryoz::kNumVoices || json_array_size(a) < 8)
				continue;
			auto& vs = inst.ikdShared.voices[v];
			vs.pitch.store(hex(json_array_get(a, 0)));
			vs.cells.store(hex(json_array_get(a, 1)));
			vs.gain.store(hex(json_array_get(a, 2)));
			vs.decay.store(hex(json_array_get(a, 3)));
			vs.signs.store(hex(json_array_get(a, 4)));
			vs.rule.store((int) json_integer_value(json_array_get(a, 5)));
			vs.gateBaseMs.store((float) json_number_value(json_array_get(a, 6)));
			inst.ikdLocks[v] = json_boolean_value(json_array_get(a, 7));
		}
		inst.ikdShared.loadSerial.fetch_add(1);
	}
	if (json_t* mw = json_object_get(root, "ikdMaster"))
		for (int w = 0; w < fdat_ryoz::kMasterWords && w < (int) json_array_size(mw); w++)
			inst.ikdShared.master[(size_t) w].store(hex(json_array_get(mw, (size_t) w)));
	if (json_t* fv = json_object_get(root, "flt")) {
		std::map<std::string, float> m;
		const char* key;
		json_t* v;
		json_object_foreach(fv, key, v) m[key] = (float) json_number_value(v);
		inst.fltCase.applyValues(m);
	}
	if (json_t* x = json_object_get(root, "preset"))
		inst.factoryIndex = (int) json_integer_value(x);
	if (json_t* x = json_object_get(root, "scene"))
		scene = clamp((int) json_integer_value(x), 0, kScenes - 1);
	reqPreset = -1;
	std::string file;
	if (json_t* x = json_object_get(root, "file"))
		file = json_string_value(x) ? json_string_value(x) : "";
	if (json_t* x = json_object_get(root, "example"))
		example = clamp((int) json_integer_value(x), 1, 9);
	if (!file.empty())
		loadFile(file);
	else
		loadExample(example);
	if (json_t* x = json_object_get(root, "head"))
		savedHead = (int64_t) json_integer_value(x);
	pullKnobs();
	params[BPM_PARAM].setValue(inst.S.bpm);
}

// ---------------------------------------------------------------------------- screens

namespace {
Snapshot makeDemo() {
	Snapshot d;
	d.loaded = true;
	uint32_t x = 7;
	for (auto& r : d.rows) {
		x = x * 1664525u + 1013904223u;
		r = ((uint64_t) x << 32) | (x * 2654435761u);
	}
	for (int k = 0; k < 64; k++)
		d.sym[k] = std::sin(k * 0.3f) * 0.8f;
	for (int k = 0; k < 24; k++)
		d.bytes[k] = (uint8_t) (k * 37 + 11);
	std::snprintf(d.name, sizeof d.name, "example 1 - lattice chemistry");
	std::snprintf(d.type, sizeof d.type, "TEXT");
	std::snprintf(d.found, sizeof d.found, "the lattice holds");
	d.entropy = 4.3f;
	d.size = 1984;
	d.headFrac = 0.31f;
	for (int v = 0; v < 9; v++)
		d.cells[v] = 0x5fb7fff3b5fe0c6dull >> v;
	for (int s = 0; s < pb::kSteps; s++)
		d.matrix[s][s + 1] = 100.f;
	d.matrix[16][1] = 60.f;
	d.matrix[4][9] = 40.f;
	d.step = 5;
	d.playing = true;
	return d;
}
} // namespace

// The datamatic picture: the file under the read head, as Ikeda draws data (barcode, matrix, scan, numbers)
// or IKD's nine automata. Inverted for a moment on a lane's "vis flash".
struct VisualScreen : Screen {
	Floatdat* module = nullptr;
	Snapshot demo = makeDemo();
	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const NVGcolor bg = s.flash ? semi::theme::text() : semi::theme::screen();
		const NVGcolor ink = s.flash ? semi::theme::screen() : semi::theme::text();
		const NVGcolor hot = s.flash ? semi::theme::screen() : semi::theme::cyan();
		if (s.flash)
			fillRect(vg, 0.f, 0.f, w, h, bg);
		if (!s.loaded) {
			text(vg, w * 0.5f, h * 0.5f, "NO DATA", 10.f, semi::theme::dim(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			return;
		}
		const float top = 12.f, bot = h - 2.f;
		char buf[64];
		std::snprintf(buf, sizeof buf, "%02d %s", s.scene + 1, kSceneNames[s.scene]);
		text(vg, 3.f, 2.f, buf, 8.f, hot);
		std::snprintf(buf, sizeof buf, "0x%08llX", (unsigned long long) s.headByte);
		text(vg, w - 3.f, 2.f, buf, 8.f, ink, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		switch (s.scene) {
			case 0: {   // BARCODE: 64 bits of 4 rows as bars, a thin bar per 0, a thick one per 1
				const float cw = (w - 6.f) / 128.f;
				for (int r = 0; r < 2; r++)
					for (int k = 0; k < 64; k++) {
						const bool on = (s.rows[r] >> k) & 1u;
						const float x = 3.f + cw * (r * 64 + k);
						if (on)
							fillRect(vg, x, top + 2.f, std::max(1.f, cw * 0.9f), (bot - top) * 0.72f, ink);
						else if (k % 3 == 0)
							fillRect(vg, x, top + 2.f, std::max(1.f, cw * 0.3f), (bot - top) * 0.72f, ink);
					}
				std::snprintf(buf, sizeof buf, "%s  %s", s.type, s.found);
				text(vg, 3.f, bot, buf, 8.f, ink, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
				break;
			}
			case 1: {   // MATRIX: 64 x 24 bits
				const float cw = (w - 6.f) / 64.f, ch = (bot - top) / 24.f;
				for (int r = 0; r < 24; r++)
					for (int k = 0; k < 64; k++)
						if ((s.rows[r] >> k) & 1u)
							fillRect(vg, 3.f + cw * k, top + ch * r, std::max(1.f, cw - 0.5f), std::max(1.f, ch - 0.5f), r == 0 ? hot : ink);
				break;
			}
			case 2: {   // SCAN: the symbols as a trace and their bars
				const float mid = 0.5f * (top + bot), amp = 0.45f * (bot - top);
				float ys[64];
				for (int k = 0; k < 64; k++) {
					ys[k] = mid - clamp(s.sym[k], -1.f, 1.f) * amp;
					fillRect(vg, 3.f + (w - 6.f) * k / 64.f, std::min(mid, ys[k]), 1.2f, std::fabs(ys[k] - mid) + 0.5f, semi::theme::cyanDim());
				}
				polyline(vg, ys, 64, 3.f, w - 6.f, hot);
				line(vg, 3.f, mid, w - 3.f, mid, ink, semi::platform::kThinStroke);
				break;
			}
			case 3: {   // NUMBERS: offsets and bytes in hex, the string found nearest the head
				for (int r = 0; r < 3; r++) {
					std::string row = string::f("%06llX ", (unsigned long long) (s.headByte + r * 8));
					for (int k = 0; k < 8; k++)
						row += string::f("%02X ", s.bytes[r * 8 + k]);
					text(vg, 3.f, top + 2.f + r * 10.f, row, 9.f, r == 0 ? hot : ink);
				}
				std::snprintf(buf, sizeof buf, "H %.2f  CRC %08X", s.entropy, (unsigned) s.crc);
				text(vg, 3.f, top + 34.f, buf, 9.f, ink);
				text(vg, 3.f, bot, s.found, 9.f, hot, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
				break;
			}
			default: {  // CELLS: IKD's nine automata, 64 cells each, the step playing
				const float cw = (w - 6.f) / 64.f, ch = (bot - top) / 9.f;
				for (int v = 0; v < 9; v++)
					for (int k = 0; k < 64; k++)
						if ((s.cells[v] >> k) & 1u)
							fillRect(vg, 3.f + cw * k, top + ch * v + 0.5f, std::max(1.f, cw - 0.4f), ch - 1.f, k == (s.ikdStep & 63) ? hot : ink);
				if (s.ikdStep >= 0)
					line(vg, 3.f + cw * ((s.ikdStep & 63) + 0.5f), top, 3.f + cw * ((s.ikdStep & 63) + 0.5f), bot, hot);
				break;
			}
		}
	}
};

// Pinball 2: the Markov matrix (row = from, column = to; a cell's size its weight), the ball's step lit.
struct PinballScreen : Screen {
	Floatdat* module = nullptr;
	Snapshot demo = makeDemo();
	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float g = std::min((w - 4.f) / pb::kStates, (h - 4.f) / pb::kStates);
		const float x0 = (w - g * pb::kStates) * 0.5f, y0 = (h - g * pb::kStates) * 0.5f;
		const int row = s.step < 0 ? 0 : s.step + 1;
		fillRect(vg, x0, y0 + g * row, g * pb::kStates, g, semi::theme::cyanDeep());
		for (int x = 0; x < pb::kStates; x++)
			for (int y = 0; y < pb::kStates; y++) {
				const float v = clamp(s.matrix[x][y] / 100.f, 0.f, 1.f);
				const float cx = x0 + g * (y + 0.5f), cy = y0 + g * (x + 0.5f);
				if (v <= 0.f) {
					fillRect(vg, cx - 0.5f, cy - 0.5f, 1.f, 1.f, semi::theme::grid());
					continue;
				}
				const float sz = std::max(1.5f, g * 0.85f * std::sqrt(v));
				fillRect(vg, cx - sz * 0.5f, cy - sz * 0.5f, sz, sz, x == row ? semi::theme::amber() : semi::theme::text());
			}
		for (int st = 0; st < pb::kSteps; st++)
			if (s.stepFlash[st] || st == s.step) {
				const float cx = x0 + g * (st + 1.5f);
				fillRect(vg, cx - g * 0.4f, y0 + g * pb::kStates - 1.5f, g * 0.8f, 1.5f, st == s.step ? semi::theme::magenta() : semi::theme::cyan());
			}
	}
};

// The file: name, type, entropy, where the head is.
struct FloatdatDataScreen : Screen {
	Floatdat* module = nullptr;
	Snapshot demo = makeDemo();
	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		text(vg, 2.f, 1.5f, s.loaded ? s.name : "--", 8.f, semi::theme::cyan());
		char buf[48];
		std::snprintf(buf, sizeof buf, "%s %lluB H%.1f", s.type, (unsigned long long) s.size, s.entropy);
		text(vg, 2.f, h - 1.f, buf, 7.f, semi::theme::text(), NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
		fillRect(vg, 2.f, h * 0.5f, w - 4.f, 1.5f, semi::theme::grid());
		fillRect(vg, 2.f + (w - 6.f) * clamp(s.headFrac, 0.f, 1.f), h * 0.5f - 1.5f, 2.f, 4.5f, semi::theme::magenta());
	}
};

// ---------------------------------------------------------------------------- menus

namespace {
void choice(Menu* m, const std::string& label, const std::vector<std::string>& names, int* v) {
	m->addChild(createIndexSubmenuItem(label, names, [=]() { return (size_t) clamp(*v, 0, (int) names.size() - 1); }, [=](size_t i) { *v = (int) i; }));
}
void boolean(Menu* m, const std::string& label, bool* v) {
	m->addChild(createBoolPtrMenuItem(label, "", v));
}
// fixed steps that work on both platforms
void steps(Menu* m, const std::string& label, float* v, std::vector<float> values, const char* fmt) {
	std::vector<std::string> names;
	for (float x : values)
		names.push_back(string::f(fmt, x));
	m->addChild(createIndexSubmenuItem(label, names,
		[=]() {
			size_t best = 0;
			for (size_t k = 1; k < values.size(); k++)
				if (std::fabs(values[k] - *v) < std::fabs(values[best] - *v))
					best = k;
			return best;
		},
		[=](size_t k) { *v = values[k]; }));
}
void intSteps(Menu* m, const std::string& label, int* v, std::vector<int> values, const char* fmt) {
	std::vector<std::string> names;
	for (int x : values)
		names.push_back(string::f(fmt, x));
	m->addChild(createIndexSubmenuItem(label, names,
		[=]() {
			size_t best = 0;
			for (size_t k = 1; k < values.size(); k++)
				if (std::abs(values[k] - *v) < std::abs(values[best] - *v))
					best = k;
			return best;
		},
		[=](size_t k) { *v = values[k]; }));
}
const std::vector<float> kDb = {-60.f, -24.f, -18.f, -12.f, -9.f, -6.f, -3.f, 0.f, 3.f, 6.f};
const std::vector<float> k01 = {0.f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.f};
const std::vector<int> kPct = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
} // namespace

struct FloatdatWidget : ModuleWidget {
	FloatdatWidget(Floatdat* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Floatdat.svg")));
		auto* vis = semi::createScreenMm<VisualScreen>(Vec(L::VISUAL.x, L::VISUAL.y), Vec(L::VISUAL.w, L::VISUAL.h));
		vis->module = module;
		addChild(vis);
		auto* pin = semi::createScreenMm<PinballScreen>(Vec(L::PINBALL.x, L::PINBALL.y), Vec(L::PINBALL.w, L::PINBALL.h));
		pin->module = module;
		addChild(pin);
		auto* dat = semi::createScreenMm<FloatdatDataScreen>(Vec(L::DATA.x, L::DATA.y), Vec(L::DATA.w, L::DATA.h));
		dat->module = module;
		addChild(dat);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		const L::P leds[8] = {L::LANE1, L::LANE2, L::LANE3, L::LANE4, L::LANE5, L::LANE6, L::LANE7, L::LANE8};
		for (int i = 0; i < 8; i++)
			addChild(createLightCentered<semi::CyanLedSmall>(at(leds[i]), module, Floatdat::LANE_LIGHT + i));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::RUN_LED), module, Floatdat::RUN_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::RUN), module, Floatdat::RUN_PARAM));
		addChild(createLightCentered<semi::AmberLedSmall>(at(L::LOAD_LED), module, Floatdat::LOAD_LIGHT));

		addParam(createParamCentered<semi::KnobM>(at(L::IKD), module, Floatdat::IKD_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::FLT), module, Floatdat::FLT_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::VTX), module, Floatdat::VTX_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::JDAT), module, Floatdat::JDAT_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::MASTER), module, Floatdat::MASTER_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::BPM), module, Floatdat::BPM_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::MULT), module, Floatdat::MULT_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::SWING), module, Floatdat::SWING_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::PATLEN), module, Floatdat::PATLEN_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::RATE), module, Floatdat::RATE_PARAM));
		addParam(createParamCentered<semi::PadCyan>(at(L::RESET), module, Floatdat::RESET_PARAM));
		addParam(createParamCentered<semi::PadMagenta>(at(L::DPATCH), module, Floatdat::DPATCH_PARAM));
		addParam(createParamCentered<semi::PadCyan>(at(L::MUTATE), module, Floatdat::MUTATE_PARAM));
		addParam(createParamCentered<semi::PadCyan>(at(L::SCENE), module, Floatdat::SCENE_PARAM));

		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, Floatdat::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RST), module, Floatdat::RST_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RUN_IN), module, Floatdat::RUN_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::LANES_IN), module, Floatdat::LANES_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RESET_IN), module, Floatdat::RESET_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::DPATCH_IN), module, Floatdat::DPATCH_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::SCENE_IN), module, Floatdat::SCENE_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::MULT_IN), module, Floatdat::MULT_INPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_L), module, Floatdat::OUT_L_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_R), module, Floatdat::OUT_R_OUTPUT));
		const L::P gates[8] = {L::GATE1, L::GATE2, L::GATE3, L::GATE4, L::GATE5, L::GATE6, L::GATE7, L::GATE8};
		for (int i = 0; i < 8; i++)
			addOutput(createOutputCentered<semi::Jack>(at(gates[i]), module, Floatdat::GATE_OUTPUT + i));
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Floatdat*>(module);
		if (!m)
			return;
		using fdat::Instrument;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(m->filePath.empty() ? fdat_pdata::exampleName(m->example) : semi::fileName(m->filePath)));
		menu->addChild(createMenuItem("Load any file", "", [=]() {
			semi::openFileDialog("Any file:*", m->filePath.empty() ? "" : semi::dirName(m->filePath), [=](const std::string& p) { m->loadFile(p); });
		}));
		menu->addChild(createSubmenuItem("Example texts", "", [=](Menu* sub) {
			for (int i = 1; i <= 9; i++)
				sub->addChild(createMenuItem(fdat_pdata::exampleName(i), "", [=]() { m->loadExample(i); }));
		}));
		menu->addChild(createIndexSubmenuItem("Read as", fdat_pdata::readAsNames(), [=]() { return (size_t) m->inst.S.dataAs; },
			[=](size_t i) { m->inst.S.dataAs = (int) i; m->reinterpret(); }));
		menu->addChild(createSubmenuItem("Data", "", [=](Menu* sub) {
			auto& S = m->inst.S;
			choice(sub, "Scan", fdat::scanNames(), &S.dataScan);
			choice(sub, "Step size", fdat::stepSizeNames(), &S.dataStep);
		}));
		menu->addChild(createSubmenuItem("Presets", "", [=](Menu* sub) {
			for (int i = 0; i < (int) fdat::factoryNames().size(); i++)
				sub->addChild(createMenuItem(fdat::factoryNames()[(size_t) i], CHECKMARK(m->inst.factoryIndex == i), [=]() { m->reqPreset = i; }));
		}));

		menu->addChild(createSubmenuItem("PINBALL", "", [=](Menu* sub) {
			auto& S = m->inst.S;
			for (int st = 0; st < pb::kSteps; st++)
				sub->addChild(createSubmenuItem(string::f("Step %d", st + 1), fdat::trigNames()[(size_t) clamp(S.steps[(size_t) st].trig, 0, 9)], [=](Menu* s2) {
					auto& sp = m->inst.S.steps[(size_t) st];
					choice(s2, "Trig", fdat::trigNames(), &sp.trig);
					intSteps(s2, "Velocity", &sp.vel, {0, 20, 40, 60, 80, 100, 110, 120, 127}, "%d");
					intSteps(s2, "Trig probability", &sp.prob, kPct, "%d%%");
					steps(s2, "Duration (1/16)", &sp.dur, {0.25f, 0.5f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 8.f, 12.f, 16.f, 32.f, 64.f}, "%g");
					steps(s2, "Delay (1/16)", &sp.dly, {0.f, 0.25f, 0.5f, 1.f, 2.f, 3.f, 4.f, 8.f}, "%g");
					intSteps(s2, "Delay probability", &sp.dprob, kPct, "%d%%");
				}));
			sub->addChild(new MenuSeparator);
			for (int l = 0; l < pb::kLanes; l++)
				sub->addChild(createSubmenuItem(string::f("Lane T%d", l + 1), fdat::destNames()[(size_t) S.lanes[(size_t) l].dest], [=](Menu* s2) {
					choice(s2, "Destination", fdat::destNames(), &m->inst.S.lanes[(size_t) l].dest);
					boolean(s2, "Mute (choked by others)", &m->inst.S.lanes[(size_t) l].choke);
				}));
			intSteps(sub, "Starting step", &S.pbStart, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}, "%d");
			boolean(sub, "Gates out", &S.pbOut);
			sub->addChild(createMenuItem("Halt", "", [=]() { m->inst.requestHalt(); }));
			sub->addChild(createSubmenuItem("Functions", "", [=](Menu* s2) {
				const char* fn[] = {"Randomize trigs", "Clear steps", "Clear matrix", "Clear all", "Matrix: chain", "Matrix: drunk walk",
				                    "Matrix: random", "Matrix: ping-pong", "Matrix: jumps", "Randomize step values"};
				for (int i = 0; i < 10; i++)
					s2->addChild(createMenuItem(fn[i], "", [=]() { m->reqFn = i; }));
			}));
		}));

		menu->addChild(createSubmenuItem("CELLULA", "", [=](Menu* sub) {
			auto& S = m->inst.S;
			choice(sub, "Clock", fdat::engineClockNames(), &S.ikdClk);
			choice(sub, "Grid", fdat::divNames(), &S.ikdDiv);
			steps(sub, "Swing", &S.ikdSwing, {0.f, 6.f, 12.f, 18.f, 25.f, 33.f, 50.f}, "%g%%");
			boolean(sub, "Hold automata", &S.ikdHold);
			choice(sub, "Auto mutate every", fdat::autoNames(), &S.ikdAuto);
			intSteps(sub, "Auto chance", &S.ikdChance, kPct, "%d%%");
			steps(sub, "Wave 1-3", &S.ikdWave, kDb, "%g dB");
			steps(sub, "Spec 4-6", &S.ikdSpec, kDb, "%g dB");
			steps(sub, "Sine 7-9", &S.ikdSine, kDb, "%g dB");
			choice(sub, "Data feed", fdat::feedNames(), &S.ikdFeed);
			boolean(sub, "Data sets sound", &S.ikdFeedSnd);
			sub->addChild(createMenuItem("Mutate all", "", [=]() { m->reqIkd = 3; }));
			sub->addChild(createMenuItem("Random patterns", "", [=]() { m->reqIkd = 0; }));
			sub->addChild(createMenuItem("Random sound", "", [=]() { m->reqIkd = 1; }));
			sub->addChild(createMenuItem("Read data now", "", [=]() { m->savedHead = (int64_t) m->inst.head; }));
		}));

		menu->addChild(createSubmenuItem("FLT (FLOATER)", "", [=](Menu* sub) {
			auto& S = m->inst.S;
			choice(sub, "Clock", fdat::fltClockNames(), &S.fltClk);
			choice(sub, "Lane map", fdat::fltMapNames(), &S.fltMap);
			choice(sub, "Data feed", fdat::feedNames(), &S.fltFeed);
			boolean(sub, "Data sets sound", &S.fltFeedSnd);
			boolean(sub, "Data picks models", &S.fltFeedModel);
			intSteps(sub, "Velocity depth", &S.fltVel, kPct, "%d%%");
			intSteps(sub, "Burst hits", &S.fltBurst, {2, 3, 4, 5, 6, 7, 8}, "%d");
			sub->addChild(createMenuItem("NEW PATCH", "", [=]() { m->reqFltNew = true; }));
			sub->addChild(createMenuItem("Unflip (forget LOCK's walk)", "", [=]() { m->inst.fltCase.resetMutations(); }));
			sub->addChild(createSubmenuItem("FLOATER patch", m->inst.fltCase.presetName, [=](Menu* s2) {
				const auto& f = flt::Case::factoryPresets();
				for (int i = 0; i < (int) f.size(); i++)
					s2->addChild(createMenuItem(f[(size_t) i].name, "", [=]() { m->inst.fltCase.loadFactory(i); }));
			}));
		}));

		menu->addChild(createSubmenuItem("VTX (Vortessa)", "", [=](Menu* sub) {
			auto& S = m->inst.S;
			steps(sub, "Kick level", &S.vtxKickLvl, kDb, "%g dB");
			steps(sub, "Tone level", &S.vtxToneLvl, kDb, "%g dB");
			steps(sub, "Ghost level", &S.vtxGhostLvl, kDb, "%g dB");
			steps(sub, "Drift level", &S.vtxDriftLvl, kDb, "%g dB");
			boolean(sub, "Kick on its own clock", &S.vtxKickOwn);
			steps(sub, "Kick rate", &S.vtxKickRate, {0.25f, 0.5f, 1.f, 2.f, 4.f, 8.f, 16.f, 32.f}, "%g Hz");
			steps(sub, "Kick probability", &S.vtxKickProb, k01, "%.1f");
			steps(sub, "Kick pitch", &S.vtxKickPitch, {25.f, 35.f, 45.f, 60.f, 80.f, 110.f, 160.f}, "%g Hz");
			steps(sub, "Kick decay", &S.vtxKickDecay, {0.05f, 0.2f, 0.5f, 1.f, 2.f, 4.f}, "%g s");
			steps(sub, "Kick drive", &S.vtxKickDrive, k01, "%.1f");
			steps(sub, "Kick cave", &S.vtxKickRev, k01, "%.1f");
			boolean(sub, "Tone on its own clock", &S.vtxToneOwn);
			steps(sub, "Tone probability", &S.vtxToneProb, k01, "%.1f");
			steps(sub, "Tone freq", &S.vtxToneFreq, {80.f, 160.f, 320.f, 640.f, 800.f, 1600.f, 3200.f, 8000.f}, "%g Hz");
			steps(sub, "Tone feedback", &S.vtxToneFb, {0.f, 0.3f, 0.5f, 0.7f, 0.85f, 0.95f}, "%.2f");
			steps(sub, "Tone mutation", &S.vtxToneMut, k01, "%.1f");
			steps(sub, "Tone crush", &S.vtxToneCrush, k01, "%.1f");
			steps(sub, "Ghost feedback", &S.vtxGFb, {0.f, 0.3f, 0.55f, 0.7f, 0.78f, 0.85f}, "%.2f");
			steps(sub, "Ghost pitch zone", &S.vtxGPitch, {120.f, 240.f, 480.f, 640.f, 1200.f, 2400.f, 5000.f}, "%g Hz");
			steps(sub, "Ghost self excitation", &S.vtxGSelf, k01, "%.1f");
			steps(sub, "Ghost input", &S.vtxGInput, k01, "%.1f");
			choice(sub, "Data feed", fdat::vtxFeedNames(), &S.vtxFeed);
		}));

		menu->addChild(createSubmenuItem("DATUM", "", [=](Menu* sub) {
			auto& S = m->inst.S;
			choice(sub, "Clock", fdat::engineClockNames(), &S.jdatClk);
			steps(sub, "Metro", &S.jdatRate, {10.f, 30.f, 60.f, 120.f, 250.f, 500.f, 1000.f}, "%g ms");
			choice(sub, "Grid", fdat::divNames(), &S.jdatDiv);
			for (int i = 0; i < 4; i++) {
				choice(sub, i < 3 ? string::f("Data %d source", i + 1) : std::string("Drone source"), fdat::sourceNames(), &S.jdatSrc[(size_t) i]);
				steps(sub, i < 3 ? string::f("Data %d level", i + 1) : std::string("Drone level"), &S.jdatLvl[(size_t) i], kDb, "%g dB");
			}
			boolean(sub, "Link 1 > 3", &S.jdatLink);
			intSteps(sub, "Clear chance", &S.jdatClear, kPct, "%d%%");
			intSteps(sub, "Noise jump", &S.jdatJump, kPct, "%d%%");
			boolean(sub, "Hold", &S.jdatHold);
			choice(sub, "Wave interp", fdat::interpNames(), &S.jdatInterp);
			choice(sub, "Write", fdat::writeNames(), &S.jdatWrite);
			choice(sub, "Data feed", fdat::feedNames(), &S.jdatFeed);
			choice(sub, "Pattern lock", fdat::loopNames(), &S.jdatLoop);
		}));

		menu->addChild(createSubmenuItem("Mix / visual", "", [=](Menu* sub) {
			auto& S = m->inst.S;
			boolean(sub, "DAC clip", &S.clip);
			boolean(sub, "Limiter", &S.limit);
			boolean(sub, "DC block", &S.dcBlock);
			intSteps(sub, "Velocity to accent", &S.velDepth, kPct, "%d%%");
			sub->addChild(createIndexSubmenuItem("Scene", std::vector<std::string>(kSceneNames, kSceneNames + kScenes),
				[=]() { return (size_t) m->scene; }, [=](size_t i) { m->scene = (int) i; }));
			choice(sub, "Scene auto", fdat::visAutoNames(), &S.visAuto);
			boolean(sub, "Flash", &S.visFlash);
		}));
	}
};

Model* modelFloatdat = createModel<Floatdat, FloatdatWidget>("Floatdat");

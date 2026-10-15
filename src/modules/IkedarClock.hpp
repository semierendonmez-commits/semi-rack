#pragma once
// Step clock shared by IKEDAR, JDAT and TST_INGP (the three Max-patch ports).
//
// The patches each ran their own [metro]. In Rack the step comes, in this order, from
//   CLK  pulses (default 4 PPQN = one pulse per 1/16, menu for 1/8, 1/4, 24 and 48 PPQN),
//   BUS  POS (FamilyBus CV ch 1, EPOCH's 32-bar cycle) when CLK is not patched,
//   the module's own metro (the patch's rate) when neither is patched.
// Steps can be shorter than a pulse (e.g. 1/32 from 4 PPQN): the position between pulses is
// interpolated from the measured pulse period. SWING delays odd steps by a fraction of a step,
// exactly like the plugins' host-sync path.
//
// Candidate for src/common (see the module reports); kept here so no shared file changes.

#include <rack.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "../common/FamilyBusCV.hpp"

namespace semi {
namespace ikclock {

static const std::vector<std::string> kPpqnNames = {"1/16 (4 PPQN)", "1/8 (2 PPQN)", "1/4 (1 PPQN)", "24 PPQN", "48 PPQN"};
static const double kPpqn[] = {4.0, 2.0, 1.0, 24.0, 48.0};

struct StepClock {
	enum Source { NONE, CLK, BUS };

	rack::dsp::SchmittTrigger clkTrig, rstTrig;
	int ppqnIndex = 0;

	// CLK state
	double sinceClk = 0.0, period = 0.0;
	int64_t pulses = -1;
	bool seen = false;
	// BUS state
	bool busInit = false;
	float prevBars = 0.f;
	int64_t busCycle = 0;
	// shared
	int64_t lastStep = -1;
	double stepSec = 0.0;      // measured length of one step (0 = unknown)
	Source source = NONE;
	bool resetPending = false; // RST seen: the next step is step 0 of the pattern

	void reset() {
		pulses = -1;
		lastStep = -1;
		resetPending = true;
	}

	// One sample. Returns true when a step fires; idx = running step index (0, 1, 2 ...).
	// stepBeats = step length in quarter notes (0.25 = 1/16), swing = 0..0.75 of a step.
	bool process(float dt, rack::engine::Input& clk, rack::engine::Input& rst, rack::engine::Input* bus,
	             double stepBeats, double swing, int64_t& idx) {
		if (rst.isConnected() && rstTrig.process(rst.getVoltage(), 0.1f, 1.f))
			reset();

		double posSteps = -1.0;
		if (clk.isConnected()) {
			if (source != CLK) {
				source = CLK;
				seen = false;
				period = 0.0;
				lastStep = pulses >= 0 ? lastStep : -1;
			}
			sinceClk += dt;
			if (clkTrig.process(clk.getVoltage(), 0.1f, 1.f)) {
				if (seen)
					period = sinceClk;
				seen = true;
				sinceClk = 0.0;
				pulses++;
			}
			if (period > 0.0 && sinceClk > 2.5 * period)
				period = 0.0; // stopped: no interpolation until it runs again
			const double pps = std::max(1e-6, kPpqn[ppqnIndex] * stepBeats);
			stepSec = period > 0.0 ? period * pps : 0.0;
			if (pulses >= 0) {
				const double frac = period > 0.0 ? std::min(sinceClk / period, 0.999) : 0.0;
				posSteps = ((double) pulses + frac) / pps;
			}
		}
		else if (bus && bus->isConnected()) {
			if (source != BUS) {
				source = BUS;
				busInit = false;
			}
			const float bars = bus::voltsToBars(rack::math::clamp(bus->getVoltage(bus::kPos), 0.f, 10.f));
			if (!busInit) {
				busInit = true;
				prevBars = bars;
				busCycle = 0;
				lastStep = (int64_t) std::floor(bars * 4.0 / stepBeats) - 1;
			}
			if (bars < prevBars - 16.f)
				busCycle++;
			else if (bars > prevBars + 16.f)
				busCycle--;
			const float dBars = bars - prevBars;
			prevBars = bars;
			const double beats = ((double) busCycle * bus::kBarsPerCycle + bars) * 4.0;
			posSteps = beats / stepBeats;
			// tempo from the POS slope (smoothed), for gate lengths
			if (std::fabs(dBars) > 0.f && std::fabs(dBars) < 1.f) {
				const double sps = dBars * 4.0 / stepBeats / dt; // steps per second
				if (sps > 0.0)
					stepSec = stepSec > 0.0 ? stepSec + (1.0 / sps - stepSec) * 0.001 : 1.0 / sps;
			}
			if (resetPending)
				lastStep = (int64_t) std::floor(posSteps) - 1;
		}
		else {
			source = NONE;
			seen = false;
			period = 0.0;
			return false;
		}

		if (posSteps < 0.0)
			return false;
		const int64_t whole = (int64_t) std::floor(posSteps);
		if (lastStep > whole || lastStep < whole - 2)
			lastStep = whole - 1; // relocated (BUS jumped back, or we fell far behind)
		const int64_t k = lastStep + 1;
		const double t = (double) k + ((k & 1) ? swing : 0.0);
		if (posSteps + 1e-9 >= t) {
			lastStep = k;
			idx = k;
			return true;
		}
		return false;
	}

	bool external() const { return source != NONE; }

	// Consumes the RST flag: true once after a reset, for the step that just fired.
	bool takeReset() {
		bool r = resetPending;
		resetPending = false;
		return r;
	}
};

} // namespace ikclock
} // namespace semi

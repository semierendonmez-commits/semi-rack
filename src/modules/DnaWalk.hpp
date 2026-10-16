#pragma once
// GENOTYPE / LOOPGENOME: the Pinball matrix and the steps strip as small pictures for the
// screens (made on the worker side when the walk is learned or derived, never by the GUI).

#include "DnaFamily.hpp"
#include "genotype/Genotype.h"

namespace dnamod {

constexpr int kMatMax = 32;   // the matrix is max-pooled to at most 32 x 32 cells

struct MatView {
	int steps = 0, cells = 0, pool = 1;
	uint8_t w[3][kMatMax][kMatMax]{};   // CALL, ANSWER, FILL; each row scaled to its max (0..255)
};

inline void poolInto(const walk::Plain& m, int steps, int pool, int cells, uint8_t out[kMatMax][kMatMax]) {
	for (int r = 0; r < cells; r++) {
		float row[kMatMax]{};
		float top = 0.f;
		for (int c = 0; c < cells; c++) {
			float v = 0.f;
			for (int i = 0; i < pool; i++)
				for (int j = 0; j < pool; j++) {
					const int x = 1 + r * pool + i, y = 1 + c * pool + j;   // state s+1 = step s
					if (x <= steps && y <= steps)
						v = std::max(v, m[(size_t) x][(size_t) y]);
				}
			row[c] = v;
			top = std::max(top, v);
		}
		for (int c = 0; c < cells; c++)
			out[r][c] = top > 0.f ? (uint8_t) clamp((int) std::lround(255.f * row[c] / top), 0, 255) : 0;
	}
}

inline void makeMatView(const walk::Plain& call, const walk::Plain& resp, const walk::Plain& fill, int steps, MatView& v) {
	v.steps = clamp(steps, 0, walk::kMaxSteps);
	v.pool = std::max(1, (v.steps + kMatMax - 1) / kMatMax);
	v.cells = (v.steps + v.pool - 1) / v.pool;
	poolInto(call, v.steps, v.pool, v.cells, v.w[0]);
	poolInto(resp, v.steps, v.pool, v.cells, v.w[1]);
	poolInto(fill, v.steps, v.pool, v.cells, v.w[2]);
}

inline NVGcolor roleColor(int role) {
	return role == geno::RoleCall ? semi::theme::cyan() : role == geno::RoleResponse ? semi::theme::magenta() : semi::theme::amber();
}
inline const char* roleName(int role) {
	return role == geno::RoleCall ? "CALL" : role == geno::RoleResponse ? "ANSWER" : "FILL";
}

inline NVGcolor laneColor(int l) {
	switch (l) {
		case geno::LKick: return semi::theme::magenta();
		case geno::LSnare: return semi::theme::cyan();
		case geno::LGhost: return semi::theme::cyanDim();
		case geno::LHat: return semi::theme::amber();
		case geno::LOpen: return semi::theme::amber();
		case geno::LRide: return semi::theme::text();
		case geno::LPerc: return semi::theme::cyan();
		default: return semi::theme::magenta();
	}
}

// The matrix (rows = from, columns = to), weights as three shades of the role's colour; the
// ball's row framed, its last moves as boxes. Only cells above 12 % are drawn (few rects).
struct Move {
	int8_t from = -1, to = -1, role = 0;
};
constexpr int kMoves = 6;

inline void drawMatrix(semi::Screen& sc, NVGcontext* vg, const MatView& m, int which, float x, float y, float size,
                       int ballStep, const Move* moves, int nMoves) {
	if (m.cells <= 0) {
		sc.text(vg, x + size * 0.5f, y + size * 0.5f, "NO WALK", 9.f, semi::theme::dim(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		return;
	}
	const float cs = size / m.cells;
	semi::Screen::fillRect(vg, x, y, size, size, semi::theme::cell());
	const NVGcolor hi = roleColor(which);
	const NVGcolor mid = which == geno::RoleCall ? semi::theme::cyanDim() : which == geno::RoleResponse ? semi::theme::magenta() : semi::theme::amber();
	const NVGcolor lo = which == geno::RoleResponse ? semi::theme::magentaDeep() : semi::theme::cyanDeep();
	for (int r = 0; r < m.cells; r++)
		for (int c = 0; c < m.cells; c++) {
			const int v = m.w[which][r][c];
			if (v < 31)
				continue;
			const NVGcolor col = v > 200 ? hi : v > 100 ? mid : lo;
			semi::Screen::fillRect(vg, x + c * cs + 0.3f, y + r * cs + 0.3f, std::max(0.8f, cs - 0.6f), std::max(0.8f, cs - 0.6f), col);
		}
	for (int i = 0; i < nMoves; i++) {
		const auto& mv = moves[i];
		if (mv.from < 1 || mv.to < 0)
			continue;
		const int r = (mv.from - 1) / m.pool, c = mv.to / m.pool;
		if (r >= m.cells || c >= m.cells)
			continue;
		nvgBeginPath(vg);
		nvgRect(vg, x + c * cs, y + r * cs, cs, cs);
		nvgStrokeColor(vg, i == nMoves - 1 ? semi::theme::text() : roleColor(mv.role));
		nvgStrokeWidth(vg, semi::platform::kThinStroke);
		nvgStroke(vg);
	}
	if (ballStep >= 0) {
		const int r = ballStep / m.pool;
		if (r < m.cells) {
			nvgBeginPath(vg);
			nvgRect(vg, x, y + r * cs, size, cs);
			nvgStrokeColor(vg, semi::theme::dim());
			nvgStrokeWidth(vg, semi::platform::kThinStroke * 0.7f);
			nvgStroke(vg);
		}
	}
}

// One cell per step: the step's loudest lane (blank = rest), the ball as a frame.
inline void drawSteps(NVGcontext* vg, const int8_t* mainLane, int steps, float x, float y, float w, float h, int ball,
                      int ballRole) {
	if (steps <= 0)
		return;
	const float cw = w / steps;
	for (int s = 0; s < steps; s++) {
		const float cx = x + s * cw;
		if (s % 4 == 0)
			semi::Screen::fillRect(vg, cx, y - 1.f, 1.f, h + 2.f, semi::theme::grid());
		if (mainLane[s] >= 0)
			semi::Screen::fillRect(vg, cx + 0.6f, y + 0.6f, std::max(0.8f, cw - 1.2f), h - 1.2f, laneColor(mainLane[s]));
	}
	if (ball >= 0 && ball < steps) {
		nvgBeginPath(vg);
		nvgRect(vg, x + ball * cw - 0.5f, y - 1.f, std::max(2.f, cw + 1.f), h + 2.f);
		nvgStrokeColor(vg, roleColor(ballRole));
		nvgStrokeWidth(vg, semi::platform::kStroke);
		nvgStroke(vg);
	}
}

inline const char* const kFormNames[] = {"CALL ONLY", "1+1", "2+2", "3+1", "4+4", "1+3"};
inline const char* const kAnswerNames[] = {"ECHO", "SHIFT", "INVERT", "MIRROR"};
inline const char* const kTimeNames[] = {"x1/4", "x1/2", "x3/4", "x1", "x3/2", "x2", "x3", "x4"};
inline const float kTimeValues[] = {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f};
inline const char* const kResetNames[] = {"Phrase", "1/2 bar", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars", "32 bars", "64 bars"};
inline const float kResetValues[] = {0.0f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f};
inline const char* const kEndingNames[] = {"Off", "1 beat", "2 beats", "1 bar"};

} // namespace dnamod

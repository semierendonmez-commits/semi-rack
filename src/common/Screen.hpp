#pragma once
#include <rack.hpp>
#include <string>
#include "Platform.hpp"
#include "Theme.hpp"

// Base class for every on-panel screen ("OLED window").
//
// The bezel and the glass are part of the static panel graphic (SVG in Rack,
// PNG on MetaModule). A Screen widget covers exactly the glass and draws only
// the live content. Rules that keep it cheap and correct on MetaModule:
//   * draw in drawLayer(1) only (the only layer MetaModule renders; in Rack it
//     is the light layer, so screens stay lit when the room is dark);
//   * never draw outside the box (MetaModule clips; we scissor in Rack too);
//   * read data from a TripleBuffer snapshot published at kUiRateHz, never
//     touch engine state directly;
//   * keep frames stable when the data is stable (no free-running animation,
//     no per-frame noise): MetaModule sends only changed pixels;
//   * thick strokes, few points, opaque colours, ShareTechMono (built into
//     both Rack and MetaModule firmware, so no font file to ship).

namespace semi {

struct Screen : widget::Widget {
	std::shared_ptr<window::Font> font;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			nvgSave(args.vg);
			nvgScissor(args.vg, 0.f, 0.f, box.size.x, box.size.y);
			drawScreen(args.vg);
			nvgRestore(args.vg);
		}
		widget::Widget::drawLayer(args, layer);
	}

	// Implemented by each screen. Called ~20 FPS on MetaModule, vsync in Rack.
	virtual void drawScreen(NVGcontext* vg) = 0;

	// ---- helpers ---------------------------------------------------------

	bool ensureFont() {
		if (!font)
			font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		return font && font->handle >= 0;
	}

	void text(NVGcontext* vg, float x, float y, const std::string& s, float size, NVGcolor c,
	          int align = NVG_ALIGN_LEFT | NVG_ALIGN_TOP) {
		if (!ensureFont())
			return;
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, std::max(size, platform::kMinText));
		nvgTextAlign(vg, align);
		nvgFillColor(vg, c);
		nvgText(vg, x, y, s.c_str(), nullptr);
	}

	static void fillRect(NVGcontext* vg, float x, float y, float w, float h, NVGcolor c) {
		nvgBeginPath(vg);
		nvgRect(vg, x, y, w, h);
		nvgFillColor(vg, c);
		nvgFill(vg);
	}

	static void line(NVGcontext* vg, float x0, float y0, float x1, float y1, NVGcolor c,
	                 float width = platform::kThinStroke) {
		nvgBeginPath(vg);
		nvgMoveTo(vg, x0, y0);
		nvgLineTo(vg, x1, y1);
		nvgStrokeColor(vg, c);
		nvgStrokeWidth(vg, width);
		nvgStroke(vg);
	}

	// Dashed horizontal or vertical line (no nvg dash support on either side).
	static void dashed(NVGcontext* vg, float x0, float y0, float x1, float y1, NVGcolor c,
	                   float dash = 4.f, float width = platform::kThinStroke) {
		float dx = x1 - x0, dy = y1 - y0;
		float len = std::sqrt(dx * dx + dy * dy);
		if (len <= 0.f)
			return;
		int n = (int) (len / (2.f * dash));
		nvgBeginPath(vg);
		for (int i = 0; i <= n; i++) {
			float t0 = std::min(1.f, (2.f * dash * i) / len);
			float t1 = std::min(1.f, (2.f * dash * i + dash) / len);
			nvgMoveTo(vg, x0 + dx * t0, y0 + dy * t0);
			nvgLineTo(vg, x0 + dx * t1, y0 + dy * t1);
		}
		nvgStrokeColor(vg, c);
		nvgStrokeWidth(vg, width);
		nvgStroke(vg);
	}

	// Polyline through n evenly spaced values, decimated to kMaxPolyPoints.
	// ys are already in box coordinates.
	static void polyline(NVGcontext* vg, const float* ys, int n, float x0, float w, NVGcolor c,
	                     float width = platform::kStroke) {
		if (n < 2)
			return;
		int step = std::max(1, (n + platform::kMaxPolyPoints - 1) / platform::kMaxPolyPoints);
		nvgBeginPath(vg);
		nvgMoveTo(vg, x0, ys[0]);
		for (int i = step; i < n; i += step)
			nvgLineTo(vg, x0 + w * i / (n - 1), ys[i]);
		nvgLineTo(vg, x0 + w, ys[n - 1]);
		nvgStrokeColor(vg, c);
		nvgStrokeWidth(vg, width);
		nvgLineJoin(vg, NVG_ROUND);
		nvgStroke(vg);
	}

	// Polyline through explicit points (xs, ys), decimated.
	static void path(NVGcontext* vg, const float* xs, const float* ys, int n, NVGcolor c,
	                 float width = platform::kStroke) {
		if (n < 2)
			return;
		int step = std::max(1, (n + platform::kMaxPolyPoints - 1) / platform::kMaxPolyPoints);
		nvgBeginPath(vg);
		nvgMoveTo(vg, xs[0], ys[0]);
		for (int i = step; i < n; i += step)
			nvgLineTo(vg, xs[i], ys[i]);
		nvgLineTo(vg, xs[n - 1], ys[n - 1]);
		nvgStrokeColor(vg, c);
		nvgStrokeWidth(vg, width);
		nvgLineJoin(vg, NVG_ROUND);
		nvgStroke(vg);
	}
};

// Convenience: create a screen at a glass rect given in mm (from layout headers).
template <class TScreen>
TScreen* createScreenMm(math::Vec posMm, math::Vec sizeMm) {
	TScreen* s = new TScreen;
	s->box.pos = mm2px(posMm);
	s->box.size = mm2px(sizeMm);
	return s;
}

} // namespace semi

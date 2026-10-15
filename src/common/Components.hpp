#pragma once
#include "../plugin.hpp"

// Panel hardware for the series. Graphics live in res/components (SVG, Rack)
// and metamodule/assets/components (PNG, MetaModule); both are produced by
// tools/panelgen.py so the two platforms always match.

namespace semi {

inline std::shared_ptr<window::Svg> comp(const char* name) {
	return Svg::load(asset::plugin(pluginInstance, std::string("res/components/") + name + ".svg"));
}

// Knob = fixed skirt (bg) + rotating cap with the indicator line.
struct KnobBase : app::SvgKnob {
	widget::SvgWidget* bg;
	KnobBase(const char* cap, const char* skirt) {
		minAngle = -0.83f * M_PI;
		maxAngle = 0.83f * M_PI;
		bg = new widget::SvgWidget;
		fb->addChildBelow(bg, tw);
		setSvg(comp(cap));
		bg->setSvg(comp(skirt));
	}
};

// L 10 mm, M 8.6 mm, S 6 mm. "Hot" = magenta indicator (destructive / chaos params).
struct KnobL : KnobBase { KnobL() : KnobBase("KnobL_cap", "KnobL_skirt") {} };
struct KnobLHot : KnobBase { KnobLHot() : KnobBase("KnobL_cap_hot", "KnobL_skirt") {} };
struct KnobM : KnobBase { KnobM() : KnobBase("KnobM_cap", "KnobM_skirt") {} };
struct KnobMHot : KnobBase { KnobMHot() : KnobBase("KnobM_cap_hot", "KnobM_skirt") {} };
struct KnobS : KnobBase { KnobS() : KnobBase("KnobS_cap", "KnobS_skirt") {} };
struct KnobSHot : KnobBase { KnobSHot() : KnobBase("KnobS_cap_hot", "KnobS_skirt") {} };

// 3.5 mm jack with hex nut. Bus = magenta nut (FamilyBus CV, 4-channel poly).
struct Jack : app::SvgPort { Jack() { setSvg(comp("Jack")); } };
struct BusJack : app::SvgPort { BusJack() { setSvg(comp("JackBus")); } };

// Round tactile button (mode/cycle) and the big square performance pad.
struct Button : app::SvgSwitch {
	Button() {
		momentary = true;
		addFrame(comp("Button_0"));
		addFrame(comp("Button_1"));
	}
};
struct PadMagenta : app::SvgSwitch {
	PadMagenta() {
		momentary = true;
		addFrame(comp("PadMagenta_0"));
		addFrame(comp("PadMagenta_1"));
	}
};
struct PadCyan : app::SvgSwitch {
	PadCyan() {
		momentary = true;
		addFrame(comp("PadCyan_0"));
		addFrame(comp("PadCyan_1"));
	}
};

// LEDs in the palette (off colour comes from the light base, see theme).
template <typename TBase = GrayModuleLightWidget>
struct TCyanLight : TBase { TCyanLight() { this->addBaseColor(nvgRGB(0x4f, 0xe3, 0xd6)); } };
template <typename TBase = GrayModuleLightWidget>
struct TMagentaLight : TBase { TMagentaLight() { this->addBaseColor(nvgRGB(0xff, 0x4f, 0x9a)); } };
template <typename TBase = GrayModuleLightWidget>
struct TAmberLight : TBase { TAmberLight() { this->addBaseColor(nvgRGB(0xe8, 0xb0, 0x4a)); } };

using CyanLed = MediumLight<TCyanLight<>>;
using MagentaLed = MediumLight<TMagentaLight<>>;
using AmberLed = MediumLight<TAmberLight<>>;
using CyanLedSmall = SmallLight<TCyanLight<>>;
using MagentaLedSmall = SmallLight<TMagentaLight<>>;
using AmberLedSmall = SmallLight<TAmberLight<>>;

} // namespace semi

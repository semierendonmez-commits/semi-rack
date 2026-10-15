// STUB — replaced by the port of BREAKMED.
#include "../plugin.hpp"

struct Breakmed : Module {
	Breakmed() { config(0, 0, 0, 0); }
};

struct BreakmedWidget : ModuleWidget {
	BreakmedWidget(Breakmed* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelBreakmed = createModel<Breakmed, BreakmedWidget>("Breakmed");

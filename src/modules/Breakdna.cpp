// STUB — replaced by the port of BREAKDNA.
#include "../plugin.hpp"

struct Breakdna : Module {
	Breakdna() { config(0, 0, 0, 0); }
};

struct BreakdnaWidget : ModuleWidget {
	BreakdnaWidget(Breakdna* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelBreakdna = createModel<Breakdna, BreakdnaWidget>("Breakdna");

// STUB — replaced by the port of BREAKBEAT.
#include "../plugin.hpp"

struct Breakbeat : Module {
	Breakbeat() { config(0, 0, 0, 0); }
};

struct BreakbeatWidget : ModuleWidget {
	BreakbeatWidget(Breakbeat* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelBreakbeat = createModel<Breakbeat, BreakbeatWidget>("Breakbeat");

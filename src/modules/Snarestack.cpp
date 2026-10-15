// STUB — replaced by the port of SNARESTACK.
#include "../plugin.hpp"

struct Snarestack : Module {
	Snarestack() { config(0, 0, 0, 0); }
};

struct SnarestackWidget : ModuleWidget {
	SnarestackWidget(Snarestack* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelSnarestack = createModel<Snarestack, SnarestackWidget>("Snarestack");

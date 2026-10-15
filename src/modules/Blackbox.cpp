// STUB — replaced by the port of BLACKBOX.
#include "../plugin.hpp"

struct Blackbox : Module {
	Blackbox() { config(0, 0, 0, 0); }
};

struct BlackboxWidget : ModuleWidget {
	BlackboxWidget(Blackbox* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelBlackbox = createModel<Blackbox, BlackboxWidget>("Blackbox");

// STUB — replaced by the port of DUBPLATE.
#include "../plugin.hpp"

struct Dubplate : Module {
	Dubplate() { config(0, 0, 0, 0); }
};

struct DubplateWidget : ModuleWidget {
	DubplateWidget(Dubplate* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelDubplate = createModel<Dubplate, DubplateWidget>("Dubplate");

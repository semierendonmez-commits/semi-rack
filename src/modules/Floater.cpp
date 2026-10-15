// STUB — replaced by the port of FLOATER.
#include "../plugin.hpp"

struct Floater : Module {
	Floater() { config(0, 0, 0, 0); }
};

struct FloaterWidget : ModuleWidget {
	FloaterWidget(Floater* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelFloater = createModel<Floater, FloaterWidget>("Floater");

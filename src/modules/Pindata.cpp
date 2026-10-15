// STUB — replaced by the port of PINDATA.
#include "../plugin.hpp"

struct Pindata : Module {
	Pindata() { config(0, 0, 0, 0); }
};

struct PindataWidget : ModuleWidget {
	PindataWidget(Pindata* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelPindata = createModel<Pindata, PindataWidget>("Pindata");

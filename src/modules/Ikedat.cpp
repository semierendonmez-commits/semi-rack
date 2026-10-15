// STUB — replaced by the port of IKEDAT.
#include "../plugin.hpp"

struct Ikedat : Module {
	Ikedat() { config(0, 0, 0, 0); }
};

struct IkedatWidget : ModuleWidget {
	IkedatWidget(Ikedat* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelIkedat = createModel<Ikedat, IkedatWidget>("Ikedat");

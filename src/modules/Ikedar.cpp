// STUB — replaced by the port of IKEDAR.
#include "../plugin.hpp"

struct Ikedar : Module {
	Ikedar() { config(0, 0, 0, 0); }
};

struct IkedarWidget : ModuleWidget {
	IkedarWidget(Ikedar* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelIkedar = createModel<Ikedar, IkedarWidget>("Ikedar");

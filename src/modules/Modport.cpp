// STUB — replaced by the port of MODPORT.
#include "../plugin.hpp"

struct Modport : Module {
	Modport() { config(0, 0, 0, 0); }
};

struct ModportWidget : ModuleWidget {
	ModportWidget(Modport* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelModport = createModel<Modport, ModportWidget>("Modport");

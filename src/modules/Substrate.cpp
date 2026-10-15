// STUB — replaced by the port of SUBSTRATE.
#include "../plugin.hpp"

struct Substrate : Module {
	Substrate() { config(0, 0, 0, 0); }
};

struct SubstrateWidget : ModuleWidget {
	SubstrateWidget(Substrate* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelSubstrate = createModel<Substrate, SubstrateWidget>("Substrate");

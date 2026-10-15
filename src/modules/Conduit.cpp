// STUB — replaced by the port of CONDUIT.
#include "../plugin.hpp"

struct Conduit : Module {
	Conduit() { config(0, 0, 0, 0); }
};

struct ConduitWidget : ModuleWidget {
	ConduitWidget(Conduit* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelConduit = createModel<Conduit, ConduitWidget>("Conduit");

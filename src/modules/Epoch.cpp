// STUB — replaced by the port of the Epoch engine.
#include "../plugin.hpp"

struct Epoch : Module {
	Epoch() { config(0, 0, 0, 0); }
};

struct EpochWidget : ModuleWidget {
	EpochWidget(Epoch* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelEpoch = createModel<Epoch, EpochWidget>("Epoch");

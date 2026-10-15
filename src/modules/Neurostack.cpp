// STUB — replaced by the port of NEUROSTACK.
#include "../plugin.hpp"

struct Neurostack : Module {
	Neurostack() { config(0, 0, 0, 0); }
};

struct NeurostackWidget : ModuleWidget {
	NeurostackWidget(Neurostack* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelNeurostack = createModel<Neurostack, NeurostackWidget>("Neurostack");

// STUB — replaced by the port of QUANTA.
#include "../plugin.hpp"

struct Quanta : Module {
	Quanta() { config(0, 0, 0, 0); }
};

struct QuantaWidget : ModuleWidget {
	QuantaWidget(Quanta* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelQuanta = createModel<Quanta, QuantaWidget>("Quanta");

// STUB — replaced by the port of SETBOOK.
#include "../plugin.hpp"

struct Setbook : Module {
	Setbook() { config(0, 0, 0, 0); }
};

struct SetbookWidget : ModuleWidget {
	SetbookWidget(Setbook* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelSetbook = createModel<Setbook, SetbookWidget>("Setbook");

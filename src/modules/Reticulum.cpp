// STUB — replaced by the port of the Reticulum engine.
#include "../plugin.hpp"

struct Reticulum : Module {
	Reticulum() { config(0, 0, 0, 0); }
};

struct ReticulumWidget : ModuleWidget {
	ReticulumWidget(Reticulum* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelReticulum = createModel<Reticulum, ReticulumWidget>("Reticulum");

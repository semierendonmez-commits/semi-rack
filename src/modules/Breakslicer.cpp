// STUB — replaced by the port of BREAKSLICER.
#include "../plugin.hpp"

struct Breakslicer : Module {
	Breakslicer() { config(0, 0, 0, 0); }
};

struct BreakslicerWidget : ModuleWidget {
	BreakslicerWidget(Breakslicer* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelBreakslicer = createModel<Breakslicer, BreakslicerWidget>("Breakslicer");

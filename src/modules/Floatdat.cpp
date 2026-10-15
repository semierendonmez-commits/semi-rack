// STUB — replaced by the port of FLOATDAT.
#include "../plugin.hpp"

struct Floatdat : Module {
	Floatdat() { config(0, 0, 0, 0); }
};

struct FloatdatWidget : ModuleWidget {
	FloatdatWidget(Floatdat* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelFloatdat = createModel<Floatdat, FloatdatWidget>("Floatdat");

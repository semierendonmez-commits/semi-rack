// STUB — replaced by the port of TST_INGP.
#include "../plugin.hpp"

struct TstIngp : Module {
	TstIngp() { config(0, 0, 0, 0); }
};

struct TstIngpWidget : ModuleWidget {
	TstIngpWidget(TstIngp* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelTstIngp = createModel<TstIngp, TstIngpWidget>("TstIngp");

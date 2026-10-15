// STUB — replaced by the port of NMS.
#include "../plugin.hpp"

struct Nms : Module {
	Nms() { config(0, 0, 0, 0); }
};

struct NmsWidget : ModuleWidget {
	NmsWidget(Nms* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelNms = createModel<Nms, NmsWidget>("Nms");

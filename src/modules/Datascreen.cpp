// STUB — replaced by the port of DATASCREEN.
#include "../plugin.hpp"

struct Datascreen : Module {
	Datascreen() { config(0, 0, 0, 0); }
};

struct DatascreenWidget : ModuleWidget {
	DatascreenWidget(Datascreen* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelDatascreen = createModel<Datascreen, DatascreenWidget>("Datascreen");

// STUB — replaced by the port of ILLUSORIUM.
#include "../plugin.hpp"

struct Illusorium : Module {
	Illusorium() { config(0, 0, 0, 0); }
};

struct IllusoriumWidget : ModuleWidget {
	IllusoriumWidget(Illusorium* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelIllusorium = createModel<Illusorium, IllusoriumWidget>("Illusorium");

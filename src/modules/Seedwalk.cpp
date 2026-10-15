// STUB — replaced by the port of the Seedwalk engine.
#include "../plugin.hpp"

struct Seedwalk : Module {
	Seedwalk() { config(0, 0, 0, 0); }
};

struct SeedwalkWidget : ModuleWidget {
	SeedwalkWidget(Seedwalk* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelSeedwalk = createModel<Seedwalk, SeedwalkWidget>("Seedwalk");

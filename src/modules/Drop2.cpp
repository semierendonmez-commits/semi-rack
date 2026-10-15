// STUB — replaced by the port of DROP II.
#include "../plugin.hpp"

struct Drop2 : Module {
	Drop2() { config(0, 0, 0, 0); }
};

struct Drop2Widget : ModuleWidget {
	Drop2Widget(Drop2* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelDrop2 = createModel<Drop2, Drop2Widget>("Drop2");

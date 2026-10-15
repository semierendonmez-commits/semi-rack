// STUB — replaced by the port of ACID.
#include "../plugin.hpp"

struct Acid : Module {
	Acid() { config(0, 0, 0, 0); }
};

struct AcidWidget : ModuleWidget {
	AcidWidget(Acid* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelAcid = createModel<Acid, AcidWidget>("Acid");

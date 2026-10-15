// STUB — replaced by the port of SPLICEBUS.
#include "../plugin.hpp"

struct Splicebus : Module {
	Splicebus() { config(0, 0, 0, 0); }
};

struct SplicebusWidget : ModuleWidget {
	SplicebusWidget(Splicebus* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelSplicebus = createModel<Splicebus, SplicebusWidget>("Splicebus");

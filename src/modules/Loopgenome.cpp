// STUB — replaced by the port of LOOPGENOME.
#include "../plugin.hpp"

struct Loopgenome : Module {
	Loopgenome() { config(0, 0, 0, 0); }
};

struct LoopgenomeWidget : ModuleWidget {
	LoopgenomeWidget(Loopgenome* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelLoopgenome = createModel<Loopgenome, LoopgenomeWidget>("Loopgenome");

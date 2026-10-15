// STUB — replaced by the port of the Turnaround engine.
#include "../plugin.hpp"

struct Turnaround : Module {
	Turnaround() { config(0, 0, 0, 0); }
};

struct TurnaroundWidget : ModuleWidget {
	TurnaroundWidget(Turnaround* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelTurnaround = createModel<Turnaround, TurnaroundWidget>("Turnaround");

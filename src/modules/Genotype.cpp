// STUB — replaced by the port of GENOTYPE.
#include "../plugin.hpp"

struct Genotype : Module {
	Genotype() { config(0, 0, 0, 0); }
};

struct GenotypeWidget : ModuleWidget {
	GenotypeWidget(Genotype* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelGenotype = createModel<Genotype, GenotypeWidget>("Genotype");

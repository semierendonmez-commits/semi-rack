// STUB — replaced by the port of VERTEX.
#include "../plugin.hpp"

struct Vertex : Module {
	Vertex() { config(0, 0, 0, 0); }
};

struct VertexWidget : ModuleWidget {
	VertexWidget(Vertex* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelVertex = createModel<Vertex, VertexWidget>("Vertex");

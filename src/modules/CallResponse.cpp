// STUB — replaced by the port of CALL & RESPONSE.
#include "../plugin.hpp"

struct CallResponse : Module {
	CallResponse() { config(0, 0, 0, 0); }
};

struct CallResponseWidget : ModuleWidget {
	CallResponseWidget(CallResponse* module) { setModule(module); box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT); }
};

Model* modelCallResponse = createModel<CallResponse, CallResponseWidget>("CallResponse");

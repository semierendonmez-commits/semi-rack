#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;

	p->addModel(modelNexusclip);
	p->addModel(modelReticulum);
	p->addModel(modelSeedwalk);
	p->addModel(modelEpoch);
	p->addModel(modelTurnaround);
}

#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;

	p->addModel(modelNexusclip);
	p->addModel(modelReticulum);
	p->addModel(modelSeedwalk);
	p->addModel(modelEpoch);
	p->addModel(modelTurnaround);
	p->addModel(modelFloater);
	p->addModel(modelFloatdat);
	p->addModel(modelSplicebus);
	p->addModel(modelIkedar);
	p->addModel(modelJdat);
	p->addModel(modelTstIngp);
	p->addModel(modelPindata);
	p->addModel(modelIkedat);
	p->addModel(modelDatascreen);
	p->addModel(modelBreakdna);
	p->addModel(modelGenotype);
	p->addModel(modelLoopgenome);
	p->addModel(modelBreakmed);
	p->addModel(modelBreakslicer);
	p->addModel(modelBreakbeat);
	p->addModel(modelNeurostack);
	p->addModel(modelSnarestack);
	p->addModel(modelSubstrate);
	p->addModel(modelDrop2);
	p->addModel(modelCallResponse);
	p->addModel(modelAcid);
	p->addModel(modelQuanta);
	p->addModel(modelIllusorium);
	p->addModel(modelVertex);
	p->addModel(modelNms);
	p->addModel(modelModport);
	p->addModel(modelDubplate);
	p->addModel(modelConduit);
	p->addModel(modelBlackbox);
	p->addModel(modelSetbook);
}

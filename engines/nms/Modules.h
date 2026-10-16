// registry hooks of the module groups
#pragma once
#include "Module.h"
namespace nms
{
std::unique_ptr<Module> createDrum (const std::string&);
std::unique_ptr<Module> createSynth (const std::string&);
std::unique_ptr<Module> createFx (const std::string&);
std::unique_ptr<Module> createControl (const std::string&);
void drumSpecs (std::vector<const TypeSpec*>&);
void synthSpecs (std::vector<const TypeSpec*>&);
void fxSpecs (std::vector<const TypeSpec*>&);
void controlSpecs (std::vector<const TypeSpec*>&);
std::unique_ptr<Module> createInstr2 (const std::string&);
void instr2Specs (std::vector<const TypeSpec*>&);
std::unique_ptr<Module> createUpstream (const std::string&);
void upstreamSpecs (std::vector<const TypeSpec*>&);
void setUpstreamDataDir (const std::string&);
std::shared_ptr<const SampleData> defaultSample();
// DIDIMIDI MIDI output queue
int drainMidi (MidiOut* out, int max);
// TRANSPORT & SCALE: the scale tables
void buildScale (Ctx&, int key, bool minor, int notes, bool twelveTone, int notesPerOct);
}
namespace nms
{
enum Request { rqLoad = 1, rqDefault = 2, rqRestart = 4, rqAdd = 1 << 4, rqDelete = 1 << 5, rqClear = 1 << 6, rqRandGen = 1 << 7, rqRandSnd = 1 << 8,
               rqNext = 1 << 9, rqPrev = 1 << 10, rqRandom = 1 << 11, rqResetMacros = 1 << 12, rqChaosRoll = 1 << 13, rqSceneFirst = 1 << 14, rqSceneNext = 1 << 15 };
int transportExternalRun (Module*);
int patternsSelect (Module*);
float wanderSpeedCv (Module*);
int wanderPlayGate (Module*);
float chaosAmountCv (Module*);
}

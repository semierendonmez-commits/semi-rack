// semi-rack: vendored; namespaces wrapped in dnafam:: (see sources.mk for every change).
// GENOTYPE: the walk a break "wants", learned from its DNA.
//
//   Lanes      every hit of A gets one of eight kit lanes (KICK SNARE GHOST HAT OPEN
//              RIDE PERC FX) from its class and its sound (decay, band shares).
//   Statistics what follows what, tick to tick: P(next symbol | this symbol) over the
//              loop, the symbols being the step's loudest lane or REST.
//   Learned    matrix weight from step i to step j:
//                 metric(j, i+1)        same place in the beat as the step that would
//                                       come next (1), same 8th (.22), else (.05);
//                                       x1.6 same half-bar place, x1.4 same bar place
//               x (0.15 + P(sym j | sym i))
//               x (0.25 + similarity of j to what would come next)
//              Learned from B: B's statistics and B's next step, played on A's steps,
//              so A's sound follows B's pattern (a symbolic cross). BLEND mixes them.
//   Call       (1 - MUTATION) x the plain run + MUTATION x the learned walk, tempered.
//   Response   ECHO (the call, hotter or colder), SHIFT (every jump moved k steps),
//              INVERT (what the call avoided, on the same metric places), MIRROR
//              (the bar walked backwards).
//   Fill       ENDING: the last beat(s) of the phrase roll: snare / ghost steps repeat.
//
// Plain C++20, no JUCE.
#pragma once

#include "../breakdna/Dna.h"
#include "Walker.h"

namespace dnafam::geno
{
using walk::Plain;
using walk::kMaxSteps;

enum Lane { LKick = 0, LSnare, LGhost, LHat, LOpen, LRide, LPerc, LFx, NumLanes };
constexpr int kRest = NumLanes;                 // symbol of an empty step
constexpr int kSyms = NumLanes + 1;

inline const char* laneName (int l)  { static const char* n[] = { "KICK", "SNARE", "GHOST", "HAT", "OPEN", "RIDE", "PERC", "FX" }; return n[l & 7]; }
inline const char* laneShort (int l) { static const char* n[] = { "K", "S", "G", "H", "O", "R", "P", "X" }; return n[l & 7]; }
inline const int laneDefaultNote[NumLanes] { 36, 38, 37, 42, 46, 51, 39, 49 };

enum Answer { AnsEcho = 0, AnsShift, AnsInvert, AnsMirror, NumAnswers };
enum Form { FormCall = 0, Form11, Form22, Form31, Form44, Form13, NumForms };
enum Ending { EndOff = 0, EndBeat, End2Beats, EndBar, NumEndings };
enum Role { RoleCall = 0, RoleResponse, RoleFill };

inline int formCallBars (int f) { static const int c[] = { 4, 1, 2, 3, 4, 1 }; return c[std::clamp (f, 0, NumForms - 1)]; }
inline int formRespBars (int f) { static const int r[] = { 0, 1, 2, 1, 4, 3 }; return r[std::clamp (f, 0, NumForms - 1)]; }
inline int formBars (int f) { return formCallBars (f) + formRespBars (f); }
inline int endingTicks (int e) { static const int t[] = { 0, 4, 8, 16 }; return t[std::clamp (e, 0, NumEndings - 1)]; }

struct StepInfo
{
    uint16_t mask = 0;          // lanes present
    int8_t main = -1;           // loudest lane, -1 rest
    float level = 0.0f;         // loudest hit's level
};

struct Learned
{
    int steps = 0;              // A's steps (the walk's states)
    int stepsB = 0;
    std::array<StepInfo, kMaxSteps> a {}, b {};
    std::vector<uint8_t> hitLane;               // lane of each of A's hits
    std::vector<int> hitsByStep;                // A's hits grouped by step
    std::array<int, kMaxSteps + 1> stepStart {};// hitsByStep[stepStart[s] .. stepStart[s + 1])
    float statA[kSyms][kSyms] {}, statB[kSyms][kSyms] {};
    Plain fromA {}, fromB {};                   // 0..100
};

/** Lane of one hit of a break (A: features of A's slices). */
int laneOf (const dna::Hit& h, const std::vector<dna::Features>& f);

/** Everything the walk can learn from the kit (A and B). */
Learned learn (const dna::Kit& k);

/** The plain run 1..n (the device's default, closed into a loop). */
void linear (Plain& m, int n);

/** (1 - x) a + x b, then every row scaled so its largest weight is 100. */
void mix (const Plain& a, const Plain& b, float x, int n, Plain& out);

/** Each row raised to 1/temp (temp < 1 sharper, > 1 flatter), rescaled to max 100. */
void temper (Plain& m, float temp, int n);

struct DeriveParams
{
    float mutation = 0.5f;      // 0 the break as it is .. 1 the learned walk
    float temp = 1.0f;
    int answer = AnsEcho;
    float answerTemp = 1.6f;
    int shift = 8;
    float roll = 0.6f;          // ENDING: how much the fill rolls
};

/** call, response and fill from the genome matrix (learned, maybe edited). */
void derive (const Plain& genome, const Learned& L, const DeriveParams& p, Plain& call, Plain& resp, Plain& fill);

/** Role of the tick at 16th position pos16 (absolute) for a form and ending. */
Role roleAt (double pos16, int form, int ending);

} // namespace dnafam::geno

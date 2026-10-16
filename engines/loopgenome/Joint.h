// semi-rack: vendored; namespaces wrapped in dnafam:: (see sources.mk for every change).
// LOOPGENOME: the break and its bass line learned as ONE loop.
//
// Every 16th step of the loop is a joint state: what the drums do there (kick, snare,
// another hit, nothing) and what the bass does (rest, held, a note on the root, another
// note). The walk counts how the joint state moves from step to step, P(J_next | J), so
// "the kick is here, the bass starts a note" and "the snare is here, the bass is silent"
// are learned together. One ball walks the loop: the step it lands on plays the break's
// hits AND the bass cell of that step, with that step's articulation. A jump moves both,
// so a drum switch-up and a bass switch-up are the same event.
//
//   learned weight from step i to step j (e = the step after i, where the loop would go):
//        metric (j, e)                same place in the beat (GENOTYPE)
//      x skeleton (j, e)              1 if j has e's kick / snare (SKELETON), else 0
//      x (0.15 + P (J_j | J_i))       the joint statistics
//      x (0.25 + drums alike (j, e))
//      x (0.25 + bass alike (j, e)) ^ COUPLE
//      x held-note entry              a held bass cell after silence is unlikely
//
// SKELETON (kick + snare, kick, off) also masks the call and the answer: whatever
// MUTATION does, the kick and the snare land where the loop has them (the fill is free).
//
// Plain C++20, no JUCE.
#pragma once

#include "Bass.h"

namespace dnafam::lg
{
using geno::Plain;

// the backbeat: a snare, or a "snare" the kit heard as a body-heavy hit (PERC)
constexpr unsigned kSnareLanes = (1u << geno::LSnare) | (1u << geno::LPerc);

enum DrumSym { DKick = 0, DSnare, DOther, DRest, NumDrumSyms };
enum BassSym { BSRest = 0, BSTie, BSRoot, BSOther, NumBassSyms };
constexpr int kJointSyms = (int) NumDrumSyms * (int) NumBassSyms;
inline const char* drumSymName (int d) { static const char* n[] = { "KICK", "SNARE", "HIT", "REST" }; return n[d & 3]; }
inline const char* bassSymName (int b) { static const char* n[] = { "REST", "HELD", "ROOT", "NOTE" }; return n[b & 3]; }

enum Skeleton { SkelKickSnare = 0, SkelKick, SkelOff, NumSkeletons };
enum ArtMap { MapHalves = 0, MapBars, MapNotes, MapDrums, MapOne, NumArtMaps };
enum FillBass { FillFollow = 0, FillPickup, FillDrop, NumFillBass };

struct Joint
{
    int steps = 0;
    BassLine bass;                              // fitted onto the break's steps
    std::array<uint8_t, kMaxSteps> skel {};     // bit 0 kick, bit 1 snare
    std::array<int8_t, kMaxSteps> dsym {}, bsym {};
    float stat[kJointSyms][kJointSyms] {};      // P (next joint symbol | this one)
    float coupling[NumDrumSyms][NumBassSyms] {};// P (bass symbol | drum symbol) on the same step
    Plain fromJoint {};                         // the learned joint walk, 0..100

    // readouts: what the loop does (shares 0..1)
    float onsetsOnKick = 0.0f;                  // bass notes that start on a kick step
    float kicksWithOnset = 0.0f;                // kicks that start a bass note
    float bassOnSnare = 0.0f;                   // snare steps where the bass sounds
    float pickups = 0.0f;                       // bars with a note in their last beat
};

int skeletonOf (const geno::StepInfo& s, int mode);
int drumSym (const geno::StepInfo& s);
int bassSym (const BassCell& c);

/** The joint walk of the break (L) and its bass line (fitted to L.steps). */
Joint learnJoint (const geno::Learned& L, const BassLine& bass, int skeleton, float couple);

/** call / answer / fill from the genome matrix (GENOTYPE's derive), then the skeleton
    mask on call and answer. */
void deriveJoint (const Plain& genome, const geno::Learned& L, const Joint& J, const geno::DeriveParams& p, int skeleton,
                  Plain& call, Plain& resp, Plain& fill);

/** The skeleton mask alone (rows that lose every weight go to the step that comes next). */
void maskSkeleton (Plain& m, const Joint& J, int skeleton);

/** Default articulation per step (1..8; 0 = hold), from the map and the loop. */
std::array<int, kMaxSteps> artMap (const Joint& J, int map);

} // namespace dnafam::lg

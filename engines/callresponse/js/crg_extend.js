// crg_extend.js — plugin extensions, loaded after crg_engine.js and before
// crg_bridge.js. The engine file itself stays untouched; everything here wraps
// or extends it through its public functions and tables.
//
//   1. STEPS up to 64
//   2. MODE "Custom": a user-built form (pass by pass, or step by step)
//   3. HARMONY: a chord progression derived from key, scale, style, ending and
//      the notes the call and response actually play

// ---------------------------------------------------------------- 1. steps
DATA.steps = Array.from({length: 63}, (_, i) => String(i + 2));     // 2..64

// slot packing: the engine packs rev*256+slot, which overflows past 256 slots
// (64 steps x 8 passes = 512). Same publish, wider key.
const CRG_SLOT_KEY = 4096;

// ---------------------------------------------------------------- 2. custom form
DATA.phraseModes.push('Custom');
S.custom = null;
const CUSTOM_VARIANTS = ['A', "A'", 'B', "B'", 'C', '~'];

function customDefault(){
  return { type: 'passes', passes: [{lane:'C', v:'A'}, {lane:'R', v:'A'}], mask: null };
}

// Turn any built-in mode into a custom form, so CUSTOMIZE starts from what was playing.
function customFromMode(mode){
  const plan = __crgPassPlan(mode);
  const steps = Number(S.params.steps) || 8;
  if(plan.interleave !== undefined){
    const mask = [];
    for(let i = 0; i < steps; i++) mask.push(__crgLaneForStep(plan, 0, i, steps) === C ? 'C' : 'R');
    return { type: 'steps', passes: [{lane:'C', v:'A'}, {lane:'R', v:'A'}], mask: mask };
  }
  const seq = plan.seq || [C, R];
  return { type: 'passes', mask: null, passes: seq.map(function(l, i){
    let v = 'A';
    if(plan.form) v = plan.form[i];
    else if(plan.vary && plan.vary[i]) v = '~';
    return { lane: l === C ? 'C' : 'R', v: v };
  }) };
}

function customMask(steps){
  const cu = S.custom || customDefault();
  const half = Math.max(1, Math.floor(steps / 2));
  const m = [];
  for(let i = 0; i < steps; i++){
    const src = cu.mask && cu.mask.length ? cu.mask[i % cu.mask.length] : (i < half ? 'C' : 'R');
    m.push(src === 'R' ? 'R' : 'C');
  }
  return m;
}

function customPlan(){
  const cu = S.custom || customDefault();
  const steps = Number(S.params.steps) || 8;
  if(cu.type === 'steps') return { interleave: 'mask', mask: customMask(steps) };
  const passes = (cu.passes && cu.passes.length) ? cu.passes.slice(0, 8) : customDefault().passes;
  const seq = passes.map(p => p.lane === 'R' ? R : C);
  if(passes.some(p => p.v === '~')) return { seq: seq, vary: passes.map(p => p.v === '~') };
  if(passes.some(p => p.v && p.v !== 'A')) return { seq: seq, form: passes.map(p => p.v || 'A') };
  return { seq: seq };
}

const __crgPassPlan = passPlan;
passPlan = function(mode){ return mode === 'Custom' ? customPlan() : __crgPassPlan(mode); };

const __crgLaneForStep = laneForStep;
laneForStep = function(plan, pass, i, steps){
  if(plan && plan.interleave === 'mask'){
    const m = plan.mask || [];
    return m.length ? (m[i % m.length] === 'R' ? R : C) : C;
  }
  return __crgLaneForStep(plan, pass, i, steps);
};

// UI edits. op: lane i | var i | add | remove | type | mask i | reset | fromMode
function crgCustom(op, a){
  ensureInit();
  if(!S.custom) S.custom = S.params.phraseMode === 'Custom' ? customDefault() : customFromMode(S.params.phraseMode);
  const cu = S.custom;
  const steps = Number(S.params.steps) || 8;
  if(!cu.passes || !cu.passes.length) cu.passes = customDefault().passes;
  a = Number(a) || 0;
  switch(op){
    case 'fromMode': S.custom = customFromMode(S.params.phraseMode); break;
    case 'reset':    S.custom = customDefault(); break;
    case 'lane':     if(cu.passes[a]) cu.passes[a].lane = cu.passes[a].lane === 'C' ? 'R' : 'C'; break;
    case 'var':      if(cu.passes[a]){
                       const k = CUSTOM_VARIANTS.indexOf(cu.passes[a].v);
                       cu.passes[a].v = CUSTOM_VARIANTS[(k + 1) % CUSTOM_VARIANTS.length];
                     } break;
    case 'add':      if(cu.passes.length < 8){
                       const last = cu.passes[cu.passes.length - 1];
                       cu.passes.push({ lane: last.lane === 'C' ? 'R' : 'C', v: 'A' });
                     } break;
    case 'remove':   if(cu.passes.length > 1) cu.passes.pop(); break;
    case 'type':     cu.type = cu.type === 'steps' ? 'passes' : 'steps';
                     if(cu.type === 'steps' && !cu.mask) cu.mask = customMask(steps); break;
    case 'mask':     cu.mask = customMask(steps); cu.mask[a % steps] = cu.mask[a % steps] === 'C' ? 'R' : 'C'; break;
    default: break;
  }
  clearVariants();
  if(S.params.phraseMode !== 'Custom') setmenu('phraseMode', DATA.phraseModes.indexOf('Custom'));
  else { pushDisplay(); nativeRefresh(); }
  rememberState();
  crgFormInfo();
}

// ---------------------------------------------------------------- 3. harmony
DATA.chordModes = ['Off', 'On'];
DATA.chordFamilies = ['Auto (style)', 'Melody-led', 'Minor loop', 'One-chord vamp', 'House 4-chord',
  'Rave stab', 'Trance lift', 'Detroit 7ths', 'Dub drift', 'Pedal + upper', 'Cadence builder'];
DATA.chordRhythms = ['Hold', 'Stab per change', 'Stabs on call', 'Stabs on response', 'Offbeat stabs', '8th pulse', 'Anticipated'];
DATA.chordChanges = ['Per pass', '2 per pass', '4 per pass', 'Per 2 passes', 'Per cycle'];
DATA.chordVoicings = ['Auto', 'Triads', '7ths', '9ths', 'Sus', 'Power', 'Stacked 4ths'];
DATA.chordOctaves = ['1', '2', '3', '4', '5'];
DATA.chordFollows = ['Off', 'Root move', 'Chord tones', 'Root + tones', 'Arpeggiate'];
DATA.followLaneSets = ['Both', 'Call', 'Response'];
DATA.chordSpans = ['1 cycle', '2 cycles', '4 cycles'];

Object.assign(ENUM_PARAMS, { chordMode: 'chordModes', chordFamily: 'chordFamilies',
  chordRhythm: 'chordRhythms', chordChange: 'chordChanges', chordVoicing: 'chordVoicings',
  chordOctave: 'chordOctaves', chordChannel: 'channels',
  chordFollow: 'chordFollows', followLanes: 'followLaneSets', chordSpan: 'chordSpans' });
Object.assign(DEFAULTS, { chordMode: 'Off', chordFamily: 'Auto (style)', chordRhythm: 'Hold',
  chordChange: 'Per pass', chordVoicing: 'Auto', chordOctave: '3', chordChannel: 'Channel 3',
  chordVelocity: 72, chordFollow: 'Off', followLanes: 'Both', chordSpan: '2 cycles' });
NUM_PARAMS.push('chordVelocity');
NUM_RANGES.chordVelocity = [1, 127, 1];
Object.assign(CTRL_LABELS, { chordMode: 'CHORD OUT', chordFamily: 'PROGRESSION', chordRhythm: 'RHYTHM',
  chordChange: 'CHANGE', chordVoicing: 'VOICING', chordOctave: 'CHORD OCT', chordChannel: 'CHORD CH',
  chordVelocity: 'CHORD VEL', chordFollow: 'FOLLOW', followLanes: 'FOLLOWERS', chordSpan: 'SPAN' });
PAGES.harmony = ['chordFollow', 'followLanes', 'chordFamily', 'chordChange', 'chordSpan',
                 'chordMode', 'chordRhythm', 'chordVoicing', 'chordOctave', 'chordChannel', 'chordVelocity'];
STEPPER_CTRLS.push('chordOctave', 'chordChannel', 'chordChange', 'chordMode', 'chordSpan', 'followLanes');
DICE_SKIP.push('chordMode', 'chordChannel', 'chordOctave', 'chordFollow', 'followLanes', 'chordSpan');
for(const k of Object.keys(DEFAULTS)) if(S.params && S.params[k] === undefined) S.params[k] = DEFAULTS[k];

S.chordSeed = 1;
S.chordMute = 0;
let crgChords = [];          // last computed progression, for the FORM page

const ROMAN = ['I', 'II', 'III', 'IV', 'V', 'VI', 'VII'];
// Seven-note parents tried for scales with fewer or more notes
const PARENTS = [
  [0,2,3,5,7,8,10], [0,2,3,5,7,9,10], [0,1,3,5,7,8,10], [0,2,4,5,7,9,11],
  [0,2,4,5,7,9,10], [0,2,3,5,7,8,11], [0,2,3,5,7,9,11], [0,2,4,6,7,9,11], [0,1,3,5,6,8,10]];

function parentScale(iv){
  const set = iv.map(x => ((x % 12) + 12) % 12);
  if(set.length === 7) return set.slice().sort((a,b) => a-b);
  const minor = set.indexOf(3) >= 0 && set.indexOf(4) < 0;
  let best = null, bestScore = -1;
  for(const p of PARENTS){
    let score = 0;
    for(const x of set) if(p.indexOf(x) >= 0) score++;
    if(minor === (p[2] === 3)) score += 0.5;
    if(score > bestScore){ bestScore = score; best = p; }
  }
  return best;
}

function rng(seed){
  let s = (seed >>> 0) || 1;
  return function(){ s ^= s << 13; s ^= s >>> 17; s ^= s << 5; return ((s >>> 0) % 100000) / 100000; };
}

function familyFor(style){
  const f = S.params.chordFamily;
  if(f !== 'Auto (style)') return f;
  const s = String(style).toLowerCase(), cat = styleCategory(style);
  if(s.includes('trance') || s.includes('hoover')) return 'Trance lift';
  if(s.includes('detroit') || s.includes('deep')) return 'Detroit 7ths';
  if(s.includes('dub')) return 'Dub drift';
  if(s.includes('stab') || cat === 'rave') return 'Rave stab';
  if(s.includes('tension')) return 'Pedal + upper';
  if(s.includes('resolver') || s.includes('question')) return 'Cadence builder';
  if(cat === 'house') return 'House 4-chord';
  if(cat === 'acid') return s.includes('hypno') || s.includes('sparse') ? 'One-chord vamp' : 'Minor loop';
  if(cat === 'techno') return s.includes('minimal') || s.includes('tool') || s.includes('loop') ? 'One-chord vamp' : 'Dub drift';
  return 'Melody-led';
}

// degree sequences (0 = tonic of the parent scale)
const FAMILY_TABLES = {
  'Minor loop':      [[0,5,2,6], [0,3,6,0], [0,5,3,4], [0,6,5,6]],
  'One-chord vamp':  [[0], [0,0,0,6], [0,0,0,3]],
  'House 4-chord':   [[0,3,6,2], [1,4,0,0], [0,5,3,4], [3,4,2,5]],
  'Rave stab':       [[0,0,6,5], [0,6,5,6], [0,5,6,0]],
  'Trance lift':     [[0,5,2,6], [5,6,0,0], [0,5,3,4], [5,3,0,6]],
  'Detroit 7ths':    [[0,3], [0,6], [1,4], [0,5]],
  'Dub drift':       [[0,0,3,3], [0,6], [0,0,5,5]],
  'Pedal + upper':   [[0,5,6,0], [0,3,4,0], [0,1,0,6]],
  'Cadence builder': [[0,3,4,0], [0,5,3,4], [5,1,4,0]],
  'Melody-led':      [[]]
};

function voicingFor(family){
  const v = S.params.chordVoicing;
  if(v !== 'Auto') return v;
  if(family === 'Detroit 7ths' || family === 'Dub drift') return '9ths';
  if(family === 'House 4-chord') return '7ths';
  if(family === 'Rave stab') return 'Stacked 4ths';
  if(family === 'One-chord vamp') return 'Sus';
  return 'Triads';
}

function chordTones(parent, deg, voicing){
  const n = parent.length, at = k => parent[(deg + k) % n] + 12 * Math.floor((deg + k) / n);
  switch(voicing){
    case '7ths':         return [at(0), at(2), at(4), at(6)];
    case '9ths':         return [at(0), at(2), at(6), at(8)];      // rootless-ish 9: 1 3 7 9
    case 'Sus':          return [at(0), at(3), at(4)];
    case 'Power':        return [at(0), at(4), at(7)];
    case 'Stacked 4ths': return [at(0), at(3), at(6)];
    default:             return [at(0), at(2), at(4)];
  }
}

function chordName(rootPc, parent, deg, voicing){
  const third = (parent[(deg + 2) % 7] - parent[deg] + 12) % 12;
  const fifth = (parent[(deg + 4) % 7] - parent[deg] + 12) % 12;
  const sev = (parent[(deg + 6) % 7] - parent[deg] + 12) % 12;
  let q = third === 3 ? (fifth === 6 ? 'dim' : 'm') : (fifth === 8 ? 'aug' : '');
  if(voicing === 'Sus') q = 'sus4';
  if(voicing === 'Power') q = '5';
  if(voicing === 'Stacked 4ths'){
    const i4 = (parent[(deg + 3) % 7] - parent[deg] + 12) % 12, i7 = sev;
    q = i4 === 5 ? (i7 === 10 ? '7sus4' : i7 === 11 ? 'maj7sus4' : 'sus4') : (i4 === 6 ? '(#11)' : 'sus');
  }
  if(voicing === '7ths') q = (third === 3 ? 'm' : '') + (sev === 11 ? 'maj7' : '7') + (fifth === 6 ? 'b5' : '');
  if(voicing === '9ths') q = (third === 3 ? 'm' : '') + (sev === 11 ? 'maj9' : '9');
  const name = NOTE_NAMES[(rootPc + parent[deg]) % 12] + q;
  let roman = ROMAN[deg];
  if(third === 3) roman = roman.toLowerCase();
  if(fifth === 6) roman += '°';
  return { name: name, roman: roman };
}

// Score every scale degree against the melody notes of one segment.
function scoreDegrees(parent, notes, rootPc){
  const scores = [];
  for(let d = 0; d < 7; d++){
    const tones = [parent[d], parent[(d+2)%7], parent[(d+4)%7]].map(x => (x + rootPc) % 12);
    const ext = (parent[(d+6)%7] + rootPc) % 12;
    let s = 0;
    for(const n of notes){
      const pc = ((n.pitch % 12) + 12) % 12;
      if(pc === tones[0]) s += 1.25 * n.w;
      else if(tones.indexOf(pc) >= 0) s += 1.0 * n.w;
      else if(pc === ext) s += 0.35 * n.w;
      else s -= 0.75 * n.w;
    }
    scores.push(s);
  }
  // normalise: the melody can break ties and veto clashes, but the family leads
  let tw = 0; for(const n of notes) tw += n.w;
  return scores.map(x => tw > 0 ? x / tw * 2.2 : 0);
}

function harmonyActive(){ return S.params.chordMode === 'On' || (S.params.chordFollow && S.params.chordFollow !== 'Off'); }
function followActive(){ return !!S.params.chordFollow && S.params.chordFollow !== 'Off'; }
function spanCount(){ const n = parseInt(S.params.chordSpan, 10); return n === 2 || n === 4 ? n : 1; }

// The progression for a whole SPAN (one or more cycles). rows are the melody of
// one cycle; for spans > 1 the melody is treated as repeating.
function computeProgression(rows, steps, baseTotal, ticks, span){
  const c = cfg();
  const rootPc = Math.max(0, NOTE_NAMES.indexOf(c.key));
  const parent = parentScale(c.scale);
  const family = familyFor(c.style);
  const voicing = voicingFor(family);
  const total = baseTotal * span;
  const change = S.params.chordChange;
  let seg = steps;
  if(change === '2 per pass') seg = Math.max(1, Math.floor(steps / 2));
  else if(change === '4 per pass') seg = Math.max(1, Math.floor(steps / 4));
  else if(change === 'Per 2 passes') seg = Math.min(total, steps * 2);
  else if(change === 'Per cycle') seg = baseTotal;
  const nSeg = Math.max(1, Math.ceil(total / seg));

  // melody per segment, weighted by length and metric position. When the
  // melody is going to FOLLOW the chords, it only gets a light say in them.
  const follow = followActive();
  const segNotes = Array.from({length: nSeg}, () => []);
  for(let copy = 0; copy < span; copy++)
    for(const r of rows){
      if(r[1] > 1) continue;
      const slot = r[0] + copy * baseTotal, k = Math.min(nSeg - 1, Math.floor(slot / seg));
      const inPass = slot % steps;
      const strong = (slot % seg === 0) ? 2.2 : (inPass % 4 === 0 ? 1.4 : 1.0);
      segNotes[k].push({ pitch: r[2], w: strong * Math.min(4, Math.max(0.5, r[5] / ticks)) });
    }
  const melodyWeight = follow ? 0.35 : 1.0;

  const rnd = rng(S.chordSeed * 7919 + rootPc * 31 + c.style.length);
  const tables = FAMILY_TABLES[family] || [[]];
  const tIdx = Math.floor(rnd() * tables.length);
  const table = tables[tIdx];
  // a second row for the B half of longer progressions (A B A B ...)
  const altTable = tables.length > 1 ? tables[(tIdx + 1 + Math.floor(rnd() * (tables.length - 1))) % tables.length] : table;
  let prior = family === 'Melody-led' ? 0 : (family === 'One-chord vamp' ? 4.0 : 2.6);
  // following the chords needs chords that move, even from "Melody-led"
  let useTable = table;
  let useAlt = altTable;
  if(follow && family === 'Melody-led'){ const ml = FAMILY_TABLES['Minor loop']; const a = Math.floor(rnd() * 4);
    useTable = ml[a]; useAlt = ml[(a + 1) % 4]; prior = 1.6; }
  const ending = String(c.endingBehaviour);
  const resolves = /root|Strong|more resolved|Hard stop/.test(ending);
  const hangs = /hanging|Ambiguous|more tense|Rise|Weak/.test(ending);

  const degrees = [];
  for(let k = 0; k < nSeg; k++){
    const sc = scoreDegrees(parent, segNotes[k], rootPc).map(x => x * melodyWeight);
    if(useTable.length){
      const L = useTable.length, phrase = Math.floor(k / L);
      const row = (phrase % 2 === 1 && nSeg > L) ? useAlt : useTable;
      sc[row[k % row.length]] += prior;
    }
    if(k === 0) sc[0] += 1.2;                                // start at home
    if(k > 0 && family !== 'One-chord vamp' && family !== 'Dub drift') sc[degrees[k-1]] -= 0.6;
    if(k === nSeg - 1 && nSeg > 1){
      if(resolves){ sc[0] += 1.6; }
      if(hangs){ sc[4] += 1.4; sc[6] += 1.0; sc[0] -= 0.8; }
      if(/5th/.test(ending)) sc[4] += 1.6;
      if(/Fall/.test(ending)) { sc[3] += 1.0; sc[5] += 0.8; }
    }
    if(k === nSeg - 2 && nSeg > 2 && resolves){ sc[4] += 0.8; sc[3] += 0.5; }
    const dimPenalty = d => ((parent[(d+4)%7] - parent[d] + 12) % 12 === 6) ? 3.0 : 0;
    let best = 0, bestScore = -1e9;
    for(let d = 0; d < 7; d++){
      const v = sc[d] - dimPenalty(d) + rnd() * 0.05;
      if(v > bestScore){ bestScore = v; best = d; }
    }
    degrees.push(best);
  }

  // voice-led voicings around CHORD OCT
  const base = 12 * (Number(S.params.chordOctave) + 1) + rootPc;
  let prev = null;
  const voiced = degrees.map(function(d){
    const tones = chordTones(parent, d, voicing).map(x => base + x);
    let bestV = tones, bestCost = 1e9;
    for(let inv = 0; inv < tones.length; inv++){
      for(const shift of [-12, 0, 12]){
        const v = tones.map((t, i) => t + shift + (i < inv ? 12 : 0)).sort((a,b) => a-b);
        if(v[0] < base - 7 || v[v.length-1] > base + 21) continue;
        const centre = v.reduce((a,b) => a+b, 0) / v.length;
        let cost = Math.abs(centre - (base + 7)) * 0.15;
        if(prev) for(let i = 0; i < Math.min(v.length, prev.length); i++) cost += Math.abs(v[i] - prev[i]);
        if(cost < bestCost){ bestCost = cost; bestV = v; }
      }
    }
    prev = bestV;
    return bestV;
  });
  const chords = degrees.map(function(d, k){
    const nm = chordName(rootPc, parent, d, voicing);
    return { start: k * seg, len: Math.min(seg, total - k * seg), degree: d, name: nm.name,
             roman: nm.roman, pitches: voiced[k], rootPc: (rootPc + parent[d]) % 12 };
  });
  return { chords: chords, family: family, voicing: voicing, seg: seg, total: total };
}

// The chords that fall inside [w0, w1), shifted to start at 0.
function windowChords(chords, w0, w1){
  const out = [];
  for(let k = 0; k < chords.length; k++){
    const ch = chords[k], a = Math.max(w0, ch.start), b = Math.min(w1, ch.start + ch.len);
    if(b <= a) continue;
    out.push(Object.assign({}, ch, { start: a - w0, len: b - a, next: chords[(k + 1) % chords.length] }));
  }
  return out;
}

function chordAt(chords, slot){
  for(const ch of chords) if(slot >= ch.start && slot < ch.start + ch.len) return ch;
  return chords.length ? chords[chords.length - 1] : null;
}

// ---- melody follows the progression -----------------------------------------
// ROOT MOVE  transposes each passage diatonically (in the phrase's own scale) so
//            its tonic lands on the chord root - the way a bassline or riff is
//            moved through changes. The nearer direction wins, so it stays put.
// CHORD TONES keeps the phrase but lands its strong notes (beats, long notes,
//            the first note under each chord, the last note of each pass) on a
//            tone of the chord.
// ARPEGGIATE  every note becomes its nearest chord tone.
function followMelody(rows, chords, steps, ticks){
  const mode = S.params.chordFollow;
  if(!mode || mode === 'Off' || !chords.length) return rows;
  const c = cfg();
  const rootPc = Math.max(0, NOTE_NAMES.indexOf(c.key));
  const lanesSel = S.params.followLanes;
  const takes = lane => lane < 2 && (lanesSel === 'Both' || (lanesSel === 'Call' ? lane === 0 : lane === 1));
  const pcs = Array.from(new Set(c.scale.map(iv => (((rootPc + iv) % 12) + 12) % 12))).sort((a, b) => a - b);
  const n = Math.max(1, pcs.length);
  const toIdx = function(p){
    const pc = ((p % 12) + 12) % 12; let oct = Math.floor(p / 12), j = -1;
    for(let q = 0; q < n; q++) if(pcs[q] <= pc) j = q;
    let off;
    if(j < 0){ j = n - 1; oct -= 1; off = pc + 12 - pcs[j]; } else off = pc - pcs[j];
    return { idx: oct * n + j, off: off };
  };
  const fromIdx = function(idx, off){ const oct = Math.floor(idx / n), j = idx - oct * n; return oct * 12 + pcs[j] + off; };
  const tonicPos = Math.max(0, pcs.indexOf(rootPc));
  const transposeSteps = function(chordRootPc){
    // scale step nearest to the chord root
    let best = 0, bestD = 99;
    for(let q = 0; q < n; q++){ const d = Math.min((pcs[q] - chordRootPc + 12) % 12, (chordRootPc - pcs[q] + 12) % 12); if(d < bestD){ bestD = d; best = q; } }
    let k = best - tonicPos; if(k < 0) k += n;
    const ref = 5 * n + tonicPos, base = fromIdx(ref, 0);
    const up = fromIdx(ref + k, 0) - base, down = fromIdx(ref + k - n, 0) - base;
    return Math.abs(down) <= Math.abs(up) ? k - n : k;
  };
  const snap = function(p, ch){
    const tones = Array.from(new Set(ch.pitches.map(x => ((x % 12) + 12) % 12)));
    let best = p, bestCost = 1e9;
    for(let d = -6; d <= 6; d++){
      const q = p + d, pc = ((q % 12) + 12) % 12;
      if(tones.indexOf(pc) < 0) continue;
      const cost = Math.abs(d) + (pcs.indexOf(pc) < 0 ? 0.4 : 0) + (d > 0 ? 0.1 : 0);
      if(cost < bestCost){ bestCost = cost; best = q; }
    }
    return best;
  };
  const doMove = mode === 'Root move' || mode === 'Root + tones';
  const doTones = mode === 'Chord tones' || mode === 'Root + tones' || mode === 'Arpeggiate';
  const all = mode === 'Arpeggiate';

  // which notes are strong: beats, long notes, first under a chord, last of a pass
  const order = rows.map((r, i) => i).filter(i => takes(rows[i][1]))
                    .sort((a, b) => (rows[a][0] - rows[b][0]) || (rows[a][1] - rows[b][1]));
  const strong = new Set();
  const seenChord = {};
  const lastInPass = {};
  for(const i of order){
    const r = rows[i], ch = chordAt(chords, r[0]);
    const tick = (r[0] % steps) * ticks;
    if(tick % 480 === 0 || r[5] >= 2 * ticks - 1) strong.add(i);
    const key = r[1] + ':' + (ch ? ch.start : 0);
    if(!seenChord[key]){ seenChord[key] = 1; strong.add(i); }
    lastInPass[r[1] + ':' + Math.floor(r[0] / steps)] = i;
  }
  for(const k of Object.keys(lastInPass)) strong.add(lastInPass[k]);

  return rows.map(function(r, i){
    if(!takes(r[1])) return r;
    const ch = chordAt(chords, r[0]);
    if(!ch) return r;
    let p = r[2];
    if(doMove){ const t = toIdx(p); p = fromIdx(t.idx + transposeSteps(ch.rootPc), t.off); }
    if(doTones && (all || strong.has(i))) p = snap(p, ch);
    const out = r.slice(); out[2] = clamp(p, 0, 127);
    return out;
  });
}

// ---- chord lane rhythm -------------------------------------------------------
function chordRows(chords, rows, total, ticks){
  if(S.params.chordMode !== 'On' || S.chordMute || !chords.length) return [];
  const out = [];
  const chan = Math.max(0, DATA.channels.indexOf(S.params.chordChannel)) + 1;
  const vel = clamp(Math.round(Number(S.params.chordVelocity) || 72), 1, 127);
  const rhythm = S.params.chordRhythm;
  const beat = 480;
  const push = function(t, dur, pitches, v){
    const slot = Math.floor(t / ticks + 1e-6);
    if(slot >= total || slot < 0) return;
    for(const p of pitches) out.push([slot, 2, clamp(p, 0, 127), v, chan, Math.max(1, dur), t - slot * ticks]);
  };
  chords.forEach(function(ch, k){
    const t0 = ch.start * ticks, t1 = Math.min(total * ticks, (ch.start + ch.len) * ticks), pitches = ch.pitches;
    if(rhythm === 'Hold') push(t0, (t1 - t0) * 0.97, pitches, vel);
    else if(rhythm === 'Stab per change') push(t0, Math.min(t1 - t0, ticks * 1.5), pitches, vel);
    else if(rhythm === 'Anticipated'){
      const lead = Math.min(ticks, (t1 - t0) / 2);
      if(k === 0) push(t0, t1 - t0 - lead, pitches, vel);
      const nx = k + 1 < chords.length ? chords[k + 1] : null;
      if(nx) push(t1 - lead, lead + (Math.min(total * ticks, (nx.start + nx.len) * ticks) - t1) * 0.9, nx.pitches, Math.min(127, vel + 10));
    }
    else if(rhythm === 'Offbeat stabs' || rhythm === '8th pulse'){
      const stepT = rhythm === '8th pulse' ? beat / 2 : beat;
      const off = rhythm === '8th pulse' ? 0 : beat / 2;
      for(let t = Math.ceil((t0 - off) / stepT) * stepT + off; t < t1; t += stepT)
        push(t, stepT * (rhythm === '8th pulse' ? 0.55 : 0.4), pitches, t === t0 ? Math.min(127, vel + 12) : vel);
    }
    else {
      const lane = rhythm === 'Stabs on call' ? 0 : 1;
      let any = false;
      for(const r of rows){
        if(r[1] !== lane) continue;
        const t = r[0] * ticks + r[6];
        if(t < t0 || t >= t1) continue;
        push(t, Math.min(r[5], ticks * 2), pitches, r[3] > vel ? Math.min(127, vel + 12) : vel);
        any = true;
      }
      if(!any) push(t0, ticks, pitches, vel);
    }
  });
  return out;
}

// Harmony is applied to every compiled record, inside the record's own snapshot.
// A static pattern is repeated SPAN times so the progression has room to move;
// an evolving one keeps one cycle per record and walks through the progression
// with the cycle counter.
const __crgNativeCompile = nativeCompile;
nativeCompile = function(snap){
  const rec = __crgNativeCompile(snap);
  rec.baseTotal = rec.total;
  nativePrivate(snap, function(){
    if(!harmonyActive()){ rec.chords = []; rec.family = ''; rec.voicing = ''; return; }
    const span = spanCount();
    const prog = computeProgression(rec.rows, rec.steps, rec.baseTotal, rec.ticks, span);
    let rows = rec.rows, chords;
    if(span > 1 && !rec.evolves){
      rows = [];
      for(let copy = 0; copy < span; copy++)
        for(const r of rec.rows){ const x = r.slice(); x[0] += copy * rec.baseTotal; rows.push(x); }
      rec.total = rec.baseTotal * span;
      chords = windowChords(prog.chords, 0, rec.total);
    } else {
      const w = ((snap.cycle || 0) % span) * rec.baseTotal;
      chords = windowChords(prog.chords, w, w + rec.baseTotal);
    }
    rows = followMelody(rows, chords, rec.steps, rec.ticks);
    rec.chords = chords; rec.family = prog.family; rec.voicing = prog.voicing;
    rec.rows = rows.concat(chordRows(chords, rows, rec.total, rec.ticks));
  });
  return rec;
};

// Pulses from a record longer than the form map back into the form, so the
// lanes and the playhead keep working while the progression walks on.
let crgCycleSlot = -1;
const __crgNativePulse = nativepulse;
nativepulse = function(slot, revision){
  const rec = N.records[Number(revision)];
  crgCycleSlot = Number(slot);
  if(rec && rec.baseTotal && rec.total > rec.baseTotal) slot = Number(slot) % rec.baseTotal;
  return __crgNativePulse(slot, revision);
};

const __crgNativeSignature = nativeSignature;
nativeSignature = function(){
  return __crgNativeSignature() + JSON.stringify([S.custom, S.chordSeed, S.chordMute]);
};

nativePublish = function(record, when, kind){
  const rev = ++N.revision; record.rev = rev; record.kind = kind || ''; record.when = when;
  N.records[rev] = record; N.pending = rev;
  for(const row of record.rows) outlet(6, ['data', rev * CRG_SLOT_KEY + row[0]].concat(row.slice(1)));
  outlet(6, 'ready', rev, record.total, record.steps, record.ticks, Math.max(0, record.sync), when);
  crgLastRecord = record;
  crgFormInfo();
  return rev;
};
let crgLastRecord = null;

function crgChordsNew(){ S.chordSeed = (S.chordSeed % 9973) + 1 + Math.floor(Math.random() * 50); nativeRefresh(); rememberState(); }
function crgChordMute(v){ S.chordMute = v ? 1 : 0; nativeRefresh(); outlet(O_UI, 'chordmute', S.chordMute); rememberState(); }

// Everything the FORM page draws, sent whenever a new pattern is published.
function crgFormInfo(){
  const rec = crgLastRecord;
  const c = cfg();
  const plan = passPlan(c.phraseMode);
  const steps = c.steps;
  let passes;
  if(plan.interleave !== undefined){
    const lanes = [];
    for(let i = 0; i < steps; i++) lanes.push(laneForStep(plan, 0, i, steps) === C ? 'C' : 'R');
    passes = [{ lane: 'X', v: '', cells: lanes.join('') }];
  } else {
    passes = (plan.seq || []).map(function(l, i){
      return { lane: l === C ? 'C' : 'R',
               v: plan.form ? plan.form[i] : (plan.vary && plan.vary[i] ? '~' : 'A') };
    });
  }
  const notes = [];
  if(rec) for(const r of rec.rows) notes.push([r[0], r[1], r[2], Math.round(r[5] / rec.ticks * 100) / 100, r[3]]);
  const info = {
    steps: steps, total: rec ? rec.total : steps, passes: passes, custom: S.custom,
    isCustom: c.phraseMode === 'Custom', notes: notes,
    chords: rec && rec.chords ? rec.chords : [], family: rec ? rec.family : '', voicing: rec ? rec.voicing : '',
    chordMute: S.chordMute, follow: followActive(), followMode: S.params.chordFollow || 'Off',
    followLanes: S.params.followLanes || 'Both', chordOut: S.params.chordMode === 'On',
    baseTotal: rec && rec.baseTotal ? rec.baseTotal : steps,
    call: S.call.map(x => x.active ? x.degree + 7 * (x.oct || 0) : null),
    response: S.response.map(x => x.active ? x.degree + 7 * (x.oct || 0) : null),
    describe: {
      style: crgItemTip('style', S.params.style), motifType: crgItemTip('motifType', S.params.motifType),
      responseStrategy: crgItemTip('responseStrategy', S.params.responseStrategy),
      phraseMode: c.phraseMode === 'Custom' ? 'Your own form: click a block to swap call and response, its letter to choose a variant.'
                                           : crgItemTip('phraseMode', S.params.phraseMode),
      endingBehaviour: crgItemTip('endingBehaviour', S.params.endingBehaviour) },
    category: styleCategory(S.params.style)
  };
  outlet(O_UI, 'forminfo', JSON.stringify(info));
}

// Persist the extension state alongside the engine's own blob.
const __crgStateValue = stateValue;
stateValue = function(){
  return __crgStateValue() + '|CRGX:' + encodeURIComponent(JSON.stringify({
    custom: S.custom, chordSeed: S.chordSeed, chordMute: S.chordMute }));
};
const __crgApplyState = applyState;
applyState = function(value){
  value = String(value);
  const cut = value.indexOf('|CRGX:');
  if(cut >= 0){
    try {
      const x = JSON.parse(decodeURIComponent(value.slice(cut + 6)));
      S.custom = x.custom || null; S.chordSeed = x.chordSeed || 1; S.chordMute = x.chordMute ? 1 : 0;
    } catch(e){ post('CRGX restore failed: ' + e.message); }
    value = value.slice(0, cut);
  }
  __crgApplyState(value);
};

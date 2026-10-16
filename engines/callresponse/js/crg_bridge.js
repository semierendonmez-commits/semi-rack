// crg_bridge.js — plugin-side glue, loaded AFTER crg_engine.js.
//
// The engine is used exactly as shipped in the Max for Live device. Its
// "native playback" path (outlet 6) is what drives the plugin: the engine
// compiles each phrase cycle into note rows, and the C++ sequencer plays them
// sample-accurately and acknowledges with nativecommit / nativepulse, the same
// handshake the Max patch used. Only Live-specific features are replaced here.

// The plugin has no Companion buses: both lanes always leave on the track's
// MIDI output, separated by MIDI channel (CALL CH / RESP CH).
routeForLane = function(lane){ return 0; };
isOutputParam = function(name){ return false; };

// OPTIONS page: channels instead of the Live-only Companion switches.
PAGES.options = ['callChannel', 'responseChannel', 'syncMode', 'mutationAmount'];
PAGES.optionsMore = ['newPairTiming', 'clipTarget', 'clipLength'];

// Scale follow reads Live 12's song scale through the Live API; plugins have no
// equivalent, so the switch is not offered.
livescale = function(v){
  S.followLive = 0;
  status('Scale follow needs Ableton Live; using KEY and SCALE.');
};

// WRITE CLIP -> a MIDI file the plugin can drag into any DAW or save to disk.
// Same note list, same length rules as the device's clip writer.
writeClip = function(){
  ensureInit();
  const c = cfg();
  const plan = passPlan(c.phraseMode);
  const passes = plan.interleave !== undefined ? 1 : (plan.seq ? plan.seq.length : 2);
  const stepBeats = RATE_BEATS[S.params.rate] || 0.25;
  let notes;
  const target = S.params.clipTarget;
  if(followActive() || (harmonyActive() && spanCount() > 1)){
    // the playing lines change with the chords: write what actually plays,
    // over the whole progression
    const rec = nativeCompile(nativeSnapshot());
    const want = target === 'Call' ? [0] : target === 'Response' ? [1] : target === 'Chords' ? [2]
               : target === 'All + chords' ? [0, 1, 2] : [0, 1];
    notes = [];
    for(const r of rec.rows) if(want.indexOf(r[1]) >= 0)
      notes.push({ pitch: r[2], vel: r[3], chan: r[4] - 1, start: (r[0] * rec.ticks + r[6]) / 480, dur: r[5] / 480 });
    finishClip(c, target, notes, rec.total * rec.ticks / 480);
    return;
  }
  if(target === 'Call')          notes = renderNotes(c, {seq:[C]}, 1);
  else if(target === 'Response') notes = renderNotes(c, {seq:[R]}, 1);
  else if(target === 'Layered')  notes = renderNotes(c, {seq:[C]}, 1).concat(renderNotes(c, {seq:[R]}, 1));
  else if(target === 'Chords')   notes = [];
  else                           notes = renderNotes(c, plan, passes);
  if(target === 'Chords' || target === 'All + chords'){
    // chords come from the compiled cycle, so they match what is playing
    const rec = nativeCompile(nativeSnapshot());
    for(const r of rec.rows) if(r[1] === 2)
      notes.push({ pitch: r[2], vel: r[3], chan: r[4] - 1, start: (r[0] * rec.ticks + r[6]) / 480, dur: r[5] / 480 });
  }
  const totalSteps = (target === 'Call' || target === 'Response' || target === 'Layered')
    ? c.steps : c.steps * passes;
  finishClip(c, target, notes, totalSteps * stepBeats);
};

function finishClip(c, target, notes, length){
  const num = Math.max(1, Number(__host.signature_numerator) || 4);
  const den = Math.max(1, Number(__host.signature_denominator) || 4);
  const barBeats = num * 4 / den;
  if(S.params.clipLength === 'Whole bars') length = Math.max(barBeats, Math.ceil((length - 1e-9) / barBeats) * barBeats);
  const name = 'C&R ' + target + ' - ' + c.key + ' ' + c.scaleName;
  outlet(7, 'clip', JSON.stringify({ name: name, target: target, length: length,
    tempo: currentTempo(), num: num, den: den, notes: notes }));
  status('MIDI ready: ' + name + ' (' + (Math.round(length * 100) / 100) + ' beats)');
}

DATA.clipTargets.push('Chords', 'All + chords');

// --- host API --------------------------------------------------------------

function crgBoot(){
  init();
  loadready();
  sendMenuItems();
  pushDisplay();
}

function crgSpec(){
  return JSON.stringify({ version: CRG_VERSION, data: DATA, enums: ENUM_PARAMS,
    nums: NUM_PARAMS, ranges: NUM_RANGES, defaults: DEFAULTS, pages: PAGES,
    labels: CTRL_LABELS, rerollable: REROLLABLE, diceSkip: DICE_SKIP });
}

function crgDrain(){
  const o = __out; __out = [];
  return JSON.stringify(o);
}

function crgLog(){ const l = __log; __log = []; return l.join('\n'); }

function crgGetState(){ return stateValue(); }
function crgSetState(v){ setvalueof(String(v || '')); }

function crgSetHost(tempo, num, den){
  if(tempo > 0) __host.tempo = tempo;
  if(num > 0) __host.signature_numerator = num;
  if(den > 0) __host.signature_denominator = den;
}

// Footer help for a hovered control, with the meaning of its current value.
function crgTip(id){
  let t = '';
  if(typeof TIPS === 'object'){
    if(id === 'btn:newboth' && S.queuedPair){
      t = S.queuedAction === 'dice'
        ? 'Dice randomization queued for the next phrase. Click New Pair to cancel it.'
        : 'New pair queued for the next phrase. Click again to cancel.';
    } else if(TIPS[id]) t = TIPS[id];
    else {
      const p = String(id).split(':');
      if(p.length > 1 && TIPS[p[1]]) t = TIPS[p[1]];
      else if(p[0] === 'lock') t = 'Lock this so the dice leaves it alone.';
    }
  }
  const name = String(id).indexOf(':') >= 0 ? String(id).split(':').pop() : String(id);
  if(ENUM_PARAMS[name]){
    const d = describeItem(name, S.params[name]);
    if(d) t = (t ? t + '  —  ' : '') + S.params[name] + ': ' + d;
  }
  if(name === 'callChannel')     t = 'MIDI channel the call lane plays on.';
  if(name === 'responseChannel') t = 'MIDI channel the response lane plays on. Give the two lanes different channels to split them onto two instruments.';
  if(name === 'clipTarget')      t = 'What DRAG / SAVE MIDI writes: call, response, the full sequence, both stacked, the chords, or everything.';
  const H = {
    chordFamily: 'Progression family. Auto picks one from the style; Melody-led follows the notes only.',
    chordRhythm: 'How the chords are played: held, stabbed, on the call or response rhythm, offbeat house stabs, 8th pulses, or anticipated changes.',
    chordChange: 'Harmonic rhythm: how often the chord changes.',
    chordVoicing: 'Chord shape. Auto chooses from the progression family.',
    chordOctave: 'Register of the chords.', chordChannel: 'MIDI channel the chords play on.',
    chordVelocity: 'Chord velocity.',
    chordFollow: 'Make call and response move with the progression. Root move transposes each passage to the chord; Chord tones lands strong notes on the chord; Root + tones does both; Arpeggiate turns every note into a chord tone.',
    followLanes: 'Which lanes follow the chords. Keep the call fixed and let only the response move, or the other way round.',
    chordSpan: 'How many times the form plays before the progression repeats. Longer spans give the chords room to travel.',
    chordMode: 'Plays the chords themselves on their own MIDI channel. FOLLOW works with it on or off.' };
  if(H[name]) t = H[name];
  if(name === 'clipLength')      t = 'Exact phrase keeps the sequence length. Whole bars pads to complete bars using the host time signature.';
  return t;
}

function crgItemTip(param, value){
  try { return describeItem(param, value) || ''; } catch(e){ return ''; }
}

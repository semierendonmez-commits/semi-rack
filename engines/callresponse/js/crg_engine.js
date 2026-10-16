// crg.js — Call / Response Generator for Max for Live
// Port of Nordl3/Call-Response-Generator (MIT) to the Max 9 [v8] object.
//
// The generation engine below is a faithful port of index.html. Sections marked
// RECONSTRUCTED were not visible in the source I read and are my own
// implementation of the documented behaviour.
//
// Outlets:
//   0  raw MIDI bytes               -> [midiout]
//   1  transport control            -> [metro]
//   2  setparam <name> <index>      -> [route setparam] -> UI
//   3  status symbol                -> [prepend set] -> [live.comment]
//   4  display data                  -> [v8ui crg_display.js]

// ---------------------------------------------------------------------------
// VERSION
//   0.93.1 direct Track / Companion switches in the Options interface.
//   0.93.0 simplified routing: each lane chooses Track or Companion. Companion
//          automatically uses the matching Call / Response receiver position.
//   0.92.0 routing test: independent Track / Bus 1-8 outputs for call and
//          response, with saved Live parameters and a companion receiver.
//          Note-offs retain their original destination; stop, bypass, and
//          routing changes also cancel delayed swing note-ons.
//   0.91.0 the scale read builds its own live_set object instead of borrowing
//          the cached one. That object is created during an early probe, when
//          Live's API is only partly up - fine for tempo, which is why the
//          transport worked while the scale came back empty every time. Also
//          reports the raw values on failure rather than only that it failed.
//   0.90.0 Scale Mode no longer reports a scale it has not read. liveRoot starts
//          at -1, and -1 mod 12 is 11, so a failed read displayed as the key of
//          B - a plausible-looking answer that was really 'nothing came back'.
//          It now says so, keeps trying, and tells you in the footer.
//   0.89.0 quieter console. A successful load printed two lines every time the
//          device was opened; nothing was wrong, so nothing needed saying.
//          Failures still report, and 'verbose 1' brings the detail back.
//   0.88.0 'theme dark|light' and 'themecolor <name> <r> <g> <b>' are forwarded
//          to the display, so the patch can follow Live's own appearance.
//   0.87.3 default SLIDE is Rare
//   0.87.2 default JOIN is Mixed ties
//   0.87.1 defaults: Medium register, Wide mix gate, Moderate accents
//   0.87.0 REGISTER is measured in octaves, not in a fixed number of scale
//          degrees. Six degrees is well over an octave in a five-note scale, so
//          'Narrow' spanned eleven semitones and its centre wandered nineteen
//          between one NEW PAIR and the next. Narrow now stays inside an octave
//          whatever the scale, and the starting octave no longer drifts.
//   0.86.0 'Connected runs' now makes runs. It tied 27% of notes against Tied
//          bias's 29%, in the same scattered way, so the two were the same
//          setting under different names. It now groups its ties into chains of
//          two or three, which is what a run is.
//   0.85.0 'offbeat' now means the third sixteenth - the 'and' of the beat -
//          rather than every odd step. And the tie/slide rule is applied
//          wherever a phrase changes, not only when one is generated: changing
//          the JOIN menu re-rolled the ties and nothing put the rule back.
//   0.84.0 slides marked on same-pitch pairs are gone. The rule was applied
//          during articulation, but enforceTies and the octave jump both change
//          pitches afterwards - so a third to a half of all slide marks ended
//          up gliding a note to itself. Ties and slides are now settled in one
//          pass, after everything else, comparing the pitch that actually plays.
//   0.83.0 a tie now only exists between notes of the SAME pitch, which is what
//          a tie is - the slide rule's mirror image. A tie into a different
//          pitch just overlapped two unrelated notes, which is neither a tie
//          nor a slide. Playback also builds each pass as a note list now,
//          the same way the clip writer does, so the two cannot disagree.
//   0.83.0 ties are resolved before the note is sent, not after. Playback used
//          to send each step and then try to hold the previous note - which
//          cannot work under swing, because the note being joined is still
//          sitting in its delay when the next step arrives. A note now leaves
//          with the full length of everything tied to it, and the tied steps
//          send nothing. Same rule the clip writer uses, so the two agree.
//   0.82.0 ties now hold during playback, not just in a written clip. The merge
//          added in 0.81.0 lived in renderNotes, which only the clip writer
//          uses - live playback goes through bang() and was still retriggering
//          the second note. Same tie, two different code paths, one of them
//          fixed. Playback now extends the sounding note instead.
//   0.81.0 SLIDE and JOIN were producing nearly the same thing - both just
//          overlapped the next note, and both landed on the same pitch about a
//          third of the time. They now do what they are named for. A slide
//          always glides into a DIFFERENT pitch. A tie HOLDS: if the next step
//          is the same pitch it becomes one longer note instead of two, which
//          is what a tie does on a 303.
//   0.80.0 DENSITY is reversible. Turning a step off threw its pitch away, so
//          turning the knob back up brought the note back copying a neighbour -
//          sweep down and back and the phrase had quietly changed. A silenced
//          step now remembers what it was and gets it back.
//   0.79.0 reroll buttons on the four articulation menus - re-roll gate, join,
//          slide or accent on its own without rebuilding the phrase. And
//          'Offbeat accents' now accents every offbeat, as the name says; the
//          half-of-them version it used to be is now 'Syncopated accents'.
//   0.78.0 a style that says it sits on the offbeat now does. offbeatBias only
//          nudged accents and gate; which steps sounded was downbeat-heavy for
//          every category, so Tech House claimed an offbeat feel and played on
//          the beat. It now shifts the activation weights themselves.
//          Also: the display reserves octave-jump headroom permanently, so
//          turning OCT JUMP no longer rescales the lanes.
//   0.77.0 JOIN joins with ties, not slides. It was adding slides on top of the
//          SLIDE menu, which swamped it - every setting from Rare to Acid heavy
//          came out at the same 21%. The two menus are now independent: JOIN
//          decides how much notes run together, SLIDE decides how many glide.
//   0.76.1 a slide into a note of the same pitch now moves that note instead of
//          being thrown away. On a repetitive phrase nearly every slide landed
//          on a repeated pitch and was dropped, so the SLIDE menu could be set
//          to anything from Rare to Acid heavy and still give one slide.
//   0.76.0 each articulation menu now changes only its own thing. All four
//          called one routine that re-rolled gate, accent, slide and tie
//          together, so touching JOIN moved the accents and touching ACCENT
//          moved the note lengths. A menu now rewrites only the fields it owns.
//   0.75.0 STYLE stops competing with the menus. Measured with every menu held
//          fixed, changing only the style moved accents from 20% to 54% and
//          gate from 0.54 to 0.80 - so a style was quietly overriding the very
//          controls sitting next to it. DENSITY, REPETITION, SLIDE and ACCENT
//          are now owned by their menus outright. A style sets what no menu
//          covers: how far the line leaps, how root-anchored it is, how much
//          it rests, how tight the answer is, and the groove of its category.
//   0.74.1 defaults: contour Slight rise, answer Higher answer
//   0.74.0 'Call dominant / response tonic' now does what it says: the call
//          centres on the fifth of the scale and the response on the root. It
//          used to leave the call alone entirely and put the response's first
//          half on tense degrees - so in A minor the answer never landed on A.
//          Wide register genuinely spans two octaves. Contour presets have
//          their own descriptions.
//   0.73.0 CONTOUR fixed. Flat now really is flat - one pitch, until OCT JUMP
//          says otherwise - where before the root-bias jump, the per-note leap
//          roll and a fresh centre for every cell all moved it about. And rise
//          and fall now shape the whole phrase, not just the cell: every preset
//          used to trend downward by about the same amount, so 'Slight rise'
//          and 'Slight fall' were indistinguishable in the bar.
//   0.72.1 mutation no longer fights DENSITY. Most of what mutate does is turn
//          steps on and off, so at 100% the phrase dropped from 16 notes to 12
//          or 13 the moment anything mutated - the dial said full and the bar
//          had holes in it. Mutation now restores the note count afterwards,
//          changing WHICH steps sound rather than how many.
//   0.72.0 new defaults - the device now starts on the acid settings rather
//          than needing 'preset acid303' to get there
//   0.71.0 more gate options, reaching much shorter. Gate is a fraction of a
//          STEP, so the same setting is a different note value at each rate:
//          at 1/16, Stab is a 1/128 and Short is Sting's 1/64. Two new mixes,
//          one weighted short and one spanning the whole range.
//   0.70.0 DENSITY at 100% now fills every step - it topped out at 14 of 16.
//          The ANSWER menu rebuilds only the response, not the call. And a
//          lane only mutates on the pass before it plays, so a fill that comes
//          round every fourth bar changes once per fill rather than four times.
//   0.69.0 held notes now come in runs. Sting's long notes cluster - steps
//          1-2-3, then 11-12 - with none at all on half the steps, while ours
//          were rolled per step and came out evenly scattered at about a
//          quarter everywhere. Same overall count, grouped instead of sprinkled,
//          which is what makes a line sound phrased rather than random.
//   0.68.0 an arrangement loop no longer resets the pass counter. Looping one
//          bar sent passIdx back to 0 every time round, so Call, call, response
//          and Call with response fill never reached the response - the loop
//          restarted the sequence before it could advance. The phrase restarts
//          at step 1 as before; the pass carries on.
//          Also: Alternate half bars now has a response-first partner.
//   0.67.0 measured the eight Sting clips properly instead of assuming: 80% of
//          notes sit on one pitch class, 46% are accented, and octave leaps go
//          down as often as up. A 303 line is a drone with octave drops, not a
//          melody - I had been steering away from that. OCT JUMP now moves in
//          both directions, and 'same rhythm' answers really do copy the
//          rhythm, note lengths included.
//   0.66.0 lengths retuned against eight Sting clips: they use three values -
//          a quarter step, a half step, and about a whole one - with nothing in
//          between. Ours smeared across the middle because a tie multiplied the
//          gate instead of holding the note to the next step. A shorter stab
//          added at 0.15 for range. NOTE LENGTH off the panel, replaced by a
//          MUTATE knob that drives both lanes.
//   0.65.0 note lengths rebuilt against real 303 output. Four Sting 2 clips
//          measured: lengths cluster at 0.25 and 0.54 of a step with about a
//          fifth reaching a full step, and accent barely affects length at all
//          (0.60 against 0.52). Ours ran 1.05 to 1.45 - every note bled into
//          the next, which is why turning the release down did nothing.
//          Connection now decides how often notes JOIN rather than lengthening
//          everything, and a note that is not slid or tied can no longer reach
//          the following step.
//   0.64.0 Mixed gate actually mixes - it stepped 0.55 to 1.15 in even fifths,
//          which is a wobble rather than a mix. Default velocity 64. A slide
//          into a note of the same pitch does nothing audible, so those are
//          dropped rather than drawn and written as glides that never happen.
//   0.63.0 ACCENT AMT, VELOCITY and NOTE LENGTH no longer disturb the phrase.
//          They were grouped with the articulation menus, so changing one
//          re-rolled which steps are accented and slid - but all three only
//          scale a value at render time and have no business touching the notes.
//   0.62.0 an external position is now checked before it is trusted. The wiring
//          I suggested fed bars rather than ticks, so the value barely moved and
//          the phrase sat on step 1 - the engine believed it over its own
//          count. It now watches the incoming values and ignores them unless
//          they advance at roughly one step per bang, falling back to counting
//          and saying so in the console.
//          VARIATION is no longer lockable, and base velocity is adjustable.
//   0.61.0 PROFILE is a preset rather than an override. It used to clamp the
//          gate, join, slide and accent menus behind their backs - under
//          'Stabbed' the GATE menu did almost nothing - which is what made it
//          feel redundant. Choosing a profile now simply sets those four menus,
//          visibly, and then stays out of the way. VARIATION moved to Options.
//   0.60.0 the articulation menus take effect immediately. Gate, join, slide,
//          accent, profile and ending were not in the rebuild list, so they sat
//          there doing nothing until the next NEW PAIR. They now re-apply to
//          the phrase you already have, which is the right behaviour anyway -
//          they shape the notes rather than choosing them.
//   0.59.0 accepts a position from Max's own transport, which is lag-free.
//          current_song_time is read on a deferred thread and arrives a bang
//          stale, so at a loop point it still reports the old position - no
//          amount of work in here can fix a reading that is wrong exactly when
//          it matters. Sting 2 uses plugsync~ and transport for this reason.
//          Send 'posticks <n>' or 'pos <beats>' just before each bang and the
//          position becomes exact; without it the old counting is used.
//   0.58.0 arrangement//   0.58.0 arrangement loops are handled by counting, not by watching the clock.
//          current_song_time is read on a deferred thread and can arrive a bang
//          stale, so at the loop point it still reported the old position: the
//          jump was noticed one bang late and the phrase stayed a step behind
//          for good. Live reports the loop length, and the metro is locked to
//          the grid, so the wrap can be counted exactly instead - no reading
//          involved, and no lag to be wrong about.
//   0.57.0 position is now a phase-locked estimate of the song clock instead of
//          a raw bang count. Counting is exact but can never recover: a dropped
//          bang or an arrangement loop left the phrase permanently out, which is
//          what "starts on step 2" and "jumps steps" were. The estimate advances
//          one step per bang and is nudged gently toward the actual reading, so
//          jitter averages out while real moves are followed.
//   0.57.0 the position self-corrects. Counting bangs is exact until one is
//          missed or the playhead moves, and the count could never recover -
//          hence starting on step 2, or skipping steps, after a loop. The count
//          is still the timing source, but it is compared against the song
//          clock each bang and snapped back when they disagree by more than a
//          step. Smaller disagreements are ignored, so a stale reading cannot
//          drag the phrase about the way deriving the step from it used to.
//   0.56.0 arrangement loops. Counting bangs locally is exact, but a loop jump
//          does not stop the transport, so nothing told the phrase to go back -
//          it kept counting through the jump and only lined up again when the
//          loop happened to be a whole number of phrases long. The song clock
//          is now read once per bang purely to spot a jump backwards; the step
//          is still counted, so the old timing jitter cannot return.
//   0.55.1 a step could reach the display with a null pitch, which drew a note
//          block at a NaN coordinate - the stray line running off the top of
//          the device. Blocked at both ends: the engine only ever sends a
//          number, and the display only draws a step whose pitch really is one.
//   0.55.0 ACCENT AMT on the Articulation page: how much louder an accented
//          note is. Replaces the hidden accentVelocity, which was a fixed
//          number nobody could reach.
//   0.54.0 preset slots removed. Live's own device presets cover the settings,
//          and the slot system needed patch wiring to persist at all.
//   0.53.0 no Live API access at all during the load window. The API is never
//          ready in the first moments after a device is built, so probing then
//          only produces warnings. Nothing is attempted for 600ms unless
//          live.thisdevice says otherwise, and song() reuses the probe's own
//          object instead of building a second one.
//   0.52.0 the last three console warnings traced to the Live scale observers.
//          Scale Mode reports its stored value at load, which built three
//          LiveAPI objects before Live was ready - one warning each. They now
//          wait behind the same gate as everything else.
//   0.51.0 stops touching the Live API until it exists. Every failed LiveAPI
//          construction prints "Live API is not initialized", and init made
//          several, so loading a device filled the console. All Live access now
//          goes through one gate that stays shut until either a 'liveready'
//          message (from live.thisdevice) or the retry finds the API up.
//          Slot data also echoes on the status outlet so it can be seen there.
//   0.50.0 the device no longer gives up on the Live API. A device loads before
//          Live finishes building the API, so the one attempt made at init hit
//          "Live API is not initialized" and transport sync stayed off for the
//          session. It now retries until live_set resolves. Preset data
//          arriving before init is queued rather than dropped.
//   0.49.0 presets now travel with the Live set. embedmessage writes into the
//          .amxd file, not the set, so slots only survived if you saved the
//          device itself and every instance shared them. The slot data is now
//          pushed out for a [pattr], which Live does save per device instance.
//   0.48.0 the form modes show the variant that is playing. The lanes were
//          always drawn from S.call and S.response, so B and C were audible but
//          never visible - the form could be heard and not seen.
//   0.47.1 the every-4th-pass mode restored to its earlier name, 'Call with
//          response fill'
//   0.47.0 the form modes hold their variants. A / B / A / C and A / A' / B / B'
//          used to mutate the live phrase on every cycle, so A never came back
//          the same and the form was never audible. The variants are now built
//          once and replayed, and only the modes that say they mutate, or the
//          per-lane MUTATE sliders, keep changing anything.
//          Single pass removed.
//   0.46.0 changing STEPS resizes the phrase instead of generating a new one.
//          It was in the rebuild list, so lengthening a bar threw away the part
//          you already had.
//   0.45.0 every duplicate option removed. Nine phrase modes, one scale and
//          three motif types behaved identically to another entry; the ones
//          with a distinct meaning available were given it instead, the rest
//          are gone. Single pass can now be restarted without reloading.
//   0.44.0 'Call sparse, response busy' and its opposite named a density
//          difference between the lanes and then played identically to 'Call
//          then response'. They now bias the two lanes apart.
//   0.40.0 per-lane drift: each lane has its own mutate amount, applied once
//          per pass. Ignored on a lane the phrase mode already mutates, so the
//          two cannot fight over the same phrase.
//   0.39.0 a falling contour now starts high enough to actually fall. Degrees
//          are clamped at zero, and phrases began near the bottom, so 'Slight
//          fall' and 'Arch' had no room to descend and read as flat.
//   0.38.0 four menus were being overruled by the style's own post-processing
//          or were too weak to hear: contour presets now actually bend the
//          line, restrictive note pools hold their degrees, Legato is no longer
//          capped by the acid pass, and None means none everywhere.
//   0.37.0 RESOLVE no longer overrides an explicit cadence: with 'Resolve to
//          root' selected it decides how often that rule is honoured, rather
//          than picking its own stable tone. 'None' on slides and accents is
//          now respected by the acid styles, which used to add both anyway.
//   0.36.0 the six motif types that had no branch of their own now have one.
//          Deleting them would have been the other option, but Live stores this
//          parameter as an index, so removing six of twenty-eight shifts 25 of
//          them onto a different motif and breaks every saved device.
//   0.95.0 Next Phrase now applies to the dice as well as New Pair. Both share
//          one full-form queue, the New Pair button reports QUEUED for either,
//          and the footer identifies which change is waiting.
//   0.35.0 per-item help: the display asks for a description of whichever
//          option is under the cursor in a popup, and gets one derived from the
//          engine's own numbers where they exist
//   0.34.0 phrase modes that mutate during playback now refresh the lanes; the
//          notes were changing while the display showed the old ones
//   0.33.0 SWING moves to the header, beside RATE - both are timing controls
//   0.32.0 MIDI channel controls off the Options page. Live routes all MIDI in
//          a track to the instrument regardless of channel, so they only matter
//          when routing out to hardware; the parameters remain at 1 and 2.
//   0.31.0 forwards hoverparam to the display, so it works whether you send it
//          to [v8 crg.js] or straight to the v8ui
//   0.30.0 tells the display which action fired, so a trigger coming from a
//          mapped button or MIDI lights the matching control in the interface
//          rather than only the ones clicked in the v8ui
//   0.29.0 DENSITY only skips to the next distinct note count when the value
//          arrives from a nudge. An absolute value - a mapped macro, a numbox,
//          automation - is taken verbatim, so those keep their full resolution
//          instead of leaping ~20% a turn.
//   0.28.0 OCT JUMP takes the third knob, RESOLVE moves to Shape. Numeric
//          parameters arriving from Live (a mapped macro or MIDI controller)
//          now actually take effect - they were being stored and ignored.
//   0.27.0 fixed the no-op guard swallowing deliberate edits: setEnum writes the
//          value before param() runs, so param() saw "no change" and skipped
//          sendRate and the regeneration dispatch. Rate, and in fact every menu
//          driven from the display, stopped taking effect.
//          OCT JUMP is now a per-note offset instead of a rebuild, and the panel
//          carries NOTE LENGTH in place of CONTRAST.
//   0.26.0 reports auto-regenerate state to the display so it can show an LED
//   0.25.0 a parameter message carrying the value the parameter already has is
//          now a no-op. Live re-sends every parameter when it rebuilds the
//          device view - switching track focus does this - and the engine was
//          treating each one as an edit and generating a new phrase, so the
//          line changed under you mid-playback. Octaves now reach 0 and -1.
//   0.24.0 octaves read as plain numbers and default to 1; rate defaults to
//          1/16; each DENSITY notch now moves the note count by one instead of
//          sometimes doing nothing; added OCT JUMP, which finally uses the
//          octaveBias the original computed but never read.
//   0.23.0 octave excluded from the dice; DENSITY is now honoured at generation
//          time too, so the knob always describes what is on screen; added a
//          303 preset
//   0.22.0 status text also goes to the display footer, so outlet 3 and its
//          live.comment are now optional
//   0.21.0 DENSITY now adds and removes notes on the phrase you already have
//          instead of regenerating it. Octave moved next to Key in the header.
//   0.20.0 three performance macros: DENSITY drives how many steps sound,
//          CONTRAST how far the response departs from the call, RESOLVE how
//          firmly it settles. The Ending menu still picks the cadence type;
//          RESOLVE controls its strength. Rate moved to the header.
//   0.19.0 undo no longer destroys what it restores: pushing the restored
//          settings back to the UI re-entered param() and regenerated the
//          phrases. Swing is reported to the display so it can offset the
//          blocks.
//   0.18.0 restart no longer derives its position from current_song_time. That
//          read happens on a deferred thread while the metro bangs on the
//          scheduler, so a stale or early value rounded to the wrong step and
//          produced duplicated or skipped first steps. It now counts locally
//          and detects a transport start from the gap between bangs, which
//          needs no cross-thread reads at all.
//          Also: swing, undo, speed/octave moved to the panel, clip target no
//          longer lockable.
//   0.17.0 fixed step skipping: the metro's quantize stayed at 8n while only
//          its interval followed the SPEED control, so any rate faster than 1/8
//          could not fire often enough. Restart no longer depends on the
//          transport observer arriving in time; a jump in the song clock is
//          detected directly.
//   0.16.0 sync mode is a real parameter shown in the header; WRITE CLIP falls
//          through to the next free slot instead of overwriting
//   0.15.0 parameter changes now do the least work that is correct: only the
//          ones that define note choice rebuild a phrase. Octave, key and mode
//          re-render, ending re-applies the cadence, articulation re-runs over
//          the existing notes. Key and scale are excluded from the dice, and
//          mutation target left the Options page.
//   0.14.0 push transport state to the UI whenever Live's is_playing changes,
//          not only on a full refresh, so the play button tracks Live
//   0.13.0 restructured UI feed: controls are declared with a page (header /
//          generate / shape / articulation / options) and a kind, so the v8ui
//          can lay out tabbed pages. Adds per-lane mute, transport toggle and
//          numeric controls.
//   0.12.0 OCT and SPEED steppers in the header
//   0.11.0 dice randomiser for the panel menus (respects locks); KEY and SCALE
//          in a header zone; optional following of Live 12's song scale
//   0.10.0 always enabled (the bypass toggle is gone); parameter locks that
//          randomisation skips; feeds the v8ui panel its menu labels, items,
//          values and lock states, and accepts setmenu/lock/action back
//   0.9.0  leave the metro running while armed instead of switching it on from
//          the transport observer. Observer callbacks are deferred, so the
//          metro started a grid point or two late and the first steps of the
//          phrase were never played. Also adds a restart sync mode that starts
//          the phrase at step 1 wherever you press play.
//   0.8.0  lock playback to Live's song position instead of counting steps
//          from wherever the metro was left; send the display an explicit
//          pitch axis derived from key/scale/octave/register so regenerating
//          one lane can never move the other
//   0.7.0  coalesce display updates onto one deferred task (init was firing
//          ~90 messages); stop pushing JS defaults over Live's own restored
//          parameter values at init
//   0.6.0  outlet 4 feeds the v8ui display (crg_display.js): phrase data on
//          every change, playhead position on every step
//   0.5.0  self-initialise on script (re)load. autowatch reloads reset every
//          global, but [live.thisdevice] does not re-bang, so the device was
//          left with empty phrases and no transport observer until reopened.
//   0.4.0  lazy/cached LiveAPI, double-init guard, quantize-safe transport,
//          version reporting
//   0.3.0  fixed [metro] being sent start/stop instead of 1/0 (device was
//          silent); added selftest/testnote/forcestart diagnostics
//   0.2.0  rebuilt from the complete index.html: 1/8 step, velocities 92/118,
//          tie x1.22 + same-pitch 1.18 rule, real keep-* randomisers,
//          mutate-target remapping, auto-regenerate
//   0.1.0  first port, built from a truncated source
// ---------------------------------------------------------------------------
const CRG_VERSION = '0.96.2'; // Max-compatible native playback expressions
var nativeEnabled = false; // enabled by the bundled native bridge, not legacy tests

autowatch = 1;
inlets = 1;
outlets = 7; // 5: unchanged routing controller; 6: native playback commands

const O_MIDI = 0, O_XPORT = 1, O_SETP = 2, O_STATUS = 3, O_UI = 4;

// ---------------------------------------------------------------------------
// 1. Constants (verbatim from source)
// ---------------------------------------------------------------------------

const NOTE_NAMES = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];

const SCALE_INTERVALS = {
  'Chromatic':[0,1,2,3,4,5,6,7,8,9,10,11],
  'Major / Ionian':[0,2,4,5,7,9,11],
  'Natural Minor / Aeolian':[0,2,3,5,7,8,10],
  'Harmonic Minor':[0,2,3,5,7,8,11],
  'Melodic Minor':[0,2,3,5,7,9,11],
  'Dorian':[0,2,3,5,7,9,10],
  'Phrygian':[0,1,3,5,7,8,10],
  'Mixolydian':[0,2,4,5,7,9,10],
  'Locrian':[0,1,3,5,6,8,10],
  'Lydian':[0,2,4,6,7,9,11],
  'Lydian Dominant':[0,2,4,6,7,9,10],
  'Aeolian b5':[0,2,3,5,6,8,10],
  'Dorian b2':[0,1,3,5,7,9,10],
  'Mixolydian b6':[0,2,4,5,7,8,10],
  'Major Pentatonic':[0,2,4,7,9],
  'Minor Pentatonic':[0,3,5,7,10],
  'Blues Minor':[0,3,5,6,7,10],
  'Blues Major':[0,2,3,4,7,9],
  'Phrygian Dominant':[0,1,4,5,7,8,10],
  'Double Harmonic':[0,1,4,5,7,8,11],
  'Hungarian Minor':[0,2,3,6,7,8,11],
  'Romanian Minor':[0,2,3,6,7,9,10],
  'Whole Tone':[0,2,4,6,8,10],
  'Diminished Half-Whole':[0,1,3,4,6,7,9,10],
  'Diminished Whole-Half':[0,2,3,5,6,8,9,11],
  'Persian':[0,1,4,5,6,8,11],
  'Arabic':[0,2,4,5,6,8,10],
  'Hirajoshi':[0,2,3,7,8],
  'In Sen':[0,1,5,7,10],
  'Japanese Pentatonic':[0,1,5,7,8],
  'Kumoi':[0,2,3,7,9],
  'Pelog-ish':[0,1,3,7,8],
  'Egyptian Pentatonic':[0,2,5,7,10],
  'Minor Triad only':[0,3,7],
  'Power Fifths':[0,7],
  'Root + b7':[0,10],
  'Root + 5th + b7':[0,7,10],
  'Root + 2nd + 5th':[0,2,7],
  'Minor 7 arpeggio pool':[0,3,7,10],
  'Dominant 7 arpeggio pool':[0,4,7,10],
  'Diminished 7 arpeggio pool':[0,3,6,9]
};

const DATA = {
  keys: NOTE_NAMES,
  scales: Object.keys(SCALE_INTERVALS),
  styles: ['Classic Acid','Raw Acid','Squelchy Acid','Dark Acid','Hypno Acid','Sparse Acid','Busy Acid','Acid Stabs','Acid Runner','Acid Question/Answer','Driving Techno','Hypnotic Techno','Minimal Techno','Raw Warehouse','Tool Techno','Dub Techno','Industrial Techno','Percussive Techno','Loop Techno','Broken Techno','Jackin House','Deep House','Minimal House','Classic House','Tech House','Bass House','Shuffly House','Organ House-ish','House Stabline','House Bounce','90s Rave','Dark Rave','Rave Stab','Electro Pulse','Breaky Electro','Detroit-ish','Proto-Trance','Hooverish Riff','Motif Study','Rooted Groove','Repetitive Hook','Wide Answer','Tight Answer','Tension Builder','Resolver','Call Lead / Response Support'],
  responseStrategies: ['Same rhythm + resolve','Same rhythm + new contour','Motif + simplify','Motif + extend','Motif + truncate','Echo','Answer upward','Answer downward','Higher answer','Lower answer','Invert contour','Mirror contour loosely','Copy rhythm / change notes','Copy notes / change rhythm','Transpose motif up','Transpose motif down','Response resolves to root','Response resolves to 5th','Response resolves lower','Response resolves harder','Repeat then break','Tighter answer','Sparser answer','Busier answer','Accent the answer','Slide the answer','Staccato answer','Legato answer','Delayed answer','Half-phrase answer','Fragment reply','Tail answer','Opposite register','Same motif darker','Same motif brighter','Question then closure','Hook then fill','Call dominant / response tonic'],
  phraseModes: ['Call only','Response only','Call then response','Response then call','Alternate half bars','Alternate half bars, response first','Alternate 2-step cells','Alternate 1-step cells','Call, call, response','Call, response, response','Call, varied call, response','Call, response, varied response','A / B / A / C',"A / A' / B / B'",'Call with response fill','Mutating call, stable response','Stable call, mutating response','Both mutate slowly','Call sparse, response busy','Call busy, response sparse'],
  motifTypes: ['Pulse','Offbeat','Syncopated','Push','Sparse','Dense','Rest-heavy','Four-on-floor companion','Stabbed','Rolling','Repeated note','Stepwise rise','Stepwise fall','Rise then drop','Drop then rise','Leap then settle','Pivot note','Two-note hook','Three-note hook','Arp fragment','Acid nibble','Techno cell','House bounce','Rave stab figure','Drone anchor','Pedal point motif','Call tag','Answer tag'],
  articulationProfiles: ['Custom','Clean','Punchy','Rolling','Acidic','Hypnotic','Choppy','Fluid','Stabbed','Tense','Released'],
  gateFeels: ['Pluck','Stab','Tight','Short','Medium','Long','Full',
              'Mixed','Mixed short','Wide mix','Random within bounds'],
  connectionFeels: ['Detached','Semi-detached','Legato','Tied bias','Mixed ties','Connected runs'],
  slideProfiles: ['None','Rare','Occasional','Acid moderate','Acid heavy','Phrase-end slides','Upward slides bias','Downward slides bias'],
  accentProfiles: ['None','Light','Moderate','Heavy','Offbeat accents','Syncopated accents','Downbeat accents','Motif accents','Random accents within motif'],
  endingBehaviours: ['Resolve to root','Resolve to 5th','Leave hanging','Rise into next phrase','Fall into next phrase','Hard stop','Fade energy','Strong cadence','Weak cadence','Ambiguous ending','Response more resolved than call','Response same tension as call','Response more tense than call'],
  registerProfiles: ['Narrow register','Medium register','Wide register'],
  densityPresets: ['Very sparse','Sparse','Medium','Busy','Dense','Very dense'],
  contourPresets: ['Flat','Slight rise','Slight fall','Arch','Inverted arch','Wandering','Stepwise','Leapy'],
  repetitionPresets: ['Very repetitive','Repetitive','Balanced','Varied','Highly varied'],
  mutationAmounts: ['Tiny','Small','Medium','Strong','Brutal'],
  mutationTargets: ['Rhythm only','Pitch only','Articulation only','Ending only','Motif only','Whole phrase','Response only','Call only','Call + response linked'],
  notePoolModes: ['Strict note pool','Pool with spice notes','Pool expands on response','Pool contracts on response','Root-heavy','Root + 5th heavy','Triad pool','Seventh pool','Scale-wide'],
  channels: Array.from({length:16}, (_,i)=>`Channel ${i+1}`),
  laneOutputs: ['Track','Companion'],
  newPairTimings: ['Now','Next Phrase'],
  clipLengths: ['Exact phrase','Whole bars'],
  octaves: ['-1','0','1','2','3','4','5'],
  steps: Array.from({length:15}, (_,i)=>String(i+2)),
  // Max for Live additions
  rates: ['1/4','1/8','1/8t','1/16','1/16t','1/32'],
  syncModes: ['Restart','Timeline','Free'],
  clipTargets: ['Call','Response','Combined','Layered']
};

// Beat length of one step for each rate.
const RATE_BEATS = { '1/4':1, '1/8':0.5, '1/8t':1/3, '1/16':0.25, '1/16t':1/6, '1/32':0.125 };
const RATE_MAXNAME = { '1/4':'4n', '1/8':'8n', '1/8t':'8nt', '1/16':'16n', '1/16t':'16nt', '1/32':'32n' };

const DEFAULTS = {
  style:'Motif Study', responseStrategy:'Higher answer', motifType:'Acid nibble',
  phraseMode:'Call with response fill', endingBehaviour:'Resolve to root',
  key:'A', scale:'Minor Pentatonic', rootOctave:'1', steps:'8',
  registerProfile:'Medium register', densityPreset:'Medium', contourPreset:'Slight rise',
  repetitionPreset:'Very repetitive', mutationAmount:'Small', mutationTarget:'Call + response linked',
  notePoolMode:'Root-heavy',
  // PROFILE is a shortcut for the four menus below it; the four are set
  // explicitly here, so it starts on Custom rather than claiming a preset
  articulationProfile:'Custom', gateFeel:'Wide mix',
  connectionFeel:'Mixed ties', slideProfile:'Rare', accentProfile:'Moderate',
  callChannel:'Channel 1', responseChannel:'Channel 2',
  callOutput:'Track', responseOutput:'Track',
  rate:'1/16', clipTarget:'Combined', syncMode:'Restart',
  newPairTiming:'Now', clipLength:'Exact phrase',
  noteLengthPct:100, baseVelocity:64, accentDepth:65, swing:5, octaveJump:0, mutateAll:0,
  density:52, contrast:50, resolve:100, driftCall:0, driftResponse:0
};

// Which DATA list backs each enum parameter. Order here is the order the
// patcher's [route] uses, so keep it in sync with build_maxpat.py.
const ENUM_PARAMS = {
  style:'styles', responseStrategy:'responseStrategies', motifType:'motifTypes',
  phraseMode:'phraseModes', endingBehaviour:'endingBehaviours',
  key:'keys', scale:'scales', rootOctave:'octaves', steps:'steps',
  registerProfile:'registerProfiles', densityPreset:'densityPresets',
  contourPreset:'contourPresets', repetitionPreset:'repetitionPresets',
  mutationAmount:'mutationAmounts', mutationTarget:'mutationTargets',
  notePoolMode:'notePoolModes', articulationProfile:'articulationProfiles',
  gateFeel:'gateFeels', connectionFeel:'connectionFeels',
  slideProfile:'slideProfiles', accentProfile:'accentProfiles',
  callChannel:'channels', responseChannel:'channels',
  rate:'rates', clipTarget:'clipTargets', syncMode:'syncModes',
  callOutput:'laneOutputs', responseOutput:'laneOutputs',
  newPairTiming:'newPairTimings', clipLength:'clipLengths'
};
const NUM_PARAMS = ['noteLengthPct','baseVelocity','accentDepth','swing',
                    'density','contrast','resolve','octaveJump',
                    'driftCall','driftResponse','mutateAll'];

// The browser attaches a change listener to exactly these and regenerates both
// phrases. Rate, channels and clip target are device-only and excluded.
// What a parameter change actually requires. The browser rebuilt both phrases
// for any of these, which meant nudging the octave or the ending threw away a
// phrase you liked. Only the parameters that decide *which notes* get chosen
// rebuild; the rest do the smallest correct amount of work.
//
// REGEN   - note choice depends on it, so the phrase has to be rebuilt
// ENDING  - only the cadence changes, so re-apply that to the last step
// ARTIC   - gate, accent, slide and tie only; rhythm and pitch are untouched
// (none)  - nothing to recompute; the renderer already reads it live. Octave
//           and key shift pitch at render time, and mode only affects the
//           order lanes play in.
// ANSWER only describes how the response is derived, so it rebuilds that lane
// alone - regenerating the call as well threw away a phrase the user was
// happy with just to hear a different answer to it.
const RESPONSE_PARAMS = ['responseStrategy'];
const REGEN_PARAMS = ['style','motifType','scale',
  'registerProfile','densityPreset','contourPreset','repetitionPreset','notePoolMode',
  'contrast'];
// STEPS keeps the phrase and changes its length
const RESIZE_PARAMS = ['steps'];
const OCTJUMP_PARAMS = ['octaveJump'];
// These shape the notes without choosing them, so they re-apply to the current
// phrase rather than building a new one.
// These decide WHICH steps are accented, slid or tied, so changing one has to
// re-roll the articulation.
const ARTIC_PARAMS = ['articulationProfile','gateFeel','connectionFeel',
                      'slideProfile','accentProfile'];
// These only scale a number as the note is played. They must not touch the
// phrase at all - re-rolling on them moved accents around when all the user
// asked for was a different velocity.
const RENDER_PARAMS = ['accentDepth','baseVelocity','noteLengthPct'];
// resolve only affects the cadence, so it re-applies the ending rather than
// throwing the phrase away
const RESOLVE_PARAMS = ['resolve'];
// density only adds or removes notes, so it never rebuilds
const DENSITY_PARAMS = ['density'];
const ENDING_PARAMS = ['endingBehaviour'];

// ---------------------------------------------------------------------------
// 2. Helpers (verbatim)
// ---------------------------------------------------------------------------

function rand(arr){ return arr[Math.floor(Math.random()*arr.length)]; }
function chance(p){ return Math.random() < p; }
function clamp(v, lo, hi){ return Math.max(lo, Math.min(hi, v)); }
function pickN(arr, n){
  const a=[...arr], out=[];
  while(a.length && out.length<n){
    out.push(a.splice(Math.floor(Math.random()*a.length),1)[0]);
  }
  return out;
}

function rootMidi(c){
  const octaveMap={'-1':0,'0':12,'1':24,'2':36,'3':48,'4':60,'5':72,
                   'C1':24,'C2':36,'C3':48,'C4':60,'C5':72};   // old names still load
  return octaveMap[c.rootOctave] + NOTE_NAMES.indexOf(c.key);
}
function degreeToMidi(deg, c){
  const s = c.scale;
  const len = s.length || 1;
  const oct = Math.floor(deg / len);
  const idx = ((deg % len)+len)%len;
  return rootMidi(c) + s[idx] + oct*12;
}

function densityValue(name){ return {'Very sparse':0.24,'Sparse':0.36,'Medium':0.52,'Busy':0.66,'Dense':0.8,'Very dense':0.92}[name] || 0.5; }
function repetitionValue(name){ return {'Very repetitive':0.88,'Repetitive':0.72,'Balanced':0.5,'Varied':0.32,'Highly varied':0.18}[name] || 0.5; }
function contourBias(name){ return {'Flat':0,'Slight rise':1,'Slight fall':-1,'Arch':2,'Inverted arch':-2,'Wandering':99,'Stepwise':100,'Leapy':101}[name] ?? 0; }

// How likely each step of a beat is to sound. offbeatBias leans this toward
// the second and fourth sixteenth - it used to touch only accents and gate, so
// a style could describe itself as offbeat while playing squarely on the beat.
// Weighted onto the 'and' of the beat, which is where an offbeat bassline sits.
const OFFBEAT_WEIGHTS = [0.34, 0.40, 0.95, 0.46];

function preferredStepActivation(cat, idx, sp){
  let base;
  if(cat==='acid') base = [0.82,0.35,0.72,0.28];
  else if(cat==='techno') base = [0.86,0.18,0.58,0.22];
  else if(cat==='house') base = [0.68,0.52,0.60,0.40];
  else if(cat==='rave') base = [0.78,0.42,0.68,0.38];
  else base = [0.7,0.35,0.6,0.3];
  const b = sp && sp.offbeatBias ? clamp(sp.offbeatBias * 2.2, 0, 1) : 0;
  const i = idx % 4;
  return base[i] * (1 - b) + OFFBEAT_WEIGHTS[i] * b;
}

const STYLE_PRESETS = {
  'Classic Acid': {cat:'acid', density:0.56, repeat:0.66, slide:0.38, accent:0.46, rootBias:0.30, syncBias:0.28},
  'Raw Acid': {cat:'acid', density:0.62, repeat:0.58, slide:0.32, accent:0.54, rootBias:0.24, choppyBias:0.24},
  'Squelchy Acid': {cat:'acid', density:0.54, repeat:0.62, slide:0.46, accent:0.42, sustainBias:0.28},
  'Dark Acid': {cat:'acid', density:0.46, repeat:0.70, slide:0.26, accent:0.34, rootBias:0.38, restBias:0.20},
  'Hypno Acid': {cat:'acid', density:0.48, repeat:0.78, slide:0.24, accent:0.28, sustainBias:0.42},
  'Sparse Acid': {cat:'acid', density:0.34, repeat:0.74, slide:0.22, accent:0.30, restBias:0.30},
  'Busy Acid': {cat:'acid', density:0.72, repeat:0.54, slide:0.36, accent:0.44, motifVar:0.24},
  'Acid Stabs': {cat:'acid', density:0.42, repeat:0.66, slide:0.10, accent:0.52, choppyBias:0.46},
  'Acid Runner': {cat:'acid', density:0.70, repeat:0.60, slide:0.34, accent:0.36, leap:0.26},
  'Acid Question/Answer': {cat:'acid', density:0.52, repeat:0.68, slide:0.26, accent:0.40, responseTightness:0.78},
  'Driving Techno': {cat:'techno', density:0.44, repeat:0.78, accent:0.24, choppyBias:0.34, rootBias:0.38},
  'Hypnotic Techno': {cat:'techno', density:0.36, repeat:0.86, accent:0.18, sustainBias:0.28, responseTightness:0.78},
  'Minimal Techno': {cat:'techno', density:0.28, repeat:0.84, accent:0.16, restBias:0.34, choppyBias:0.40},
  'Raw Warehouse': {cat:'techno', density:0.48, repeat:0.66, accent:0.34, choppyBias:0.42, leap:0.18},
  'Tool Techno': {cat:'techno', density:0.40, repeat:0.82, accent:0.18, rootBias:0.44},
  'Dub Techno': {cat:'techno', density:0.26, repeat:0.88, accent:0.14, restBias:0.32, sustainBias:0.36},
  'Industrial Techno': {cat:'techno', density:0.46, repeat:0.60, accent:0.38, choppyBias:0.48, leap:0.20},
  'Percussive Techno': {cat:'techno', density:0.42, repeat:0.74, accent:0.32, choppyBias:0.44},
  'Loop Techno': {cat:'techno', density:0.38, repeat:0.90, accent:0.16, responseTightness:0.84},
  'Broken Techno': {cat:'techno', density:0.44, repeat:0.48, accent:0.24, motifVar:0.28, leap:0.22},
  'Jackin House': {cat:'house', density:0.52, repeat:0.66, accent:0.24, offbeatBias:0.42},
  'Deep House': {cat:'house', density:0.36, repeat:0.76, accent:0.18, sustainBias:0.24, offbeatBias:0.36},
  'Minimal House': {cat:'house', density:0.30, repeat:0.82, accent:0.18, restBias:0.26, offbeatBias:0.30},
  'Classic House': {cat:'house', density:0.46, repeat:0.70, accent:0.22, offbeatBias:0.38},
  'Tech House': {cat:'house', density:0.44, repeat:0.68, accent:0.22, choppyBias:0.22, offbeatBias:0.28},
  'Bass House': {cat:'house', density:0.58, repeat:0.62, accent:0.28, rootBias:0.40, offbeatBias:0.26},
  'Shuffly House': {cat:'house', density:0.48, repeat:0.66, accent:0.22, offbeatBias:0.48, sustainBias:0.18},
  'Organ House-ish': {cat:'house', density:0.42, repeat:0.72, accent:0.18, sustainBias:0.30, offbeatBias:0.34},
  'House Stabline': {cat:'house', density:0.40, repeat:0.70, accent:0.34, choppyBias:0.34, offbeatBias:0.30},
  'House Bounce': {cat:'house', density:0.50, repeat:0.68, accent:0.22, offbeatBias:0.50, leap:0.10},
  '90s Rave': {cat:'rave', density:0.56, repeat:0.46, accent:0.28, leap:0.30, octaveBias:0.22},
  'Dark Rave': {cat:'rave', density:0.44, repeat:0.56, accent:0.24, leap:0.24, rootBias:0.30},
  'Rave Stab': {cat:'rave', density:0.40, repeat:0.62, accent:0.38, choppyBias:0.36},
  'Electro Pulse': {cat:'rave', density:0.46, repeat:0.62, accent:0.24, offbeatBias:0.24},
  'Breaky Electro': {cat:'rave', density:0.48, repeat:0.42, accent:0.22, motifVar:0.26},
  'Detroit-ish': {cat:'other', density:0.40, repeat:0.70, accent:0.18, rootBias:0.34, sustainBias:0.20},
  'Proto-Trance': {cat:'rave', density:0.48, repeat:0.64, accent:0.18, sustainBias:0.24, leap:0.18},
  'Hooverish Riff': {cat:'rave', density:0.52, repeat:0.50, accent:0.30, leap:0.34},
  'Motif Study': {cat:'other', density:0.42, repeat:0.92, accent:0.14, motifVar:0.06, responseTightness:0.88},
  'Rooted Groove': {cat:'other', density:0.40, repeat:0.80, accent:0.16, rootBias:0.56},
  'Repetitive Hook': {cat:'other', density:0.46, repeat:0.90, accent:0.18, motifVar:0.10},
  'Wide Answer': {cat:'other', density:0.44, repeat:0.60, accent:0.18, responseTightness:0.46, leap:0.28},
  'Tight Answer': {cat:'other', density:0.44, repeat:0.72, accent:0.18, responseTightness:0.88},
  'Tension Builder': {cat:'other', density:0.48, repeat:0.66, accent:0.22, stableEnd:0.40, leap:0.22},
  'Resolver': {cat:'other', density:0.40, repeat:0.74, accent:0.16, stableEnd:0.90, responseTightness:0.78},
  'Call Lead / Response Support': {cat:'other', density:0.42, repeat:0.72, accent:0.18, responseTightness:0.84}
};

function styleCategory(style){
  if(STYLE_PRESETS[style] && STYLE_PRESETS[style].cat) return STYLE_PRESETS[style].cat;
  const s = style.toLowerCase();
  if(s.includes('acid')) return 'acid';
  if(s.includes('house')) return 'house';
  if(s.includes('techno')) return 'techno';
  if(s.includes('rave') || s.includes('electro') || s.includes('hoover')) return 'rave';
  return 'other';
}

// Which of a style's fields a menu already owns. A style must not set these:
// it was pushing accents to 54% while the ACCENT menu said 24%, which is the
// same trap the articulation PROFILE fell into before it became a preset.
const MENU_OWNED = ['density','repeat','accent','slide'];

function styleProfile(c){
  const style=c.style.toLowerCase();
  let density=0.45, leap=0.18, repeat=0.45, stableEnd=0.7, accent=0.25, slide=0.18;
  let restBias=0.18, motifVar=0.18, responseTightness=0.5, rootBias=0.22, octaveBias=0.12;
  let syncBias=0.18, offbeatBias=0.14, sustainBias=0.18, choppyBias=0.14;
  const preset = STYLE_PRESETS[c.style];
  if(style.includes('acid')){leap=0.22; rootBias=0.28; responseTightness=0.66; syncBias=0.26; sustainBias=0.34;}
  if(style.includes('raw')){motifVar+=0.08; choppyBias+=0.12;}
  if(style.includes('hypno')){leap-=0.08; stableEnd=0.55; restBias=0.1; responseTightness=0.72; sustainBias+=0.16;}
  if(style.includes('minimal')){restBias=0.28;}
  if(style.includes('busy')){restBias-=0.06;}
  if(style.includes('sparse')){restBias+=0.12;}
  if(style.includes('house')){leap=0.12; rootBias=0.32; responseTightness=0.62; offbeatBias=0.3; syncBias=0.2; sustainBias=0.12;}
  if(style.includes('techno')){leap=0.14; rootBias=0.35; restBias=0.24; responseTightness=0.7; choppyBias=0.3; syncBias=0.12;}
  if(style.includes('rave')||style.includes('electro')||style.includes('hoover')){leap=0.24; octaveBias=0.2; offbeatBias=0.22;}
  if(style.includes('dub')){restBias=0.3; stableEnd=0.78; sustainBias=0.22;}
  if(style.includes('industrial')||style.includes('warehouse')){leap=0.18; rootBias=0.4; choppyBias=0.28;}
  if(style.includes('motif study')){motifVar=0.08; responseTightness=0.85;}
  if(style.includes('rooted groove')){rootBias=0.5; stableEnd=0.8;}
  if(style.includes('repetitive hook')){motifVar=0.1;}
  if(preset){
    leap = preset.leap ?? leap;
    stableEnd = preset.stableEnd ?? stableEnd;
    restBias = preset.restBias ?? restBias;
    motifVar = preset.motifVar ?? motifVar;
    responseTightness = preset.responseTightness ?? responseTightness;
    rootBias = preset.rootBias ?? rootBias;
    octaveBias = preset.octaveBias ?? octaveBias;
    syncBias = preset.syncBias ?? syncBias;
    offbeatBias = preset.offbeatBias ?? offbeatBias;
    sustainBias = preset.sustainBias ?? sustainBias;
    choppyBias = preset.choppyBias ?? choppyBias;
  }
  // density, repeat, accent and slide are deliberately absent: their menus own
  // them. Anything reading sp.accent or sp.slide now falls back to the menu.
  return {leap, stableEnd, restBias, motifVar, responseTightness, rootBias,
          octaveBias, syncBias, offbeatBias, sustainBias, choppyBias};
}

function notePool(c){
  const scaleLen = c.scale.length;
  let pool = [...Array(Math.min(scaleLen,7)).keys()];
  if(c.notePoolMode==='Strict note pool') pool = pickN(pool, Math.min(pool.length, 3));
  else if(c.notePoolMode==='Pool with spice notes') pool = [...new Set([...pickN(pool, Math.min(pool.length,4)), Math.min(scaleLen-1, Math.floor(scaleLen/2))])];
  else if(c.notePoolMode==='Pool expands on response') pool = [0,1,2,4].filter(x=>x<scaleLen);
  else if(c.notePoolMode==='Pool contracts on response') pool = [...Array(scaleLen).keys()];
  else if(c.notePoolMode==='Root-heavy') pool = [0,0,0,1,2,4].filter(x=>x<scaleLen);
  else if(c.notePoolMode==='Root + 5th heavy') pool = [0,0,4,4,1,2].filter(x=>x<scaleLen);
  else if(c.notePoolMode==='Triad pool') pool = [0,2,4].filter(x=>x<scaleLen);
  else if(c.notePoolMode==='Seventh pool') pool = [0,2,4,6].filter(x=>x<scaleLen);
  else if(c.notePoolMode==='Scale-wide') pool = [...Array(scaleLen).keys()];
  return pool.length ? pool : [0];
}

// Which scale degree is the fifth. It is not a fixed index: minor pentatonic
// reaches it in three steps, a seven-note scale in four, so it has to be found
// by interval rather than counted.
function fifthDegree(c){
  if(!c.scale || !c.scale.length) return 0;
  let best = 0, bestD = 99;
  for(let i=0;i<c.scale.length;i++){
    const d = Math.abs((c.scale[i] % 12) - 7);
    if(d < bestD){ bestD = d; best = i; }
  }
  return best;
}

// How far a phrase may roam, in scale degrees, worked out from the scale rather
// than fixed. Six degrees is a comfortable octave in a seven-note scale and
// well over one in a pentatonic, which is why 'Narrow' was not narrow.
function registerCeiling(c){
  const len = Math.max(3, (c.scale && c.scale.length) || 7);
  if(c.registerProfile === 'Narrow register') return len - 1;        // inside one octave
  if(c.registerProfile === 'Medium register') return len + Math.floor(len / 2);
  return len * 2 + 2;                                                // Wide: about 2.5
}

// Pools whose name promises a specific set of degrees.
const RESTRICTIVE_POOLS = ['Strict note pool','Triad pool','Seventh pool',
                           'Root-heavy','Root + 5th heavy'];

// Snap a stray degree back onto the pool, keeping the octave it landed in.
//
// Nearest-match alone throws away the weighting: 'Root-heavy' is [0,0,0,1,2,4],
// three-fifths root, but nearest-match treats the three zeros as one. Weighted
// pools therefore re-draw from the pool part of the time so the duplicates
// count for something.
function snapToPool(deg, pool, c, nearestOnly){
  const len = Math.max(1, c.scale.length);
  const oct = Math.floor(deg / len);
  const within = deg - oct * len;
  const weighted = pool.length !== new Set(pool).size;
  if(weighted && !nearestOnly && chance(0.5)) return (rand(pool) % len) + oct * len;
  let best = pool[0], bestD = 99;
  for(let i=0;i<pool.length;i++){
    const d = Math.abs((pool[i] % len) - within);
    if(d < bestD){ bestD = d; best = pool[i] % len; }
  }
  return best + oct * len;
}

function motifTemplate(c){
  const t = c.motifType.toLowerCase();
  let len = c.steps <= 4 ? 2 : (c.steps <= 8 ? rand([2,3,4]) : rand([3,4]));
  let rhythm = Array(len).fill(false), contour=[];
  if(t.includes('pulse')) rhythm = Array(len).fill(true);
  else if(t.includes('offbeat')) rhythm = Array.from({length:len},(_,i)=>i%2===1);
  else if(t.includes('sync')) rhythm = Array.from({length:len},(_,i)=>chance(i===0?0.78:0.55));
  else if(t.includes('sparse')||t.includes('rest')) rhythm = Array.from({length:len},()=>chance(0.45));
  else if(t.includes('dense')||t.includes('rolling')) rhythm = Array.from({length:len},()=>chance(0.82));
  else if(t.includes('stab')) rhythm = Array.from({length:len},(_,i)=> i===0 || chance(0.25));
  // anticipates: leans on the step before the strong one
  else if(t.includes('push')) rhythm = Array.from({length:len},(_,i)=> i===len-1 || chance(0.35));
  // sits with the kick: only on the strong step of the cell
  else if(t.includes('four-on-floor')) rhythm = Array.from({length:len},(_,i)=> i===0);
  else if(t.includes('house bounce')) rhythm = Array.from({length:len},(_,i)=> i===0 || i%2===1);
  // a held note underneath: first step only, long
  else if(t.includes('drone')) rhythm = Array.from({length:len},(_,i)=> i===0);
  else if(t.includes('pedal')) rhythm = Array.from({length:len},(_,i)=> i%2===0);
  // a short closing figure: the last two steps
  else if(t.includes('tag')) rhythm = Array.from({length:len},(_,i)=> i >= len-2);
  else rhythm = Array.from({length:len},()=>chance(0.65));
  if(!rhythm.some(Boolean)) rhythm[0]=true;

  if(t.includes('repeated')) contour = Array(len).fill(0);
  else if(t.includes('stepwise rise')) contour = Array.from({length:len},(_,i)=>i);
  else if(t.includes('stepwise fall')) contour = Array.from({length:len},(_,i)=>-i);
  else if(t.includes('rise then drop')) contour = [0,1,2,0,-1].slice(0,len);
  else if(t.includes('drop then rise')) contour = [0,-1,-2,0,1].slice(0,len);
  else if(t.includes('leap')) contour = [0, rand([2,3,4]), 1, 0].slice(0,len);
  else if(t.includes('pivot')) contour = [0,1,0,-1].slice(0,len);
  else if(t.includes('two-note')) contour = [0,1,0,1].slice(0,len);
  else if(t.includes('three-note')) contour = [0,1,2,1].slice(0,len);
  else if(t.includes('arp')) contour = [0,2,4,2].slice(0,len);
  // these three shared one contour and were indistinguishable; each now has
  // the shape its name describes
  else if(t.includes('acid nibble')) contour = [0,0,1,0,-1].slice(0,len);
  else if(t.includes('techno cell')) contour = [0,0,0,1].slice(0,len);
  else if(t.includes('house bounce')) contour = [0,2,0,1].slice(0,len);
  // drone and pedal hold one pitch; push shoves upward into the next beat
  else if(t.includes('drone')) contour = Array(len).fill(0);
  // a pedal point is a held note with movement above it, not a flat line
  else if(t.includes('pedal')) contour = [0,3,0,4].slice(0,len);
  else if(t.includes('push')) contour = [0,0,1,2].slice(0,len);
  else if(t.includes('four-on-floor')) contour = [0,0,0,1].slice(0,len);
  else if(t.includes('call tag')) contour = [0,1].concat(Array(Math.max(0,len-2)).fill(0)).slice(0,len);
  else if(t.includes('answer tag')) contour = [0,-1].concat(Array(Math.max(0,len-2)).fill(0)).slice(0,len);
  else contour = Array.from({length:len},()=>rand([-1,0,1]));
  return {len, rhythm, contour};
}

function articulationForStep(idx, active, c, motifPos){
  const sp = styleProfile(c);
  const cat = styleCategory(c.style);
  let gate=0.82, accent=false, slide=false, tie=false;
  const gateFeel=c.gateFeel, conn=c.connectionFeel, slideP=c.slideProfile, accentP=c.accentProfile;

  // Measured from real 303 patterns: a stab is about a quarter of a step, a
  // normal note about half, and only joined notes reach a whole one.
  // A fraction of one step. At 1/16 and 120bpm a step is 125ms, so Pluck is
  // about 8ms, Stab 12ms, Short 31ms - Short is Sting's shortest note.
  if(gateFeel==='Pluck') gate=0.06;
  else if(gateFeel==='Stab') gate=0.10;
  else if(gateFeel==='Tight') gate=0.15;
  else if(gateFeel==='Short') gate=0.25;
  else if(gateFeel==='Medium') gate=0.54;
  else if(gateFeel==='Long') gate=0.85;
  else if(gateFeel==='Full') gate=0.95;
  // Stabs, normal notes and held notes in the same bar. The old cycle ran
  // 0.55 to 1.15 in even steps, so everything sounded much the same length.
  // Sting's weighting - about a third short, about half at a half step - with
  // a shorter stab thrown in for range
  else if(gateFeel==='Mixed') gate=[0.54,0.25,0.15,0.54,0.25,0.85,0.54,0.25,0.54,0.15][idx%10];
  // weighted to the short end - mostly plucks and stabs with the odd longer one
  else if(gateFeel==='Mixed short') gate=[0.10,0.25,0.06,0.15,0.25,0.10,0.54,0.06,0.15,0.10][idx%10];
  // the whole range in one bar, from a click to a held note
  else if(gateFeel==='Wide mix') gate=[0.06,0.54,0.15,0.95,0.25,0.10,0.85,0.25,0.54,0.06][idx%10];
  else if(gateFeel==='Random within bounds') gate=0.20 + Math.random()*0.70;

  // The genre category used to clamp gate here - acid pushed everything past
  // 0.72, techno capped it at 0.78 - which meant the GATE menu was only
  // advisory. A category may still lean on a step, but only as a nudge from
  // whatever the menu chose, never as a floor or ceiling of its own.
  if(cat==='acid' && idx % 4 === 3 && chance(sp.sustainBias)) gate *= 1.15;
  else if(cat==='techno' && idx % 4 === 0 && chance(sp.choppyBias)) gate *= 0.85;
  else if(cat==='house' && idx % 2 === 1 && chance(sp.offbeatBias)) gate *= 1.12;

  // JOIN sets how often a note runs into the next, not how long every note is.
  // Forcing every gate past 1.0 made the whole line legato, which is why the
  // synth's release had nothing to work with.
  // Only the gate part can run here. The extra joins have to wait until after
  // the SLIDE menu below, which assigns slide outright and would wipe them.
  // JOIN uses ties. It used to add slides as well, on top of whatever the SLIDE
  // menu had already decided, which left every SLIDE setting sounding the same.
  if(conn==='Detached') gate=Math.min(gate,0.45);
  else if(conn==='Semi-detached') gate=Math.min(gate,0.62);
  else if(conn==='Legato') tie=chance(0.45);
  else if(conn==='Tied bias') tie=chance(0.30);
  else if(conn==='Mixed ties') tie=chance(0.18);
  else if(conn==='Connected runs') tie=chance(0.28);

  if(slideP==='None') slide=false;
  else if(slideP==='Rare') slide = chance(0.06);
  else if(slideP==='Occasional') slide = chance(0.16);
  else if(slideP==='Acid moderate') slide = chance(0.28);
  else if(slideP==='Acid heavy') slide = chance(0.42);
  else if(slideP==='Phrase-end slides') slide = chance(motifPos>=2 ? 0.32 : 0.04);
  // these named a direction and then behaved identically; the bias now decides
  // which way the following note is nudged, which is what makes it audible
  else if(slideP==='Upward slides bias') slide = chance(0.30);
  else if(slideP==='Downward slides bias') slide = chance(0.30);

  if(accentP==='None') accent=false;
  else if(accentP==='Light') accent = chance(0.14);
  else if(accentP==='Moderate') accent = chance(0.24);
  else if(accentP==='Heavy') accent = chance(0.38);
  // every offbeat, not half of them - the scattered version is now its own
  // entry, 'Syncopated accents'
  // The offbeat is the third sixteenth of the beat - the 'and'. Every odd step
  // is the 'e' and the 'a' as well, which is a different feel entirely.
  else if(accentP==='Offbeat accents') accent = idx%4===2;
  else if(accentP==='Syncopated accents') accent = idx%2===1 && chance(0.5);
  else if(accentP==='Downbeat accents') accent = idx%4===0 || idx===0;
  else if(accentP==='Motif accents') accent = motifPos===0 || chance(0.08);
  // distinct from Moderate: never the first step of a cell, which is what
  // Motif accents does - this is the inverse, accents anywhere but the downbeat
  else if(accentP==='Random accents within motif') accent = motifPos > 0 && chance(0.40);

  // PROFILE last, so a profile that shapes accents or slides is not immediately
  // overwritten by the accent and slide menus running after it. Explicit None
  // still wins over everything.
  const noAcc = (accentP === 'None'), noSli = (slideP === 'None');


  // Categories no longer ADD accents or slides, and no longer clamp the gate.
  // A style pushing accents from the menu's 24% up to 54% behind the user's
  // back is exactly what made STYLE feel like it was fighting the controls.
  // What a genre still does here is lean on a step, not overrule the menu.
  if(cat==='acid' && active) tie = tie || chance(0.06);

  if(!active){ accent=false; slide=false; tie=false; }
  return {gate, accent, slide, tie};
}

// Ties and slides are rolled one step at a time, which spreads them evenly.
// Real patterns group them: two or three held notes together, then a stretch of
// short ones. This keeps the count the engine already decided on and just moves
// them into runs.
function clusterHolds(phrase, only, runs){
  // Only regroup the kind that changed. Redistributing both together meant a
  // change to SLIDE also moved every tie.
  const kindOn = function(k){ return !only || only.indexOf(k) !== -1; };
  const live = [];
  for(let i=0;i<phrase.length;i++) if(phrase[i].active) live.push(i);
  if(live.length < 4) return;

  const held = live.filter(function(i){
    return (kindOn('tie') && phrase[i].tie) || (kindOn('slide') && phrase[i].slide);
  });
  const want = held.length;
  if(!want) return;

  // remember which kind each held note was, so the mix is preserved
  const kinds = held.map(function(i){ return (kindOn('tie') && phrase[i].tie) ? 'tie' : 'slide'; });
  held.forEach(function(i){
    if(kindOn('tie')) phrase[i].tie = false;
    if(kindOn('slide')) phrase[i].slide = false;
  });

  let placed = 0, guard = 0;
  while(placed < want && guard++ < 40){
    // Sting's runs are 76% single and 24% pairs, never three. 'Connected runs'
    // is the exception - it exists to make chains, so it reaches further.
    const shape = runs ? [2,2,3,3,4] : [1,1,1,2];
    const runLen = Math.min(rand(shape), want - placed);
    const startAt = Math.floor(Math.random() * Math.max(1, live.length - runLen));
    let clash = false;
    for(let k=0;k<runLen;k++){
      const idx = live[startAt + k];
      if(idx === undefined || phrase[idx].tie || phrase[idx].slide){ clash = true; break; }
      // leave a gap either side so runs stay separate
      const before = live[startAt - 1], after = live[startAt + runLen];
      if(before !== undefined && (phrase[before].tie || phrase[before].slide)) clash = true;
      if(after !== undefined && (phrase[after].tie || phrase[after].slide)) clash = true;
    }
    if(clash) continue;
    for(let k=0;k<runLen;k++){
      const idx = live[startAt + k];
      const kind = kinds[placed] || 'tie';
      phrase[idx][kind] = true;
      placed++;
    }
  }
}

function postProcessArticulation(phrase, c, only){
  const touches = function(f){ return !only || only.indexOf(f) !== -1; };
  if(touches('tie') || touches('slide'))
    clusterHolds(phrase, only, c.connectionFeel === 'Connected runs');

  // A slide into the same pitch is silent - the synth glides from a note to
  // itself. Drop those so the marks on screen and the overlaps in the clip
  // mean something.
  for(let i=0;i<phrase.length-1;i++){
    const s = phrase[i];
    if(!touches('slide') || !s || !s.active || !s.slide) continue;
    let j = i + 1;
    while(j < phrase.length && !phrase[j].active) j++;
    if(j >= phrase.length){ s.slide = false; continue; }
    if(phrase[j].degree === s.degree){
      // A glide to the same note is silent. Move the destination rather than
      // dropping the slide - on a repetitive phrase almost every slide landed
      // on a repeat, so the SLIDE menu had almost no effect at all.
      const ceilDeg = registerCeiling(c);
      const dir = (s.degree >= ceilDeg) ? -1 : (s.degree <= 0 ? 1 : rand([-1,1]));
      phrase[j].degree = clamp(s.degree + dir, 0, ceilDeg);
      // if the clamp put it back where it started, go the other way
      if(phrase[j].degree === s.degree)
        phrase[j].degree = clamp(s.degree - dir, 0, ceilDeg);
    }
  }

  // A tie is a HOLD, so it only means anything between two notes of the same
  // pitch - the mirror of the slide rule above. Tying into a different pitch
  // just overlapped two unrelated notes, which is neither a tie nor a slide.
  for(let i=0;i<phrase.length-1;i++){
    const s = phrase[i];
    if(!touches('tie') || !s || !s.active || !s.tie) continue;
    let j = i + 1;
    while(j < phrase.length && !phrase[j].active) j++;
    if(j >= phrase.length){ s.tie = false; continue; }
    if(s.slide){ s.tie = false; continue; }        // it is gliding, not holding
    if(phrase[j].degree !== s.degree) phrase[j].degree = s.degree;
  }

  const cat = styleCategory(c.style);

  // A slide is only audible as a direction if the note it runs into moves that
  // way. These two options named a direction and then behaved identically.
  const dirBias = (c.slideProfile === 'Upward slides bias') ? 1
                : (c.slideProfile === 'Downward slides bias') ? -1 : 0;
  if(dirBias){
    for(let i=0;i<phrase.length-1;i++){
      const s = phrase[i], n = phrase[i+1];
      if(!s.active || !s.slide || !n || !n.active || n.degree == null || s.degree == null) continue;
      const moving = n.degree - s.degree;
      if(moving === 0 || Math.sign(moving) !== dirBias){
        n.degree = clamp(s.degree + dirBias * rand([1,1,2]), 0, 11);
      }
    }
  }

  if(cat==='acid'){
    for(let i=0;i<phrase.length;i++){
      const s=phrase[i];
      if(!s.active || s.degree==null) continue;
      // Acid moves accents onto the downbeat and the root - it does not add
      // any. The ACCENT menu decides how many there are.
      if(touches('accent') && s.accent && !(i % 4 === 0 || s.degree === 0) && chance(0.4)){
        s.accent = false;
        for(let k=0;k<phrase.length;k++){
          if(phrase[k].active && !phrase[k].accent && (k % 4 === 0 || phrase[k].degree === 0)){
            phrase[k].accent = true; break;
          }
        }
      }
      if(touches('tie') && i < phrase.length-1 && phrase[i+1] && phrase[i+1].active){
        const diff = Math.abs((phrase[i+1].degree ?? s.degree) - s.degree);
        if(diff <= 2 && chance(0.10)){ s.tie=true; s.gate=Math.max(s.gate||0.8,1.05); }
      }
    }
    let slides = phrase.filter(s=>s.slide).length;
    const maxSlides = Math.ceil(phrase.length / 3);
    if(slides > maxSlides){
      for(let i=phrase.length-1;i>=0 && slides>maxSlides;i--){
        if(phrase[i].slide){ phrase[i].slide=false; slides--; }
      }
    }
  } else if(cat==='techno'){
    // techno: nudge accents onto the downbeats rather than adding more
    phrase.forEach((s,i)=>{ if(touches('accent') && s.active && s.accent && i%4!==0 && chance(0.25)){
      s.accent=false;
      for(let k=0;k<phrase.length;k+=4) if(phrase[k].active && !phrase[k].accent){ phrase[k].accent=true; break; }
    }});
  } else if(cat==='house'){
    // house: move accents onto the offbeats rather than adding more
    phrase.forEach((s,i)=>{ if(touches('accent') && s.active && s.accent && i%2===0 && chance(0.3)){
      s.accent=false;
      for(let k=1;k<phrase.length;k+=2) if(phrase[k].active && !phrase[k].accent){ phrase[k].accent=true; break; }
    }});
  }
  return phrase;
}

// Upstream bug guard. Several places in the original revive a step
// (active: false -> true) without giving it a degree, so it ends up active with
// no pitch. The browser app renders those as rests and exports no note for
// them, so collapsing them to real rests here keeps output identical while
// making the invariant "active implies pitched" hold everywhere downstream.
// Fires on roughly 0.8% of generated phrases. See README for the alternative
// fix, which revives the step with a pitch instead of silencing it.
function normalisePhrase(phrase){
  for(let i=0;i<phrase.length;i++){
    const s = phrase[i];
    if(s.active && s.degree == null){ s.active = false; s.accent = false; s.slide = false; s.tie = false; }
    if(!s.active) s.degree = null;
  }
  // If the only "active" step was a pitchless one, silencing it would leave the
  // phrase completely empty. applyEnding and buildCall's motif guard both fall
  // back to root in that situation, so do the same here.
  if(phrase.length && !phrase.some(s=>s.active)){
    const last = phrase[phrase.length-1];
    last.active = true;
    last.degree = 0;
  }
  return phrase;
}

function stableDegreesForScale(c){ return [0,2,4,6].filter(x=>x < c.scale.length); }
function phraseContour(phrase){
  const degs = phrase.filter(s=>s.active && s.degree!=null).map(s=>s.degree);
  if(degs.length < 2) return 0;
  return degs[degs.length-1] - degs[0];
}
function motifFromCall(call, steps){
  const motifLen = Math.max(2, Math.min(4, Math.floor(steps/2)));
  return call.slice(0,motifLen).map(s=>({...s}));
}

function applyEnding(phrase,c,phase='call'){
  const lastIdx = phrase.length - 1;
  if(lastIdx < 0) return;
  const before = phrase[lastIdx].degree;   // RESOLVE falls back to this
  if(!phrase[lastIdx].active){
    phrase[lastIdx].active = true;
    phrase[lastIdx].degree = 0;
  }
  const end = c.endingBehaviour;
  if(end.includes('Resolve to root')) phrase[lastIdx].degree = 0;
  else if(end.includes('Resolve to 5th')) phrase[lastIdx].degree = Math.min(4, c.scale.length-1);
  else if(end.includes('Rise into')) phrase[lastIdx].degree = clamp((phrase[lastIdx-1]?.degree||0)+1,0,10);
  else if(end.includes('Fall into')) phrase[lastIdx].degree = clamp((phrase[lastIdx-1]?.degree||1)-1,0,10);
  else if(end.includes('Hard stop')) { phrase[lastIdx].gate = 0.35; phrase[lastIdx].tie=false; }
  else if(end.includes('Fade energy')) { phrase[lastIdx].accent=false; phrase[lastIdx].gate=0.55; }
  else if(end.includes('Strong cadence')) phrase[lastIdx].degree = rand([0,0,0,2,4].filter(x=>x<c.scale.length));
  else if(end.includes('Weak cadence')) phrase[lastIdx].degree = rand([1,3,5].filter(x=>x<c.scale.length));
  else if(end.includes('Ambiguous')) phrase[lastIdx].degree = rand([1,2,5].filter(x=>x<c.scale.length));

  if(phase==='response' && c.endingBehaviour.includes('more resolved')) phrase[lastIdx].degree = rand([0,0,2,4].filter(x=>x<c.scale.length));
  if(phase==='response' && c.endingBehaviour.includes('more tense')) phrase[lastIdx].degree = rand([1,3,5].filter(x=>x<c.scale.length));

  // RESOLVE macro. Where the menu names a specific target - resolve to root, to
  // the fifth - it has already been applied above, and this only decides how
  // often that is honoured: at 100 always, at 0 the phrase is left where it
  // landed. Where the menu names no target, it picks stable against tense.
  if(c.resolve !== undefined){
    const stableTones = [0,2,4].filter(x=>x<c.scale.length);
    const tenseTones  = [1,3,5,6].filter(x=>x<c.scale.length);
    const explicit = /root|5th|cadence|resolved|tense/i.test(c.endingBehaviour || '');
    const apply = (phase === 'response') || chance(0.5);
    if(apply && stableTones.length && tenseTones.length){
      if(explicit){
        // honour the menu's own choice with probability = resolve; below that,
        // put the phrase back where it was so the rule simply is not applied
        if(!chance(c.resolve)) phrase[lastIdx].degree = before;
      } else {
        phrase[lastIdx].degree = chance(c.resolve) ? rand(stableTones) : rand(tenseTones);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// 3. Phrase construction (verbatim)
// ---------------------------------------------------------------------------

function buildCall(c){
  const sp = styleProfile(c);
  const cat = styleCategory(c.style);
  const motif = motifTemplate(c);
  const pool = notePool(c);
  const steps = c.steps;
  const out = [];
  // DENSITY macro replaces the old preset: 0 is very sparse, 100 very dense.
  const densTarget = (c.densityTarget !== undefined)
    ? (0.20 + c.densityTarget * 0.74)
    : densityValue(c.densityPreset);
  const repTarget = repetitionValue(c.repetitionPreset);
  const contourMode = contourBias(c.contourPreset);

  // The starting point is a fraction of the register, so a narrow phrase starts
  // in nearly the same place every time and a wide one has room to move.
  const ceilNow = registerCeiling(c);
  let center = 0;
  if(c.registerProfile === 'Narrow register') center = 0;
  else if(c.registerProfile === 'Medium register') center = Math.floor(Math.random() * Math.max(1, ceilNow * 0.25));
  else center = Math.floor(Math.random() * Math.max(1, ceilNow * 0.5));
  // Give a descending shape somewhere to descend from.
  if(contourBias(c.contourPreset) < 0) center += (c.registerProfile==='Narrow register') ? 2 : 3;

  const motifSteps = [];
  let prevDeg = center + rand([0,0,1,-1]);
  for(let m=0;m<motif.len;m++){
    let active = motif.rhythm[m];
    if(chance(sp.restBias + (1-densTarget)*0.25)) active = active && chance(0.68);
    let deg = prevDeg;
    // Flat means flat: no jumping back to the root at the start of a cell.
    if(contourMode!==0 && m===0 && chance(sp.rootBias)) deg = rand([0,0,1,2].filter(x=>x<c.scale.length));
    else{
      const contour = motif.contour[m] ?? 0;
      // No 'm>0' guard: the first step of each cell was falling through to the
      // random branch below, which is where Flat's second pitch came from.
      if(contourMode===0) deg = prevDeg;                                       // Flat: holds
      else if(contourMode===99 && m>0) deg = prevDeg + rand([-3,-2,-1,1,2,3]); // Wandering: roams
      else if(contourMode===100 && m>0) deg = prevDeg + rand([-1,0,1]);
      else if(contourMode===101 && m>0 && chance(0.35)) deg = prevDeg + rand([-3,-2,2,3]);
      // The bias has to outweigh the jitter or the preset is inaudible: a
      // +1 tendency with a -1..+1 wobble on top averages out to nothing.
      else if(contourMode===1) deg = prevDeg + 1 + Math.max(0, contour) + rand([-1,0,0,0]);
      else if(contourMode===-1) deg = prevDeg - 1 + Math.min(0, contour) + rand([0,0,0,1]);
      else if(contourMode===2) deg = prevDeg + (m < Math.ceil(motif.len/2) ? 1 : -1) * 1.5 + rand([0,0,0,1]);
      else if(contourMode===-2) deg = prevDeg + (m < Math.ceil(motif.len/2) ? -1 : 1) * 1.5 + rand([-1,0,0,0]);
      else if(chance(0.65)) deg = prevDeg + contour + rand([-1,0,0,1]);
      else deg = rand(pool) + rand([0,0,Math.max(0,Math.floor(center/2))]);
    }
    const leapScale = (c.registerProfile==='Wide register') ? 1.6
                    : (c.registerProfile==='Narrow register') ? 0.5 : 1;
    // and no leaps or style wobble on top of it either
    if(contourMode!==0){
      if(chance(sp.leap * (contourMode===101 ? 1.45 : 1) * leapScale))
        deg += rand(c.registerProfile==='Wide register' ? [-4,-3,3,4] : [-3,3]);
      if(cat==='acid' && chance(sp.syncBias)) deg += rand([-1,0,1]);
      if(cat==='house' && chance(sp.offbeatBias) && m%2===1) deg += rand([0,1]);
    }
    if(cat==='techno' && chance(sp.rootBias*0.4)) deg = rand([0,0,2,4].filter(x=>x<c.scale.length));
    deg = clamp(deg, 0, registerCeiling(c));
    // A named pool should still be recognisable after the contour and leap
    // arithmetic has moved things around, so pull strays back onto it.
    // A weighted pool re-draws to honour its duplicates, which moves the pitch.
    // Flat must not be moved, so it snaps to the nearest member instead.
    if(RESTRICTIVE_POOLS.indexOf(c.notePoolMode) !== -1 && pool.length && chance(0.8))
      deg = snapToPool(deg, pool, c, contourMode === 0);
    prevDeg = deg;
    const art = articulationForStep(m, active, c, m);
    motifSteps.push({active, degree:active?deg:null, dur:1, ...art});
  }
  if(!motifSteps.some(s=>s.active)){
    motifSteps[0].active = true;
    motifSteps[0].degree = 0;
  }

  for(let i=0;i<steps;i++){
    const m = i % motif.len;
    const block = Math.floor(i / motif.len);
    let step = {...motifSteps[m]};
    if(block>0){
      if(chance(repTarget)) step = {...motifSteps[m]};
      else{
        // Flat keeps its pitch; the rhythm may still vary between blocks
        if(contourMode!==0 && step.active && step.degree!=null){
          const spread = c.repetitionPreset==='Highly varied' ? [-3,-2,-1,0,1,2,3] : [-2,-1,0,1,2];
          step.degree = clamp(step.degree + rand(spread), 0, registerCeiling(c));
        }
        if(chance(sp.motifVar + (1-repTarget)*0.25)) step.active = !step.active ? chance(0.38) : step.active;
      }
      if(contourMode!==0 && i >= Math.floor(steps*0.75) && step.active && chance(sp.stableEnd)) {
        step.degree = clamp(step.degree + rand([-1,0,1]), 0, 10);
      }
    }
    const styleAct = preferredStepActivation(cat, i, sp);
    const dens = Math.min(0.98, Math.max(0.05, (densTarget*0.6) + (styleAct*0.4)));
    if(chance(1-dens)) step.active = step.active && chance(dens);

    if(contourMode!==0 && step.active && chance(sp.rootBias*0.4) && i % Math.max(2,motif.len) === 0){
      step.degree = rand([0,0,2,4].filter(x=>x<c.scale.length));
    }
    if(cat==='house' && step.active && i%2===1 && chance(sp.offbeatBias)) step.gate = Math.max(step.gate||0.8, 0.95);
    // (the techno downbeat accent used to be added here; the ACCENT menu owns
    // how many accents there are, and postProcessArticulation moves them)
    if(!step.active) step.degree = null;
    out.push(step);
  }

  // CONTOUR shapes the whole phrase, and it has to be applied after the
  // repetition pass below - that clones the opening cell across most of the bar
  // and would otherwise wipe the shape straight back out. All bends are
  // positive and differ in shape rather than sign: degrees are clamped at zero,
  // so a downward bend on a phrase already near the bottom just flattens.
  const applyContourShape = function(ph){
    if(steps < 2) return;
    // Flat is enforced rather than coaxed. Half a dozen later passes can each
    // nudge a degree - the pool snap, the density fill, the repetition clone -
    // and plugging them one at a time kept missing one. Setting every step to
    // the pitch the phrase already favours is a guarantee instead.
    if(contourMode === 0){
      const tally = {};
      ph.forEach(function(s){ if(s.active && s.degree != null) tally[s.degree] = (tally[s.degree]||0)+1; });
      const keys = Object.keys(tally);
      if(!keys.length) return;
      let best = Number(keys[0]);
      keys.forEach(function(k){ if(tally[k] > tally[best]) best = Number(k); });
      ph.forEach(function(s){ if(s.active && s.degree != null) s.degree = best; });
      return;
    }

    let bend;
    if(contourMode === 1) bend = t => t * 4;                                  // rise
    else if(contourMode === -1) bend = t => (1 - t) * 4;                      // fall
    else if(contourMode === 2) bend = t => Math.sin(t * Math.PI) * 4;         // arch
    else if(contourMode === -2) bend = t => (1 - Math.sin(t * Math.PI)) * 4;  // dip
    else return;
    const ceilDeg = registerCeiling(c);
    for(let i=0;i<ph.length;i++){
      if(!ph[i].active || ph[i].degree == null) continue;
      ph[i].degree = clamp(Math.round(ph[i].degree + bend(i / (ph.length - 1))), 0, ceilDeg);
    }
  };

  if(c.repetitionPreset==='Very repetitive' || c.repetitionPreset==='Repetitive'){
    const cloneLen = Math.min(motif.len, Math.floor(steps/2));
    for(let i=cloneLen;i<steps;i++){
      if(chance(c.repetitionPreset==='Very repetitive' ? 0.9 : 0.72)){
        const src = out[i % cloneLen];
        out[i] = {...src, ...(articulationForStep(i, src.active, c, i%motif.len))};
      }
    }
  }

  applyContourShape(out);      // after the repetition clone, so it survives
  applyEnding(out, c, 'call');
  postProcessArticulation(out, c);
  return normalisePhrase(out);
}

function buildResponse(call,c){
  const steps = c.steps;
  const out = call.map(s=>({...s}));
  const rt = c.responseStrategy;
  const sp = styleProfile(c);
  const motif = motifFromCall(call, steps);
  const motifLen = motif.length;
  const activeDegrees = call.filter(s=>s.active && s.degree!=null).map(s=>s.degree);
  const avgDeg = activeDegrees.length ? activeDegrees.reduce((a,b)=>a+b,0)/activeDegrees.length : 0;

  let respPool = notePool(c);
  if(c.notePoolMode==='Pool expands on response') respPool = [...Array(Math.min(c.scale.length,7)).keys()];
  else if(c.notePoolMode==='Pool contracts on response') respPool = stableDegreesForScale(c);

  const callContour = phraseContour(call);
  const stable = stableDegreesForScale(c);
  const copyRhythm = call.map(s=>!!s.active);

  // CONTRAST macro: 0 keeps the response close to the call (an echo), 100 lets
  // it depart. responseTightness is the engine's "how much do we pull the
  // response back toward the call" figure, so it is the inverse of contrast.
  if(c.contrast !== undefined){
    sp.responseTightness = clamp(1 - c.contrast, 0.04, 0.96);
    sp.motifVar = clamp(sp.motifVar + c.contrast * 0.35, 0, 0.9);
    sp.leap = clamp(sp.leap + c.contrast * 0.25, 0, 0.9);
  }

  const mapMotif = (fn) => {
    for(let i=0;i<steps;i++){
      const base = motif[i % motifLen];
      out[i] = {...base};
      if(out[i].active && out[i].degree!=null){
        let d = fn(base.degree, i, base);
        if(chance(sp.responseTightness*0.28) && respPool.length){
          d = rand(respPool) + Math.floor(d / Math.max(1,c.scale.length)) * Math.max(1,c.scale.length);
        }
        out[i].degree = clamp(d, 0, 10);
      } else out[i].degree = null;
    }
  };

  // "Same rhythm" has to mean the same rhythm: which steps sound AND how long
  // they last. Copying only the on/off pattern and re-rolling the gates gave a
  // response with the same skeleton but different note lengths, which does not
  // sound like the same rhythm at all.
  const copyFeel = function(i){
    out[i].gate = call[i].gate;
    out[i].slide = call[i].slide;
    out[i].tie = call[i].tie;
    out[i].accent = call[i].accent;
  };

  if(rt==='Same rhythm + resolve'){
    for(let i=0;i<steps;i++){
      out[i].active = copyRhythm[i];
      out[i].degree = out[i].active ? clamp((call[i].degree??0) + rand([-1,0,0,1]),0,10) : null;
      if(out[i].active) copyFeel(i);
    }
  }
  else if(rt==='Same rhythm + new contour'){
    const dir = callContour >= 0 ? -1 : 1;
    for(let i=0;i<steps;i++){
      out[i].active = copyRhythm[i];
      out[i].degree = out[i].active ? clamp((call[i].degree??0) + dir*Math.floor(i/Math.max(2,steps/4)) + rand([-1,0,1]),0,10) : null;
      if(out[i].active) copyFeel(i);
    }
  }
  else if(rt==='Motif + simplify'){
    mapMotif((deg)=>deg + rand([-1,0,0,1]));
    for(let i=0;i<steps;i++){ out[i].active = out[i].active && chance(i%2===0 ? 1 : 0.35); if(!out[i].active) out[i].degree=null; }
  }
  else if(rt==='Motif + extend'){
    mapMotif((deg)=>deg + rand([-1,0,1]));
    for(let i=1;i<steps;i++){
      if(!out[i].active && out[i-1].active && chance(0.42)){ out[i].active=true; out[i].degree=out[i-1].degree; out[i].tie=true; out[i].gate=Math.max(out[i].gate||0.8,1.0); }
    }
  }
  else if(rt==='Motif + truncate'){
    mapMotif((deg)=>deg + rand([-1,0,1]));
    for(let i=Math.floor(steps*0.65); i<steps; i++){ if(chance(0.65)){ out[i].active=false; out[i].degree=null; } }
  }
  else if(rt==='Echo' || rt==='Delayed answer'){
    for(let i=0;i<steps;i++){
      const src = call[(i-2+steps)%steps];
      out[i].active = src.active;
      out[i].degree = out[i].active ? clamp((src.degree??0)+rand([-1,0,1]),0,10) : null;
    }
    if(rt==='Delayed answer'){
      for(let i=steps-1;i>0;i--) out[i] = {...out[i-1]};
      out[0] = {active:false, degree:null, gate:0.6, accent:false, slide:false, tie:false, dur:1};
    }
  }
  else if(rt==='Answer upward' || rt==='Higher answer') mapMotif((deg)=>deg + rand([1,2,3]));
  else if(rt==='Answer downward' || rt==='Lower answer') mapMotif((deg)=>deg - rand([1,1,2]));
  else if(rt==='Invert contour' || rt==='Mirror contour loosely') mapMotif((deg)=>Math.round(avgDeg - (deg-avgDeg)) + rand([-1,0,1]));
  else if(rt==='Copy rhythm / change notes'){
    for(let i=0;i<steps;i++){ out[i].active = copyRhythm[i]; out[i].degree = out[i].active ? clamp(rand(respPool)+rand([0,0,2]),0,10) : null; }
  }
  else if(rt==='Copy notes / change rhythm'){
    for(let i=0;i<steps;i++){ out[i].degree = call[i].degree; out[i].active = chance(call[i].active?0.68:0.22); if(!out[i].active) out[i].degree=null; }
  }
  else if(rt==='Transpose motif up') mapMotif((deg)=>deg + rand([2,3,4]));
  else if(rt==='Transpose motif down') mapMotif((deg)=>deg - rand([2,2,3]));
  else if(rt==='Response resolves to root'){
    mapMotif((deg)=>deg + rand([-1,0,1]));
    for(let i=Math.max(0,steps-3); i<steps; i++) if(out[i].active) out[i].degree = rand([0,0,2].filter(x=>x<c.scale.length));
  }
  else if(rt==='Response resolves to 5th'){
    mapMotif((deg)=>deg + rand([-1,0,1]));
    for(let i=Math.max(0,steps-3); i<steps; i++) if(out[i].active) out[i].degree = rand([4,2,0].filter(x=>x<c.scale.length));
  }
  else if(rt==='Response resolves lower'){
    mapMotif((deg)=>deg - rand([1,2]));
    for(let i=Math.max(0,steps-2); i<steps; i++) if(out[i].active) out[i].degree = clamp((out[i].degree??0)-1,0,10);
  }
  else if(rt==='Response resolves harder'){
    mapMotif((deg)=>deg + rand([-1,0,1]));
    for(let i=Math.max(0,steps-4); i<steps; i++) if(out[i].active){ out[i].degree = rand(stable.length?stable:[0]); out[i].accent = true; }
  }
  else if(rt==='Repeat then break'){
    mapMotif((deg)=>deg + rand([-1,0,1]));
    for(let i=0;i<steps-2;i++) if(chance(0.72)) out[i] = {...call[i]};
    if(out[steps-2].active) out[steps-2].degree = clamp((out[steps-2].degree??0)+rand([2,3,-2]),0,10);
    if(out[steps-1].active) out[steps-1].degree = rand(stable.length?stable:[0]);
  }
  else if(rt==='Tighter answer'){
    mapMotif((deg)=>deg + rand([-1,0,1]));
    for(let i=0;i<steps;i++){ if(out[i].active){ out[i].gate = 0.48 + (i%2===0 ? 0.04 : 0); out[i].accent = chance(0.3); } }
  }
  else if(rt==='Sparser answer'){
    mapMotif((deg)=>deg + rand([-1,0,1]));
    for(let i=0;i<steps;i++){ out[i].active = out[i].active && chance(i%2===0 ? 0.85 : 0.22); if(!out[i].active) out[i].degree=null; }
  }
  else if(rt==='Busier answer'){
    mapMotif((deg)=>deg + rand([-1,0,1]));
    for(let i=0;i<steps;i++){ if(!out[i].active && chance(0.3)){ out[i].active=true; out[i].degree = out[Math.max(0,i-1)].degree ?? rand(respPool); } }
  }
  else if(rt==='Accent the answer'){
    mapMotif((deg)=>deg + rand([-1,0,1]));
    for(let i=0;i<steps;i++) out[i].accent = out[i].active && (i%2===0 || chance(0.22));
  }
  else if(rt==='Slide the answer'){
    mapMotif((deg)=>deg + rand([-1,0,1]));
    for(let i=0;i<steps;i++){ out[i].slide = out[i].active && chance(0.34); out[i].gate = Math.max(out[i].gate||0.8, 1.0); }
  }
  else if(rt==='Staccato answer'){
    mapMotif((deg)=>deg + rand([-1,0,1]));
    for(let i=0;i<steps;i++){ out[i].gate = 0.42; out[i].tie=false; }
  }
  else if(rt==='Legato answer'){
    mapMotif((deg)=>deg + rand([-1,0,1]));
    for(let i=0;i<steps;i++){ out[i].gate = 1.08; out[i].tie = out[i].active && chance(0.18); }
  }
  else if(rt==='Half-phrase answer'){
    mapMotif((deg)=>deg + rand([-1,0,1]));
    for(let i=0;i<Math.floor(steps/2); i++){ out[i].active=false; out[i].degree=null; }
  }
  else if(rt==='Fragment reply' || rt==='Tail answer'){
    const start = rt==='Tail answer' ? Math.floor(steps/2) : Math.floor(steps/3);
    for(let i=0;i<steps;i++){
      const src = call[(start+i)%steps];
      out[i].active = src.active && chance(0.88);
      out[i].degree = out[i].active ? clamp((src.degree??0)+rand([-1,0,1]),0,10) : null;
    }
  }
  else if(rt==='Opposite register') mapMotif((deg)=>deg + (c.registerProfile==='Wide register' ? -3 : 3));
  else if(rt==='Same motif darker') mapMotif((deg)=>deg - 1);
  else if(rt==='Same motif brighter') mapMotif((deg)=>deg + 1);
  else if(rt==='Question then closure'){
    mapMotif((deg)=>deg + rand([-1,0,1]));
    const qEnd = [1,3,5].filter(x=>x<c.scale.length);
    if(out[steps-2] && out[steps-2].active) out[steps-2].degree = rand(qEnd.length?qEnd:[1]);
    if(out[steps-1] && out[steps-1].active) out[steps-1].degree = rand(stable.length?stable:[0]);
  }
  else if(rt==='Hook then fill'){
    for(let i=0;i<steps;i++){
      if(i < Math.floor(steps/2)) out[i] = {...call[i]};
      else{
        const src = motif[i % motifLen];
        out[i] = {...src};
        if(out[i].active && out[i].degree!=null) out[i].degree = clamp(src.degree + rand([-2,-1,1,2]),0,10);
      }
    }
  }
  else if(rt==='Call dominant / response tonic'){
    // The response is the tonic half: it sits on the root. The call is moved
    // onto the fifth in generate(), since a response strategy cannot reach it.
    mapMotif((deg)=>deg + rand([-1,0,1]));
    for(let i=0;i<steps;i++){
      if(!out[i].active) continue;
      out[i].degree = chance(0.78) ? 0 : rand([0,0,2].filter(x=>x<c.scale.length));
    }
  }
  else mapMotif((deg)=>deg + rand([-1,0,1]));

  for(let i=0;i<steps;i++){
    if(out[i].active && call[i] && call[i].active && chance(sp.responseTightness)){
      out[i].degree = clamp(Math.round((out[i].degree + call[i].degree)/2), 0, 10);
    }
    const a = articulationForStep(i, out[i].active, c, i%motifLen);
    out[i] = {...out[i], ...a, gate: out[i].gate ?? a.gate};
    if(!out[i].active) out[i].degree = null;
  }

  // CONTRAST macro, second half: above the midpoint it also breaks the rhythm
  // away from the call, below it pulls the rhythm back toward it. Without this
  // the macro did nothing on the strategies that copy the call's rhythm
  // verbatim, which is most of them.
  if(c.contrast !== undefined){
    if(c.contrast > 0.5){
      const p = (c.contrast - 0.5) * 2 * 0.4;
      for(let i=0;i<steps;i++){
        if(!chance(p)) continue;
        if(out[i].active){ out[i].active = false; out[i].degree = null; }
        else {
          out[i].active = true;
          const near = out[Math.max(0,i-1)].degree;
          out[i].degree = (near != null) ? near : rand(respPool);
        }
      }
    } else if(c.contrast < 0.5) {
      const p = (0.5 - c.contrast) * 2 * 0.6;
      for(let i=0;i<steps;i++){
        if(!chance(p) || !call[i]) continue;
        if(!!out[i].active === !!call[i].active) continue;
        out[i].active = !!call[i].active;
        if(out[i].active && out[i].degree == null) out[i].degree = call[i].degree;
        if(!out[i].active) out[i].degree = null;
      }
    }
  }

  // 'Pool expands / contracts on response' previously only showed up on the
  // strategies that pick fresh pitches; most strategies transform the call
  // instead, so the setting did nothing. Snap the response onto its pool so
  // the choice is audible whichever strategy is running.
  if((c.notePoolMode === 'Pool contracts on response' ||
      c.notePoolMode === 'Pool expands on response') && respPool.length){
    const len = Math.max(1, c.scale.length);
    for(let i=0;i<steps;i++){
      if(!out[i].active || out[i].degree == null) continue;
      if(!chance(0.75)) continue;
      const oct = Math.floor(out[i].degree / len), within = out[i].degree - oct * len;
      let best = respPool[0], bd = 99;
      for(let k=0;k<respPool.length;k++){
        const d = Math.abs((respPool[k] % len) - within);
        if(d < bd){ bd = d; best = respPool[k] % len; }
      }
      out[i].degree = clamp(best + oct * len, 0, 11);
    }
  }

  applyEnding(out,c,'response');
  // a copied rhythm keeps the call's articulation; re-processing would move it
  if(rt !== 'Same rhythm + resolve' && rt !== 'Same rhythm + new contour')
    postProcessArticulation(out,c);
  return normalisePhrase(out);
}

// DENSITY sets how many steps sound. Mutation moves notes around; it has no
// business changing the count, so whatever it adds or removes is balanced out
// again here.
// A new phrase invalidates what the old steps remembered.
// The last word on ties and slides, settled together because they compete for
// the same pair of notes. Density can insert a note between a pair and the
// octave jump can move one of them, so this has to run after everything else -
// and it compares the pitch that actually plays, not the scale degree, since a
// jump can put two different degrees on one pitch.
function enforceTies(ph, c){
  const pitchOf = function(s){
    if(!s || !s.active || s.degree == null) return null;
    return foldPitch(degreeToMidi(s.degree + (s.oct || 0) * c.scale.length, c));
  };
  const nextActive = function(i){
    let j = i + 1;
    while(j < ph.length && !ph[j].active) j++;
    return j < ph.length ? j : -1;
  };

  for(let i=0;i<ph.length-1;i++){
    const s = ph[i];
    if(!s || !s.active) continue;

    // a step cannot both hold and glide
    if(s.tie && s.slide) s.tie = false;

    const j = nextActive(i);
    if(j < 0){ s.tie = false; s.slide = false; continue; }

    if(s.tie){
      // a tie holds one pitch: bring the next note onto it
      ph[j].degree = s.degree;
      ph[j].oct = s.oct || 0;
      continue;
    }

    if(s.slide){
      // and it has to be the very next step: a glide across a rest is not a
      // glide, the note has already stopped by the time the next one starts
      if(j !== i + 1){ s.slide = false; continue; }
      // a slide has to reach a DIFFERENT pitch or it is silent
      if(pitchOf(ph[j]) !== pitchOf(s)) continue;
      const ceilDeg = registerCeiling(c);
      ph[j].oct = s.oct || 0;                    // move by step, not by octave
      const up = clamp(s.degree + 1, 0, ceilDeg);
      const dn = clamp(s.degree - 1, 0, ceilDeg);
      ph[j].degree = (up !== s.degree) ? up : dn;
      // nowhere to move: drop the mark rather than show a glide that is silent
      if(pitchOf(ph[j]) === pitchOf(s)) s.slide = false;
    }
  }
}

function forgetDensityMemory(ph){
  for(let i=0;i<ph.length;i++){ ph[i].was = null; ph[i].rank = Math.random(); }
  ph.__silenced = [];
}

function restoreDensity(phrase, c){
  const want = densityTargetCount(c);
  const cat0 = styleCategory(c.style);
  const sp0 = styleProfile(c);
  for(let i=0;i<phrase.length;i++) if(phrase[i].rank === undefined) phrase[i].rank = Math.random();
  const wt = function(i){ return preferredStepActivation(cat0, i, sp0) + phrase[i].rank * 0.15; };
  const idx = [];
  for(let i=0;i<phrase.length;i++) idx.push(i);
  let live = phrase.filter(s=>s.active).length;
  let guard = 0;
  while(live < want && guard++ < 200){
    const off = idx.filter(i=>!phrase[i].active);
    if(!off.length) break;
    // strongest position first, so mutation keeps the groove
    off.sort(function(a,b){ return wt(b) - wt(a); });
    const i = off[0];
    phrase[i].active = true;
    // take the pitch from a neighbour rather than the pool, the way applyDensity
    // does - a random pool note here broke Flat, which should hold one pitch
    if(phrase[i].degree == null){
      let common = 0, tally = {};
      phrase.forEach(function(x){ if(x.active && x.degree != null) tally[x.degree] = (tally[x.degree]||0)+1; });
      Object.keys(tally).forEach(function(k){ if(!tally[common] || tally[k] > tally[common]) common = Number(k); });
      const near = (i > 0 && phrase[i-1].degree != null) ? phrase[i-1].degree
                 : ((i < phrase.length-1 && phrase[i+1].degree != null) ? phrase[i+1].degree : common);
      phrase[i].degree = near;
    }
    const a = articulationForStep(i, true, c, i % 4);
    phrase[i].gate = a.gate; phrase[i].accent = a.accent;
    phrase[i].slide = a.slide; phrase[i].tie = a.tie;
    live++;
  }
  while(live > want && guard++ < 200){
    const on = idx.filter(i=>phrase[i].active);
    if(on.length <= 1) break;
    on.sort(function(a,b){ return wt(a) - wt(b); });   // weakest position first
    const i = on[0];
    phrase[i].active = false; phrase[i].degree = null;
    live--;
  }
}

function mutatePhrase(phrase,c,target='Whole phrase',stepsOverride){
  if(target==='Call + response linked') target='Whole phrase';
  const out = phrase.map(s=>({...s}));
  // per-lane drift passes its own step count; otherwise the VARIATION menu decides
  const amt = (stepsOverride != null)
    ? clamp(Math.round(stepsOverride), 1, out.length)
    : ({'Tiny':1,'Small':2,'Medium':3,'Strong':5,'Brutal':Math.max(4, Math.floor(out.length*0.7))}[c.mutationAmount] || 3);
  const idxs = pickN([...Array(out.length).keys()], amt);
  idxs.forEach(i=>{
    if(target==='Rhythm only' || target==='Whole phrase' || target==='Call only' || target==='Response only'){
      if(chance(0.7)) out[i].active = !out[i].active;
    }
    if((target==='Pitch only' || target==='Whole phrase' || target==='Call only' || target==='Response only') && out[i].active){
      out[i].degree = clamp((out[i].degree??0)+rand([-2,-1,1,2]),0,10);
    }
    if(target==='Articulation only' || target==='Whole phrase'){
      const a = articulationForStep(i, out[i].active, c, i%4);
      out[i] = {...out[i], ...a};
    }
    if(target==='Ending only' && i===out.length-1) applyEnding(out,c,'call');
    if(target==='Motif only'){
      const src = i % Math.max(2, Math.floor(out.length/4));
      out[i] = {...out[src]};
    }
    if(!out[i].active) out[i].degree = null;
  });
  // mutate changes WHICH steps sound, not how many - DENSITY owns the count
  restoreDensity(out, c);
  settlePhrase(out, c);
  return out;
}

// RECONSTRUCTED: shift was not visible in the source I read, but "Shift ←/→"
// on a fixed-length step phrase is an array rotation.
function shiftPhrase(phrase, dir){
  const n = phrase.length;
  if(n < 2) return phrase.map(s=>({...s}));
  const out = new Array(n);
  for(let i=0;i<n;i++) out[i] = {...phrase[(((i + dir) % n) + n) % n]};
  return out;
}

// ---------------------------------------------------------------------------
// 4. Device state
// ---------------------------------------------------------------------------

const S = {
  params: Object.assign({}, DEFAULTS),
  call: [],
  response: [],
  enabled: 1,   // always on; the bypass toggle was removed
  deviceActive: 1, // Live's Device Activator also gates the companion buses
  locks: {},    // param name -> 1 if randomisation should skip it
  playing: false,
  stepIdx: 0,
  passIdx: 0,
  cellCounter: 0,
  activeNotes: {},      // "pitch:chan:destination" -> original note destination
  autoRegen: 1,
  // 'restart'  phrase starts at step 1 wherever you press play (default)
  // 'timeline' step derived from absolute song position, so the phrase is
  //            pinned to the arrangement and starting mid-way lands mid-phrase
  // 'free'     counter advancing one step per metro bang
  syncMode: 'restart',
  origin: -1,
  finished: false,
  lastT: -1,
  lastIdx: -1,
  lastBangMs: -1,
  lastSongT: -1e9,
  verbose: 0,           // 'verbose 1' to hear about a normal load
  loopOn: 0,
  loopLen: 0,
  sinceWrap: 0,
  wrapped: false,
  queuedPair: 0,         // true for either kind of pending full-pair change
  queuedAction: '',      // 'pair' or 'dice'; never stored with the Live set
  restoring: false,
  followLive: 0,          // follow Live 12's song scale
  liveRoot: -1,
  liveIntervals: null,
  liveScaleName: '',
  scaleUnavailable: 0,
  mute: { call: 0, response: 0 }
};

function cfg(){
  const p = S.params;
  return {
    style: p.style,
    responseStrategy: p.responseStrategy,
    motifType: p.motifType,
    phraseMode: p.phraseMode,
    endingBehaviour: p.endingBehaviour,
    key: (S.followLive && S.liveRoot >= 0)
      ? NOTE_NAMES[((S.liveRoot % 12) + 12) % 12]
      : p.key,
    scaleName: (S.followLive && S.liveIntervals) ? (S.liveScaleName || 'Live scale') : p.scale,
    scale: (S.followLive && S.liveIntervals)
      ? S.liveIntervals
      : (SCALE_INTERVALS[p.scale] || SCALE_INTERVALS['Minor Pentatonic']),
    rootOctave: p.rootOctave,
    steps: Number(p.steps),
    registerProfile: p.registerProfile,
    densityPreset: p.densityPreset,
    contourPreset: p.contourPreset,
    repetitionPreset: p.repetitionPreset,
    mutationAmount: p.mutationAmount,
    mutationTarget: p.mutationTarget,
    notePoolMode: p.notePoolMode,
    articulationProfile: p.articulationProfile,
    gateFeel: p.gateFeel,
    connectionFeel: p.connectionFeel,
    slideProfile: p.slideProfile,
    accentProfile: p.accentProfile,
    noteLengthPct: Number(p.noteLengthPct),
    // performance macros, 0..1
    densityTarget: clamp(Number(p.density) / 100, 0, 1),
    contrast: clamp(Number(p.contrast) / 100, 0, 1),
    resolve: clamp(Number(p.resolve) / 100, 0, 1),
    octaveJump: clamp(Number(p.octaveJump) / 100, 0, 1),
    callChannel: DATA.channels.indexOf(p.callChannel),
    responseChannel: DATA.channels.indexOf(p.responseChannel)
  };
}

function status(txt){
  if(S.preparing) return;
  outlet(O_STATUS, txt);          // legacy: [prepend set] -> live.comment
  outlet(O_UI, 'status', txt);    // and the display footer, which is always there
}

// --- display feed ----------------------------------------------------------
// One message per lane: name, count, then 5 values per step. Pitch is the
// rendered pitch (octave-folded) and gate is the rendered duration, so the
// display shows what actually plays rather than the raw phrase data.
// Many things change a phrase in one tick - init alone used to fire this
// ~90 times. Coalesce onto a single deferred repaint.
let displayTask = null;
function pushDisplay(){
  if(S.preparing) return;
  if(nativeEnabled) nativeRefresh();
  rememberState();
  if(!initDone) return;
  if(!displayTask) displayTask = new Task(sendDisplayNow);
  displayTask.cancel();
  displayTask.schedule(30);
}

// Controls mirrored in the v8ui, grouped by the page they appear on.
// Order within each list is left-to-right in the control row.
const PAGES = {
  header:       ['rootOctave','key','scale','steps','rate','swing'],
  generate:     ['style','motifType','responseStrategy','phraseMode','endingBehaviour'],
  shape:        ['registerProfile','contourPreset','repetitionPreset',
                 'notePoolMode','resolve'],
  articulation: ['articulationProfile','gateFeel','connectionFeel','slideProfile',
                 'accentProfile','baseVelocity','accentDepth'],
  options:      ['callOutput','responseOutput','syncMode','mutationAmount'],
  optionsMore:  ['newPairTiming','clipTarget','clipLength'],
  panel:        ['density','mutateAll','octaveJump']
};

const CTRL_LABELS = {
  key:'KEY', scale:'SCALE', steps:'STEPS',
  style:'STYLE', motifType:'MOTIF', responseStrategy:'ANSWER',
  phraseMode:'MODE', endingBehaviour:'ENDING',
  registerProfile:'REGISTER', densityPreset:'DENSITY', contourPreset:'CONTOUR',
  repetitionPreset:'REPETITION', notePoolMode:'NOTE POOL', mutationAmount:'VARIATION',
  articulationProfile:'PROFILE', gateFeel:'GATE', connectionFeel:'JOIN',
  slideProfile:'SLIDE', accentProfile:'ACCENT', noteLengthPct:'NOTE LENGTH',
  swing:'SWING', density:'DENSITY', contrast:'CONTRAST', resolve:'RESOLVE',
  accentDepth:'ACCENT AMT', baseVelocity:'VELOCITY',
  octaveJump:'OCT JUMP', driftCall:'MUTATE', driftResponse:'MUTATE', mutateAll:'MUTATE',
  rate:'RATE', rootOctave:'OCT', callChannel:'CALL CH', responseChannel:'RESP CH',
  callOutput:'CALL OUT', responseOutput:'RESP OUT',
  newPairTiming:'NEW PAIR', clipLength:'LOOP LENGTH',
  clipTarget:'CLIP', mutationTarget:'MUTATE TARGET', syncMode:'SYNC',

};

// Short ordered lists read better as -/+ steppers than as dropdowns.
const STEPPER_CTRLS = ['steps','rate','rootOctave','callChannel','responseChannel','syncMode','newPairTiming','clipLength'];
const BUTTON_CTRLS = [];
// the three performance macros, drawn as knobs
const KNOB_CTRLS = ['density','mutateAll','octaveJump'];

function ctrlKind(name){
  if(BUTTON_CTRLS.indexOf(name) !== -1) return 'button';
  if(KNOB_CTRLS.indexOf(name) !== -1) return 'knob';
  if(NUM_PARAMS.indexOf(name) !== -1) return 'number';
  if(STEPPER_CTRLS.indexOf(name) !== -1) return 'stepper';
  return 'menu';
}

// A lock is only meaningful on something the dice can actually change.
function isLockable(name){
  return !!ENUM_PARAMS[name] && DICE_SKIP.indexOf(name) === -1;
}

const NUM_RANGES = {
  noteLengthPct: [40, 200, 5],
  swing:         [0, 75, 5],
  octaveJump:    [0, 100, 5],
  driftCall:     [0, 100, 5],
  driftResponse: [0, 100, 5],
  mutateAll:     [0, 100, 5],
  density:       [0, 100, 2],
  contrast:      [0, 100, 2],
  resolve:       [0, 100, 2],
  baseVelocity:  [1, 127, 1],
  accentDepth:   [0, 100, 5],
  baseVelocity:  [1, 127, 1]
};

function allCtrls(){
  let out = [];
  for(const p in PAGES) out = out.concat(PAGES[p]);
  return out;
}

// What the dice rerolls: every enum on the four pages except the ones where a
// random value is disorienting rather than musical.
const DICE_SKIP = ['rate','callChannel','responseChannel','clipTarget','steps','key','scale',
                   'syncMode','rootOctave','mutationAmount','callOutput','responseOutput','newPairTiming','clipLength'];
function diceMenus(){
  return allCtrls().filter(function(n){
    return ENUM_PARAMS[n] && DICE_SKIP.indexOf(n) === -1;
  });
}

function sendMenuItems(){
  for(const page in PAGES){
    const list = PAGES[page];
    for(let i=0;i<list.length;i++){
      const name = list[i];
      const kind = ctrlKind(name);
      if(kind === 'button'){
        outlet(O_UI, 'ctrl', name, page, i, CTRL_LABELS[name] || name, kind, 0);
      } else if(kind === 'number' || kind === 'knob'){
        const r = NUM_RANGES[name] || [0, 127, 1];
        outlet(O_UI, 'ctrl', name, page, i, CTRL_LABELS[name] || name, kind, r[0], r[1], r[2]);
      } else {
        const items = DATA[ENUM_PARAMS[name]];
        outlet(O_UI, 'ctrl', name, page, i, CTRL_LABELS[name] || name, kind, items.length);
        for(let k=0;k<items.length;k++) outlet(O_UI, 'ctrlitem', name, k, items[k]);
      }
      outlet(O_UI, 'ctrllockable', name, isLockable(name) ? 1 : 0);
      outlet(O_UI, 'ctrlreroll', name, REROLLABLE.indexOf(name) !== -1 ? 1 : 0);
    }
  }
  sendMenuValues();

  // The two drift sliders sit under the lanes rather than on a page, so the
  // loop above does not reach them, but the display still needs their range.
  ['driftCall','driftResponse'].forEach(function(name, i){
    const r = NUM_RANGES[name];
    outlet(O_UI, 'ctrl', name, 'lane', i, CTRL_LABELS[name] || name, 'number', r[0], r[1], r[2]);
    outlet(O_UI, 'ctrlnum', name, Number(S.params[name]));
  });
}

function sendMenuValues(){
  if(S.preparing) return;
  if(nativeEnabled) nativeConfigure();
  rememberState();
  outlet(5,'outputs',S.params.callOutput==='Companion'?1:0,S.params.responseOutput==='Companion'?1:0);
  outlet(O_UI,'queued',S.queuedPair?1:0,S.queuedAction||'');
  const all = allCtrls();
  for(let i=0;i<all.length;i++){
    const name = all[i];
    const k = ctrlKind(name);
    if(k === 'button') continue;          // action rows carry no value
    if(k === 'number' || k === 'knob'){
      outlet(O_UI, 'ctrlnum', name, Number(S.params[name]));
    } else {
      const items = DATA[ENUM_PARAMS[name]];
      const idx = items.indexOf(S.params[name]);
      outlet(O_UI, 'ctrlval', name, idx < 0 ? 0 : idx);
    }
    outlet(O_UI, 'ctrllock', name, S.locks[name] ? 1 : 0);
  }
  // Only claim a key once one has actually been read. The old expression turned
  // the -1 'not read' marker into B, which looked like a real answer.
  const haveScale = (S.liveRoot >= 0) && !!S.liveIntervals;
  outlet(O_UI, 'livescale', S.followLive ? 1 : 0,
         (S.followLive && haveScale) ? NOTE_NAMES[S.liveRoot % 12] : '-',
         S.followLive ? (haveScale ? (S.liveScaleName || 'Live scale')
                       : (S.scaleUnavailable ? 'needs Live 12' : 'waiting for Live')) : '-');
  outlet(O_UI, 'lanestate', 'call', S.mute.call, 'response', S.mute.response);
  outlet(O_UI, 'transport', liveGet('is_playing', 0));
  outlet(O_UI, 'autoregen', S.autoRegen ? 1 : 0);
  // which lanes the phrase mode mutates itself, so the display can grey out
  // the drift slider that would otherwise fight it
  const pl = passPlan(S.params.phraseMode);
  let vc = 0, vr = 0;
  if(pl.vary) for(let i=0;i<pl.vary.length;i++){
    if(!pl.vary[i]) continue;
    const l = pl.seq ? pl.seq[i] : C;
    if(l === C) vc = 1; else vr = 1;
  }
  outlet(O_UI, 'driftlock', vc, vr);
}

function setmenu(name, idx){
  ensureInit();
  const list = DATA[ENUM_PARAMS[name]];
  if(!list){ status('Unknown control: ' + name); return; }
  const i = clamp(Math.round(idx), 0, list.length - 1);
  setEnum(name, list[i]);
  param(name, i, true);
  sendMenuValues();
}

function setnum(name, value, mode){
  ensureInit();
  const r = NUM_RANGES[name];
  if(!r){ status('Unknown numeric control: ' + name); return; }
  const force = (mode === true);
  let v = clamp(Number(value), r[0], r[1]);
  if(v === Number(S.params[name]) && initDone && !force) return;   // same reason as param()

  // DENSITY maps a 0-100 dial onto a whole number of steps, so consecutive
  // notches could land on the same count and the knob appeared to do nothing.
  // Skipping ahead to the next distinct count fixes that for a nudge - but it
  // must not apply to an absolute value from a macro, a numbox or automation,
  // which would otherwise jump a whole count's worth (~20%) per move.
  if(name === 'density' && mode === 'nudge' && !S.restoring){
    const dir = (v > Number(S.params.density)) ? 1 : (v < Number(S.params.density) ? -1 : 0);
    if(dir){
      const target = function(x){
        const st = Number(S.params.steps) || 8;
        return clamp(Math.round(st * (0.12 + clamp(x/100,0,1) * 0.88)), 1, st);
      };
      const was = target(Number(S.params.density));
      let guard = 0;
      while(target(v) === was && v > r[0] && v < r[1] && guard++ < 60) v = clamp(v + dir * r[2], r[0], r[1]);
    }
  }
  S.params[name] = v;
  outlet(O_SETP, 'setparam', name, S.params[name]);
  outlet(O_UI, 'ctrlnum', name, S.params[name]);
  if(S.restoring){ pushDisplay(); return; }
  if(REGEN_PARAMS.indexOf(name) !== -1)        generate('both', true);
  else if(RESOLVE_PARAMS.indexOf(name) !== -1)  reapplyEnding();
  else if(DENSITY_PARAMS.indexOf(name) !== -1)  applyDensity();
  else if(OCTJUMP_PARAMS.indexOf(name) !== -1)  applyOctaveJump();
  else if(ARTIC_PARAMS.indexOf(name) !== -1)    reapplyArticulation();
  else if(name === 'mutateAll'){
    // one knob for both lanes; the per-lane sliders stay usable underneath
    S.params.driftCall = v;
    S.params.driftResponse = v;
    outlet(O_SETP, 'setparam', 'driftCall', v);
    outlet(O_SETP, 'setparam', 'driftResponse', v);
    outlet(O_UI, 'ctrlnum', 'driftCall', v);
    outlet(O_UI, 'ctrlnum', 'driftResponse', v);
  }
  else if(RENDER_PARAMS.indexOf(name) !== -1)   { /* render-time only */ }
  pushDisplay();
}

// Per-lane mute. The step still advances, it just doesn't sound, so the two
// lanes stay in phase with each other.
function mute(laneName, v){
  ensureInit();
  if(laneName !== 'call' && laneName !== 'response') return;
  S.mute[laneName] = v ? 1 : 0;
  if(nativeEnabled) nativeConfigure();
  rememberState();
  if(S.mute[laneName]) allNotesOff();
  outlet(O_UI, 'lanestate', 'call', S.mute.call, 'response', S.mute.response);
  status((S.mute[laneName] ? 'Muted ' : 'Unmuted ') + laneName);
}

// Start or stop Live's transport from the header play button.
function transport(){
  ensureInit();
  const a = song();
  if(!a){ status('No Live transport'); return; }
  try {
    if(liveGet('is_playing', 0) == 1) a.call('stop_playing');
    else a.call('start_playing');
  } catch(e){ status('Transport failed: ' + e.message); }
  // the is_playing observer will confirm, but echo immediately so the button
  // does not sit stale if the observer is slow
  outlet(O_UI, 'transport', liveGet('is_playing', 0));
}

// The pitch axis the display should use. Derived from the configuration, not
// from the notes: buildCall clamps degrees to this ceiling, so the axis covers
// everything that can be generated and stays put when a lane is regenerated.
// It only moves when key, scale, octave or register actually change.
function displayRange(c){
  const regMax = registerCeiling(c);
  let top = Math.max(regMax, 10);   // response strategies clamp to 10
  // Reserve the octave-jump headroom whether or not any jumps are in use. It
  // used to be added only when the knob was above zero, so the first turn of
  // OCT JUMP widened the range and every note on screen - including the ones
  // that had not jumped - was redrawn at a different height.
  top += c.scale.length;
  let lo = degreeToMidi(0, c) - 12;   // and the same room below, for drops
  let hi = degreeToMidi(top, c);
  if(hi > 127) hi = 127;
  if(lo < 0) lo = 0;
  if(hi - lo < 12) hi = Math.min(127, lo + 12);
  return [lo, hi];
}

function sendDisplayNow(){
  if(!initDone) return;
  const c = cfg();
  const r = displayRange(c);
  outlet(O_UI, 'range', r[0], r[1]);
  outlet(O_UI, 'swing', Number(S.params.swing) || 0);
  sendMenuValues();
  const send = function(name, phrase, laneCode){
    const msg = ['lane', name, phrase.length];
    // Never let a non-number out: a null pitch downstream becomes NaN geometry.
    for(let i=0;i<phrase.length;i++){
      const s = phrase[i];
      const ev = stepEvent(phrase, i, c, laneCode);
      const p = (ev && typeof ev.pitch === 'number' && isFinite(ev.pitch)) ? ev.pitch : -1;
      msg.push(p,
               s.accent ? 1 : 0,
               s.slide ? 1 : 0,
               s.tie ? 1 : 0,
               ev ? Math.round(ev.dur * 100) : 0);
    }
    outlet(O_UI, msg);
  };
  // A form mode plays stored variants, so draw those rather than the phrase
  // they were derived from - otherwise B and C never appear on screen.
  const shown = S.shownVariant || {};
  send('call', shown.call || S.call, C);
  send('response', shown.response || S.response, R);
}

// ---------------------------------------------------------------------------
// 5. Phrase modes  [RECONSTRUCTED]
//
// The web app plays a browser-side preview; the source for how phraseMode maps
// onto that loop wasn't in the part of index.html I could read. This is my
// implementation of the documented names. Each mode returns either a per-pass
// lane sequence, or a cell-interleave spec for the modes that alternate inside
// a single pass.
// ---------------------------------------------------------------------------

const C = 'call', R = 'response';

function passPlan(mode){
  switch(mode){
    case 'Call only':                          return { seq:[C] };
    case 'Response only':                      return { seq:[R] };
    case 'Call then response':                 return { seq:[C,R] };
    case 'Response then call':                 return { seq:[R,C] };
    case 'Alternate half bars':                return { interleave:'half' };
    case 'Alternate half bars, response first': return { interleave:'half', flip:true };
    case 'Alternate 2-step cells':             return { interleave:2 };
    case 'Alternate 1-step cells':             return { interleave:1 };
    case 'Call, call, response':               return { seq:[C,C,R] };
    case 'Call, response, response':           return { seq:[C,R,R] };
    case 'Call, varied call, response':        return { seq:[C,C,R], vary:[false,true,false] };
    case 'Call, response, varied response':    return { seq:[C,R,R], vary:[false,false,true] };
    // A form: each slot names a variant, and the same name always plays the
    // same phrase. Built once, then replayed - that is what makes it a form
    // rather than a drift.
    case 'A / B / A / C':                      return { seq:[C,R,C,R], form:['A','B','A','C'] };
    case "A / A' / B / B'":                    return { seq:[C,C,R,R], form:['A',"A'",'B',"B'"] };
    case 'Call with response fill':           return { seq:[C,C,C,R] };
    case 'Mutating call, stable response':     return { seq:[C,R], vary:[true,false] };
    case 'Stable call, mutating response':     return { seq:[C,R], vary:[false,true] };
    case 'Both mutate slowly':                 return { seq:[C,R], vary:[true,true], varyEvery:4 };
    case 'Call sparse, response busy':         return { seq:[C,R] };
    case 'Call busy, response sparse':         return { seq:[C,R] };
    default:                                   return { seq:[C,R] };
  }
}

// Which lane owns step `i` of the current pass.
function laneForStep(plan, pass, i, steps){
  if(plan.interleave !== undefined){
    const cell = plan.interleave === 'half' ? Math.max(1, Math.floor(steps/2)) : plan.interleave;
    const first = (Math.floor(i / cell) % 2 === 0);
    return (first !== !!plan.flip) ? C : R;   // flip starts on the response
  }
  return plan.seq[pass % plan.seq.length];
}

// ---------------------------------------------------------------------------
// 6. Note rendering
//
// gate      -> note length in steps (>1 overlaps the next step, which is what
//              makes a mono synth glide — that's how slides are realised)
// accent    -> lifted toward 127 by ACCENT AMT
// tie       -> extend over the next step; if the next step is the same pitch,
//              suppress its note-on so the two become one note
// ---------------------------------------------------------------------------

// Two-note scales ("Power Fifths", "Root + b7") span an octave per two
// degrees, so a high base octave plus a wide register can push degree 11 up to
// MIDI 153. Clamping would pile every overflowing note onto 127; folding by
// octaves keeps the pitch class and the shape of the line intact.
function foldPitch(m){
  while(m > 127) m -= 12;
  while(m < 0) m += 12;
  return m;
}

// Swing pushes every second step later by up to half a step.
function swingOffset(i, stepBeats){
  const sw = Number(S.params.swing) || 0;
  if(!sw || (i % 2) === 0) return 0;
  return stepBeats * (sw / 100) * 0.5;
}

// Reused rather than allocated per note; eight is far more than the number of
// swung notes that can ever be in flight at once.
const SWING_TASKS = [];
let swingIdx = 0;
function swingTask(){
  if(SWING_TASKS.length < 8){
    const t = new Task(function(){
      const e = t.__ev;
      if(!e || !S.deviceActive || !S.enabled || S.mute[e.lane]) return;
      // A swung note fires after bang() has moved on. Keep its original lane;
      // its duration already includes any subsequent tied steps.
      sendNote(e.p, e.v, e.c, e.d, e.lane);
    });
    SWING_TASKS.push(t);
    return t;
  }
  const t = SWING_TASKS[swingIdx];
  swingIdx = (swingIdx + 1) % SWING_TASKS.length;
  t.cancel();
  return t;
}

function stepEvent(phrase, i, c, lane){
  const s = phrase[i];
  if(!s || !s.active || s.degree == null) return null;
  const pitch = foldPitch(degreeToMidi(s.degree + (s.oct || 0) * c.scale.length, c));
  if(typeof pitch !== 'number' || !isFinite(pitch)) return null;   // no pitch, no note
  const base = clamp(Number(S.params.baseVelocity) || 92, 1, 127);
  // ACCENT AMT is how far an accent travels from the base velocity toward full:
  // 0% and an accent is no louder at all, 100% and it hits 127.
  const depth = clamp(Number(S.params.accentDepth) || 0, 0, 100) / 100;
  const vel = s.accent ? Math.round(clamp(base + (127 - base) * depth, 1, 127)) : base;

  let gateVal = (s.gate || 0.82) * (c.noteLengthPct / 100);
  const next = phrase[i+1];
  const nextPitch = (next && next.active && next.degree != null)
    ? foldPitch(degreeToMidi(next.degree + (next.oct || 0) * c.scale.length, c)) : null;
  // The original stretched any repeated pitch to 1.18 so it "felt tied". Real
  // 303 patterns do not do that - repeated notes there stay at a quarter or a
  // half of a step - and it was a large part of why everything ran long. A
  // repeat is only held when it is actually tied.
  // A tie or a slide holds the note into the next step. Sting writes these as
  // 0.96 to 1.08 of a step, a single cluster - multiplying the gate instead
  // smeared them across everything between a half and a whole.
  let dur = gateVal;
  if(s.tie) dur = 1.0 + Math.random() * 0.10;
  // The source glides the preview oscillator on a slide but writes nothing
  // special into the MIDI file, so slides vanish on export. Overlapping the
  // next note-on is the MIDI equivalent: it's what makes a mono synth glide.
  // The earlier check compares scale degrees, but an octave jump can land two
  // different degrees on the same pitch - and a glide from a note to itself is
  // silent. Checked here, where the real pitch is known.
  if(s.slide && nextPitch != null && nextPitch !== pitch) dur = Math.max(dur, 1.05);
  // A note that is neither slid nor tied must stop before the next step. This
  // is what gives a synth's release something to act on.
  if(!s.slide && !s.tie) dur = Math.min(dur, 0.95);

  return { pitch, vel, dur: Math.max(0.08, dur),
           chan: lane === C ? c.callChannel : c.responseChannel };
}

// Flatten a lane (or an interleaved pair) into clip-ready note events.
function renderNotes(c, plan, passes){
  const steps = c.steps;
  const stepBeats = RATE_BEATS[S.params.rate] || 0.25;
  const notes = [];
  let call = S.call, resp = S.response;
  for(let pass=0; pass<passes; pass++){
    for(let i=0;i<steps;i++){
      const lane = laneForStep(plan, pass, i, steps);
      const phrase = lane === C ? call : resp;
      const ev = stepEvent(phrase, i, c, lane);
      if(!ev) continue;

      // A tie into the same pitch holds the note rather than playing it again.
      // Two overlapping note-ons on one pitch is not a tie, it is a retrigger -
      // and it made JOIN sound the same as SLIDE.
      const prev = notes.length ? notes[notes.length - 1] : null;
      const prevStep = phrase[i - 1];
      if(prev && prevStep && prevStep.active && prevStep.tie &&
         prev.pitch === ev.pitch && prev.chan === ev.chan){
        const endsAt = (pass*steps + i) * stepBeats + swingOffset(i, stepBeats)
                     + Math.max(0.02, ev.dur * stepBeats);
        prev.dur = endsAt - prev.start;
        continue;
      }

      notes.push({
        pitch: ev.pitch,
        start: (pass*steps + i) * stepBeats + swingOffset(i, stepBeats),
        dur: Math.max(0.02, ev.dur * stepBeats),
        vel: ev.vel,
        chan: ev.chan
      });
    }
  }
  return notes;
}

// ---------------------------------------------------------------------------
// 7. Realtime playback
// ---------------------------------------------------------------------------

// Named Max buses keep lane identity intact before Live merges MIDI channels.
// 0 is the original track output. Call uses bus 1; response uses bus 2.
// The namespace deliberately does not overlap the Drifter buses.
function routeForLane(lane){
  const value = lane === R ? S.params.responseOutput : S.params.callOutput;
  // No shared fallback while waiting for this instance's runtime Live ID.
  return value === 'Companion' ? (S.routingID ? 'sidebrain_crg_v2_'+S.routingID+(lane===R?'_response':'_call') : -1) : 0;
}

function routingid(id){
  id=Number(id)||0;
  if(S.routingID!==id){ allNotesOff(); S.routingID=id; }
  if(nativeEnabled) nativeConfigure();
}
function linkcounts(call,response){ outlet(O_UI,'linkcounts',call,response); }

function isOutputParam(name){ return name === 'callOutput' || name === 'responseOutput'; }

function rawNote(pitch, vel, chan, destination){
  if(destination === -1) return;
  if(typeof destination === 'string'){
    messnamed(destination, 'list', 0x90 | (chan & 0x0f), pitch & 0x7f, vel & 0x7f);
    return;
  }
  // [midiout] wants raw bytes, one int at a time.
  outlet(O_MIDI, 0x90 | (chan & 0x0f));
  outlet(O_MIDI, pitch & 0x7f);
  outlet(O_MIDI, vel & 0x7f);
}

// The note-off Tasks are cached per pitch and reused for the life of the
// device; S.activeNotes only records which of them are currently sounding.
const noteTasks = {};
function noteTaskFor(pitch, chan, destination){
  const k = pitch + ':' + chan + ':' + destination;
  if(!noteTasks[k]) noteTasks[k] = new Task(function(){ noteOffNow(pitch, chan, destination); });
  return noteTasks[k];
}

function allNotesOff(){
  if(S.preparing) return;
  if(nativeEnabled) outlet(6,'flush');
  // A pending swung note must not turn on again after stop, mute, or reroute.
  for(let i=0;i<SWING_TASKS.length;i++){
    SWING_TASKS[i].cancel();
    SWING_TASKS[i].__ev = null;
  }
  for(const k in S.activeNotes){
    if(!S.activeNotes[k]) continue;
    const t = noteTasks[k];
    if(t && t.cancel) t.cancel();
    const note = S.activeNotes[k];
    rawNote(note.pitch, 0, note.chan, note.destination);
  }
  S.activeNotes = {};
}

function noteOffNow(pitch, chan, destination){
  const k = pitch + ':' + chan + ':' + destination;
  if(!S.activeNotes[k]) return;
  delete S.activeNotes[k];
  rawNote(pitch, 0, chan, destination);
}

function deviceactive(v){
  const active = v ? 1 : 0;
  if(active === S.deviceActive) return;
  S.deviceActive = active;
  allNotesOff();
  if(active && initDone) rearm();
}

function dispose(){
  S.deviceActive = 0;
  allNotesOff();
  outlet(O_XPORT, 0);
}

// Hold a note that is already sounding: push its note-off back without sending
// a new note-on. This is what a tie does - retriggering would be a new note.
// Is this step a continuation of the one before - tied, and the same pitch?
// Worked out from the phrase alone, so it does not depend on what has actually
// been sent yet. That matters under swing, where the note being joined may
// still be waiting in its delay.
function isTieContinuation(phrase, i, c, lane){
  if(i <= 0) return false;
  const prev = phrase[i-1];
  if(!prev || !prev.active || !prev.tie) return false;
  const here = stepEvent(phrase, i, c, lane);
  const before = stepEvent(phrase, i-1, c, lane);
  return !!(here && before && here.pitch === before.pitch && here.chan === before.chan);
}

// How long a note lasts once everything tied to it is counted, in ms. The note
// leaves with its whole length rather than being extended later.
function tiedDurationMs(phrase, i, c, lane, ev, stepBeats, msPerBeat){
  const stepMs = stepBeats * msPerBeat;
  let end = swingOffset(i, stepBeats) * msPerBeat + ev.dur * stepMs;
  let j = i;
  while(j + 1 < phrase.length && isTieContinuation(phrase, j + 1, c, lane)){
    j++;
    const nx = stepEvent(phrase, j, c, lane);
    if(!nx) break;
    end = (j - i) * stepMs + swingOffset(j, stepBeats) * msPerBeat + nx.dur * stepMs;
  }
  return Math.max(10, end - swingOffset(i, stepBeats) * msPerBeat);
}

function sendNote(pitch, vel, chan, durMs, lane){
  if(!S.deviceActive || !S.enabled || S.mute[lane]) return;
  const destination = routeForLane(lane);
  const k = pitch + ':' + chan + ':' + destination;
  const t = noteTaskFor(pitch, chan, destination);
  if(S.activeNotes[k]){                 // retrigger: close the old one first
    t.cancel();
    rawNote(pitch, 0, chan, destination);
  }
  rawNote(pitch, vel, chan, destination);
  S.activeNotes[k] = {pitch: pitch, chan: chan, destination: destination};
  t.schedule(Math.max(10, durMs));
}

// A LiveAPI object built at the wrong moment resolves to nothing, and every
// later get() on it prints "no valid object set". Build it once, lazily, and
// only trust it if it actually resolved to an id.
// A device is built before Live finishes constructing its API, and every
// LiveAPI made too early prints "Live API is not initialized". Init used to
// make several. One gate now guards all of them: it stays shut until either
// live.thisdevice tells us we are ready, or a throttled probe finds the API up.
let liveReady = false;
let lastProbeMs = -1;
const bootMs = Date.now();
const LOAD_QUIET_MS = 600;   // the API is never up this early; do not even ask

function liveUp(){
  if(liveReady) return true;
  const now = Date.now();
  // Silence during the load window. Every probe made here would fail and print
  // a warning, and the retry task will pick things up immediately afterwards.
  if((now - bootMs) < LOAD_QUIET_MS) return false;
  // and once past it, probe at most every 250ms
  if(lastProbeMs >= 0 && (now - lastProbeMs) < 250) return false;
  lastProbeMs = now;
  try {
    const a = new LiveAPI('live_set');
    if(a && a.id && a.id != 0){ liveReady = true; songAPI = a; return true; }
  } catch(e){}
  return false;
}

// Sent from [live.thisdevice] -> [t b] -> [liveready(  when Live has finished
// loading the device. With it wired the console stays clean; without it the
// probe above gets there on its own, just noisily.
function liveready(){
  liveReady = true;
  lastProbeMs = -1;
  initTransport(true);
  if(S.followLive){ initLiveScale(); readLiveScale(); }
  sendRate();
  armMetro();
  pushDisplay();
}

let songAPI = null;
function song(){
  if(songAPI && songAPI.id && songAPI.id != 0) return songAPI;
  // liveUp() keeps the object it proved with, so there is no second one to build
  if(!liveUp()) return null;
  return (songAPI && songAPI.id && songAPI.id != 0) ? songAPI : null;
}

function liveGetList(prop){
  const a = song();
  if(!a) return null;
  try {
    const v = a.get(prop);
    if(!v) return null;
    return Array.isArray(v) ? v.slice() : [v];
  } catch(e){ return null; }
}

// Live 12 scale names that differ from ours. Only consulted when
// scale_intervals is unavailable (Live 12.0 and earlier expose the name only).
const LIVE_SCALE_ALIASES = {
  'Major':'Major / Ionian', 'Minor':'Natural Minor / Aeolian',
  'Minor Blues':'Blues Minor', 'Major Blues':'Blues Major',
  'Half-whole Dim.':'Diminished Half-Whole', 'Whole-half Dim.':'Diminished Whole-Half',
  'In-Sen':'In Sen', 'Iwato':'In Sen',
  'Pelog Selisir':'Pelog-ish', 'Pelog Tembung':'Pelog-ish',
  'Bhairav':'Double Harmonic', 'Super Locrian':'Locrian'
};

let scaleReadTries = 0;
let scaleReadTask = null;

function readLiveScale(){
  // A fresh object, not the cached one. The cache is built by the startup probe
  // and is enough for tempo and is_playing; the song's scale properties were
  // coming back empty from it every time even on Live 12.
  let a = null;
  try { a = new LiveAPI('live_set'); } catch(e){ a = null; }
  if(!a || !a.id || a.id == 0) a = song();

  const grab = function(prop, fallback){
    if(!a) return fallback;
    try {
      const v = a.get(prop);
      if(v === undefined || v === null) return fallback;
      const n = Array.isArray(v) ? v[0] : v;
      return (n === undefined || n === null) ? fallback : n;
    } catch(e){ return fallback; }
  };
  const grabList = function(prop){
    if(!a) return null;
    try {
      const v = a.get(prop);
      if(!v) return null;
      return Array.isArray(v) ? v.slice() : [v];
    } catch(e){ return null; }
  };

  const root = grab('root_note', -1);
  if(root >= 0) S.liveRoot = Number(root);
  const name = grab('scale_name', '');
  if(name) S.liveScaleName = String(name);

  // Preferred: the actual intervals, so any Live scale works even if we have
  // no equivalent by name.
  const iv = grabList('scale_intervals');
  if(iv && iv.length > 1){
    S.liveIntervals = iv.map(Number).filter(function(x){ return isFinite(x); });
    S.scaleUnavailable = 0;
    scaleReadTries = 0;
    return;
  }
  const mapped = LIVE_SCALE_ALIASES[S.liveScaleName] || S.liveScaleName;
  S.liveIntervals = SCALE_INTERVALS[mapped] ? SCALE_INTERVALS[mapped].slice() : null;

  // Nothing came back. The song's scale properties need Live 12, and even there
  // they are not readable the instant a device loads, so try again for a few
  // seconds before saying it is not available.
  if(S.followLive && (S.liveRoot < 0 || !S.liveIntervals)){
    if(scaleReadTries < 20){
      scaleReadTries++;
      if(!scaleReadTask) scaleReadTask = new Task(function(){ readLiveScale(); pushDisplay(); });
      scaleReadTask.cancel();
      scaleReadTask.schedule(250);
    } else if(scaleReadTries === 20){
      scaleReadTries++;
      S.scaleUnavailable = 1;
      post('crg: Live reported no song scale after 5s.\n' +
           '   root_note = ' + JSON.stringify(grab('root_note', 'nothing')) + '\n' +
           '   scale_name = ' + JSON.stringify(grab('scale_name', 'nothing')) + '\n' +
           '   scale_intervals = ' + JSON.stringify(grabList('scale_intervals')) + '\n' +
           '   live_set id = ' + (a ? a.id : 'no object') + '\n');
      status('Live scale unavailable - needs Live 12');
    }
  } else {
    scaleReadTries = 0;
    S.scaleUnavailable = 0;
  }
}

// 'dumpscale' - print exactly what Live returns for the song scale, so a device
// that will not follow it can be told apart from a Live that will not report it.
function dumpscale(){
  const a = song();
  post('crg scale check:\n');
  post('   live_set resolved: ' + (a ? 'yes' : 'NO - the Live API is not up') + '\n');
  if(a){
    ['root_note','scale_name','scale_intervals','scale_mode'].forEach(function(p){
      let v;
      try { v = a.get(p); } catch(e){ v = 'error: ' + e.message; }
      post('   ' + p + ' = ' + (v === undefined ? 'undefined' :
           (v && v.length === 0 ? '(empty - property not on this Live version)' : String(v))) + '\n');
    });
  }
  post('   Scale Mode is ' + (S.followLive ? 'on' : 'off') + '\n');
  post('   using: key ' + (S.liveRoot >= 0 ? NOTE_NAMES[S.liveRoot % 12] : 'none') +
       ', scale ' + (S.liveScaleName || 'none') +
       ', intervals ' + (S.liveIntervals ? S.liveIntervals.join(' ') : 'none') + '\n');
}

let scaleObs = [];
let scaleRetry = null;
function initLiveScale(){
  if(scaleObs.length) return;
  // Scale Mode reports its stored value the moment the device loads, well
  // before Live has built its API. Three observers meant three warnings.
  if(!liveUp()){
    if(!scaleRetry) scaleRetry = new Task(function(){ if(S.followLive) initLiveScale(); });
    scaleRetry.cancel();
    scaleRetry.schedule(250);
    return;
  }
  ['scale_name', 'root_note', 'scale_intervals'].forEach(function(prop){
    try {
      const a = new LiveAPI(function(){
        const beforeRoot = S.liveRoot, beforeName = S.liveScaleName;
        const beforeIv = S.liveIntervals ? S.liveIntervals.join(',') : '';
        readLiveScale();
        const changed = (beforeRoot !== S.liveRoot) || (beforeName !== S.liveScaleName) ||
                        (beforeIv !== (S.liveIntervals ? S.liveIntervals.join(',') : ''));
        if(S.followLive && changed){ generate('both', true); pushDisplay(); }
        else if(S.followLive) pushDisplay();
      }, 'live_set');
      if(a && a.id && a.id != 0){ a.property = prop; scaleObs.push(a); }
    } catch(e){}
  });
  readLiveScale();
}

// livescale 1 : key and scale follow Live's song scale
// livescale 0 : use the device's own key and scale menus
function livescale(v){
  ensureInit();
  S.followLive = v ? 1 : 0;
  if(S.followLive){
    scaleReadTries = 0;
    initLiveScale();
    readLiveScale();
    if(!S.liveIntervals){
      // Not a failure yet - the first read almost always comes back empty
      // because Live has not finished loading. readLiveScale keeps trying and
      // reports once if it truly never arrives. This used to shout on every
      // attempt, which filled the console while nothing was actually wrong.
      status('Reading Live scale...');
    } else {
      status('Following Live scale: ' + S.liveScaleName);
    }
  } else {
    status('Using device key/scale');
  }
  generate('both', true);
  pushDisplay();
}

function liveGet(prop, fallback){
  const a = song();
  if(!a) return fallback;
  try {
    const v = a.get(prop);
    if(v === undefined || v === null) return fallback;
    const n = Array.isArray(v) ? v[0] : v;
    return (n === undefined || n === null) ? fallback : n;
  } catch(e){ return fallback; }
}

// Cached so we aren't constructing a LiveAPI object on every single step.
let tempoCache = 120;
function currentTempo(){
  const t = Number(liveGet('tempo', tempoCache));
  if(t > 0){ tempoCache = t; }
  return tempoCache;
}

// Called by [metro] on every step.
function bang(){
  if(nativeEnabled) return; // no JavaScript event can drive native playback
  ensureInit();
  if(!S.enabled || !S.deviceActive) return;
  let c = cfg();
  const steps = c.steps;
  const previousStep=S.stepIdx, previousPass=S.passIdx, previousBang=S.lastBangMs;
  if(!S.call.length || !S.response.length) generate('both', true);

  // Derive the position from Live's timeline rather than counting bangs.
  // Counting meant the phrase resumed wherever the metro was left, so
  // restarting the transport carried on mid-phrase instead of from step 1.
  // Reading the song position also means arrangement jumps and loops land in
  // the right place for free.
  const stepBeats = RATE_BEATS[S.params.rate] || 0.5;
  const msPerBeat = 60000 / currentTempo();
  const nowMs = Date.now();

  if(S.syncMode === 'timeline'){
    // Opt-in: pinned to the arrangement. Uses the song clock and therefore
    // inherits its jitter, which is why it is not the default.
    const t = liveGet('current_song_time', -1);
    if(t >= 0){
      const idx = Math.max(0, Math.round(t / stepBeats));
      S.stepIdx = ((idx % steps) + steps) % steps;
      S.passIdx = Math.floor(idx / steps);
    }
  } else {
    // Timing comes from the metro, which is transport-quantized and therefore
    // rhythmically exact. Position comes from a running estimate of the song
    // clock rather than the clock itself:
    //
    //   - the estimate advances by exactly one step per bang
    //   - each bang it is nudged a fifth of the way toward the actual reading
    //   - a reading more than a beat away is treated as a real move, not noise
    //
    // Nudging is what makes it immune to jitter: a stale value pulls the
    // estimate by a fraction of a step and the next reading pulls it back.
    // Deriving the step straight from the reading is what used to duplicate or
    // skip the first step, and counting alone is what could never recover from
    // a loop or a dropped bang.
    const gap = nowMs - S.lastBangMs;
    const fresh = (S.lastBangMs < 0) ||
                  (S.syncMode === 'restart' && gap > stepBeats * msPerBeat * 2.5);

    // A lag-free position, if the patch is feeding one, settles everything:
    // loops, playhead moves and dropped bangs all fall out of the arithmetic.
    if(havePos(nowMs, stepBeats)){
      if(S.posAdopt){
        // Line the origin up with wherever the count had got to, so switching
        // over is silent rather than throwing the phrase back to step 1.
        S.posAdopt = false;
        // +1: stepIdx still holds the previous bang, and this bang is the next
        // step - without it the step at the handover played twice.
        S.posOrigin = S.extPos - (S.passIdx * steps + S.stepIdx + 1) * stepBeats;
      }
      if(fresh || S.posOrigin === undefined || S.extPos < S.posOrigin - stepBeats * 0.5){
        // A loop sends the phrase back to step 1, but the pass counter has to
        // keep going: a form like call, call, response spans several passes and
        // resetting it here meant it never got past the first one.
        const looped = !fresh && (S.posOrigin !== undefined);
        S.posOrigin = S.extPos;
        S.stepIdx = 0;
        S.passIdx = looped ? (S.passIdx + 1) : 0;
        if(looped) S.wrapped = true;
      } else {
        const elapsed = Math.max(0, Math.round((S.extPos - S.posOrigin) / stepBeats));
        const idx = elapsed % steps;
        // The pass is COUNTED, not derived from elapsed time. Deriving it meant
        // an arrangement loop sent it back to zero every time round, so a form
        // spanning several passes - call, call, response - never got past the
        // first one.
        if(idx < S.stepIdx){ S.passIdx++; S.wrapped = true; }
        S.stepIdx = idx;
      }
    } else if(fresh){
      S.stepIdx = 0;
      S.passIdx = 0;
      S.sinceWrap = 0;
      S.loopPoll = 0;
      refreshLoop();
    } else {
      // Re-read the loop settings now and then; they can be changed while
      // playing, and reading them every bang would be wasteful.
      if((S.loopPoll = (S.loopPoll || 0) + 1) >= 64){ S.loopPoll = 0; refreshLoop(); }

      S.stepIdx++;
      S.sinceWrap = (S.sinceWrap || 0) + 1;
      if(S.stepIdx >= steps){
        S.stepIdx = 0;
        S.passIdx++;
        S.wrapped = true;
      }

      // The arrangement loop, counted rather than watched. The metro is locked
      // to Live's grid, so a loop of a known length is exactly this many bangs -
      // and unlike the song position, a count cannot arrive stale.
      if(S.syncMode === 'restart' && S.loopOn && S.loopLen > 0){
        const loopSteps = Math.round(S.loopLen / stepBeats);
        if(loopSteps > 0 && S.sinceWrap >= loopSteps){
          S.sinceWrap = 0;
          S.stepIdx = 0;
          S.passIdx++;          // the form carries on across the loop
          S.wrapped = true;
        }
      }
    }
  }
  S.lastBangMs = nowMs;

  // Finish the whole form before changing a pair, so a response still answers
  // the call the listener just heard. Interleaved modes complete in one pass.
  const queuePlan=passPlan(c.phraseMode);
  const queuePasses=queuePlan.interleave!==undefined?1:queuePlan.seq.length;
  if(S.queuedPair && S.stepIdx===0 && S.passIdx%queuePasses===0 &&
     (previousBang<0 || previousStep!==0 || previousPass!==S.passIdx)){
    applyQueuedPair();
    c=cfg();
    S.wrapped=false; // do not immediately drift the newly generated pair
  }

  const plan = passPlan(c.phraseMode);
  const lane = laneForStep(plan, S.passIdx, S.stepIdx, steps);
  let phrase = lane === C ? S.call : S.response;
  if(plan.form){
    const key = plan.form[S.passIdx % plan.form.length];
    phrase = variantFor(key, lane, c);
    // remember what is sounding so the lanes can draw it
    if(!S.shownVariant) S.shownVariant = {};
    const bank = lane === C ? 'call' : 'response';
    if(S.shownVariant[bank] !== phrase){
      S.shownVariant[bank] = phrase;
      S.shownKey = key;
      pushDisplay();
    }
  } else if(S.shownVariant){
    S.shownVariant = null;                 // back to a plain mode
    pushDisplay();
  }

  // "Response only on every Nth pass" gates the response lane
  let muted = false;
  if(c.phraseMode === 'Call with response fill' && lane === R && (S.passIdx % 4) !== 3) muted = true;

  if(S.mute[lane]) muted = true;
  if(!muted){
    // A tied continuation sends nothing: the note it belongs to already left
    // with the length to cover it.
    const ev = isTieContinuation(phrase, S.stepIdx, c, lane)
             ? null : stepEvent(phrase, S.stepIdx, c, lane);
    if(ev){
      const swMs = swingOffset(S.stepIdx, stepBeats) * msPerBeat;
      const durMs = tiedDurationMs(phrase, S.stepIdx, c, lane, ev, stepBeats, msPerBeat);

      if(swMs > 1){
        // A small ring of reusable Tasks. Allocating one per swung note left
        // thousands of them behind over a long session.
        const t = swingTask();
        t.__ev = { p:ev.pitch, v:ev.vel, c:ev.chan, d:durMs, lane:lane };
        t.schedule(swMs);
      } else {
        sendNote(ev.pitch, ev.vel, ev.chan, durMs, lane);
      }
    }
  }

  outlet(O_UI, 'playhead', S.stepIdx, lane === C ? 0 : 1);

  if(S.wrapped){ S.wrapped = false; endOfPass(plan, c); }
}

// ---------------------------------------------------------------------------
// Form variants
//
// A / B / A / C and A / A' / B / B' name a shape. Each letter is a fixed
// phrase: A is the lane as generated, the others are mutations of it made once
// and then kept. Replaying them is what makes the form audible - mutating live
// meant A never came back the same, which is just drift with a fancy name.
// ---------------------------------------------------------------------------
function variantFor(key, lane, c){
  if(key === 'A') return lane === C ? S.call : S.response;
  const bank = lane === C ? 'call' : 'response';
  if(!S.variants) S.variants = {};
  if(!S.variants[bank]) S.variants[bank] = {};
  const store = S.variants[bank];
  if(!store[key]){
    // a prime is a light variation of A, a new letter is a bigger departure
    const base = lane === C ? S.call : S.response;
    const steps = Number(S.params.steps) || 8;
    const amt = (key.indexOf("'") >= 0)
      ? Math.max(1, Math.round(steps * 0.12))
      : Math.max(2, Math.round(steps * 0.35));
    store[key] = mutatePhrase(base, c, 'Whole phrase', amt);
  }
  return store[key];
}

// Any fresh phrase invalidates the stored variants: they were derived from the
// old one and would otherwise be replayed against a phrase they no longer match.
function clearVariants(){ S.variants = null; S.shownVariant = null; }

function endOfPass(plan, c){
  const seqLen = plan.seq ? plan.seq.length : 2;

  // Per-lane drift. A mode like 'Stable call, mutating response' already
  // mutates one of the lanes; applying drift there as well would mutate it
  // twice a pass, so the lane the mode owns is left alone.
  const modeVaries = function(lane){
    if(!plan.vary) return false;
    for(let i=0;i<plan.vary.length;i++){
      if(!plan.vary[i]) continue;
      const l = plan.seq ? plan.seq[i] : C;
      if(l === lane) return true;
    }
    return false;
  };
  // Only mutate a lane on the pass before it plays. Drifting every lane every
  // pass meant a response that comes round every fourth bar had already been
  // mutated four times by the time it was heard - so the fill never sounded
  // like a variation of the one before, it sounded unrelated.
  const nSteps = Number(S.params.steps) || 8;
  const nextLane = laneForStep(plan, S.passIdx + 1, 0, nSteps);
  const interleaved = (plan.interleave !== undefined);   // both lanes play every pass

  let drifted = false;
  [['call', 'driftCall', C], ['response', 'driftResponse', R]].forEach(function(e){
    const amt = clamp(Number(S.params[e[1]]) / 100, 0, 1);
    if(amt <= 0 || modeVaries(e[2])) return;
    if(!interleaved && nextLane !== e[2]) return;        // not this lane's turn
    const steps = Math.max(1, Math.round(amt * nSteps * 0.30));
    if(e[2] === C) S.call = mutatePhrase(S.call, c, 'Whole phrase', steps);
    else S.response = mutatePhrase(S.response, c, 'Whole phrase', steps);
    drifted = true;
  });
  if(drifted) pushDisplay();
  if(plan.form) return;      // a form replays stored variants; nothing to mutate
  if(plan.vary){
    const every = plan.varyEvery || 1;
    if(S.passIdx % every === 0){
      const idx = S.passIdx % plan.vary.length;
      if(plan.vary[idx]){
        const lane = plan.seq ? plan.seq[idx] : C;
        if(lane === C) S.call = mutatePhrase(S.call, c, 'Whole phrase');
        else S.response = mutatePhrase(S.response, c, 'Whole phrase');
        // the phrase changed under playback; the lanes have to redraw or the
        // display goes on showing notes that are no longer being played
        pushDisplay();
      }
    }
  }
}

// Keep the metro switched on the whole time the device is armed. With
// @quantize it only bangs while Live's transport rolls, so the transport still
// gates playback - but the first bang lands on the very first grid point
// rather than whenever a deferred observer callback got round to us.
// Both attributes have to move together. Setting interval alone left quantize
// pinned to 8n, so at 1/16 or 1/32 the metro could only bang on every second or
// fourth step and the sequence appeared to skip.
function sendRate(){
  if(nativeEnabled){ nativeRefresh(); return; }
  const n = RATE_MAXNAME[S.params.rate] || '8n';
  outlet(O_XPORT, 'interval', n);
  outlet(O_XPORT, 'quantize', n);
  S.origin = -1; S.lastT = -1; S.lastIdx = -1; S.lastBangMs = -1;
}

function armMetro(){
  if(nativeEnabled) outlet(6,'run',S.deviceActive?1:0);
  outlet(O_XPORT, 1);            // [metro] wants 1/0, NOT start/stop
  S.playing = true;
}

function start(){
  S.stepIdx = 0; S.passIdx = 0; S.origin = -1;
  armMetro();
  status('Playing');
}

function stop(){
  if(nativeEnabled){ outlet(6,'run',0); outlet(6,'apply'); }
  // Silence now, but mark it rather than relying on the metro staying off:
  // re-arming is what lets the next transport start pick things up again.
  outlet(O_XPORT, 0);
  S.playing = false;
  S.finished = true;
  allNotesOff();
  applyQueuedPair();
  outlet(O_UI, 'playhead', -1, 0);
  status('Stopped');
}

// Put the metro back. It is transport-quantized, so an armed metro sits idle
// until Live rolls - there is no cost to leaving it on, and leaving it off is
// how the device used to die permanently.
function rearm(){
  if(S.preparing) return;
  if(nativeEnabled) outlet(6,'run',S.deviceActive?1:0);
  S.finished = false;
  S.lastBangMs = -1;
  outlet(O_XPORT, 1);
}

// Transport stopped: silence and clear, but leave the metro armed so the next
// transport start is caught on its first grid point.
function transportStopped(){
  if(nativeEnabled) return; // native transport state handles note-offs/reset
  allNotesOff();
  applyQueuedPair();
  rearm();                       // ready for the next start, whatever stopped us
  S.origin = -1; S.lastT = -1; S.lastIdx = -1; S.lastBangMs = -1; S.lastSongT = -1e9;
  S.lastSent = null;
  S.stepIdx = 0;
  S.passIdx = 0;
  outlet(O_UI, 'playhead', -1, 0);
}

function panic(){
  allNotesOff();
  status('Panic — all notes off');
}

// Live transport observation
let transportAPI = null;
let transportTries = 0;
let transportRetry = null;

// A device is created before Live has finished building its API, so the first
// attempt usually fails with "Live API is not initialized". One attempt at init
// therefore left transport sync off for the whole session. Keep trying.
// Live's loop settings. Static while playing, so they are read occasionally
// rather than every bang, and never used for timing - only for the length.
function refreshLoop(){
  const a = song();
  if(!a) return;
  try {
    const on  = a.get('loop');
    const len = a.get('loop_length');
    S.loopOn  = (on && on[0] == 1) ? 1 : 0;
    S.loopLen = (len && len[0] > 0) ? Number(len[0]) : 0;
  } catch(e){ S.loopOn = 0; S.loopLen = 0; }
}

function scheduleTransportRetry(){
  if(transportAPI || transportTries > 40) return;
  if(!transportRetry) transportRetry = new Task(function(){
    initTransport(true);
    // the first successful attach is also the moment the rest of the Live-side
    // setup can safely run
    if(transportAPI){
      if(S.followLive){ initLiveScale(); readLiveScale(); }
      sendRate();
      armMetro();
      pushDisplay();
    }
  });
  transportRetry.cancel();
  // wait out the load window on the first go, then retry briskly
  transportRetry.schedule(Math.max(250, LOAD_QUIET_MS - (Date.now() - bootMs) + 50));
}

function initTransport(quiet){
  if(transportAPI){ if(!quiet) post('crg: transport observer already attached\n'); return; }
  if(!liveUp()){ scheduleTransportRetry(); return; }
  transportTries++;
  try {
    const a = new LiveAPI(function(iarg){
      if(iarg && iarg[0] === 'is_playing'){
        // Do NOT switch the metro here - this callback is deferred and would
        // arrive after the first grid point. Just reset the phrase anchor.
        if(iarg[1] == 1){
          // Deliberately does NOT reset the phrase anchor - this callback is
          // deferred and would yank the phrase back to step 1 mid-flight. It
          // does re-arm the metro, which is not timing-critical because the
          // metro quantizes to the grid itself.
          if(S.finished) rearm();
        } else {
          transportStopped();
        }
        outlet(O_UI, 'transport', iarg[1] == 1 ? 1 : 0);
      }
    }, 'live_set');
    if(!a || !a.id || a.id == 0){
      scheduleTransportRetry();          // Live is not ready yet; try again shortly
      if(transportTries > 40) post('crg: live_set never resolved - use forcestart\n');
      return;
    }
    a.property = 'is_playing';
    transportAPI = a;
    outlet(O_UI, 'transport', liveGet('is_playing', 0));
    if(S.verbose) post('crg: transport observer attached after ' + transportTries + ' attempt(s)\n');
  } catch(e){
    scheduleTransportRetry();
    if(transportTries > 40) post('crg: LiveAPI unavailable (' + e.message + ') - use forcestart\n');
  }
}

// ---------------------------------------------------------------------------
// 8. Max message handlers
// ---------------------------------------------------------------------------

function loadbang(){ init(); }

let initDone = false;
let settleTask = null;

// Every entry point routes through this. autowatch reloads wipe the globals
// without re-banging [live.thisdevice], so relying on that bang alone leaves
// the device dead until the patcher is reopened.
function ensureInit(){ if(!initDone) init(); }

let initRunning = false;
function init(){
  if(initDone) return;
  initDone = true; initRunning = true;
  try {
    // Deliberately NOT pushing JS defaults into the UI here. In a device the
    // live.* objects own their values, Live restores them on load, and they
    // report in by themselves. Pushing defaults first fought that and left
    // whatever arrived last in charge.
    if(!S.call.length) generate('both', true);
    sendRate();
    initTransport();          // will schedule its own retry past the quiet window
    // the observer only fires on change, so catch a transport already rolling
    armMetro();
    // give the live.* objects a moment to report their restored values,
    // then rebuild if the step count turned out different from the default
    if(!settleTask) settleTask = new Task(function(){
      regenerateIfShapeChanged();
      pushDisplay();
    });
    settleTask.cancel();
    settleTask.schedule(400);

    sendMenuItems();
    pushDisplay();
    status('v' + CRG_VERSION + ' ready');
    if(S.verbose) post('crg v' + CRG_VERSION + ': init ok, ' + S.call.length + ' steps\n');
  } catch(e){
    post('crg v' + CRG_VERSION + ': INIT FAILED: ' + e.message + '\n');
    status('init failed: ' + e.message);
  }
  initRunning = false;
}

function param(name, value, force){
  if(!initDone && !initRunning) ensureInit();
  if(ENUM_PARAMS[name]){
    const list = DATA[ENUM_PARAMS[name]];
    const idx = clamp(Math.round(value), 0, list.length-1);

    // Live re-sends every parameter whenever it rebuilds the device view, which
    // happens on things like changing track focus. Treating those as edits meant
    // regenerating the phrase for no reason - the line audibly changed while
    // playing. A message that carries the value we already hold is a no-op.
    // Only redundant messages from Live are ignored. A deliberate edit arrives
    // with force set, because setEnum has already written the value and this
    // guard would otherwise treat the real change as a no-op.
    if(!force && S.params[name] === list[idx] && initDone && !initRunning) return;

    if(isOutputParam(name) && S.params[name] !== list[idx]) allNotesOff();
    S.params[name] = list[idx];
    if(name==='newPairTiming' || name==='clipLength' || name==='clipTarget'){
      if(name==='newPairTiming' && S.params[name]==='Now') applyQueuedPair();
      sendMenuValues();
      return;
    }
    if(isOutputParam(name)){
      sendMenuValues();
      status((name === 'callOutput' ? 'Call' : 'Response') + ' output: ' + list[idx]);
      return; // routing never regenerates or changes either phrase
    }
    if(name === 'rate') sendRate();
    if(name === 'steps') S.stepIdx = 0;
    // While undo is pushing restored values back to the UI, those values come
    // straight back in here. Regenerating on them would overwrite the phrases
    // undo had just put back.
    if(S.restoring) return;
    if(name === 'syncMode'){
      const m = String(S.params.syncMode).toLowerCase();
      S.syncMode = (m.indexOf('time') === 0) ? 'timeline'
                 : (m.indexOf('free') === 0) ? 'free' : 'restart';
      S.origin = -1;
    }
    if(S.autoRegen && RESPONSE_PARAMS.indexOf(name) !== -1)   generate('response', true);
    else if(S.autoRegen && REGEN_PARAMS.indexOf(name) !== -1) generate('both', true);
    else if(RESOLVE_PARAMS.indexOf(name) !== -1)              reapplyEnding();
    else if(DENSITY_PARAMS.indexOf(name) !== -1)              applyDensity();
    else if(OCTJUMP_PARAMS.indexOf(name) !== -1)              applyOctaveJump();
    else if(RESIZE_PARAMS.indexOf(name) !== -1)               resizePhrases();
    else if(name === 'articulationProfile'){ applyArtPreset(S.params[name]); reapplyArticulation('articulationProfile'); }
    else if(ARTIC_PARAMS.indexOf(name) !== -1){ markArtCustom(); reapplyArticulation(name); }
    else if(ENDING_PARAMS.indexOf(name) !== -1)               reapplyEnding();
    else                                                      regenerateIfShapeChanged();
    pushDisplay();
  } else if(NUM_PARAMS.indexOf(name) !== -1){
    // These arrive from mapped macros, MIDI controllers and automation as well
    // as from our own UI. Storing the value without running the dispatch meant
    // a mapped macro moved the number and nothing happened.
    setnum(name, value);
  } else {
    status('Unknown param: ' + name);
  }
}

// Re-run the cadence over the phrases already on screen. Only the last step
// is touched, so the phrase you built survives an ending change.
// How many steps the DENSITY macro is asking for.
function densityTargetCount(c){
  const t = (c.densityTarget !== undefined) ? c.densityTarget : 0.5;
  // 100% has to mean every step. The old curve topped out at 0.90 of them, so
  // the knob could be hard right and still leave gaps.
  return clamp(Math.round(c.steps * (0.12 + t * 0.88)), 1, c.steps);
}

// Add or remove notes on the phrases already on screen so the count matches
// DENSITY, leaving every surviving step's pitch and articulation untouched.
// Turning the knob used to regenerate, which threw away the phrase.
// OCT JUMP lifts a proportion of notes by an octave without touching the
// phrase itself. The offset is stored per step and reassigned from scratch each
// time, so turning the knob up and back down returns exactly what you had
// rather than compounding.
// STEPS changes the length of the bar, not its content. Shortening truncates;
// lengthening repeats what is already there, so a 16-step version of an 8-step
// phrase is that phrase twice rather than something unrelated.
function resizePhrases(){
  clearVariants();
  const want = Number(S.params.steps) || 8;
  ['call','response'].forEach(function(which){
    const p = which === 'call' ? S.call : S.response;
    if(!p.length) return;
    const old = p.length;
    if(want === old) return;
    const out = [];
    for(let i=0;i<want;i++) out.push(Object.assign({}, p[i % old]));
    if(which === 'call') S.call = out; else S.response = out;
  });
  S.stepIdx = 0;
  // Deliberately does NOT re-apply density: that would edit notes, and changing
  // the bar length should change the length and nothing else. Repeating keeps
  // the note count proportional, so the DENSITY knob stays honest anyway.
  status('Steps: ' + want);
}

function applyOctaveJump(quiet){
  const c = cfg();
  const p = clamp(Number(S.params.octaveJump) / 100, 0, 1);
  ['call','response'].forEach(function(which){
    const ph = which === 'call' ? S.call : S.response;
    // pick an exact count rather than rolling per note, so the knob moves
    // predictably instead of scattering on short phrases
    const live = [];
    for(let i=0;i<ph.length;i++){ ph[i].oct = 0; if(ph[i].active) live.push(i); }
    const want = Math.round(live.length * p * 0.6);
    // The clips drop an octave as often as they lift one. Only lifting made the
    // line climb away from its root instead of punching below it.
    pickN(live, want).forEach(function(i){ ph[i].oct = chance(0.5) ? 1 : -1; });
    // The jump moves pitches, so the tie and slide rules get the last word.
    settlePhrase(ph, c);
  });
  if(!quiet) status('Octave jump: ' + Math.round(p * 100) + '%');
}

function applyDensity(quiet){
  clearVariants();
  const c = cfg();
  const cat = styleCategory(c.style);
  const base = densityTargetCount(c);

  // Two phrase modes name a density difference between the lanes. They used to
  // produce exactly the same plan as 'Call then response', so the names
  // promised something the device never did.
  const mode = String(c.phraseMode || '');
  let bias = { call: 1, response: 1 };
  if(mode === 'Call sparse, response busy') bias = { call: 0.6, response: 1.4 };
  else if(mode === 'Call busy, response sparse') bias = { call: 1.4, response: 0.6 };

  ['call','response'].forEach(function(which){
    const p = which === 'call' ? S.call : S.response;
    if(!p.length) return;
    const want = clamp(Math.round(base * bias[which]), 1, c.steps);
    const last = p.length - 1;

    let live = [];
    let dead = [];
    for(let i=0;i<p.length;i++) (p[i].active ? live : dead).push(i);

    // Prefer to fill strong positions first and thin weak ones first, so the
    // groove keeps its shape as the count changes.
    // Pass the style profile: without it this used the neutral weights, so the
    // groove a style asked for was undone every time DENSITY refilled a step.
    const sp0 = styleProfile(c);
    // Each step keeps a fixed rank for the life of the phrase. Rolling a fresh
    // random here made the order different on every call, so the step the knob
    // removed on the way down was rarely the one it restored on the way up.
    for(let i=0;i<p.length;i++) if(p[i].rank === undefined) p[i].rank = Math.random();
    const weight = function(i){ return preferredStepActivation(cat, i, sp0) + p[i].rank * 0.15; };
    dead.sort(function(a,b){ return weight(b) - weight(a); });
    // Steps silenced by this knob are kept on a stack and come back in reverse
    // order. Sorting by weight alone could not do this: the weight carries a
    // random term, so the step that left was rarely the step that returned.
    if(!p.__silenced) p.__silenced = [];
    live.sort(function(a,b){ return weight(a) - weight(b); });

    while(live.length < want && (p.__silenced.length || dead.length)){
      // put back the most recently silenced step before touching anything else
      const i = p.__silenced.length ? p.__silenced.pop() : dead.shift();
      if(p[i].active) continue;
      const di = dead.indexOf(i);
      if(di !== -1) dead.splice(di, 1);
      const s = p[i];
      s.active = true;
      const si = p.__silenced.indexOf(i);
      if(si !== -1) p.__silenced.splice(si, 1);
      if(s.was){
        // this step has been here before - put it back exactly as it was
        s.degree = s.was.degree; s.gate = s.was.gate; s.accent = s.was.accent;
        s.slide = s.was.slide; s.tie = s.was.tie;
        s.was = null;
      } else {
        if(s.degree == null){
          // fall back to the pitch the phrase already uses, not to zero - on a
          // Flat contour a stray zero was the whole second pitch
          let common = 0, tally = {};
          p.forEach(function(x){ if(x.active && x.degree != null) tally[x.degree] = (tally[x.degree]||0)+1; });
          Object.keys(tally).forEach(function(k){ if(!tally[common] || tally[k] > tally[common]) common = Number(k); });
          const near = (i > 0 && p[i-1].degree != null) ? p[i-1].degree
                     : ((i < last && p[i+1].degree != null) ? p[i+1].degree : common);
          s.degree = near;
        }
        const a = articulationForStep(i, true, c, i % Math.max(2, Math.floor(c.steps/2)));
        s.gate = a.gate; s.accent = a.accent; s.slide = a.slide; s.tie = a.tie;
      }
      live.push(i);
    }
    // Remember the note before silencing it, so turning DENSITY back up brings
    // back what was there rather than inventing something from a neighbour.
    const remember = function(s){
      if(s.degree == null) return;
      s.was = { degree:s.degree, gate:s.gate, accent:s.accent, slide:s.slide, tie:s.tie };
    };
    while(live.length > want && live.length > 1){
      const i = live.shift();
      if(i === last) { live.push(i); if(live.length <= want) break; continue; }
      remember(p[i]);
      p.__silenced.push(i);
      p[i].active = false; p[i].degree = null;
      p[i].accent = false; p[i].slide = false; p[i].tie = false;
    }
    settlePhrase(p, c);
  });
  if(!quiet) status('Density: ' + base + ' of ' + c.steps + ' steps');
}

// Recompute gate, accent, slide and tie on the phrase already on screen. Which
// steps sound, and what pitch they are, is left completely alone.
// PROFILE is a shortcut for the four menus below it, not a separate layer.
// Choosing one writes those values into the menus so you can see what it did
// and adjust from there; touching any of them switches PROFILE to Custom.
const ART_PRESETS = {
  'Clean':    { gateFeel:'Medium', connectionFeel:'Semi-detached', slideProfile:'None',          accentProfile:'None' },
  'Punchy':   { gateFeel:'Short',  connectionFeel:'Detached',      slideProfile:'Rare',          accentProfile:'Moderate' },
  'Rolling':  { gateFeel:'Long',   connectionFeel:'Tied bias',     slideProfile:'Occasional',    accentProfile:'Light' },
  'Acidic':   { gateFeel:'Mixed',  connectionFeel:'Legato',        slideProfile:'Acid moderate', accentProfile:'Motif accents' },
  'Hypnotic': { gateFeel:'Long',   connectionFeel:'Connected runs',slideProfile:'Rare',          accentProfile:'None' },
  'Choppy':   { gateFeel:'Tight',  connectionFeel:'Detached',      slideProfile:'None',          accentProfile:'Moderate' },
  'Fluid':    { gateFeel:'Long',   connectionFeel:'Legato',        slideProfile:'Occasional',    accentProfile:'Light' },
  'Stabbed':  { gateFeel:'Tight',  connectionFeel:'Detached',      slideProfile:'None',          accentProfile:'Heavy' },
  'Tense':    { gateFeel:'Short',  connectionFeel:'Semi-detached', slideProfile:'Rare',          accentProfile:'Heavy' },
  'Released': { gateFeel:'Long',   connectionFeel:'Semi-detached', slideProfile:'Occasional',    accentProfile:'Light' }
};
const ART_MENUS = ['gateFeel','connectionFeel','slideProfile','accentProfile'];

function applyArtPreset(name){
  const p = ART_PRESETS[name];
  if(!p) return;                       // 'Custom' changes nothing
  S.artPresetting = true;
  try { ART_MENUS.forEach(function(k){ setEnum(k, p[k]); }); }
  finally { S.artPresetting = false; }
  sendMenuValues();
}

// changing one of the four by hand means the profile no longer describes them
function markArtCustom(){
  if(S.artPresetting) return;
  if(S.params.articulationProfile !== 'Custom'){
    setEnum('articulationProfile', 'Custom');
    sendMenuValues();
  }
}

// Which fields each menu is allowed to rewrite. JOIN decides whether notes run
// together, so it owns ties, the slides it adds, and the gate clamps that go
// with being detached - but it has no business re-rolling the accents.
const ARTIC_FIELDS = {
  gateFeel:            ['gate'],
  // no longer 'slide': JOIN uses ties, so the SLIDE menu owns slides alone
  connectionFeel:      ['gate','tie'],
  slideProfile:        ['slide'],
  accentProfile:       ['accent'],
  articulationProfile: ['gate','accent','slide','tie']
};

// Everything that alters a phrase ends here, so the tie and slide rules cannot
// be left behind by a path that forgot to call them.
function settlePhrase(ph, c){
  enforceTies(ph, c);
  normalisePhrase(ph);
}

function reapplyArticulation(name){
  const fields = ARTIC_FIELDS[name] || ['gate','accent','slide','tie'];
  const c = cfg();
  ['call','response'].forEach(function(which){
    const p = which === 'call' ? S.call : S.response;
    if(!p.length) return;
    let motifPos = 0;
    for(let i=0;i<p.length;i++){
      if(!p[i].active) continue;
      const a = articulationForStep(i, true, c, motifPos);
      // copy only what this menu owns; everything else stays as the user left it
      for(let f=0;f<fields.length;f++) p[i][fields[f]] = a[fields[f]];
      motifPos = (motifPos + 1) % 4;
    }
    // the genre pass moves accents and groups held notes, so run only the part
    // that matches what actually changed
    postProcessArticulation(p, c, fields);
    settlePhrase(p, c);
  });
  status((CTRL_LABELS[name] || 'Articulation') + ' updated');
}

function reapplyEnding(){
  const c = cfg();
  if(S.call.length){ applyEnding(S.call, c, 'call'); normalisePhrase(S.call); }
  if(S.response.length){ applyEnding(S.response, c, 'response'); normalisePhrase(S.response); }
  status('Ending: ' + c.endingBehaviour);
}

// Recompute surface detail over the existing rhythm and pitches. Which steps
// sound and what pitch they are stay exactly as they were.
function regenerateIfShapeChanged(){
  const c = cfg();
  if(S.call.length !== c.steps) generate('both', true);
}

function autoregen(v){
  S.autoRegen = v ? 1 : 0;
  rememberState();
  outlet(O_UI, 'autoregen', S.autoRegen);
  status(S.autoRegen ? 'Auto-regenerate on' : 'Auto-regenerate off');
}

// Kept only so an old [prepend enable] left in a patch is harmless. The device
// is always enabled now.
function enable(){
  ensureInit();
  S.enabled = 1;
  armMetro();
}

function action(what){
  ensureInit();
  const c = cfg();
  switch(what){
    case 'newcall':     generate('call'); break;
    case 'newresponse': generate('response'); break;
    case 'newboth': {
      // live.text differs across Live/Max builds: some send one bang, others
      // send a press and release. Accept bangs again, then collapse a possible
      // press/release pair here without blocking a later intentional click.
      const now=Date.now();
      if(S.lastNewPairActionMs===undefined || now-S.lastNewPairActionMs>=40){
        S.lastNewPairActionMs=now;
        requestNewPair();
      }
      break;
    }
    case 'mutatecall': {
      const t = (c.mutationTarget==='Response only' || c.mutationTarget==='Call + response linked')
        ? 'Whole phrase' : c.mutationTarget;
      S.call = mutatePhrase(S.call, c, t); status('Mutated call'); break;
    }
    case 'mutateresponse': {
      const t = (c.mutationTarget==='Call only' || c.mutationTarget==='Call + response linked')
        ? 'Whole phrase' : c.mutationTarget;
      S.response = mutatePhrase(S.response, c, t); status('Mutated response'); break;
    }
    case 'mutateboth': {
      const t = (c.mutationTarget==='Call only' || c.mutationTarget==='Response only' ||
                 c.mutationTarget==='Call + response linked') ? 'Whole phrase' : c.mutationTarget;
      S.call = mutatePhrase(S.call, c, t);
      S.response = mutatePhrase(S.response, c, t);
      status('Mutated both'); break;
    }
    case 'shiftcallleft':  S.call = shiftPhrase(S.call, 1); break;
    case 'shiftcallright': S.call = shiftPhrase(S.call, -1); break;
    case 'shiftrespleft':  S.response = shiftPhrase(S.response, 1); break;
    case 'shiftrespright': S.response = shiftPhrase(S.response, -1); break;
    case 'shiftbothleft':  S.call = shiftPhrase(S.call, 1); S.response = shiftPhrase(S.response, 1); break;
    case 'shiftbothright': S.call = shiftPhrase(S.call, -1); S.response = shiftPhrase(S.response, -1); break;
    case 'panic': panic(); break;
    case 'writeclip': writeClip(); break;
    default: status('Unknown action: ' + what);
  }
  // Let the interface show the press. Without this an action fired from a
  // mapped live.text, MIDI or automation changed the phrase with no visible
  // feedback, because the display only knew about its own clicks.
  outlet(O_UI, 'flash', what);
  pushDisplay();
}

function queueAction(kind){
  if(S.preparing) return;
  S.queuedAction=kind==='pair'||kind==='dice'?kind:'';
  S.queuedPair=S.queuedAction?1:0;
  outlet(O_UI,'queued',S.queuedPair,S.queuedAction);
}
// Kept as a small compatibility wrapper for saved-state and older test paths.
function queuePair(on){
  queueAction(on?'pair':'');
}
function queueIsRunning(){
  const recent=S.lastBangMs>=0 && Date.now()-S.lastBangMs<(RATE_BEATS[S.params.rate]||0.25)*60000/currentTempo()*2.5;
  // A recent sequencer bang is definitive proof that the quantized metro is
  // running. Live's is_playing observer can be one UI tick late, especially
  // when a control is pressed immediately after transport starts.
  return S.deviceActive && !S.finished && (recent || !!liveGet('is_playing',0));
}
function requestNewPair(){
  if(nativeEnabled) return nativeRequest('pair');
  if(S.queuedPair){
    const cancelled=S.queuedAction;
    queueAction('');
    status(cancelled==='dice'?'Randomization cancelled':'New pair cancelled');
    return;
  }
  if(S.params.newPairTiming==='Next Phrase' && queueIsRunning()){
    queueAction('pair');
    status('Queued for the next full phrase. Press New Pair again to cancel.');
  } else {
    allNotesOff(); pushUndo(); generate('both');
  }
}
function requestDice(){
  if(nativeEnabled) return nativeRequest('dice');
  if(S.queuedAction==='dice'){
    queueAction('');
    status('Randomization cancelled');
    return;
  }
  if(S.params.newPairTiming==='Next Phrase' && queueIsRunning()){
    // Dice also creates a new pair, so it supersedes a plain queued New Pair.
    queueAction('dice');
    status('Randomization queued for the next full phrase. Press the dice again to cancel.');
    return;
  }
  if(S.queuedPair) queueAction('');
  performDice(false);
}
function performDice(quiet){
  pushUndo();
  diceMenus().forEach(randEnum);
  generate('both',true);
  outlet(O_UI,'flash','dice');
  if(!quiet) status('Randomised: panel');
}
function applyQueuedPair(){
  if(nativeEnabled){ if(S.queuedPair) outlet(6,'apply'); return; }
  if(!S.queuedPair) return;
  const queued=S.queuedAction||'pair';
  const wasStopped=S.finished;
  queueAction('');
  allNotesOff();
  if(queued==='dice') performDice(true);
  else { pushUndo(); generate('both',true); }
  if(wasStopped){ S.finished=true; S.playing=false; outlet(O_XPORT,0); }
  status(queued==='dice'?'Randomization ready':'New pair ready');
}
function generate(which, quiet){
  // An explicit rebuild supersedes an earlier queued request.
  if(S.queuedPair && !S.preparing){
    if(nativeEnabled) nativeCancel();
    queueAction('');
  }
  // If anything has stopped playback, building a new phrase is an explicit
  // request to hear it, so put the metro back.
  if(S.finished) rearm();
  const c = cfg();
  if(which === 'call' || which === 'both') S.call = buildCall(c);
  if(!S.call.length) S.call = buildCall(c);
  if(which === 'response' || which === 'both') S.response = buildResponse(S.call, c);
  clearVariants();
  // a fresh phrase means the old steps have nothing worth remembering
  forgetDensityMemory(S.call);
  forgetDensityMemory(S.response);
  // The knob is an absolute target, so a freshly generated phrase has to meet
  // it as well. Without this the dial sat at 62% next to a phrase the style had
  // made sparse, and only became truthful once you touched it.
  applyDensity(true);
  applyOctaveJump(true);
  enforceTies(S.call, c);
  enforceTies(S.response, c);
  // Density and the ending both touch articulation after the strategy has run,
  // so a "same rhythm" answer has the last word restored here.
  // This one names both lanes, so it has to shape the call as well.
  if(S.params.responseStrategy === 'Call dominant / response tonic'){
    const fifth = fifthDegree(c);
    for(let i=0;i<S.call.length;i++){
      if(!S.call[i].active || S.call[i].degree == null) continue;
      if(chance(0.78)) S.call[i].degree = fifth;
    }
  }

  const rt0 = S.params.responseStrategy;
  if(rt0 === 'Same rhythm + resolve' || rt0 === 'Same rhythm + new contour'){
    for(let i=0;i<S.response.length && i<S.call.length;i++){
      const cs = S.call[i], rs = S.response[i];
      // density thins each lane on its own, which pulls the two rhythms apart
      if(cs.active && !rs.active){
        rs.active = true;
        if(rs.degree == null) rs.degree = cs.degree;
      } else if(!cs.active && rs.active){
        rs.active = false; rs.degree = null;
      }
      if(!cs.active) continue;
      rs.gate = cs.gate;
      rs.tie = cs.tie;
      rs.accent = cs.accent;
      rs.slide = cs.slide;
    }
  }
  pushDisplay();
  if(!quiet) status('New ' + which);
}

// --- randomisation --------------------------------------------------------

function setEnum(name, value){
  if(isOutputParam(name) && S.params[name] !== value) allNotesOff();
  S.params[name] = value;
  const list = DATA[ENUM_PARAMS[name]];
  if(!S.preparing) outlet(O_SETP, 'setparam', name, list.indexOf(value));
}
function randEnum(name){
  if(S.locks[name]) return;          // locked params are skipped by randomisation
  setEnum(name, rand(DATA[ENUM_PARAMS[name]]));
}

function lock(name, v){
  ensureInit();
  if(!ENUM_PARAMS[name]){ status('Unknown param to lock: ' + name); return; }
  S.locks[name] = v ? 1 : 0;
  rememberState();
  outlet(O_UI, 'menulock', name, S.locks[name]);
  status((S.locks[name] ? 'Locked ' : 'Unlocked ') + name);
}

// Snapshot before anything that rerolls, so the dice is reversible.
function pushUndo(){
  if(S.preparing) return;
  if(!S.undo) S.undo = [];
  S.undo.push({
    params: Object.assign({}, S.params),
    call: S.call.map(function(s){ return Object.assign({}, s); }),
    response: S.response.map(function(s){ return Object.assign({}, s); })
  });
  if(S.undo.length > 24) S.undo.shift();
  outlet(O_UI, 'undoable', 1);
}

// preset acid303 - classic Roland TB-303 acid house territory
const PRESETS = {
  acid303: {
    // Every value here was measured against eight Sting 2 clips rather than
    // chosen by ear. What those clips actually are: 14 notes of 16, EIGHTY
    // percent of them on one pitch class, only three or four distinct pitches
    // in the whole bar, octave leaps in both directions, and about half the
    // notes accented. A 303 line is a drone with octave drops, not a melody -
    // earlier versions of this preset kept trying to make it melodic.
    style:'Motif Study', motifType:'Acid nibble',
    responseStrategy:'Same rhythm + resolve', phraseMode:'Call then response',
    endingBehaviour:'Resolve to root',
    key:'A', scale:'Minor Pentatonic', rootOctave:'2', steps:'16', rate:'1/16',
    registerProfile:'Medium register', contourPreset:'Flat',
    repetitionPreset:'Very repetitive', notePoolMode:'Root-heavy',
    mutationAmount:'Small',
    articulationProfile:'Custom', gateFeel:'Mixed', connectionFeel:'Mixed ties',
    slideProfile:'Occasional', accentProfile:'Heavy',   // 22% held, Sting 19%
    noteLengthPct:100, swing:0,
    baseVelocity:70, accentDepth:75,
    density:90, resolve:70, octaveJump:40,
    driftCall:0, driftResponse:0, mutateAll:0
  }
};

function preset(name){
  ensureInit();
  const p = PRESETS[name];
  if(!p){ status('Unknown preset: ' + name); return; }
  pushUndo();
  S.restoring = true;
  try {
    for(const k in p) S.params[k] = p[k];
    const m = String(S.params.syncMode || 'Restart').toLowerCase();
    S.syncMode = (m.indexOf('time') === 0) ? 'timeline'
               : (m.indexOf('free') === 0) ? 'free' : 'restart';
    sendRate();
    pushAllParams();
  } finally { S.restoring = false; }
  generate('both', true);
  sendMenuValues();
  pushDisplay();
  status('Preset: ' + name);
}

function undo(){
  ensureInit();
  if(!S.undo || !S.undo.length){ status('Nothing to undo'); return; }
  const prev = S.undo.pop();

  S.restoring = true;
  try {
    // Undo changes the musical phrase, never the user's instrument routing.
    const outputs = {callOutput:S.params.callOutput, responseOutput:S.params.responseOutput};
    S.params = Object.assign({}, DEFAULTS, prev.params, outputs);
    const m = String(S.params.syncMode || 'Restart').toLowerCase();
    S.syncMode = (m.indexOf('time') === 0) ? 'timeline'
               : (m.indexOf('free') === 0) ? 'free' : 'restart';
    sendRate();
    pushAllParams();          // round-trips through the UI; guarded above
  } finally {
    S.restoring = false;
  }

  // restore the phrases after the parameter round-trip has settled
  S.call = prev.call.map(function(s){ return Object.assign({}, s); });
  S.response = prev.response.map(function(s){ return Object.assign({}, s); });

  sendMenuValues();
  pushDisplay();
  outlet(O_UI, 'undoable', S.undo.length ? 1 : 0);
  outlet(O_UI, 'flash', 'undo');
  status('Undone');
}

function random(what){
  ensureInit();
  switch(what){
    case 'motif':     randEnum('motifType'); break;
    case 'strategy':  randEnum('responseStrategy'); break;
    case 'style':     randEnum('style'); break;
    case 'keyscale':  randEnum('key'); randEnum('scale'); break;
    case 'art':
      randEnum('articulationProfile'); randEnum('gateFeel');
      randEnum('connectionFeel'); randEnum('slideProfile'); randEnum('accentProfile');
      break;
    case 'phrase':    randEnum('phraseMode'); break;

    case 'panel':     // the dice follows the selected New Pair timing
      requestDice();
      return;

    case 'call':      generate('call'); status('Randomised call'); return;
    case 'response':  generate('response'); status('Randomised response'); return;
    case 'both':      generate('both'); status('Randomised both'); return;

    case 'allx':
      ['style','responseStrategy','motifType','phraseMode','endingBehaviour','key','scale',
       'rootOctave','steps','registerProfile','densityPreset','contourPreset',
       'repetitionPreset','mutationAmount','mutationTarget','notePoolMode',
       'articulationProfile','gateFeel','connectionFeel','slideProfile',
       'accentProfile'].forEach(randEnum);
      break;

    case 'phrasesetup':
      ['key','scale','rootOctave','steps','registerProfile','densityPreset','contourPreset',
       'repetitionPreset','mutationAmount','mutationTarget','notePoolMode'].forEach(randEnum);
      break;

    case 'phrasesetupkeep':   // everything except key and steps
      ['scale','rootOctave','registerProfile','densityPreset','contourPreset',
       'repetitionPreset','mutationAmount','mutationTarget','notePoolMode'].forEach(randEnum);
      break;

    case 'keeprhythm': {      // keep the rhythm, nudge every pitch
      const c = cfg();
      S.call = normalisePhrase(S.call.map(s=>({...s,
        degree: s.active ? clamp((s.degree ?? 0) + rand([-2,-1,1,2]), 0, 10) : null})));
      S.response = buildResponse(S.call, c);
      pushDisplay();
      status('Randomised: keep rhythm');
      return;
    }
    case 'keeppitch': {       // keep the pitches, re-roll which steps sound
      const c = cfg();
      S.call = normalisePhrase(S.call.map(s=>({...s, active: chance(s.active ? 0.72 : 0.28)})));
      S.response = buildResponse(S.call, c);
      pushDisplay();
      status('Randomised: keep pitch');
      return;
    }
    case 'keepmotif': {       // tile the first four steps across the phrase
      const c = cfg();
      const motif = S.call.slice(0, Math.min(4, S.call.length)).map(s=>({...s}));
      if(!motif.length){ generate('both'); return; }
      S.call = normalisePhrase(Array.from({length:c.steps}, (_, i)=>({
        ...motif[i % motif.length],
        ...articulationForStep(i, motif[i % motif.length].active, c, i % motif.length)
      })));
      S.response = buildResponse(S.call, c);
      pushDisplay();
      status('Randomised: keep motif');
      return;
    }
    default: status('Unknown random: ' + what); return;
  }
  generate('both');           // source falls through to a full regenerate
  status('Randomised: ' + what);
  pushDisplay();
}

// --- clip writing ---------------------------------------------------------

// Find somewhere to write. Start at the highlighted slot and walk down the
// track until an empty one turns up, so an existing clip is never clobbered.
// If the track is full to the bottom, append a scene.
function freeClipSlot(){
  let trackPath = 'live_set view selected_track';
  let track;
  try {
    track = new LiveAPI(trackPath);
    if(!track || !track.id || track.id == 0) return null;
  } catch(e){ return null; }

  let count = 0;
  try { count = track.getcount('clip_slots'); } catch(e){ count = 0; }
  if(!count) return null;

  // index of the highlighted slot within this track
  let start = 0;
  try {
    const hi = new LiveAPI('live_set view highlighted_clip_slot');
    if(hi && hi.id && hi.id != 0){
      for(let i=0;i<count;i++){
        const s = new LiveAPI(trackPath + ' clip_slots ' + i);
        if(s && s.id == hi.id){ start = i; break; }
      }
    }
  } catch(e){}

  for(let i=start;i<count;i++){
    const p = trackPath + ' clip_slots ' + i;
    try {
      const s = new LiveAPI(p);
      const hc = s.get('has_clip');
      if((Array.isArray(hc) ? hc[0] : hc) != 1){
        if(i !== start) status('Slot busy, wrote to scene ' + (i + 1));
        return p;
      }
    } catch(e){}
  }

  // every slot from here down is taken, so make a new scene
  try {
    const ls = new LiveAPI('live_set');
    ls.call('create_scene', -1);
    status('All slots busy, added a scene');
    return trackPath + ' clip_slots ' + count;
  } catch(e){ return null; }
}

function writeClip(){
  ensureInit();
  const c = cfg();
  const plan = passPlan(c.phraseMode);
  const passes = plan.interleave !== undefined ? 1 : (plan.seq ? plan.seq.length : 2);
  const stepBeats = RATE_BEATS[S.params.rate] || 0.25;

  let notes;
  const target = S.params.clipTarget;
  if(target === 'Call')          notes = renderNotes(c, {seq:[C]}, 1);
  else if(target === 'Response') notes = renderNotes(c, {seq:[R]}, 1);
  else if(target === 'Layered'){
    notes = renderNotes(c, {seq:[C]}, 1).concat(renderNotes(c, {seq:[R]}, 1));
  } else {
    notes = renderNotes(c, plan, passes);
  }

  const totalSteps = (target === 'Call' || target === 'Response' || target === 'Layered')
    ? c.steps : c.steps * passes;
  let length = totalSteps * stepBeats;
  const numerator=Math.max(1,Number(liveGet('signature_numerator',4))||4);
  const denominator=Math.max(1,Number(liveGet('signature_denominator',4))||4);
  const barBeats=numerator*4/denominator;
  if(S.params.clipLength==='Whole bars') length=Math.max(barBeats,Math.ceil((length-1e-9)/barBeats)*barBeats);
  const clipName='C&R '+target+' - '+c.key+' '+c.scaleName;
  const clipColor=target==='Call' ? 0x6ef200 : target==='Response' ? 0x9733fa : 0x8895a7;

  try {
    const slotPath = freeClipSlot();
    if(!slotPath){ status('No clip slot available'); return; }
    const slot = new LiveAPI(slotPath);
    if(!slot || !slot.id || slot.id === 0){ status('No clip slot selected'); return; }
    slot.call('create_clip', length);

    const clip = new LiveAPI(slotPath + ' clip');
    clip.call('set_notes');
    clip.call('notes', notes.length);
    for(let i=0;i<notes.length;i++){
      const n = notes[i];
      clip.call('note', n.pitch, n.start, n.dur, n.vel, 0);
    }
    clip.call('done');
    clip.set('name',clipName);
    clip.set('color',clipColor);
    clip.set('signature_numerator',numerator);
    clip.set('signature_denominator',denominator);
    clip.set('loop_start',0);
    clip.set('loop_end', length);
    clip.set('looping',1);
    status('Wrote '+clipName+' ('+length+' beats)');
  } catch(e){
    status('Clip write failed: ' + e.message);
  }
}

// --- UI sync --------------------------------------------------------------

function pushAllParams(){
  for(const name in ENUM_PARAMS){
    const list = DATA[ENUM_PARAMS[name]];
    const idx = list.indexOf(S.params[name]);
    outlet(O_SETP, 'setparam', name, idx < 0 ? 0 : idx);
  }
}

// --- persistence ----------------------------------------------------------

// One Stored Only pattr blob is bound to this v8 object's get/setvalueof.
// It is separate from the existing automatable controls and includes musical
// state those controls cannot represent. Runtime IDs, notes-off Tasks and
// queued actions are never stored. Legacy six-field phrases still load below.
let deviceReady=false, pendingState=null, lastStoredState='';
function packPhrase(phrase){
  return {steps:phrase || [],removed:(phrase && phrase.__silenced) || []};
}
function unpackPhrase(value){
  if(!value || !Array.isArray(value.steps) || value.steps.length>256) throw new Error('Invalid phrase');
  const phrase=value.steps.map(function(s){
    if(!s || typeof s!=='object' || (s.degree!=null && !Number.isFinite(s.degree)) || !Number.isFinite(s.gate)) throw new Error('Invalid note');
    return Object.assign({},s);
  });
  if(value.removed && value.removed.length) phrase.__silenced=value.removed.slice();
  return phrase;
}
function packVariants(){
  const out={};
  for(const lane of ['call','response']) if(S.variants && S.variants[lane]){
    out[lane]={};
    for(const key of Object.keys(S.variants[lane])) out[lane][key]=packPhrase(S.variants[lane][key]);
  }
  return out;
}
function stateValue(){
  return 'CRG2:'+encodeURIComponent(JSON.stringify({version:2,params:S.params,
    call:packPhrase(S.call),response:packPhrase(S.response),variants:packVariants(),
    locks:S.locks,mute:S.mute,autoRegen:S.autoRegen,followLive:S.followLive,
    liveScale:{root:S.liveRoot,intervals:S.liveIntervals,name:S.liveScaleName}}));
}
function getvalueof(){ return pendingState || stateValue(); }
function rememberState(){
  if(S.preparing) return;
  if(!deviceReady || S.restoring) return;
  const value=stateValue();
  if(value===lastStoredState) return;
  lastStoredState=value;
  if(typeof notifyclients==='function') notifyclients();
}
function loadready(){
  ensureInit();
  deviceReady=true;
  if(pendingState){ const value=pendingState; pendingState=null; applyState(value); }
  rememberState();
}
function setvalueof(value){
  value=String(value || '');
  if(value.indexOf('CRG2:')!==0) return;
  // Live's native parameter restoration must finish before the blob wins.
  if(!deviceReady){ pendingState=value; return; }
  applyState(value);
}
function restorestate(value){ setvalueof(value); }
function applyState(value){
  try {
    const saved=JSON.parse(decodeURIComponent(value.slice(5)));
    if(saved.version!==2 || !saved.params) throw new Error('Unsupported saved state');
    const params=Object.assign({},DEFAULTS);
    for(const name of Object.keys(params)){
      if(ENUM_PARAMS[name]){
        if(DATA[ENUM_PARAMS[name]].indexOf(saved.params[name])>=0) params[name]=saved.params[name];
      } else if(NUM_RANGES[name] && Number.isFinite(saved.params[name])){
        const r=NUM_RANGES[name]; params[name]=clamp(saved.params[name],r[0],r[1]);
      }
    }
    const call=unpackPhrase(saved.call), response=unpackPhrase(saved.response), variants={};
    if(call.length!==Number(params.steps) || response.length!==Number(params.steps)) throw new Error('Saved phrase length mismatch');
    for(const lane of ['call','response']) if(saved.variants && saved.variants[lane]){
      variants[lane]={};
      for(const key of Object.keys(saved.variants[lane])) variants[lane][key]=unpackPhrase(saved.variants[lane][key]);
    }
    allNotesOff(); queuePair(0);
    S.restoring=true;
    S.params=params; S.call=call; S.response=response; S.variants=variants; S.shownVariant=null;
    S.locks={};
    for(const name of Object.keys(ENUM_PARAMS)) if(saved.locks && saved.locks[name]) S.locks[name]=1;
    S.mute={call:saved.mute && saved.mute.call?1:0,response:saved.mute && saved.mute.response?1:0};
    S.autoRegen=saved.autoRegen?1:0; S.followLive=saved.followLive?1:0;
    const scale=saved.liveScale || {};
    S.liveRoot=Number.isFinite(scale.root)?scale.root:-1;
    S.liveIntervals=Array.isArray(scale.intervals)?scale.intervals.slice():null;
    S.liveScaleName=String(scale.name || '');
    S.syncMode=String(params.syncMode).toLowerCase();
    S.stepIdx=0; S.passIdx=0; S.posOrigin=undefined; S.shownKey=null;
    if(settleTask) settleTask.cancel();
    sendRate(); pushAllParams();
    for(const name of NUM_PARAMS) outlet(O_SETP,'setparam',name,S.params[name]);
    S.restoring=false;
    sendMenuValues(); pushDisplay();
    if(S.followLive){ initLiveScale(); readLiveScale(); }
  } catch(e){ S.restoring=false; status('State restore failed: '+e.message); }
}

function serialisePhrase(p){
  const out = [];
  for(let i=0;i<p.length;i++){
    const s = p[i];
    out.push(s.active?1:0, s.degree==null?-1:s.degree,
             Math.round((s.gate ?? 0.8)*1000),
             s.accent?1:0, s.slide?1:0, s.tie?1:0);
  }
  return out;
}
function deserialisePhrase(arr, offset, count){
  const out = [];
  for(let i=0;i<count;i++){
    const b = offset + i*6;
    out.push({
      active: arr[b]===1,
      degree: arr[b+1] === -1 ? null : arr[b+1],
      gate: arr[b+2]/1000,
      accent: arr[b+3]===1,
      slide: arr[b+4]===1,
      tie: arr[b+5]===1,
      dur: 1
    });
  }
  return out;
}

function save(){
  embedmessage('restorestate',getvalueof());
}

function restore(){
  if(pendingState || deviceReady) return; // a complete saved state takes precedence
  const a = arrayfromargs(arguments);
  const n = a[0];
  if(!n || a.length < 1 + n*12) return;
  S.call = deserialisePhrase(a, 1, n);
  S.response = deserialisePhrase(a, 1 + n*6, n);
  pushDisplay();
}

// --- misc -----------------------------------------------------------------

function dump(){
  const c = cfg();
  post('--- Call / Response Generator ---\n');
  post('steps', c.steps, 'key', c.key, c.scaleName, '\n');
  ['call','response'].forEach(function(lane){
    const p = lane === 'call' ? S.call : S.response;
    let line = lane + ': ';
    for(let i=0;i<p.length;i++){
      const s = p[i];
      line += s.active && s.degree != null
        ? degreeToMidi(s.degree, c) + (s.accent?'>':'') + (s.slide?'~':'') + (s.tie?'_':'') + ' '
        : '. ';
    }
    post(line + '\n');
  });
}


// ---------------------------------------------------------------------------
// 10. Diagnostics
//
//   [selftest(   -> full state dump to the Max Console
//   [testnote(   -> fire one note immediately, bypassing transport and enable
//   [forcestart( -> run the metro without waiting for Live's transport
//   [forcestop(
// ---------------------------------------------------------------------------

function selftest(){
  ensureInit();
  post('\n===== Call/Response Generator selftest =====\n');
  post('crg.js version     : ' + CRG_VERSION + '\n');
  post('muted lanes       : ' + (S.mute.call ? 'call ' : '') + (S.mute.response ? 'response' : '') + '\n');
  post('locked params      : ' + (Object.keys(S.locks).filter(function(k){return S.locks[k];}).join(', ') || 'none') + '\n');
  post('metro running      : ' + S.playing + '\n');
  post('follow Live scale  : ' + (S.followLive ? ('yes  ' + S.liveScaleName + ' root ' + S.liveRoot) : 'no') + '\n');
  post('sync mode          : ' + S.syncMode + (S.origin >= 0 ? '  (anchor ' + S.origin + ')' : '') + '\n');
  post('song position      : ' + liveGet('current_song_time', -1) + ' beats\n');
  post('metro grid         : ' + (RATE_MAXNAME[S.params.rate] || '8n') + '  (interval and quantize)\n');
  post('auto-regenerate    : ' + S.autoRegen + '\n');
  post('call steps         : ' + S.call.length + '\n');
  post('response steps     : ' + S.response.length + '\n');

  const c = cfg();
  post('rate / step        : ' + S.params.rate + '  (' + (RATE_BEATS[S.params.rate]) + ' beats)\n');
  post('key / scale        : ' + c.key + ' ' + c.scaleName + '  root=' + c.rootOctave + '\n');
  post('phrase mode        : ' + c.phraseMode + '\n');
  post('steps param        : ' + JSON.stringify(S.params.steps) + ' -> cfg=' + c.steps + '\n');
  post('channels           : call=' + (c.callChannel+1) + ' response=' + (c.responseChannel+1) + '\n');

  const active = S.call.filter(function(s){ return s.active; }).length;
  post('active call steps  : ' + active + ' of ' + S.call.length + '\n');
  if(active === 0) post('  !! call phrase is empty - press New both\n');

  const a = song();
  post('LiveAPI live_set   : ' + (a ? ('ok, id=' + a.id) : 'NOT RESOLVED') + '\n');
  const playing = liveGet('is_playing', -1);
  post('Live tempo         : ' + liveGet('tempo', -1) + '\n');
  post('Live is_playing    : ' + playing + '\n');
  if(playing != 1){
    post('  !! [metro @quantize 8n] only bangs while Live\'s transport is\n');
    post('     rolling. With Live stopped, even forcestart produces nothing.\n');
    post('     Press play in Live, or remove @quantize from the metro.\n');
  }
  try {
    const slot = new LiveAPI('live_set view highlighted_clip_slot');
    post('highlighted slot   : ' + ((slot && slot.id && slot.id != 0) ? ('id=' + slot.id) : 'none selected') + '\n');
  } catch(e){
    post('highlighted slot   : FAILED - ' + e.message + '\n');
  }

  const ev = stepEvent(S.call, 0, c, C);
  post('first call event   : ' + (ev ? ('pitch=' + ev.pitch + ' vel=' + ev.vel +
       ' dur=' + ev.dur.toFixed(2) + ' steps ch=' + (ev.chan+1)) : 'step 0 is a rest') + '\n');
  post('===========================================\n\n');
}

function testnote(){
  ensureInit();
  // straight out of outlet 0, ignoring enable and transport entirely.
  // If you hear nothing from this, the problem is downstream of [v8]:
  // outlet 0 -> [midiout], or no instrument on the track.
  post('crg: firing C3 on channel 1 for 500ms\n');
  rawNote(60, 100, 0);
  const t = new Task(function(){ rawNote(60, 0, 0); });
  t.schedule(500);
}

function forcestart(){
  ensureInit();
  post('crg: forcing metro on (ignoring Live transport)\n');
  if(liveGet('is_playing', 0) != 1){
    post('crg: NOTE - Live is stopped. [metro @quantize 8n] will not bang\n');
    post('     until the transport rolls. Send "freerun" to drop quantize.\n');
  }
  S.enabled = 1;
  start();
}

// Remove transport quantization so the metro free-runs. Useful for proving the
// MIDI path works without pressing play in Live.
function freerun(){
  ensureInit();
  outlet(O_XPORT, 'quantize', 0);
  outlet(O_XPORT, 1);
  S.enabled = 1;
  S.playing = true;
  post('crg: metro free-running, quantize off\n');
}

function version(){ post('crg.js v' + CRG_VERSION + '\n'); status('crg.js v' + CRG_VERSION); }

function forcestop(){ stop(); }

// syncmode 1 (default): step position follows Live's song position, so the
//   phrase always lines up with the timeline and restarts from step 1.
// syncmode 0: free-running counter, advancing one step per metro bang.
// syncmode restart  - phrase starts at step 1 wherever you press play (default)
// syncmode timeline - phrase pinned to the arrangement's absolute position
// syncmode free     - counter, one step per metro bang
function syncmode(v){
  let m = v;
  if(v === 0 || v === '0') m = 'free';
  if(v === 1 || v === '1') m = 'timeline';
  if(v === 2 || v === '2') m = 'restart';
  if(['restart','timeline','free'].indexOf(m) === -1){
    post('crg: syncmode must be restart, timeline or free\n'); return;
  }
  S.syncMode = m;
  S.params.syncMode = (m === 'timeline') ? 'Timeline' : (m === 'free' ? 'Free' : 'Restart');
  S.origin = -1;
  sendMenuValues();
  post('crg: sync mode ' + m + '\n');
  status('Sync: ' + m);
}


// ---------------------------------------------------------------------------
// Boot. Deferred rather than immediate: LiveAPI is not safe to touch at script
// load time. If [live.thisdevice] fires first, init() is already done and this
// is a no-op.
// ---------------------------------------------------------------------------
const bootTask = new Task(function(){
  if(!initDone){
    if(S.verbose) post('crg v' + CRG_VERSION + ': self-initialising after script load\n');
    init();
  }
});
bootTask.schedule(300);


// The v8ui reloads independently of this script, so let it ask for the current
// phrases rather than sitting blank until the next edit.
// Passed through to the display. The natural place to send this is the v8ui,
// but its inlet is one more cord away than the engine's, so accept it here too
// and forward rather than erroring.
// ---------------------------------------------------------------------------
// Per-item help
//
// Descriptions are derived from the engine's own tables wherever the numbers
// exist, so they cannot drift out of step with what the code does. Only the
// values with no numeric character of their own are written by hand.
// ---------------------------------------------------------------------------

const ITEM_TIPS = {
  // response strategies - what the response is built from
  'Same rhythm + resolve':'Keeps the call\'s rhythm exactly; nudges each pitch and settles.',
  'Same rhythm + new contour':'Keeps the rhythm, sends the pitch line the opposite way.',
  'Motif + simplify':'Takes the opening cell and thins it out.',
  'Motif + extend':'Takes the opening cell and holds notes across the gaps.',
  'Motif + truncate':'Takes the opening cell and empties the last third.',
  'Echo':'Repeats the call two steps later.',
  'Answer upward':'The motif, moved up one to three degrees.',
  'Answer downward':'The motif, moved down one or two degrees.',
  'Higher answer':'The motif, moved up one to three degrees.',
  'Lower answer':'The motif, moved down one or two degrees.',
  'Invert contour':'Flips the line about its average pitch.',
  'Mirror contour loosely':'Flips the line about its average pitch.',
  'Copy rhythm / change notes':'Same rhythm, fresh pitches from the note pool.',
  'Copy notes / change rhythm':'Same pitches, re-rolls which steps sound.',
  'Transpose motif up':'The motif, two to four degrees higher.',
  'Transpose motif down':'The motif, two or three degrees lower.',
  'Response resolves to root':'Forces the last three steps onto the root.',
  'Response resolves to 5th':'Forces the last three steps onto the fifth.',
  'Response resolves lower':'Pulls the ending down a degree.',
  'Response resolves harder':'Last four steps land on stable tones, accented.',
  'Repeat then break':'Copies the call, then breaks away in the last two steps.',
  'Tighter answer':'Same notes, gates cut to about half.',
  'Sparser answer':'Drops most of the offbeats.',
  'Busier answer':'Fills gaps with repeats of the previous note.',
  'Accent the answer':'Accents every other step.',
  'Slide the answer':'Adds glides and stretches gates past the step.',
  'Staccato answer':'All gates cut to 0.42, ties off.',
  'Legato answer':'All gates 1.08, notes run into each other.',
  'Delayed answer':'Echo, shifted one step later, first step silent.',
  'Half-phrase answer':'Silences the first half.',
  'Fragment reply':'Starts from a third of the way into the call.',
  'Tail answer':'Starts from the middle of the call.',
  'Opposite register':'Moves three degrees away from the call.',
  'Same motif darker':'The motif, one degree lower throughout.',
  'Same motif brighter':'The motif, one degree higher throughout.',
  'Question then closure':'Penultimate step tense, final step stable.',
  'Hook then fill':'First half copies the call, second half varies it.',
  'Call dominant / response tonic':'The call sits on the fifth, the answer on the root.',

  // endings
  'Resolve to root':'Final note forced to the root.',
  'Resolve to 5th':'Final note forced to the fifth.',
  'Leave hanging':'No cadence applied - the last note stays wherever it landed.',
  'Rise into next phrase':'Final note a degree above the one before it.',
  'Fall into next phrase':'Final note a degree below the one before it.',
  'Hard stop':'Final gate cut to 0.35, tie removed.',
  'Fade energy':'Final note unaccented, gate 0.55.',
  'Strong cadence':'Final note from root, third or fifth, weighted to the root.',
  'Weak cadence':'Final note from the second, fourth or sixth.',
  'Ambiguous ending':'Final note from the second, third or sixth.',
  'Response more resolved than call':'Response ends on a stable tone.',
  'Response same tension as call':'No cadence difference between the lanes.',
  'Response more tense than call':'Response ends on an unstable tone.',

  // articulation
  'Clean':'No colouration - gate, accent and slide come from the other four menus.',
  'Punchy':'Accents on a quarter of notes, gate capped at 0.68.',
  'Rolling':'Gate at least 0.95, occasional ties.',
  'Acidic':'Accents and slides follow the style, gate at least 0.86.',
  'Hypnotic':'Long, even gates.',
  'Choppy':'Gate capped at 0.55, slides off.',
  'Fluid':'Gate at least 1.05, notes overlap.',
  'Stabbed':'Gate capped at 0.5, a third of notes accented.',
  'Tense':'Accents on a third of notes, slides rare.',
  'Released':'Gate at least 0.95, accents rare.',
  'Tight':'Gate 0.45.', 'Short':'Gate 0.6.', 'Medium':'Gate 0.82.', 'Long':'Gate 1.15.',
  'Mixed':'Gate cycles 0.55 / 0.7 / 0.85 / 1.0 / 1.15 across steps.',
  'Random within bounds':'Gate anywhere from 0.45 to 1.25.',
  // JOIN is about how often a note runs into the next, and in what shape.
  // Ties only ever join notes of the same pitch - a move to a different pitch
  // is a slide, which the SLIDE menu owns.
  'Detached':'No ties at all, and notes cut short - clear gaps between them.',
  'Semi-detached':'No ties, but notes run their full length up to the next step.',
  'Mixed ties':'Ties about a fifth of notes, nearly always singly.',
  'Tied bias':'Ties about a third of notes, mostly singly.',
  'Connected runs':'Same number of ties as Tied bias, but grouped into chains of two and three.',
  'Legato':'Ties as many notes as possible - the longest, smoothest line.',
  'None':'Off.', 'Rare':'6% of notes.', 'Occasional':'16% of notes.',
  'Acid moderate':'28% of notes.', 'Acid heavy':'42% of notes.',
  'Phrase-end slides':'Only towards the end of the cell.',
  'Upward slides bias':'18% of notes.', 'Downward slides bias':'18% of notes.',
  'Light':'14% of notes.', 'Moderate':'24% of notes.', 'Heavy':'38% of notes.',
  'Offbeat accents':'Every offbeat.',
  'Syncopated accents':'Half the offbeats, chosen at random.',
  'Downbeat accents':'Every fourth step.',
  'Motif accents':'The first step of each cell.',
  'Random accents within motif':'22% of notes.'
};

// Values that fall through to the generic branch: they still generate, they
// just have no shape of their own. Worth saying rather than implying otherwise.
// every motif now has a branch; kept as an empty list so describeItem still
// has something to check if one is ever added back
const GENERIC_MOTIFS = [];

const MOTIF_TIPS = {
  'Push':'Leans on the step before the strong beat, pushing upward.',
  'Four-on-floor companion':'One note on the strong step of each cell, with the kick.',
  'Drone anchor':'A single held pitch, no movement.',
  'Pedal point motif':'A single held pitch, no movement.',
  'Call tag':'A short two-step figure that rises at the cell end.',
  'Answer tag':'A short two-step figure that falls at the cell end.'
};

function describeItem(param, value){
  if(param === 'style'){
    // Only describe what a style actually still controls. It used to claim
    // density, repetition, slides and accents, which their own menus now own -
    // a tooltip promising 'dense, heavily accented' while the DENSITY and
    // ACCENT menus said otherwise was worse than no tooltip at all.
    const c0 = cfg();
    const saved = S.params.style;
    S.params.style = value;
    const p = styleProfile(cfg());
    S.params.style = saved;
    const cat = styleCategory(value);
    const bits = [];
    if(p.leap > 0.22) bits.push('leaps about');
    else if(p.leap < 0.13) bits.push('moves in small steps');
    if(p.rootBias > 0.34) bits.push('anchored to the root');
    if(p.restBias > 0.26) bits.push('leaves gaps');
    else if(p.restBias < 0.13) bits.push('keeps going');
    if(p.motifVar < 0.12) bits.push('holds its figure');
    else if(p.motifVar > 0.24) bits.push('reshapes as it goes');
    if(p.responseTightness > 0.72) bits.push('tight answers');
    else if(p.responseTightness < 0.5) bits.push('loose answers');
    if(p.sustainBias > 0.3) bits.push('lets notes ring');
    if(p.choppyBias > 0.26) bits.push('choppy');
    if(p.offbeatBias > 0.26) bits.push('sits on the offbeat');
    const groove = cat === 'acid' ? 'Accents pull to the downbeat and the root'
                 : cat === 'techno' ? 'Accents pull to the downbeat'
                 : cat === 'house' ? 'Accents pull to the offbeat'
                 : 'No genre push on the accents';
    // capitalise the first trait so the sentence does not read "root. lets"
    let tail = '';
    if(bits.length){
      const j = bits.join(', ');
      tail = ' ' + j.charAt(0).toUpperCase() + j.slice(1) + '.';
    }
    // An offbeat feel needs gaps to be audible: fill every step and there is no
    // offbeat left to sit on. Measured, it holds to about 60% and fades above.
    const needsRoom = (p.offbeatBias > 0.26)
      ? ' Keep DENSITY below about 60% or there are no gaps for the offbeats to show.'
      : '';
    return groove + '.' + tail + needsRoom;
  }

  if(param === 'motifType'){
    if(MOTIF_TIPS[value]) return MOTIF_TIPS[value];
    if(GENERIC_MOTIFS.indexOf(value) !== -1)
      return 'No shape of its own - falls back to the generic random cell.';
    return 'Shapes the short cell the phrase is built from.';
  }

  if(param === 'scale'){
    const iv = SCALE_INTERVALS[value];
    if(!iv) return '';
    return iv.length + ' notes: ' + iv.join(', ') + ' semitones from the root.';
  }

  if(param === 'notePoolMode'){
    const c = cfg();
    const saved = S.params.notePoolMode;
    S.params.notePoolMode = value;
    const pool = notePool(cfg());
    S.params.notePoolMode = saved;
    const uniq = [];
    pool.slice().sort(function(a,b){ return a-b; }).forEach(function(d){
      if(uniq.length === 0 || uniq[uniq.length-1] !== d) uniq.push(d);
    });
    const rep = pool.length > uniq.length ? ' (weighted)' : '';
    return 'Degrees available: ' + uniq.join(', ') + rep + '.';
  }

  if(param === 'registerProfile'){
    // Describe it in octaves, since that is what the ear hears - and a scale
    // degree is a different size in a five-note scale than in a seven-note one.
    const saved = S.params.registerProfile;
    S.params.registerProfile = value;
    const ceil = registerCeiling(cfg());
    S.params.registerProfile = saved;
    const oct = (ceil / Math.max(3, cfg().scale.length)).toFixed(1);
    return 'Up to about ' + oct + ' octaves in this scale (' + ceil + ' scale degrees). ' +
           'OCT JUMP adds on top of this.';
  }

  if(param === 'repetitionPreset')
    return 'Cell repeats literally about ' + Math.round(repetitionValue(value) * 100) + '% of the time.';

  if(param === 'mutationAmount'){
    const amt = {'Tiny':1,'Small':2,'Medium':3,'Strong':5,'Brutal':'most'}[value];
    return 'Mutate changes ' + amt + ' step' + (amt === 1 ? '' : 's') + '.';
  }

  if(param === 'phraseMode'){
    const plan = passPlan(value);
    if(plan.interleave !== undefined)
      return 'Alternates lanes every ' + (plan.interleave === 'half' ? 'half bar' : plan.interleave + ' steps') +
             (plan.flip ? ', starting with the response.' : '.');
    if(plan.form)
      return 'A fixed form: ' + plan.form.join(' / ') + '. Each letter is its own phrase, ' +
             'built once and replayed, so the form repeats exactly. Use the lane MUTATE ' +
             'sliders if you want it to keep changing.';
    const seq = (plan.seq || []).map(function(x){ return x === C ? 'call' : 'response'; });
    let t = 'Plays ' + seq.join(' then ') + ', looping.';
    if(plan.vary) t += ' Mutates as it goes.';
    if(value === 'Call sparse, response busy') t += ' The call is thinned and the response filled out.';
    if(value === 'Call busy, response sparse') t += ' The call is filled out and the response thinned.';
    if(value === 'Call with response fill') t += ' The response is silent on the other three.';
    return t;
  }

  if(param === 'accentDepth') return 'How much louder an accented note is.';
  if(param === 'gateFeel'){
    const g = {'Pluck':0.06,'Stab':0.10,'Tight':0.15,'Short':0.25,
               'Medium':0.54,'Long':0.85,'Full':0.95}[value];
    if(g){
      const sb = RATE_BEATS[S.params.rate] || 0.25;
      const ms = Math.round(g * sb * (60000 / currentTempo()));
      return Math.round(g * 100) + '% of a step - about ' + ms + 'ms at the current rate and tempo.';
    }
    if(value === 'Mixed') return 'Mostly half steps and quarters, a few longer.';
    if(value === 'Mixed short') return 'Weighted short: mostly plucks and stabs.';
    if(value === 'Wide mix') return 'The whole range in one bar, from a click to a held note.';
    if(value === 'Random within bounds') return 'A different length every step.';
  }
  if(param === 'articulationProfile'){
    if(value === 'Custom') return 'The four menus below are set by hand.';
    const p = ART_PRESETS[value];
    if(p) return 'Sets gate ' + p.gateFeel + ', join ' + p.connectionFeel +
                 ', slide ' + p.slideProfile + ', accent ' + p.accentProfile + '.';
  }
  if(param === 'contourPreset'){
    return {
      'Flat':'Holds one pitch. With OCT JUMP at 0 the whole phrase is a single note.',
      'Slight rise':'Climbs about three degrees from start to end.',
      'Slight fall':'Starts high and comes down about three degrees.',
      'Arch':'Rises to the middle of the bar, then falls back.',
      'Inverted arch':'Starts and ends high, dipping through the middle.',
      'Wandering':'No overall direction, but roams further between notes.',
      'Stepwise':'No overall direction; moves a degree at a time.',
      'Leapy':'No overall direction; jumps in thirds and larger.'
    }[value] || 'Pitch shape across the phrase.';
  }
  if(param === 'mutationTarget') return 'What Mutate is allowed to change.';

  // shared table last, so a name used by two menus resolves to the right one
  if(ITEM_TIPS[value]) return ITEM_TIPS[value];
  return '';
}

// Asked for by the display when the cursor is over an option in a popup.
function itemtip(param, idx){
  const list = DATA[ENUM_PARAMS[param]];
  if(!list){ outlet(O_UI, 'itemtip', ''); return; }
  const i = clamp(Math.round(idx), 0, list.length - 1);
  outlet(O_UI, 'itemtip', describeItem(param, list[i]) || '');
}

// ---------------------------------------------------------------------------
// Transport position from Max rather than from the Live API.
//
// [transport] and [plugsync~] report the position on the scheduler thread, in
// step with the metro. LiveAPI's current_song_time is fetched on a deferred
// thread and can be a whole bang behind, which is why loops misbehaved: at the
// loop point it still returned the pre-loop position.
//
// Send one of these immediately before each metro bang:
//    posticks <ticks>     from [transport @unitstyle ticks]
//    pos <beats>          if you already have beats
// ---------------------------------------------------------------------------
function posticks(t){ pos(Number(t) / 480); }

function pos(beats){
  const b = Number(beats);
  if(!isFinite(b)) return;
  S.extPos = b;
  S.extPosAt = Date.now();
}

// A position is only believed once it has proved it moves like one: roughly a
// step per bang. Feeding it bars, or beats-within-a-bar, gives a value that
// barely changes or wraps, and trusting that pins the phrase to step 1.
function havePos(nowMs, stepBeats){
  if(S.extPosAt === undefined || (nowMs - S.extPosAt) >= 250) return false;
  if(S.posTrusted) return true;

  const prev = S.posPrev;
  S.posPrev = S.extPos;
  if(prev === undefined) return false;
  const moved = S.extPos - prev;

  // one step forward, or a jump backwards (a loop) - both are plausible
  if(Math.abs(moved - stepBeats) < stepBeats * 0.5 || moved < -stepBeats){
    S.posGood = (S.posGood || 0) + 1;
    if(S.posGood >= 8){
      S.posTrusted = true;
      S.posAdopt = true;      // take over from the count without jumping
      post('crg: using the transport position from the patch\n');
    }
  } else {
    S.posBad = (S.posBad || 0) + 1;
    S.posGood = 0;
    if(S.posBad === 8){
      post('crg: the position being sent does not advance a step per bang ' +
           '(got ' + moved.toFixed(3) + ' beats) - ignoring it and counting instead. ' +
           'Check that posticks is fed ticks, not bars.\n');
    }
  }
  return false;
}

// Re-roll one articulation attribute. The menu keeps its value and the phrase
// keeps its notes; only the thing that menu owns is rolled again.
const REROLLABLE = ['gateFeel','connectionFeel','slideProfile','accentProfile'];

function reroll(name){
  ensureInit();
  if(REROLLABLE.indexOf(name) === -1){ status('Nothing to reroll for ' + name); return; }
  pushUndo();
  reapplyArticulation(name);
  outlet(O_UI, 'flash', 'reroll:' + name);
  pushDisplay();
}

// Sent on to the display. Accepting them here as well means the patch can wire
// everything to one inlet rather than remembering which object wants what.
function theme(name){ outlet(O_UI, 'theme', String(name)); }

// 'verbose 1' - report the things that go right as well as the things that
// do not. Off by default: a device that loaded correctly has nothing to say.
function verbose(v){
  S.verbose = v ? 1 : 0;
  post('crg v' + CRG_VERSION + ': verbose ' + (S.verbose ? 'on' : 'off') + '\n');
}
function themecolor(){ outlet(O_UI, ['themecolor'].concat(arrayfromargs(arguments))); }

function hoverparam(name, state){
  if(state === undefined) outlet(O_UI, 'hoverparam', name);
  else outlet(O_UI, 'hoverparam', name, state);
}

function refresh(){
  ensureInit();
  sendMenuItems();
  pushDisplay();
}

// Force the UI menus to match the JS state. Not called at load on purpose -
// see init(). Useful if the two ever drift.
function sync(){
  ensureInit();
  pushAllParams();
  pushDisplay();
}

// Appended to crg.js in the frozen device. This code PREPARES patterns only.
// Native Max owns transport position, MIDI, swing and every note-off.
var N={revision:0,active:0,pending:0,records:{},running:0,signature:'',config:'',
       applying:false,cycle:0,lastPass:-1};
nativeEnabled=true;

function nativeCopy(x){ return JSON.parse(JSON.stringify(x)); }
function nativeSnapshot(){
  return nativeCopy({params:S.params,call:packPhrase(S.call),response:packPhrase(S.response),
    variants:packVariants(),cycle:N.cycle});
}
function nativeInstall(snap){
  // Routing and non-musical preferences may have changed while a pair waited.
  const retained={};
  for(const key of ['callOutput','responseOutput','newPairTiming','clipTarget','clipLength']) retained[key]=S.params[key];
  S.params=Object.assign({},snap.params,retained);
  S.call=unpackPhrase(snap.call);S.response=unpackPhrase(snap.response);
  S.variants={};
  for(const lane of Object.keys(snap.variants||{})){
    S.variants[lane]={};
    for(const key of Object.keys(snap.variants[lane])) S.variants[lane][key]=unpackPhrase(snap.variants[lane][key]);
  }
  S.syncMode=String(S.params.syncMode).toLowerCase();
  S.shownVariant=null;
}
function nativeSignature(){
  const snap=nativeSnapshot();delete snap.cycle;
  // No pattern rebuild is needed for a destination, mute, lock or clip option.
  for(const key of ['callOutput','responseOutput','newPairTiming','clipTarget','clipLength']) delete snap.params[key];
  return JSON.stringify([snap,S.followLive,S.liveRoot,S.liveIntervals]);
}
function nativePrivate(snap,fn){
  const saved=Object.assign({},S);
  try{nativeInstall(snap);S.preparing=true;return fn();}
  finally{for(const key of Object.keys(S)) if(!(key in saved)) delete S[key];Object.assign(S,saved);}
}
function nativeCompile(snap){
  return nativePrivate(snap,function(){
    const c=cfg(),plan=passPlan(c.phraseMode),passes=plan.seq?plan.seq.length:1;
    const steps=c.steps,ticks=(RATE_BEATS[S.params.rate]||0.25)*480;
    const rows=[],views=[],cycle=snap.cycle||0;
    const evolves=!!plan.vary || Number(S.params.driftCall)>0 || Number(S.params.driftResponse)>0;
    for(let pass=0;pass<passes;pass++){
      const formLane=laneForStep(plan,pass,0,steps);
      const variant=plan.form?variantFor(plan.form[pass],formLane,c):null;
      views.push(nativeCopy({call:packPhrase(S.call),response:packPhrase(S.response),
        variant:variant?packPhrase(variant):null,lane:formLane}));
      const events=[];
      for(let i=0;i<steps;i++){
        const lane=laneForStep(plan,pass,i,steps),phrase=variant||(lane===C?S.call:S.response);
        const ev=stepEvent(phrase,i,c,lane);
        if(!ev) continue;
        const sw=swingOffset(i,ticks),start=i*ticks+sw;
        const previous=events.length?events[events.length-1]:null;
        // Join only adjacent steps played by the SAME lane (including cells).
        if(previous && previous.lastStep===i-1 && previous.lane===lane &&
           phrase[i-1].tie && previous.pitch===ev.pitch && previous.chan===ev.chan){
          previous.duration=Math.max(1,start+ev.dur*ticks-previous.start);
          previous.lastStep=i;
          continue;
        }
        events.push({slot:pass*steps+i,lane,pitch:ev.pitch,vel:ev.vel,chan:ev.chan,
          duration:Math.max(1,ev.dur*ticks),swing:sw,start,lastStep:i});
      }
      for(const e of events) rows.push([e.slot,e.lane===C?0:1,e.pitch,e.vel,e.chan+1,e.duration,e.swing]);
      if(evolves){S.passIdx=cycle*passes+pass;endOfPass(plan,c);}
    }
    const next=nativeSnapshot();next.cycle=cycle+1;
    if(plan.form) snap.variants=packVariants(); // fixed letter variants persist
    return {snap,rows,views,next,evolves,steps,total:passes*steps,ticks,
      sync:['restart','timeline','free'].indexOf(String(S.params.syncMode).toLowerCase())};
  });
}
function nativePublish(record,when,kind){
  // A revision is immutable once ready is sent. Publish data first, pointer last.
  const rev=++N.revision;record.rev=rev;record.kind=kind||'';record.when=when;
  N.records[rev]=record;N.pending=rev;
  for(const row of record.rows) outlet(6,['data',rev*256+row[0]].concat(row.slice(1)));
  outlet(6,'ready',rev,record.total,record.steps,record.ticks,Math.max(0,record.sync),when);
  return rev;
}
function nativeConfigure(){
  if(S.preparing) return;
  const a=routeForLane(C),b=routeForLane(R);
  const signature=JSON.stringify([a,b,S.mute.call,S.mute.response]);
  if(signature===N.config) return;
  // Release on the OLD destination before changing either forward or gate.
  outlet(6,'flush');
  outlet(6,'buses',typeof a==='string'?a:'sidebrain_crg_unbound_call',typeof b==='string'?b:'sidebrain_crg_unbound_response');
  outlet(6,'outputs',a===-1?0:a===0?1:2,b===-1?0:b===0?1:2);
  outlet(6,'mutes',S.mute.call?1:0,S.mute.response?1:0);
  N.config=signature;
}
function nativeRefresh(){
  if(S.preparing || N.applying || !initDone || !S.call.length || !S.response.length) return;
  nativeConfigure();
  const signature=nativeSignature();
  if(signature===N.signature) return;
  nativeCancel();N.signature=signature;
  nativePublish(nativeCompile(nativeSnapshot()),0,'edit');
}
function nativeCancel(){
  outlet(6,'cancel');N.pending=0;queueAction('');
}
function nativeRequest(kind){
  if((kind==='pair' && S.queuedPair) || (kind==='dice' && S.queuedAction==='dice')){
    nativeCancel();status('Queued change cancelled');nativePrepareSuccessor();return;
  }
  nativeCancel();
  if(S.finished) rearm();
  const when=S.params.newPairTiming==='Next Phrase' && S.deviceActive &&
    (N.running || liveGet('is_playing',0)) ? 1:0;
  const snap=nativePrivate(nativeSnapshot(),function(){
    if(kind==='dice') diceMenus().forEach(randEnum);
    generate('both',true);return nativeSnapshot();
  });
  if(when) queueAction(kind);
  nativePublish(nativeCompile(snap),when,kind);
  status(when?'Queued for the next full phrase. Press again to cancel.':'New pair prepared');
}
function nativePrepareSuccessor(){
  const current=N.records[N.active];
  if(!current || !current.evolves || N.pending || S.queuedPair || !S.deviceActive || !N.running) return;
  nativePublish(nativeCompile(nativeCopy(current.next)),1,'evolve');
}
function nativecommit(revision){
  revision=Number(revision);
  if(revision<=N.active) return; // ignore stale UI acknowledgements
  const record=N.records[revision];if(!record) return;
  N.active=revision;N.lastPass=-1;
  // A later user edit may already be published. Never roll its controls back.
  const superseded=N.pending>revision;
  if(N.pending===revision) N.pending=0;
  N.applying=true;
  try{
    if(!superseded){
      if(record.kind==='pair'||record.kind==='dice') pushUndo();
      nativeInstall(record.snap);N.cycle=record.snap.cycle||0;
      queueAction('');
      if(record.kind==='dice'){
        S.restoring=true;
        try{for(const name of diceMenus()) outlet(O_SETP,'setparam',name,DATA[ENUM_PARAMS[name]].indexOf(S.params[name]));}
        finally{S.restoring=false;}
      }
      N.signature=nativeSignature();sendMenuValues();pushDisplay();
    }
    // Retire only revisions older than the acknowledged native pointer.
    for(const key of Object.keys(N.records)) if(Number(key)<revision){
      for(const row of N.records[key].rows) outlet(6,'drop',Number(key)*256+row[0]);
      delete N.records[key];
    }
  } finally{N.applying=false;}
  // Wait for the first native playback pulse before staging an automatic
  // successor, otherwise transport start's slot 0 could skip this whole bank.
}
function nativepulse(slot,revision){
  if(Number(revision)!==N.active || N.pending && N.records[N.pending].kind==='edit') return;
  const record=N.records[N.active];if(!record) return;
  const pass=Math.floor(Number(slot)/record.steps),step=Number(slot)%record.steps;
  S.stepIdx=step;S.passIdx=pass;S.playing=true;S.finished=false;
  const lane=laneForStep(passPlan(record.snap.params.phraseMode),pass,step,record.steps);
  outlet(O_UI,'playhead',step,lane===C?0:1);
  if(pass!==N.lastPass){
    N.lastPass=pass;const view=record.views[pass];if(!view) return;
    N.applying=true;
    try{
      S.call=unpackPhrase(view.call);S.response=unpackPhrase(view.response);
      S.shownVariant=view.variant?{[view.lane]:unpackPhrase(view.variant)}:null;
      N.signature=nativeSignature();pushDisplay();
    } finally{N.applying=false;}
  }
  nativePrepareSuccessor();
}
function nativetransport(on){
  N.running=on?1:0;
  if(!on){S.stepIdx=0;S.passIdx=0;outlet(O_UI,'playhead',-1,0);}
  outlet(O_UI,'transport',N.running);
}

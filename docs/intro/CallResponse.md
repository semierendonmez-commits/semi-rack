CALL & RESPONSE writes a short call and an answering response and plays them as two mono voices. It is the Rack port of the Call & Response plugin, which runs the JavaScript engine of Side Brain's C&R Generator Max for Live device (sidebrain.net), itself a port of Nordl3's Call-Response-Generator (MIT); here that JavaScript runs as a C++ port. Phrase modes include call then response, interleaved cells, letter forms like A / B / A / C, and mutating lanes with DRIFT. The phrase screen draws the call roll (cyan) over the response roll (magenta) with a pass strip and playhead; a second screen names style, motif, answer and mode.

**Patch it**
- Patch a 1/16 clock into CLK, the call's V/OCT and GATE to one mono synth and the response's to another. Pick STYLE, MOTIF, ANSWER and MODE, then press NEW for a new pair.
- RST: the next clock is step 1. The phrase RATE (1/16 by default) and swing are in the menu.
- A tie or slide keeps the gate high while only V/OCT moves, so a mono synth with glide slides. VEL carries 0-10 V velocity.
- FILL is the run mode: as a gate it plays while high, as a trigger a rising edge plays it for TRIG LENGTH. The NEW input makes a new pair, V/OCT in transposes both lanes in semitones.

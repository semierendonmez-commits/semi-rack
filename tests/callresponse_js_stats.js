// Statistics of the ORIGINAL JavaScript engine (engines/callresponse/js, run under node) for the
// configurations tests/callresponse_engine.cpp checks the C++ port against.
//   node tests/callresponse_js_stats.js > tests/callresponse_js_stats.txt
const fs = require('fs'), vm = require('vm'), path = require('path');
const dir = path.join(__dirname, '..', 'engines', 'callresponse', 'js');
const src = ['crg_shim.js', 'crg_engine.js'].map(f => fs.readFileSync(path.join(dir, f), 'utf8')).join('\n;\n');
vm.runInThisContext(src + '\n;globalThis.__E = {S, DEFAULTS, PRESETS, generate, nativeCompile, nativeSnapshot, RATE_BEATS};');
const E = globalThis.__E;
const CONFIGS = require('./callresponse_configs.json');
const N = Number(process.argv[2] || 400);
for (const cfg of CONFIGS) {
  const ov = cfg.preset ? Object.assign({}, E.PRESETS[cfg.preset], cfg.over || {}) : (cfg.over || {});
  const st = { rows: 0, slots: 0, pitch: 0, pitch2: 0, acc: 0, dur: 0, call: 0, ties: 0, slides: 0, act: 0, steps: 0, n: 0 };
  for (let k = 0; k < N; k++) {
    E.S.params = Object.assign({}, E.DEFAULTS, ov);
    E.S.variants = null;
    E.generate('both', true);
    for (const ph of [E.S.call, E.S.response]) for (const s of ph) { st.steps++; if (s.active) { st.act++; if (s.tie) st.ties++; if (s.slide) st.slides++; } }
    const rec = E.nativeCompile(E.nativeSnapshot());
    st.slots += rec.total;
    const base = Number(E.S.params.baseVelocity) || 92;
    for (const r of rec.rows) {
      st.rows++; st.pitch += r[2]; st.pitch2 += r[2] * r[2]; if (r[3] > base) st.acc++; st.dur += r[5] / rec.ticks; if (r[1] === 0) st.call++;
    }
  }
  const m = st.pitch / Math.max(1, st.rows);
  const out = [st.rows / st.slots, m, Math.sqrt(Math.max(0, st.pitch2 / Math.max(1, st.rows) - m * m)), st.acc / Math.max(1, st.rows),
               st.dur / Math.max(1, st.rows), st.call / Math.max(1, st.rows), st.act / st.steps, st.ties / Math.max(1, st.act), st.slides / Math.max(1, st.act)];
  console.log(cfg.name + ' ' + out.map(v => v.toFixed(4)).join(' '));
}

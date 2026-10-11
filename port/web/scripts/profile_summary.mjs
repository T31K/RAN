// port/web/scripts/profile_summary.mjs <file.cpuprofile> [top] - self and total time per
// function of a V8 CPU profile (run_headless.mjs "profile" action). Test tooling only.
import fs from 'node:fs';

const prof = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const top = Number(process.argv[3] || 40);
const byId = new Map(prof.nodes.map((n) => [n.id, n]));
const parent = new Map();
for (const n of prof.nodes) for (const c of n.children || []) parent.set(c, n.id);
const name = (n) => n.callFrame.functionName || '(anonymous ' + (n.callFrame.url || '').split('/').pop() + ')';

// Sample durations from timeDeltas.
const samples = new Map();
for (let i = 0; i < prof.samples.length; ++i) {
  const dt = (prof.timeDeltas[i + 1] ?? prof.timeDeltas[i] ?? 0) / 1000;
  samples.set(prof.samples[i], (samples.get(prof.samples[i]) || 0) + dt);
}
const self = new Map(), total = new Map();
let all = 0;
for (const [id, ms] of samples) {
  all += ms;
  const n = byId.get(id);
  self.set(name(n), (self.get(name(n)) || 0) + ms);
  const seen = new Set();
  for (let at = id; at !== undefined; at = parent.get(at)) {
    const nm = name(byId.get(at));
    if (seen.has(nm)) continue;
    seen.add(nm);
    total.set(nm, (total.get(nm) || 0) + ms);
  }
}
const pct = (ms) => (100 * ms / all).toFixed(1).padStart(5) + '%';
console.log(`profile: ${(all / 1000).toFixed(1)} s sampled`);
console.log('\n== self time');
for (const [n, ms] of [...self].sort((a, b) => b[1] - a[1]).slice(0, top)) console.log(pct(ms), n);
console.log('\n== total time (inclusive)');
for (const [n, ms] of [...total].sort((a, b) => b[1] - a[1]).slice(0, top)) console.log(pct(ms), n);

// Resolves Titanfall 2 player .set files (#base inheritance, [$mp]/[$sp] conditionals; ClassMods ignored).
// Usage: node setresolve.js <playersDir> <outJson> <name> [name...]
const fs = require('fs');
const path = require('path');
const [dir, outJson, ...names] = process.argv.slice(2);

function resolve(name) {
  const file = path.join(dir, name.endsWith('.set') ? name : name + '.set');
  const src = fs.readFileSync(file, 'latin1');
  const base = (src.match(/^\s*#base\s+"([^"]+)"/m) || [])[1];
  const out = base ? JSON.parse(JSON.stringify(resolve(base))) : { global: {}, stand: {}, crouch: {} };
  const stack = [];
  const lines = src.split(/\r?\n/);
  for (let i = 0; i < lines.length; i++) {
    const s = lines[i].replace(/\/\/.*$/, '').trim();
    if (!s || s.startsWith('#')) continue;
    if (s === '{') { stack.push((lines[i - 1] || '').replace(/\/\/.*$/, '').trim().replace(/"/g, '')); continue; }
    if (s === '}') { stack.pop(); continue; }
    const m = s.match(/^"?([\w.]+)"?\s+("([^"]*)"|(\S+))(\s+\[\$(\w+)\])?/);
    if (!m) continue;
    const cond = m[6];
    if (cond && cond !== 'mp') continue;
    const sect = stack[1];
    if (stack.length !== 2 || !['global', 'stand', 'crouch'].includes(sect)) continue; // skip ClassMods/PoseMods
    out[sect][m[1].toLowerCase()] = m[3] ?? m[4];
  }
  return out;
}

const res = {};
for (const n of names) res[n] = resolve(n);
fs.writeFileSync(outJson, JSON.stringify(res, null, 1));
const keys = ['health', 'healthshield', 'healthdoomed', 'healthpersegment', 'dodgespeed', 'dodgepowerdrain', 'dodgeinterval', 'powerregenrate', 'jumpheight', 'gravityscale'];
for (const [n, r] of Object.entries(res)) {
  console.log(n.padEnd(28), keys.map(k => `${k}=${r.global[k] ?? '-'}`).join(' '), `walk=${r.stand.speed ?? '-'} sprint=${r.stand.sprintspeed ?? '-'} crouch=${r.crouch.speed ?? '-'}`);
}

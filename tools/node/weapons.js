// Parses Titanfall 2 weapon keyvalues (scripts/weapons/*.txt), resolves #base and MP_BASE,
// and computes combat stats (DPS, shots/time to kill, sustained DPS, ADS).
// Usage: node weapons.js <weaponsDir> <outDir>
const fs = require('fs');
const path = require('path');

const [dir, outDir] = process.argv.slice(2);

function tokenize(src) {
  const t = [];
  let i = 0;
  while (i < src.length) {
    const c = src[i];
    if (/\s/.test(c)) { i++; continue; }
    if (c === '/' && src[i + 1] === '/') { while (i < src.length && src[i] !== '\n') i++; continue; }
    if (c === '{' || c === '}') { t.push(c); i++; continue; }
    if (c === '"') {
      let j = i + 1;
      while (j < src.length && src[j] !== '"') j++;
      t.push({ s: src.slice(i + 1, j) });
      i = j + 1;
      continue;
    }
    let j = i;
    while (j < src.length && !/[\s{}"]/.test(src[j])) j++;
    t.push({ s: src.slice(i, j) });
    i = j;
  }
  return t;
}

// Returns an ordered list of [key, value] where value is string or nested list.
function parse(tokens) {
  let p = 0;
  function block() {
    const out = [];
    while (p < tokens.length && tokens[p] !== '}') {
      const k = tokens[p++];
      if (k === '{') { out.push(['', block()]); p++; continue; }
      const v = tokens[p];
      if (v === '{') { p++; out.push([k.s, block()]); p++; }
      else if (v && v.s !== undefined) { p++; out.push([k.s, v.s]); }
      else out.push([k.s, '']);
    }
    return out;
  }
  return block();
}

function load(file) {
  const src = fs.readFileSync(file, 'latin1');
  const bases = [...src.matchAll(/^\s*#base\s+"([^"]+)"/gm)].map(m => m[1]);
  const kv = parse(tokenize(src.replace(/^\s*#base.*$/gm, '')));
  const wd = (kv.find(([k]) => k === 'WeaponData') || [null, []])[1];
  let flat = {};
  let mods = {};
  for (const b of bases) {
    const bf = load(path.join(path.dirname(file), b));
    flat = { ...flat, ...bf.flat };
    mods = { ...mods, ...bf.mods };
  }
  const mp = {};
  for (const [k, v] of wd) {
    if (typeof v === 'string') flat[k.toLowerCase()] = v;
    else if (k === 'MP_BASE') for (const [k2, v2] of v) if (typeof v2 === 'string') mp[k2.toLowerCase()] = v2;
    else if (k === 'Mods') for (const [mk, mv] of v) {
      mods[mk] = {};
      for (const [k2, v2] of mv) if (typeof v2 === 'string') mods[mk][k2.toLowerCase()] = v2;
    }
  }
  for (const [k, v] of Object.entries(mp)) if (v !== '<KEEP_DEFAULT>') flat[k] = v;
  return { flat, mods };
}

const num = (w, k, d = null) => {
  const v = w[k];
  if (v === undefined || v === '' || v === '<KEEP_DEFAULT>') return d;
  const n = parseFloat(v);
  return Number.isFinite(n) ? n : d;
};

const PILOT_HP = 100;

// Pilot-usable weapons, keyed by file, with in-game names. Category drives the CSV split.
const PILOT = {
  mp_weapon_rspn101: ['R-201 Carbine', 'AR'], mp_weapon_rspn101_og: ['R-101 Carbine', 'AR'], mp_weapon_hemlok: ['Hemlok BF-R', 'AR'],
  mp_weapon_g2: ['G2A5', 'AR'], mp_weapon_vinson: ['V-47 Flatline', 'AR'],
  mp_weapon_car: ['CAR', 'SMG'], mp_weapon_alternator_smg: ['Alternator', 'SMG'], mp_weapon_hemlok_smg: ['Volt', 'SMG'], mp_weapon_r97: ['R-97', 'SMG'],
  mp_weapon_lmg: ['Spitfire', 'LMG'], mp_weapon_lstar: ['L-STAR', 'LMG'], mp_weapon_esaw: ['X-55 Devotion', 'LMG'],
  mp_weapon_sniper: ['Kraber-AP Sniper', 'Sniper'], mp_weapon_doubletake: ['D-2 Double Take', 'Sniper'], mp_weapon_dmr: ['Longbow-DMR', 'Sniper'],
  mp_weapon_shotgun: ['EVA-8 Auto', 'Shotgun'], mp_weapon_mastiff: ['Mastiff', 'Shotgun'], mp_weapon_shotgun_doublebarrel: ['Double-barrel (SP only)', 'Shotgun'],
  mp_weapon_softball: ['Softball', 'Grenadier'], mp_weapon_epg: ['EPG-1', 'Grenadier'], mp_weapon_smr: ['Sidewinder SMR', 'Grenadier'],
  mp_weapon_pulse_lmg: ['Cold War', 'Grenadier'], mp_weapon_mgl: ['MGL Mag Launcher', 'Anti-Titan'],
  mp_weapon_autopistol: ['RE-45 Auto', 'Secondary'], mp_weapon_semipistol: ['P2016', 'Secondary'], mp_weapon_wingman: ['Wingman B3', 'Secondary'],
  mp_weapon_shotgun_pistol: ['Mozambique', 'Secondary'], mp_weapon_wingman_n: ['Wingman Elite', 'Secondary'], mp_weapon_smart_pistol: ['Smart Pistol', 'Secondary'],
  mp_weapon_defender: ['Charge Rifle', 'Anti-Titan'], mp_weapon_rocket_launcher: ['Archer', 'Anti-Titan'], mp_weapon_arc_launcher: ['Thunderbolt', 'Anti-Titan'],
  melee_pilot_emptyhanded: ['Pilot melee', 'Melee'], melee_pilot_sword: ['Pilot sword', 'Melee'],
};
const ORDNANCE = {
  mp_weapon_frag_grenade: 'Frag Grenade', mp_weapon_grenade_emp: 'Arc Grenade', mp_weapon_thermite_grenade: 'Firestar',
  mp_weapon_grenade_gravity: 'Gravity Star', mp_weapon_satchel: 'Satchel', mp_weapon_grenade_electric_smoke: 'Electric Smoke',
  mp_weapon_grenade_sonar: 'Pulse Blade', mp_ability_cloak: 'Cloak', mp_ability_grapple: 'Grapple', mp_ability_heal: 'Stim',
  mp_ability_holopilot: 'Holo Pilot', mp_ability_shifter: 'Phase Shift', mp_weapon_deployable_cover: 'A-Wall',
  mp_weapon_hard_cover: 'Hard cover', mp_weapon_frag_drone: 'Tick', mp_ability_arc_blast: 'Arc blast',
};
// Pellets per trigger pull from the weapon scripts (vscripts/weapons/*.nut).
// EVA-8 calls ShotgunBlast: ONE distance-scaled hit per entity in the cone, not per pellet.
const PELLETS = { mp_weapon_mastiff: 8, mp_weapon_shotgun_doublebarrel: 8, mp_weapon_shotgun_pistol: 3 };

const rows = [];
const ord = [];
const titan = [];
const all = {};
for (const f of fs.readdirSync(dir).filter(f => f.endsWith('.txt')).sort()) {
  const name = f.replace('.txt', '');
  let w;
  try { w = load(path.join(dir, f)); } catch (e) { console.error('parse fail', f, e.message); continue; }
  all[name] = w;
  const x = w.flat;

  if (ORDNANCE[name]) {
    const clip = num(x, 'ammo_clip_size'), per = num(x, 'ammo_per_shot', 1), regen = num(x, 'regen_ammo_refill_rate');
    const r = v => (v == null ? '' : Math.round(v * 1000) / 1000);
    ord.push({
      file: name, name: ORDNANCE[name],
      impact_dmg: r(num(x, 'damage_near_value')), explosion_dmg: r(num(x, 'explosion_damage')), explosion_dmg_titan: r(num(x, 'explosion_damage_heavy_armor')),
      inner_radius: r(num(x, 'explosion_inner_radius')), radius: r(num(x, 'explosionradius')),
      fuse_s: r(num(x, 'grenade_fuse_time')), charges: r(clip && per ? clip / per : null),
      cooldown_s: r(clip && regen ? per / regen : null), regen_delay_s: r(num(x, 'regen_ammo_refill_start_delay')),
      fire_duration_s: r(num(x, 'fire_duration')), projectile_speed: r(num(x, 'projectile_launch_speed')),
      mods: Object.keys(w.mods).join(' '),
    });
    continue;
  }
  if (/^mp_titanweapon_|^mp_titancore_|^mp_titanability_|^melee_titan/.test(name)) {
    const r = v => (v == null ? '' : Math.round(v * 1000) / 1000);
    const rate = num(x, 'fire_rate'), burst = num(x, 'burst_fire_count', 0), bd = num(x, 'burst_fire_delay', 0);
    const sps = rate && burst > 1 ? burst / (burst / rate + bd) : rate;
    const nT = num(x, 'damage_near_value_titanarmor'), fT = num(x, 'damage_far_value_titanarmor');
    const eT = num(x, 'explosionradius', 0) > 0 ? num(x, 'explosion_damage_heavy_armor', 0) : 0;
    titan.push({
      file: name, fire_mode: burst > 1 ? `burst ${burst}` : (x['fire_mode'] || ''), rpm_effective: r(sps && sps * 60),
      dmg_near_vs_titan: r(nT), dmg_far_vs_titan: r(fT), explosion_vs_titan: r(eT || null),
      dmg_near_vs_pilot: r(num(x, 'damage_near_value')), explosion_vs_pilot: r(num(x, 'explosion_damage')), explosion_radius: r(num(x, 'explosionradius')),
      near_dist: r(num(x, 'damage_near_distance')), far_dist: r(num(x, 'damage_far_distance')), crit_scale: r(num(x, 'critical_hit_damage_scale')),
      dps_vs_titan_near: r(nT != null && sps ? (nT + eT) * sps : null),
      clip: r(num(x, 'ammo_clip_size')), reload_s: r(num(x, 'reload_time')), reload_empty_s: r(num(x, 'reloadempty_time')),
      charge_time: r(num(x, 'charge_time')), fire_duration_s: r(num(x, 'fire_duration')),
      cooldown_s: r(num(x, 'ammo_clip_size') && num(x, 'regen_ammo_refill_rate') ? num(x, 'ammo_per_shot', 1) / num(x, 'regen_ammo_refill_rate') : null),
      ads_in_s: r(num(x, 'zoom_time_in')), projectile_speed: r(num(x, 'projectile_launch_speed')),
    });
    continue;
  }
  if (!PILOT[name]) continue;

  const near = num(x, 'damage_near_value'), far = num(x, 'damage_far_value'), vfar = num(x, 'damage_very_far_value');
  const pellets = PELLETS[name] ?? num(x, 'projectiles_per_shot', 1);
  const rate = num(x, 'fire_rate');
  const burst = num(x, 'burst_fire_count', 0);
  const burstDelay = num(x, 'burst_fire_delay', 0);
  const semi = /semi/.test(x['fire_mode'] || '');
  const clip = num(x, 'ammo_clip_size');
  const perShot = num(x, 'ammo_per_shot', 1);
  const hs = num(x, 'damage_headshot_scale', 1);
  const reload = num(x, 'reload_time'), reloadEmpty = num(x, 'reloadempty_time');
  const chargeTime = num(x, 'charge_time');

  // effective shots per second (bursts: count shots over count/rate + delay)
  let sps = rate;
  if (rate && burst > 1) sps = burst / (burst / rate + burstDelay);
  const perPellet = pellets || 1;
  // explosion_damage only matters with a non-zero radius (L-STAR ships 30 dmg with radius 0)
  const expl = num(x, 'explosionradius', 0) > 0 ? num(x, 'explosion_damage', 0) : 0;
  // direct-hit damage per trigger pull = impact × pellets + splash (splash assumed to land on a direct hit)
  const stk = d => (d != null && d + expl > 0 ? Math.ceil(PILOT_HP / (d * perPellet + expl)) : null);
  const ttk = n => (n && sps ? (n - 1) / sps : null);
  const r = v => (v == null ? '' : Math.round(v * 1000) / 1000);
  const stkNear = stk(near), stkFar = stk(far), stkHead = stk(near && near * hs);
  const magTime = clip && sps ? (clip / perShot) / sps : null;

  rows.push({
    weapon: PILOT[name][0], category: PILOT[name][1], file: name,
    fire_mode: burst > 1 ? `burst ${burst}` : semi ? 'semi' : (x['fire_mode'] || 'auto'),
    fire_rate: r(rate),
    rpm_effective: r(sps && sps * 60),
    pellets: name === 'mp_weapon_shotgun' ? 'blast (1 hit)' : pellets,
    dmg_near: r(near), dmg_far: r(far), dmg_very_far: r(vfar),
    near_dist: r(num(x, 'damage_near_distance')), far_dist: r(num(x, 'damage_far_distance')), very_far_dist: r(num(x, 'damage_very_far_distance')),
    headshot_scale: r(hs), headshot_dist: r(num(x, 'headshot_distance')),
    titanarmor_near: r(num(x, 'damage_near_value_titanarmor')),
    explosion_dmg: r(num(x, 'explosion_damage')), explosion_radius: r(num(x, 'explosionradius')),
    dps_near: r(near != null && sps ? (near * perPellet + expl) * sps : null), dps_far: r(far != null && sps ? (far * perPellet + expl) * sps : null),
    stk_near: stkNear ?? '', stk_far: stkFar ?? '', stk_head_near: stkHead ?? '',
    ttk_near_s: r(ttk(stkNear)), ttk_far_s: r(ttk(stkFar)), ttk_head_s: r(ttk(stkHead)),
    clip: r(clip), mag_dump_s: r(magTime),
    reload_s: r(reload), reload_empty_s: r(reloadEmpty),
    sustained_dps_near: r(near != null && magTime && reloadEmpty && clip > 0 ? ((near * perPellet + expl) * clip / perShot) / (magTime + reloadEmpty) : null),
    ads_in_s: r(num(x, 'zoom_time_in')), ads_out_s: r(num(x, 'zoom_time_out')), ads_fov: r(num(x, 'zoom_fov')),
    ads_move_scale: r(num(x, 'ads_move_speed_scale')), move_speed_scale: r(num(x, 'move_speed_modifier')),
    deploy_s: r(num(x, 'deploy_time')), holster_s: r(num(x, 'holster_time')), raise_s: r(num(x, 'raise_time')), lower_s: r(num(x, 'lower_time')),
    spread_hip: r(num(x, 'spread_stand_hip')), spread_hip_max_kick: r(num(x, 'spread_max_kick_stand_hip')), spread_ads: r(num(x, 'spread_stand_ads')),
    projectile_speed: r(num(x, 'projectile_launch_speed')), charge_time: r(chargeTime),
    mods: Object.keys(w.mods).join(' '),
  });
}

fs.mkdirSync(outDir, { recursive: true });
fs.writeFileSync(path.join(outDir, 'weapons_resolved.json'), JSON.stringify(all, null, 1));
const cols = Object.keys(rows[0]);
const csv = [cols.join(',')].concat(rows.map(o => cols.map(c => `"${String(o[c]).replace(/"/g, '""')}"`).join(','))).join('\n');
fs.writeFileSync(path.join(outDir, 'pilot_weapons.csv'), csv);
const ocols = Object.keys(ord[0]);
fs.writeFileSync(path.join(outDir, 'ordnance_abilities.csv'),
  [ocols.join(',')].concat(ord.map(o => ocols.map(c => `"${String(o[c]).replace(/"/g, '""')}"`).join(','))).join('\n'));
const tcols = Object.keys(titan[0]);
fs.writeFileSync(path.join(outDir, 'titan_weapons.csv'),
  [tcols.join(',')].concat(titan.map(o => tcols.map(c => `"${String(o[c]).replace(/"/g, '""')}"`).join(','))).join('\n'));
console.log(`weapons parsed: ${Object.keys(all).length}, pilot rows: ${rows.length}, ordnance rows: ${ord.length}, titan rows: ${titan.length}`);

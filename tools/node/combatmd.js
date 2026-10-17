// Builds notes/TITANFALL2_COMBAT.md from combat/*.csv and data/titans_resolved.json.
// Usage: node combatmd.js <combatDir> <outMd>
const fs = require('fs');
const path = require('path');
const [cdir, outMd] = process.argv.slice(2);

function csv(file) {
  const [head, ...lines] = fs.readFileSync(path.join(cdir, file), 'utf8').trim().split('\n');
  const split = l => l.match(/("([^"]|"")*")/g).map(s => s.slice(1, -1).replace(/""/g, '"'));
  const cols = split(head.replace(/([^,]+)/g, '"$1"'));
  return lines.map(l => Object.fromEntries(split(l).map((v, i) => [cols[i], v])));
}
const table = (rows, cols) =>
  [`| ${cols.map(c => c[0]).join(' | ')} |`, `|${cols.map(() => '---').join('|')}|`]
    .concat(rows.map(r => `| ${cols.map(c => (typeof c[1] === 'function' ? c[1](r) : r[c[1]]) ?? '').join(' | ')} |`)).join('\n');
const n = v => (v === '' || v == null ? '' : String(Math.round(parseFloat(v) * 100) / 100));

const order = ['AR', 'SMG', 'LMG', 'Sniper', 'Shotgun', 'Grenadier', 'Secondary', 'Anti-Titan'];
const pw = csv('pilot_weapons.csv').filter(r => r.category !== 'Melee')
  .sort((a, b) => order.indexOf(a.category) - order.indexOf(b.category) || a.weapon.localeCompare(b.weapon));
const ord = csv('ordnance_abilities.csv');
const tw = csv('titan_weapons.csv');
const titans = JSON.parse(fs.readFileSync(path.join(cdir, 'titans_resolved.json'), 'utf8'));

let md = `# Titanfall 2 combat values: weapons, ADS, DPS, TTK, abilities, titans

All values come from the shipped game files. The weapon keyvalues are \`scripts/weapons/*.txt\` and the player classes are \`scripts/players/mp/*.set\`; both were extracted from \`vpk/englishclient_mp_common.bsp.pak000_dir.vpk\` and CRC-verified.
- **Multiplayer values only:** each weapon's \`MP_BASE\` block is applied over its base data; \`SP_BASE\` is ignored.
- **Generated, do not hand-edit:** this file is built by \`tools/node/combatmd.js\` from \`data/pilot_weapons.csv\`, \`data/ordnance_abilities.csv\`, \`data/titan_weapons.csv\` and \`data/titans_resolved.json\`. The CSVs have more columns (spread, deploy/holster/raise, projectile speed, mod lists).

## Confidence legend
- **[R]**: read from server.dll.
- **[D]**: read directly from shipped data files (weapon \`.txt\`, \`.set\`, \`.nut\`/\`.gnut\` scripts).
- **[C]**: computed by us from [D]/[R] values.
- **[I]**: inferred or assumed.
- **[?]**: unknown.

In the tables, column headers carry the tag:
- **[D] columns:** values copied verbatim from the files.
- **[C] columns:** derived numbers (RPM for bursts, STK, TTK, DPS, sustained DPS, cooldowns). They inherit every assumption listed below.

The full register of assumptions is in [ASSUMPTIONS.md](ASSUMPTIONS.md).

## How damage works
- **Pilot health is 100** **[D]** (\`pilot_mp.set\`).
  - \`healthShield 200\` is set in pilot_base **[D]**, but no pilot shield mechanic was found **[I]**.
- **Range falloff:**
  - Full \`damage_near_value\` up to \`damage_near_distance\` **[D]**.
  - Falls to \`damage_far_value\` at \`damage_far_distance\` **[D]**.
  - Some weapons drop again to \`damage_very_far_value\` at \`damage_very_far_distance\` **[D]**.
  - The engine computes this (\`CalcWeaponDamage\`, inside the engine, not decompiled). Linear interpolation between those points is assumed (Source convention) **[I]**.
  - The "far" columns use the far value, i.e. at or beyond the far distance **[C]**.
- **Headshots:** multiply by \`damage_headshot_scale\`, but **only within the weapon's max far distance**. Beyond that, headshots deal body damage **[R]** (\`sh_damage_utility.gnut\`).
- **EVA-8 (and other \`ShotgunBlast\` users):** each target in the cone takes **one** distance-scaled hit, not one hit per pellet **[R]** (\`_weapon_utility.nut\` ShotgunBlast/ShotgunBlastDamageEntity). Its 8 "blasts" just get consumed per entity hit.
- **Pellets per shot:**
  - **Script constants [D]:** Mastiff **8** bolts, Mozambique **3**, Double-barrel **8**. Each bolt is assumed to do the listed per-bolt damage **[I]**.
  - **D-2 Double Take:** **3** (\`projectiles_per_shot\`, **[D]**).
- **Splash:** \`explosion_damage\` is counted only when \`explosionradius > 0\` **[I]** (the rule is ours). For example, the L-STAR ships \`explosion_damage 30\` with radius 0 **[D]**.
  - For explosive weapons, a direct hit is assumed to land impact + full splash **[I]**. Real splash falls off with distance from the inner radius.
- **Titan damage:** uses the \`*_titanarmor\` / \`explosion_damage_heavy_armor\` fields **[D]**. Critical hits multiply by \`critical_hit_damage_scale\` **[D]** (crit hit areas not analyzed **[?]**).
- **Not modeled [?]:** projectile travel time and gravity, ADS time before the first shot, recoil and spread misses, damage to titans from pilot bullets beyond the titan-armor value, and Amped/attachment mods (listed but not applied).

**How the derived columns are computed (all [C])**
- **RPM**: \`fire_rate × 60\`.
  - **Burst weapons:** \`count / (count/fire_rate + burst_fire_delay)\` × 60.
  - **Semi-auto weapons:** the cap at perfect clicking.
- **STK** (shots to kill): \`ceil(100 / (damage × pellets + splash))\`.
- **TTK**: \`(STK − 1) / shots-per-second\`. It is the time from first to last shot; it ignores travel time, ADS and misses.
- **Sustained DPS**: one full magazine plus an empty reload.

## Pilot weapons: damage, TTK, DPS
${table(pw, [
  ['Weapon', 'weapon'], ['Type', 'category'], ['Mode [D]', 'fire_mode'], ['RPM [C]', r => n(r.rpm_effective)],
  ['Pellets [D]', 'pellets'], ['Dmg near/far [D]', r => `${r.dmg_near}/${r.dmg_far}`], ['Splash [D]', r => (r.explosion_radius > 0 ? r.explosion_dmg : '')],
  ['Near→far dist [D]', r => `${r.near_dist}→${r.far_dist}`], ['HS × [D]', 'headshot_scale'],
  ['STK near/far/head [C]', r => `${r.stk_near}/${r.stk_far}/${r.stk_head_near}`],
  ['TTK near (s) [C]', r => n(r.ttk_near_s)], ['TTK far (s) [C]', r => n(r.ttk_far_s)], ['TTK head (s) [C]', r => n(r.ttk_head_s)],
  ['DPS near [C]', r => n(r.dps_near)], ['DPS far [C]', r => n(r.dps_far)], ['Sustained DPS [C]', r => n(r.sustained_dps_near)],
])}

## Pilot weapons: magazine, reload, ADS, handling
${table(pw, [
  ['Weapon', 'weapon'], ['Mag [D]', 'clip'], ['Mag dump (s) [C]', r => n(r.mag_dump_s)], ['Reload (s) [D]', 'reload_s'], ['Empty reload (s) [D]', 'reload_empty_s'],
  ['ADS in/out (s) [D]', r => `${r.ads_in_s}/${r.ads_out_s}`], ['ADS FOV [D]', 'ads_fov'], ['ADS move × [D]', 'ads_move_scale'],
  ['Hip spread / max kick (°) [D]', r => `${r.spread_hip}/${r.spread_hip_max_kick}`], ['ADS spread [D]', 'spread_ads'],
  ['Deploy (s) [D]', 'deploy_s'], ['Raise (s) [D]', 'raise_s'], ['Holster (s) [D]', 'holster_s'],
  ['Projectile speed [D]', 'projectile_speed'], ['Charge (s) [D]', 'charge_time'],
])}

- **ADS move ×:** your movement speed while aiming is max speed × this value. The max-speed ADS blend \`lerp(1, weapon field, adsFrac)\` is **[R]**; that the weapon field is \`ads_move_speed_scale\` is **[I]** (see the movement record §2). With \`ads_move_speed_scale\` 0.5 and a 173.5 walk speed, that is about 87 u/s while aiming **[C]**.
- **Mods:** each weapon's attachments and Amped (\`burn_mod_*\`) versions are listed in \`pilot_weapons.csv\` (\`mods\` column). Their operators (\`*0.7\`, \`++5\`, absolute values) are in the weapon \`.txt\` files. Those are not redistributed here; \`tools/node/weapons.js\` writes \`weapons_resolved.json\` from your own copy of the game.

## Pilot tacticals and ordnance
${table(ord, [
  ['Item', 'name'], ['Impact dmg [D]', 'impact_dmg'], ['Explosion vs pilot [D]', 'explosion_dmg'], ['Explosion vs titan [D]', 'explosion_dmg_titan'],
  ['Inner/outer radius [D]', r => `${r.inner_radius}/${r.radius}`], ['Fuse (s) [D]', 'fuse_s'], ['Charges [C]', 'charges'],
  ['Cooldown/charge (s) [C]', r => n(r.cooldown_s)], ['Duration (s) [D]', 'fire_duration_s'],
])}

- **Cooldown:** \`ammo_per_shot / regen_ammo_refill_rate\` **[C]**, assuming regen is linear and starts immediately **[I]**. Grapple runs on its own suit-power regen **[D]** (movement record §0: 3/s after 3 s, ×1.35 with Power Cell).
- **Stim:** applies \`speed_boost\` at severity **0.4** for duration + 0.5 s **[D]** (\`sh_stim.gnut\`). Max speed gets the move-speed factor \`1 + 2·0.4\` = **1.8×** **[R]** (\`SpeedScale\`, movement record §2). That gives sprint 260 → **468 u/s** and walk 173.5 → 312 **[C]**. The ×2 extra scale is also stated in a script comment ('assuming movement_speedboost_extraScale is 2.0') **[D]**.
- **Phase Shift:** lasts 2 s **[D]**. \`mp_ability_shifter.nut\` has an exit \`move_slow\` (0.75 for 0.75 s, or 1.0 for 1.5 s), but it only runs for Frontier Defense ability levels (\`GetPVEAbilityLevel >= 0\`). No exit slow was found for normal MP Phase Shift **[D]**.
- **Holo Pilot:** the decoy has 50 HP and lasts 10 s **[D]** (\`mp_ability_holopilot.nut\`).

## Titans
### Chassis (MP, ClassMods/kits not applied): all values [D] from \`scripts/players/mp/titan_*.set\`
${table(Object.entries(titans).filter(([k]) => k.startsWith('titan_')).map(([k, v]) => ({ k, ...v.global, walk: v.stand.speed, sprint: v.stand.sprintspeed, crouch: v.crouch.speed })), [
  ['Set file', r => ({ titan_atlas_stickybomb: 'Ion', titan_atlas_tracker: 'Tone', titan_atlas_vanguard: 'Monarch', titan_ogre_meteor: 'Scorch', titan_ogre_minigun: 'Legion', titan_stryder_leadwall: 'Ronin', titan_stryder_sniper: 'Northstar' }[r.k] + ` (${r.k})`)],
  ['Health [D]', 'health'], ['Shield [D]', 'healthshield'], ['Doomed [D]', 'healthdoomed'], ['Per bar [D]', 'healthpersegment'],
  ['Walk [D]', 'walk'], ['Sprint [D]', 'sprint'], ['Crouch [D]', 'crouch'],
  ['Dash speed [D]', 'dodgespeed'], ['Dash cost [D]', 'dodgepowerdrain'], ['Power regen /s [D]', 'powerregenrate'], ['Dash interval (s) [D]', 'dodgeinterval'],
])}

- **Dash count** = 100 / dash cost, assuming 100 max power **[I]**. Kit and ClassMod changes are not applied in the table:
  - \`pas_mobility_dash_capacity\`: cost 50, regen 8.33.
  - \`turbo_titan\`: regen 20.825.
  - \`pas_dash_recharge\`: regen +2.5.
  - Monarch \`core_health_upgrade\`: +5000 health.
  - \`sur_level_*\`: ±2000 health.
- **Hardcore:** \`pas_titan_hardcore_settings\` sets health to 5000 (Atlas), 6250 (Ogre) or 3750 (Stryder).

### Titan weapons, abilities and cores (vs titan armor)
${table(tw, [
  ['File', 'file'], ['Mode [D]', 'fire_mode'], ['RPM [C]', r => n(r.rpm_effective)],
  ['Dmg near/far vs titan [D]', r => `${r.dmg_near_vs_titan}/${r.dmg_far_vs_titan}`], ['Splash vs titan [D]', 'explosion_vs_titan'],
  ['Crit × [D]', 'crit_scale'], ['DPS vs titan (near) [C]', r => n(r.dps_vs_titan_near)], ['Vs pilot [D]', r => [r.dmg_near_vs_pilot, r.explosion_vs_pilot].filter(Boolean).join(' + ')],
  ['Mag [D]', 'clip'], ['Reload (s) [D]', 'reload_s'], ['Charge (s) [D]', 'charge_time'], ['Duration (s) [D]', 'fire_duration_s'], ['Cooldown (s) [C]', r => n(r.cooldown_s)],
])}

- **[I]** Offhand and core entries use \`fire_rate\` and \`ammo\` differently: they are charge-based or scripted. Treat their RPM and DPS as **not meaningful**; check the duration and cooldown columns and the weapon's \`.nut\` script.
- Rows with no titan-armor damage deal script-driven damage (for example, flame wall and arc wave ticks).
`;
fs.writeFileSync(outMd, md);
console.log('wrote', outMd, md.length);

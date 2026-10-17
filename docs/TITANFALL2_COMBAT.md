# Titanfall 2 combat values: weapons, ADS, DPS, TTK, abilities, titans

All values come from the shipped game files. The weapon keyvalues are `scripts/weapons/*.txt` and the player classes are `scripts/players/mp/*.set`; both were extracted from `vpk/englishclient_mp_common.bsp.pak000_dir.vpk` and CRC-verified.
- **Multiplayer values only:** each weapon's `MP_BASE` block is applied over its base data; `SP_BASE` is ignored.
- **Generated, do not hand-edit:** this file is built by `tools/node/combatmd.js` from `data/pilot_weapons.csv`, `data/ordnance_abilities.csv`, `data/titan_weapons.csv` and `data/titans_resolved.json`. The CSVs have more columns (spread, deploy/holster/raise, projectile speed, mod lists).

## Confidence legend
- **[R]**: read from server.dll.
- **[D]**: read directly from shipped data files (weapon `.txt`, `.set`, `.nut`/`.gnut` scripts).
- **[C]**: computed by us from [D]/[R] values.
- **[I]**: inferred or assumed.
- **[?]**: unknown.

In the tables, column headers carry the tag:
- **[D] columns:** values copied verbatim from the files.
- **[C] columns:** derived numbers (RPM for bursts, STK, TTK, DPS, sustained DPS, cooldowns). They inherit every assumption listed below.

The full register of assumptions is in [ASSUMPTIONS.md](ASSUMPTIONS.md).

## How damage works
- **Pilot health is 100** **[D]** (`pilot_mp.set`).
  - `healthShield 200` is set in pilot_base **[D]**, but no pilot shield mechanic was found **[I]**.
- **Range falloff:**
  - Full `damage_near_value` up to `damage_near_distance` **[D]**.
  - Falls to `damage_far_value` at `damage_far_distance` **[D]**.
  - Some weapons drop again to `damage_very_far_value` at `damage_very_far_distance` **[D]**.
  - The engine computes this (`CalcWeaponDamage`, inside the engine, not decompiled). Linear interpolation between those points is assumed (Source convention) **[I]**.
  - The "far" columns use the far value, i.e. at or beyond the far distance **[C]**.
- **Headshots:** multiply by `damage_headshot_scale`, but **only within the weapon's max far distance**. Beyond that, headshots deal body damage **[R]** (`sh_damage_utility.gnut`).
- **EVA-8 (and other `ShotgunBlast` users):** each target in the cone takes **one** distance-scaled hit, not one hit per pellet **[R]** (`_weapon_utility.nut` ShotgunBlast/ShotgunBlastDamageEntity). Its 8 "blasts" just get consumed per entity hit.
- **Pellets per shot:**
  - **Script constants [D]:** Mastiff **8** bolts, Mozambique **3**, Double-barrel **8**. Each bolt is assumed to do the listed per-bolt damage **[I]**.
  - **D-2 Double Take:** **3** (`projectiles_per_shot`, **[D]**).
- **Splash:** `explosion_damage` is counted only when `explosionradius > 0` **[I]** (the rule is ours). For example, the L-STAR ships `explosion_damage 30` with radius 0 **[D]**.
  - For explosive weapons, a direct hit is assumed to land impact + full splash **[I]**. Real splash falls off with distance from the inner radius.
- **Titan damage:** uses the `*_titanarmor` / `explosion_damage_heavy_armor` fields **[D]**. Critical hits multiply by `critical_hit_damage_scale` **[D]** (crit hit areas not analyzed **[?]**).
- **Not modeled [?]:** projectile travel time and gravity, ADS time before the first shot, recoil and spread misses, damage to titans from pilot bullets beyond the titan-armor value, and Amped/attachment mods (listed but not applied).

**How the derived columns are computed (all [C])**
- **RPM**: `fire_rate × 60`.
  - **Burst weapons:** `count / (count/fire_rate + burst_fire_delay)` × 60.
  - **Semi-auto weapons:** the cap at perfect clicking.
- **STK** (shots to kill): `ceil(100 / (damage × pellets + splash))`.
- **TTK**: `(STK − 1) / shots-per-second`. It is the time from first to last shot; it ignores travel time, ADS and misses.
- **Sustained DPS**: one full magazine plus an empty reload.

## Pilot weapons: damage, TTK, DPS
| Weapon | Type | Mode [D] | RPM [C] | Pellets [D] | Dmg near/far [D] | Splash [D] | Near→far dist [D] | HS × [D] | STK near/far/head [C] | TTK near (s) [C] | TTK far (s) [C] | TTK head (s) [C] | DPS near [C] | DPS far [C] | Sustained DPS [C] |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| G2A5 | AR | semi | 330 | 1 | 40/35 |  | 1500→2000 | 1.5 | 3/3/2 | 0.36 | 0.36 | 0.18 | 220 | 192.5 | 107.99 |
| Hemlok BF-R | AR | burst 3 | 405.82 | 1 | 33/25 |  | 1500→2000 | 1.5 | 4/4/3 | 0.44 | 0.44 | 0.3 | 223.2 | 169.09 | 120.95 |
| R-101 Carbine | AR | auto | 810 | 1 | 25/17 |  | 1500→2000 | 1.5 | 4/6/3 | 0.22 | 0.37 | 0.15 | 337.5 | 229.5 | 127.72 |
| R-201 Carbine | AR | auto | 810 | 1 | 25/17 |  | 1500→2000 | 1.5 | 4/6/3 | 0.22 | 0.37 | 0.15 | 337.5 | 229.5 | 127.72 |
| V-47 Flatline | AR | auto | 600 | 1 | 30/20 |  | 1500→2000 | 1.5 | 4/5/3 | 0.3 | 0.4 | 0.2 | 300 | 200 | 157.9 |
| Alternator | SMG | auto | 600 | 1 | 35/18 |  | 1000→1500 | 1.5 | 3/6/2 | 0.2 | 0.5 | 0.1 | 350 | 180 | 148.94 |
| CAR | SMG | auto | 846 | 1 | 25/13 |  | 1000→1500 | 1.5 | 4/8/3 | 0.21 | 0.5 | 0.14 | 352.5 | 183.3 | 161.03 |
| R-97 | SMG | auto | 1080 | 1 | 20/12 |  | 1000→1500 | 1.5 | 5/9/4 | 0.22 | 0.44 | 0.17 | 360 | 216 | 174.97 |
| Volt | SMG | auto | 660 | 1 | 25/15 |  | 1000→1500 | 1.5 | 4/7/3 | 0.27 | 0.55 | 0.18 | 275 | 165 | 165 |
| L-STAR | LMG | auto | 720 | 1 | 25/18 |  | 1800→2000 | 1.5 | 4/6/3 | 0.25 | 0.42 | 0.17 | 300 | 216 | 97.8 |
| Spitfire | LMG | auto | 540 | 1 | 35/25 |  | 1800→2000 | 1.5 | 3/4/2 | 0.22 | 0.33 | 0.11 | 315 | 225 | 229.15 |
| X-55 Devotion | LMG | auto | 300 | 1 | 25/20 |  | 1500→1800 | 1.5 | 4/5/3 | 0.6 | 0.8 | 0.4 | 125 | 100 | 93.99 |
| D-2 Double Take | Sniper | semi | 120 | 3 | 30/30 |  | 4500→15000 | 2 | 2/2/1 | 0.5 | 0.5 | 0 | 180 | 180 | 84.38 |
| Kraber-AP Sniper | Sniper | semi | 111 | 1 | 350/350 |  | 2000→15000 | 1.5 | 1/1/1 | 0 | 0 | 0 | 647.5 | 647.5 | 242.54 |
| Longbow-DMR | Sniper | semi | 180 | 1 | 55/55 |  | 4500→4500 | 2 | 2/2/1 | 0.33 | 0.33 | 0 | 165 | 165 | 58.3 |
| Double-barrel (SP only) | Shotgun | semi | 150 | 8 | 16/10 |  | 500→1000 | 1 | 1/2/1 | 0 | 0.4 | 0 | 320 | 200 | 213.33 |
| EVA-8 Auto | Shotgun | auto | 165 | blast (1 hit) | 200/10 |  | 100→750 | 1.25 | 1/10/1 | 0 | 3.27 | 0 | 550 | 27.5 | 231.58 |
| Mastiff | Shotgun | semi | 78 | 8 | 20/12 |  | 350→750 | 1.5 | 1/2/1 | 0 | 0.77 | 0 | 208 | 124.8 | 121.28 |
| Cold War | Grenadier | burst 4 | 288 | 1 | 30/30 | 30 | 1250→15000 | 1.25 | 2/2/2 | 0.21 | 0.21 | 0.21 | 288 | 288 | 101.41 |
| EPG-1 | Grenadier | semi | 60 | 1 | 100/100 | 90 | 2000→2500 | 1.25 | 1/1/1 | 0 | 0 | 0 | 190 | 190 | 115.85 |
| Sidewinder SMR | Grenadier | auto | 540 | 1 | 50/50 | 15 | 1800→15000 | 1.25 | 2/2/2 | 0.11 | 0.11 | 0.11 | 585 | 585 | 248.94 |
| Softball | Grenadier | semi | 120 | 1 | 10/10 | 90 | 100→1500 | 1 | 1/1/1 | 0 | 0 | 0 | 200 | 200 | 151.9 |
| Mozambique | Secondary | auto | 180 | 3 | 30/25 |  | 750→1000 | 1.5 | 2/2/1 | 0.33 | 0.33 | 0 | 270 | 225 | 131.71 |
| P2016 | Secondary | semi | 480 | 1 | 30/20 |  | 750→1000 | 2 | 4/5/2 | 0.38 | 0.5 | 0.13 | 240 | 160 | 130.91 |
| RE-45 Auto | Secondary | auto | 780 | 1 | 20/13 |  | 750→1000 | 1.5 | 5/8/4 | 0.31 | 0.54 | 0.23 | 260 | 169 | 109.34 |
| Smart Pistol | Secondary | semi | 480 | 1 | 40/40 |  | 1250→1400 | 1 | 3/3/3 | 0.25 | 0.25 | 0.25 | 320 | 320 | 160.54 |
| Wingman B3 | Secondary | semi | 156 | 1 | 40/40 |  | 1000→1500 | 2 | 3/3/2 | 0.77 | 0.77 | 0.39 | 104 | 104 | 54.45 |
| Wingman Elite | Secondary | semi | 156 | 1 | 75/55 |  | 1500→2000 | 2 | 2/2/1 | 0.39 | 0.39 | 0 | 195 | 143 | 102.09 |
| Archer | Anti-Titan | auto | 60 | 1 | 100/100 | 100 | 1200→15000 | 1 | 1/1/1 | 0 | 0 | 0 | 200 | 200 | 50 |
| Charge Rifle | Anti-Titan | semi | 60 | 1 | 300/300 |  | 4800→5300 | 1 | 1/1/1 | 0 | 0 | 0 | 300 | 300 |  |
| MGL Mag Launcher | Anti-Titan | burst 2 | 116.13 | 1 | 20/20 | 35 | 70→1500 | 1 | 2/2/2 | 0.52 | 0.52 | 0.52 | 106.45 | 106.45 | 48.18 |
| Thunderbolt | Anti-Titan | auto | 120 | 1 | 70/70 | 40 | 1200→15000 | 1 | 1/1/1 | 0 | 0 | 0 | 220 | 220 | 28.57 |

## Pilot weapons: magazine, reload, ADS, handling
| Weapon | Mag [D] | Mag dump (s) [C] | Reload (s) [D] | Empty reload (s) [D] | ADS in/out (s) [D] | ADS FOV [D] | ADS move × [D] | Hip spread / max kick (°) [D] | ADS spread [D] | Deploy (s) [D] | Raise (s) [D] | Holster (s) [D] | Projectile speed [D] | Charge (s) [D] |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| G2A5 | 14 | 2.55 | 2.2 | 2.64 | 0.25/0.2 | 35 | 0.5 | 5.5/8 | 0 | 0.8 | 0.3 | 0.5 |  |  |
| Hemlok BF-R | 24 | 3.55 | 2.52 | 3 | 0.25/0.2 | 35 | 0.5 | 5.5/8 | 0 | 1.13 | 0.3 | 0.5 |  |  |
| R-101 Carbine | 24 | 1.78 | 2.2 | 2.92 | 0.25/0.2 | 35 | 0.5 | 2/8 | 0 | 0.7 | 0.3 | 0.5 |  |  |
| R-201 Carbine | 24 | 1.78 | 2.2 | 2.92 | 0.25/0.2 | 35 | 0.5 | 2/8 | 0 | 0.7 | 0.3 | 0.5 |  |  |
| V-47 Flatline | 30 | 3 | 2.073 | 2.7 | 0.25/0.2 | 35 | 0.5 | 1/2 | 0 | 0.7 | 0.3 | 0.5 |  |  |
| Alternator | 20 | 2 | 2.3 | 2.7 | 0.2/0.15 | 45 | 0.8 | 0/0 | 0 | 0.63 | 0.3 | 0.4 |  |  |
| CAR | 30 | 2.13 | 1.8 | 2.53 | 0.2/0.15 | 45 | 0.8 | 0/0 | 0 | 0.66 | 0.3 | 0.5 |  |  |
| R-97 | 40 | 2.22 | 1.8 | 2.35 | 0.2/0.15 | 45 | 0.75 | 0/2 | 0 | 0.66 | 0.3 | 0.5 |  |  |
| Volt | 33 | 3 | 1.5 | 2 | 0.2/0.15 | 45 | 0.75 | 0/0 | 0 | 0.4 | 0.3 | 0.5 |  |  |
| L-STAR | 35 | 2.92 |  | 6.03 | 0.3/0.3 | 35 | 0.5 | 1/1 | 1 | 1 | 0.4 | 0.5 | 3500 |  |
| Spitfire | 80 | 8.89 | 2.8 | 3.33 | 0.3/0.3 | 30 | 0.35 | 1.5/7 | 0 | 0.9 | 0.3 | 0.6 |  |  |
| X-55 Devotion | 55 | 11 | 2.8 | 3.63 | 0.3/0.3 | 30 | 0.3 | 2/0 | 0.4 | 0.7 | 0.3 | 0.5 |  |  |
| D-2 Double Take | 12 | 3 | 2.4 | 3.4 | 0.4/0.3 | 26.26 | 0.5 | 8/18 | 0 | 0.6 | 0.3 | 0.45 |  |  |
| Kraber-AP Sniper | 4 | 2.16 | 2.5 | 3.61 | 0.4/0.3 | 26.26 | 0.5 | 10/ | 0 | 0.8 | 0.3 | 0.5 |  |  |
| Longbow-DMR | 6 | 2 | 2.66 | 3.66 | 0.4/0.3 | 30 | 0.5 | 8/ | 0.05 | 0.8 | 0.3 | 0.5 |  |  |
| Double-barrel (SP only) | 10 | 4 | 2 | 2 | 0.2/0.2 | 50 | 0.5 | 7/ | 7 | 0.8 | 0.3 | 0.5 |  |  |
| EVA-8 Auto | 6 | 2.18 | 2.75 | 3 | 0.25/0.2 | 50 | 0.5 | 8/ | 8 | 0.8 | 0.3 | 0.5 |  |  |
| Mastiff | 4 | 3.08 | 1.03 | 2.2 | 0.25/0.2 | 50 | 0.65 | 0/ | 0 | 0.8 | 0.3 | 0.5 |  |  |
| Cold War | 12 | 2.5 | 4 | 4.6 | 0.2/0.15 | 45 | 0.75 | 0.5/8 | 0 | 0.66 | 0.3 | 0.5 | 3500 | 0.5 |
| EPG-1 | 5 | 5 | 2.9 | 3.2 | 0.2/0.15 | 45 |  | 0/0 | 0 |  |  |  | 2000 |  |
| Sidewinder SMR | 20 | 2.22 | 3 | 3 | 0.2/0.15 | 45 |  | 0.5/3 | 0 | 0.8 | 0.3 | 0.5 | 3300 |  |
| Softball | 6 | 3 | 0.95 | 0.95 | 0.2/0.15 | 45 |  | 0.25/2 | 0.35 | 0.63 | 0.3 | 0.5 | 2500 |  |
| Mozambique | 6 | 2 | 2.1 | 2.1 | 0.2/0.15 | 55 | 0.5 | 0/ | 0 | 0.4 | 0.2 | 0.3 |  |  |
| P2016 | 12 | 1.5 | 1.25 | 1.25 | 0.2/0.15 | 70 | 0.75 | 2.5/3.5 | 0 | 0.4 | 0.2 | 0.3 |  |  |
| RE-45 Auto | 20 | 1.54 | 1.74 | 2.12 | 0.2/0.15 | 70 | 0.75 | 3/4 | 0 | 0.4 | 0.2 | 0.3 |  |  |
| Smart Pistol | 12 | 1.5 | 1.45 | 1.49 | 0.2/0.15 | 55 | 0.75 | 2.5/3.5 | 0 | 0.4 | 0.2 | 0.5 |  |  |
| Wingman B3 | 6 | 2.31 | 2.1 | 2.1 | 0.2/0.15 | 55 | 0.75 | 1/4.5 | 0 | 0.4 | 0.2 | 0.5 |  |  |
| Wingman Elite | 6 | 2.31 | 2.1 | 2.1 | 0.2/0.15 | 35 | 0.75 | 0/0 | 0 | 0.4 | 0.2 | 0.5 |  |  |
| Archer | 1 | 1 | 3 | 3 | 0.35/0.35 | 45 | 0.5 | 3/ | 0.2 | 0.8 | 0.3 | 0.5 |  |  |
| Charge Rifle | 0 |  | 3.5 | 3.5 | 0.2/0.15 | 45 |  | 3/ | 0.35 | 0.8 | 0.3 | 0.5 |  | 1.3 |
| MGL Mag Launcher | 6 | 3.1 | 3.75 | 3.75 | 0.2/0.15 | 55 |  | 0.25/2 | 0.35 | 0.8 | 0.3 | 0.5 | 1200 |  |
| Thunderbolt | 1 | 0.5 | 3.35 | 3.35 | 0.35/0.35 | 45 | 0.5 | 3/ | 0.2 | 0.8 | 0.3 | 0.5 |  |  |

- **ADS move ×:** your movement speed while aiming is max speed × this value. The max-speed ADS blend `lerp(1, weapon field, adsFrac)` is **[R]**; that the weapon field is `ads_move_speed_scale` is **[I]** (see the movement record §2). With `ads_move_speed_scale` 0.5 and a 173.5 walk speed, that is about 87 u/s while aiming **[C]**.
- **Mods:** each weapon's attachments and Amped (`burn_mod_*`) versions are listed in `pilot_weapons.csv` (`mods` column). Their operators (`*0.7`, `++5`, absolute values) are in the weapon `.txt` files. Those are not redistributed here; `tools/node/weapons.js` writes `weapons_resolved.json` from your own copy of the game.

## Pilot tacticals and ordnance
| Item | Impact dmg [D] | Explosion vs pilot [D] | Explosion vs titan [D] | Inner/outer radius [D] | Fuse (s) [D] | Charges [C] | Cooldown/charge (s) [C] | Duration (s) [D] |
|---|---|---|---|---|---|---|---|---|
| Arc blast |  |  |  | / |  | 1 |  | 1 |
| Cloak | 0 |  | 0 | / |  | 1 | 20 | 15 |
| Grapple | 0 |  | 0 | / |  | 2 |  | 0.001 |
| Stim | 0 |  | 0 | / |  | 1 | 25 | 3 |
| Holo Pilot | 0 |  | 0 | / |  | 2 | 12.5 |  |
| Phase Shift | 0 |  | 0 | / |  | 1 | 15 | 2 |
| A-Wall | 0 | 0 | 0 | 250/450 |  | 1 | 20 | 15 |
| Tick | 0 | 0 | 0 | 0/0 | 6 | 2 |  |  |
| Frag Grenade | 10 | 200 | 800 | 5/320 | 3.25 | 2 | 15.02 |  |
| Electric Smoke | 0 | 150 | 800 | 50/210 | 0 | 1 | 18.18 |  |
| Arc Grenade | 0 | 40 | 400 | 75/350 | 0 | 1 | 12.5 |  |
| Gravity Star | 75 | 25 | 800 | 160/160 | 0 | 1 | 18.18 |  |
| Pulse Blade | 100 | 0 | 0 | 0/1250 | 0 | 1 | 25 |  |
| Hard cover | 0 | 0 | 0 | 250/450 |  | 1 | 20 | 15 |
| Satchel | 0 | 125 | 2200 | 120/250 |  | 2 | 20 |  |
| Firestar | 15 | 30 | 60 | 5/120 | 0 | 1 | 22.22 |  |

- **Cooldown:** `ammo_per_shot / regen_ammo_refill_rate` **[C]**, assuming regen is linear and starts immediately **[I]**. Grapple runs on its own suit-power regen **[D]** (movement record §0: 3/s after 3 s, ×1.35 with Power Cell).
- **Stim:** applies `speed_boost` at severity **0.4** for duration + 0.5 s **[D]** (`sh_stim.gnut`). Max speed gets the move-speed factor `1 + 2·0.4` = **1.8×** **[R]** (`SpeedScale`, movement record §2). That gives sprint 260 → **468 u/s** and walk 173.5 → 312 **[C]**. The ×2 extra scale is also stated in a script comment ('assuming movement_speedboost_extraScale is 2.0') **[D]**.
- **Phase Shift:** lasts 2 s **[D]**. `mp_ability_shifter.nut` has an exit `move_slow` (0.75 for 0.75 s, or 1.0 for 1.5 s), but it only runs for Frontier Defense ability levels (`GetPVEAbilityLevel >= 0`). No exit slow was found for normal MP Phase Shift **[D]**.
- **Holo Pilot:** the decoy has 50 HP and lasts 10 s **[D]** (`mp_ability_holopilot.nut`).

## Titans
### Chassis (MP, ClassMods/kits not applied): all values [D] from `scripts/players/mp/titan_*.set`
| Set file | Health [D] | Shield [D] | Doomed [D] | Per bar [D] | Walk [D] | Sprint [D] | Crouch [D] | Dash speed [D] | Dash cost [D] | Power regen /s [D] | Dash interval (s) [D] |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Ion (titan_atlas_stickybomb) | 10000 | 2500 | 2500 | 2500 | 250 | 380 | 150 | 700 | 100 | 16.66 | 0.3 |
| Tone (titan_atlas_tracker) | 10000 | 2500 | 2500 | 2500 | 250 | 380 | 150 | 700 | 100 | 16.66 | 0.3 |
| Monarch (titan_atlas_vanguard) | 10000 | 2500 | 2500 | 2500 | 250 | 380 | 150 | 700 | 100 | 16.66 | 0.3 |
| Scorch (titan_ogre_meteor) | 12500 | 2500 | 2500 | 2500 | 210 | 310 | 112 | 600 | 100 | 10 | 0.2 |
| Legion (titan_ogre_minigun) | 12500 | 2500 | 2500 | 2500 | 210 | 310 | 112 | 600 | 100 | 10 | 0.2 |
| Ronin (titan_stryder_leadwall) | 7500 | 2500 | 2500 | 2500 | 270 | 400 | 187 | 800 | 50 | 10 | 0.35 |
| Northstar (titan_stryder_sniper) | 7500 | 2500 | 2500 | 2500 | 270 | 400 | 187 | 800 | 50 | 10 | 0.35 |

- **Dash count** = 100 / dash cost, assuming 100 max power **[I]**. Kit and ClassMod changes are not applied in the table:
  - `pas_mobility_dash_capacity`: cost 50, regen 8.33.
  - `turbo_titan`: regen 20.825.
  - `pas_dash_recharge`: regen +2.5.
  - Monarch `core_health_upgrade`: +5000 health.
  - `sur_level_*`: ±2000 health.
- **Hardcore:** `pas_titan_hardcore_settings` sets health to 5000 (Atlas), 6250 (Ogre) or 3750 (Stryder).

### Titan weapons, abilities and cores (vs titan armor)
| File | Mode [D] | RPM [C] | Dmg near/far vs titan [D] | Splash vs titan [D] | Crit × [D] | DPS vs titan (near) [C] | Vs pilot [D] | Mag [D] | Reload (s) [D] | Charge (s) [D] | Duration (s) [D] | Cooldown (s) [C] |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| melee_titan_punch | offhand_melee |  | / |  |  |  |  |  |  |  |  |  |
| melee_titan_punch_fighter | offhand_melee |  | / |  |  |  |  |  |  |  |  |  |
| melee_titan_punch_ion | offhand_melee |  | / |  |  |  |  |  |  |  |  |  |
| melee_titan_punch_legion | offhand_melee |  | / |  |  |  |  |  |  |  |  |  |
| melee_titan_punch_northstar | offhand_melee |  | / |  |  |  |  |  |  |  |  |  |
| melee_titan_punch_scorch | offhand_melee |  | / |  |  |  |  |  |  |  |  |  |
| melee_titan_punch_tone | offhand_melee |  | / |  |  |  |  |  |  |  |  |  |
| melee_titan_punch_vanguard | offhand_melee |  | / |  |  |  |  |  |  |  |  |  |
| melee_titan_sword | offhand_melee |  | / |  |  |  |  |  |  |  |  |  |
| melee_titan_sword_aoe | semi-auto |  | / |  |  |  |  |  |  |  |  |  |
| mp_titanability_ammo_swap | offhand_instant | 60000 | / |  |  |  |  | 200 |  |  | 0.1 | 2 |
| mp_titanability_basic_block | offhand | 54 | / |  |  |  |  | -1 |  | -1 |  |  |
| mp_titanability_electric_smoke | offhand_instant | 60 | / | 250 |  |  | 900 + 100 | 1 | 1 |  |  | 0.2 |
| mp_titanability_gun_shield | offhand_instant | 60 | / |  |  |  |  | 200 |  |  | 6 | 8 |
| mp_titanability_hover | offhand_instant | 60000 | / |  |  |  |  | 200 | 0 |  | 2.75 | 10 |
| mp_titanability_laser_trip | offhand_instant | 60 | 0/0 |  |  | 0 | 0 + 0 | 200 | 0 |  |  | 10 |
| mp_titanability_nuke_eject | offhand_instant | 60 | 0/0 |  |  | 0 | 0 + 0 | 200 | 0 |  |  | 10 |
| mp_titanability_particle_wall | offhand_instant | 60000 | / |  |  |  |  | 140 |  |  |  | 14 |
| mp_titanability_phase_dash | offhand_instant | 60000 | / | 250 |  |  | 900 + 100 | 200 | 0 |  | 1 | 12.5 |
| mp_titanability_power_shot | offhand_instant | 540 | 2000/2000 | 2000 | 1.5 | 36000 | 200 + 200 | 80 |  |  |  | 8 |
| mp_titanability_rearm | offhand | 120 | / |  |  |  |  | 200 |  | 0.5 |  | 17.65 |
| mp_titanability_rocketeer_ammo_swap | offhand_instant | 1.8 | / |  |  |  |  | 200 |  |  |  |  |
| mp_titanability_slow_trap | offhand_instant | 240 | 0/0 |  |  | 0 | 0 + 0 | 90 | 0 |  |  | 15 |
| mp_titanability_smoke | offhand_instant | 60 | / | 250 |  |  | 900 + 100 | 100 | 0 |  |  | 20 |
| mp_titanability_sonar_pulse | offhand | 90 | 0/0 |  | 1.5 | 0 | 0 + 0 | 200 | 3.5 | 0.01 |  | 16.67 |
| mp_titanability_tether_trap | offhand_instant | 60000 | 0/0 |  |  | 0 | 0 + 0 | 200 |  |  | 0.1 | 20 |
| mp_titancore_amp_core | burst 100 | 1043.48 | 150/150 | 100 | 1.1 | 4347.83 | 150 + 100 | 200 | 2.6 | 0.1 |  | 0.01 |
| mp_titancore_dash_core | offhand_instant | 0 | / |  |  |  |  | 200 |  | 1 |  |  |
| mp_titancore_flame_wave | offhand |  | 4500/4500 |  |  |  | 300 | 200 |  | 0.75 |  |  |
| mp_titancore_flight_core | offhand_instant |  | / |  |  |  |  | 200 |  |  |  |  |
| mp_titancore_laser_cannon | offhand | 480 | 325/325 | 325 | 2.7 | 5200 | 325 + 325 | 200 | 3.5 | 1.1 |  |  |
| mp_titancore_salvo_core | burst 20 | 720 | 140/140 | 140 |  | 3360 | 60 + 60 | 30 | 3.5 | 0.5 |  |  |
| mp_titancore_shift_core | offhand_instant | 0 | / |  |  |  |  | 200 |  | 0.75 |  |  |
| mp_titancore_siege_mode | offhand_instant | 1800 | / |  |  |  |  | 200 |  | 0 |  |  |
| mp_titancore_upgrade | offhand_instant | 1800 | / |  |  |  |  | 200 |  | 0 |  |  |
| mp_titanweapon_arc_wave | offhand | 90 | 1000/1000 |  |  | 1500 | 75 | 100 |  | 30 |  | 10 |
| mp_titanweapon_dumbfire_rockets | offhand | 105 | / | 150 |  |  | 300 + 66 | 180 | 0 | 0.01 |  | 9 |
| mp_titanweapon_flame_wall | offhand | 75 | 0/0 |  |  | 0 | 0 + 0 | 200 |  | 0.2 |  | 10 |
| mp_titanweapon_flightcore_rockets |  | 720 | 300/300 | 300 |  | 7200 | 300 + 55 | 500 | 3.5 |  |  |  |
| mp_titanweapon_heat_shield | offhand | 300 | 200/200 |  |  | 1000 | 25 | -1 |  | 4 |  |  |
| mp_titanweapon_homing_rockets | burst 10 | 29.03 | / | 2 |  |  | 125 + 1 | 400 | 0 |  |  |  |
| mp_titanweapon_laser_lite | offhand | 90 | 1200/1200 | 1200 | 1.5 | 3600 | 300 + 300 | 120 | 3.5 | 3 |  |  |
| mp_titanweapon_leadwall | semi-auto | 150 | 100/75 |  | 1.5 | 250 | 60 | 4 | 2.25 |  |  |  |
| mp_titanweapon_meteor | semi-auto | 102 | 800/800 | 200 | 1 | 1700 | 310 + 50 | 1 | 2.4 |  |  |  |
| mp_titanweapon_orbital_strike | burst 12 | 80 | / | 200 |  |  | 250 + 100 | 1023 | 0 |  |  |  |
| mp_titanweapon_particle_accelerator | auto | 540 | 80/60 |  | 1.5 | 720 | 25 + 0 | 40 | 4 |  |  |  |
| mp_titanweapon_predator_cannon |  | 900 | 100/72 |  | 1.5 | 1500 | 35 + 0 | 100 | 4.6 |  |  |  |
| mp_titanweapon_predator_cannon_siege |  | 600 | 100/72 |  | 1.5 | 1000 | 35 + 0 | 100 | 4.6 |  |  | 0.02 |
| mp_titanweapon_rocketeer_rocketstream | auto | 90 | 350/300 | 200 | 1.25 | 825 | 50 + 50 | 20 | 3 |  |  |  |
| mp_titanweapon_salvo_rockets | burst 6 | 300 | / | 250 |  |  | 250 + 100 | 120 | 0 | 0.01 |  | 1 |
| mp_titanweapon_shoulder_rockets | offhand | 720 | 350/350 | 350 |  | 8400 | 250 + 100 | 400 | 0 | -1 |  |  |
| mp_titanweapon_sniper | semi-auto | 54 | 250/250 |  | 1.5 | 225 | 250 + 0 | 6 | 2 | 2.25 |  |  |
| mp_titanweapon_sticky_40mm | semi-auto | 105 | 330/330 | 200 | 1.5 | 927.5 | 200 + 75 | 12 | 3 |  |  |  |
| mp_titanweapon_stun_laser | offhand | 90 | 100/100 | 100 | 1.5 | 300 | 20 + 20 | 120 | 3.5 | 3 |  | 12 |
| mp_titanweapon_tracker_rockets | burst 6 | 318.99 | / | 250 |  |  | 350 + 100 | 200 | 0 | 0.01 |  | 1 |
| mp_titanweapon_vortex_shield | offhand | 120 | 140/140 |  |  | 280 | 35 + 0 | -1 |  | -1 |  |  |
| mp_titanweapon_vortex_shield_ion | offhand | 120 | 140/140 |  |  | 280 | 35 + 0 | -1 |  | -1 |  |  |
| mp_titanweapon_xo16 |  | 720 | 120/100 |  | 1.5 | 1440 | 100 + 50 | 30 | 2.6 |  |  |  |
| mp_titanweapon_xo16_shorty |  | 720 | 120/100 |  | 1.5 | 1440 | 100 + 50 | 30 | 2.6 |  |  |  |
| mp_titanweapon_xo16_vanguard |  | 720 | 120/100 |  | 1.5 | 1440 | 45 | 40 | 2.6 |  |  |  |

- **[I]** Offhand and core entries use `fire_rate` and `ammo` differently: they are charge-based or scripted. Treat their RPM and DPS as **not meaningful**; check the duration and cooldown columns and the weapon's `.nut` script.
- Rows with no titan-armor damage deal script-driven damage (for example, flame wall and arc wave ticks).

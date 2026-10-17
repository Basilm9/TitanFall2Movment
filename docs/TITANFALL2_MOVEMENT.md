# Titanfall 2 pilot movement: reverse-engineered record

This was reconstructed by static analysis of `server.dll` from the installed game (`C:\Program Files\EA Games\Titanfall2`). The tools were Ghidra 12.1.4 headless and REA. The game was never run and no files were modified; all work was on a copy.

It covers ground, sprint, crouch, jump, double jump, skip (bunny-hop), air control, slide, wallrun, wallhang, mantle, dodge, grapple, zipline, window traversal and air slow-mo.

**How to read this.** This file is the one-page summary for reimplementation. Each section links to a detailed note with per-function pseudocode, addresses and confidence tags. Every value and functional claim in Sections 0–13 carries one of these tags:
- **[R]**: read directly from server.dll (decompiled code, disassembly, or binary data such as ConVar registrations and the player-settings table).
- **[D]**: read directly from shipped game data files: `.set` player files, weapon `.txt` keyvalues, `.nut`/`.gnut` scripts extracted from the VPK.
- **[C]**: computed from [R]/[D] values (e.g. jump launch speed sqrt(2·750·60), apex heights, TTK).
- **[I]**: inferred or assumed: naming, meaning of an unnamed field, design intent, interpolation shape, anything not directly read.
- **[?]**: unknown or not located.

An [R] tag covers what the code does. Function names (`CheckJumpButton`, `WallRunMove`, ...) and most field names are inferred [I] unless the detailed note says they come from a datamap, string or settings table. Nothing was measured in a running game. Every [I] and [?] item, with how to verify it, is collected in [ASSUMPTIONS.md](ASSUMPTIONS.md).

**Source of values.** Sections 1–12 give **code defaults** from server.dll's ConVar registrations and its player-settings table [R]. **Section 0 lists the values the game actually ships for multiplayer pilots** [D]. Those come from `scripts/players/mp/pilot_base.set` → `pilot_mp.set`, extracted from `vpk\englishclient_mp_common.bsp.pak000_dir.vpk` with every file CRC-verified. Where Section 0 gives a value, it overrides the default quoted later. ConVar values are not affected by `.set` files [R]; playlist or script overrides of ConVars were not checked [?].

**Units.** Source/Respawn units: 1 unit ≈ 1 inch, speeds in u/s, gravity `sv_gravity` = **750 u/s²** [R].

| Detailed note | Covers |
|---|---|
| [ground_misc.md](ground_misc.md) | per-tick dispatcher, walk/sprint/crouch speed and accel, stairs, dodge, sprint tilt and power, air slow-mo, window traversal |
| [jump_air_friction.md](jump_air_friction.md) | jump, coyote time, keyboard grace, double jump, skip, air accel, ground friction, velocity clamps |
| [slide_mantle.md](slide_mantle.md) | slide start, boost, decel, slide-jump; auto-mantle detection, classes, timing |
| [wallrun.md](wallrun.md) | wallrun start, boost, gravity ramp, slip, friction, end, jump-off; wallhang; Denuvo trap |
| [grapple_zipline.md](grapple_zipline.md) | grapple fire, attach, pull, gravity, detach, auto-melee; zipline attach and dismount |
| [player_settings_table.md](player_settings_table.md) / [.csv](player_settings_table.csv) | all 421 per-class settings fields with offsets, defaults and help text |
| [TITANFALL2_COMBAT.md](TITANFALL2_COMBAT.md) | companion record: weapon damage, TTK, DPS, ADS, reloads, tacticals and ordnance, titan chassis and weapons |
| [ASSUMPTIONS.md](ASSUMPTIONS.md) | register of everything assumed or unknown in this record and the combat record, with verification steps |

---

## 0. Shipped multiplayer pilot values (what the game actually uses)
**Inheritance.** Every MP pilot class inherits `pilot_mp.set` (which is `#base "pilot_base.set"`) [D]. This covers pilot_light/medium/heavy/geist/grapple/stalker/nomad and their _male/_female variants, which change only footsteps and models [D]. **All tacticals move identically** [D]: none of those class files sets a movement field. Only Coliseum differs (see the end of this section) [D]. A field in neither file falls back to the code default from server.dll, marked *(default)* [R].

In the tables below, a **Shipped** value is [D] unless tagged otherwise, and a **Code default** is [R] (settings table, class-mod parser or ConVar registration) unless tagged otherwise.

**Ground and stance**
| Value | Shipped | Code default | Notes |
|---|---|---|---|
| Walk speed (`stand.speed`) | **173.5** [D] | 120 [R] | |
| Sprint speed (`stand.sprintspeed`) | **260** [D] | 0 [R] | |
| Crouch speed (`crouch.speed`) | **80** [D] | 120 [I] | 120 is the parser default for `speed`; that it applies per stance is not traced |
| Ground acceleration | 2500 *(default)* [R] | 2500 [R] | not overridden in any pilot file [D] |
| Ground deceleration | 1500 [C] *(default: 0.6 × accel [R])* | 1500 [C] | |
| Step height | **18** [D] | (required) [R] | |
| Stand hull / eye | 32×32×72 / **60** [D] | none [R] | |
| Crouch hull / eye | 32×32×47 / **32** [D] | none [R] | pilot_mp lowers eye from 38 to 32 [D] |
| Side / back speed scale | 1 *(default)* [R] | 1 [R] | moving backward or sideways isn't slower [R] |
| impactSpeed | **380** [D] | 580 [R] | horizontal speed lost in one move that triggers impact effects (`ImpactCheck`) [R]; not the fall-damage/landing check |

**Jump and air**
| Value | Shipped | Code default |
|---|---|---|
| `jumpHeight` | **60** [D] | (required) [R] |
| `gravityScale` | **0.80** [D] | 1.0 [R] |
| Jump launch speed | **300 u/s** = sqrt(2·750·60) [C] | |
| Ground-jump apex | **≈ 75 u** = 300²/(2·600) [C] (uses the [I] gravityScale link below) | |
| Double jump `superjumpMinHeight` / Max | **60 / 60** [D] | 80 / 80 [R] |
| Double jump horizontal redirect `superjumpHorzSpeed` | **180** [D] | 240 [R] |
| Double jump extra apex | **≈ 75 u** [C] (same [I] link) | |
| Air speed cap `airSpeed` | 60 *(default)* [R] | 60 [R] |
| Air accel `airAcceleration` | 500 *(default)* [R] | 500 [R] |
| Air slow-mo `airSlowmoSpeed` | **0.15** [D] | 1.0 [R] |

- The jump impulse uses raw `sv_gravity` 750 **[R]** (`fn_180172870` line 381: `sqrt(sv_gravity·2·h)`). Flight gravity is `sv_gravity × entity gravity` **[R]**; that entity gravity equals gravityScale 0.8 is **[I]**. A shipped script restores `player.kv.gravity = player.GetPlayerSettingsField( "gravityScale" )` (`mp_titanability_hover.nut`) [D], which supports the link but does not prove it for pilots.
- The double-jump rise calculation also uses raw `sv_gravity` [R]. Its extra apex of about 75 u comes from H/0.8 [C] (same [I] link).
- Air slow-mo applies only with the `pas_ads_hover` kit (`hoverEnabled 1`) [D]: with `airslowmo_enabled` 0, only hovering drives the slow-mo blend [R].

**Slide** (only two fields overridden [D])
| Value | Shipped | Code default |
|---|---|---|
| `slideDecel` (fixed loss/s) | **50** [D] | 160 [R] |
| `slideVelocityDecay` (retained/s) | **0.7** [D] | 0.85 [R] |
| boost / boost cap / start speed / stop / max-stop / max-jump / jump height / accel / want-to-stop | 150 / 400 / 200 / 125 / 350 / 350 / 50 / 50 / 400 *(defaults)* [R] | same [R] |

**Wallrun**
| Value | Shipped | Code default |
|---|---|---|
| `wallrun_timeLimit` | **1.75 s** [D] | 8 [R] (base 4.0 [D]) |
| `wallrun_hangTimeLimit` | **4 s** [D] | 9 [R] |
| `wallrunMaxSpeedHorizontal` | **420** [D] | 450 [R] |
| `wallrunMaxSpeedVertical` | **225** [D] | 300 [R] |
| `wallrunAccelerateHorizontal` | 1500 [D] | 1500 [R] |
| `wallrunAccelerateVertical` | **360** [D] | 450 [R] |
| `wallrunJumpUpSpeed` | **230** [D] | 320 [R] |
| `wallrunJumpOutwardSpeed` | **205** [D] | 250 [R] |
| `wallrunJumpInputDirSpeed` | **80** [D] | 100 [R] |
| `wallrunFriction` | 4 *(default)* [R] | 4 [R] |
| `wallrunAdsType` | **"ADS"** [D] | "wallhang" [R] |

- `wallrunAdsType "ADS"` means aiming while wallrunning just aims [R]: only type "wallhang" enters a hang, and type "none" cancels ADS. Matching "ADS" to the option string "ads" relies on case-insensitive key and value lookup [I]. Wallhang requires the `pas_wallhang` kit [D].
- Wall-runner kit (`pas_wall_runner`): time limit +2.5 s and hang limit +21 s [D] (values `"2.5++"` / `"21++"`; reading `++` as "add" is [I]).

**Grapple** (all from pilot_base.set unless noted)
| Value | Shipped | Code default |
|---|---|---|
| detach length min / max | 33 / 50 [D] | 33 / 50 [R] |
| detach away speed | **500** [D] | 300 [R] |
| gravity fraction min / max | **0.25 / 0.7** [D] | 0.0 / 0.75 [R] |
| detach vertical boost / cap | 200 / 200 [D] | 200 / 200 [R] |
| detach speed loss | **300** [D] | 400 [R] |
| detach speed loss floor | **460** (pilot_mp; pilot_base 430) [D] | 430 [R] |
| low-speed threshold | 250 [D] | 250 [R] |
| low-speed time: air / wall / ground | 1.5 / 1.2 / 0.7 s [D] | same [R] |
| impact vertical boost / cap | 300 / 300 [D] | 300 / 300 [R] |
| `grapple_airSpeedMax` | 420 [D] | 420 [R] |
| `grapple_airAccel` | **650** [D] | 1000 [R] |
| power regen delay | **3 s** [D] | 1 [R] |
| power regen rate | **3/s** (×1.35 with Power Cell) [D] | 17 [R] |
| range `grapple_maxLength` / `grapple_maxLengthVert` (weapon `mp_ability_grapple.txt`) | 1100 / 1100 [D] | 10 / 10 with no weapon [R] |
| power per grapple / drain rate (`grapple_power_required` / `grapple_power_use_rate`, same file) | 50 / 50 per s [D] | 0 / 0 with no weapon [R] |

The weapon struct offsets that the code reads for range and power are matched to these weapon keys by key order and usage [I].

**Zipline**
| Value | Shipped | Code default |
|---|---|---|
| `ziplineSpeed` | **600** [D] | 800 [R] |
| zipline accel / jump-off speed / damp | 400 / 400 / 1 *(defaults)* [R] | same [R] |
| Zipline ride cap | **600 × rope `m_ziplineMoveSpeedScale`** [C] (scale is map data [?]) | 800 × scale [C] |

**Other shipped fields**
- Sprint view: offset **−6** [D] (default −10 [R]); start duration **0.8 s** [D] (default 0.3 [R]).
- `sprinttiltMaxRoll` **2°** [D] (default 10 [R]).
- `health` 100 [D]. pilot_base also sets `healthShield 200` [D], but its use is not established [?].
- `fov` 70 [D].
- Dodge disabled (`dodge 0`) [D].
- Mantle, slide and double jump enabled [D].

**Not shipped for MP.** These are left as ConVar defaults (Sections 3–7), which apply to everyone [R] (playlist overrides not checked [?]):
- skip: 0.1 s window, 0.75 height, keeps 450 speed, −12 per skip [R].
- coyote time 0.2 s [R].
- `wallrun_upWallBoost` 250 [R].
- slide boost cooldown 2 s [R].
- mantle values [R].
- `player_extraairaccelleration` 2 [R].

**Other pilot sets, for reference only (not MP)**
- **Campaign** (`pilot_solo`): walk 162.5, sprint 243, gravity 0.75, wallrun horizontal max 340, wallrun horizontal accel 1400, input-direction jump 75 [D].
- **Coliseum** (`pilot_coliseum_*`): walk 250, sprint 375, crouch 160, jump 150, double jump 150, gravity 0.97, air 85 / 720, slide boost 215 / cap 575, wallrun 600 / 324 with accel 2160 / 500 [D]. Its `superjumpheightmax` key matches no settings field [R], so it is presumably ignored [I].
- **`pilot_boost`**: a jetpack subclass (boost / glide / hover) [D]; that it is unused in shipped MP is [I].

---

## 1. Frame order (PlayerMove, `0x180179950`)
Each tick runs these steps, then dispatches on movetype [R]:
1. Stuck check.
2. CategorizePosition (`0x180163830`).
3. Duck.
4. Air slow-mo timestep blend.
5. Compute max speed.
6. Sprint tilt.

Walking uses FullWalkMove (`0x18016a9f0`) [R]: gravity half-step → jump check → AirMove (in air) or WalkMove (on ground: Friction then Accelerate, then step/slide-move) → categorize → gravity half-step. Wallrun, mantle/traversal, zipline and grapple are their own branches or sub-steps [R]. See ground_misc.md §1.

## 2. Ground movement
- **Max speed** = `SpeedScale × (sprinting ? sprintspeed : speed)` × ADS weapon blend [R].
  - Class-file keys per stance: `speed` default 120, `sprintspeed` default 0 [R]. The real pilot numbers live in `.set` files [D] (Section 0).
  - `SpeedScale` = weapon scale × `m_playerMoveSpeedScale` × (1 − slow) × (1 + 2·boost) [R]. That the two status-effect terms are the `move_slow` and `speed_boost` effects is [I].
  - Sprint multiplies stick input by 1.25 before clamping [R].
  - Crouch caps input magnitude at `speed[crouch]/speed[stand]` [R]; that stance index 1 is crouch is [I].
  - **`sv_maxspeed` is not used for pilots** [R] (only noclip and a shadow fallback read it). **`sv_backspeed` has no user among the analysed functions** [R; no whole-binary scan]. Back and side speeds use `speedScaleBack` and `speedScaleSide` (default 1) [R].
- **Accelerate** is an absolute rate, unlike stock Source's `accel × wishspeed` [R]:
  - `add = min(wish − cur, accel·dt·surfFriction)` [R].
  - The result never exceeds `max(oldSpeed, wishspeed)`, so the ground can't push you past your target [R].
  - Accel tiers: `acceleration` **2500**; `lowAcceleration` below `lowSpeed`; `sprintAcceleration` above run speed; `slideAccel` **50** while sliding [R]. A value of −1 means "use `acceleration`" [R].
  - Analogue input is eased by `(2−m)·m` [R].
- **Friction** (`0x1801696b0`) is not stock either [R]:
  - The velocity component along your input direction, up to wishspeed, is **not slowed at all** [R].
  - Everything else (overspeed, sideways drift, all velocity with no input) loses a **fixed amount per second** [R]. That amount is `decel × surfaceFriction × m_groundFrictionScale` [R].
  - There is **no stop-speed snap**, and `sv_friction` and `sv_stopspeed` are not read by pilot friction [R].
  - **There is no edge friction at all [R].** `sv_edgefriction` is never read anywhere in server.dll, and the HL1-style edge probe `0x18015fe70` has no caller: no call, pointer, RVA or rel32 reference (jump_air_friction.md §2).
  - Decel is `deceleration`, falling back to **0.6 × acceleration = 1500** [R]. Above run speed, `sprintDeceleration` (else `sprintAcceleration`) is used if set [R]. Sliding uses `slideDecel` **160** (or `slideWantToStopDecel` **400** when crouch is released or you push against your motion), plus a multiplicative `slideVelocityDecay` **0.85**/s [R].
- **Crouch:** going down takes 0.4 s (0.2 s from a slide); standing up takes 0.2 s [R].
- **Stairs:** per-class `stepHeight` [R]; that the step size the code uses (player+0x1610) is a copy of it is [I]. When the landing is steep, a forward re-probe uses `stepTestDist` **10**, and climbs up to about 45.6° are accepted [R].

## 3. Jumping
- **Ground jump:** `vel += surfaceJumpFactor · upDir · sqrt(2 · 750 · jumpHeight)` [R].
  - `jumpHeight` is per-class with no code default, so it's in the `.set` file [R].
  - The jump is added along `m_upDir` rather than setting vz [R].
- **Coyote time:** a jump within `jump_graceperiod` **0.2 s** of leaving the ground counts as a ground jump, if you haven't already jumped [R].
- **Keyboard grace:** a move key pressed within 0.5 s after a jump rotates horizontal velocity toward the key at constant speed [R].
  - Blend 0.7 [R].
  - Full effect until 0.2 s, fading to zero at 0.5 s [R].
  - The change is capped at 0.7 × sprint speed [R].
- **Double jump** (internally "superjump" [I, strong]):
  - Limit `superjumpLimit` **1** per airtime [R].
  - Height H = `superjumpMinHeight` **80** [R]. The interpolation input `t` is hard-coded 0 (`0x1805d30c0` is `xorps; ret`), so `superjumpMaxHeight` and the power cost are effectively unused [R].
  - If the remaining rise is below 0.75·H, vz is zeroed and the full H is applied. Otherwise an impulse for 0.25·H is added [R]. Either way you get at least H more apex, and a fall is cancelled [C].
  - Horizontal velocity is redirected toward input at `superjumpHorzSpeed` **240** [R].
  - It refreshes on landing (`skip_replenish_double_jump` 1) [R]. With `superjump_limitreset_onwallrun` 1 and the class flag `superjumpCanUseAfterWallrun` (default 1), starting a wallrun refreshes it too [R].
- **Skip (bunny-hop):**
  - Jumping within `skip_time` **0.1 s** of landing uses `skip_jump_height_fraction` **0.75** of the height [R].
  - If you are faster than `skip_speed_retain` **450**, you lose `skip_speed_reduce` **12** u/s per skip, but never drop below 450 [R].
  - A skip never adds speed [R]; its benefit is avoiding ground friction on the jump tick [I].

## 4. Air control
- **Air accel** (`0x180160f40`): the speed cap is `airSpeed` **60** (the wish cap, not a speed limit) [R]. Accel is `airAcceleration` **500** u/s², an absolute rate with no wishspeed factor [R]. AirMove reads the player netvars `m_airSpeed`/`m_airAcceleration`; that they hold the class values is [I].
- Stock Source's 30 u/s cap is gone [R]. `sv_airaccelerate` is not referenced by any analysed function [R; no whole-binary scan].
- Once you're at or above wishspeed, `player_extraairaccelleration` **2.0** only steers and adds no speed [R]. Below that, the accelerate branch has no total-speed clamp, so classic air-strafing can still add speed [R]; how much it adds with airSpeed 60 is not quantified [?]. Large speed gains come from slide boosts, wallrun and grapple [I].
- **Gravity** is 750 × entity gravity [R]; that entity gravity is the class `gravityScale` (code default **1.0**, shipped 0.8) is [I].
- **Velocity clamp:** the whole vector is scaled so no axis exceeds `sv_maxvelocity` 12100 [R].

## 5. Slide
- **Start:**
  - Ground normal z ≥ 0.7 (unless `slide_whileInAir`, default 0) [R].
  - Horizontal speed ≥ `slideRequiredStartSpeed` **200** [R].
  - On the ground, `dot(viewFwd, wishDir)` ≥ `slide_max_angle_dot` **0.6** [R].
- **Boost:** applies if more than `slide_boost_cooldown` **2 s** has passed since the last slide [R]. It sets `speed = min(speed + slideSpeedBoost 150, slideSpeedBoostCap 400)`, horizontally, and never reduces speed [R]. Every slide resets the cooldown timer, boosted or not [R].
- **During the slide:**
  - Input along the current velocity is removed, so you can steer but not accelerate (apart from `slideAccel` 50) [R].
  - Slopes add `g·n.xy·n.z` downhill [R].
  - Stepping up costs `stepHeight × slide_step_velocity_reduction 10` u/s [R].
- **End:** the slide ends below `slideStopSpeed` **125** [R], but can't end above `slideMaxStopSpeed` **350**, because Duck forces the crouch above that speed [R] (ground_misc.md §2.6).
- **Slide-jump:** height `slideJumpHeight` **50** [R]. Within 0.4 s of the slide start, and only while `m_duckState` ≠ 2, the boost is stripped [R]; since 2 likely means fully crouched and the slide crouch takes 0.2 s, the effective window may be shorter than 0.4 s [I]. Speeds between 85% and 100% of `slideMaxJumpSpeed` **350** are set to exactly 350, once per boosted slide [R].

## 6. Wallrun
- **Start:**
  - Wall normal z between −0.35 and 0.7 [R].
  - You must be moving into the wall (`wallrun_minAngle_air` 180) [R].
  - Two probes must find the wall within `wallrun_allowed_wall_dist` **13** [R].
  - A 0.5 s timer gates starts and a jump clears it [R]. That the timer is "time airborne after walking off a ledge" is [I]: the field it reads (player+0x2588) is named `m_onSlopeTime` in the datamap.
- **Boost:** upward speed along the wall is raised to at least `wallrun_upWallBoost` **250**, or 250 is added if you were falling [R].
  - Gravity ramps 0 → full over `wallrun_gravityRampUpTime` **1 s** [R].
  - Both only happen on a *strong* run [R]. A run is *weak* after landing, after a previous wallrun ends, or when starting on a remembered same wall; a ground or wall jump clears the weak flag [R].
- **Same wall:** normals with dot ≥ `wallrun_sameWallDot` **0.9** count as the same wall [R]. It can only be re-run from below the previous start height, and that run is weak [R].
- **Per tick:**
  - Friction `wallrunFriction` **4**, as `speed·(1 − dt·k)` [R].
  - Accel: `wallrunAccelerateHorizontal` **1500** and `wallrunAccelerateVertical` **450** [R].
  - Speed caps: `wallrunMaxSpeedHorizontal` **450**, `wallrunMaxSpeedVertical` **300**, `wallrunMaxSpeedHorizontalBackward` **50** [R].
  - Forward input pushes upward via `wallrun_upwardAutoPush` **0.65** [R].
- **Slip:** starts at `wallrun_slipstarttime` **1.5 s** and ramps over `wallrun_slipduration` **1 s**; with no input it is at least **0.7** [R]. Slip reduces vertical accel and stops friction from holding you up [R]. The time limit is `wallrun_timeLimit` **8 s** [R].
- **End:** crouch; pushing away for more than 0.05 s; the time limit; losing the wall (checked every 4th tick); or reaching a floor [R]. You are pushed off at `wallrun_fallAwaySpeed` **70** [R]. Near the top of a wall, upward speed bleeds at 3·3000/s [R].
- **Jump-off:**
  - Up `wallrunJumpUpSpeed` **320**: `vz = min(vz + 1.5·320, 320)` [R].
  - Input direction `wallrunJumpInputDirSpeed` **100** [R].
  - Outward `wallrunJumpOutwardSpeed` **250**, cut by up to 80% when you look and push into the wall [R].
- **Wallhang:** needs `wallrunAdsType` = "wallhang" and ADS held [R].
  - Speed and gravity fade out over `wallrun_hangStopTime` 0.5 s [R].
  - Slip starts at 3 s and is complete by 4 s; the hang limit is `wallrun_hangTimeLimit` 9 s [R].
  - The wall must stay within 18 units [R].
- **View tilt:** `wallrun_maxViewTilt` 15° is not read by any analysed server function [R]; the roll is presumably computed client-side [I]. The server only predicts the impact normal [R].

## 7. Auto-mantle
- **Gating:** on the ground it needs jump held (or a wallrun in progress) [R]. Cooldown `automantle_cooldown` **0.25 s** [R].
- **Detection:**
  - A hull sweep finds the wall within `automantle_searchdist` − 0.5 = 4.5 u, or one tick of forward travel if that is longer [R].
  - A ledge probe goes `automantle_forwarddist` **30** past the face, with at least `automantle_mindist` **18** of penetration [R].
  - Probe heights cycle through {80, 66.5, 34.5, 26.5, 16.5}; the maximum is `automantle_height` **80** [R].
- **Angles:** push angle ≤ 50°; view angle ≤ 50° (45° while wallrunning) [R].
- **Classes**, by eye height relative to the ledge [R]:

  | Class | Eye height vs ledge | Duration |
  |---|---|---|
  | BELOW | below −10 | **1.11 s** [R] |
  | LEVEL | −10 to 10 | **1.0 s** [R] |
  | ABOVE | 10 to 30 | **0.5 s** [R] |
  | HIGH | 30 and up | **0.35 s** [R] |

- **Motion:** the player hangs at `rest_frac` 0.4 (0.3 for BELOW) [R]. The actual path follows the first-person-proxy mantle animation (`traversal_anim` 1), which is model data not in server.dll [?].
  - Jump-off happens after 0.1 s, or immediately once progress passes 0.5 [R].
  - Pulling back past 0.7 aborts the mantle; crouching while hanging drops you [R].

## 8. Dodge (pilot/titan, class-enabled)
- Needs you standing (not crouched), at least `dodgeInterval` **0.6 s** since the last dodge, and power ≥ `dodgePowerDrain` **33** [R]. The dodge then costs exactly 33 power and delays regen by `dodgePowerDelay` (default 0) × a player multiplier [R].
- Below `dodgeSpeed` **750** your speed becomes exactly 750 in the dodge direction [R]. Above it, your speed is kept and partly redirected [R].
- Height `dodgeHeight` **25** (vertical dodge `dodgeVerticalHeight` 200) [R]. The dodge ends early below `dodgeStopSpeed` **300** [R].
- Power regen `powerRegenRate` **10**/s [R].

## 9. Grapple
- **Fire:**
  - Hook speed `grapple_shootVel` **3000** [R]; retract speed **6000**, or **3000** when forced or once the line has wrapped [R].
  - Range comes from the weapon's `maxLength`/`maxLengthVert`, as an ellipse [R]; shipped 1100 / 1100 [D].
  - Pull delay **0.2 s**; grace period **0.25 s** before a world hook costs power [R].
- **Attach:** the initial impulse is `grapple_initialImpulse_human` **350**, but as decompiled it is capped at `(1−slow)·speedRampMin`, i.e. **50 u/s** for pilots [R]. Off the ground the impulse is **50** [R].
- **Pull, per tick:**
  - Speed toward a point 25 u (`grapple_lift`) above the hook grows by `grapple_accel_human` **1000**·dt [R].
  - The cap ramps **50 → 800** over **1.5 s** [R]. Speed above the cap is not removed [R].
  - Sideways speed decays by `grapple_decel_human` **425**·dt [R].
  - With `grapple_dontFightGravity` 1, the grapple never reduces vertical speed [R].
  - Pulling into a surface pushes around the obstacle at 4·`grapple_around_obstacle_accel` 1000 [R].
- **Air control while grappled:** `grapple_airSpeedMax` **420**, `grapple_airAccel` **1000** [R].
- **Gravity while grappled:** full when the point is steeply below (cos 0.8); otherwise it lerps between `grapple_gravityFracMin` and `grapple_gravityFracMax`, driven by how you push [R].
- **Detach** [R]:
  - The line exceeds max length + 50.
  - The line is too short, with a threshold that grows with speed; this gives an impact vertical boost.
  - You pass the point or move away faster than `grapple_detachAwaySpeed`; this gives a detach vertical boost.
  - Low-speed timers run out.
  - The target dies.
  - You get within 250 u of a titan embark.
  - **Detach speed loss** [R] (`0x1805d40d0`): if 3D speed s > `detachSpeedLossMin`, then `s = max(s − detachSpeedLoss, detachSpeedLossMin)`, keeping direction.
    - Shipped values 300 / 460 [D]: ≤ 460 is unchanged, 460–760 snaps to 460, above 760 loses 300 [C].
    - It only applies when the pull had started (`m_grapplePulling`) and the detach is not effect-suppressed; titan embark and window entry are suppressed [R]. A manual crouch-release counts [R].
    - It is applied *after* the detach or impact vertical boost [R].
    - Window entry applies the same loss itself before the window adjust (grapple_zipline.md §1.6a) [R].
- **Auto-melee:** when you detach on a pilot within reach, it fires within about 32° and 10 u, or when contact is predicted within 0.13 s [R].

## 10. Zipline
- **Attach:** within `zipline_use_range` **120**; reuse cooldown 1.0 s [R].
  - Mount time is the length of the viewmodel's zipline-mount animation [R]; that length is model data [?]. `mountZiplineTime` (0.5 s) is only a fallback for when that animation is missing [R].
  - The mount ends with velocity exactly `ziplineSpeed` along the rope chord, with no rope scale [R].
- **Ride** (`Zipline_MoveUpdateSliding`, `0x1806b0920`; movetypes 2/7 → `0x180187930`, state 3) [R]:
  - **Speed** is measured along the straight chord between two rope nodes [R]. Each tick: `s = dot(dir, vel) + ziplineAcceleration·dt`, floored at 0 (no running backwards), capped at `m_ziplineMoveSpeedScale × ziplineSpeed` [R]. With shipped values that is accel **400 u/s²** [R] and a cap of **600 × rope scale** [C].
  - **Position:** XY moves along the chord. Z follows the rope's sag parabola `A·t² + B·t + C`, and the player hangs one hull height below it [R].
  - **Gravity** only acts on a descending chord, and as written it *slows* the rider by `0.5·sv_gravity·entGravity·dir.z²` per second (sign checked in the disassembly) [R]. That is always less than 400, so **steady-state speed is the cap on any slope** [C]; this assumes entity gravity ≈ gravityScale 0.8 [I], and holds for any entity gravity up to about 1.06 [C].
  - **Bends** keep `s·cos θ`, and accel then recovers the rest [R].
  - **Detach** happens on a hull hit, at the end of the last segment, or within the end node's `m_ziplineAutoDetachDistance` [R].
- **Dismount:** velocity × `ziplineJumpOffDampScale` 1 (no change by default); jump-off speed is 400 [R].

## 11. Other
- **Window traversal** [R]:
  - You must be at a window hint's edge with forward input ≥ 0.3.
  - Hull-checked mid and end points use sideways offset 18 and forward offset 6; the hand is placed 22 u up.
  - A hand spot needs 6 u of width.
  - The move takes 0.3 s on movetype 12, with a 0.5 s cooldown.
- **Sprint:** free by default (`sprint_powerdrain` 0); starting needs power ≥ 10 [R].
  - Sprint is the only entry in the suit-power "device" drain table [R].
  - If `sprint_powerdrain` were non-zero, sprinting would drain `rate·dt` per tick, stop the sprint when power hits 0, charge `0.1·rate` on stop, and hold regen off until 0.5 s after stopping [R].
  - Power helpers: `0x1805e2170` subtracts power, clamps it at 0 and returns false on overdraw. `0x1805e2150` extends the regen-blocked-until time to `curtime + delay` (ground_misc.md §3.5) [R].
  - The sprint view lowers by 10 u after 0.2 s and takes 0.3 s: these are the code defaults of `sprintViewOffset`/`sprintStartDelay`/`sprintStartDuration` [R], and the behaviour is read from their help text, not traced (client-side) [I].
  - Sprint body tilt is a spring (accel 35, max velocity 2), reaching full tilt at 120°/s turn [R]. The up-to-10° roll is the `sprinttiltMaxRoll` default and help text; its client-side use is [I].
- **Air slow-mo:** scales the timestep by `airSlowMoSpeed`, with a 0.25 s ramp in and 1.0 s ramp out [R]. The default 1.0 means off unless a class enables it, and by default it only applies while hovering [R].

## 12. Anti-tamper (not gameplay)
server.dll carries Denuvo trigger blocks [R] that, judging by their names and bodies, sabotage movement on tampered copies [I]:
- **`Denuvo_CheckWallrun`** sets a flag that makes CanWallRun fail after 120 s on maps whose name contains "_ti" [R]. That this fires only on copies judged tampered is [I]; Denuvo may virtualise the block in the shipped binary, so the exact trigger is unreliable [?].
- **`Denuvo_SlowSpeed`** multiplies the move-speed scale by 0.1 after 600 s [R], under the same kind of tamper check [I].

Ignore both for a reimplementation. See wallrun.md and ground_misc.md.

## 13. Open items
- **Per-pilot `.set` values:** resolved; see Section 0 [D].
  - Files: `vpk_extract\scripts\players\` (77 files, CRC-verified).
  - Extractor: `scripts\vpkdir.js` plus pg9182's `tf2lzham.wasm` (MIT, from github.com/pg9182/tf2lzham v0.0.8), run under Node's WASI. The built tf2vpk `.exe` was blocked by Windows Smart App Control, so it wasn't used.
  - **Stim** (from scripts): `speed_boost` severity 0.4 [D] gives SpeedScale × (1 + 2·0.4) = **1.8×** [C], so sprint 260 → 468 u/s [C], for duration + 0.5 s [D]. This rests on the `(1 + 2·boost)` term being the `speed_boost` effect [I]; `sh_stim.gnut`'s comment assumes an extra scale of 2.0 [D], matching the hard-coded 2 [R].
    - **Phase Shift exit:** `move_slow` 0.75 for 0.75 s [D], but only when the ability carries a Frontier Defense `pm0`–`pm2` mod and the shift is cancelled early (`mp_ability_shifter.nut`: the block is gated on `GetPVEAbilityLevel(weapon) >= 0`, which is −1 without those mods) [D]. No exit slow was found for the normal MP Phase Shift [D; client/shared scripts only].
    - Details are in [TITANFALL2_COMBAT.md](TITANFALL2_COMBAT.md). Playlist overrides were not checked [?].
- **Resolved in the logic8 pass** (outputs in `logic8\`):
  - **Grapple detach speed loss** [R]: `0x1805d40d0`, `s = max(s − L, M)` when `s > M`. It is gated on `applyEffects && m_grapplePulling`. See §9 and grapple_zipline.md §1.6a.
  - **Steady-state zipline ride** [R]:
    - `0x1806b0920` (Zipline_MoveUpdateSliding) is reached through ZiplineMove `0x180187930` for movetypes 2/7.
    - It is the only reader of `ziplineAcceleration` +0x3a8. The integrator is `0x1806af1d0`, and the gravity sign was checked against the disassembly.
    - See §10 and grapple_zipline.md §2.
  - **Edge friction** [R]: `0x18015fe70` has no caller. Ghidra xrefs, raw pointer/RVA scans of all blocks and a rel32 brute force over `.text` find only its own `.pdata`/unwind records. `sv_edgefriction` is never read, so there is no edge friction. See §2 and jump_air_friction.md §2.
  - **Power helpers** [R]: `0x1805e2170` is DrainSuitPower (subtract, clamp at 0, false on overdraw). `0x1805e2150` is DelayPowerRegen (`m_flTimeAllSuitDevicesOff = max(old, curtime + delay)`). Sprint is the only suit-power device. See §11 and ground_misc.md §3.5.
- **Still open** [?]:
  - The value of each rope's `m_ziplineMoveSpeedScale`, which sets the actual ride cap. It is map or script data, not in server.dll.
  - The real zipline mount duration, i.e. the length of the viewmodel's mount animation (activity 0x136), which is in model data.
  - Whether the zipline "gravity slows descent" sign was intended. The code is unambiguous; the intent is not.
  - The purpose of `FUN_18016dd00`, called after the detach speed loss.
  - The context of `FUN_18062b3f0`, a second dodge/continuous power-drain user.
- **Client-side visuals** (view tilt, view punch springs) live in client.dll, which was not analyzed [?].

## Reproducing
Inputs and tools live in ``:
- Ghidra project: `proj\`. Scripts: `scripts\`:
  - `DumpConVars.java`
  - `DumpConVarUsers.java`
  - `DumpCallees.java`
  - `DumpMovement.java`
  - `Scan8.java`: references to targets, plus an all-instruction scan for `[reg+disp]` displacements.
  - `Disasm8.java`: instruction or byte listing for an address range.
  - `RefsRange8.java`: references into a range, plus raw pointer and RVA hits.
  - `RelScan8.java`: rel32 brute force over `.text`.
  - `StrRefs8.java`: string to reference context, used for datamap field names.
  - Arguments to these are `+`-separated lists or `<start>x<end>` ranges, because cmd.exe splits on commas.
  - `vpkdir.js` (VPK directory lister)
- Data: `convars.tsv` (916 ConVar registrations); raw decompiles in `decomp\` and `logic*\`.
- Command shape:

  ```bash
  analyzeHeadless.bat <proj> tf2 -process server.dll -noanalysis -readOnly -scriptPath scripts -postScript <Script>.java <args>
  ```

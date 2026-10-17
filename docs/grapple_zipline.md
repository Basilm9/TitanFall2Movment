# Titanfall 2 pilot GRAPPLE and ZIPLINE (server.dll static analysis)

> **Errata:** some statements in this note were superseded by later analysis passes or by the extracted `.set`/weapon files. See [ASSUMPTIONS.md §12](ASSUMPTIONS.md) for the list, and [TITANFALL2_MOVEMENT.md §0](TITANFALL2_MOVEMENT.md) for shipped values. Prefer those sources where they disagree.


Source: Ghidra output in `logic2\` (the functions listed below), helpers in `logic\`, `logic6\`, `logic8\` (detach speed loss, zipline ride; scripts `Scan8`, `Disasm8`, `RefsRange8`, `StrRefs8`) and `decomp\`, `convars.tsv`, the string snapshot `server.snap`, and `notes\player_settings_table.md` (code defaults for player-settings keys).
Every number below is a ConVar default from `convars.tsv`, a player-settings code default from the settings-descriptor table, or a `.rdata` constant (`DAT_x /*f=...*/`) read in code. No values are guessed. Class `.set` overrides and weapon `.txt` values are **not available**, so weapon keys (maxLength, power) are given by name and struct offset only.

Confidence tags:
- **[R]**: read directly from the decompiled code or the string table.
- **[I]**: inferred. This covers function identities, field names, and intent the code does not state.

## 0. Shared conventions

This section builds on `notes\slide_mantle.md` section 0. The same `mv` offsets apply: `mv+0x8c` velocity, `mv+0xe4` origin, `mv+0x6c` wish direction, `mv+0x24` forwardmove, `mv+0x84` hull radius/width.

| Expression | Meaning | Tag |
|---|---|---|
| `PTR_DAT_180b75f60` | global `mv` (CMoveData) used by the server-side grapple code | [I] (`+0x8c` velocity, `+0xe4` origin, same as `param_1+0x10` in movement code) |
| `mv+0x30..0x38` | an aim/view forward vector, used for "in front" tests | [I] |
| `mv+0x54..0x58` | view forward xy, used for "behind the point" tests | [I] (prior note) |
| `mv+0x7c` | a hull height/step value, used for "reasonable height" and the vertical obstacle probe | [I] |
| `mv+0x18` bit `0x40000` | a button bit that forces immediate auto-melee | [I] (unknown button) |
| `mv+0xa4..0xac` | an unknown vector compared with velocity elevation in the detach test | [I] |
| vfunc `+0x2a0` | `IsTitan()` (it selects every `_titan` / `_human` ConVar) | [I] (strong) |
| vfunc `+0x298` | `IsPlayer()` | [I] |
| vfunc `+0x218` | `IsNPC()` | [I] |
| vfunc `+0x428` | eye/world-centre position used as the rope's player end | [I] |
| vfunc `+0x9c8` | returns `&m_grapple` (the code reads `+0x14` point[0] and `+0x50` pullTime from it) | [I] (strong) |
| vfunc `+0x9d0` | kill/reset the hook (used on every failure path) | [I] |
| vfunc `+0x478` | gravity scale (0 means 1.0) | [I] |
| `FUN_180222030(ent)` | a 0..1 slow fraction; ramp speeds are multiplied by `(1 - x)` | [I] (string `grapple_slow` exists; function not dumped) |
| `FUN_1805cbca0(player)` | air-slowmo time scale, 1.0 when not slowed (`logic2\fn_1805cbca0.c`) | [R] |
| `FUN_1806458b0(player)` | IsRodeoing (Event_Killed prints "Player has not ended rodeo" when it is true) | [I] (strong) |
| `FUN_1805d3f60(player, vel, applyEffects)` | GrappleDetach (called whenever the detach test fires). Dumped in `logic8\d1`; speed loss in §1.6a | [R] body / [I] name |
| `FUN_18060ccb0(grapple, params, hook, owner, player)` | StartPull (`logic6`; §3) | [R] body / [I] name |
| `FUN_18060bf70(owner, target, isReattach, vel*)` | Applies the attach/pull-start impulses to the owner and the reeled target (`logic6`; §3) | [R] body / [I] name |
| `FUN_1806101b0(ent, other, entIsGrappler)` | CanBePulled: decides whether `ent` moves at all (`logic6`; §3) | [R] body / [I] name |
| `FUN_1805e1f80(player, amt)` | DrainSuitGrapplePower: `player+0x15e4 -= amt`, floored at 0; returns false if it would go negative | [R] |
| `FUN_180611540(hook)` | Unparents the hook (vfunc 0xf8 SetParent(NULL)) and notifies the old parent if it is an NPC. It does **not** kill the grapple. | [R] |
| `FUN_18060fcd0(pos)` | IsGrappleAllowedAt: `GrappleAllowedEnumerator` over triggers (`trigger_no_grapple`) | [R] |
| `FUN_18060ff50(player,a,b,c)` | Wrapper that calls `FUN_18016dd00(player, mv.origin, mv.vel, mv+0x30, a,b,c)`, the window velocity adjust | [R] wrapper / [I] purpose |
| `FUN_1806079b0` | FindWindowHint (uses `window_hint_debug`). `fn_180609500`/`fn_180609d70` are its geometry helpers, not zipline code | [I] |
| `DAT_1814feb94` / `DAT_180c03950` | normalisation epsilon (value not annotated) | [R] use |
| `gpGlobals` | `DAT_180bfbe08`: `+0x10` curtime, `+0x30` frametime | [I] (prior note) |

### 0.1 `CPlayer::m_grapple` (GrappleData, at player+0x2380, size 0x68)

`decomp\fn_180572380.c:57` registers `m_grapple` at 0x2380. The datatable gives it size 0x68 [R].
The sub-field names exist in `.rdata` in this order: `m_grappleVel`, `m_grapplePoints`, `m_grapplePointCount`, `m_grappleAttached`, `m_grapplePulling`, `m_grappleRetracting`, `m_grappleForcedRetracting`, `m_grappleUsedPower`, `m_grapplePullTime`, `m_grappleAttachTime`, `m_grappleDetachTime`, `m_grappleMeleeTarget`, `m_grappleHasGoodVelocity`, `m_grappleLastGoodVelocityTime` (0x180970a18..0x180970b50).
The offsets come from how the code uses each field. That layout fills 0x68 exactly in the same order:

| GrappleData off | player off | Field | Evidence | Tag |
|---|---|---|---|---|
| +0x00 | 0x2380 | vtable (NetworkStateChanged) | `(**(code**)*param_1)(param_1,&field)` before every write | [R] |
| +0x08 | 0x2388 | m_grappleVel (vec3) | shoot writes `dir*grapple_shootVel` here (ce50, e0580) | [R] use / [I] name |
| +0x14 | 0x2394 | m_grapplePoints[4] (vec3 each, stride 0xc) | point[0] is the anchor nearest the player and the pull target. point[count-1] is the hook tip. Wraps shift the points up and insert at [0] (171290) | [R] use / [I] name |
| +0x44 | 0x23c4 | m_grapplePointCount | set to 1 on shoot, ++ per wrap, >3 means fail, 0 means hook home | [R] use |
| +0x48 | 0x23c8 | m_grappleAttached | set on attach (d010/170660), cleared on shoot | [R] use |
| +0x49 | 0x23c9 | m_grapplePulling | gates the pull, gravity frac and jumpFrac | [R] use |
| +0x4a | 0x23ca | m_grappleRetracting | if set when the hook attaches, StartPull runs immediately (no pull delay) | [R] use / [I] name |
| +0x4b | 0x23cb | m_grappleForcedRetracting | selects grapple_forcedRetractVel | [R] use |
| +0x4c | 0x23cc | m_grappleUsedPower | accumulates suit-power drain | [R] use |
| +0x50 | 0x23d0 | m_grapplePullTime | speed-ramp and grace-period origin | [R] use |
| +0x54 | 0x23d4 | m_grappleAttachTime | set at attach. Pull starts after pullDelay | [R] use |
| +0x58 | 0x23d8 | m_grappleDetachTime | Event_Killed compares it with autoMeleePredictTime | [I] |
| +0x5c | 0x23dc | m_grappleMeleeTarget (EHANDLE) | set within 300 u of a hooked target | [R] use |
| +0x60 | 0x23e0 | m_grappleHasGoodVelocity | low-speed detach timer input | [R] use |
| +0x64 | 0x23e4 | m_grappleLastGoodVelocityTime | stamped when good velocity is lost | [R] use |

Other player netvars [R], from `decomp\fn_18055ba00.c` and `fn_180572380.c`:
- 0x23e8 `m_grappleActive`
- 0x23e9 `m_grappleNeedWindowCheck`
- 0x23ec `m_grappleNextWindowHint`
- 0x2050 `m_grappleHook`
- Zipline: 0x23fc `m_activeZipline`, 0x2400 `m_ziplineReverse`, 0x2404 `m_lastZipline`, 0x2408 `m_useLastZiplineCooldown`, 0x240c `m_ziplineValid3pWeaponLayerAnim`, 0x2410 `m_ziplineState`, 0x2418 `m_zipline` (sub-struct; field names are not in the dump)

### 0.2 Per-tick GrappleParams block (`param_2` of dad0/610390/ccb0)

`fn_180170660.c:200-220` builds this block on the stack before calling StartPull, which shows where every slot comes from:

| slot (float idx) | source | key | Tag |
|---|---|---|---|
| [0] | weapon+0x1f38 (0 with no weapon) | grapple_power_use_rate | [I] (multiplied by dt and drained as suit power) |
| [1] | weapon+0x1f34 (0) | grapple_power_required | [I] (caps total power used per grapple) |
| [2] | settings+0x2a8 | grapple_detachLowSpeedThreshold | [R] use / [I] name |
| [3] | settings+0x2ac | grapple_detachLowSpeedMeleeThreshold | [R] use / [I] name |
| [4] | settings+0x288 | grapple_detachLengthMax | [R] use / [I] name |
| [5] | settings+0x284 | grapple_detachLengthMin | [R] use / [I] name |
| [6] | weapon+0x1f3c (10.0 with no weapon) | grapple_maxLength | [I] (strong; see below) |
| [7] | weapon+0x1f40 (10.0 with no weapon) | grapple_maxLengthVert | [I] (strong) |

The fallback with no weapon is `0x4120000041200000`, i.e. 10.0 and 10.0 [R].
Weapon key strings appear in this order: `grapple_weapon`, `grapple_power_required`, `grapple_power_use_rate`, `grapple_maxLength`, `grapple_maxLengthVert`.
`fn_1805e0580` uses weapon+0x1f3c and +0x1f40 in an elliptical range formula: `L / sqrt(((L/Lv)^2 - 1)*dir.z^2 + 1)` gives L for a horizontal aim and Lv for a vertical aim. That confirms +0x1f3c is maxLength (horizontal) and +0x1f40 is maxLengthVert.

### 0.3 Per-class grapple settings (`.set` keys; settings block = `param_1+0x18`, or `FUN_180556080(player[0x373])`)

Key strings follow the slide keys in `.rdata`, at consecutive 4-byte offsets. `notes\player_settings_table.md` now confirms each key's offset and **code default** from the settings-descriptor table (descriptor addresses 0x180b82940..0x180b82ca0) [R]. These are the binary's built-in defaults. A class `.set` file can override them, and those files are still unavailable.

| offset | key | code default | help string (binary) | consumer | Tag |
|---|---|---|---|---|---|
| 0x284 | grapple_detachLengthMin | 33 | "Minimum length of grapple line (always detaches when shorter than this)" | params[5], 610390 | [R] |
| 0x288 | grapple_detachLengthMax | 50 | "...(detaches when shorter than this and at full speed)" | params[4], 610390 | [R] |
| 0x28c | grapple_detachAwaySpeed | 300 | "Grapple detaches when moving away from it at this speed" | 610390:238 | [R] |
| 0x290 | grapple_gravityFracMin | 0.0 | "Fraction of gravity used when grappling; fights with grapple_accel" | 170e60:34 | [R] |
| 0x294 | grapple_gravityFracMax | 0.75 | "...when grappling and pulling away from the grapple or pushing under it" | 170e60:68 | [R] |
| 0x298 | grapple_detachVerticalBoost | 200 | "Vertical boost given when detaching after passing the grapple point" | 610390:248 | [R] |
| 0x29c | grapple_detachVerticalMaxSpeed | 200 | "Limits vertical boost from grapple_detachVerticalBoost" | 610390:244 | [R] |
| 0x2a0 | grapple_detachSpeedLoss | 400 (shipped 300) | "Reduces speed when grapple detaches" | `logic8\d1\fn_1805d40d0.c` (instr 0x1805d414e), called from Detach `fn_1805d3f60` and from the window-entry branch of 610390. See §1.6a | [R] |
| 0x2a4 | grapple_detachSpeedLossMin | 430 (shipped 460) | "grapple_detachSpeedLoss doesn't reduce speed lower than this" | `fn_1805d40d0` (instr 0x1805d4110). See §1.6a | [R] |
| 0x2a8 | grapple_detachLowSpeedThreshold | 250 | "Grapple detaches when below this speed for grapple_detachLowSpeed(Ground)Time" | params[2] | [R] |
| 0x2ac | grapple_detachLowSpeedMeleeThreshold | 100 | "...below this speed for grapple_detachLowSpeed(Melee)Time" | params[3] | [R] |
| 0x2b0 | grapple_detachLowSpeedTime | 1.5 | "...for this length of time in the air" | 610390:299 | [R] |
| 0x2b4 | grapple_detachLowSpeedWallTime | 1.2 | "...when wall running" | 610390:294 | [R] |
| 0x2b8 | grapple_detachLowSpeedGroundTime | 0.7 | "...on the ground" | 610390:290 | [R] |
| 0x2bc | grapple_detachLowSpeedMeleeTime | 1.0 | "...below grapple_detachSpeedMeleeThreshold ... on the ground" | 610390:303 | [R] |
| 0x2c0 | grapple_impactVerticalBoost | 300 | "Vertical boost given when hitting the grapple point" | 610390:131 | [R] |
| 0x2c4 | grapple_impactVerticalMaxSpeed | 300 | "Limits vertical boost from grapple_impactVerticalBoost" | 610390:126 | [R] |
| 0x2c8 | grapple_airSpeedMax | 420 | "Max speed from player movement controls while grappling" | `logic\fn_180161170.c:93` (AirMove) | [R] |
| 0x2cc | grapple_airAccel | 1000 | "Acceleration from player movement controls while grappling (fights with grapple_decel)" | `logic\fn_180161170.c:94` | [R] |

Two per-weapon/suit grapple power keys also appear in the settings table: `grapple_power_regen_delay` (+0x188, default 1) and `grapple_power_regen_rate` (+0x198, default 17). I found no consumer for either in my functions.

### 0.4 Per-class zipline settings (from `notes\player_settings_table.md`)

| offset | key | code default | help string | consumer | Tag |
|---|---|---|---|---|---|
| 0x39c | mountZiplineTime | 0.5 | "Time taken for player to mount up to a zipline and begin sliding" | `logic8\d2\fn_1806af500.c` (mount start). It is only a **fallback**: `m_mountingZiplineDuration` (m_zipline+0xc) is normally set to the length of viewmodel activity 0x136 (the zipline mount anim), and this key is used only when that activity is missing ("Zipline activity %s was not found on viewmodel %s") | [R] |
| 0x3a0 | useZiplineCooldown | 1.0 | "Time before player can use the zipline again after detaching" | `fn_1806afa00` writes it into `m_useLastZiplineCooldown` | [R] |
| 0x3a4 | ziplineSpeed | 800 (shipped 600) | "Maximum speed when moving along zipline" | `fn_1806afca0` (mount) and `fn_1806b0920` (ride), × rope `m_ziplineMoveSpeedScale` (+0xb60) | [R] |
| 0x3a8 | ziplineAcceleration | 400 | "Acceleration to zipline speed when moving along zipline" | `logic8\d2\fn_1806b0920.c` (instr 0x1806b0c35), passed as the `accel` argument of `fn_1806af1d0`. An all-instruction scan (`logic8\scan1\disp.txt`) finds 22 float reads of `[reg+0x3a8]`. This is the only one inside a function that also calls the settings getter `FUN_180556080` (101 callers, `logic8\refs4.txt`). None of the others is in the game-movement range 0x18016xxxx–0x18018xxxx, where settings come from `this+0x18` | [R] |
| 0x3ac | ziplineJumpOffSpeed | 400 | "Speed player will move at when they jump off a zipline" | `fn_1806afca0` dismount | [R] |
| 0x3b0 | ziplineJumpOffDampScale | 1 | "When the player detaches from a zipline, the current velocity is immediately scaled by this amount (0 = instant stop, 1 = no change)" | `fn_1806afca0` dismount | [R] |

---

## 1. Grapple lifecycle

### 1.1 Fire
- **Shoot** (`fn_18060ce50`) [R]: pointCount=1, `m_grappleVel = aimDir * grapple_shootVel` (3000), point[0]=start.
  It clears attached, pulling, retracting, forcedRetracting, usedPower=0 and meleeTarget, and sets hasGoodVelocity=1.
- **Range** [R]: an ellipse with `grapple_maxLength` horizontally and `grapple_maxLengthVert` vertically (weapon fields; values unknown). The live line detaches if `len > max(maxLength, maxLengthVert) + 50` [R, 610390:85].
- **Edge aim-assist on fire** (`fn_1805e0580`) [R/I]:
  - If the aim trace (mask 0x46404033) reaches full range without a hit, it searches for a traversal/ledge hint along the aim (`FUN_1806079b0`, using `automantle_height` and 90° limits).
  - It tries both sides of the hint, offset by `hint+0xa00 + 4`. A side is accepted if its direction is within dot ≥ **0.95** of the aim and a trace there hits at `≥ dist - 15`.
  - When accepted, it rewrites `m_grappleVel = newDir * grapple_shootVel`.

### 1.2 Travel and hook return (`fn_18060ed50`, per tick)
- Return speed [R]:
  - `grapple_retractVel` (6000, "hasn't hit anything yet") when pointCount ≤ 1 and the retract is not forced.
  - Otherwise `grapple_forcedRetractVel` (3000, "finished or cancelled").
  - The step per tick is `dt * vel`.
- The tip moves toward the player along the stored polyline. Whole segments shorter than the step are consumed (count--) [R].
- On a non-forced single-segment return the tip also drops `grapple_fallSpeed * dt` (300) [R].
  The tip is traced (CGrappleTraceFilter, mask 0x4640420b). A hit can still **attach on the way back**: it goes to OnAttach and, because `retracting` is set, the pull starts immediately.
  The hit is ignored and the hook is killed if:
  - `sv_forceGrapplesToFail` is set,
  - the player is rodeoing (`m_rodeo` +0x1c10), or
  - the spot is inside a `trigger_no_grapple` (GrappleAllowedEnumerator / surface flag 0x800).
- When the tip reaches the player (`dist ≤ step`), the code plays `Pilot_Grapple_Retract_1P/3P` and `Pilot_Grapple_Reset` and sets count=0 [R].

### 1.3 Attach (`fn_18060d010` on the hook side; `fn_180170660` in movement when the line is re-hooked onto an entity)
These steps are [R]:
1. On the first attach, `m_grappleAttachTime = curtime`. Then attached=1, retracting=0, meleeTarget is cleared.
2. A water trace runs back 100 u from the hit point (`DAT_18087ebe8` = 100.0). If blocked, in water or start-solid, the "grapple_disallowed" impact FX plays; otherwise "grapple" plays.
3. The hook entity is parented to the hit entity. Script callbacks fire (CodeCallback_OnPlayerGrappled / OnProjectileGrappled / OnGrapple [I]).
4. If not yet pulling and `retracting` was set, StartPull runs at once. If already pulling and the target changed, `FUN_18060bf70` runs and the Traverse or ReelInTarget sounds play.
5. **Rodeo**: if the player is rodeoing, `vel.z += grapple_rodeoVerticalImpulse` (750) and the rodeo ends. d010 also snaps `mv.origin` to the player origin.

### 1.4 Pull delay, then StartPull
- In `fn_18060dad0` [R]: if no detach is due and not yet pulling, StartPull (`FUN_18060ccb0`) runs once `curtime - attachTime > grapple_pullDelay_human` (0.2, titan 0.2).
- **StartPull** (`logic6\fn_18060ccb0`) [R]:
  - Sets `m_grapplePulling=1` and `m_grapplePullTime=curtime`.
  - If the hook is parented to an entity (reeling a target), it sets `usedPower = power_required` and drains that whole amount from suit power at once. Sound: `Pilot_Grapple_ReelInTarget`. Otherwise the sound is `Pilot_Grapple_Traverse` and power drains only after the grace period.
  - Calls `FUN_18060bf70(owner, hookParent, 0, &mv.vel)` to apply the impulses.
  - For players: detaches from any zipline (`FUN_1806afa00`), clears `m_grappleNextWindowHint`, then searches for a window hint along the line (`FUN_18060ca70`).
- **Impulse application** (`logic6\fn_18060bf70`) [R]:
  - If the hooked target is an enemy player, the target is reeled toward the owner. It gets `Grapple_InitialImpulse(target, owner, 0, targetVel)`, is knocked off the ground if that reports so, and gets `SetAbsVelocity`. A target in state 0xc first runs `FUN_1805ce2a0` (exits that state [I]).
  - If the target is an NPC, its AI is notified (`FUN_1802f14f0`).
  - The owner gets `Grapple_InitialImpulse(owner, target, 1, &mv.vel)`, then `SetGroundEntity(NULL)` if that returns true.
  - On a re-attach while already pulling (`isReattach=1`, from OnAttach), only the off-ground impulse is applied, and only if on ground and `CanBePulled`.
- **CanBePulled** (`logic6\fn_1806101b0`) [R] gates both the impulse and the per-tick pull:
  - No target, or a world target: true.
  - Titan `ent`: true only when the titan is the one being reeled (`entIsGrappler=0`) by a titan.
  - Pilot grappling an enemy pilot: false while the pilot stands on ground and the target is unparented. The enemy is reeled instead; the pilot only follows once airborne.
  - Pilot hooked to a titan: true.
  - Pilot hooked to an NPC: true for AI types in bitmask 0x12814 (2, 4, 11, 13, 16); otherwise only when airborne.
  - NPC `ent` by AI type: 2, 0xb, 0x10 false; 0xd only toward titans; others true.
- **Initial impulse** (`fn_18060c0c0`) [R], called from `FUN_18060bf70` as above. See §3 for pseudocode. Pilot defaults:
  - The along-line part (≥0) is kept. XY perpendicular speed is kept ×1.0 (`initialSlowFrac_human`) and vertical perpendicular speed ×0.4 (`initialSlowFracVert_human`).
  - If the along-line speed is below `(1-slow)*grapple_speedRampMin_human` (=50), it is raised by `(1-slow)*grapple_initialImpulse_human` (350). The result is clamped to `[initialSpeedMin (0), (1-slow)*rampMin]`.
  - **As decompiled, the 350 impulse is capped at speedRampMin, so a pilot gets at most 50 u/s along the line.** Titan: rampMin 400, impulse 350.
  - Then the **off-ground impulse** (`fn_18060c3e0`): only if standing on ground, the into-ground part of the velocity along `m_upDir` is removed and `grapple_initialImpulseOffGround_human` (50) is added along up. NPCs get `vel.z = max(vel.z,0) + 150` (`_human_npc`); titans get 0.

### 1.5 Per-tick pull (movement: `fn_16a9f0` calls `fn_18016fa80`, which calls `fn_18060b9a0`)
This runs only while `m_grappleActive && m_grapplePulling` [R]. The pull vector is `d = point[0] + (0,0,grapple_lift) - eyePos` (lift 25, "Distance above grapple hook that player is pulled to") [R].

1. **Along-line acceleration** [R]:
   - Speed toward the point `a = vel·d̂`.
   - Ramp cap `S = (1-slow) * lerp(speedRampMin, speedRampMax, clamp01((t - pullTime)/speedRampTime))`. Pilot: 50 to 800 over 1.5 s. Titan: 400 to 750 over 1.0 s.
   - If `a < S`: `a = min(a + grapple_accel*dt, S)`. Pilot accel is 1000 u/s² (titan 1800).
   - Speed above S is **not** removed.
2. **Off-axis deceleration** [R]: the perpendicular part's magnitude shrinks by `grapple_decel*dt` (pilot 425, titan 200), clamped at 0.
   Two parts are exempt (re-added unchanged):
   - With `grapple_dontFightGravity`=1, the downward part of the perpendicular velocity.
   - The part along a normal from pulled-entity vfunc `+0x998`, if positive [I: plausibly the up vector].
   These exemptions are skipped when a melee target is valid, when a titan reels in a pilot, or when a window hint is active.
3. **Melee closing**: if the hooked target can be auto-meleed, perpendicular speed is also capped at `max(0,(dist-100))*grapple_decelMeleeStrength` (1.0) [R].
4. **Don't fight gravity** [R]: with `grapple_dontFightGravity`=1 and no melee target, `vel.z_new = max(vel.z_old, min(vel.z_new, maxVz))`, where maxVz = FLT_MAX in the pilot path. The grapple never lowers vertical speed.
5. **Titans** ignore an upward pull component (`d.z` clamped to ≤0 before normalising) [R].
6. **Window hints** [R]: when `m_grappleNextWindowHint` is valid and `m_grappleNeedWindowCheck`==0, the accel direction becomes the current velocity direction, so the pull carries momentum through the window.
   The hint is dropped (needWindowCheck=1) if horizontal distance to it ≥ `grapple_windowCheckDist` (150) or the velocity no longer points into it.
7. **Being reeled in** [R]: for each hook attached *to* this player (player+0x37c list), the same accel runs toward that hook's owner (`param_4=0`). Their directions are summed and normalised. A hook is skipped when its owner is a player and `FUN_180418e10` (probably team [I]) matches.
8. **Leave ground** [R]: if on ground (non-titan) and `groundNormal·dir ≥ -0.3` (or ≥ -0.001 while wallrunning), `SetGroundEntity(NULL)` is called.
9. **Around-obstacle push** [R]:
   - Triggers when `surfaceNormal·dir < -0.5`, i.e. pulling into a surface. The normal is player+0x258c, or `m_upDir` if that is ~0 and the player is on ground.
   - Each tick picks one probe direction by `command_number % 4`:

     | case | direction | step |
     |---|---|---|
     | 0 | up | `mv+0x7c*0.4` |
     | 1 | side `(n.y,-n.x,0)` | `hullRadius*0.4` |
     | 2 | side `(-n.y,n.x,0)` | `hullRadius*0.4` |
     | 3 | down | `mv+0x7c*0.15` |

   - At 1× and 2× the step it hull-traces from the offset point along `dir*hullRadius`, then from origin to the offset point. If both are clear: `vel += probeDir * grapple_around_obstacle_accel * 4 * dt`, i.e. 4000 u/s² at default.
10. **Gravity fraction** (`fn_180170e60`, applied in the half-step gravity of `fn_16a9f0`/`fn_1688d0`) [R]. Pilots only; titans use 1.0:
    - If the point is **below and steep** (`d.z<0` and `d.z²/|d|² > grapple_letGravityHelpCosAngle²`, 0.8²): use **1.0** (full gravity).
    - If a window hint is active: use **0.0**.
    - Otherwise `frac = gravityFracMin + (gravityFracMax-gravityFracMin) * t`, where `t = max(min(1, pushUnder * grapple_gravityPushUnderContribution (2)), pullAwayXY)`:
      - `pushUnder` = forward-input component perpendicular to the rope, pointing "under" it. It only counts while looking down: factor `clamp01(-10*forwardmove*fwd.z)`.
      - `pullAwayXY` = wish-direction component pointing horizontally away from the point.
    - The result multiplies the wallrun gravity factor before `vel += gravDir*gscale*sv_gravity*0.5*dt*frac`.
11. **Jump while grappled**: jump velocity is multiplied by `grapple_jumpFrac` (1.0) (`logic2\fn_180172870.c`) [R].
11b. **Air control while grappled** (`logic\fn_180161170.c:84-98`, AirMove) [R]:
    - While `m_grappleActive && m_grapplePulling`, AirMove uses `grapple_airSpeedMax` (settings+0x2c8, default 420) as the max wish speed and `grapple_airAccel` (settings+0x2cc, default 1000) as the acceleration. Otherwise it uses the normal player air values at player+0x25a0/+0x25a4.
    - Wish speed = `airSpeedMax * |input| (≤1) * FUN_18016e9e0 * FUN_1805d2510` (two speed scales [I]). Accel = `airAccel * player+0x1600`.
    - Both go to the shared air-accelerate `FUN_180160f40`. This input accel fights `grapple_decel` (425), which strips sideways speed.
12. **Suit power** (`fn_18060dad0`) [R]: after `grapple_gracePeriod` (0.25 s, "Length of time player can grapple without using a charge") from pullTime, each tick drains `min(power_use_rate*dt, power_required - used - 0.001)`.
13. **Line wrapping** (`fn_180171290`, per movement tick) [R]:
    - It traces eye to point[0] and back (mask 0x46404003). If both traces are blocked (fraction ≤ 0.999):
      - Hitting a non-world entity moves the hook onto that entity: count=1, then re-attach (170660).
      - Hitting world inserts a new corner point at point[0]. The corner comes from intersecting the two hit planes, with a fallback to the midpoint, and must be within 30 u (`900.0` squared) of both hits.
    - More than 3 points calls `FUN_180611540(hook)`. That only unparents the hook (SetParent NULL plus an NPC notify); it does not kill it [R]. The same call unparents the hook before re-hooking it onto a newly hit entity.
    - A wrap point inside a `trigger_no_grapple` (`FUN_18060fcd0` false) forces a detach (`FUN_1805d3f60`) [R]. Either wrap trace hitting a surface with flag 0x800 (probably no-grapple [I]) also forces a detach. A third rule: if `pointCount > 2` and the spot is allowed, `FUN_1805db610` is called (unknown).

### 1.6 Detach conditions (`fn_180610390`; returns 1 = detach) [R]
`len` is the straight distance from the hook tip to the owner (min of origin and eye). `d` is the pull vector.

1. The owner is in some invalid state (`FUN_1800e2d90 && !FUN_1800e2e30`) while pulling [I].
2. `len > max(maxLength, maxLengthVert) + 50`.
3. **Too short**: `len < detachLengthMax`, no melee target, and `len < detachLengthMin + (vel·d̂ / speedRampMax)*1.5*(detachLengthMax - detachLengthMin)`. In words, the line may be longer before detaching when moving fast; `1.5` is `DAT_18087e124`.
   On that detach, if pulling, the player exists and the hook is on world geometry (no parent entity), the **impact boost** applies: `if vel.z < impactVerticalMaxSpeed: vel.z = min(vel.z + impactVerticalBoost, impactVerticalMaxSpeed)`.
4. The hooked entity is a phase-shifted player (vfunc 0x658 [I]) or dead (`+0x4f1`).
5. **Titan embark**: the hooked entity is the player's own titan, `len < grapple_titanEmbarkDist` (250), a trace is clear, and the script `CodeCallback_EmbarkTitanFromGrapple` agrees. This detaches with `suppressEffects=1`.
6. **Passed the point / moving away** (pulling, player only):
   - `movingAway = vel·d < 0`, with the hook not on an entity.
   - `passed = d·fwd < 0`. When looking down, both the pull vector and the tip must also be behind the view in XY.
   - *Detach with boost* if `movingAway && (vel·d̂)² > detachAwaySpeed²`, or if passed and moving away (not in air-slowmo, or moving away). Boost: `if vel.z < detachVerticalMaxSpeed: vel.z = min(vel.z + detachVerticalBoost, detachVerticalMaxSpeed)`.
   - *Detach without boost* if passed, not moving away, and `slowmoScale ≥ 1`.
   - If the velocity climbs at least as steeply as the vector at `mv+0xa4`, the passed test is skipped [I].
7. **Low speed**: "good velocity" means speed toward the point > `detachLowSpeedThreshold`, or the hooked target approaching faster than `detachLowSpeedMeleeThreshold`. It always counts as good before pulling starts.
   If not good for longer than:
   - `detachLowSpeedMeleeTime`, when a melee target is set,
   - `detachLowSpeedGroundTime`, on solid ground,
   - `detachLowSpeedWallTime`, on ground while `FUN_1805d5230` (IsWallRunning [I]) is true,
   - `detachLowSpeedTime`, in the air,
   
   then it detaches.
8. **Window entry**: while in the air with needWindowCheck set and within `windowCheckDist - hullRadius` of the hint, a window adjust runs (`FUN_18060ff50`). If the tip is below `hint height + 20` or behind the hint plane by more than 16 u, the grapple detaches with `suppressEffects=1`. Otherwise the velocity is restored and the hint cleared.

When a detach happens on a player, `FUN_1805d3f60(player, vel, !suppressEffects)` runs. NPC owners just kill the hook.

### 1.6a Detach speed loss (resolved; `logic8\d1\fn_1805d40d0.c`, disassembly in `logic8\dis1.txt`) [R]

`FUN_1805d40d0(player, vec3* vel)` is the only reader of `grapple_detachSpeedLoss` (+0x2a0) and `grapple_detachSpeedLossMin` (+0x2a4). Settings come from `FUN_180556080(player+0x1b98)`.

```c
L = settings.grapple_detachSpeedLoss;      // +0x2a0
M = settings.grapple_detachSpeedLossMin;   // +0x2a4
s = |vel|;                                 // full 3D speed, vertical included
if (s*s > M*M) {                           // strictly faster than the floor
    s2 = max(s - L, M);
    vel *= s2 / s;                         // direction kept
}                                          // at or below M: untouched
```

- **Shipped MP pilot (L 300, M 460):**
  - speed ≤ 460: unchanged;
  - 460 < speed ≤ 760: set to exactly 460;
  - speed > 760: loses 300.
- **Code defaults (L 400, M 430):** above 830 you lose 400; between 430 and 830 you drop to 430.
- **When it runs.** `FUN_1805d3f60(player, vel, applyEffects)` is the GrappleDetach:
  - It does nothing unless `m_grappleActive` (0x23e8) is set and `m_grapplePointCount` (0x23c4) ≠ 0.
  - The loss runs only when `applyEffects` is true **and** `m_grapplePulling` (0x23c9) is set. RCX/RDX pass through unchanged into `FUN_1805d40d0` (instr 0x1805d3f98). So a hook released during flight or the 0.2 s pull delay costs no speed.
  - After the loss it calls `FUN_18016dd00(player, origin, dir-from-vfunc-0x438, vel, 0, 1, 1.0)`. Its purpose is not established.
  - It then kills the hook (`FUN_1805db610`, `FUN_18060d7e0`, `FUN_180611540`, `FUN_1800eb4e0`).
- **Callers and their `applyEffects`:**

  | Caller | `applyEffects` |
  |---|---|
  | ServerThink `fn_18060dad0`, after `fn_180610390` returns 1 | `!suppressEffects` |
  | `FUN_180171140`, when mv buttons `& 0x4000004` (crouch [I]) or player+0x1bf4 | 1. A manual release costs speed |
  | Line-wrap no-grapple detaches in `fn_180171290` (two sites) | 1 |
  | `FUN_1805d4070`, a wrapper that uses the entity's abs velocity (+0x428) and writes it back | 1 |
  | `FUN_1801625e0` (traversal start; it zeroes mv velocity first, so the loss is a no-op) | 1 |

  - Titan-embark and window-entry detaches pass `suppressEffects=1`, so `FUN_1805d3f60` itself skips the loss for them.
- **Order:** the detach and impact vertical boosts (§1.6 items 3 and 6) are written into mv velocity *inside* `fn_180610390`. The detach call comes afterwards in ServerThink, so **the loss is applied after the boost**, to the 3D speed including the boosted vz.
- **Window-entry branch** (§1.6 item 8, `logic8\d2\fn_180610390.c:343-371`):
  1. Save mv velocity (mv+0x8c).
  2. Call `FUN_1805d40d0(player, mv+0x8c)`, which applies the same loss.
  3. Run the window adjust `FUN_18060ff50(player,1,1,…)`.
  - If that leads to a detach (`*suppressEffects=1; return 1`), the reduced velocity is **kept** and is not reduced a second time.
  - Otherwise the saved velocity is restored.

### 1.7 Auto-melee (`fn_18060dad0` + `fn_18060fc00`; `grapple_autoMeleeOnDetach`=1) [R]
- **Eligibility** (`fc00`), all required: the ConVar is on; attacker is not a player, or `FUN_1806304d0` (enemy check [I]) passes; a pilot cannot auto-melee a titan; target is a player, or an NPC whose type is not 0xb/0x10 (type 0xd only for a titan attacker).
- Within **300 u** (`90000.0` squared) of the hooked target, `m_grappleMeleeTarget` is set.
- **Melee now** if the owner is an NPC or button bit 0x40000 is held. Otherwise all of these are needed:
  - "In front": `dirXY·fwdXY > 0` and `cos² > 0.7225` (0.85, about 31.8°), or horizontally within the hull radius.
  - "Reasonable height": `|Δz| < mv+0x7c + 20`.
  - Either the horizontal gap minus 2×hullRadius is < 10 ("Close enough"), or `gap/closingSpeed - grapple_autoMeleePredictTime (0.13) < 0`.
- **Converge**: if the closing time is ≤ `grapple_autoMeleeConvergeTime` (1.0), both the attacker's and the target's velocities are blended toward each other by `k = dt/convergeTime` each tick (their along-line parts cancel and the target's vertical is damped by `1-k`). The target gets the new velocity.
- **Trigger**: `player+0x18f0` (m_queuedGrappleMeleeTime [I]) = curtime.
- **Killed by grapple melee** (`fn_18058afc0`, CPlayer::Event_Killed [I]): if the attacker's meleeTarget is the victim and `curtime - attacker.m_grappleDetachTime < autoMeleePredictTime + 0.4`, the victim's velocity is zeroed.
- `grapple_disableMeleeWhenActive` (0) blocks normal melee while the grapple is out (`fn_1805db7e0`) [R].
- `grapple_autoMeleeViewRotateSpeedNear/Far` (3/1) and `grapple_autoMeleePredict` (0, client-side) have no consumer in this dump.

---

## 2. Zipline

- **Attach range** (`fn_1806adaf0`, Zipline_Find [I]) [R]:
  - The horizontal distance from the player to the rope's XY line must be `< zipline_use_range` (120), compared squared. Then the closest polyline point must be within 120 u in 3D.
  - Each segment is clipped to a sphere of radius `use_range + 10` (130) around the player (`FUN_18011bba0`). The best candidate is the segment point closest to the view ray (`FUN_1802718d0`, tolerance 0.1).
  - The first and last segments are trimmed by hull traces 200 u in from each anchor (`DAT_18087e158` = 200.0), lowered by hull height, so the player does not clip the anchors.
  - The rope the player just left (`m_lastZipline`) is skipped while `m_useLastZiplineCooldown > 0`.
  - **Direction**: reverse if `ropeDir·viewDir < 0`. Near a rope end with no continuation (within `max(rope+0xa2c, 0.01)`), the direction is forced away from that end.
  - A script callback can veto (`DAT_180b37198`).
  - The attach point is lerped in XY. Its Z follows the rope sag curve `z = (t*A + B)*t + C` (rope+0xa38/0xa3c/0xa40) minus hull height. A final hull trace from the player to the attach point must be clear.
  - Debug prints: "Zipline_Find: Ropes ... are too close together" and "... is connected with itself".
- **Can use** (`logic6\fn_1806ad790`) [R]: the player must be alive (`+0x4f1`==0), not a titan, not parented, and pass `FUN_1805d6150` and `FUN_1800b5ab0` (both false) plus a `FUN_1800e2d90`/`FUN_1800e2e40` state test [I]. Both Find and the mount tick bail out without it.
- **Movement dispatch** (`logic8\d1\fn_180179950.c`, `fn_180187930.c`) [R]:
  - PlayerMove's switch sends movetypes **2 and 7** to `FUN_180187930` (ZiplineMove [I name]) when `FUN_1805e6af0(player)` is true (on a zipline [I]). Otherwise those movetypes run the traversal check `FUN_180183500`, then FullWalkMove.
  - `FUN_180187930` copies mv origin (mv+0xe4) and velocity (mv+0x8c) and then switches on `m_ziplineState` (player+0x2410):

    | State | Handler |
    |---|---|
    | 1 | `FUN_1806af500`, mount start |
    | 2 | `FUN_1806afca0`, mount blend |
    | anything else, i.e. 3 | `FUN_1806b0920`, ride |

  - It writes origin and velocity back, then calls CheckVelocity (`FUN_180164bc0`).
  - If the state was non-zero it also calls `FUN_18016dd00(player, origin, vel, this+0x30, 0, 1, 1.0)` and CategorizePosition(…, 0).
  - If `FUN_180162ff0` is true at entry, it detaches instead.
- **Mount start** (`logic8\d2\fn_1806af500.c`, state 1 → 2) [R]:
  - Plays viewmodel activity 0x136 and sets `m_mountingZiplineDuration` (m_zipline+0xc, player+0x2424) to that sequence's duration (`FUN_1806db1b0`).
  - **`mountZiplineTime` (settings+0x39c, 0.5) is used only if the activity is missing.** The real shipped mount time is therefore the length of the viewmodel animation, which this binary does not contain [R; duration value unknown].
  - Clears `m_mountingZiplineAlpha` (+0x2428), stamps `m_ziplineStartTime`-like +0x242c = curtime [I name], and sets state = 2.
  - Returns the stored source position (+0x2434) and velocity (+0x2440).
  - Field names come from the datamap strings at 0x1805742b4.. (`m_mountingZiplineDuration` = +0xc, `m_mountingZiplineAlpha` = +0x10) [R].
- **Mount transition** (`fn_1806afca0`, state 2, which becomes 3 when done) [R]:
  - Blend `b += dt / max(0.01, m_mountingZiplineDuration)`, clamped to 0..1.
  - The attach point is advanced along the rope by `FUN_1806af1d0` at speed `b*0.5*maxSpeed`, with accel 0 and `maxSpeed = rope.m_ziplineMoveSpeedScale (+0xb60) * ziplineSpeed` (settings+0x3a4, default 800, shipped 600).
  - The hang offset below the rope is `FUN_1806af190` = hull maxs.z - mins.z (hull height) [R].
  - Output position = `lerp(playerOrigin, attachPoint, b)`. Output velocity = `lerp(entryVel (player+0x2440), ±ropeDir*ziplineSpeed, b)`. That target is the bare `ziplineSpeed` with **no** rope speed scale, so the ride starts at exactly 600 u/s along the chord for shipped pilots [R].
  - At `b≥1` it sets `m_ziplineState=3` (riding), does weapon handling, and calls the script callback `DAT_180b371d8`.
  - If both hull traces (from the current origin and from the old attach point) are blocked, it detaches (`FUN_1806afa00`).
- **Rope motion** `FUN_1806af1d0(A, B, AB, |AB|², dir, reverse, accel, maxSpeed, curveA, curveB, curveC, pos, vel, entGravity, out pos, out vel, out t)` (`logic8\d2\fn_1806af1d0.c`; sign checks against the disassembly in `logic8\dis2.txt`) [R]:
  - A and B are the current and next rope nodes (+0x490 = abs origin), and `dir = normalize(B − A)`, the straight **chord**, not the sag tangent.
  - `entGravity` is vfunc 0x478, with 0 treated as 1. This is the same idiom FinishGravity `fn_1801688d0` uses, so it is the entity gravity scale [I strong]; for MP pilots that is presumably gravityscale 0.8 [I].
  - `dt` is gpGlobals+0x30.

  ```c
  t0  = max(0, dot2(pos - A, AB) / |AB.xy|²);              // XY projection, no upper clamp
  v   = vel;
  G   = entGravity * sv_gravity * 0.5 * dt * dir.z;
  if (G > 0) G = -G;                                         // G = -|G|
  descending = reverse ? (dir.z > 0) : (dir.z < 0);
  if (!descending && dir.z != 0) G = 0;                     // only on a descending chord
  v.z -= G;                                                  // i.e. v.z += |G|  (UPWARD)
  s = dot(dir, v) + (reverse ? -accel*dt : +accel*dt);
  if (reverse ? s > 0 : s < 0) s = 0;                       // never runs backwards
  s = clamp(s, -maxSpeed, maxSpeed);
  outVel = dir * s;
  p  = A + (B - A)*t0 + outVel*dt;  (p.z uses the 3D chord)
  t  = dot2(p - A, AB) / |AB.xy|², clamped to 0..1;
  outPos.xy = lerp(A.xy, B.xy, t);
  outPos.z  = (curveA*t + curveB)*t + curveC;                // quadratic sag; overrides p.z
  ```

  - The gravity sign is now verified from the instructions (0x1806af2dc–0x1806af327): `COMISS xmm0,0; JBE; XORPS xmm0,-0.0` and then `SUBSS vz, xmm0`. On a descending chord the code adds `0.5·g·eg·dt·|dir.z|` to **upward** vz. After projection that is `Δs = −0.5·sv_gravity·eg·dt·dir.z²`, so gravity **slows** a descending rider and does nothing on an ascending one [R]. Whether this was intended is unknown [I].
  - Rope field names come from the datamap [R]:
    - +0xa38/+0xa3c/+0xa40 are `m_ziplineCurveA/B/C` (typedescription entries at 0x180b7f4e0/548/5b0);
    - +0xb60 is `m_ziplineMoveSpeedScale` (0x1804b6263);
    - +0xa2c is `m_ziplineAutoDetachDistance` (0x1804b61c4).
  - Accel is 0 in the mount call.
- **Steady-state ride** `FUN_1806b0920(player, pos, vel)` (`logic8\d2\fn_1806b0920.c`; debug string "Zipline_MoveUpdateSliding") [R]:
  - **Gating:**
    - If CanUse fails, detach.
    - If buttons `& 0x4000004` or `& 2` are pressed, it uses the same dismount formulas as the mount tick, then detaches.
    - If the two nodes are less than 0.1 u apart ("Ropes %s and %s are too close together"), detach.
  - **Per-tick call:** `FUN_1806af1d0(A, B, …, accel = ziplineAcceleration (+0x3a8), maxSpeed = rope.m_ziplineMoveSpeedScale × ziplineSpeed (+0x3a4), …, pos with z + hullHeight, vel, entGravity)`. The returned z is lowered by hull height again, so the player hangs a hull-height below the rope.
  - **Collision:** a player hull trace (`FUN_1802721f0`) runs from the current origin to the new position. If fraction < 1, or either of the trace's two solid flags is set, the player detaches. Otherwise the new position and velocity are written.
  - **Segment hand-off and end of line:**
    - The next node is `FUN_1804b6590(B)` (`m_ziplineNext`), or `FUN_1804b65d0` (`m_ziplinePrev`) when reversed [I names].
    - If a next node exists and t reached the end (1, or 0 when reversed), `FUN_1806ad070(&m_activeZipline)` advances to the next segment.
    - If no next node exists: detach once t reaches the end, or once the rider is within `max(0.01, endNode.m_ziplineAutoDetachDistance)` of the end node.
    - The script callback `DAT_180b371f8` (presumably CodeCallback_ZiplineMove [I]) fires on each successful tick.
  - **What it means for speed** [R arithmetic / I summary]:
    - **Accel** is `ziplineAcceleration` 400 u/s², added every tick (gravity scale not applied).
    - **Max speed** is `m_ziplineMoveSpeedScale × ziplineSpeed`: 600 × rope scale for shipped pilots, 800 × rope scale by code default. The rope scale default is map data and was not found [I].
    - Since the mount hands over at exactly `ziplineSpeed`, a rope with scale ≤ 1 is ridden at its cap from the first tick. Accel only matters on a rope with scale > 1, or after speed is lost.
    - **Bends:** at a segment change the old velocity is projected onto the new chord, so a bend of angle θ keeps `s·cosθ`. Accel then recovers the rest at up to 400 u/s².
    - **Slope:**
      - Ascending: no gravity term, so net +400 u/s² below the cap.
      - Descending: net `400 − 375·eg·dir.z²` u/s², which is 400 − 300·dir.z² if eg = 0.8. That is always positive, so **steady-state speed is the cap on any slope**. The vertical motion follows the sag parabola; the reported velocity stays on the chord.
- **Dismount** (top of both `fn_1806afca0` and `fn_1806b0920`; identical code) [R]. Button bits are in player+0x1e54 (pressed buttons [I]):
  - Bits `0x4000004` (bit 4 = crouch [I]): `vel = T + (vel - T) * ziplineJumpOffDampScale`, with `T = (0,0,-ziplineJumpOffSpeed)`.
  - Bit 2 (jump [I]): the same formula with `T = viewForward * ziplineJumpOffSpeed`.
  - Defaults: speed 400, damp scale 1.
  - **With the code default damp scale of 1 the velocity is unchanged on dismount.** The 400 jump-off speed only matters when a class sets the damp scale below 1. The help string ("0 for instant stop, 1 for no change") agrees.
  - Both paths then call `FUN_1806afa00`.
- **Zipline_Detach** (`logic6\fn_1806afa00`) [R]:
  - `m_lastZipline = m_activeZipline`, then `m_activeZipline = NULL`.
  - `m_useLastZiplineCooldown = useZiplineCooldown` (settings+0x3a0, default 1.0). Find skips that rope while the value is > 0; it is presumably decremented elsewhere [I].
  - Clears the valid-3p-anim flag. If state==2 it does weapon handling; otherwise, if alive, `FUN_180187ad0`.
  - Sets `m_ziplineState=0` and stamps `m_zipline+0x18` (player+0x2430) = curtime as the detach time [I].
  - Fires the script callback `DAT_180b37218`.
  - PlayerMove also calls it whenever `m_ziplineState != 0` but `FUN_1805e6af0(player)` (on-zipline test [I]) is false (`fn_180179950:106`). The ride calls it at the end of the line (see above).
- **ConVars**: only `zipline_use_range` (120.0) and `zipline_debug` (0). Everything else is per-class settings (table §0.4) times the per-rope `m_ziplineMoveSpeedScale` (rope+0xb60; its value is map/script data, not in server.dll).

---

## 3. Function reference

### fn_18060ce50: `Grapple_Shoot(GrappleData* g, vec3 start, vec3 dir)` [R body / I name]
```
g.pointCount = 1
g.vel = dir * grapple_shootVel            // 3000
g.points[0] = start
g.attached = g.pulling = g.retracting = g.forcedRetracting = 0
g.usedPower = 0; g.hasGoodVelocity = 1; g.meleeTarget = NULL
```

### fn_1805e0580: `Grapple_AimAssistToLedge(player, vec3 aimDir)` [R body / I name]
```
w = GetGrappleWeapon(); if !w return
L = w.maxLength(+0x1f3c); Lv = w.maxLengthVert(+0x1f40)
range = L / sqrt(((L/Lv)^2 - 1) * aimDir.z^2 + 1)
tr = Trace(eye, eye + aimDir*range, mask 0x46404033)
if tr.fraction >= 1:
  hint = FindTraversalHint(eye, aimDir, range, 90,90,90, automantle_height)
  for side in (+1,-1) (side starts -1 if hint.dir·aimDir >= 0):
    p = hint.origin + hint.dir * (hint.halfWidth(+0xa00) + 4) * side
    nd = normalize(p - eye)
    if nd·aimDir >= 0.95:
      tr2 = Trace(eye, eye + nd*range)
      if tr2.fraction < 1 && |p-eye| - 15 < tr2.fraction*range:
        player.m_grappleVel = nd * grapple_shootVel; return
```

### fn_18060ed50: `Grapple_RetractHook(g, ?, hook, ownerEnt, player, dt)` [R body / I name]
```
speed = (g.forcedRetracting || g.pointCount > 1) ? grapple_forcedRetractVel /*3000*/ : grapple_retractVel /*6000*/
step = dt*speed; tip = g.points[count-1]
loop:
  if count < 2:
    if |eye - tip| <= step: play Retract_1P/3P, Reset; g.pointCount = 0; return
    newTip = tip + normalize(eye - tip)*step
    if count == 1 && !forced:
      newTip.z -= dt * grapple_fallSpeed       // 300
      tr = Trace(tip, newTip, CGrappleTraceFilter, 0x4640420b)
      if !sv_forceGrapplesToFail && tr.hit:
        if hit world/non-special: if player rodeoing -> kill hook; clear forced; return
                                  if no trigger_no_grapple at hit -> Grapple_OnAttach(tr)
                                  else kill hook
        else if hitEnt flag 0x800 && hitEnt.AcceptsGrapple(): tip=hit; dispatch hit
    store tip; orient hook toward -dir; return
  // multi-point: consume whole segments
  prev = points[count-2]
  if |prev - tip| > step: move tip toward prev by step ... (same as above, no fall)
  count--; step -= |prev - tip|; tip = prev
while count > 3 || forced
```

### fn_18060d010 / fn_180170660: `Grapple_OnAttach(g, params, hook, owner, trace, hitPos, hitEnt, hitBox)` [R body / I name]
```
if !g.attached: g.attachTime = curtime
wasRetracting = g.retracting
g.attached = 1; g.retracting = 0; g.meleeTarget = NULL
impact FX = (water/solid within 100 u back along ray) ? "grapple_disallowed" : "grapple"
parent hook to hitEnt; orient; script callbacks
if !g.pulling: if wasRetracting: StartPull()          // skips grapple_pullDelay
else: if hitEnt changed: FUN_18060bf70(); sound Traverse or ReelInTarget
if IsRodeoing(player): mv.vel.z += grapple_rodeoVerticalImpulse /*750*/; EndRodeo(player)
                       (d010 only) mv.origin = player.origin
// 170660 builds params {use_rate, power_required, lowSpeedThr, lowSpeedMeleeThr,
//                       detachLengthMax, detachLengthMin, maxLength, maxLengthVert}
// and also clears player+0x18e8.
```

### fn_18060dad0: `Grapple_ServerThink(g, params, owner, hook, player, dt)` [R body / I name]
```
if hook has parent: g.points[count-1] = hook.origin
d = g.points[0] + (0,0,grapple_lift) - owner.eye
if !g.pulling: good = true
elif !player: if hook has no parent (or parent is world): KillHook; return
else:
  if curtime > g.pullTime + grapple_gracePeriod /*0.25*/:
    amt = min(params.useRate*dt, params.powerRequired - g.usedPower - 0.001)
    g.usedPower += amt; DrainSuitGrapplePower(player, amt)
  good = (vel·d > 0) && (vel·d)^2/|d|^2 > params.lowSpeedThreshold^2
if parent && !good: good = (parent.vel·d < 0) && (parent.vel·d)^2/|d|^2 > params.lowSpeedMeleeThreshold^2
melee = false
if parent && CanGrappleAutoMelee(owner, parent) && |parent.origin - myOrigin| < 300:
  g.meleeTarget = parent
  if owner.IsNPC() || (mv.buttons & 0x40000): melee = true
  elif InFront(cos 0.85) || |dXY| < hullR:
    rel = eye - parent.eye
    if |rel.z| < mv.hullH + 20:
      gap = horiz(rel along dir) - 2*hullR
      if gap < 10: melee = true
      else: closing = (vel - parent.vel)·dirXY; if closing>0 && gap/closing - grapple_autoMeleePredictTime < 0: melee = true
    // velocity convergence
    n = normalize(rel with xy shortened by 2*hullR); dist = |..|
    if (parent.vel·n - vel·n) > 0 && dist/(...) <= grapple_autoMeleeConvergeTime:
      k = dt / grapple_autoMeleeConvergeTime; blend both velocities toward each other by k; SetAbsVelocity(parent,...)
update g.hasGoodVelocity = good (stamp g.lastGoodVelocityTime = curtime on true->false)
if melee && player: player.m_queuedGrappleMeleeTime = curtime
suppress = false
if Grapple_ShouldDetach(g, params, owner, hook, player, &suppress, g.meleeTarget != NULL):
  player ? GrappleDetach(player, mv.vel, !suppress) : KillHook
elif !g.pulling && curtime - g.attachTime > grapple_pullDelay_(human|titan) /*0.2*/:
  StartPull(g, params, hook, owner, player)            // FUN_18060ccb0 (not dumped)
```

### fn_180610390: `Grapple_ShouldDetach(...)`
Pseudocode is in §1.6, and every branch there is [R]. Constants [R]: `+50.0` (length margin), `1.5` (speed-scaled min length), `16.0` and `-16.0` (window plane tests), `20.0` (window height margin), `grapple_windowCheckDist` (150), `grapple_titanEmbarkDist` (250).

### fn_18016fa80: `GameMovement_GrapplePull(this, dt)` [R body / I name]
```
if !(m_grappleActive && m_grapplePulling): skip own-grapple part
if windowHint valid && !needWindowCheck:
  if horizDist(origin, hint) >= grapple_windowCheckDist || vel·hint.normal >= 0:
     clear hint; needWindowCheck = 1; goto normal
  GrappleAccel(hook, me, hookParent, 1, &mv.vel, dirOverride=&mv.vel, FLT_MAX, dt); dir = normalize(vel)
else normal:
  GrappleAccel(hook, me, hookParent, 1, &mv.vel, NULL, FLT_MAX, dt); dir = normalize(pullVec)
for each hook h attached to me (player+0x37c list; skipped when owner is a player and FUN_180418e10(owner)==FUN_180418e10(me), probably same team [I]):
  if GrappleAccel(h, me, h.owner, 0, &mv.vel, NULL, FLT_MAX, dt): dir = normalize(dir + normalize(h.owner - me))
if !any: return 0
if onGround && !titan && groundN·dir >= -0.3 && (!wallrunning || groundN·dir >= -0.001): SetGroundEntity(NULL)
n = player+0x258c (or m_upDir if |n|max <= 0.01 && onGround)
if n·dir >= -0.5: return 1
probe = cmd_number % 4 -> {up, step=mv+0x7c*0.4} | {(n.y,-n.x,0), hullR*0.4} | {(-n.y,n.x,0), hullR*0.4} | {down, mv+0x7c*0.15}
for i in 1..2:
  p = origin + probe*step*i
  if HullTrace(p, p + dir*hullR) clear && HullTrace(origin, p) clear:
     mv.vel += probe * grapple_around_obstacle_accel * 4 * dt; return 1
return 1
```

### fn_18060b9a0: `GrappleAccel(hook, ent, other, bool entIsGrappler, vec3* vel, vec3* dirOverride, float maxVz, float dt)` [R body / I name]
```
if !FUN_1806101b0(ent, other, entIsGrappler): return false
grappler = hook.owner; titan = ent.IsTitan()
d = PullVector(ent, other, entIsGrappler)               // fn_18060c910, includes grapple_lift
if titan && d.z >= 0: d.z = 0
dist = |d|; dir = d/dist; if dirOverride: dir = normalize(*dirOverride)
a = vel·dir; perp = vel - dir*a
meleeT = other ? (entIsGrappler ? CanGrappleAutoMelee(ent,other) : CanGrappleAutoMelee(other,ent)) : false
titanReelsPilot = entIsGrappler && other.IsTitan() && !ent.IsTitan()
keep = 0
if !(meleeT || titanReelsPilot || (ent is player && windowHint valid)):
  if grapple_dontFightGravity && perp.z < 0: keep.z = perp.z; perp.z = 0
  n = ent->vfunc_0x998(); k = n·perp; if k > 0: perp -= n*k; keep += n*k
p = max(0, |perp| - grapple_decel_(human|titan)*dt)          // 425 | 200
if meleeT: p = min(p, max(0, (dist - 100) * grapple_decelMeleeStrength))
S = SpeedRampCap(grappler, ent)                               // fn_18060c750
if a < S: a = min(a + grapple_accel_(human|titan)*dt, S)      // 1000 | 1800
newVz = dir.z*a + perpDir.z*p + keep.z
vel = dir*a + perpDir*p + keep
if grapple_dontFightGravity && !meleeT: vel.z = max(oldVz, min(newVz, maxVz))
return true
```

### fn_18060c750: `SpeedRampCap(grapplerData, ent)` [R]
```
f = min(1, (curtime - grappler.m_grapplePullTime) / grapple_speedRampTime_X)    // 1.5 human, 1.0 titan
return (1 - SlowFrac(ent)) * (Min_X + (Max_X - Min_X) * f)                      // 50..800 human, 400..750 titan
```

### fn_18060c910: `PullVector(ent, other, bool entIsGrappler)` [R]
```
if entIsGrappler && (other == NULL || (!other.IsPlayer() && !other.IsNPC())):
   return ent.m_grapple.points[0] + (0,0,grapple_lift) - ent.eye      // 25
return other.eye - ent.eye                                             // reel toward/along an entity
```

### fn_18060c0c0: `Grapple_InitialImpulse(ent, other, flag, vec3* vel)` [R body / I name]
```
if !FUN_1806101b0(): return
dir = normalize(PullVector(ent, other, flag))
X = ent.IsTitan() ? titan : human
a = max(0, vel·dir)
vel.xy = (vel.xy - dir.xy*a) * initialSlowFrac_X + dir.xy*a          // human 1.0, titan 0.5
vel.z  = (vel.z  - dir.z*a)  * initialSlowFracVert_X + dir.z*a       // human 0.4, titan 0.1
a2 = vel·dir; s = 1 - SlowFrac(ent)
cap = s * speedRampMin_X                                               // human 50, titan 400
if a2 < cap:
  t = clamp(a2 + s*initialImpulse_X /*350*/, initialSpeedMin_X /*0*/, cap)   // max first, then min
  vel += dir*(t - a2)
Grapple_OffGroundImpulse(ent, vel)
```

### fn_18060c3e0: `Grapple_OffGroundImpulse(ent, vec3* vel) -> bool` [R]
```
if !GetGroundEntity(ent): return true
if ent.IsPlayer():
  s = max(0, -(m_upDir·vel)) + (titan ? initialImpulseOffGround_titan /*0*/ : initialImpulseOffGround_human /*50*/)
  vel += m_upDir * s; return impulse > 0
vel.z = max(0, vel.z) + (titan ? 0 : initialImpulseOffGround_human_npc /*150*/)
```

### fn_180170e60: `Grapple_GravityFrac(this) -> float` [R]
Formula in §1.5 item 10. Constants: `-10.0` (look-down gate gain), `1.0` caps, `grapple_letGravityHelpCosAngle` (0.8), `grapple_gravityPushUnderContribution` (2).

### fn_180171290: `Grapple_UpdateLineWrap(this)` [R body / I name]
Described in §1.5 item 13. Constants: trace fraction threshold `0.999`, cross-product magnitude `0.1`, corner radius `900.0` (30 u squared), debug line life `5.0`.

### logic6\fn_18060ccb0: `Grapple_StartPull(g, params, hook, owner, player)` [R body / I name]
```
g.pulling = 1
if hook.parent valid:                         // reeling an entity
   g.usedPower = params.powerRequired; if player: DrainSuitGrapplePower(player, params.powerRequired)
   sound ReelInTarget_1P/3P
else sound Traverse_1P/3P
g.pullTime = curtime                          // speed-ramp origin
Grapple_ApplyImpulses(owner, hook.parent, isReattach=0, player ? &mv.vel : NULL)   // fn_18060bf70
if player: if OnZipline(player) Zipline_Detach(player)
           player.m_grappleNextWindowHint = NULL; Grapple_FindWindowHint(player)   // fn_18060ca70
```

### logic6\fn_18060bf70: `Grapple_ApplyImpulses(owner, target, isReattach, vec3* ownerVel)` [R body / I name]
```
if target:
  if target is NPC: target.AI.OnGrappled(owner)
  elif target is player && team(target) != team(owner):          // FUN_180418e10 [I team]
     if target.state == 0xc: FUN_1805ce2a0(target)
     v = target.absVelocity
     if Grapple_InitialImpulse(target, owner, 0, &v): SetGroundEntity(target, NULL)
     SetAbsVelocity(target, v)
if !ownerVel: return
if !isReattach: if !Grapple_InitialImpulse(owner, target, 1, ownerVel): return
else:           if !OnGround(owner) || !CanBePulled(owner, target, 1): return
                Grapple_OffGroundImpulse(owner, ownerVel)
SetGroundEntity(owner, NULL)
```

### logic6\fn_1806101b0: `Grapple_CanBePulled(ent, other, entIsGrappler) -> bool` [R body / I name]
```
if !other || other == world: return true
if ent.IsTitan(): return !entIsGrappler && other.IsTitan()
if ent is NPC: type = ent.aiType; return type==0xd ? other.IsTitan() : type not in {2,0xb,0x10}
// ent is a pilot
if other.IsTitan(): return true
if other.IsNPC(): return (other.aiType in {2,4,11,13,16}) || !OnGround(ent)       // mask 0x12814
if entIsGrappler && other.IsPlayer() && team differs:
     return !( other.parent == NULL && OnGround(ent) )
return true
```

### fn_18060fc00: `CanGrappleAutoMelee(attacker, target) -> bool` [R]
Eligibility list is in §1.7.

### fn_18058afc0: `CPlayer::Event_Killed(info)` [I name]
The grapple-relevant branch is in §1.7: zero the victim's velocity if killed by the grapple-melee attacker within `grapple_autoMeleePredictTime + 0.4` s of that attacker's `m_grappleDetachTime` [R].

### fn_1806adaf0: `Zipline_Find(player, startPos, eye, viewDir, out rope, out start, out attach, out reverse)` [R body / I name]
Described in §2.

### fn_1806afca0: `Zipline_MountTick(player, out pos, out vel)` [R body / I name]
Described in §2. Fields used:
- `m_zipline+0xc`/`+0x10` (player+0x2424/0x2428): `m_mountingZiplineDuration` / `m_mountingZiplineAlpha` [R names, datamap]
- `+0x28..+0x30`: entry velocity
- `+0x34..+0x3c`: attach point
- Settings: `ziplineSpeed` (+0x3a4), `ziplineJumpOffSpeed` (+0x3ac), `ziplineJumpOffDampScale` (+0x3b0). Names come from the settings table [R].

### logic8\d1\fn_1805d3f60: `Grapple_Detach(player, vec3* vel, bool applyEffects)` [R body / I name]
Described in §1.6a. It gates on `m_grappleActive` and pointCount. If `applyEffects && m_grapplePulling`, it runs SpeedLoss (`FUN_1805d40d0`) and then `FUN_18016dd00`. It always kills the hook afterwards.

### logic8\d1\fn_1805d40d0: `Grapple_DetachSpeedLoss(player, vec3* vel)` [R body / I name]
`if |v| > M: v *= max(|v| − L, M)/|v|`, with L = settings+0x2a0 and M = settings+0x2a4.

### logic8\d1\fn_180187930: `ZiplineMove(this)` [R body / I name]
State dispatch 1 → af500, 2 → afca0, else → b0920. Writes origin and velocity back to mv, then runs CheckVelocity and CategorizePosition.

### logic8\d2\fn_1806af500: `Zipline_MountStart(player, out pos, out vel)` [R body / I name]
Sets the mount duration from viewmodel activity 0x136. It falls back to `mountZiplineTime` only when that activity is missing. Sets state 2.

### logic8\d2\fn_1806b0920: `Zipline_MoveUpdateSliding(player, pos, vel)` [R body; name from its debug string]
The steady-state ride (§2). It is the only reader of `ziplineAcceleration`.

### logic8\d2\fn_1806af1d0: `Zipline_AdvanceAlongSegment(...)` [R body / I name]
Chord-speed integrator plus the sag-curve position (§2).

### logic6\fn_1806afa00: `Zipline_Detach(player)` [R body / I name]
Described in §2.

### logic6\fn_1806ad790: `Zipline_CanUse(player)` [R body / I name]
Described in §2.

---

## 4. ConVar table (defaults from `convars.tsv`; help strings from the binary)

| ConVar | Default | Help / role | Used in |
|---|---|---|---|
| grapple_shootVel | 3000 | Outward speed of grapple hook | ce50, e0580 |
| grapple_retractVel | 6000 | Return speed when it hasn't hit anything yet | ed50 |
| grapple_forcedRetractVel | 3000 | Return speed when grapple is finished or cancelled | ed50 |
| grapple_fallSpeed | 300 | Fall speed of the hook while returning | ed50 |
| grapple_lift | 25 | Distance above grapple hook that player is pulled to | c910, dad0, 610390 |
| grapple_pullDelay_human / _titan | 0.2 / 0.2 | Delay between attachment and acceleration | dad0 |
| grapple_initialImpulse_human / _titan | 350 / 350 | Initial launch speed when grapple connects (capped by speedRampMin) | c0c0 |
| grapple_initialImpulseOffGround_human / _titan / _human_npc | 50 / 0 / 150 | Initial launch speed off the ground | c3e0 |
| grapple_initialSpeedMin_human / _titan | 0 / 0 | Speed set to at least this on connect | c0c0 |
| grapple_initialSlowFrac_human / _titan | 1 / 0.5 | Fraction of XY speed retained on connect | c0c0 |
| grapple_initialSlowFracVert_human / _titan | 0.4 / 0.1 | Fraction of vertical speed retained on connect | c0c0 |
| grapple_accel_human / _titan | 1000 / 1800 | Speed added per second, up to the ramp speed | b9a0 |
| grapple_decel_human / _titan | 425 / 200 | Decel of speed not toward the point | b9a0 |
| grapple_decelMeleeStrength | 1 | Perp-speed cap scale near a melee target | b9a0 |
| grapple_speedRampMin_human / _titan | 50 / 400 | Start of speed cap | c750, c0c0 |
| grapple_speedRampMax_human / _titan | 800 / 750 | End of speed cap; also scales the min-length detach | c750, 610390 |
| grapple_speedRampTime_human / _titan | 1.5 / 1.0 | Ramp time | c750 |
| grapple_dontFightGravity | 1 | Grapple never lowers vz; downward perp not decelerated | b9a0 |
| grapple_letGravityHelpCosAngle | 0.8 | Full gravity when point is steeply below | 170e60 |
| grapple_gravityPushUnderContribution | 2 | Pushing forward while looking "under" the point increases gravity this much | 170e60 |
| grapple_around_obstacle_accel | 1000 | Side/up push around obstacles (applied ×4) | 16fa80 |
| grapple_windowCheckDist | 150 | Check for window hints at this distance from point | 16fa80, 610390 |
| grapple_jumpFrac | 1.0 | Jump velocity multiplier when grappled | fn_180172870 |
| grapple_gracePeriod | 0.25 | Time grappling before power is used | dad0 |
| grapple_titanEmbarkDist | 250 | Embark own titan via grapple within this length | 610390 |
| grapple_rodeoVerticalImpulse | 750 | vz added when grapple attaches while rodeoing | d010, 170660 |
| grapple_autoMeleeOnDetach | 1 | Starts a melee sequence when the grapple detaches | fc00 |
| grapple_autoMeleePredictTime | 0.13 | Lead time for "will make contact" + kill-velocity window | dad0, 58afc0 |
| grapple_autoMeleeConvergeTime | 1.0 | Velocity convergence window | dad0 |
| grapple_autoMeleeViewRotateSpeedNear / Far | 3 / 1 | View rotation toward melee target | (not in dump) |
| grapple_autoMeleePredict | 0 | Run grapple melee logic on client | (not in dump) |
| grapple_disableMeleeWhenActive | 0 | Disallow melee while the grapple is out | fn_1805db7e0 |
| grapple_debug | 0 | Debug prints/lines | several |
| sv_forceGrapplesToFail | 0 | Force all grapples to fail on the server | ed50, 171290 |
| ai_titan_grapple_max_len | 3000 | AI titan grapple max length | (not in dump) |
| zipline_use_range | 120.0 | Zipline attach range | adaf0 |
| zipline_debug | 0 | Zipline debug draw | adaf0, afca0 |

### 4b. Player-settings keys: code defaults (from `notes\player_settings_table.md`; a class `.set` may override)

| Key (settings offset) | Code default | Role | Used in |
|---|---|---|---|
| grapple_detachLengthMin (0x284) | 33 | Always detach below this line length | 610390 via params[5] |
| grapple_detachLengthMax (0x288) | 50 | Detach below this at full speed | 610390 via params[4] |
| grapple_detachAwaySpeed (0x28c) | 300 | Detach (with boost) when moving away faster | 610390 |
| grapple_gravityFracMin (0x290) | 0.0 | Gravity fraction while grappling | 170e60 |
| grapple_gravityFracMax (0x294) | 0.75 | Gravity fraction when pulling away or pushing under | 170e60 |
| grapple_detachVerticalBoost (0x298) | 200 | vz added on pass-the-point detach | 610390 |
| grapple_detachVerticalMaxSpeed (0x29c) | 200 | Cap for that boost | 610390 |
| grapple_detachSpeedLoss (0x2a0) | 400 (shipped 300) | Speed loss on detach: `s = max(s - L, M)` when `s > M` (3D) | 1805d40d0 via 1805d3f60, 610390 |
| grapple_detachSpeedLossMin (0x2a4) | 430 (shipped 460) | Floor for that loss | 1805d40d0 |
| grapple_detachLowSpeedThreshold (0x2a8) | 250 | "Good velocity" toward point | dad0 via params[2] |
| grapple_detachLowSpeedMeleeThreshold (0x2ac) | 100 | Target approach speed | dad0 via params[3] |
| grapple_detachLowSpeedTime (0x2b0) | 1.5 | Low-speed timeout, air | 610390 |
| grapple_detachLowSpeedWallTime (0x2b4) | 1.2 | Low-speed timeout, wallrun | 610390 |
| grapple_detachLowSpeedGroundTime (0x2b8) | 0.7 | Low-speed timeout, ground | 610390 |
| grapple_detachLowSpeedMeleeTime (0x2bc) | 1.0 | Low-speed timeout with melee target | 610390 |
| grapple_impactVerticalBoost (0x2c0) | 300 | vz added on too-short detach (world hook) | 610390 |
| grapple_impactVerticalMaxSpeed (0x2c4) | 300 | Cap for that boost | 610390 |
| grapple_airSpeedMax (0x2c8) | 420 | Air-control max wish speed while pulling | fn_180161170 |
| grapple_airAccel (0x2cc) | 1000 | Air-control accel while pulling | fn_180161170 |
| grapple_power_regen_delay (0x188) | 1 | Suit grapple power regen delay | (not in my functions) |
| grapple_power_regen_rate (0x198) | 17 | Suit grapple power regen rate | (not in my functions) |
| mountZiplineTime (0x39c) | 0.5 | Mount blend duration, used only if the viewmodel lacks the mount activity | 1806af500 |
| useZiplineCooldown (0x3a0) | 1.0 | Re-use cooldown for last rope | afa00 |
| ziplineSpeed (0x3a4) | 800 (shipped 600) | Mount hand-off speed; ride cap × rope `m_ziplineMoveSpeedScale` | afca0, b0920 |
| ziplineAcceleration (0x3a8) | 400 | Ride accel (u/s², added every tick, then clamped to the cap) | b0920 → af1d0 |
| ziplineJumpOffSpeed (0x3ac) | 400 | Dismount target speed | afca0 |
| ziplineJumpOffDampScale (0x3b0) | 1 | Velocity retention on dismount (1 = unchanged) | afca0 |

Hard-coded constants [R]:

| Constant | Where |
|---|---|
| 50 u | max-length margin |
| 1.5 | min-length speed scale |
| 100 u | melee decel offset; disallowed-FX back-trace |
| 300 u | auto-melee range (90000 squared) |
| 0.85 | in-front cosine (0.7225 squared) |
| 10 u | close-enough gap |
| 20 u | height margin |
| 0.4 s | added to predictTime for kill velocity |
| -0.3 / -0.001 | leave-ground dot, ground / wallrun |
| -0.5 | obstacle trigger dot |
| ×4 | around-obstacle accel multiplier |
| 0.4 / 0.15 | probe step fractions |
| 0.95 | edge aim-assist cosine |
| 15 u | edge aim-assist slack |
| 200 u | zipline anchor trim |
| +10 u | zipline clip radius |

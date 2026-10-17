# Titanfall 2 pilot ground movement and misc movement (server.dll)

> **Errata:** some statements in this note were superseded by later analysis passes or by the extracted `.set`/weapon files. See [ASSUMPTIONS.md §12](ASSUMPTIONS.md) for the list, and [TITANFALL2_MOVEMENT.md §0](TITANFALL2_MOVEMENT.md) for shipped values. Prefer those sources where they disagree.


This is static analysis of the Ghidra output in `logic\`, `logic2\`, `logic3\`, `logic4\`, `logic5\` and `decomp\`. Function names are guesses.

Tags:
- **[R]**: read directly from code, convars.tsv, or the settings table in `player_settings_table.md`.
- **[I]**: inferred from usage, from the stock Source SDK 2013 CGameMovement layout, or from datamap names.
- **[?]**: unknown, or the code is not in the dump.

**About the numbers.** Every number is either a literal in the code, a ConVar default, or a **code default** from `notes\player_settings_table.md`. Code defaults are used only when a class `.set` file omits the field. The shipped pilot `.set` files (not available) almost certainly override the speeds. Examples: `speed` defaults to 120 and `sprintspeed` to 0, and `stepHeight` is required (no default).

Related notes (owned by other agents; not redone here):
- `jump_air_friction.md`: CheckJumpButton, AirMove, gravity, friction `FUN_1801696b0`.
- `wallrun.md`: the wallrun branch of WalkMove, TryStartWallRun.
- `slide_mantle.md`: slide, auto-mantle, the traversal move `FUN_180180780`, StepMove wrapper `FUN_18017f2c0`.
- `player_settings_table.md`: the class settings table and its code defaults.
- Grapple and zipline are covered by other agents.

---

## 0. Conventions and names used

- `this` = CGameMovement. `this+8` = player, `this+0x10` = mv (CMoveData).
- `this+0x18` = **class settings** (`FUN_180556080(classIdx@player+0x1b98)`). Field names and defaults are in table A of player_settings_table.md.
- `this+0x20` = **per-stance block**: `FUN_180556050(class, pose)` = `&DAT_181351978 + pose*0x110 + class*0x68d0` [R]. Fields are in table B.
- `this+0x28` = stance index from `FUN_1805d1230(player)`. Index 0 is standing and index 1 crouched [I]: WalkMove uses `(…,1)/(…,0)` as the crouch ratio.
- `gpGlobals` = `DAT_180bfbe08`: `+0x10` curtime, `+0x30` frametime.
- mv offsets match `jump_air_friction.md`:
  - 0x24/0x28/0x2c: normalised forward/side/up move.
  - 0x6c: wish direction.
  - 0x88: max speed.
  - 0x8c: velocity.
  - 0xb0..0xb8: step-height output.
  - 0xbc: out wish velocity.
  - 0xe4: origin.
  - 0x98..0xa0: last tick's velocity.

### Per-stance block (table B), as used by the ground code [R]

| off | name (table B) | code default | used in |
|---|---|---|---|
| +0x44 | lowSpeed | −1 | GroundAccel threshold (× speedScale) |
| +0x48 | acceleration | 2500 | GroundAccel; decel fallback (×0.6) |
| +0x4c | deceleration | −1 | ground decel |
| +0x50 | lowAcceleration | −1 | GroundAccel below lowSpeed |
| +0x54 | sprintAcceleration | −1 | GroundAccel above run speed; decel fallback above run speed |
| +0x58 | sprintDeceleration | −1 | ground decel above run speed |

### Player fields

Names marked [R] are from the datamap `decomp\fn_18055ba00.c`.

| off | name | use here |
|---|---|---|
| 0x1d18.. | **m_classModValues** (see below) | |
| **0x1dac** | **m_perPosValues**: 4 stances × {`speed`, `sprintspeed`} floats, 8-byte stride (see 2.1) | |
| 0x1e88 | m_playerFlags | `&0x10` blocks dodge |
| 0x1e90 | **m_playerMoveSpeedScale** | multiplies all move speeds (2.1) |
| 0x1f08/0x1f0c/0x1f10 | m_flTimeLastTouchedGround / LastJumped / LastLanded | |
| 0x1f40 | m_damageImpulseNoDecelEndTime | ground decel is 0 until 0.5 s before this time, then ramps back in |
| 0x2250 | m_duckState (0 stand, 1 ducking, 2 ducked, 3 unducking) [I values] | |
| 0x22c0 | m_groundFrictionScale | multiplies ground decel |
| 0x2284 | m_upDir (ground/wall normal used as "up") | |
| 0x22ec / 0x22f0 | m_traversalType / m_traversalState | |
| 0x22f4 / 0x2300 / 0x230c | m_traversalBegin / Mid / End | |
| 0x2318 / 0x231c / 0x2328 | m_traversalMidFrac / ForwardDir / RefPos | |
| 0x2334 / 0x2338 / 0x233c / 0x2340 | m_traversalProgress / StartTime / HandAppearTime / ReleaseTime | |
| 0x2344 / 0x2348 / 0x2354 / 0x2358 | m_traversalBlendOutStartTime / BlendOutStartOffset / YawDelta / YawPoseParameter | |
| 0x236c | m_prevMoveYaw | |
| 0x2588 / 0x258c | m_onSlopeTime / m_lastWallNormal | |
| 0x2598 / 0x2599 / 0x259c | m_dodgingInAir / m_dodging / m_lastDodgeTime | |
| 0x25b4 / 0x25bc | m_glideMeter / m_hovering | |
| 0x269c | m_needStuckCheck | |
| 0x27b0 | m_flTimeAllSuitDevicesOff | |
| 0x27bc / 0x27c4 | m_bPlayedSprintStartEffects / **m_fIsSprinting** | |
| 0x27cc..0x27d8 | m_sprintStartedTime / StartedFrac / EndedTime / EndedFrac | |
| 0x31c4 | m_pushAwayFromTopAcceleration | |

These fields have no datamap entry, so their names are [I] from use:
- 0x15c8 duck-toggle latch.
- 0x15cc crouch mode (1 = no crouch, 2 = forced, 3 = forced on ground).
- 0x15d0 duck timer (ms).
- 0x15dc **suit power** (0..100).
- 0x15e0 superjump power.
- 0x15e4 grapple power.
- 0x15ec fall velocity.
- 0x1600 accel/decel multiplier.
- 0x1604 power-regen multiplier.
- 0x1608 dodge power-delay multiplier.
- 0x1610 **step size**. This is presumably a copy of `stepHeight` (settings 0xa0, required) [I].
- 0x1614 step-up enabled.
- 0x1618 air-slowmo blend.
- 0x1620 power-drain bitmask.
- 0x23d0 last grapple-power use.
- 0x2370 / 0x2618 sprint-tilt velocity / value.
- 0x2780 m_surfaceFriction.

### m_classModValues (0x1d18), decoded from the parser order

The layout is decoded from the order in `decomp\fn_180553b60.c` and matches every offset the code reads. Defaults come from Table C [R].

| off | key | default | read by |
|---|---|---|---|
| 0x1d18..24 | health, healthShield, healthDoomed, healthPerSegment | –, 0, 0, 0 | |
| **0x1d28** | powerRegenRate | **10** | UpdateSuitPower |
| **0x1d2c** | dodgeDuration | **0** | UpdateDodgeState |
| **0x1d30** | dodgeSpeed | **750** | DodgeSpeed `FUN_1805d2210` [R] |
| **0x1d34** | dodgePowerDrain | **33** | dodge power gate |
| 0x1d38 | smartAmmoLockTimeModifier | 1 | |
| 0x1d3c / 40 / 44 | wallrunAccelerateVertical / Horizontal / MaxSpeedHorizontal | 450 / 1500 / 450 | wallrun, movement bounds |
| 0x1d48 / 4c | wallrun_timeLimit / hangTimeLimit | 8 / 9 | |
| 0x1d50 (b) | wallrun | (required) | |
| 0x1d54 | wallrunAdsType | wallhang | |
| 0x1d58 (b) | doubleJump | (required) | |
| 0x1d5c / 60 | pitchMaxUp / pitchMaxDown | 89 / 89 | |
| 0x1d64 / 65 / 66 / 67 / 68 (b) | mantlePitchLeveling / **dodge** / sprint / stealthSounds / hoverEnabled | 0 / **0** / 1 / 0 / 0 | |
| 0x1d6c / 70 | grapple_power_regen_delay / rate | 1 / 17 | UpdateSuitPower |
| 0x1d74..7c | slideFOVScale / LerpInTime / LerpOutTime | 1 / 0.5 / 0.5 | |
| **0x1d80** | airSlowMoSpeed | **1.0** | air slow-mo |
| 0x1d84 / 88 | sharedEnergyTotal / RegenRate | 0 / 0 | |

---

## 1. The per-tick dispatcher

### FUN_180179950: `CGameMovement::PlayerMove()` [I name, R body]

The steps below are in code order [R].

1. Cache settings: `this+0x18` (class), `this+0x20` (stance block), `this+0x28` (stance index).
2. If `FUN_1805db7b0(player)` (frozen/locked [?]) or `player.f4f1`, zero fwd/side/up move and the wish direction.
3. `FUN_180165c50`: view-punch spring decay.
4. If `player.f4f1`, force hull pose 2.
5. Build a `CTraceFilterPlayerMovement` (symbol name visible). Set `this+0x224` = collision group. Clear mv.outWishVel.
6. `FUN_18017c210`: ReduceTimers (duck timer `f15d0 -= dt*1000`, clamped at 0). Then copy mv+0x30..0x50.
7. If not ziplining and `player.f2410`, call `FUN_1806afa00` (zipline detach [I]).
8. **Animation/parented early-out.** If `FUN_1800b5ab0(player)` is true or the player has a move parent:
   - Clear the duck toggle.
   - Call EndSlide, Duck, `FUN_1801797c0` and the grapple update.
   - **If sprinting, StopSprint.**
   - Clear base velocity ("Clearing base velocity on player ... for animation/parenting").
   - Return.
9. Custom movetype transitions out of WALK(2):
   - 2 ↔ 0xe when `FUN_1805d2250()==3`.
   - 2 ↔ 0xd when `FUN_1806458b0()`.
   - 2 ↔ 0xf when `FUN_18062e730()` (melee lunge [I]).
10. **Stuck check.** It runs if `m_needStuckCheck`, the movetype is not in {0,1,9,10,13,14}, the player is not ziplining and is alive. If CheckStuck `FUN_180164b30` still reports stuck, the whole move is skipped. Otherwise the flag is cleared.
11. `FUN_180161c70`: moving-platform / base-velocity history (16-entry ring at player+0x3094) [I].
12. If movetype != 0xc:
    - Decrement `player.f2364`.
    - For WALK with `sv_optimizedmovement` (1) and `!(mv.flags&2)`: only test `dot(vel,upDir) > 200` → SetGroundEntity(NULL).
    - Otherwise, if on ground, try **StayOnGround** `FUN_18017f110`. If that fails, run **CategorizePosition** `FUN_180163830(this,0)`.
13. Water level goes to `this+0x2c`. A 100-unit overhead trace sets a flag. While airborne, `player.f15ec` = fall velocity.
14. `FUN_1805e4310` (surface bookkeeping [I]), then **Duck** `FUN_180166720`, `FUN_1801797c0` and the grapple update.
15. **Air slow-mo blend** update (3.2), then `FUN_1805d4200(player)` [?].
16. **`mv.maxspeed = MaxSpeedScale(player) * BaseMaxSpeed(player)`** (2.1).
17. **Sprint view tilt** `FUN_180184da0` (3.4).
18. **Movetype switch** [R]:

| movetype | handler |
|---|---|
| 0 | velocity = 0 |
| 1 | FullWalkMove `FUN_18016a9f0` |
| 2, 7 | If ziplining: `FUN_180187930`. Else if `CheckTraversal` `FUN_180183500` (auto-mantle `FUN_180181d20`, then window `FUN_180183ff0`): `FUN_180180780(this,1)`. **Else FullWalkMove.** |
| 4, 5 | `FUN_18016a390` (fly / fly-gravity) |
| 9 | FullNoClipMove `FUN_180169ae0(sv_noclipspeed 5, sv_noclipaccelerate 10000)` |
| 10 | FullObserverMove `FUN_180169f70` |
| 0xc | traversal move `FUN_180180780(this,0)` (mantle and window) |
| 0xd | `FUN_180645bf0(player,&origin,&vel)`, then CheckVelocity [?] |
| 0xe | `FUN_180629a70(...)`, then CheckVelocity [?] |
| 0xf | `FUN_1801750f0`: melee-lunge move (melee_lunge_* ConVars) [I] |
| other | "Bogus pmove player movetype %i on %s" |

19. The rest of PlayerMove, in order:
    - Grapple post-step `FUN_180171290`.
    - `FUN_1805e2b30` (animation [I]).
    - Impact base-velocity push-out ("Setting base velocity on player ... from impact").
    - In-air sound state, then the freefall sound (`freefall_sound_autoplay_time` 1.0 s, or a 200-unit `freefall_sound_height` down-trace that finds nothing).
    - `mv+0x98 = velocity`.

### FUN_18016a9f0: `FullWalkMove()` [I name, R body]

```c
dt  = frametime * AirSlowMoTimeScale(player);           // FUN_1805cbca0 (3.2)
UpdateDodgeState();                                     // FUN_180165f40 (3.1)
StartGravity (half step, uses dt);  if sliding on ground: slope gravity push (FUN_1805d2c80)
CheckVelocity();                                        // FUN_180164bc0
FUN_180176190();   // forced 75 u/s backward push when linked to player.f2904 [?]
CheckJumpButton();                                      // FUN_180172870 (jump / double jump / DODGE)
dt = frametime * AirSlowMoTimeScale(player);
WallRunUpdate FUN_180186d60; grappleFlag = FUN_18016fa80(dt); Tether FUN_18017f640(dt);
if (!FUN_180171dc0(dt) && player.f2584) { FUN_180224780(player, f2584, 0); f2584 = 0; }
CheckVelocity();
oldVel = vel;
if (!groundEntity) { if (wallrunning) EndWallrun; AirMove FUN_180161170(dt, grappleFlag); }
else               WalkMove FUN_180185230(dt);          // ground AND wallrun
ImpactCheck(&oldVel);    // FUN_180165040: horizontal speed lost > impactSpeed(settings 0xc8, default 580) -> strength 1.0,
                         //                > 0.5*impactSpeed -> 0.85; calls FUN_18017abe0 (impact FX)
CategorizePosition(grappleFlag);
FinishGravity(dt);
if (groundEntity) vel -= dot(vel, upDir)*upDir;
CheckVelocity();
FUN_180164950();   // landing: fallVel >= 80 (pilot) / 400 (titan) -> fall damage / land FX
FUN_1801652c0();   // "player_enter_water"/"player_leave_water" (titan_* for titans)
```

Air slow-mo scales the dt of the whole walk move [R].

---

## 2. Ground movement

### 2.1 Max speed, sprint and crouch: resolved [R]

```c
mv.maxspeed = MaxSpeedScale(player) * BaseMaxSpeed(player);              // PlayerMove

float BaseMaxSpeed(player) {                                             // FUN_1805d24d0
  return SpeedScale(player) * (m_fIsSprinting ? m_perPosValues[0].sprintspeed   // +0x1db0
                                              : m_perPosValues[0].speed);       // +0x1dac
}
float StanceSpeed(player, s)       = SpeedScale(player) * m_perPosValues[s].speed;        // FUN_1805d2b50 / FUN_1805d2bd0
float StanceSprintSpeed(player, s) = SpeedScale(player) * m_perPosValues[s].sprintspeed;  // FUN_1805d2c00 / FUN_1805d2b80
float LowSpeed(player, stanceBlk)  = SpeedScale(player) * stanceBlk.lowSpeed(+0x44);      // FUN_1805d2bb0

float SpeedScale(player) {                                               // FUN_1805d2510
  s = WeaponSpeedScale(player)                     // FUN_1805d25d0
    * player.m_playerMoveSpeedScale(0x1e90)
    * (1 - StatusEffect(player, DAT_180c91c10))    // FUN_180222070: a slow effect [I]
    * (1 + 2*StatusEffect(player, DAT_180c91564)); // FUN_1802220b0: a speed-boost effect [I]
  // a Denuvo anti-tamper block multiplies s by 0.1 once curtime > 600 if its check trips (ANTITAMPER_TRIGGER strings)
  return s;
}
float WeaponSpeedScale(player) {                                         // FUN_1805d25d0
  w = activeWeapon; if (!w) return 1;
  k = 1; if (w.f1428 in 2..6) { k = (w.f148c >= 0 && !FUN_180692aa0(w)) ? w.f148c : w.f1488;  w2 = FUN_1805d1dc0(player); } else w2 = w;
  if (w2) k *= (w2.f148c >= 0 && !FUN_180692aa0(w2)) ? w2.f148c : w2.f1488;
  return k;
}
float MaxSpeedScale(player) {                                            // FUN_18016e9e0
  if (activeWeapon && FUN_1805e6ba0(player) && !IsWallRunning(player))
     return lerp(1, weapon.f1484, ADSFraction(player));                  // FUN_1805d3bf0: weapon zoom fraction [I]
  return 1;
}
```

**m_perPosValues (0x1dac..0x1dcb)** [R layout, I stance meaning]:
- 4 stances × {`speed`, `sprintspeed`}.
- Filled from the class-file keys `speed` / `sprintspeed`. Parser `FUN_180554280`; code defaults **speed 120, sprintspeed 0**.
- Stance 0 = standing: 0x1dac speed, 0x1db0 sprintspeed.
- Stance 1 = crouched: 0x1db4 / 0x1db8.

**Sprint:**
- The max speed switches from `speed[0]` to `sprintspeed[0]` while `m_fIsSprinting` is set.
- There is no ramp here. The sprint start/end fractions only drive view and animation.
- WalkMove also divides the input by the constant **0.8** (`FUN_1805d2fa0` returns 0.8). Sprinting therefore multiplies forward and side input by 1.25 before the unit clamp, so 80% stick already gives full sprint speed.

**Crouch:**
- `mv.maxspeed` always uses stance 0.
- WalkMove caps the input magnitude at `lim = speed[1]/speed[0]` (SpeedScale cancels). The effective crouched max speed is therefore `speed[1] × scales`.
- Math consequence, not a confirmed gameplay path: if sprint were still active while crouched, the cap would give `sprintspeed[0] × speed[1]/speed[0]`.

**sv_maxspeed (320) is not the pilot ground speed** [R]:
- It is read only by noclip (`factor*sv_maxspeed`).
- The VPhysics shadow `FUN_1805dc9c0` uses `BaseMaxSpeed` (same formula: SpeedScale × sprint ? 0x1db0 : 0x1dac) and falls back to sv_maxspeed only if that is ≤ 0.

**Other stock ConVars:**
- **sv_backspeed (0.6)** has no dumped user [R for dump]. The pilot back scale is `speedScaleBack` (settings 0x9c, default **1**).
- **sv_stopspeed (100)** is used only by non-player step entities.
- **sv_accelerate** is not registered at all [R].

### 2.2 WalkMove ground branch: FUN_180185230 (not wallrunning) [R body]

```c
vel -= dot(vel, upDir)*upDir;  player.fallVel = 0;
wasStopped = |vel|^2 <= 1;
fwd,right = view basis projected into the plane of upDir;
f = mv.forwardmove;  s = mv.sidemove;                 // normalised -1..1
if (m_fIsSprinting) { f /= 0.8; s /= 0.8; }           // FUN_1805d2fa0 == 0.8
lim = 1;
if (m_duckState is 1 or 2) {                          // crouching / crouched
   a = StanceSpeed(player,1); b = StanceSpeed(player,0);
   lim = (|b| <= 0.001) ? (a==0 ? 0 : 1) : a/b;       // crouch speed / stand speed
}
m2 = f*f + s*s;
if (m2 > lim)      scale = lim/sqrt(m2);              // clamp (note: compares m^2 with lim, exact only for lim=1)
else if (m2 > 0) { m = sqrt(m2); scale = (2-m)*m*lim/m; }   // analogue ease-out curve
f *= scale; s *= scale;
s *= settings.speedScaleSide(0x98, default 1);
if (f < 0) f *= settings.speedScaleBack(0x9c, default 1);   // replaces sv_backspeed
wish = fwd*f + right*s;
if (standing on a moving entity that is not a standable titan)
    wish = 0.25*wish + 150 * normalize(player.origin - groundEnt.origin).xy;   // slide off heads/NPCs
wish -= dot(wish, upDir)*upDir;
RepelFromOtherPlayers(player, &wish);   // FUN_18017c4a0 (symbol name), +repel*20*dt
if (FUN_1801b8210()) RepelFromOtherNPCs(player, &wish); // FUN_18017c2a0, *20*dt (*100 for titans)
if (m_sliding) remove the component of wish along vel (when dot>0, or dot<0 with crouchMode==3);
wishspeed = min(|wish|,1) * mv.maxspeed;  wishdir = wish/|wish|;
accel = GroundAccel();                         // FUN_18016ea60 (2.3)
Friction(wishdir, wishspeed);                  // FUN_1801696b0 (decel from 2.3)
Accelerate(wishdir, wishspeed, accel);         // FUN_180160230
mv.outWishVel += wishdir*wishspeed;
if (wasStopped && |vel|>0) player.f21e4 = curtime + 0.4;    // [?]
if (m_sliding && |vel| < slideStopSpeed(0x26c, 125) && crouchMode != 3) EndSlide();
if (|vel| >= 1) {
   FUN_1801b8730(player, &vel);                 // [?] not dumped
   on world ground with a slope vector and not wallrunning: re-project vel onto the slope keeping |vel|;
   m_lastWallNormal = 0;
   tr = TracePlayerBBox(origin, origin + vel*dt);
   if (tr.startsolid) m_needStuckCheck = 1;
   if (tr.fraction == 1) origin = tr.endpos;
   else if (!SlopeRedirectOrStep(tr))           // FUN_18017b290 -> StepMove (2.4)
   {  m_lastWallNormal = tr.plane.normal; TryStartWallRun(...); }
   StayOnGround();                              // FUN_18017f110
} else vel = 0;
```

### 2.3 Ground acceleration, deceleration and Accelerate [R]

```c
float GroundAccel() {                                   // FUN_18016ea60
  spd2 = vel.x^2 + vel.y^2;  B = stance block;
  if (m_sliding) a = settings.slideAccel(0x264, 50);
  else if (B.sprintAcceleration >= 0 && spd2 > (StanceSpeed(player,stance) - 1)^2)
       a = B.sprintAcceleration;                        // above run speed
  else if (B.lowAcceleration >= 0 && spd2 < LowSpeed(player,B)^2)
       a = B.lowAcceleration;                           // below lowSpeed
  else a = B.acceleration;                              // default 2500 u/s^2
  return a * player.f1600;
}

float GroundDecel() {                                   // FUN_18016eda0 (called by Friction FUN_1801696b0)
  d = -1;
  if (|vel| > StanceSpeed(player,stance))               // above run speed
     d = (B.sprintDeceleration >= 0) ? B.sprintDeceleration : (B.sprintAcceleration >= 0 ? B.sprintAcceleration : -1);
  if (m_sliding) d = (crouchMode==3) ? 0
                   : (!duckHeld || dot(wishdir, vel) < 0) ? settings.slideWantToStopDecel(0x25c, 400)
                                                          : settings.slideDecel(0x258, 160);
  if (d < 0) d = (B.deceleration >= 0) ? B.deceleration : B.acceleration * 0.6;   // default 2500*0.6 = 1500
  ramp = curtime < m_damageImpulseNoDecelEndTime ? clamp((curtime - (end-0.5))*2, 0, 1) : 1;
  return ramp * d * player.f1600;
}
// Friction (FUN_1801696b0, details in jump_air_friction.md) applies it linearly:
//   newspeed = max(speed - GroundDecel() * m_surfaceFriction * m_groundFrictionScale(0x22c0) * dt, 0)

void Accelerate(wishdir, wishspeed, accel) {            // FUN_180160230
  if (dead || m_hovering) return;
  cap2 = max(|vel|^2, wishspeed^2);
  add = wishspeed - dot(vel, wishdir);  if (add <= 0) return;
  vel += min(add, accel*dt*m_surfaceFriction) * wishdir;   // no *wishspeed (stock Source has it)
  if (|vel|^2 > cap2) vel *= sqrt(cap2/|vel|^2);           // never faster than max(old speed, wishspeed)
}
```

With all code defaults, a pilot reaches its run speed at **2500 u/s²** and stops at **1500 u/s²** (linear, no stopspeed), times `player.f1600` [R defaults; .set may override].

Accelerate never raises total speed above max(previous speed, wishspeed). Steering at high speed rotates the velocity instead of adding to it.

### 2.4 Step-up / stairs

1. **SlopeRedirect `FUN_18017b290`** [R]. If the blocking plane is walkable (`dot(n, upDir) > 0.7`), the remaining `(1-fraction)*dt` of motion is re-projected onto the plane at the same speed, lifted 0.01 off it, and traced again. Otherwise it falls back to `FUN_18017f2c0`.
2. **`FUN_18017f2c0`** (slide_mantle.md). If `player.f1614` is false, it does a plain TryPlayerMove. Otherwise it calls the step core with `stepSize = player.f1610` (≈ `stepHeight`, settings 0xa0, required) and `minStep = settings.stepTestDist` (0xa4, **default 10**). Help text: "Steep slopes shorter than this will be ignored when stepping onto an object".
3. **Step core `FUN_18016cca0(ctx, origin, vel, outDelta, dt, stepSize, stepTestDist, upDir, tr)`** [R]:

```c
if (vel == 0) return;
flat = TryPlayerMove(origin, vel, dt);                     // FUN_18016bed0
up   = trace(origin, origin + (stepSize + 1/32)*upDir);
upAmt = up.startsolid ? 0 : up.fraction*(stepSize+1/32);
step = TryPlayerMove(up.endpos, origVel, dt);
if (horiz(step - origin)^2 * 0.999 <= horiz(flat - origin)^2) return flat;   // stepping must gain >0.1%
down = trace(step, step - (upAmt + stepSize + 1/32)*upDir);
if (dot(down.normal, upDir) < 0.7) {                       // landed on something steep
   d = horiz(down.end - flat);  if (|d|^2 <= 0) return flat;
   if (|d| < stepTestDist) { re-run the raised move for time stepTestDist/|vel| (vel.z >= 0), trace down again; }
   accept only if walkable, OR (normal·up > 0 AND (rise <= 0 OR horiz^2/(horiz^2+rise^2) > 0.49))   // < ~45.6°
}
reject if |landingHeight - startHeight| > max(2*|probeHeight - startHeight|, 0.1*stepSize);
outDelta = landing - start;  (if upDir != (0,0,1): projected onto the landing normal, clamped >= 0)
origin = landing; vel = step-move velocity;
```

The `step_debug` ConVar (0) draws these traces [R].

4. **StepSmoothing `FUN_18017ba30`** [R]:
   - `h = min(dot(delta, upDir), stepSize)`.
   - If h > 0, `mv.outStepHeight += h*upDir`, and the view offset `player.f2648 -= h*upDir`.
   - Also used by snap-downs larger than sqrt(2).

**StayOnGround `FUN_18017f110`** [R]:
- Trace along −upDir by stepSize and cache it at `this+0x1b0` (flag `this+0x220`).
- Snap to the hit if not wallrunning.
- CategorizePosition reuses the cached trace when the start is within 2 units.

### 2.5 CategorizePosition: FUN_180163830 [I name, R body]

- Each tick: `m_surfaceFriction = 1.0`, `m_pushAwayFromTopAcceleration = 0`. It returns early for observer modes other than 0/5, or movetype 0xc.
- Probe distance: stepSize when walking on ground or wallrunning, else 2 units, along −upDir.
- **Leave ground** if any of these hold:
  - `dot(vel, upDir) > 140` (relative to the ground entity's velocity) and not wallrunning;
  - the grapple flag is set (and not wallrunning, or `dot > 1`);
  - `m_dodgingInAir`;
  - ziplining.
- Non-standable entity below: ground = NULL and the up component is stripped. Below 100 u/s (titan 200), set `m_pushAwayFromTopAcceleration = dir*400` (titan 500).
- **Walkable = normal·up ≥ 0.7.** If not walkable:
  - try a modified trace (`FUN_18017fd40`), then ground = NULL;
  - `m_surfaceFriction = 0.25` when moving up;
  - `m_onSlopeTime` is stamped under some conditions.
- If the trace succeeds but `dot(vel + baseVel − groundVel, up) ≥ 140`, the ground is not taken.
- Wallrun hand-offs are covered in wallrun.md.
- **Snap.** If it was on ground and `0 < fraction < 1`, the origin is set to the trace end, with smoothing if the move is > sqrt(2).
- `ground_debug` (0) prints the reasons.

### 2.6 Crouch: FUN_180166720 `Duck()` [I name, R body]

- Wants duck = IN_DUCK held, or the toggle latch `f15c8`.
  - The latch toggles on button bit 0x4000000. Not while wallrunning, in traversal state 0xb, or grapple-attached.
  - The latch is cleared when IN_DUCK changes.
- Forced duck while sliding faster than `slideMaxStopSpeed` (settings 0x270, **350**) [R]. The help text is "slide may not end while above this speed".
- Crouch mode `f15cc`: 2 always, 3 on ground.
- Duck is blocked:
  - when crouch mode is 1;
  - when ziplining;
  - when `FUN_1805d6150`;
  - when wallrunning with mode 0;
  - when `settings.crouch` (0x83, **default 1**) is 0.
- **Timings** [R, literal]:
  - Stand → ducking: **400 ms (200 ms while sliding)**.
  - Ducked → unducking: **200 ms**.
  - Reversals mid-transition rescale the timer by the current fraction.
- On the timer hitting 0: FinishDuck `FUN_1801686f0` (state 2) or FinishUnDuck `FUN_180168b10` (state 0).
- Unducking requires CanUnduck `FUN_1801632b0`.
- Sounds: settings+0xd8c when starting to duck; settings+0xdcc when fully standing, which also calls EndSlide.
- The effect on speed is the `speed[1]/speed[0]` cap in 2.2.
- `FUN_180166de0` = ForceDuck: shift by the hull difference, FinishDuck, re-categorize. Window traversal uses it.

### 2.7 CheckVelocity: FUN_180164bc0 [R]

- Runs only if velocity changed by more than 0.001.
- NaN components are zeroed.
- Origin is clamped to ±65536 ("Got outside the map").
- Velocity over `sv_maxvelocity` (12100) on any axis is scaled **uniformly** on all three axes.

---

## 3. Misc movement

### 3.1 Dodge (dodge branch of CheckJumpButton FUN_180172870) [R]

**Trigger.** Either of these:
- the dodge button is pressed (bit 0x10000000 in mv.pressed) and jump was not pressed;
- jump is pressed in the air with no ground/coyote jump available and no doubleJump.

**Requirements:**

```c
m_duckState == 0 && !(m_playerFlags & 0x10) && classMods.dodge(0x1d65, default 0)
&& curtime >= m_lastDodgeTime + settings.dodgeInterval(0x23c, 0.6)
&& suitPower(0x15dc) >= classMods.dodgePowerDrain(0x1d34, 33)      // else fail sound settings+0xb4c
&& (dodge_no_movement_forward_enable || |wishdir|^2 > 1e-7)
```

**Execution:**

```c
FUN_1805e2170(player, classMods.dodgePowerDrain(0x1d34));   // DrainPower [R]: XMM1 = XMM3 = [player+0x1d34] (0x180172b93 / 0x180172be1)
FUN_1805e2150(player, settings.dodgePowerDelay(0x250, 0) * player.f1608);   // DelayPowerRegen [R]; see 3.5
SetGroundEntity(NULL);
vertical = dodge_vertical_enable(0) && |wishdir| < dodge_vertical_threshold(0.1);
h = vertical ? settings.dodgeVerticalHeight(0x24c, 200) : settings.dodgeHeight(0x244, 25);
TetherDodge(this, wishdir);                        // FUN_18017f9c0: tether_dodge_damage(0)
vUp = sqrt(2 * sv_gravity * h) * jumpFactor;       // dodgeHeight 25 -> sqrt(2*750*25) ~ 193.6 u/s (code defaults)
if (dodge_vertical_in_air(1) || onGround/coyote) vel += upDir * vUp;
if (dot(wishdir, m_lastWallNormal) < 0) vel -= m_lastWallNormal*30;
dir = vertical ? 0 : normalize(wishdir.xy)  (no input + dodge_no_movement_forward_enable -> view forward);
D   = DodgeSpeed(player) = classMods.dodgeSpeed(0x1d30, 750) * (1 - FUN_180221ff0(player));   // FUN_1805d2210 [R]
keep = (|D| < |vel.xy|) ? 1 - |D|/|vel.xy| : 0;
if (vertical) keep *= dodge_vertical_horzspeedscale(0.5);
keep += (1 - keep) * settings.dodgeKeepSpeedFrac(0x254, 0);
vel.xy = keep*vel.xy + D*dir;
m_lastDodgeTime = curtime; m_dodgingInAir = 1; m_dodging = 1;
```

The common tail follows: EndSlide, FinishGravity, CheckVelocity, `outStepHeight += 0.15`, `m_flTimeLastJumped = curtime`.

- Below D, the dodge sets horizontal speed to exactly D (750 by code default) in the dodge direction.
- Above D, total speed is preserved and redirected.
- `dodgeHeightMax` (0x248, 25) is not read here [?].

**UpdateDodgeState `FUN_180165f40`** [R]:
- `m_dodgingInAir` clears after `classMods.dodgeDuration` (**default 0**, so by default it clears on the next tick).
- Both flags clear if `|vel| < dodgeStopSpeed` (settings 0x240, **300**).
- `m_dodging` clears after `dodgeAnimDuration` (0x238, **0.5 s**).
- While `m_dodgingInAir`, CategorizePosition refuses ground.

`dodge_viewTilt*` and `dodge_cockpit*` are not referenced by dumped server code [?].

**Misattribution in the index.** `logic2\fn_180165850` is Source's **CreateStuckTable**, not dodge code. The ConVar dumper attributed `dodge_vertical_in_air` to it only because that ConVar object sits right before the table [R].

### 3.2 Air slow-mo and ADS hover [R]

```c
float AirSlowMoTimeScale(player) {                         // FUN_1805cbca0, FullWalkMove dt *= this
  if (airslowmo_ground_immediate_end(0) && onGround) return 1.0;
  return lerp(1.0, classMods.airSlowMoSpeed(0x1d80, default 1.0), player.f1618);
}
// PlayerMove:
if (airslowmo_enabled(0) && airSlowMoSpeed != 1.0) target = onGround ? 0 : ADSFraction(player);  // FUN_1805d3bf0 [I ADS]
else target = (airslowmo_when_hovering(1) && m_hovering && !onGround) ? 1.0 : 0;
f1618 ramps up at 1/airslowmo_enter_time(0.25) per s, down at 1/airslowmo_leave_time(1.0) per s;
```

**With code defaults (`airSlowMoSpeed` 1.0), the scale is 1, so slow-mo has no effect.** A class must set airSlowMoSpeed < 1. With `airslowmo_enabled` off, only hovering drives the blend.

**Hover `FUN_1805cc6d0`** is **ADS hover**: the table A help text for `hoverEnabled` (settings 0x1d8, default 0) is "Enables ADS hover".
- Start conditions:
  - `settings.hoverEnabled` or `classMods.hoverEnabled`;
  - the ADS/zoom button `m_nButtons & 0x10000` [I] (or `flags & 8`);
  - `curtime - max(lastTouchedGround, lastJumped) >= hoverSafety` (0x1ec, **0.25**);
  - walking, not ziplining;
  - `m_glideMeter > 0`.
- While hovering with `airslowmo_when_hovering` on, no damping is applied; slow-mo is meant to handle it.
- Otherwise it cancels gravity and damps velocity by `exp(-ln2/tightness * dt)`: horizontal `hoverTightnessHorizontal` (0x1e0, **0.08**), vertical `hoverTightnessVertical` (0x1dc, **0.15**). This acts as a half-life in seconds.

### 3.3 Window traversal [R]

**CheckTraversal `FUN_180183500`** runs in movetype 2/7 when not ziplining:

```c
traversal_enable(1) && !isTitan && movetype != 0xc
&& curtime >= m_traversalReleaseTime + traversal_cooldown(0.5)  ->  CheckAutoMantle() || TryWindowTraversal()
```

**TryWindowTraversal `FUN_180183ff0`**:

```c
if (!traversal_window_enable(1)) return 0;
if (!wallrunning) { if (m_lastWallNormal == 0) return 0; if (onGround && !(buttons & IN_JUMP)) return 0; }
if (mv.forwardmove < 0.3) return 0;
win = FUN_1806079b0(...);   // window hint: normal +0x9e8, side +0x9f4, halfWidth +0xa00, halfHeight +0xa04
if (wallrunning) { dot(win.normal, wallNormal) >= 0.8;
                   if (dot(win.normal, viewFwd) > -0.25) bottom <= origin.z+16 && top >= origin.z+hullTop+10 && FUN_180221c20 <= 0; }
require bottom <= headZ <= top;  dist to window plane <= radius + 6;  window ahead;
require |lateral| >= halfWidth - radius;   sign = side of the opening the player is on;
fitsStanding = standHeight <= (halfHeight - 2.01)*2;   (else duck hull, ForceDuck)
A = edge - side*sign*(radius+2.01) + normal*depth;
B = edge - side*sign*(traversal_window_sideways_offset(18) + radius) - normal*traversal_window_forward_offset(6);
hull traces origin->A and A->B must be clear;
FindHandPlacement(...);   // hand at traversal_window_hand_vertical_offset (22)
BeginTraversal(type=1, ..., begin=origin, mid=A, end=B, midFrac=0.5);
m_traversalState = side (1/2);  m_traversalYawDelta = clamp(..., -90, 90);  yaw pose = clamp(sign*delta, 0, 90);
```

**BeginTraversal `FUN_1801625e0`**:
- Sets the "ledgeAngle" pose parameter and starts the anim (`traversal_anim` 1).
- Resets the traversal fields and converts the begin/mid/end/refpos points into the traversed entity's space (refpos snapped to 1/32).
- Clears m_dodgingInAir, m_dodging and m_sliding, and ends the wallrun.
- Parents the player, zeroes velocity, and sets **movetype 0xc**.

**Duration.** Window progress advances at `1/traversal_window_duration` per second, so **0.3 s** (in `FUN_180180780`).

**FindHandPlacement `FUN_1801670f0`**:
- Two edge probes at ±(radius−0.1).
- Requires `traversal_hand_required_width` (6) units of surface.
- Bisects to a 0.05 tolerance.
- Rejects results more than 80 units away.

`traversal_window_finish_angle` (45) and `traversal_hand_debug` are not read in the dump [?].

### 3.4 Sprint view tilt [R]

```c
float SprintTiltTarget() {                           // FUN_18016ef70; on ground, not wallrunning
  run = StanceSpeed(player,stance); sprint = StanceSprintSpeed(player,stance);     // now resolved [R]
  if (|vel| < 1.05*run) return 0;
  turn = clamp((AngleDiff(viewYaw, m_prevMoveYaw)/dt) / sprinttilt_turnrange(120), -1, 1);  m_prevMoveYaw = viewYaw;
  return clamp((|vel| - run)/(sprint - run), 0, 1) * turn;
}
// FUN_180184da0 (every PlayerMove tick, skipped while curtime == 0):
err = target - tilt;
vDes = clamp(sign(err)*sqrt(2*sprinttilt_accel(35)*max(|err|-|v*dt|,0)), ±sprinttilt_maxvel(2));
v += clamp(vDes - v, ±35*dt);   tilt += 0.5*(vOld+v)*dt;   snap when crossed and |v|<0.1;
tilt = clamp(tilt,-1,1) -> pose parameter player.f2374
```

The roll in degrees is client side. Table A has `sprinttiltMaxRoll` (0xf8, **10°**), "Degrees of view tilt when turning while sprinting" [R name; client use I].

### 3.5 Sprint power drain and suit power [R]

- **StartSprint `FUN_1805e1590`**:
  - Allowed if already started, or `suitPower >= 10`.
  - If `sprint_powerdrain != 0`, set the sprint drain bit.
  - Store the quantised sprint fraction and time, `m_fIsSprinting = 1`, and the start-effects flag.
- **StopSprint `FUN_1805e1ad0`**:
  - If the bit is set: drain `0.1*rate`, clear the bit, and if no devices remain draining set `m_flTimeAllSuitDevicesOff = curtime + 0.5`.
  - Store the end fraction and time, `m_fIsSprinting = 0`, then notify the weapon.
- **UpdateSuitPower `FUN_1805e23e0`**:
  - Superjump power gets `+dt*0` (no regen).
  - Grapple power gets `+grapple_power_regen_rate(17)*dt` once `curtime > lastUse + grapple_power_regen_delay(1)`, capped at 100.
  - Suit power, with no drain bits: `+= powerRegenRate(10) * (1 - FUN_180221fb0) * f1604 * dt`, while < 100 and past `m_flTimeAllSuitDevicesOff`.
  - Suit power, with drain bits: sum the per-device rates (table `PTR_DAT_180b94a40`) and DrainPower. On failure, StopSprint.
- **Default `sprint_powerdrain` = 0, so sprint is free.**
- A dodge needs `power >= dodgePowerDrain` (33) and charges exactly that [R, logic8]. With code defaults, power refills at 10/s, so 3.3 s regains one dodge's worth of power [R].

**The two power helpers (resolved in logic8; `logic8\d1\fn_1805e2150.c`, `fn_1805e2170.c`, disassembly in `logic8\dis1.txt`) [R]:**

```c
// FUN_1805e2170: DrainSuitPower(player, amount) -> bool
power = player.m_suitPower(0x15dc) - amount;      // networked write
if (power < 0) { power = 0; return false; }       // overdraw: clamps to 0, still charged
return true;

// FUN_1805e2150: DelayPowerRegen(player, delay)
t = curtime + delay;
if (t > player.m_flTimeAllSuitDevicesOff(0x27b0)) player.m_flTimeAllSuitDevicesOff = t;   // only ever extends
```

- **DrainPower does not check first.** It always subtracts, clamps at 0 and reports whether the player could afford it. Callers that need a gate test `power >= cost` beforehand (dodge does).
- **Regen gate:** `m_flTimeAllSuitDevicesOff` (0x27b0) is the regen gate in UpdateSuitPower. Regen runs only while `curtime >= 0x27b0`, no drain bits are set, and power < 100.
  - DelayPowerRegen pushes that time out with `max`.
  - StopSprint and StopDevice *overwrite* it with `curtime + 0.5`.
- **Suit "devices"** [R, from `.data` bytes]:
  - `PTR_DAT_180b94a40` is a NULL-terminated list of pointers to `{uint mask; float defaultRate; ConVar* rate}`.
  - It holds exactly **one** entry, 0x180b94c60 = `{mask 1, defaultRate 0.0, ConVar 0x1814e8020 = sprint_powerdrain}`, and the next slot is NULL. **Sprint is the only continuous suit-power drain.**
- **Sprint drain, step by step:**
  1. StartSprint (`FUN_1805e1590`) needs power ≥ 10 unless the start flag (0x27bc) is already set. It sets mask bit 1 only when `sprint_powerdrain != 0`.
  2. Each UpdateSuitPower tick (`FUN_1805e23e0`) adds up the rates of the set bits (`sprint_powerdrain`) and calls `DrainPower(rate·dt)`. If that returns false (power hit 0) while `m_fIsSprinting` (0x27c4) is set, it calls StopSprint.
  3. StopSprint (`FUN_1805e1ad0`) charges a further one-off `DrainPower(0.1·sprint_powerdrain)` and clears the bit. If no bits remain, it sets `0x27b0 = curtime + 0.5`, so regen restarts 0.5 s after sprint ends.
  - `FUN_1805e22f0(player, deviceEntry)` is the same stop logic for any device entry, and returns whether the bit was set.
- **With the shipped and default `sprint_powerdrain` = 0** the bit is never set. Sprint costs nothing and never delays regen; it only needs power ≥ 10 to start [R].
- **Other callers:** `FUN_18062b3f0` (context unidentified [I]) has two power paths.
  - A dodge path with the same gates (classMods.dodge 0x1d65, `dodgeInterval`, power ≥ dodgePowerDrain). It calls `DrainPower(dodgePowerDrain)` (XMM1 = [player+0x1d34] at 0x18062ba00) and `DelayPowerRegen(dodgePowerDelay × f1608)`.
  - A continuous path, taken when player+0x24d0 is set. While power ≥ `dodgePowerDrain·dt` it drains `dodgePowerDrain·dt` every tick and calls `DelayPowerRegen`; otherwise it plays the settings+0xa8c fail sound.

### 3.6 Movement bounds: FUN_18017e580 [R body, I purpose]

- With `player_useMovementBounds` (1), it builds an AABB: hull bounds around the origin, padded by `max(BaseMaxSpeed or wallrunMaxSpeedHorizontal, |absVel|) * T + 1`, plus stepSize vertically.
- It hands the box to the collision system, as a collision prefetch/cache. It is not a gameplay limit.
- The caller is not dumped.
- `player_movementBounds_predictionShare` is not read [?].

---

## 4. Function list with guessed names

| addr | guessed name | tag |
|---|---|---|
| 180179950 | PlayerMove (dispatcher, air slow-mo blend) | I / R |
| 18016a9f0 | FullWalkMove | I / R |
| 180185230 | WalkMove (ground + wallrun) | I / R |
| 180163830 | CategorizePosition | I / R |
| 18017f110 | StayOnGround | I / R |
| 18016b890 | SetGroundEntity | I |
| 18016ea60 | GroundAccel (table B accel / lowAccel / sprintAccel, slideAccel) | R |
| 18016eda0 | GroundDecel (table B decel / sprintDecel, slide decels, damage-impulse ramp) | R |
| 180160230 | Accelerate (capped, absolute) | I / R |
| 1801696b0 | Friction (linear decel), see jump_air_friction.md | I |
| **1805d24d0** | **BaseMaxSpeed** (sprint ? perPos[0].sprintspeed : perPos[0].speed) × SpeedScale | R |
| 1805d2510 | SpeedScale (weapon × m_playerMoveSpeedScale × status effects; anti-tamper ×0.1) | R |
| 1805d25d0 | WeaponSpeedScale | R |
| 1805d2b50 / 1805d2bd0 | StanceSpeed(s) | R |
| 1805d2c00 / 1805d2b80 | StanceSprintSpeed(s) | R |
| 1805d2bb0 | LowSpeed(stance block) | R |
| 1805d2fa0 | SprintInputDivisor, returns 0.8 | R |
| 1805d2210 | DodgeSpeed = dodgeSpeed × (1 − effect) | R |
| 1805d3bf0 | ADS/zoom fraction (slow-mo target, ADS speed blend) | I |
| 18016e9e0 | MaxSpeedScale (ADS speed blend) | I / R |
| 180554280 | parse `speed` / `sprintspeed` (defaults 120 / 0) | R |
| 180556050 | GetStanceSettings(class, stance) | R |
| 18016bed0 | TryPlayerMove | I |
| 18017b290 | SlopeRedirectOrStep | I / R |
| 18016cca0 | StepMove core | I / R |
| 18017ba30 | StepSmoothing | I / R |
| 180164bc0 | CheckVelocity | I / R |
| 180165850 | CreateStuckTable (not dodge) | R |
| 18017c210 | ReduceTimers | I / R |
| 180166720 / 180166de0 / 1801632b0 | Duck / ForceDuck / CanUnduck | I / R |
| 18017c4a0 / 18017c2a0 | RepelFromOtherPlayers / RepelFromOtherNPCs (symbol names) | R |
| 180165f40 | UpdateDodgeState | I / R |
| 180172870 | CheckJumpButton (incl. dodge) | I / R |
| 18017f9c0 | TetherDodge | I / R |
| 1805cbca0 | AirSlowMoTimeScale | I / R |
| 1805cc6d0 | ADS Hover | I / R |
| 180183500 / 180183ff0 | CheckTraversal / TryWindowTraversal | I / R |
| 1801670f0 / 1801625e0 | FindHandPlacement / BeginTraversal | I / R |
| 18016ef70 / 180184da0 | SprintTiltTarget / UpdateSprintTilt | I / R |
| 1805e1590 / 1805e1ad0 / 1805e23e0 | StartSprint / StopSprint / UpdateSuitPower | I / R |
| 18017e580 | UpdateMovementBounds | I / R |
| 1805dc9c0 | UpdateVPhysicsShadow | I / R |
| 180169ae0 / 180169f70 | FullNoClipMove / FullObserverMove | I / R |
| 180187930 / 1801750f0 / 18016a390 | ZiplineMove (state dispatch; see grapple_zipline.md §2) / melee-lunge / fly move | I name / R body for 187930 (logic8) |
| 180165040 | ImpactCheck (impactSpeed 580) | I / R |
| 180164950 / 1801652c0 | Landing check / water events | I / R |

Still not dumped:

| addr | needed for |
|---|---|
| 180221c50 | status-effect lookup behind the speed scale |
| 180221ff0 / 180221fb0 | dodge-speed and regen penalties |
| 1805d4200 | apply slow-mo |
| 1801b8730 | post-move velocity hook |

The base-speed copy into m_perPosValues is also not traced. Its parser is `FUN_180554280`, which has no direct caller.

---

## 5. ConVar table (defaults from convars.tsv)

| ConVar | default | used where | status |
|---|---|---|---|
| sv_maxspeed | 320 | noclip, VPhysics shadow fallback only | R |
| sv_backspeed | 0.6 | **no dumped user** (pilot uses speedScaleBack) | R (dump) |
| sv_stopspeed | 100 | non-player step entities only | R |
| sv_gravity | 750 | gravity, jump/dodge hop `sqrt(2gh)`, hover | R |
| sv_friction | 4 | noclip/spectator friction | R |
| sv_maxvelocity | 12100 | CheckVelocity | R |
| sv_optimizedmovement | 1 | PlayerMove fast categorize path | R |
| sv_noclipspeed / sv_noclipaccelerate | 5 / 10000 | noclip | R |
| sv_turbophysics_player | 1 | VPhysics shadow | R |
| ground_debug / step_debug | 0 / 0 | debug output | R |
| player_useMovementBounds | 1 | movement bounds prefetch | R |
| player_movementBounds_predictionShare | 1 | not read in dump | ? |
| dodge_no_movement_forward_enable | 1 | dodge forward with no input | R |
| dodge_vertical_enable / threshold / horzspeedscale | 0 / 0.1 / 0.5 | vertical dodge | R |
| dodge_vertical_in_air | 1 | air dodge gets an upward hop | R |
| dodge_viewTiltMax / IncreaseSpeed / DecreaseSpeed / FalloffTime | 10 / 5 / 2.5 / .7 | not in dump | ? |
| dodge_cockpitHack / OffsetMax / TiltMax | 0 / 3 / 4 | not in dump | ? |
| tether_dodge_damage | 0 | TetherDodge | R |
| airslowmo_enabled / enter_time / leave_time | 0 / 0.25 / 1.0 | slow-mo blend | R |
| airslowmo_ground_immediate_end / when_hovering | 0 / 1 | slow-mo | R |
| traversal_enable / cooldown / anim | 1 / 0.5 / 1 | CheckTraversal / BeginTraversal | R |
| traversal_window_enable / duration | 1 / 0.3 | window traversal | R |
| traversal_window_forward_offset / sideways_offset / hand_vertical_offset | 6 / 18 / 22 | window geometry | R |
| traversal_window_finish_angle | 45 | not in dump | ? |
| traversal_hand_required_width / hand_debug | 6 / 0 | hand placement / not read | R / ? |
| traversal_viewLerpInDuration | 0.15 | fn_1806047b0 (not analysed) | R (index) |
| sprinttilt_accel / maxvel / turnrange | 35 / 2 / 120 | sprint tilt spring | R |
| sprint_powerdrain | 0 | sprint drain per s (0 = free) | R |
| freefall_sound_autoplay_time / height | 1.0 / 200.0 | freefall sound | R |
| wallrun_angleChangeMinCos | 0.8 | Categorize wall-to-wall | R |

### Class-setting code defaults used by this code

Source: player_settings_table.md, tables A/B/C. These are code defaults that .set files can override.

| setting | where | default |
|---|---|---|
| `speed` / `sprintspeed` (m_perPosValues) | BaseMaxSpeed, StanceSpeed | 120 / 0 |
| acceleration / lowAcceleration / sprintAcceleration (stance +0x48/50/54) | GroundAccel | 2500 / −1 / −1 |
| deceleration / sprintDeceleration / lowSpeed (stance +0x4c/58/44) | GroundDecel / GroundAccel | −1 / −1 / −1 |
| speedScaleSide / speedScaleBack (0x98/0x9c) | WalkMove | 1 / 1 |
| stepHeight / stepTestDist (0xa0/0xa4) | StepMove | required / 10 |
| crouch (0x83) | Duck | 1 |
| impactSpeed (0xc8) | ImpactCheck | 580 |
| dodgeAnimDuration / dodgeInterval / dodgeStopSpeed (0x238/23c/240) | dodge | 0.5 / 0.6 / 300 |
| dodgeHeight / dodgeHeightMax / dodgeVerticalHeight (0x244/248/24c) | dodge | 25 / 25 / 200 |
| dodgePowerDelay / dodgeKeepSpeedFrac (0x250/254) | dodge | 0 / 0 |
| dodge / dodgeSpeed / dodgePowerDrain / dodgeDuration (classMods) | dodge | 0 / 750 / 33 / 0 |
| powerRegenRate (classMods) | suit power | 10 |
| slideDecel / slideWantToStopDecel / slideAccel (0x258/25c/264) | decel / accel | 160 / 400 / 50 |
| slideStopSpeed / slideMaxStopSpeed (0x26c/270) | EndSlide / forced duck | 125 / 350 |
| hoverEnabled / hoverTightnessVertical / hoverTightnessHorizontal / hoverSafety (0x1d8/1dc/1e0/1ec) | ADS hover | 0 / 0.15 / 0.08 / 0.25 |
| airSlowMoSpeed (classMods) | slow-mo | 1.0 |
| sprinttiltMaxRoll (0xf8) | client view roll | 10 |

### Hard-coded constants (all [R])

| value | meaning |
|---|---|
| 140 | leave-ground up-speed |
| 0.7 | walkable normal |
| 2 | air probe distance |
| 1/32 | step clearance |
| 0.999 | step must gain horizontal distance |
| 0.49 | slope ratio, ~45.6° |
| 0.1·stepSize | min height tolerance |
| 0.8 | sprint input divisor |
| 0.6 | decel fallback = 0.6 × accel |
| 400 / 200 ms | duck / slide-duck time |
| 200 ms | unduck time |
| 0.25 / 150 u/s | head-slide blend |
| 100 / 400 (titan 200 / 500) | push-off-top speed / accel |
| 10 | min power to start sprint |
| 0.5 s | regen delay after devices off |
| 1.05 | tilt starts above this × run speed |
| 30 u/s | wall nudge on jump/dodge |
| 80 / 400 | landing check fall speed, pilot / titan |
| 0.85 | impact strength at 0.5 × impactSpeed |
| 65536 | map bound |

---

## 6. Confidence summary

**Read directly [R]:**
- The dispatcher and FullWalkMove order.
- **Max speed** = MaxSpeedScale × SpeedScale × (sprinting ? `sprintspeed[0]` : `speed[0]`).
- **Crouch** is the `speed[1]/speed[0]` input cap.
- The sprint input ×1.25.
- Ground accel and decel selection against table B fields.
- Accelerate math, StepMove, CategorizePosition and Duck timers.
- The dodge formula with named settings.
- Air slow-mo and ADS hover.
- Window traversal, sprint tilt, suit power and movement bounds.
- All ConVar and code defaults quoted.

**Inferred [I]:**
- Function names.
- That stance index 0/1 means stand/crouch.
- That player.f1610 copies stepHeight.
- That `FUN_1805d3bf0` is the ADS fraction.
- The status-effect meaning of SpeedScale terms.
- Custom movetypes 0xd/0xe.
- The purpose of the movement-bounds prefetch.

**Unknown [?]:**
- The shipped pilot values (`.set`), above all `speed`/`sprintspeed` per stance and `stepHeight`.
- Status-effect lookups. (The power-drain bodies were resolved in logic8; see 3.5.)
- The sprint input logic that calls StartSprint.
- Client-side dodge/sprint view tilt.

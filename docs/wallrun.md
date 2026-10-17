# Titanfall 2 pilot WALLRUN / WALLHANG (server.dll, static analysis)

> **Errata:** some statements in this note were superseded by later analysis passes or by the extracted `.set`/weapon files. See [ASSUMPTIONS.md §12](ASSUMPTIONS.md) for the list, and [TITANFALL2_MOVEMENT.md §0](TITANFALL2_MOVEMENT.md) for shipped values. Prefer those sources where they disagree.


Source: Ghidra output in `logic\fn_*.c`, `convars.tsv`, `decomp\fn_180553b60.c`, plus a few direct reads of
server.dll (`.rdata` settings tables and the bytes of small helper functions).

Confidence tags used throughout:
- **[R]**: read directly from the decompiled code (Ghidra output, constants from the `f=` annotation), or directly from server.dll bytes (the player-settings field table, which has names, struct offsets,
  default strings and help text, or the instruction bytes of a small helper).
- **[I]**: inferred (naming, meaning of an unnamed field or callee, or design intent).

Notation: `mv` is CMoveData (`param_1+0x10`), `pl` is the player (`param_1+8`), `set` is the player-class
settings block (`param_1+0x18`), `gp` is gpGlobals (`DAT_180bfbe08`: `gp+0x10` = curtime, `gp+0x30` = frametime) [I].
`n` is the wall normal at `pl+0x2284`.

---

## 1. Overview

**Start.** An airborne pilot starts a wallrun when a movement trace hits a surface. The hit can come from
AirMove, from WalkMove/WallRunMove, or from CategorizePosition sliding onto a steep surface. A per-tick retry also
runs `wallrun_retry_interval` after a near-miss. TryStartWallRun (`1801835c0`) calls the validator CanWallRun
(`180174890`), which requires all of the following:
- `wallrun_enable` and the class's `wallrun` setting are on.
- The player is not dodging.
- At least 0.5 s have passed since the player was last on the ground. A jump zeroes that timestamp.
- The surface normal's z is in [-0.35, 0.7].
- The approach angle is within `wallrun_minAngle_air`.
- The same-wall rules pass.
- Two probes (at +16 and at mid-hull) find the wall within `wallrun_allowed_wall_dist` (13 units).

On success the wall normal is stored and `m_wallRunCount` increments. Upward speed along the wall is raised to
`wallrun_upWallBoost` (250), scaled by a "strength" that is 0 for a "weak" wallrun. A wallrun is weak after you fall
off a wall or walk off the ground without jumping, or when you re-run the same wall. Sounds and script callbacks fire.

**Per tick.** WallRunMove (`180185230`, the wallrun branch of the generic surface WalkMove) does the following:
- Removes the into-wall velocity.
- Applies friction `wallrunFriction × frictionScale`.
- Accelerates separately in vertical (`wallrunAccelerateVertical`, capped at `wallrunMaxSpeedVertical`) and
  horizontal (`wallrunAccelerateHorizontal`, capped at `wallrunMaxSpeedHorizontal` or `...Backward`) directions.
- Converts some forward input into upward push (`wallrun_upwardAutoPush`).

Gravity during a wallrun ramps from 0 to full over `wallrun_gravityRampUpTime` (1 s), or is full at once if the run
is weak. Corners and curved walls rotate the stored normal at up to `wallrun_rotateMaxRate` rad/s.

**Slip.** After `wallrun_slipstarttime` (1.5 s) a slip fraction ramps to 1 over `wallrun_slipduration` (1 s). With no
input it is at least `wallrun_noInputSlipFrac` (0.7). Slip cuts vertical acceleration by (1 − slip) and stops friction
from removing downward speed.

**End.** WallRunThink (`180186d60`) ends the wallrun on any of these:
- Crouch or another button bit (mask 0x4000004).
- Holding the stick away from the wall for longer than `wallrun_pushAwayFallOffTime` (0.05 s).
- Exceeding `wallrun_timeLimit` (from the .set file).
- Losing the wall. This is probed every 4th tick at 13 units.
- Leaving a wallhang (see Wallhang below).
- Landing on a floor normal (z ≥ 0.7). This end is detected in TryStartWallRun and CategorizePosition.

On end, the player is pushed away at `wallrun_fallAwaySpeed` (70) along n if the position is clear. The wall is
recorded for repel and same-wall checks, the next run is marked weak, and a fall sound plays. Near the top of a wall,
upward speed decays at 3 × `wallrun_avoid_wall_top_decel`.

**Jump-off.** WallRunJump (`180173a00`) adds:
- input direction × `wallrunJumpInputDirSpeed` (100),
- an outward component of at least `wallrunJumpOutwardSpeed` (250), reduced by up to 80% when you look and push
  into the wall,
- vertical speed raised toward `wallrunJumpUpSpeed` (320).

It also clears the weak flag.

**Wallhang.** If the class's `wallrunAdsType` is "wallhang", pressing ADS during a wallrun enters a hang. A factor
ramps from 1 to 0 over `wallrun_hangStopTime` (0.5 s), freezing speed and gravity. After
`wallrun_hangslipstarttime` (3 s) the factor ramps back to 1 over `wallrun_hangslipduration` (1 s), so the player
slides. The hang ends when ADS is released, when `wallrun_hangTimeLimit` (from the .set file) is reached, or when no
wall is found within `wallrun_allowed_wall_dist_wallhang` (18).

---

## 2. Functions

### fn_180174890: `CGameMovement::CanWallRun(hitPos, n, trace, vel)` returns 0, 1 or 2 [inf name]
Validator; 2 means OK. A value of 1 comes from the wall-probe helper `FUN_180186840`, which is not decompiled; the
caller uses 1 to schedule a retry [I].
```c
if (!wallrun_enable) return 0;                                                     // [R]
if ((onGround || curtime - pl.lastGroundTime(0x2588) < 0.5) && !IsWallRunning()) return 0;   // [R]; 0x2588 meaning [I]
if (!pl.wallrunAllowed(0x1d50) || IsDodging() /*FUN_1805e6af0*/) return 0;          // [R]/[I]
if (n.z > 0.7 || n.z < -0.35) return 0;                                            // [R]
if (pl.grappleActive(0x23e8) && pl.0x23c4 > 0 && dot(grappleDir, n) > 0.0) return 0;  // [R], grapple meaning [I]
// trace-entity rejects: surface/contents flags (trace+0x3b, trace+0x34 & 0x100000), hit a player/NPC/titan (vfuncs
// 0x298/0x218/0x2a0), or a failed collision filter                               // [R], meanings [I]
if (pl.0x2250 != 0 && !FUN_1801632b0() && pl.0x15cc != 3) return 0;               // [R], meaning unknown
// same-wall rule (skipped if set.wallstickEnabled)
if (!set.wallstickEnabled /*0x218*/) {
  nh = normalize(n.x, n.y, 0);
  for each stored previous wall normal p (pl+0x18cc, count pl+0x18e8; normally 1):
    if (dot(nh, p) > wallrun_sameWallDot) {
      if (!set.wallrunSameWallAllowed /*0x234*/) return 0;
      zRef = lastWallRunStartPos.z (0x22a4) + wallrun_sameWallHeight;
      if (origin.z >= zRef) {
        d = len2D(origin - lastWallRunStartPos);
        if (d <= wallrun_sameWallDist) return 0;
        if ((d - wallrun_sameWallDist) * wallrun_sameWallSlope <= origin.z - zRef) return 0;
      }
    }
}                                                                                  // [R]
if (!IsWallRunning()) {                                    // approach angle only checked when starting
  v = normalize(vel);                                      // 3D
  if (dot(v, n) >= -cos(wallrun_minAngle_air * 0.5 * DEG2RAD)) return 0;  // cos: FUN_1807dae00 [inf, see 18017c6a0]
}
base = hitPos; behind = dot(origin - hitPos, n) < -0.01; if (behind) base = origin;   // [R]
// optional moving-platform/interp offset added to base (global DAT_180bfbd09, pl+0x298 bit1, pl+0x2594) [I]
probe z offsets: +16 and +hullHeight(mv+0x7c)*0.5 + mv+0x84
for each: r = WallProbe(mv, base+(0,0,off), off, n, mv+0x84, wallrun_allowed_wall_dist, &tr);
          if (r != 2) return r;  if (behind && tr.fraction > 0.7) return 0;           // [R]
if (!IsWallRunning() && AutomantleWants(viewFwd, n) /*180174650*/) {
  // also need a wall at hull top, otherwise mantle instead of wallrun
  r = WallProbe(mv, base+(0,0,mv.0x7c - mv.0x84), mv.0x7c, n, mv.0x84, wallrun_allowed_wall_dist, 0);
  if (r != 2) return r;                                                              // [R]
}
if (gp->field_0x38 == 2 /*FUN_1801b8210, bytes*/) { DENUVO TRIGGER "Denuvo_CheckWallrun" ... }   // [R]/[R]
if (g_wallrunTamperFlag /*DAT_180c01494*/) return 0;                               // see section 5
return 2;
```
`AutomantleWants` (`180174650`) [R]: the stick must push into the wall within `automantle_maxangle_push` (50°),
and `-dot(viewFwd, n) * set.mantleAngleScale(0x114) >= cos(automantle_maxangle_view)` (50°). While wallrunning it
uses `automantle_wallrun_maxangle_view` (45°) instead and also requires `dot(vel, n) >= -100`.

### fn_1801835c0: `CGameMovement::TryStartWallRun(hitPos, n, trace, vel)` [inf name]
Called on collisions from AirMove (`180161170`), WalkMove (`180185230`) and CategorizePosition (`180163830`).
```c
pl.wallrunRetryTime(0x22c4) = 0;
r = CanWallRun(...);
if (r == 2) {
  if (!IsWallRunning()) {                                   // BEGIN WALLRUN
    pl.wallNormal(0x2284) = n;
    pl.wallRunCount(0x22b0)++;
    s = Strength();                                         // see section 3.4
    boost = wallrun_upWallBoost * s;
    u = upAlongWall(n) = ( -n.z*n.x/|n.xy|, -n.z*n.y/|n.xy|, |n.xy| );   // unit "up" in the wall plane
    vu = dot(vel, u);
    if (vu < boost) vel += (boost - max(vu, 0)) * u;        // [R]
    play sound_wallrunImpact (set+0xbcc), jumpjet_wallrun_start/body/finish 1p/3p (0x128c..0x13cc)
      or "%s.stealth" if pl.stealthSounds (0x1d67)          // [R]/[R]
    for each stored normal p: if (dot(n, p) > wallrun_sameWallDot) pl.wallRunWeak(0x22b4) = 1;   // [R]
    if (curtime - pl.wallRunStartTime > 0.25) FUN_180160ba0(pl, n);   // begin-wallrun event/rumble [I]
    pl.lastWallRunStartPos(0x229c) = origin;  pl.wallRunStartTime(0x22a8) = curtime;
    pl.wallRunPushAwayTime(0x22b8) = 0;  pl.wallHanging(0x22e0) = 0;  pl.0x2598 = pl.0x2599 = 0;
    EndSlide() /*FUN_180166e80*/;  script callback (likely CodeCallback_OnPlayerBeginWallrun) [I]
  } else RotateWallNormal(n);                               // already running: follow the new wall (18017c6a0)
} else {
  if (r == 1 && !onGround && n.z <= 0.7) {                  // near miss: retry later
    pl.wallrunRetryTime(0x22c4) = curtime + wallrun_retry_interval;
    pl.wallrunRetryPos(0x22c8) = origin;  pl.wallrunRetryNormal(0x22d4) = n;     // [R]
  }
  if (n.z <= 0.7) {
    if (IsWallRunning() && vel.z <= 140) {
      trace origin -> origin - (0,0,pl.0x1610);
      if (tr.fraction < 1 && tr.normal.z > 0.7) StopWallRun();   // landed [R]
    }
  } else if (dot(origin - hitPos, n) >= 0) StopWallRun();   // hit a floor [R]
}
```
The retry is consumed in AirMove (`180161170`) [R]: when `curtime >= retryTime` and
`|dot(origin - retryPos, retryNormal)| < 1.0`, it traces toward −retryNormal and calls TryStartWallRun again.

### fn_180185230: `CGameMovement::WalkMove(dt)`, the surface move covering ground and wall [inf name]
Called when the "ground entity" is non-null. During a wallrun the wall acts as the ground surface. `IsWallRunning()`
is `FUN_1805d5230` = `1.0 > pl.wallNormal.z` ([R], from its instruction bytes). In other words, the surface normal
at `0x2284` is (0,0,1)-like on flat ground and the wall normal while wallrunning.
```c
vel -= dot(vel, n) * n;                                     // remove into-surface velocity [R]
noInput = |vel|^2 <= 1.0;
AngleVectors(viewangles, fwd, right, up);
// build in-surface axes: A ("forward") and B ("side", vertical-ish on a wall) from fwd/right crossed with n [R]
fm = mv.forwardmove(0x24); sm = mv.sidemove(0x28);
if (IsWallRunning()) {
  t = normalize(n.y, -n.x, 0);                              // horizontal wall tangent
  k = dot(A, t);
  sm' = sm + max(fm, 0) * k * wallrun_upwardAutoPush;       // forward input -> push along B (upward)
  fm' = fm + max(k * sm, 0) * fm;
  if ((B.z < -0.01 && sm' > 0) || (B.z > 0.01 && sm' < 0)) sm' = 0;   // never steer downward with side input
} else { /* normal ground input shaping, speedScaleSide (set+0x98), speedScaleBack (set+0x9c) */ }
wish = A*fm' + B*sm';   wish -= dot(wish, n) * n;           // [R]
if (!IsWallRunning()) { ground accelerate ... }
else {
  h = WallHangFactor();                                     // 180186bc0, 1.0 if not hanging
  if (!hanging) {
    slip = clamp((curtime - wallRunStartTime - wallrun_slipstarttime) / wallrun_slipduration, 0, 1);
    if (|fm'| < 0.1 && |sm'| < 0.1 && slip <= wallrun_noInputSlipFrac) slip = wallrun_noInputSlipFrac;
  } else
    slip = clamp((hangElapsed(0x22e8) - wallrun_hangslipstarttime) / wallrun_hangslipduration, 0, 1);
  // friction
  ramp = 1; if (curtime < pl.0x1f40) ramp = curtime <= pl.0x1f40-0.5 ? 0 : (curtime - (pl.0x1f40-0.5))*2;  // fade-in [R], meaning [I]
  fr = pl.wallrunFrictionScale(0x22bc) * set.wallrunFriction(0x224) * pl.surfaceFriction(0x2780) * ramp;
  if (set.wallstickEnabled) fr = 10;
  spd = |vel|; newspd = max(0, (spd - spd*dt*fr) * h); vz0 = vel.z; vel *= newspd/spd;
  if (slip != 0 && vz0 < 0) vel.z = lerp(vel.z, vz0, slip);    // slipping: friction does not stop the fall [R]
  // vertical accel
  if (|wish.z| > 1e-4) {
    ws = min(|wish.z|,1) * set.wallrunMaxSpeedVertical(0x21c) * speedMulA * speedMulB * h;  // 0 if wallstick
    Accelerate(dir=(0,0,sign(wish.z)), ws, accel = (1 - slip) * pl.wallrunAccelerateVertical(0x1d3c));
  }
  // horizontal accel
  if (|wish.xy|^2 > 1e-4) {
    d = normalize(wish.xy);
    cap = dot(d, mv.0x30 /*facing, [I]*/) <= -0.6 ? set.wallrunMaxSpeedHorizontalBackward(0x220)
                                                    : pl.wallrunMaxSpeedHorizontal(0x1d44);
    ws = min(|wish.xy|,1) * cap * speedMulA * speedMulB * h;                                // 0 if wallstick
    Accelerate(d, ws, pl.wallrunAccelerateHorizontal(0x1d40) * pl.0x1600 * speedMulA * speedMulB);
  }
}   // speedMulA/B = FUN_1805d2510 / FUN_18016e9e0 (player speed scales, [I])
// then hull trace origin -> origin + vel*dt; on a blocked step-move -> TryStartWallRun(endpos, planeNormal, ...)
```

### fn_180186d60: `CGameMovement::WallRunThink()`, the per-tick state machine (start/end/hang) [inf name]
```c
if (!IsWallRunning()) {                                     // view-tilt prediction only
  if (onGround || !pl.wallrunAllowed) return;
  T = wallrun_viewTiltPredictTime;
  p = origin + vel*T - (0,0, 0.5*T*T*g);     g = FUN_18048c400() [R]; NOTE: sv_gravity is not multiplied here
  trace hull origin->p; if (fraction >= 1) return;
  vPred = (vel.xy, vel.z - g*sv_gravity*fraction*T);         // here sv_gravity *is* multiplied [R]
  if (CanWallRun(endpos, planeNormal, tr, vPred) == 2) pl.predictedWallNormal(0x2290) = planeNormal;  // netvar, feeds view tilt [I]
  return;
}
prevHang = hangElapsed;  end = (mv.buttons & 0x4000004) != 0;   // 0x4 = IN_DUCK [I]
end |= pl.0x15cc == 2 || pl.0x15cc == 3;
if (!hanging && (pl.adsHeld(0x1599) || mv.buttons & 0x10000 /*ADS [I]*/)) {
  if (pl.wallrunAdsType(0x1d54) == 1 /*"wallhang"*/) { hanging = 1; hangStart(0x22e4)=curtime; hangElapsed=0; callbacks }
  else if (type == 0 && pl.adsHeld) pl.adsHeld = 0;
}
if (hanging && WallProbe(origin+(0,0,mv.0x7c), 0, n, ..., wallrun_allowed_wall_dist_wallhang) != 2) hanging = 0;
if (!hanging) {
  if (dot(mv.inputDir(0x6c), n) > 0.7) {                   // stick pushed away from wall
    if (pushAwayTime == 0) pushAwayTime = curtime;
    if (curtime - pushAwayTime > wallrun_pushAwayFallOffTime) end = true;
  } else pushAwayTime = 0;
  if (curtime - wallRunStartTime > pl.wallrun_timeLimit(0x1d48)) { end = true; timedOut = true; }
} else {
  hangElapsed += frametime;
  if (hangElapsed > pl.wallrun_hangTimeLimit(0x1d4c)) { end = true; mv.0x98 = 0; }
  if (!pl.adsHeld && !(buttons & 0x10000))            { end = true; mv.0x98 = 0; }
  if (justStartedHang) { event FUN_180160a00; sound_wallHangStart (0xc0c) }
  else if (prevHang < wallrun_hangStopTime && wallrun_hangStopTime <= hangElapsed) sound_wallHangComplete (0xc4c);
}
phase = (*pl.0x21d8) % 4;                                   // tick/cmd counter [I]
if (phase == 0) { if (WallProbe(origin+(0,0,16), 16, n, ..., wallrun_allowed_wall_dist) == 2 && !end) return; /* else END */ }
else { if (phase == 2 && vel.z > 0 && WallProbe(eye+(0,0,32), 16, n, ..., wallrun_allowed_wall_dist) != 2)
         vel.z = max(0, vel.z - frametime*3*wallrun_avoid_wall_top_decel);
       if (!end) return; }
// END WALLRUN
pl.adsHeld = 0;
if (!TestPosition(...) /*FUN_18016dd00 returns 0*/) vel += wallrun_fallAwaySpeed * n;   // [R], test meaning [I]
play hanging ? sound_wallHangFall (0xd4c) : sound_wallrunFall (0xd0c);
RecordRepelWall(n) /*1801603d0*/;  StopWallRun() /*FUN_1805ce3c0*/;  pl.wallRunWeak = 1;  SetGroundEntity(NULL);
if (timedOut) FUN_180160d40(pl, n);                         // timeout event/rumble [I]
```

### fn_180163830: `CGameMovement::CategorizePosition(bool)` (wall-relevant parts) [inf name]
- It traces along −n (not straight down) by `pl+0x1610` when on ground or wallrunning, otherwise by 2 units [R].
  A wallrun is therefore "grounded" on the wall.
- With `fwdDot = dot(trace.normal, n)` [R]:
  - If the player is wallrunning, `fwdDot < 0.999`, and the new normal's z is in (−0.35, 0.7), it calls
    RotateWallNormal(trace.normal), following curved walls and corners.
  - `wallrun_angleChangeMinCos` (0.8) enters only through a Ghidra comma expression. It clears the "snap origin to
    trace end" flag when `fwdDot < 0.8` (a change of more than about 36.9°). The rotation itself is not gated by it
    [code, reading of a comma expression].
- If the hit surface type differs from the current state (steep while not wallrunning, or a floor while wallrunning)
  [R]:
  - Not wallrunning, and more than 1.0 s since the last wallrun start: SetGroundEntity(NULL) and TryStartWallRun.
  - Wallrunning, and more than 0.5 s since the wallrun start: StopWallRun (landing).

### fn_180173a00: `CGameMovement::WallRunJump(n)`, the jump off a wall [inf name; called from CheckJumpButton `180172870` when wallrunning]
```c
in = mv.inputDir(0x6c);
if (hanging) { pl.adsHeld = 0; L = |in|; if (L < 1) in += (1 - L) * mv.0x30; }   // pad weak input toward facing
J = in * set.wallrunJumpInputDirSpeed(0x22c);
a = min(0, dot(n, mv.0x54 /*view fwd [I]*/));  b = min(0, dot(n, in));
out = (1 - a*b*0.8) * set.wallrunJumpOutwardSpeed(0x230);
if (dot(n, J) < out) J += (out - dot(n, J)) * n;            // guarantee outward speed
RecordRepelWall(n);  vel += J;  pl.wallRunWeak = 0;
up = Strength() * set.wallrunJumpUpSpeed(0x228);
if (vel.z < up) vel.z = min(vel.z + 1.5*up, up);
FUN_18016dd00(...)                                          // post-move position fix [I]
```

### fn_180186c70: `GetWallRunGravityScale(t = curtime - wallRunStartTime)` [inf name; used by gravity step `1801688d0` and others]
```c
T = set.wallstickEnabled ? 100 : wallrun_gravityRampUpTime;
t = max(t, 0);
if (t < T) return WallHangFactor() * (1 - (1 - t/T) * Strength());
return WallHangFactor();
```
Gravity half-step (`1801688d0`) [R]: `vel += gravDir * entGravity * sv_gravity * dt * 0.5 * scale`
(the 0.5 is a double constant).

### fn_180186bc0: `WallHangFactor()` [inf name]
```c
if (!IsWallRunning() || !hanging) return 1;
h = hangElapsed;  s = clamp((h - wallrun_hangslipstarttime) / wallrun_hangslipduration, 0, 1);
if (h >= wallrun_hangStopTime) return s;
a = 1 - h / wallrun_hangStopTime;   return (1 - a)*s + a;      // 1 -> s over hangStopTime
```

### fn_18017c6a0: `RotateWallNormal(newN)`, rate-limited [inf name]
```c
ang = wallrun_rotateMaxRate * frametime;                 // used directly as radians (no DEG2RAD)
if (dot(newN, n) > cos(ang)) n = newN;
else { p = newN - n*dot(n,newN) /*via cross products*/; n = n*cos(ang) + normalize(p)*sin(ang); normalize(n); }
```
`FUN_1807dae00` is cos and `FUN_1807db4c0` is sin. This follows from the rotation formula: the value compared
against the dot product is cos, and the perpendicular weight is sin [inf, strong].

### fn_1801603d0: `RecordRepelWall(n)` [inf name]
If `wallrun_repelEnable`: `pl.repelNormals[0] = normalize(n.x, n.y, 0)`, `pl.repelTime(0x18e4) = curtime`,
`pl.repelCount(0x18e8) = 1` [R]. Elsewhere, landing (`1805e2670`) sets the count to 0 [R].

### fn_18017eb80: `ApplyWallRepel(vel*, dir, softness)` [inf name; no caller in logic\]
```c
if (repelCount == 0) return;
t = curtime - repelTime;  if (IsWallRunning()) t *= 0.3;
soft = t < 0.2 ? 0 : (t < 0.5 ? softness*(t-0.2)/0.3 : softness);
w = t <= wallrun_repelTimeMin ? 1 : (t >= wallrun_repelTimeMax ? return : (t - Max)/(Min - Max));
v = *vel;
for each repel normal p: into = -dot(p, v);
  if (into > 0) { allow = dot(p,dir) > 0 ? dot(p,dir)*soft : 0; if (allow < into) v += (into - allow) * p; }
*vel = lerp(*vel, v, w);
```
`wallrun_repelSoftness` (5.0) is not referenced by any indexed function. It is probably the `softness` argument
[I].

### fn_1801c9350 / fn_1801c7110: `CPlayerDecoy::WallRunMove` / `CPlayerDecoy::RotateWallNormal` [inf names]
The Holo-Pilot decoy (`player_decoy`, created in `1801c40d0`) copies the player's `0x2284` normal into its own
`+0xf14` and continues the wallrun [R]. The functions do the following:
- Clip the move end against the wall plane (+0.001), hull-trace with mask 0x201400b, and rotate the normal with the
  same `wallrun_rotateMaxRate` math.
- Re-probe the wall at +16 within `wallrun_allowed_wall_dist`.

If the probe fails, or the hit normal's z is outside [−0.35, 0.7], the decoy switches to state 4 (fall). Its
velocity becomes the `+0xeec` speed along `normalize((vel.xy + n.xy)/2)`, with a vertical component of
`speed * (n.z + 0.2)` [R]. It is not part of player movement but uses the same ConVars.

---

## 3. Formulas (defaults from convars.tsv / settings table)

### 3.1 Start boost (`wallrun_upWallBoost` = 250) [R]
`u = (-n.z*n.x/|n.xy|, -n.z*n.y/|n.xy|, |n.xy|)`, `B = 250 * Strength`, `vu = vel·u`.
If `vu < B`: `vel += (B − max(vu,0))·u`. Upward-along-wall speed becomes at least B when you were already rising.
When you were falling (vu ≤ 0), B is added on top of the downward speed instead of replacing it.

### 3.2 Gravity ramp (`wallrun_gravityRampUpTime` = 1.0) [R]
`gscale(t) = h · (1 − (1 − t/T)·Strength)` for t < T, and `h` afterwards, where t = time since wallrun start and
h = WallHangFactor.
- Normal run (Strength 1): gravity rises linearly from 0 at contact to full at 1 s.
- Weak run (Strength 0): full gravity at once.
- `wallstickEnabled`: T = 100 s.

### 3.3 Slip [R]
- Wallrun: `slip = clamp((tRun − 1.5)/1.0, 0, 1)`. With no input (|fm|, |sm| < 0.1), slip ≥ `wallrun_noInputSlipFrac`
  (0.7) from the first tick.
- Vertical acceleration is multiplied by (1 − slip). If vel.z was negative before friction, friction's effect on
  vel.z is undone by the fraction slip: `vz = lerp(vz_after, vz_before, slip)`.
- Hang: `slip = clamp((hangT − 3.0)/1.0, 0, 1)`.
- `wallrun_slipvel` (70), `wallrun_hangslipvel` (70) and `wallrun_slipslowdown` (0.5) are registered but not
  referenced by any indexed server function [code: absent from index.txt]. They may be client-side or dead.

### 3.4 Strength / strength loss (`wallrun_strengthLossStart` = `wallrun_strengthLossEnd` = 1000) [R]
```
Strength = weak ? 0 : (count <= S ? 1 : (count < E ? (count−S)/(E−S) : 0))
```
`count` = `m_wallRunCount`, incremented at each wallrun start and reset to 0 by a ground jump (`180172870`).
- The middle branch rises from 0 to 1, which looks inverted for a "loss" ramp. Read as-is.
- With S = E = 1000 the ramp is unreachable, so Strength is 1 for counts up to 1000 and 0 after.
- In practice Strength is controlled by the `m_wallRunWeak` flag (0x22b4).

| Event | Weak flag |
|---|---|
| Wallrun end in WallRunThink | set to 1 [R] |
| Starting on a wall within sameWallDot of a remembered wall | set to 1 [R] |
| Ground contact / landing (`1805e2670`) | set to 1 [R] |
| Wall jump | cleared to 0 [R] |
| Ground jump | cleared to 0 [R] |

Design reading [I]: up-boost and the low-gravity start only happen after a jump, never after dropping off a ledge
or a wall.

### 3.5 Push-away / fall-off [R]
- Stick pushed away: `dot(inputDir, n) > 0.7`, held for longer than `wallrun_pushAwayFallOffTime` (0.05 s), ends the
  run.
- On end: `vel += wallrun_fallAwaySpeed (70) · n` if the position test (`FUN_18016dd00`) returns 0.
- Wall top: `vel.z = max(0, vel.z − frametime·3·wallrun_avoid_wall_top_decel (3000))`. This applies when the probe
  at eye+32 finds no wall, on phase-2 ticks (one tick in four).

### 3.6 Repel (`wallrun_repelEnable` 1, `repelTimeMin` 0.2, `repelTimeMax` 0.4, `repelSoftness` 5.0) [R]
The last wall's horizontal normal and time are stored when a run ends or on a wall jump. For
`t = curtime − leaveTime` (×0.3 while wallrunning), velocity into that wall is removed:
- fully while t ≤ 0.2,
- blended linearly to 0 by t = 0.4,
- with a softness allowance that is 0 before t = 0.2 and ramps in to t = 0.5.

### 3.7 Same-wall rules [R]
"Same wall" means `dot(horizNormal_new, horizNormal_last) > wallrun_sameWallDot` (0.9, about 25.8°).
- `wallrunSameWallAllowed` (class setting, code default 1) = 0: never.
- Below `lastStart.z + sameWallHeight` (0): always allowed.
- Above that height: rejected if the horizontal distance to the last start is ≤ `sameWallDist` (100). Also rejected if
  `(d − 100)·sameWallSlope ≥ heightGain`. With slope 0 that holds for any gain ≥ 0.

Net effect with defaults: you can only re-run the same wall from lower than where you last started on it. Even then
the run is marked weak (no boost, full gravity).

### 3.8 Allowed wall distance [R]
- Start: probes at +16 and at mid-hull reach up to `wallrun_allowed_wall_dist` (13) along −n.
- Maintain: the +16 probe at the same distance, every 4th tick.
- Wallhang: `wallrun_allowed_wall_dist_wallhang` (18), every tick.
- The decoy uses 13.

### 3.9 Angle requirements [R]
- Wall normal z ∈ [−0.35, 0.7]: overhang up to about 20.5° past vertical, slope at most about 45.6° from vertical.
- Approach: `dot(v̂, n) < −cos(minAngle_air/2)`. With the default 180 this is `dot < 0`, i.e. any motion into the wall.
  The parameter is a full cone angle around −n [cos identity inf].
- Following a curved wall: normals with z ∈ (−0.35, 0.7) are tracked at up to `wallrun_rotateMaxRate` = 3 rad/s
  (about 172°/s). The radians unit is inferred from cos being applied directly.
- `wallrun_angleChangeMinCos` (0.8, about 36.9°) only gates snapping the origin onto the new surface in
  CategorizePosition.
- In WalkMove, side-input axis selection switches basis when `|dot(right, n)| >= 0.7`.

### 3.10 View tilt [code/inf]
The server only does prediction:
- It extrapolates `wallrun_viewTiltPredictTime` (0.25 s) ballistically.
- If CanWallRun would succeed at the predicted impact, it stores the impact normal in netvar `0x2290`.

`wallrun_maxViewTilt` (15°) and `wallrun_viewTiltSpeed` (6.0) are not referenced by any indexed server function.
The roll itself is probably computed client-side from the wall normal and predicted normal [I].
The prediction mixes units: the z-drop uses `0.5·T²·g` without `sv_gravity`, while the velocity uses `g·sv_gravity`
[R]. Either `FUN_18048c400` is not a plain scale, or this is a game bug.

### 3.11 Jump-off velocity (set fields; values are the .set or code defaults) [code + bin]
```
J   = inputDir · wallrunJumpInputDirSpeed(100)
out = wallrunJumpOutwardSpeed(250) · (1 − 0.8·min(0,n·view)·min(0,n·inputDir))
J  += max(0, out − n·J) · n
vel += J
vel.z = vel.z < up ? min(vel.z + 1.5·up, up) : vel.z,   up = wallrunJumpUpSpeed(320) · Strength (Strength = 1 right after the jump, since weak is cleared first)
```
These three speeds are player-class fields at settings offsets 0x228, 0x22c and 0x230. Their code defaults (320 / 100
/ 250) come from the settings table in `.rdata` [R]. Actual pilot values come from the .set files and may differ.

### 3.12 Horizontal / vertical caps [R]
- Vertical wishspeed = `min(|wish.z|,1)·wallrunMaxSpeedVertical` (set 0x21c, code default 300).
- Horizontal wishspeed = `min(|wish.xy|,1)·wallrunMaxSpeedHorizontal` (player 0x1d44, from the .set file).
  `wallrunMaxSpeedHorizontalBackward` (set 0x220, code default 50) applies when the wish direction is more than
  about 127° from facing (cos −0.6).
- Friction = `wallrunFriction` (set 0x224, code default 4) × per-player scale × surface friction.

---

## 4. ConVar table (defaults from convars.tsv; flags 0x2000 on all wallrun_*)

The help text is truncated in convars.tsv for most of these, so it is not reproduced. "Used in" lists the indexed
functions.

| ConVar | Default | Used in | Role |
|---|---|---|---|
| wallrun_enable | 1 | 174890 | master enable ("Enables wall running") |
| wallrun_debug | 0 | 1835c0, 163830 | debug lines ("Shows wall run debug info") |
| wallrun_allowed_wall_dist | 13 | 174890, 186d60, 1c9350 | wall probe distance |
| wallrun_allowed_wall_dist_wallhang | 18 | 186d60 | hang probe distance |
| wallrun_minAngle_air | 180.0 | 174890 | approach cone (deg, full angle) |
| wallrun_angleChangeMinCos | 0.8 | 163830 | max normal change for surface snapping |
| wallrun_rotateMaxRate | 3 | 17c6a0, 1c7110 | normal rotation rate (rad/s, inf) |
| wallrun_upWallBoost | 250.0 | 1835c0 | start up-boost |
| wallrun_upwardAutoPush | 0.65 | 185230 | forward input converted to up push |
| wallrun_gravityRampUpTime | 1.0 | 186c70 | gravity 0 to full ramp |
| wallrun_strengthLossStart | 1000 | 1835c0, 173a00, 186c70 | strength ramp start (wallrun count) |
| wallrun_strengthLossEnd | 1000 | 1835c0, 173a00, 186c70 | strength ramp end |
| wallrun_slipstarttime | 1.5 | 185230 | slip begins (s) |
| wallrun_slipduration | 1.0 | 185230 | slip ramp length (s) |
| wallrun_noInputSlipFrac | 0.7 | 185230 | minimum slip with no input |
| wallrun_slipvel | 70 | (none indexed) | unused on server? |
| wallrun_slipslowdown | 0.5 | (none indexed) | unused on server? |
| wallrun_pushAwayFallOffTime | 0.05 | 186d60 | stick-away hold time to drop |
| wallrun_fallAwaySpeed | 70.0 | 186d60 | push off wall on end |
| wallrun_avoid_wall_top_decel | 3000 | 186d60 | vz decel near the wall top (×3×dt) |
| wallrun_retry_interval | 0.07 | 1835c0 | near-miss retry delay |
| wallrun_sameWallDist | 100 | 174890 | same-wall horizontal distance |
| wallrun_sameWallDot | 0.9 | 174890, 1835c0 | same-wall normal dot |
| wallrun_sameWallHeight | 0 | 174890 | same-wall height offset |
| wallrun_sameWallSlope | 0 | 174890 | same-wall height/distance slope |
| wallrun_repelEnable | 1 | 1603d0 | record wall for repel |
| wallrun_repelTimeMin | 0.2 | 17eb80 | full repel until (s) |
| wallrun_repelTimeMax | 0.4 | 17eb80 | repel gone at (s) |
| wallrun_repelSoftness | 5.0 | (none indexed) | probably the softness arg of 17eb80 (inf) |
| wallrun_hangStopTime | 0.5 | 186bc0, 186d60 | hang decel to stop (s) |
| wallrun_hangslipstarttime | 3.0 | 186bc0, 185230 | hang slip begins (s) |
| wallrun_hangslipduration | 1.0 | 186bc0, 185230 | hang slip ramp (s) |
| wallrun_hangslipvel | 70 | (none indexed) | unused on server? |
| wallrun_viewTiltPredictTime | 0.25 | 186d60 | view-tilt lookahead (s) |
| wallrun_maxViewTilt | 15.0 | (none indexed) | client view roll (inf) |
| wallrun_viewTiltSpeed | 6.0 | (none indexed) | client view roll speed (inf) |
| wallrun_curveEnable / wallrun_curveDebug | 1 / 0 | 1806073f0, 180606820 | func_wallrun_curve entity, not analysed |
| automantle_wallrun_maxangle_view | 45 | 174650 | mantle-vs-wallrun view cone |
| automantle_maxangle_push / _view | 50 / 50 | 174650 | mantle push/view cones |
| superjump_limitreset_onwallrun | 1 | 1805e2670 | wallrun refunds double jump |
| sv_gravity | 750 | 186d60, 1688d0 | gravity |

### Player-class (.set) fields involved
- Values for the `decomp\fn_180553b60.c` fields are not in code.
- The settings-table fields have code defaults [R].
  The table lives in `.rdata` at 0x180b80ff0–0x180b84xxx, with 0x30-byte entries: {name, default string,
  help, type, offset}.

| Field | Where | Default / note |
|---|---|---|
| wallrunAccelerateVertical | parser `param_1+0x13*8` (0x98); player copy 0x1d3c [I] | .set only |
| wallrunAccelerateHorizontal | parser `+0x15*8` (0xa8); player 0x1d40 [I] | .set only |
| wallrunMaxSpeedHorizontal | parser `+0x17*8` (0xb8); player 0x1d44 [I] | .set only |
| wallrun_timeLimit | parser `+0x19*8` (0xc8); player 0x1d48 [I] | .set only |
| wallrun_hangTimeLimit | parser `+0x1b*8` (0xd8); player 0x1d4c [I] | .set only |
| wallrun (bool) | parser `+0x1d*8` type/0x1e*8 value; player 0x1d50 [I] | required field |
| wallrunAdsType | parser `+0x1f*8`/`0x20*8` (index as float); player 0x1d54 [I] | options table {"none","wallhang","ads"} [R]; "wallhang" if missing in a required context |
| stealthSounds | player 0x1d67 [I] | selects "%s.stealth" sounds |
| wallstickEnabled | set 0x218 [R] | 0 |
| wallrunMaxSpeedVertical | set 0x21c [R] | 300 |
| wallrunMaxSpeedHorizontalBackward | set 0x220 [R] | 50 |
| wallrunFriction | set 0x224 [R] | 4 (help: per-player via CPlayer.SetWallRunFriction) |
| wallrunJumpUpSpeed | set 0x228 [R] | 320 |
| wallrunJumpInputDirSpeed | set 0x22c [R] | 100 |
| wallrunJumpOutwardSpeed | set 0x230 [R] | 250 |
| wallrunSameWallAllowed | set 0x234 [R] | 1 |
| speedScaleSide / speedScaleBack | set 0x98 / 0x9c [R] | 1 / 1 (non-wallrun input scaling) |
| sound_wallrunImpact … sound_wallHangFall | set 0xbcc, 0xc0c, 0xc4c, 0xc8c, 0xccc, 0xd0c, 0xd4c [R] | strings |
| sound_jumpjet_wallrun_start/body 1p/3p | set 0x128c/0x12cc/0x130c/0x134c [R] | 0x138c/0x13cc = finish 1p/3p [inf, table order] |

The player offsets 0x1d3c–0x1d67 match the parser order exactly: 5 floats, 2 bools/ints, doubleJump, pitch×2, then
4 bools ending at stealthSounds. Hence the high-confidence [I] on those mappings.

### Player state fields (inferred names; several match strings present in the DLL)
| Offset | Name |
|---|---|
| 0x2284 | wall/surface normal (IsWallRunning ⇔ n.z < 1) |
| 0x2290 | predicted/anim wall normal |
| 0x229c | m_lastWallRunStartPos |
| 0x22a8 | m_wallRunStartTime |
| 0x22b0 | m_wallRunCount |
| 0x22b4 | m_wallRunWeak |
| 0x22b8 | m_wallRunPushAwayTime |
| 0x22bc | m_wallrunFrictionScale |
| 0x22c4 / 0x22c8 / 0x22d4 | m_wallrunRetryTime / Pos / Normal |
| 0x22e0 | wallhanging |
| 0x22e4 | hang start time |
| 0x22e8 | hang elapsed |
| 0x18cc / 0x18e4 / 0x18e8 | repel normals / time / count |
| 0x2588 | last-ground timestamp (zeroed by jump; possibly m_wallRunClearTime) |
| 0x1599 | ADS-held latch |

---

## 5. Confidence notes and obfuscation

- **Denuvo trigger in CanWallRun (`180174890`).**
  - Condition: `FUN_1801b8210()`, which by its bytes is `gpGlobals->[0x38] == 2` [R].
  - Body: `ANTITAMPER_TRIGGER_CODEMARKER` markers around a data-block string naming `Denuvo_CheckWallrun()`
    (performance CRITICAL).
  - Check: if `FUN_180155da0()` (returns a global dword at 0x180be7e68) equals `DAT_180af0fa0` (initial value 1 in the file's `.data`; it may be patched at runtime), it
    calls `FUN_180165e50`.
  - By its bytes [R], `FUN_180165e50` sets `DAT_180c01494 = 1` when the flag is clear, `curtime > 120.0`, and the
    map name contains `"_ti"` (string at 0x18089e2e8; the comparison is a jmp-thunked substring search, inf).
  - CanWallRun returns 0 whenever `DAT_180c01494` is set.
  - Net effect [I]: on a copy judged tampered, wallrunning silently stops working 2 minutes into maps whose name
    contains "_ti" (e.g. the timeshift maps). This is deliberate sabotage. It is not part of the design and should be
    omitted from any reimplementation.
  - The static Ghidra output shows only markers. In the shipped binary Denuvo may replace or virtualise this block,
    so the exact trigger semantics are not reliable.
- **Ghidra artefacts.** Several conditions use comma expressions (CategorizePosition's `angleChangeMinCos` line), and
  stack-passed arguments are garbled (`CONCAT44(...)` in WallProbe calls). The ConVar *values* passed are clear; the
  exact argument order of the non-decompiled `FUN_180186840` (WallProbe) is inferred.
- **Not decompiled / unknown callees.** These are behaviour-named only [I]:
  - `FUN_180186840` (WallProbe, returns 0/1/2)
  - `FUN_1805ce3c0` (StopWallRun)
  - `FUN_18016dd00` (position test / unstick)
  - `FUN_180160230` (Accelerate)
  - `FUN_1805e6af0` (IsDodging)
  - `FUN_18048cbd0` (GetGroundEntity)
  - the speed multipliers `FUN_1805d2510` / `FUN_18016e9e0`
  - player fields 0x1600, 0x1610, 0x1f40, 0x2250, 0x15cc
- **Button bits.** 0x4 = duck, 0x4000000 = ?, 0x10000 = ADS: inferred from Source conventions and context.
- **Every-4th-tick probes.** The `(*pl.0x21d8) % 4` phase is read directly. That the counter is a command or tick
  number is inferred.
- **No invented numbers.** All numbers above are either ConVar defaults (convars.tsv), `.rdata` float constants
  (`f=` annotations or direct reads: 0.7, −0.35, 0.5, 16, 32, 140, 0.25, 0.8, 1.5, 3.0, 0.3, 0.2, −0.6, 0.01, 120.0),
  or code defaults of class-settings fields from the `.rdata` settings table [R]. The real pilot .set values
  (timeLimit, hangTimeLimit, accelerations, max horizontal speed, and any override of the jump speeds or friction)
  live in the VPK and are not known here.

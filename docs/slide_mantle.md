# Titanfall 2 pilot SLIDE and AUTO-MANTLE (server.dll static analysis)

> **Errata:** some statements in this note were superseded by later analysis passes or by the extracted `.set`/weapon files. See [ASSUMPTIONS.md §12](ASSUMPTIONS.md) for the list, and [TITANFALL2_MOVEMENT.md §0](TITANFALL2_MOVEMENT.md) for shipped values. Prefer those sources where they disagree.


Source: Ghidra output in `logic\`, `decomp\`, `convars.tsv`, and the string snapshot `server.snap`.
All numbers below come from code constants (`DAT_x /*f=...*/`) or ConVar defaults in `convars.tsv`. None are guessed.

Confidence tags used throughout:
- **[R]** read directly from the decompiled code or the string table.
- **[I]** inferred: a field or function identity, or intent, that the code does not state outright.

## 0. Shared conventions

| Expression in decomp | Meaning | Tag |
|---|---|---|
| `param_1` | `CGameMovement *this` | [I] |
| `param_1+8` | `player` | [I] |
| `param_1+0x10` | `mv` (CMoveData) | [I] |
| `param_1+0x18` | per-class player-settings block (the `.set` values) | [I] |
| `DAT_180bfbe08+0x10` / `+0x30` | `gpGlobals->curtime` / `gpGlobals->frametime` | [I] |
| `mv+0x8c..0x94` | velocity xyz | [I] (used as velocity everywhere) |
| `mv+0xe4..0xec` | abs origin | [I] |
| `mv+0x6c..0x74` | world wish direction (move input) | [I] |
| `mv+0x54..0x5c` | view forward | [I] |
| `mv+0x24` / `mv+0x28` | forwardmove / sidemove (normalised) | [I] (fn_180185230 uses them as move axes) |
| `mv+0x18` bit 2 / bit 4 | IN_JUMP / IN_DUCK held | [I] (Source button values) |
| `mv+0x20` bit 2 | jump pressed this frame | [I] |
| `mv+0x84` | a hull dimension (used like hull radius or width) | [I] |
| `FUN_18048cbd0(player)` | GetGroundEntity (non-null means on ground) | [I] |
| `FUN_1805d5230(player)` | IsWallRunning (it selects `automantle_wallrun_maxangle_view`) | [I] |
| `FUN_18016d980` / `FUN_18017fc10` | TracePlayerBBox / trace | [I] |
| `FUN_1807da374` | sqrt; `FUN_1807dae00` is cos | [I] |
| trace_t layout | `+0x10` endpos, `+0x20` plane normal, `+0x30` fraction, `+0x3b` startsolid | [I] (consistent across all uses) |

Player netvar offsets come from the datatable in `decomp\fn_18055ba00.c` and are therefore **[R]**:

| Offset | Name |
|---|---|
| 0x2250 | `m_duckState` |
| 0x2284 | `m_upDir` |
| 0x2588 | `m_onSlopeTime` |
| 0x21d8 | `m_pCurrentCommand` |
| 0x23f0 | `m_sliding` |
| 0x23f1 | `m_slideLongJumpAllowed` |
| 0x23f4 | `m_lastSlideTime` |
| 0x23f8 | `m_lastSlideBoost` |
| 0x22ec | `m_traversalType` (0 = mantle) |
| 0x22f0 | `m_traversalState` |
| 0x230c | `m_traversalEnd` |
| 0x2318 | `m_traversalMidFrac` |
| 0x2328 | `m_traversalRefPos` |
| 0x2334 | `m_traversalProgress` |
| 0x2338 | `m_traversalStartTime` |
| 0x233c | `m_traversalHandAppearTime` |
| 0x2340 | `m_traversalReleaseTime` |
| 0x2344 | `m_traversalBlendOutStartTime` |
| 0x2348 | `m_traversalBlendOutStartOffset` |
| 0x235c | `m_wallDangleJumpOffTime` |
| 0x2360 | `m_wallDangleMayHangHere` |
| 0x2361 | `m_wallDangleForceFallOff` |
| 0x2362 | `m_wallDangleLastPushedForward` |
| 0x2364 | `m_wallDangleDisableWeapon` |
| 0x2368 | `m_wallDangleClimbProgressFloor` |
| 0x23e8 | `m_grappleActive` |

### Per-class slide settings: struct offsets

The `.set` parser `decomp\fn_180553b60.c` only covers `slideFOVScale`, `slideFOVLerpInTime` and `slideFOVLerpOutTime`. The physics keys are parsed elsewhere, in code that is not in the dump.

In `.rdata` the key strings sit in this order: `slideDecel`, `slideWantToStopDecel`, `slideVelocityDecay`, `slideAccel`, `slideRequiredStartSpeed`, `slideStopSpeed`, `slideMaxStopSpeed`, `slideMaxJumpSpeed`, `slideSpeedBoost`, `slideSpeedBoostCap`, `slideJumpHeight`.

The consumers below line up exactly with that order at consecutive 4-byte offsets:

| settings offset | key | help string (from binary) | consumer | Tag |
|---|---|---|---|---|
| 0x258..0x264 | slideDecel, slideWantToStopDecel, slideVelocityDecay, slideAccel | none in snapshot | **no consumer found** in logic\ or decomp\ | offsets [I] |
| 0x268 | slideRequiredStartSpeed | "required speed to begin a slide" | slide start fn_180183180 | [R] use / [I] name |
| 0x26c | slideStopSpeed | "slide ends when below this speed" | fn_180185230:456 (slide end) | [R] use / [I] name |
| 0x270 | slideMaxStopSpeed | "slide may not end while above this speed" | no consumer found | [I] |
| 0x274 | slideMaxJumpSpeed | "velocity is reduced to this when jumping from slide" | jump fn_180172870:207 | [R] use / [I] name |
| 0x278 | slideSpeedBoost | "extra speed given when a slide starts" | slide start | [R] use / [I] name |
| 0x27c | slideSpeedBoostCap | "extra slide speed doesn't bring player speed above this" | slide start | [R] use / [I] name |
| 0x280 | slideJumpHeight | none | jump fn_180172870:309 | [R] use / [I] name |
| 0x84 (byte) | class key `slide` (slide allowed) | none | slide start | [I] |
| 0x80 (byte) | class key `automantle` | none | mantle detect | [I] |
| 0x114 | `mantleAngleScale` | "Scales the maximum mantle angle convar. Allows different mantle angle settings for boost and wallrunner..." | angle check | [I] |

The `.set` files hold the actual values of these keys, and they are not in this dataset. Their numbers are therefore unknown here.

---

## 1. SLIDE

### 1.1 Start conditions (fn_180183180, `TryStartSlide`)

These rules run in order. If any one fails, the slide does not start.

1. **Ground or air.** Requires `groundNormal.z >= 0.7` (`param_2+8`) **or** `slide_whileInAir != 0`. The default is 0, so a slide needs walkable ground (normal z of at least 0.7). [R] The tie between `param_2` and the ground normal is [I].
   - Help text for `slide_whileInAir`: "Allows beginning a slide (including the boost) while still in the air".
2. **Forced slide.** If `player+0x15cc == 3`, the speed and angle checks are skipped and speed² is floored at 0.01. [R] That state 3 means "ForceSlide" (script: "Forces the player to crouch and slide") is [I].
3. **Otherwise:**
   - The class must allow sliding (`settings+0x84`). [R]
   - Horizontal speed² must be at least `slideRequiredStartSpeed²`. [R]
   - **If on ground only:** `dot(viewForward, wishDir) >= slide_max_angle_dot`. The default is 0.6, which is about 53.1°. The help text is "Cosine of max angle from forward that you can slide when sprinting". [R] The comparison is plain `<` → return, and the vectors are not normalised in this function.
   - **Sprinting:** this function has no sprint check, so the caller presumably requires sprint, as the help text implies. [I]
4. **On success:**
   - Sets `m_sliding = 1`. [R]
   - Calls `FUN_18043a7d0(player, 2)` and `FUN_1805e0f30(player, 0x16, 1)`. These are probably crouch and slide-state flag helpers. [I]

### 1.2 Boost and cooldown [R]

```
elapsed = curtime - m_lastSlideTime
if (elapsed > slide_boost_cooldown):            // default 2 s, strict >
    speed  = sqrt(hspeed2)
    target = min(speed + slideSpeedBoost, slideSpeedBoostCap)
    if (target <= speed): m_lastSlideBoost = 0  // already at/above cap: no boost, never slowed
    else:
        m_lastSlideBoost = target - speed
        vel.xy *= target / speed                // horizontal only
    m_slideLongJumpAllowed = true
    EmitSound("jumpjet_slide_start_1p", "jumpjet_slide_start_3p"); FUN_180556080(...)  // rumble? [I]
m_lastSlideTime = curtime                        // ALWAYS, even when no boost was given
```

Points to note:
- The cooldown counts from the **last slide start, boosted or not**. Chaining slides within 2 s keeps resetting the timer, so none of them gets a boost.
- `m_slideLongJumpAllowed` is only set on slides that were eligible for the boost.
- If speed is already above `slideSpeedBoostCap`, the slide neither boosts nor clamps speed.

### 1.3 Per-tick behaviour while sliding

**Steering (fn_180185230:268-281)** [R]
- While `m_sliding` and not wall-running, the wish direction loses its component along the current velocity, but only when that component is positive.
- So a slide cannot be accelerated forward. Input can only steer sideways or brake.
- In forced-slide state (`0x15cc==3`) the negative (braking) component is removed as well.

**Slope gravity (fn_1801688d0 / fn_18016a9f0, half-step gravity)** [R]
- While sliding and on ground, each gravity half-step adds `g_half * n.xy * n.z` to horizontal velocity, where `n = FUN_1805d2c80(player)`.
- Here `g_half = 0.5 * sv_gravity(750) * dt * gravityScale`.
- In effect, slides accelerate downhill on slopes. That `n` is the ground normal is [I].

**slideDecel / slideWantToStopDecel / slideVelocityDecay / slideAccel**
- These values are not referenced by any function in this dump. The friction and accelerate callees `FUN_18016ea60`, `FUN_1801696b0` and `FUN_180160230`, called from fn_180185230:295-297, are not decompiled here.
- **The per-tick deceleration formula therefore cannot be stated from this data.** Next step: decompile those three callees.

**Step reduction (fn_18017f2c0)** [R]
```
if (!player->m_bStepFlag@0x1614) { FUN_180183460(); return; }   // other move path [I]
StepMove(..., stepSize=player+0x1610, frametime, ..., out stepInfo)
if (m_sliding && stepInfo.heightStepped > 0):                  // stepInfo+8 [I name]
    s = |vel.xy|
    s' = max(0, s - heightStepped * slide_step_velocity_reduction)   // default 10 speed per unit stepped up
    vel.xy *= s'/s
```
- Example: stepping up an 18-unit step while sliding costs 180 units/s. The 18 is arithmetic on the default value, not a number from the code.

### 1.4 Stop conditions

- **fn_180185230:455** [R]: if `m_sliding`, 3D speed < `slideStopSpeed`, and not in forced slide (`0x15cc != 3`), the code calls `EndSlide()` (fn_180166e80).
- **slideMaxStopSpeed** ("slide may not end while above this speed") has no consumer in this dump. Other stop paths (for example, releasing crouch) are not in the dump either.

**`EndSlide()` (fn_180166e80)** [R]
- Clears `m_sliding`.
- Calls `FUN_1805e0f30(player, 0x16)`, which undoes the start-time flag.
- If `slide_auto_stand` (default 0, "Automatically stand when slide ends") is on and `player+0x15c8` is set, it clears `player+0x15c8`. That field is probably the crouch-toggle or "wants duck" flag, so clearing it stands the player up. [I]

### 1.5 Slide jump (in `CheckJumpButton`, fn_180172870:188-226 and 305-310) [R]

```
if (m_sliding):
    h2 = |vel.xy|^2
    if (m_slideLongJumpAllowed && h2 > 0):
        s = sqrt(h2)
        if (m_duckState != 2 && curtime - 0.4 < m_lastSlideTime):     // jumped < 0.4 s after slide start
            s = max(0, s - m_lastSlideBoost); vel.xyz *= s/sqrt(h2)    // strips the slide boost (z too)
        M = slideMaxJumpSpeed
        if ((0.85*M)^2 < h2 && h2 < M^2):  vel.xyz *= M / s           // speeds in (0.85M, M) snapped to M
        m_slideLongJumpAllowed = false                                 // one long-jump per boosted slide
jumpHeight = m_sliding ? slideJumpHeight : settings+0x94 (normal jump height)
vel += m_upDir * sqrt(2 * sv_gravity * jumpHeight) * scale
```

- Jumping within **0.4 s** of slide start removes the boost, but only while `m_duckState != 2`. That probably means "not yet fully crouched". [I]
- The `slideMaxJumpSpeed` block only acts when the original horizontal speed lies strictly between 85% and 100% of it, and then it **sets** speed to exactly `slideMaxJumpSpeed`. Speeds above the maximum are left alone.
- The help text says "velocity is reduced to this", which does not match the code. [R] for the code. A wider clamp elsewhere is possible but was not found.
- `CheckJumpButton` also refuses to jump when `m_duckState==3` and the player is not sliding. [R]

---

## 2. AUTO-MANTLE

The state values for `m_traversalState` are 3..12. The activity is state + 0x13e, and the order matches the `ACT_VM_MANTLE_*` string order. The mapping is [R] for the arithmetic and [I] for the names.

| state | activity | | state | activity |
|---|---|---|---|---|
| 3 | BELOW | | 7 | CROUCH_BELOW |
| 4 | LEVEL | | 8 | CROUCH_LEVEL |
| 5 | ABOVE | | 9 | CROUCH_ABOVE |
| 6 | HIGH | | 10 | CROUCH_HIGH |
| 11 | IDLE (dangling) | | 12 | JUMPOFF |

The crouch variants are the base state + 4 when `m_duckState == 2`.

### 2.1 Gating (fn_180181d20, top) [R]

- Requires `automantle_enable` (1) and the class `automantle` flag (`settings+0x80`).
- **On ground:** only if wall-running or jump is held.
- If `curtime - m_onSlopeTime < 0.25` and jump is not held: no mantle.
- No mantle while grappling (`m_grappleActive && player+0x23c4 > 0`).
- **Cooldown:** `curtime - m_traversalStartTime < automantle_cooldown` (0.25 s, "Minimum time between mantles") blocks a new mantle.
- `pressingFwd = forwardmove >= 0.3`. If not pressing forward, not wall-running, and `dot(yawForward, vel.xy) <= 0.01`, there is no mantle.

### 2.2 Detection traces

**A. Forward hull sweep** [R]
```
dist = max(automantle_searchdist - 0.5, 0.5)       // 5 -> 4.5
dist = max(dist, max(fwdSpeed,0) * frametime)      // look further at speed
tr = TraceHull(origin -> origin + yawForward*dist)
if (tr.normal.z > 0.7)                             // walked into a slope: follow it down and retrace
    end.z -= dot(yawForward, n)/n.z * dist; retrace
```
- `automantle_searchdist` help text: "Forward distance within which to look for a ledge to auto-mantle".

**B. If nothing was hit (or only floor): anticipation only** [R]
- This branch runs only in air or while wall-running.
- It predicts the position `t = lookahead + 1/30 s` ahead using ballistic gravity, and runs the ledge check without the full checks.
- If a ledge is coming and the predicted eye height is at most `ledge.z + automantle_gun_enable_height` (33), it sets `m_wallDangleDisableWeapon = 5`, which lowers the gun early. If no ledge is found, it resets the value to 0.
- **This branch always returns 0, so it never starts a mantle.**
- The lookahead value comes from `FUN_1800e37d0(player)->vfunc_0x238()+0x17e8`, which is unresolved. [I]

**C. Wall hit: ledge check, `FindMantleLedge` (fn_180173f00)** [R]

Rejection checks:
- Rejects if the surface contents have flag `0x100000`, or if the hit entity fails `FUN_180174810`, a mantle-able check. [I name]
- `wallN` = the wall normal flattened to xy and normalised. `wallDist = dot(pos - hit, wallN)`.
- **Angle gate:** see 2.3.

Probe geometry:
```
reach = automantle_forwarddist + wallDist                    // 30
d     = max(automantle_forwarddist, automantle_mindist) + wallDist   // max(30,18)
top   = extraHeight + automantle_height + 0.03125            // 80
probeZ = pos.z + {top, 66.5, 34.5, 26.5, 16.5}[command_number % 5]  // probe height cycles per usercmd
trace start=(pos.xy, probeZ) -> start - wallN*d
pen = frac*d
```
- If `pen - wallDist < automantle_mindist` (18), the function returns 1: the wall is too tall at this height, so there is no ledge.
- Otherwise it traces down from `lerp(start, end, min(frac, reach/d))` by `top` units to get the ledge trace.

When the ledge normal z is at least 0.7 (walkable top):
- `ledgeH = ledge.z - pos.z`. Rejects if `ledgeH < 0.03125`, or the ledge entity or contents are bad, or the vertical headroom trace from `pos` up to probe height is blocked.
- **Full check only (`param_9`):**
  - **Low ledge (stairs):** if `ledgeH < stepSize(player+0x1610) + 0.03125`, not wall-running, and moving into the wall (`dot(vel, wallN) < -1`), the code tests the step with `FUN_18017fb90`. If that test does not start solid, it returns 0 and normal stepping handles the ledge. If crouched (`m_duckState==2`), it returns 0.
  - **Slope or stair guard:** if `ledgeH < 2*pen`, moving into the wall, and (`|wall normal z| > 0.1` or `ledgeH < 2*stepSize`), it traces horizontally at `pos.z + ledgeH/2` by `0.35*pen`. If that trace is clear, it returns 0: this is not a real wall.
  - **Dangle space:** traces from `(pos.xy, ledge.z - (automantle_dangle_required_space(60) - 5))` into the wall by `2*mv+0x84`. `dangleFlag = frac < 1`, meaning there is wall for the feet or hands about 55 units below the ledge. [R] The intent is [I].
  - `outDist = min(reach, pen)`.
- Returns 2 (ledge found).

**D. Back in detect, after `FindMantleLedge` returns 2**

A mantle starts only if `dangleFlag || pressingFwd`.
- **Ledge-top settle loop** [R]: raises the top position in +4 steps. On sloped hits it re-projects onto the plane and accepts rises under 6 units. Each step retraces down until a trace is clear (fraction 1), capped at the probe start height.
- **Stand vs. crouch** [R/I]: if not crouched, it traces the standing hull from the ledge top toward the wall by `0.99*dist`. If that is blocked or starts solid, it restores the origin and calls `FUN_180166de0`, which is probably force-duck, so the mantle runs crouched. [I]
- `begin = origin`, but if the origin sits more than `automantle_height + 0.03125` below the ledge top, `begin.z` is raised to `ledgeTop.z - 80.03`. [R]
- `horiz = min(tr.frac*dist, dist - automantle_mindist) + mv+0x84`. `m_traversalRefPos = ledgeTop - wallN*horiz`. [R]
- **`midFrac = automantle_height / (automantle_height + horiz)`**: the share of the path that is vertical. It is passed to `FUN_1801625e0`, `StartTraversal(type 0)`, together with begin, mid and end. [R] for the value, [I] that it becomes `m_traversalMidFrac`.
- `m_wallDangleMayHangHere` is computed by `FUN_1801670f0`. It is cleared if `frac*automantle_height < automantle_dangle_required_space` or there is no `dangleFlag` (the code multiplies a fraction by `automantle_height`), or if the ledge top is more than `horiz + 4` from `RefPos` along the normal. [R] The intent is [I].
- **Initial progress** [R]: `m_traversalProgress = min(1, (begin.z - (ledgeTop.z - 80.03)) / 80) * midFrac`. A player who starts higher skips part of the climb.
- `m_wallDangleClimbProgressFloor = (1 - frac) * midFrac`. Viewmodel cycle (`player+0x2614`) is reset to 0. Script `CodeCallback_OnPlayerMantle` fires (string present; the callback pointer use is [I]). Returns 1.

### 2.3 Angle gating (fn_180174650, `MantleAngleOK`) [R]

```
push = -dot(wallN, wishDir)
require push >= 0 && |wishDir|^2 * cos(automantle_maxangle_push)^2 <= push^2     // 50 deg; zero input passes
view = -dot(wallN, yawForward) * settings.mantleAngleScale
if (!wallrunning) return view >= cos(automantle_maxangle_view)                  // 50 deg
else return view >= cos(automantle_wallrun_maxangle_view)                        // 45 deg
            && dot(vel, wallN) >= -100
```
- `mantleAngleScale` scales the dot product, not the angle. Its offset (0x114) is [I].

### 2.4 Classification below / level / above / high (fn_180180780, on the first tick `param_2 != 0`) [R]

```
h = (eyeOffset.z + origin.z) - traversalEnd.z      // "mantle height above ledge" (negative = ledge above eye)
if (h >= automantle_height_below(-10)  ||  dot(eyePos - end, forwardDir) <= -mv+0x84 - 4):
    state = (h >= automantle_height_level(10)) ? ((h >= automantle_height_above(30)) ? HIGH : ABOVE) : LEVEL
else state = BELOW
```

| h (eye height above ledge) | class |
|---|---|
| < -10 | BELOW (but only when horizontally close, otherwise LEVEL) |
| -10 .. 10 | LEVEL |
| 10 .. 30 | ABOVE |
| ≥ 30 | HIGH |

Then, every tick:
- IDLE if hanging at the rest point.
- LEVEL when leaving a hang.
- +4 if crouched.
- JUMPOFF if a jump-off is pending.

`eyeOffset` (`FUN_18043f7d0(player)+8`) being the view offset z is [I].

### 2.5 Durations and progress rate [R]

The rate is chosen by the base state (crouch variants map back with -4):

| state | duration ConVar | threshold `thr` |
|---|---|---|
| LEVEL | `automantle_duration_level` (1.0) | `height_below - eyeZ` |
| ABOVE | `automantle_duration_above` (0.5) | `height_level - eyeZ` |
| HIGH | `automantle_duration_high` (0.35) | `height_above - eyeZ` |
| BELOW or other | `automantle_duration_below` (1.11) | `-automantle_height` |

```
startFrac = ((thr + automantle_height) / automantle_height) * midFrac
rate      = (1 - startFrac) / duration           // progress units per second
```
- The animated span from `startFrac` to 1 therefore takes exactly `duration` seconds.
- Window traversals (`m_traversalType != 0`) use `rate = 1 / traversal_window_duration` (0.3).
- Velocity is zeroed every tick while traversing. [R]

### 2.6 Progress target, hang, jump-off, back-off (fn_180180780) [R]

Inputs:
- `pushing = dot(traversalForwardDir, wishDir) > 0.1`
- `pulling = dot(...) < -0.5`
- `m_wallDangleLastPushedForward` latches the last push.

**Jump-off start**
- Pressing jump (pressed this frame) sets `m_wallDangleJumpOffTime = curtime`.
- Conditions: no force-fall-off, no jump-off already pending, and not (pushing while `progress < midFrac`).

**Target**
- No jump-off pending: `target = 1`.
- Not advancing, and (pulling, or force-fall-off, or `!m_wallDangleMayHangHere`, or the surface up-vector `|z| <= 0.99`): `target = 0`.
- Not advancing otherwise (can hang): `target = restFrac`. That is `automantle_rest_frac` (0.4), or `automantle_rest_frac_below` (0.3) for BELOW.
- `target = max(target, m_wallDangleClimbProgressFloor)`.
- "Advancing":
  - true if pushing;
  - once past the rest point with no input, true if `progress > midFrac`, else it falls back to `LastPushedForward`.
- While a jump-off is pending, progress is frozen.

**Movement toward target**
- `progress` moves toward `target` by `rate * frametime` without overshooting.
- When it sits at the rest target it snaps to `automantle_rest_frac`. That holds even for BELOW, which looks odd and may be a decompiler artefact. The state becomes IDLE.

**Weapon**
- `gunFrac = (automantle_gun_enable_height(33) - eyeZ + H) / (H + mv+0x84)`.
- The weapon stays disabled (`m_wallDangleDisableWeapon = 1`) until both `target` and `progress` are at least `gunFrac`. Then it is set to 0.

**Jump-off execution**
- Fires when `m_wallDangleJumpOffTime != 0` and either `curtime >= jumpOffTime + automantle_jumpoff_duration` (0.1 s) or `progress > automantle_jumpoff_anim_maxfrac` (0.5). The help text for the latter is "Maximum fraction of mantle at which jump off animation is played".
- The code then calls `StopTraversal` and sets `m_bHasJumpedSinceTouchedGround = 1`.
- If `progress >= midFrac`: velocity = wishDir × speed (`FUN_1805d2bd0`, probably current max speed [I]), then `CheckJumpButton()` (a normal jump from the top).
- Otherwise it kicks off the wall: `vel.xyz = -forwardDir * settings+0x230; vel.z = settings+0x228`. These are per-class values whose key names were not resolved.

**Back-off and abort** (`automantle_backoff_anim_maxfrac`, 0.7: "Fraction of mantle after which pulling back simply aborts the mantle")
- Pulling, with `progress >= midFrac` or `progress >= 0.7`: immediate `StopTraversal`.
- Pulling earlier sets target 0. Progress then slides back, and once `progress <= climbFloor`, or `target==0` with viewmodel cycle ≤ 0, while not pushing (or force-fall-off), `StopTraversal` drops the player.
- Pulling, or crouch (`IN_DUCK|0x4000000`) while IDLE:
  - if the base state was IDLE, sets `m_wallDangleForceFallOff` and the player falls;
  - otherwise it stops the traversal.

**Completion** (`progress == 1`)
- `m_traversalProgress = 1`, then `FUN_180180710` (end-mantle cleanup [I]).
- The origin becomes `m_traversalEnd` transformed by the ground or parent entity matrix, so moving platforms are supported.
- If pushing, `vel = wishDir * speed` (an exit at full move speed). Otherwise velocity stays 0.

**Window traversals**
- Pulling: stop.
- Jump pressed (not on the first tick): stop, then jump.

### 2.7 Position interpolation over the mantle [R/I]

- **`traversal_anim = 0`:** `origin = FUN_1805d1e50(player)`. This is probably begin→mid for `progress < midFrac` (vertical) and mid→end after (horizontal). [I] The callee is not decompiled.
- **`traversal_anim = 1` (default):**
  - The origin follows the first-person-proxy animation's root motion at `cycle`, placed in a frame built from `traversalForwardDir` at the traversal reference position.
  - `cycle = clamp((progress - startFrac)/(1 - startFrac), 0, 1)`.
  - The stored cycle moves toward that value at `1.5 × rate × (1/(1-startFrac))` per second.
  - When the animation changes, `m_traversalBlendOutStartOffset` captures `(currentOrigin - animOrigin)`, with any positive component along the facing removed. That offset fades out linearly over **0.2 s** (`(curtime - blendStart)/0.2`).
  - The `ledgeAngle` pose parameter is copied from the player to the viewmodel. [R]

**`automantle_min_frac` (0.1) / `automantle_max_frac` (0.5):** no function in `logic\index.txt` reads these. They are registered but unused in this server code; possibly client-only or dead. [R] that they are absent from the indexed logic.

### 2.8 Other `automantle_height` users

| function | role | Tag |
|---|---|---|
| fn_18016dd00 | window-hint jump assist (uses `window_hint_*`, `sv_gravity`): adjusts a jump arc to pass through a window, and treats arcs that reach within `automantle_height` below the sill as acceptable because a mantle will catch them | [I] |
| fn_18060ca70, fn_1805e0580 | grapple window hint: `automantle_height` is passed as the maximum height to the same window finder `FUN_1806079b0` | [R] |

---

## 3. Per-function notes and pseudocode

### fn_180183180: `CGameMovement::TryStartSlide(groundNormal)`
Starts a slide and applies the boost.
```c
void TryStartSlide(Vector *groundN) {
  if (!(groundN->z >= 0.7f || slide_whileInAir)) return;
  float s2;
  if (player->state_0x15cc == 3) s2 = max(hspeed2(mv->vel), 0.01f);           // forced slide
  else {
    if (!settings->slideAllowed) return;
    s2 = hspeed2(mv->vel);
    if (s2 < sq(settings->slideRequiredStartSpeed)) return;
    if (GetGroundEntity() && dot(mv->viewForward, mv->wishDir) < slide_max_angle_dot) return;
  }
  player->m_sliding = true;
  SetDuck(player, 2); FUN_1805dd180(player); SetFlag(player, 0x16, 1);
  if (curtime - player->m_lastSlideTime > slide_boost_cooldown) {
    float s = sqrt(s2), t = min(s + settings->slideSpeedBoost, settings->slideSpeedBoostCap);
    if (t <= s) player->m_lastSlideBoost = 0;
    else { player->m_lastSlideBoost = t - s; mv->vel.xy *= t / s; }
    player->m_slideLongJumpAllowed = true;
    EmitSound("jumpjet_slide_start_1p", "jumpjet_slide_start_3p"); Rumble();
  }
  player->m_lastSlideTime = curtime;
}
```

### fn_18017f2c0: `CGameMovement::StepMove` (slide step penalty)
Runs the step-move and applies the slide's step-up speed loss.
```c
void StepMoveWithSlidePenalty(..) {
  if (!player->flag_0x1614) { FUN_180183460(); return; }
  StepInfo si; DoStepMove(origin, &mv->vel, &si, frametime, player->stepSize, settings->f_0xa4, player->m_upDir, ..);
  mv->origin = result; PostStep(&si);
  if (player->m_sliding && si.stepUp > 0) {
    float s = len2d(mv->vel); if (s > 0) mv->vel.xy *= max(0, s - si.stepUp * slide_step_velocity_reduction) / s;
  }
}
```

### fn_180166e80: `CGameMovement::EndSlide()`
```c
void EndSlide() {
  if (!player->m_sliding) return;
  player->m_sliding = false; FUN_1805dd180(player); ClearFlag(player, 0x16);
  if (slide_auto_stand && player->duckToggle_0x15c8) player->duckToggle_0x15c8 = false;   // stand up
}
```

### fn_180181d20: `CGameMovement::CheckAutoMantle()` (returns 1 when started)
Gating, forward sweep, anticipation, ledge settle, crouch fit, building begin/mid/end, midFrac and initial progress (sections 2.1 to 2.2).
```c
bool CheckAutoMantle() {
  if (!automantle_enable || !settings->automantle) return 0;
  if (OnGround() && !IsWallRunning() && !(mv->buttons & IN_JUMP)) return 0;
  if (curtime - m_onSlopeTime < 0.25f && !(mv->buttons & IN_JUMP)) return 0;
  if (m_grappleActive && grappleAttached) return 0;
  if (curtime - m_traversalStartTime < automantle_cooldown) return 0;
  fwd = YawForward(); fwdSpd = dot2(fwd, vel); pressingFwd = mv->forwardmove >= 0.3f;
  if (!pressingFwd && !IsWallRunning() && fwdSpd <= 0.01f) return 0;
  dist = max(max(automantle_searchdist - 0.5f, 0.5f), max(fwdSpd,0)*frametime);
  tr = TraceHull(origin, origin + fwd*dist); if (tr.n.z > 0.7f) { end.z -= dot(fwd,tr.n)/tr.n.z*dist; tr = TraceHull(..); }
  if (tr.frac >= 1 || tr.n.z >= 0.7f) { AnticipateMantleLowerWeapon(); return 0; }
  r = FindMantleLedge(fwd, origin, 0, tr, &ledge, &wallN, /*full*/1, &d, &dangle);
  if (r != 2 || !(dangle || pressingFwd)) return 0;
  top = SettleLedgeTop(ledge);                           // +4 steps, <6 slope rise
  if (m_duckState != 2 && !FitsStanding(top, wallN, 0.99f*d)) ForceDuck();
  begin = origin; if (origin.z < top.z - (automantle_height + 0.03125f)) begin = {top.xy, top.z - 80.03125f};
  horiz = min(trFrac*d, d - automantle_mindist) + hullR;  m_traversalRefPos = top - wallN*horiz;
  midFrac = automantle_height / (horiz + automantle_height);
  ComputeMayHang(&m_wallDangleMayHangHere); /* cleared if frac*H < dangle_required_space || !dangle || too far */
  StartTraversal(0, wallN, fwd, -wallN, .., end=top, top, mid, midFrac, begin, 1);
  m_traversalProgress = min(1, (begin.z - (top.z - 80.03125f)) / automantle_height) * midFrac;
  m_wallDangleClimbProgressFloor = (1 - frac) * midFrac;
  vmCycle = 0; ScriptCallback(OnPlayerMantle); return 1;
}
```

### fn_180173f00: `FindMantleLedge(player, fwd, pos, extraH, wallTr, out ledgeTr, out wallN, full, out dist, out dangle)`
Returns 0 = reject, 1 = no ledge, 2 = ledge. Section 2.2-C covers it.
```c
int FindMantleLedge(...) {
  if ((wallTr.contents & 0x100000) || !CanMantleOn(wallTr.ent)) return 0;
  wallN = normalize({wallTr.n.x, wallTr.n.y, 0}); wd = dot(pos - wallTr.end, wallN);
  if (!MantleAngleOK(fwd, wallN)) return 0;
  reach = automantle_forwarddist + wd; d = max(automantle_forwarddist, automantle_mindist) + wd;
  top = extraH + automantle_height + 0.03125f;
  z = pos.z + (float[]){top, 66.5f, 34.5f, 26.5f, 16.5f}[cmd->command_number % 5];
  t = Trace({pos.xy,z}, {pos.xy,z} - wallN*d); pen = t.frac*d;
  if (pen - wd < automantle_mindist) return 1;
  p = lerp(start, end, min(t.frac, reach/d)); ledgeTr = Trace(p, p - up*top);
  if (ledgeTr.n.z < 0.7f) return 1;
  lh = ledgeTr.end.z - pos.z;
  if (lh < 0.03125f || !CanMantleOn(ledgeTr.ent) || (ledgeTr.contents & 0x100000) || Trace(pos, start).frac < 1) return 0;
  if (full) {
    into = dot(vel, wallN) < -1;
    if (lh < stepSize + 0.03125f && !IsWallRunning() && into) { if (m_duckState == 2) return 0; if (!StepTestStartSolid()) return 0; }
    if (lh < 2*pen && into && (fabs(wallTr.n.z) > 0.1f || lh < 2*stepSize))
        if (Trace({pos.xy, pos.z + lh*0.5f}, .. - wallN*pen*0.35f).frac == 1) return 0;
    q = {pos.xy, ledgeTr.end.z - (automantle_dangle_required_space - 5)};
    *dangle = TraceLine(q, q - wallN*2*hullR).frac < 1;
    *dist = min(reach, pen);
  }
  return 2;
}
```

### fn_180174650: `MantleAngleOK(fwd, wallN)`
See 2.3.

### fn_180180780: `CGameMovement::TraversalMove(bool firstTick)` (name from its own Error string)
Runs every tick of a mantle or window traversal: input, progress, animation, positioning, exit (sections 2.4 to 2.7).
```c
void TraversalMove(bool first) {
  isMantle = m_traversalType == 0; base = (state in 7..10) ? state-4 : state;
  rest = isMantle ? (base==BELOW ? automantle_rest_frac_below : automantle_rest_frac) : 0;
  m_traversalReleaseTime = 0; mv->vel = 0; mid = m_traversalMidFrac;
  d = dot(TraversalForwardDir(), mv->wishDir); pushing = d > 0.1f; pulling = d < -0.5f;
  update m_wallDangleLastPushedForward;
  if (isMantle) {
    if (jumpPressed && !forceFallOff && (!pushing || mid <= prog) && !jumpOffTime) jumpOffTime = curtime;
    idle = jumpOffTime == 0; abort = idle && pulling; advance = idle && pushing;
    if (rest < prog && !advance && !abort && !jumpPressed) advance = (mid < prog) ? 1 : lastPushedFwd;
    if (forceFallOff) { advance = 0; abort = 1; }
    (dur, thr) = per-base-state table (2.5);  startFrac = (thr + H)/H * mid; rate = (1 - startFrac)/dur;
  } else { advance = 1; rate = 1/traversal_window_duration; abort = pulling; }
  if (!jumpOffTime) {
    target = 1;
    if (isMantle) { if (!advance) target = (abort || !mayHang || fabs(groundUp.z) <= 0.99f) ? 0 : rest;
                    target = max(target, climbProgressFloor); }
    prog = approach(prog, target, rate*frametime);
    if (isMantle && prog == target && target == rest) prog = target = automantle_rest_frac;
  }
  if (isMantle) disableWeapon = !(gunFrac <= target && gunFrac <= prog);
  if (traversal_anim) { if (first) classify (2.4); choose IDLE/LEVEL/+4 crouch/JUMPOFF; set sequence;
                        cycle = clamp((prog - startFrac)/(1 - startFrac)); approach vmCycle at 1.5x; }
  if (prog == 1) { m_traversalProgress = 1; EndTraversalCleanup(); origin = Xform(m_traversalEnd);
                   if (!OnGround()) SetGround(..); if (pushing) vel = wishDir*MaxSpeed(); return; }
  origin = traversal_anim ? AnimRootMotion(cycle) + blendOffset*(1 - clamp((curtime-blendStart)/0.2f)) : PathPos();
  if (!isMantle) { if (pulling) StopTraversal(); else if (jumpPressed && !first) { StopTraversal(); CheckJumpButton(); } return; }
  if (jumpOffTime && (curtime >= jumpOffTime + automantle_jumpoff_duration || prog > automantle_jumpoff_anim_maxfrac)) {
    StopTraversal(); hasJumped = 1;
    if (prog >= mid) { vel = wishDir*MaxSpeed(); CheckJumpButton(); }
    else { vel = -fwdDir*settings->f_0x230; vel.z = settings->f_0x228; }
    return;
  }
  if ((abort && (prog >= mid || prog >= automantle_backoff_anim_maxfrac)) ||
      ((prog <= climbFloor || (target == 0 && vmCycle <= 0)) && (!pushing || forceFallOff))) { StopTraversal(); return; }
  if (abort || (state == IDLE && (buttons & (IN_DUCK|0x4000000)))) {
    if (base == IDLE) forceFallOff = 1; else if (!forceFallOff) { StopTraversal(); return; } }
  update m_traversalHandAppearTime; if (TraversalInvalid()) StopTraversal(); m_traversalProgress = prog;
}
```

### fn_18016f850: `GetMantleProgressPastRest(player)`
Normalised progress beyond the rest point. Probably used for the camera or viewmodel. [I]
```c
float f(Player *p) {
  if (FUN_18043ee80() != 12 || (unsigned)(p->m_traversalState - 3) >= 10 || p->m_traversalType != 0 || !p->b_0x1d64) return 0;
  float r = (state==BELOW || state==CROUCH_BELOW) ? automantle_rest_frac_below : automantle_rest_frac;
  float x = p->m_traversalProgress - r;
  return (x > 0 && r != 1) ? x / (1 - r) : 0;
}
```

### fn_18016dd00: `CalcWindowHintJump(player, pos, inout vel, ..., permissive, ..., gravScale)`
Window-hint jump assist.
- Finds a window ahead with `FUN_1806079b0`, using FOV and look-ahead ConVars `window_hint_*` and `automantle_height`.
- Picks the first of 4 sample points across the window opening whose required horizontal direction stays within `window_hint_*max_horz_vel_change_dot`.
- Changes `vel.z` within `window_hint_max_vel_change_up/down`, and the `permissive_*` variants when `param_5` is set. The goal is for the ballistic arc (`sv_gravity`) to clear the sill.
- Arcs that reach the sill zone minus `automantle_height` are accepted. [R] The reason, that the mantle covers them, is [I].
- Returns the window entity.

The mantle-related details are [R]. The overall role is [I] from the ConVar names.

---

## 4. ConVars (defaults from convars.tsv; help text from binary strings)

| ConVar | Default | Help / role |
|---|---|---|
| slide_whileInAir | 0 | Allows beginning a slide (including the boost) while still in the air |
| slide_max_angle_dot | 0.6 | Cosine of max angle from forward that you can slide when sprinting |
| slide_boost_cooldown | 2 | Min time between slides before getting another boost |
| slide_step_velocity_reduction | 10 | (no help) speed lost per unit of step-up while sliding |
| slide_auto_stand | 0 | Automatically stand when slide ends |
| automantle_enable | 1 | Enables player auto-mantle behavior |
| automantle_debug | 0 | Debugs player auto-mantle behavior |
| automantle_height | 80.f | Maximum step height to auto-mantle player |
| automantle_height_below | -10 | Mantle height above ledge below which the "below" animation is used |
| automantle_height_level | 10 | ... below which the "level" animation is used |
| automantle_height_above | 30 | ... below which "above" is used and above which "high" is used |
| automantle_searchdist | 5.f | Forward distance within which to look for a ledge to auto-mantle |
| automantle_forwarddist | 30.f | (no help) how far past the wall face the ledge probe reaches |
| automantle_mindist | 18.f | (no help) minimum penetration past the wall face for a ledge |
| automantle_duration_below | 1.11 | Duration of "below" mantle animation |
| automantle_duration_level | 1.0 | Duration of "level" mantle animation |
| automantle_duration_above | 0.5 | Duration of "above" mantle animation |
| automantle_duration_high | 0.35 | Duration of "high" mantle animation |
| automantle_rest_frac | 0.4 | (no help) hang point (progress) |
| automantle_rest_frac_below | 0.3 | (no help) hang point for BELOW |
| automantle_min_frac | 0.1 | (no help) **not read by any function in this dump** |
| automantle_max_frac | 0.5 | (no help) **not read by any function in this dump** |
| automantle_maxangle_push | 50 | Max angle the player can be pushing from the wall normal to auto-mantle |
| automantle_maxangle_view | 50 | Max angle the player can be facing from the wall to auto-mantle |
| automantle_wallrun_maxangle_view | 45 | ... while wall running |
| automantle_jumpoff_duration | 0.1 | (no help) delay before jump-off executes |
| automantle_jumpoff_anim_maxfrac | 0.5 | Maximum fraction of mantle at which jump off animation is played |
| automantle_backoff_anim_maxfrac | 0.7 | Fraction of mantle after which pulling back simply aborts the mantle |
| automantle_dangle_required_space | 60 | (no help) wall space below ledge needed to hang |
| automantle_gun_enable_height | 33 | (no help) weapon re-enabled once this high relative to ledge |
| automantle_cooldown | .25 | Minimum time between mantles |
| traversal_anim | 1 | Enables automantle animation |
| traversal_window_duration | 0.3 | window traversal duration |
| sv_gravity | 750 | World gravity |

Hard-coded constants [R]:

| Constant | Where it is used |
|---|---|
| 0.7 | walkable normal z |
| 0.25 s | slope gate |
| 0.3 | forwardmove "pressing forward" |
| 0.5 | searchdist offset and floor |
| 1/30 s | anticipation lookahead pad |
| {66.5, 34.5, 26.5, 16.5} | probe heights |
| 0.03125 | epsilon |
| 4 / 6 | ledge-top settle step / slope rise |
| 0.35 | slope-guard probe |
| 5 | dangle offset |
| 0.99 | stand-fit trace and level-surface test |
| 0.1 / -0.5 | push / pull dots |
| 1.5 | viewmodel catch-up |
| 0.2 s | blend-out |
| 0.4 s | slide-jump boost strip window |
| 0.85 | slideMaxJumpSpeed lower band |
| -100 | wall-run into-wall velocity limit |

## 5. Confidence summary

**Read directly [R]:**
- Every formula in the pseudocode, including the comparisons, constants, ConVar uses and netvar names/offsets.
- The slide boost, cooldown and long-jump logic.
- The step reduction.
- The stop-speed check.
- The classification thresholds.
- The duration and rate formula.
- The jump-off, back-off and dangle rules.
- The fact that min_frac and max_frac are unused in the indexed logic.

**Inferred [I]:**
- Function names.
- mv field identities (wishDir, viewForward, forwardmove, buttons, hull radius at `mv+0x84`).
- Ground-normal parameter.
- Settings offsets mapped to slide key names (strong: the `.rdata` order matches and the help strings match the uses).
- `0x15cc==3` = forced slide.
- `0x15c8` = duck toggle.
- `m_duckState==2` = fully ducked.
- Path interpolation when `traversal_anim=0`.
- The identities of `settings+0x228/0x230` (jump-off kick) and `settings+0x114` (mantleAngleScale).

**Not determinable from this dump:**
- How slideDecel, slideWantToStopDecel, slideVelocityDecay, slideAccel and slideMaxStopSpeed are applied. The likely locations are callees of fn_180185230 (`FUN_18016ea60`, `FUN_1801696b0`, `FUN_180160230`).
- All per-class `.set` values.

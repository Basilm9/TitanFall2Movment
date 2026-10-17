# Titanfall 2 pilot jump / double jump / skip / air accel / friction (server.dll)

> **Errata:** some statements in this note were superseded by later analysis passes or by the extracted `.set`/weapon files. See [ASSUMPTIONS.md §12](ASSUMPTIONS.md) for the list, and [TITANFALL2_MOVEMENT.md §0](TITANFALL2_MOVEMENT.md) for shipped values. Prefer those sources where they disagree.


Static analysis of Ghidra output in `logic\` and `decomp\`. Function names are my guesses. Confidence tags:

- **[R]** = read directly from code or convars.tsv.
- **[I]** = inferred (from usage, from the stock Source SDK 2013 call pattern, or from the datamap names).
- **[?]** = unknown / not in the dump.

No pilot numbers (jump height, airSpeed, airAcceleration, ...) appear here unless the code or convars.tsv contains them. Per-class values live in the `.set` files in the VPK.

---

## 0. Conventions and struct offsets

`this` = CGameMovement-like object. `this+0x8` = player, `this+0x10` = mv (CMoveData), `this+0x18` = class settings ("settings").
Globals `DAT_180bfbe08` = gpGlobals: `+0x10` curtime, `+0x30` frametime **[I]** (every per-second rate is multiplied by `+0x30`, and timestamps are compared with `+0x10`).

### mv (CMoveData) offsets

| off | meaning | tag |
|---|---|---|
| 0x18 | buttons held (bits 8 = FORWARD, 0x10 = BACK, 0x200 = MOVELEFT, 0x400 = MOVERIGHT, matching the stock IN_* bits) | [I] |
| 0x1c | old buttons (CheckJumpButton sets `|= 2` = IN_JUMP at the end, like stock `m_nOldButtons`) | [I] |
| 0x20 | buttons **pressed this frame**: jump tests `&2` (IN_JUMP) and `&0x10000000` (dodge?); keyboard grace tests `&0x618` | [I] |
| 0x24/0x28/0x2c | forward/side/up move, apparently **normalised** (noclip multiplies them by `sv_maxspeed`) | [I] |
| 0x30.. | a direction vector (view forward, flattened?) used by the wall jump | [?] |
| 0x54 / 0x60 | forward / right vectors (keyboard grace builds a key direction from them) | [I] |
| 0x6c | wish direction vector, magnitude 0..1 (analogue). Used by AirMove, the double-jump redirect and dodge | [I] |
| 0x88 | max speed (WalkMove wishspeed = min(|wish|,1) * mv+0x88), like stock `m_flMaxSpeed` | [I] |
| 0x8c/0x90/0x94 | velocity | [R] |
| 0xb8 | m_outStepHeight (`+= 0.15` on jump, exactly as in stock) | [I] |
| 0xbc.. | m_outWishVel (AirAccelerate adds to it, as stock does) | [I] |
| 0xe4.. | abs origin | [R] |

### Player offsets (named from the datamap in `decomp\fn_18055ba00.c`, so the names are [R])

| off | datamap name | use in jump/air code |
|---|---|---|
| 0x1f08 | m_flTimeLastTouchedGround | class-specific extra jump grace |
| 0x1f0c | m_flTimeLastJumped | set at the end of every jump. Keyboard grace keys off it |
| 0x1f10 | m_flTimeLastLanded | set in SetGroundEntity when landing from the air. Skip keys off it |
| 0x1f14 | m_upDirWhenLastTouchedGround | jump normal used during coyote time |
| 0x1f20 | m_bHasJumpedSinceTouchedGround | blocks coyote jump. Cleared by the replenish function |
| 0x2250 | m_duckState | ==3 and not sliding means no jump. 1..2 on landing means slide start |
| 0x2284 | **m_upDir** | jump impulse direction. Tilts while wallrunning (z<1 means wall jump) |
| 0x22a8 | m_wallRunStartTime | |
| 0x22b0 / 0x22b4 | m_wallRunCount / m_wallRunWeak | zeroed on a ground jump |
| 0x22bc | m_wallrunFrictionScale | wallrun friction |
| 0x22c4 | m_wallrunRetryTime | zeroed by the replenish function |
| 0x23e8 | m_grappleActive | |
| 0x23f0 / 0x23f1 | m_sliding / m_slideLongJumpAllowed | slide-jump logic |
| 0x23f4 / 0x23f8 | m_lastSlideTime / m_lastSlideBoost | |
| 0x2588 | m_onSlopeTime | zeroed on a ground jump |
| 0x258c | m_lastWallNormal | 30 u/s nudge on jump |
| 0x2598/0x2599/0x259c | m_dodgingInAir / m_dodging / m_lastDodgeTime | |
| 0x25a0 | **m_airSpeed** (keyfield "airSpeed") | AirMove wishspeed |
| 0x25a4 | **m_airAcceleration** (keyfield "airAcceleration") | AirMove accel |
| 0x2778 | m_gameMovementUtil (`+0x40` = jump factor, `+0x2c` = surface index for sounds) | like stock `m_pSurfaceData->game.jumpFactor` [I] |
| 0x31c4 | m_pushAwayFromTopAcceleration | added in AirMove |
| 0x22c0 | **m_groundFrictionScale** | multiplies the pilot ground-friction deceleration (logic3 `FUN_1801696b0`) |
| 0x25bc | m_hovering | zeroed on a ground jump. Accelerate does nothing while it is set |
| 0x1f64 | m_iObserverMode (7 = roaming) | |
| 0x27c4 | m_fIsSprinting | |

The following player offsets have no datamap hit, so their names are [I] from usage:

- 0x1bf4: pl.deadflag. The jump and AirAccelerate early-outs mirror stock.
- 0x15d4: air-jump count, used by superjump.
- 0x15e0: superjump power, reset to 100.0.
- 0x1d58: class has doubleJump.
- 0x1d65: class has dodge.
- 0x1d67: stealth sounds.
- 0x2780: m_surfaceFriction. CategorizePosition sets it to 1.0 and to 0.25 when leaving the ground moving up, exactly as stock does.
- 0x1610: step size. AirMove doubles it when settings+0x118 is set.
- 0x1600: an extra multiplier on air accel.

### settings (`this+0x18`) offsets. Read directly; names are [I]

| off | guessed meaning |
|---|---|
| 0x82 (bool) | ground jump allowed |
| 0x94 | **jump height** (fed into sqrt(2 g h)) |
| 0x280 | slide-jump height (used instead of 0x94 when m_sliding) |
| 0x100 / 0x104 | **double-jump height min / max**, lerped by `t = FUN_1805d30c0(player)`. **t is always 0** (logic4; bytes `0f 57 c0 c3` = `xorps xmm0,xmm0; ret`), so only 0x100 is ever used and 0x104 is dead [R] |
| 0x108 | max air (double) jumps. 0 means no double jump |
| 0x10c | double-jump horizontal redirect speed |
| 0x110 (bool) | read in the replenish function. Picks between "set count to max" and "reset count on wallrun" |
| 0x118 (bool) / 0x130 | class flag enabling a second coyote window of length 0x130. The same flag doubles step size in air and skips jump sounds |
| 0x1c8 (bool) | forces air speed to 0 |
| 0x23c / 0x244 / 0x24c / 0x250 / 0x254 | dodge: cooldown, jump height, vertical-dodge height, power drain, speed-keep fraction |
| 0x274 | slide long-jump speed cap |
| 0x228 / 0x22c / 0x230 | wall jump: vertical speed / along-input speed / push-off speed |
| 0x218 / 0x21c / 0x220 / 0x224 | wallrun: hang flag, vertical speed, back-input speed, friction |
| 0x98 / 0x9c | WalkMove: side-move scale / backward scale |
| 0x2c8 / 0x2cc | air speed / air accel while grappling |

The same struct is also returned by `FUN_180556080(player+0x1b98)`: the double-jump code, the decoy and the replenish function read 0x100/0x104/0x108/0x110 through it **[I]**.

### decomp\fn_180553b60.c (class-settings parser)

This parser fills a **different** struct, made of 16-byte slots: a type byte at +0, a "present" word at +2, and the value at +8.

- Floats: health, dodgeDuration, dodgeSpeed, dodgePowerDrain, wallrunAccelerateVertical/Horizontal, wallrunMaxSpeedHorizontal, wallrun_timeLimit, wallrun_hangTimeLimit, grapple_power_*, slideFOV*, airSlowMoSpeed, sharedEnergy*.
- Bools: `wallrun`@0xf0, `doubleJump`@0x110, `mantlePitchLeveling`@0x140, `dodge`@0x150, `sprint`@0x160, `stealthSounds`@0x170, `hoverEnabled`@0x180.

Its layout does not line up with the movement-settings reads. For example, the movement code reads 0x100/0x104/0x108 as floats, while this struct has `wallrunAdsType` at 0x100 and a type byte at 0x108. The only coincidence is the char read at 0x110, which is `doubleJump` here. So I do **not** tie these offsets together **[I]**.

airSpeed and airAcceleration are not in this parser. They are player netvars (0x25a0/0x25a4) with keyfields "airSpeed"/"airAcceleration" **[R]**.

---

## 1. Pipeline overview

**Tick order.** In FullWalkMove `FUN_18016a9f0` **[R]**, each tick runs:

1. StartGravity (inline half-step: `vel += gravDir * 0.5 * entGrav * sv_gravity * dt * timescale * wallrunGravScale`).
2. CheckVelocity.
3. `FUN_180176190`.
4. **CheckJumpButton `FUN_180172870`**, called every tick; it tests the pressed bits itself.
5. Wallrun, mantle and other helpers.
6. If there is no ground entity, **AirMove `FUN_180161170`**. Otherwise **WalkMove/WallrunMove `FUN_180185230`**.
7. CategorizePosition `FUN_180163830`.
8. FinishGravity `FUN_1801688d0`.
9. While grounded, strip the velocity component along m_upDir.

Because the jump calls SetGroundEntity(NULL) before the move step, **a jump tick gets no ground friction**, as in stock Source **[I from call order]**.

### Jump type selection (CheckJumpButton)

- **Ground jump.** You need a ground entity **[R]**.
- **Coyote time.** In the air, if `TimeSinceLeftGround < jump_graceperiod` (0.2 s) and `!m_bHasJumpedSinceTouchedGround`, the jump counts as a ground jump. The impulse direction is then `m_upDirWhenLastTouchedGround`. `FUN_1805d10e0` = time since last touched ground **[I]**.
- **Second, class-specific coyote window.** If `settings.b118 && curtime - m_flTimeLastTouchedGround <= settings.f130 && !hasJumped`, the jump also counts as a ground jump **[R]**.
- **Double jump.** In the air (with no grace and no dodge), if the class has the doubleJump flag (player+0x1d58), the jump becomes a double jump. Without the flag, the press is treated as a **dodge** attempt **[R]**.
  - **"superjump" is verified to be the internal name of the double jump [R]:**
    - CanDoubleJump `FUN_1805cda30` checks `power >= superjump_min_power_use && doubleJumpFlag && count < settings.f108`.
    - The double-jump branch uses `superjump_min_height_fraction` and `superjump_min/max_power_use`, and increments the count.
    - The reset function uses `superjump_limitreset_onwallrun` and `superjump_powerreset_onground`.
  - Script natives `IsDoubleJumping` and `ConsumeDoubleJump` also exist (decomp `fn_18058ecb0`).
- **Wall jump.** The jump becomes a wall jump when the chosen jump normal has `z < 1.0` and it is not a dodge. m_upDir tilts while wallrunning, so this is the wallrun kick-off `FUN_180173a00` **[R code, I meaning]**.
  - Coyote time stores m_upDir at last ground contact. A jump pressed within 0.2 s of leaving a wall should therefore still be a wall jump **[I]**.
- **Duck block.** `m_duckState == 3 && !m_sliding` returns false, so there is no jump **[R]**.

### Skip (bunny-hop landing) **[R]**

- SetGroundEntity `FUN_18016b890` writes `m_flTimeLastLanded = curtime` when you land from the air.
- If a normal (non-double, non-dodge) jump happens while `curtime - m_flTimeLastLanded < skip_time` (0.1 s) and you are not wallrunning (`FUN_1805d5230` = IsWallRunning **[I]**), the jump is a **skip**:
  - `jumpHeight *= skip_jump_height_fraction` (0.75). Vertical speed is therefore `sqrt(0.75) ≈ 0.866×` normal.
  - If horizontal speed `> skip_speed_retain` (450): `hspeed = max(hspeed - skip_speed_reduce, skip_speed_retain)`, i.e. you lose 12 u/s per skip but never drop below 450 through this clamp. At or below 450, speed is not touched. **A skip never adds speed.**
  - Sound: `"%s.skip.%s"` if `skip_sounds` (default 0), otherwise `"%s.jump.%s"`.
- Double-jump replenish happens on landing, in SetGroundEntity with a ground entity. `ReplenishAirAbilities FUN_1805e2670` is called when any of these holds:
  - `skip_replenish_double_jump` (default 1; help text: "Whether the player can double jump after skipping"),
  - `curtime - m_flTimeLastLanded > skip_time`,
  - IsWallRunning.

  With the default of 1, the double jump comes back immediately on touchdown. With 0, it only comes back after you have stood on the ground for more than skip_time **[R]**.
- Friction during the skip window: WalkMove (with friction) runs on any tick you are grounded and not jumping. A jump on the first grounded tick skips friction entirely **[I]**. Pilot ground friction (logic3 `FUN_1801696b0`, section 2) is a constant deceleration that **does not touch the part of your velocity along your input direction, up to wishspeed**. Holding a direction while landing therefore loses nothing to friction on the speed you are steering. Only the excess above wishspeed, and the off-axis part, decays **[R]**.

### Keyboard grace **[R]**

This runs in AirMove. For the details see `FUN_180161170` below.

- It applies when a movement key is pressed this frame within `jump_keyboardgraceperiodmax` (0.5 s) of m_flTimeLastJumped.
- The horizontal velocity direction is rotated toward the key direction while its magnitude is kept.
- The blend weight is `jump_keyboardgrace_strength` (0.7), full up to `jump_keyboardgraceperiodmin` (0.2 s) and linearly down to 0 at 0.5 s.
- The per-frame change is capped at `jump_keyboardgrace_max` (0.7) × `FUN_1805d2b80(player,0)` = moveSpeedScale × `m_perPosValues[0].f4` (player+0x1db0, the sprint max speed) **[R body (logic4), I "sprint"]**.

### Slide jump **[R]**

If you jump while m_sliding:

- The jump height is settings.f280.
- If m_slideLongJumpAllowed: when you jump within 0.4 s of m_lastSlideTime (and duckState != 2), m_lastSlideBoost is removed from your speed.
- Then, if the pre-removal horizontal speed lies in `(0.85*cap, cap)` with cap = settings.f274, your speed is set to exactly `cap`.
- m_slideLongJumpAllowed is then cleared.

---

## 2. Functions

### FUN_180172870: `CGameMovement::CheckJumpButton` (TF2) **[name I, body R]**

```c
bool CheckJumpButton() {
  if (player.deadflag || validHandle(player+0x378)) return false;      // stock: deadflag / waterjump checks
  if (player.b15d8) { player.b15d8 = 0; return false; }                 // one-shot jump block
  if (mv.flags & 4) return false;
  if (!(mv.pressed & IN_JUMP)) {
     mv.oldbuttons &= ~IN_JUMP;
     if (!(mv.pressed & 0x10000000)) return false;
     isDodge = true;
  } else isDodge = false;
  if (player.b15c8) { player.b15c8 = 0; if (GetGroundEntity() && !m_sliding) return false; }

  jumpNormal = &m_upDir;
  if (!GetGroundEntity() || ground->vfunc_0x298()) {
     if (TimeSinceLeftGround() < jump_graceperiod && !m_bHasJumpedSinceTouchedGround)
        { groundJump = true; jumpNormal = &m_upDirWhenLastTouchedGround; }   // coyote
     else groundJump = false;
  } else groundJump = true;
  if (settings.b118 && curtime - m_flTimeLastTouchedGround <= settings.f130 && !m_bHasJumpedSinceTouchedGround)
     groundJump = true;

  isDouble = false;
  if (!groundJump && !isDodge) { if (player.b1d58 /*doubleJump*/) isDouble = true; else isDodge = true; }
  if (m_duckState == 3 && !m_sliding) return false;

  if (isDouble) {
     if (settings.f108 == 0) return false;
     if (!CanDoubleJump()) {                       // FUN_1805cda30
        mv.oldbuttons |= IN_JUMP;
        if (FUN_1805e25f0(player) || superjump_fail_sound_when_jump_limit) PlaySound(settings+0xa8c);
        if (FUN_1805e25f0(player)) FUN_1805e2040(player);   // drain on fail? (index lists superjump_drain_power_onfail) [I]
        return false;
     }
     FUN_180160550(player);                         // double-jump view punch ("viewpunchDoubleJumpX/Y/Z") [R, logic3]
  } else if (isDodge) {
     if (m_duckState != 0 || (m_playerFlags & 0x10) || !player.b1d65 /*dodge*/) return false;
     if (m_lastDodgeTime + settings.f23c > curtime || player.f1d34 < player.f15dc) { PlaySound(settings+0xb4c); return false; }
     if (!dodge_no_movement_forward_enable && |mv.wishdir|^2 <= 1e-7) return false;
     FUN_1805e2170(player, classMods.dodgePowerDrain);   // DrainPower(0x1d34) [R, logic8]
     FUN_1805e2150(player, settings.f250 /*dodgePowerDelay*/ * player.f1608);   // DelayPowerRegen; see ground_misc 3.5
  } else {
     if (!settings.b82) return false;
     m_onSlopeTime = 0; FUN_180160690(player);      // jump view punch ("viewpunchJumpX/Y/Z") [R, logic3]
  }

  SetGroundEntity(NULL);                           // FUN_18016b890(this,0)
  groundFactor = m_gameMovementUtil ? m_gameMovementUtil->f40 : 1.0;   // surface jumpFactor

  if (m_sliding && m_slideLongJumpAllowed && (h2 = vx*vx+vy*vy) > 0) {
     hs = sqrt(h2); ns = hs;
     if (m_duckState != 2 && curtime - 0.4 < m_lastSlideTime) {
        ns = max(hs - m_lastSlideBoost, 0); vel *= ns/hs;           // remove slide boost
     }
     cap = settings.f274;
     if ((0.85*cap)^2 < h2 && h2 < cap^2) vel *= cap/ns;           // snap to cap
     m_slideLongJumpAllowed = 0;
  }
  isWallJump = (jumpNormal->z < 1.0) && !isDodge;

  if (isDouble) {
     t  = FUN_1805d30c0(player);                                  // ALWAYS 0.0 (stub, logic4 + raw bytes) [R]
     H  = lerp(settings.f100, settings.f104, t);
     rise = vel.z >= 0 ? vel.z*vel.z / (2*sv_gravity) : 0;        // height still to gain
     h  = H * superjump_min_height_fraction;
     if (h + rise < H) { vel.z = 0; h = H; }
     ConsumePower(player, lerp(superjump_min_power_use, superjump_max_power_use, t));  // FUN_1805e2040
     sounds (settings+0x110c.. or "%s.stealth"); airJumpCount++ ; animEvent(5);
  } else if (isDodge) {
     h = (dodge_vertical_enable && |wishdir| < dodge_vertical_threshold) ? settings.f24c /*vertical dodge*/ : settings.f244;
     animEvent(6); FUN_18017f9c0(...);
  } else {
     h = m_sliding ? settings.f280 : settings.f94;
     isSkip = false;
     if (curtime - m_flTimeLastLanded < skip_time && !IsWallRunning()) {
        isSkip = true; h *= skip_jump_height_fraction;
        if (|vel.xy| > skip_speed_retain)
           vel.xy *= max(|vel.xy| - skip_speed_reduce, skip_speed_retain) / |vel.xy|;
     }
     sound isSkip && skip_sounds ? "%s.skip.%s" : "%s.jump.%s"; animEvent(isWallJump ? 7 : 4);
  }

  v = sqrt(2 * sv_gravity * h);                    // DAT_180873b18 f=2.0
  m_bHasJumpedSinceTouchedGround = 1;
  if (isWallJump) WallJump(this, jumpNormal);      // FUN_180173a00: uses settings.f228/22c/230, not v
  else {
     if (m_grappleActive && player.b23c9) v *= grapple_jumpFrac;
     m_wallRunCount = 0; m_wallRunWeak = 0; m_hovering = 0;
     if (dodge_vertical_in_air || !isDodge || groundJump)
        vel += groundFactor * m_upDir * v;         // NOTE: always m_upDir, additive
     if (dot(mv.wishdir, m_lastWallNormal) < 0) vel -= m_lastWallNormal * 30.0;
     if (isDouble || isDodge) {                    // horizontal redirect
        w = vertDodge ? 0 : wishdir.xy (dodge: normalised, or view-forward if no input);
        w *= isDouble ? settings.f10c : DodgeSpeed();          // FUN_1805d2210
        keep = (|w| < |vel.xy|) ? 1 - |w|/|vel.xy| : 0;
        if (vertDodge) keep *= dodge_vertical_horzspeedscale;
        if (isDodge)   keep += (1-keep) * settings.f254;
        vel.xy = keep*vel.xy + w;
        if (isDodge) { set m_lastDodgeTime/m_dodgingInAir/m_dodging; goto done; }
     }
     FUN_1801878d0(this, 0, 1, 1.0);               // wrapper -> FUN_18016dd00(origin, vel): automantle probe [R, logic3]
  }
done:
  FUN_180166e80(this);                 // slide auto-stand
  FinishGravity(); CheckVelocity();
  mv.outStepHeight += 0.15;
  m_flTimeLastJumped = curtime;
  mv.oldbuttons |= IN_JUMP;
  return true;
}
```

Differences from stock SDK 2013 CheckJumpButton:

- **Jump height.** Stock uses `sqrt(2*g*21)` (`GAMEMOVEMENT_JUMP_HEIGHT` = 21) and sets the velocity instead of adding when ducking. TF2 uses a per-class height and **always adds** along m_upDir **[R]**.
- **Re-press check.** Stock rejects the jump if `m_nOldButtons & IN_JUMP` (no pogo). TF2 uses a pressed-this-frame mask instead **[I]**.
- **Additions.** Stock has none of these: coyote time, double jump (with power and count), dodge, skip, slide long-jump, wall jump, the last-wall-normal nudge, surface jump factor via m_gameMovementUtil, and the grapple jump fraction.

### FUN_18016b890: `CGameMovement::SetGroundEntity(trace_t*)` **[I name, R body]**

```c
newG = tr ? tr->m_pEnt : NULL; oldG = GetGroundEntity();
if (oldG != newG) {
  if (newG) {
     oldBase = m_vecBaseVelocity; newBase = GroundVelocityAt(newG, origin);
     (optional plane fix-up via a short trace, cached in this+0x54..0x60)
     vel += oldBase - newBase;  m_vecBaseVelocity = newBase;   // (debug msg "setting base velocity")
     if (!oldG) m_flTimeLastLanded = curtime;                  // landing timestamp for skip
     if (m_duckState in {1,2}) FUN_180183180(this, tr->plane);   // slide start on crouched landing
  }
  player->SetGroundEntity(newG);
}
if (newG) {
  if (skip_replenish_double_jump || curtime - m_flTimeLastLanded > skip_time || IsWallRunning())
     ReplenishAirAbilities(player);              // FUN_1805e2670
  if (!tr->DidHitWorld()) MoveHelper()->AddToTouched(tr, vel);
  if (movetype != 9) vel -= dot(vel, m_upDir) * m_upDir;     // stock: vel.z = 0
}
```

### FUN_1805e2670: `ReplenishAirAbilities` / reset double jump **[I name, R body]**

```c
m_bHasJumpedSinceTouchedGround = 0; m_wallrunRetryTime = 0;
if (m_upDir.z < 1.0) {                       // tilted = on a wall
   if (!settings.b110) airJumpCount = (int)settings.f108;          // wall contact uses up air jumps
   else if (superjump_limitreset_onwallrun && airJumpCount) airJumpCount = 0;
} else {                                     // real ground
   player.i18e8 = 0; airJumpCount = 0; m_wallRunWeak = 1;
   if (superjump_powerreset_onground) superjumpPower(0x15e0) = 100.0;
}
```

The meaning of settings.b110 is [?]. The branch logic is [R].

### FUN_1805cda30: `CanDoubleJump` **[I name, R body]**

```c
return superjumpPower >= superjump_min_power_use && player.b1d58 && (float)airJumpCount < settings.f108;
```

### FUN_180161170: `CGameMovement::AirMove(dt)` **[I name, R body]**

```c
w = mv.wishdir; mag = |w|; wishdir = w / max(mag, eps); mag = min(mag, 1);
if (m_grappleActive && b23c9) { spd = settings.f2c8; acc = settings.f2cc; }
else { spd = settings.b1c8 ? 0 : m_airSpeed; acc = m_airAcceleration; }
wishspeed = FUN_1805d2510(player) * FUN_18016e9e0(player) * mag * spd;
   // FUN_18016e9e0 = weapon/ADS move scale: lerp(1, activeObj.f1484, FUN_1805d3bf0(player)) when not wallrunning [R body, I meaning]
   // FUN_1805d2510 = moveSpeedScale (logic4): FUN_1805d25d0() * m_playerMoveSpeedScale(0x1e90)
   //                  * (1 - FUN_180222070(p)) * FUN_1802220b0(p)  [R];  + Denuvo trap (see section 3)
AirAccelerate(wishdir, wishspeed, acc * player.f1600, dt);            // FUN_180160f40

// keyboard grace
if ((mv.pressed & (FWD|BACK|LEFT|RIGHT)) && (dtJ = curtime - m_flTimeLastJumped) < jump_keyboardgraceperiodmax) {
   kdir = normalize((held(FWD)-held(BACK))*mv.fwd + (held(RIGHT)-held(LEFT))*mv.right);
   hs = |vel.xy|; vdir = vel.xy / hs;
   a = clamp((dtJ - periodmax)/(periodmin - periodmax), 0, 1) * jump_keyboardgrace_strength;
   nd = lerp(vdir, kdir.xy, a); if (|nd| > 0.01) nd = nd/|nd| * hs;     // keep speed
   d = nd - vel.xy; cap = FUN_1805d2b80(player,0) * jump_keyboardgrace_max;
   if (|d| > cap) d *= cap/|d|;
   vel.xy += d;  FUN_18016dd00(...)   // automantle probe
}
FUN_1801b8730(player, &vel);
vel += dt * m_pushAwayFromTopAcceleration;
m_lastWallNormal = 0;
if (settings.b118) stepSize *= 2 (restored at end);
TryPlayerMove-style trace; on hit store m_lastWallNormal and run wallrun checks (FUN_1801835c0 ...)
```

### FUN_180160f40: `CGameMovement::AirAccelerate(wishdir, wishspeed, accel, dt)` **[I name, R body]**

```c
if (player.deadflag) return;
add = wishspeed - dot(vel, wishdir);
if (add > 0) {
   a = min(add, accel * dt);                 // NO *wishspeed, NO *surfaceFriction, NO 30-unit cap
   vel += a*wishdir; mv.outWishVel += a*wishdir;
} else {                                     // already at/over wishspeed along wishdir
   cap2 = max(|vel|^2, wishspeed^2);
   vel += wishdir * dt * player_extraairaccelleration;
   if (|vel|^2 > cap2) vel *= sqrt(cap2/|vel|^2);   // steer only, never gain speed
}
```

The convar's help text says it is "extra air acceleration given to players, even if they're already at max speed. Helps to start wall running" **[R]**.

Stock SDK 2013 for comparison:

```c
wishspd = min(wishspeed, 30);
add = wishspd - v·wishdir;
if (add <= 0) return;
accelspeed = accel * wishspeed * dt * m_surfaceFriction;   // accel = sv_airaccelerate (10)
```

TF2 drops the 30 cap, so the cap is the class airSpeed. It also makes accel an absolute rate (u/s²), and adds the extra-accel steering branch.

**sv_airaccelerate is not referenced by any dumped function** **[R for the dump only]**.

### FUN_180185230: `WalkMove / WallrunMove(dt)` **[I name, R body]**. Grounded and wallrunning both land here.

- **Ground path** (not wallrunning) **[R]**:
  - It strips the velocity along m_upDir.
  - It builds the wish vector from mv.forward/sidemove. Sprinting divides by `FUN_1805d2fa0`. Duck states 1..2 rescale. The vector is clamped to unit length; when shorter it is shaped by `(2-|w|)*|w|`. Then side × settings.f98, and back × settings.f9c.
  - The wish vector is projected onto the ground plane. While m_sliding, the component along the velocity is removed.
  - `wishspeed = min(|w|,1) * mv.f88`.
  - It then calls:
    - `accel = FUN_18016ea60(this)`;
    - **`FUN_1801696b0(this, wishdir, wishspeed)` = pilot ground Friction** **[R body (logic3), name I]**. See below;
    - **`FUN_180160230(this, wishdir, wishspeed, accel)` = Accelerate** **[R body (logic3), name I]**. It is also used by noclip and observer. See below.
  - On a moving ground platform it blends `0.25*wish + 150*dir`.
- **Wallrun path** **[R]**. The friction here is exponential, with no stop speed:

```c
k = m_wallrunFrictionScale * settings.f224 * m_surfaceFriction * ramp;  // ramp: 0 → 1 linearly over the 0.5 s before player.f1f40 (offset 8000)
if (settings.b218) k = 10.0;
newspeed = (speed - speed*dt*k) * HangStopFactor();        // FUN_180186bc0
```

  This is followed by slip-gravity blending (wallrun_slipstarttime, wallrun_slipduration, wallrun_noInputSlipFrac, wallrun_hangslip*), vertical accel (settings.f21c, wallrun_upwardAutoPush) and horizontal accel (settings.f220 when looking away, otherwise player.f1d44).
- Slide: if m_sliding and speed < settings.f26c, `FUN_180166e80` (auto-stand) is called.

### logic3 FUN_1801696b0: pilot ground **Friction(wishdir, wishspeed)** **[R body, I name]**

```c
v = mv.vel; d = dot(wishdir, v);
if (d <= 0) { keep = 0; rest = v; }                         // nothing protected
else {
   f = min(1, d * wishspeed / |v|^2);                        // aligned case: f*d = min(|v|, wishspeed)
   dir = wishdir;
   if (dot(dir, m_lastWallNormal) < 0) {                     // input pushes into the last-touched wall
      dir += m_lastWallNormal * (1/128 - dot(m_lastWallNormal, dir));  // clip off wall (overbounce 0.0078125)
      dir = normalize(dir);
      keep = dir * max(dot(dir, v), 0) * f;
   } else keep = dir * (f * d);
   rest = v - keep;
}
r = |rest|;
newr = max(r - GroundDecel(this) /*FUN_18016eda0, logic4 — below*/ * m_surfaceFriction * m_groundFrictionScale * dt, 0);
if (m_sliding)                                               // slide friction: exponential
   newr *= pow(settings.f260, (1 - FUN_180221c20(player, DAT_180c91564) * 0.75) * dt);   // FUN_1807dc0b0 = pow [I]
if (newr < r) {
   s = newr / r;
   vel = rest*s + keep;
   mv.outWishVel -= (1 - s) * rest * s;   // as written in the code
}
```

How this differs from stock SDK 2013 Friction, which uses `drop = max(speed, sv_stopspeed) * sv_friction * surfaceFriction * dt` on the full speed:

- **No sv_friction, no sv_stopspeed, no edge friction.** None of them is read here **[R]**.
- **Constant deceleration.** It subtracts a fixed amount per second, so it is not proportional to speed and there is no stop-speed floor. The amount is `GroundDecel()` × m_surfaceFriction × m_groundFrictionScale. GroundDecel is `FUN_18016eda0`; logic4 has its body, shown below.

### logic4 FUN_18016eda0: **GroundDecel()**, the friction base D **[R body, I name]**

```c
D = -1;
if (|vel|^2 (3D) > walkSpeed^2)  D = (P.f58 >= 0) ? P.f58 : (P.f54 >= 0 ? P.f54 : -1);   // above-walk-speed decel
     // walkSpeed = FUN_1805d2bd0(player, this.i28) = moveSpeedScale * m_perPosValues[i].f0 (player+0x1dac + 8*i)
if (m_sliding) {
   if (player.i15cc == 3) return 0;                                          // no friction (and no ramp)
   if ((!player.b15c8 && !(mv.buttons & 4 /*IN_DUCK*/)) || dot(mv.wishdir, vel) < 0)
        D = settings.f25c;                                                   // slide friction: crouch released / pushing against motion
   else D = settings.f258;                                                   // slide friction: holding crouch
}
if (D < 0) { D = P.f4c; if (D < 0) D = 0.6 * P.f48; }                        // default: P.f4c, else 60% of normal accel
ramp = 1; if (curtime < player.f1f40) ramp = clamp((curtime - (player.f1f40 - 0.5)) * 2, 0, 1);
return ramp * D * player.f1600;
```

Notes on GroundDecel:

- P = `*(this+0x20)` is the same parameter block as in GetGroundAccel (f44 start-speed threshold, f48 normal accel, f4c decel, f50 start accel, f54 above-walk accel/decel, f58 above-walk decel) **[R offsets, I names]**.
- **If no decel is defined, friction falls back to 0.6 × the normal ground accel** **[R]**.
- `player+0x1f40` is a timestamp. Friction is 0 until 0.5 s before it, then ramps linearly to full at it. The same ramp scales wallrun friction. No datamap name was found **[R mechanism, ? name]**.
- player.f1600 scales both accel and decel **[R]**.
- No sv_friction, sv_stopspeed or sv_edgefriction here either **[R]**.
- **Input-aligned velocity is protected.** The component of velocity along wishdir, up to wishspeed, is exempt from friction. Running at or below wishspeed in your input direction takes no friction. Speed above wishspeed, off-axis drift, and all velocity when there is no input decay at the constant rate.
- **Slide friction** is a separate exponential factor `settings.f260^(k·dt)` while m_sliding.

### logic3 FUN_180160230: **Accelerate(wishdir, wishspeed, accel)** (ground, noclip, observer) **[R body, I name]**

```c
if (player.deadflag || m_hovering) return;
cap2 = max(|v|^2, wishspeed^2);
add = wishspeed - dot(v, wishdir);
if (add <= 0) return;
a = min(add, accel * dt * m_surfaceFriction);   // stock: accel * dt * wishspeed * surfaceFriction
v += wishdir * a;
if (|v|^2 > cap2) v *= sqrt(cap2 / |v|^2);      // never raises |v| above max(old |v|, wishspeed)
```

Differences from stock:

- accel is an absolute u/s² rate (no `* wishspeed`).
- A no-gain clamp is added: you cannot use ground accel to exceed max(current speed, wishspeed). This removes the "strafe-accelerate on the ground" gain.

Observer mode passes `sv_specaccelerate` (1000), and noclip passes its `maxaccel` argument. Both are therefore absolute u/s² rates as well **[R]**.

### logic3 FUN_18016ea60: **GetGroundAccel()** **[R body, I name]**

```c
h2 = vel.x^2 + vel.y^2;
if (m_sliding) a = settings.f264;                                     // slide accel
else if (P.f54 >= 0 && h2 > (FUN_1805d2bd0(player, this.i28) - 1)^2) a = P.f54;   // above speed A
else {
   B = FUN_1805d2bb0(player, P);
   a = (h2 >= B^2 || P.f50 < 0) ? P.f48 : P.f50;                      // below B: f50 (start accel), else f48
}
return a * player.f1600;
```

- `P = *(this+0x20)` is a per-stance or per-move-type parameter block, indexed by `this+0x28`. It may be the player's m_perPosValues (0x1dac) **[I]**.
- So ground accel has three tiers, chosen by horizontal speed: start accel (f50), normal (f48), above-max (f54). A negative entry disables its tier.
- Tier thresholds (logic4) **[R]**:
  - `FUN_1805d2bd0(player, i)` = moveSpeedScale × `m_perPosValues[i].f0` (player+0x1dac + 8i, walk/max speed).
  - `FUN_1805d2b80(player, i)` = moveSpeedScale × `m_perPosValues[i].f4` (player+0x1db0 + 8i, sprint speed; the VPhysics shadow picks 0x1db0 when m_fIsSprinting) **[I "sprint"]**.
  - `FUN_1805d2bb0(player, P)` = moveSpeedScale × `P.f44`.
  - So the tiers are: below P.f44 → start accel f50; from P.f44 up to walk speed (perPos.f0 − 1) → f48; above walk speed → f54. The index i = `this+0x28` (stance / posture) **[I]**. player.f1600 multiplies both ground and air accel, so it is a global accel scale **[I]**.

### logic3 FUN_18016e9e0: ADS / weapon move-speed scale **[R body, I meaning]**

```c
obj = FUN_1800e37d0();                       // active weapon? [I]
if (obj && FUN_1805e6ba0(player) && !IsWallRunning())
   return lerp(1, obj.f1484, FUN_1805d3bf0(player));
   // FUN_1805d3bf0 (logic4): a 0..1 fraction from (weaponTime - player.f15a0)/duration, rising if player.b1598 else falling,
   // offset by player.f159c — consistent with an ADS-in/out fraction [R body, I meaning]
return 1;
```

This scales wishspeed in both AirMove and the wallrun path.

### FUN_18015fe70: `IsAtEdge` (edge-friction probe) **[I name, R body]**

```c
if (!sv_use_edgefriction) return false;               // default 1
vh = vel - dot(vel, m_upDir)*m_upDir;  if (|vh| == 0) return false;
start = absOrigin + 16 * vh/|vh| + m_upDir;           // 16 units ahead, 1 unit up
end   = start - 49 * m_upDir;
TraceHull(start, end, playerMins/Maxs (duck hull if ducked), mask, filter);
return tr.fraction == 1.0;                            // nothing below → standing at an edge
```

HL1's `PM_Friction` used a **point** trace from origin + 16 units ahead at foot height, going 34 units down. When the trace found nothing it set `friction = sv_friction * sv_edgefriction`. SDK 2013 CGameMovement has no edge friction at all.

**Resolved (logic8): this function has no caller, and `sv_edgefriction` is never read. Edge friction is dead code in TF2's server.dll [R].** The evidence is in `logic8\refs2.txt` and `logic8\rel.txt`:
- **Ghidra references to 0x18015fe70:** only the `.pdata` RUNTIME_FUNCTION entry (0x1823cacd0) and three chained-unwind records in `.rdata` (0x180a25fe8/ffc/600c, each `{0x15fe70, 0x15ffbb, …}`). There is no CALL, JMP, LEA or data pointer.
- **Raw byte scans of every initialized block:**
  - no 8-byte absolute pointer to 0x18015fe70, so it is in no vtable or function table;
  - no 4-byte RVA other than the unwind and pdata entries;
  - over `.text`, no rel32 at any byte offset that resolves to it (k = 0..4 trailing-immediate bytes). Using the same method, the one `sv_use_edgefriction` read inside this function is found.
- **The scans do work.** On the `sprint_powerdrain` ConVar, the same raw-pointer scan finds its `.data` reference (0x180b94c68).
- **`sv_edgefriction` (ConVar object 0x180c046e0):** the only references are its registration (`LEA` at 0x180838b12) and a `LEA` at 0x18085cab0 outside any function, presumably the static destructor stub [I]. Nothing reads its value pointer at +0x38 (0x180c04718), whether by Ghidra xref or by rel32 brute force.
- **`sv_use_edgefriction` (0x180c04380):** its only value read is inside `FUN_18015fe70` itself (0x18015fe82).
- So the probe is an orphaned leftover. Pilot, titan and NPC ground friction never get an edge multiplier.

### FUN_180169ae0: `CGameMovement::FullNoClipMove(factor, maxaccel)` **[I name, R body]**

```c
maxspeed = factor * sv_maxspeed;
wishvel = fwd*(factor*fmove*sv_maxspeed) + right*(factor*smove*sv_maxspeed); wishvel.z += factor*upmove;
wishspeed = |wishvel|; if (wishspeed > maxspeed) { wishvel *= maxspeed/wishspeed; wishspeed = maxspeed; }
if (maxaccel > 0) {
   Accelerate(wishdir, wishspeed, maxaccel);            // FUN_180160230
   spd = |vel|; if (spd < 1) { vel = 0; return; }
   control = max(spd, maxspeed*0.25);                   // DAT_18089f368 d=0.25
   drop = control * sv_friction * m_surfaceFriction * dt;
   vel *= max(spd - drop, 0)/spd;
} else vel = wishvel;
origin += vel*dt; if (maxaccel < 0) vel = 0;
```

This is the same as stock, except that fmove and smove are multiplied by sv_maxspeed, which implies normalised inputs **[I]**.

### FUN_180169f70: `CGameMovement::FullObserverMove` **[I name, R body]**

- Only runs when m_iObserverMode == 7 (roaming).
- If `sv_specnoclip` (1) is set, it calls `FullNoClipMove(sv_specspeed, sv_specaccelerate)` (5.0, 1000.0).
- Otherwise:
  - `factor = sv_specspeed * sv_maxvelocity`, halved if buttons & 0x8000.
  - wishspeed is clamped to sv_maxvelocity.
  - Then: `Accelerate(…, sv_specaccelerate)`, friction `spd*sv_friction*dt` (no stop speed), CheckVelocity, and a TryPlayerMove-like step.
- Close to stock. Spectator only.

### FUN_180482be0: `CBaseEntity::PhysicsStepRunTimestep`-like (non-player MOVETYPE_STEP entities) **[I name, R body]**

```c
if (onGround(flags&1)) {
   speed = |absVel|  (3D — stock uses 2D xy speed);
   control = max(speed, sv_stopspeed);
   newspeed = max(speed - control * dt * GetFriction() * sv_friction, 0);
   absVel.xy *= newspeed/speed;
}
absVel += baseVel; step move...; absVel -= baseVel;
```

This is the **only** dumped use of `sv_stopspeed`. It is entity physics (NPCs, props), not pilots.

### FUN_180164bc0: `CGameMovement::CheckVelocity` **[I name, R body]**

- It skips work unless the velocity has changed since the last check (`|Δv|² >= 0.001`).
- It zeroes NaN velocity and origin components.
- It clamps origin to ±65536 (1<<16) and logs "Got outside the map".
- If any velocity axis exceeds ±`sv_maxvelocity` (12100), the **whole vector is scaled** so that axis equals the limit. Stock clamps only that axis, which can change direction.

### FUN_1801688d0: `FinishGravity(dt)` **[I name, R body]**

```c
vel += gravDir(DAT_180b0e9b0..b8) * (entGravity * sv_gravity * dt * 0.5 * wallrunGravityScale * grappleScale);
```

wallrunGravityScale comes from `FUN_180186c70`, which uses wallrun_gravityRampUpTime and the time since m_wallRunStartTime. The jump calls FinishGravity and CheckVelocity at its end, as stock does.

### FUN_1801c40d0: `CreateHoloPilotDecoy` ("player_decoy") **[I name, R body]**

The decoy copies the player's state. In its double-jump branch it recomputes exactly the double-jump formula: `H = lerp(f100, f104, FUN_1805d30c0)` (t = 0, so H = f100), the `superjump_min_height_fraction` test, and `vz' + sqrt(2*sv_gravity*h)`. This independently confirms that settings.f100/f104 are the double-jump heights **[R code, I meaning]**.

### FUN_1801750f0: melee **lunge** movement **[I name, R body]**

- Lerps the position toward the target, with velocity = Δpos/dt.
- Clamps speed to `FUN_1805d24d0` (lunge speed) and to player.f2960.
- Applies a half-step of gravity (`0.5*g*dt*scale`) unless `melee_lunge_force_enable_flying` is set.
- Includes abort logic.

It has nothing to do with jumping. I note it here only because it is a sv_gravity user.

### FUN_180173a00: wallrun **wall jump** (called by CheckJumpButton) **[I name, R body]**

```c
off = wish * settings.f22c;
into = min(dot(n, mv.fwd),0) * min(dot(n, wish),0);
push = (1 - into*0.8) * settings.f230;
if (dot(n,off) < push) off += n*(push - dot(n,off));
vel += off;
vz = strength(m_wallRunCount; wallrun_strengthLossStart/End) * settings.f228;
if (vel.z < vz) vel.z = min(vel.z + 1.5*vz, vz);
```

The sqrt(2gh) impulse is **not** applied on a wall jump.

---
## 3. Formulas

g = `sv_gravity` (750). Every jump height h below is in units.

**Ground or coyote jump** **[R]**:

```
v_up = groundFactor * sqrt(2 * g * h),  h = settings.f94 (sliding: settings.f280)
vel += m_upDir * v_up                     (additive)
```

- On top of that, `* grapple_jumpFrac` (1.0) applies while grappling.
- The resulting apex gain is h above the take-off point if vel.z was 0 **[I]**.

**Skip jump** **[R]**:

```
h_skip = h * skip_jump_height_fraction   → v_up = sqrt(0.75) * v_up(normal) ≈ 0.866 * v_up
```

- Horizontal: if `|v_xy| > skip_speed_retain` (450), `|v_xy| ← max(|v_xy| - skip_speed_reduce, 450)`. Otherwise it is untouched.
- The window is a jump within `skip_time` (0.1 s) of landing.

**Double jump ("superjump")** **[R]**:

```
t      = FUN_1805d30c0(player) = 0.0 always  [R]  →  H = settings.f100, power cost = superjump_min_power_use (default 0)
         (settings.f104 and superjump_max_power_use are effectively unused)
H      = settings.f100 + (settings.f104 - settings.f100) * t
rise   = max(vel.z,0)^2 / (2g)                                    (height the current upward speed would still give)
if (H*superjump_min_height_fraction + rise < H)  { vel.z = 0; h = H }      // i.e. rise < 0.75 H (default)
else                                             { h = 0.25 H }             // keep vel.z, add a small kick
vel += m_upDir * groundFactor * sqrt(2 g h)
power -= superjump_min_power_use + (superjump_max_power_use - superjump_min_power_use) * t     (0 .. 100)
```

- The result is at least H of extra apex from the double-jump point, and falling speed is cancelled **[I, algebra]**. Proof for the second case: `(vz + sqrt(0.5 g H))²/2g ≥ rise + 0.25H ≥ H`.
- Horizontal redirect (w = input dir × settings.f10c):

  ```
  vel_xy ← (|w| < |v_xy| ? (1 - |w|/|v_xy|) : 0) * vel_xy + w
  ```

  When w is aligned with v_xy, |v_xy| is preserved. When w is perpendicular, it rotates toward the input. When `|w| >= |v_xy|`, it simply sets `vel_xy = w`.
- The availability limit is `airJumpCount < settings.f108`. The count is reset on ground (0). On walls (m_upDir.z < 1) it is reset to 0 if `superjump_limitreset_onwallrun` (1) and settings.b110, and otherwise set to max.

**Air acceleration (pilot)** **[R]**:

```
wishspeed = speedScale(FUN_1805d2510) * scale2(FUN_18016e9e0) * min(|input|,1) * m_airSpeed
accel     = m_airAcceleration * player.f1600
add = wishspeed - v·wishdir
add > 0 : v += wishdir * min(add, accel*dt)                                     (Quake-style, cap = airSpeed)
add <= 0: v += wishdir * player_extraairaccelleration * dt ; |v| clamped to max(|v_old|, wishspeed)
```

- With the default of 2.0 u/s², the steer-only term is tiny per tick **[R value, I significance]**.
- Keyboard grace is applied after this. Its blend weight is `a = clamp((Δt-0.5)/(0.2-0.5),0,1) * 0.7`, and the delta is capped at `0.7 * FUN_1805d2b80(player,0)`.

**Friction**:

- **Pilot ground friction** (logic3 `FUN_1801696b0`) **[R]**:

  ```
  keep = component of v along wishdir, capped at wishspeed   (f = min(1, (v·ŵ)·wishspeed/|v|²); keep = ŵ·f·(v·ŵ))
         (ŵ is first clipped off m_lastWallNormal if it points into it)
  rest = v - keep
  |rest| ← max(|rest| - D * m_surfaceFriction * m_groundFrictionScale * dt, 0)
  D = ramp * player.f1600 * ( |v|>walkSpeed ? P.f58 (or P.f54) : P.f4c (or 0.6*P.f48) )   [R, logic4]
      sliding: D = settings.f258 (crouch held, moving with input) / settings.f25c (otherwise) / 0 if player.i15cc==3
  sliding: |rest| *= settings.f260 ^ ((1 - 0.75*FUN_180221c20(...)) * dt)
  v = keep + rest
  ```

  - **Pilots do not use sv_friction, sv_stopspeed or sv_edgefriction** in this routine **[R]**.
  - This is a linear (constant-rate) deceleration, unlike Source's speed-proportional drop with its stop-speed floor.
  - It applies only to velocity that is not "explained" by your input. Bunny-hop speed above wishspeed decays at D per second while grounded **[I]**.
- **Ground Accelerate** (logic3 `FUN_180160230`) **[R]**:

  ```
  add = wishspeed - v·ŵ;  if add > 0: v += ŵ · min(add, accel*dt*m_surfaceFriction);  |v| ≤ max(|v_old|, wishspeed)
  accel = GetGroundAccel() = tier(P.f50 start | P.f48 normal | P.f54 above-max | settings.f264 sliding) * player.f1600
  wishspeed = min(|wish|,1) * mv.f88
  ```

  The ground version clamps speed to max(|v_old|, wishspeed), so it cannot gain speed beyond that. The **air** version has no such clamp in its `add > 0` branch, so air-strafing **can** gain speed up to the Quake-style limit set by airSpeed **[R]**.
- **Stock reference** (SDK 2013 Friction):

  ```
  control = max(speed, sv_stopspeed=100)
  drop    = control * sv_friction(4) * m_surfaceFriction * dt
  newspeed = max(speed - drop, 0)
  ```

- **Noclip** **[R]**: `drop = max(spd, 0.25*maxspeed) * sv_friction * surfaceFriction * dt`.
- **Observer** **[R]**: `drop = spd * sv_friction * dt`.
- **Step entities** **[R]**: `drop = max(spd3D, sv_stopspeed) * sv_friction * ent.GetFriction() * dt`, applied to xy only.
- **Wallrun** **[R]**: `newspeed = speed*(1 - dt*k) * hangFactor`, where `k = m_wallrunFrictionScale * settings.f224 * m_surfaceFriction * ramp`, or `k = 10` when settings.b218.
- **m_surfaceFriction** is reset to 1.0 each CategorizePosition, and set to 0.25 when leaving the ground with upward velocity **[R]**.

**Edge friction** **[R: unused]**:

- The probe is described under `FUN_18015fe70` in section 2.
- The pilot ground Friction (`FUN_1801696b0`) does **not** call it and does not read `sv_edgefriction` **[R]**.
- **`FUN_18015fe70` has no caller anywhere in server.dll, and `sv_edgefriction` (2) is never read [R]** (whole-binary xref, pointer, RVA and rel32 scans; see section 2). GroundDecel `FUN_18016eda0` (logic4) has no edge term either **[R]**. **There is no edge friction in TF2 pilot movement.**

**Move-speed scale and anti-tamper** **[R]**:

- `FUN_1805d2510` multiplies every speed tier, wishspeed and the keyboard-grace cap. It contains a **Denuvo "Denuvo_SlowSpeed" trigger**: on a copy that fails the tamper check, speed is multiplied by 0.1 once curtime > 600 s.
- This is anti-tamper, **not game design**. Ignore it for design reference.

**Velocity clamps** **[R]**:

- CheckVelocity scales the whole vector so that no axis exceeds `sv_maxvelocity` (12100), and clamps origin to ±65536.
- In noclip, wishspeed ≤ factor × `sv_maxspeed` (320).
- In observer mode, wishspeed ≤ `sv_maxvelocity`.
- In the VPhysics shadow (`FUN_1805dc9c0`), if the computed max speed is ≤ 0, `sv_maxspeed` is used as a fallback.
- Slide long-jump snaps horizontal speed to settings.f274 when it is within 85–100% of it.
- **There is no speed cap on pilots in the jump or air code**, apart from the skip floor/reduce and the extra-accel no-gain rule.
- Ground Accelerate never raises |v| above max(|v_old|, wishspeed) **[R]**.

---

## 4. ConVars (defaults from convars.tsv) **[R]**

| ConVar | Default | Used in / effect |
|---|---|---|
| sv_gravity | 750 | jump v = sqrt(2 g h), double-jump rise calc, Start/FinishGravity |
| jump_graceperiod | 0.2 | coyote time after leaving ground (no prior jump) |
| jump_keyboardgraceperiodmin | 0.2 | full-strength keyboard redirect window after a jump |
| jump_keyboardgraceperiodmax | 0.5 | keyboard redirect fades to 0 here |
| jump_keyboardgrace_strength | 0.7 | max blend toward key direction |
| jump_keyboardgrace_max | 0.7 | Δv cap = this × FUN_1805d2b80(player,0) |
| skip_time | 0.1 | jump within this many seconds of landing = skip. Also gates replenish when skip_replenish_double_jump=0 |
| skip_jump_height_fraction | 0.75 | skip jump height multiplier |
| skip_speed_retain | 450 | floor; skip speed reduction only applies above it |
| skip_speed_reduce | 12 | "Speed lost when skipping" |
| skip_replenish_double_jump | 1 | "Whether the player can double jump after skipping" |
| skip_sounds | 0 | "Enables skip-specific sounds" |
| superjump_min_height_fraction | 0.25 | double-jump: min fraction of H; full H if rise < (1-f)·H |
| superjump_min_power_use | 0 | power cost at t=0. Also the minimum power to double jump |
| superjump_max_power_use | 100 | power cost at t=1. **t is always 0, so this is unused in practice** [R] |
| superjump_powerreset_onground | 1 | power reset to 100 on ground |
| superjump_limitreset_onwallrun | 1 | air-jump count reset on wall contact (if settings.b110) |
| superjump_fail_sound_when_jump_limit | 1 | play fail sound when out of jumps |
| superjump_drain_power_onfail | 0 | listed for CheckJumpButton in index. Reference not visible in the decompiled text [?] |
| superjump_limit | 0 | not referenced by any dumped function [?] |
| superjump_disabled_from_water | 0 | not referenced by any dumped function [?] |
| player_extraairaccelleration | 2.0 | steer-only air accel (u/s²) when already at/over wishspeed |
| sv_airaccelerate | 10 | **not referenced** by dumped pilot code (TF2 uses m_airAcceleration) |
| sv_friction | 4 | noclip / observer / step-entity friction only. **Not used by pilot ground friction** (logic3 FUN_1801696b0) [R] |
| sv_stopspeed | 100 | step-entity friction only. **Not used by pilot friction** [R] |
| sv_edgefriction | 2 | registered but **never read anywhere in server.dll** [R] (logic8 whole-binary scan). No edge friction |
| sv_use_edgefriction | 1 | gates the edge probe FUN_18015fe70, which itself has no caller [R] |
| sv_maxspeed | 320 | noclip speed scale, shadow fallback |
| sv_maxvelocity | 12100 | CheckVelocity per-axis limit (whole-vector scale) |
| sv_specspeed / sv_specaccelerate / sv_specnoclip | 5.0 / 1000.0 / 1 | observer movement |
| grapple_jumpFrac | 1.0 | jump impulse multiplier while grappling |
| dodge_vertical_enable / _threshold / _horzspeedscale / _in_air | 0 / 0.1 / 0.5 / 1 | dodge variants of the jump |
| dodge_no_movement_forward_enable | 1 | dodge with no input uses view forward |

Hard-coded constants **[R]**:

- 2.0 (sqrt(2gh))
- 0.85 (slide-jump cap band)
- 0.4 s (slide-boost removal window)
- 30.0 (wall-normal nudge)
- 0.15 (outStepHeight)
- 16 / 49 (edge probe)
- 0.25 (noclip control, surface friction off-ground)
- 10.0 (wallhang friction)
- 0.8 (wall-jump into-wall term)
- 1.5 (wall-jump vertical)
- 65536 (origin clamp)

---

## 5. Confidence summary

- **Read directly:**
  - every formula in section 3 marked [R];
  - all ConVar names and defaults;
  - datamap offsets (m_upDir, m_airSpeed, m_airAcceleration, m_flTimeLastLanded, ...);
  - the ordering inside FullWalkMove;
  - the skip clamp math;
  - the double-jump height rule;
  - the AirAccelerate body;
  - **(logic3 update)** the pilot ground Friction body (input-aligned velocity up to wishspeed is protected; constant decel × m_surfaceFriction × m_groundFrictionScale; no sv_friction / sv_stopspeed / sv_edgefriction);
  - the ground Accelerate body (absolute accel, surfaceFriction factor, no-gain clamp);
  - the 3-tier GetGroundAccel;
  - the ADS move-scale helper;
  - the jump and double-jump view-punch helpers;
  - m_groundFrictionScale (0x22c0) and m_hovering (0x25bc) from the datamap.
- **Inferred:**
  - function names;
  - "superjump = double jump" (strong: CanDoubleJump, the count vs settings.f108, the decoy, script natives);
  - mv offsets 0x18/0x1c/0x20 as held/old/pressed buttons;
  - normalised move inputs;
  - wall jump = m_upDir tilted while wallrunning;
  - settings offsets' meanings;
  - the names Friction / Accelerate / GetGroundAccel (bodies are now [R]);
  - FUN_18016e9e0 = ADS scale (via FUN_1805d3bf0);
  - player.f1600 = global accel scale;
  - no friction on the jump tick.
- **Unknown / not in dump** (still missing after logic3):
  - ~~the caller of the edge probe~~ resolved in logic8: it has no caller, and sv_edgefriction is unused [R];
  - `FUN_1805d25d0`, `FUN_180222070` and `FUN_1802220b0` (factors inside moveSpeedScale);
  - the name of the friction-ramp timestamp player+0x1f40;
  - the meanings of player.i15cc / b15c8 in slide friction.
- **Resolved by logic4** **[R]**:
  - GroundDecel `FUN_18016eda0`;
  - double-jump t = 0 (a stub; confirmed from the raw bytes `0f 57 c0 c3`);
  - moveSpeedScale `FUN_1805d2510` (m_playerMoveSpeedScale 0x1e90, plus a Denuvo trap);
  - the speed tiers `FUN_1805d2bd0` / `FUN_1805d2b80` (m_perPosValues 0x1dac / 0x1db0, 8 bytes per index);
  - `FUN_1805d2bb0` (P.f44);
  - the ADS fraction `FUN_1805d3bf0`.
  - the meanings of settings.b110 and b118/f130;
  - all per-class numbers (.set files).

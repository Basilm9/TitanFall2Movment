# TFMovement: Titanfall 2 pilot movement for Unreal Engine 5 (example plugin)

A clean-room C++ reimplementation of Titanfall 2 multiplayer pilot movement, written only from the
reverse-engineering notes in [`docs/`](../../docs/TITANFALL2_MOVEMENT.md). No game code was copied: the
docs' pseudocode was treated as a specification and re-expressed in this plugin's own structure and
names. Comments cite the doc section a rule comes from.

> **Not compiled with Unreal.** This machine has no Unreal Engine install, so the plugin has never been
> built by UnrealBuildTool or UHT. The code targets UE 5.3+ APIs and was written and reviewed with that in
> mind, but expect to fix the odd API detail on first build. What *was* checked:
> - The engine-independent core (`TFMovementCore.h/.cpp`) compiles cleanly with llvm-mingw clang and the real
>   standard library (`-std=c++17 -Wall -Wextra -Wshadow`, zero warnings).
> - The core was also built to WebAssembly with a box-world test harness and run under node. The scenario
>   results in [Verified behaviour](#verified-behaviour-core-simulation) come from that run.
> - The Unreal-side files were syntax-checked against hand-written engine stubs. That catches C++ and
>   declaration/definition mismatches, not wrong Unreal API names.

## Files

```
Plugins/TFMovement/
  TFMovement.uplugin
  Source/TFMovement/
    TFMovement.Build.cs
    Public/TFMovementCore.h            engine-independent core: math, ITFWorld, settings, ConVars, input, state
    Private/TFMovementCore.cpp         all movement logic (plain C++17, no UE headers)
    Public/TFPilotSettingsUE.h         GENERATED USTRUCT mirrors of the settings and ConVars (+ UENUM)
    Public/TFPilotMovementComponent.h  UPawnMovementComponent wrapper + ITFWorld adapter
    Private/TFPilotMovementComponent.cpp
    Public/TFZiplineActor.h            spline rope actor
    Private/TFZiplineActor.cpp
    Public/TFPilotCharacterExample.h   minimal pawn: capsule, camera, legacy input bindings
    Private/TFPilotCharacterExample.cpp
    (TFWeapon*.h/.cpp: weapon math; see "Weapons" below)
../../tools/node/gen_settings_ue.js    (repo root) regenerates TFPilotSettingsUE.h from TFMovementCore.h
```

## Install

1. Use a **C++** UE 5.3+ project. Copy `examples/unreal/Plugins/TFMovement` into `<Project>/Plugins/`.
2. Regenerate project files, build, and enable "Titanfall 2 Pilot Movement (reimplementation)" in
   Edit > Plugins if it is not already enabled.
3. Add the legacy input mappings (Project Settings > Input, or `Config/DefaultInput.ini`):

   ```ini
   [/Script/Engine.InputSettings]
   +AxisMappings=(AxisName="MoveForward",Scale=1.0,Key=W)
   +AxisMappings=(AxisName="MoveForward",Scale=-1.0,Key=S)
   +AxisMappings=(AxisName="MoveRight",Scale=1.0,Key=D)
   +AxisMappings=(AxisName="MoveRight",Scale=-1.0,Key=A)
   +AxisMappings=(AxisName="Turn",Scale=1.0,Key=MouseX)
   +AxisMappings=(AxisName="LookUp",Scale=-1.0,Key=MouseY)
   +ActionMappings=(ActionName="Jump",Key=SpaceBar)
   +ActionMappings=(ActionName="Crouch",Key=LeftControl)
   +ActionMappings=(ActionName="Crouch",Key=C)
   +ActionMappings=(ActionName="Sprint",Key=LeftShift)
   +ActionMappings=(ActionName="ADS",Key=RightMouseButton)
   +ActionMappings=(ActionName="Grapple",Key=Q)
   +ActionMappings=(ActionName="Use",Key=E)
   ```

4. Set your GameMode's *Default Pawn Class* to `TFPilotCharacterExample` (or add a
   `UTFPilotMovementComponent` to your own pawn whose root is a `UCapsuleComponent`, and call
   `SetMoveInput`, `SetJumpPressed`, `SetCrouchPressed`, `SetSprintPressed`, `SetAdsPressed`,
   `SetGrapplePressed` and `SetUsePressed` from your input code).
5. For ziplines, place `TFZiplineActor`s and edit the spline points. The pilot hangs one hull height
   below the rope. Add the actor or component tag `NoGrapple` to surfaces that should refuse the hook.

Settings live on the component (`Settings`, and `ConVars` under Advanced) in **Titanfall units**. After
changing `TFPilotSettings` or `TFMovementConVars` in the core header, run
`node tools/node/gen_settings_ue.js` from the repo root ([script](../../tools/node/gen_settings_ue.js)) to regenerate the Unreal mirror. It parses every `<type> <Name> = <default>; // [tag] ...` line (and `// @group` markers) in the two core structs and writes the `USTRUCT`s, the `UENUM` and `ToCore`/`FromCore` copy functions, so the mirror cannot drift from the core.

## Architecture

- `TFPilotMove(State, Input, Settings, World, dt)` (or `TFPilotMoveEx` with explicit ConVars) is the
  per-tick entry point. It follows the doc's frame order (TITANFALL2_MOVEMENT.md Â§1, ground_misc.md Â§1):
  stuck check â†’ CategorizePosition â†’ Duck â†’ grapple update â†’ ADS / air slow-mo blend â†’ sprint â†’ max speed
  â†’ sprint tilt â†’ zipline attach â†’ dispatch (zipline ride, mantle traversal, or FullWalkMove) â†’ line wrap,
  power, view roll.
- FullWalkMove: gravity half-step â†’ CheckJumpButton â†’ WallRunThink â†’ grapple pull â†’ AirMove, WalkMove or
  WallRunMove â†’ ImpactCheck â†’ CategorizePosition â†’ gravity half-step â†’ strip velocity along `m_upDir`.
- `ITFWorld` is the only contact with the engine: `SweepHull`, `TraceLine`, `Now`, `QueryZiplines`,
  `GetZipline`, `IsGrappleTargetValid` and `OnMoveEvent`. The component implements it with capsule sweeps
  on `ECC_Pawn` and line traces on `ECC_Visibility`.
- The component steps the core at a fixed `SimulationRate` (60 Hz default) and moves `UpdatedComponent`
  with `SafeMoveUpdatedComponent` / `SlideAlongSurface`. Afterwards it re-syncs the core origin to where
  Unreal actually placed the capsule.

## Units and axes

- The core works in Titanfall/Source units: 1 unit = 1 inch, speeds in u/s, `sv_gravity` = 750 u/sÂ².
- The component converts at the boundary with one constant, `TFMove::CmPerUnit = 2.54`.
- Source is right-handed with +Y left; Unreal is left-handed with +Y right. The conversion mirrors Y and
  negates view yaw and pitch (Source pitch is positive looking down). See `TFMove::ToTF` / `ToUnreal`.
- Hull: 32 Ã— 32 Ã— 72 u standing and 47 u crouched, mapped to a capsule of radius 16 u. Eye height is
  60 u standing and 32 u crouched.
- Blueprint getters report Unreal units (`GetSpeed` in cm/s) except the `*TF` variants
  (`GetSpeedTF`, `GetHorizontalSpeedTF`), which report u/s like the game's speedometer.

## Tag legend

Every tunable in `TFPilotSettings` / `TFMovementConVars` (and every tooltip in the Unreal mirror) carries
one of the repo's tags:

| Tag | Meaning |
|---|---|
| **[D]** | Shipped multiplayer pilot value from `pilot_base.set` / `pilot_mp.set` (TITANFALL2_MOVEMENT.md Â§0) |
| **[R]** | Code default or ConVar default read from server.dll |
| **[C]** | Computed from [R]/[D] values |
| **[I]** | Inferred or assumed (best guess) |
| **[?]** | Unknown in the docs; a placeholder is used |

Defaults are the **shipped MP pilot values**. Where a `.set` file leaves a field unset, the code default
applies. ConVars are their registered defaults; playlist overrides were never checked [?].

## What is implemented

- **Ground:** walk 173.5 / sprint 260 / crouch 80 u/s.
  - Absolute-rate Accelerate (2500 u/sÂ², never exceeds max(old speed, wishspeed)).
  - Pilot friction: input-aligned velocity is protected; the rest loses a fixed 1500 u/sÂ². No stop speed and no edge friction.
  - Sprint input Ã—1.25 and the crouch input cap; the analogue `(2-m)Â·m` ease; side and back scales.
  - Step-up (18 u) with the slide step penalty, and StayOnGround.
- **Crouch:** 0.4 s duck (0.2 s while sliding), 0.2 s unduck, CanUnduck. Forced crouch while sliding faster than 350 u/s.
- **Jump:**
  - Ground jump `sqrt(2Â·750Â·60)` = 300 u/s, added along `m_upDir`.
  - Coyote time 0.2 s; keyboard grace (0.2â€“0.5 s, strength 0.7, cap 0.7 Ã— sprint).
  - Skip / bunny-hop (0.1 s, Ã—0.75 height, âˆ’12 u/s above 450).
  - Double jump: H = 60, the 0.75Â·H rule, 180 u/s redirect, one per airtime. It refreshes on landing and on wallrun start.
  - Wall jump (230 up / 205 out / 80 input, 80% outward cut). The jump tick gets no ground friction.
- **Air:** airSpeed 60 / airAcceleration 500 absolute, `player_extraairaccelleration` steer-only branch,
  gravity 750 Ã— 0.8, and a velocity clamp that scales the whole vector to `sv_maxvelocity`.
- **Slide:**
  - Start: speed â‰¥ 200, walkable ground, angle dot â‰¥ 0.6. Boost +150 up to 400, with a 2 s cooldown measured from the last slide start.
  - While sliding: no forward accel (only slideAccel 50), 50 u/sÂ² decel (400 when crouch is released or you push against the slide) Ã— 0.7^dt decay, and slope gravity `gÂ·n.xyÂ·n.z`.
  - Ends below 125 u/s, or when the crouch is released and you stand up.
  - Slide-jump: height 50, boost strip within 0.4 s while not fully ducked, and the 85â€“100% snap to 350.
- **Wallrun:**
  - Start: normal z in [âˆ’0.35, 0.7], approach cone, 0.5 s airborne gate, two probes at 13 u, and the mantle-instead-of-wallrun probe.
  - Strong/weak runs and same-wall rules. Up-wall boost 250 and gravity ramp 0 â†’ 1 over 1 s on strong runs only.
  - Per tick: friction `speedÂ·(1âˆ’4Â·dt)`, vertical accel 360 / cap 225, horizontal accel 1500 / cap 420 (50 backward), and upward auto-push 0.65.
  - Slip from 1.5 s over 1 s (â‰¥ 0.7 with no input), plus top-of-wall decel. Normals are followed around curved walls at 3 rad/s.
  - Ends on crouch, pushing away for 0.05 s, the 1.75 s limit, losing the wall (probed every 4th tick) or landing. It ends with a 70 u/s push-off.
  - Wall-runner kit time bonuses.
- **Wallhang:** `wallrunAdsType` "wallhang" (or `bWallHangKit`). The hang factor fades speed and gravity over 0.5 s, slips from 3 s, and is limited by the hang time limit. It uses an 18 u probe.
- **Auto-mantle:**
  - Gating, including jump held on the ground and the 0.25 s cooldown.
  - Detection: forward hull sweep, the cycling ledge probe heights {80, 66.5, 34.5, 26.5, 16.5}, the 18 u penetration rule, the low-ledge and slope guards, and the dangle check.
  - Push and view angle cones; BELOW/LEVEL/ABOVE/HIGH classes with 1.11 / 1.0 / 0.5 / 0.35 s.
  - midFrac, start fraction and rate; rest/hang at 0.4 (0.3 BELOW).
  - Jump-off after 0.1 s or past 0.5; pull-back abort past 0.7; crouch while hanging drops you; weapon-lowered flag.
- **Dodge:** fully implemented in CheckJumpButton and UpdateDodgeState, but **disabled** (`bDodgeEnabled = false`, as in the shipped pilot files).
- **Grapple:**
  - Fire: 3000 u/s hook with elliptical range 1100/1100. It retracts at 6000 (3000 when forced) and can still attach on the way back.
  - Attach, then a 0.2 s pull delay. The initial impulse is capped at 50 u/s as decompiled, plus a 50 u/s off-ground impulse.
  - Pull: ramp cap 50 â†’ 800 over 1.5 s at 1000 u/sÂ², sideways decel 425, `dontFightGravity`, 25 u lift, and the around-obstacle push (4 Ã— 1000).
  - Gravity fraction 0.25â€“0.7 (full when the point is steeply below).
  - Air control 420 / 650; jump Ã— `grapple_jumpFrac`; power drain 50/s after 0.25 s up to 50 per grapple; regen 3/s after 3 s (Ã—1.35 Power Cell).
  - Detach rules: too long (+50); too short with a speed-scaled threshold plus impact boost 300/300; moving away faster than 500 or passing the point, with detach boost 200/200; low speed for 0.7 s (ground), 1.2 s (wall) or 1.5 s (air); target invalid; manual crouch release; pressing grapple again.
  - **Detach speed loss** `s = max(s âˆ’ 300, 460)` when `s > 460`. It applies only once pulling has started, and after the vertical boosts.
  - Simplified line wrapping around corners.
- **Zipline:**
  - Attach within 120 u (Use key, or automatically in the air), with a 1 s reuse cooldown for the last rope.
  - Mount blend over `mountZiplineTime`, ending at exactly 600 u/s along the chord.
  - Chord ride: `s = dot(dir, v) + 400Â·dt`, floored at 0 and capped at 600 Ã— rope scale. The sag parabola sets height, and the rider hangs one hull height below.
  - Gravity only on descending chords, with the documented sign (it slows the rider).
  - Bends keep `sÂ·cosÎ¸`. Detach on a hull hit, at the end of the line, or within the auto-detach distance.
  - Dismount: crouch drops, jump launches along the view, damp scale 1.
- **Stim:** `TFStartStim` / `StartStim(duration)` sets `State.SpeedBoostSeverity` = 0.4 for duration + 0.5 s. SpeedScale Ã— (1 + 2 Ã— 0.4) = 1.8, so sprint becomes 468 u/s; slide decay also slows (exponent Ã— (1 âˆ’ 0.75Â·boost)).
- **Also:**
  - Suit power (regen 10/s; sprint start needs 10; `sprint_powerdrain` 0) and the sprint tilt spring.
  - Air slow-mo blend (only active with ADS hover, `bHoverEnabled`) and ADS speed blend via `SetWeaponSpeedScales`.
  - Impact strength (380 u/s) and landing events, all broadcast through `OnMoveEvent`.
- **Omitted on purpose:** the Denuvo anti-tamper traps (`Denuvo_CheckWallrun`, `Denuvo_SlowSpeed`). They are not gameplay (doc Â§12).

## Fidelity: what is a guess

Everything below is either marked [I]/[?] in the docs ([ASSUMPTIONS.md](../../docs/ASSUMPTIONS.md)) or was
filled in by this implementation where the docs give no rule. Each is commented `// [I]` or `// [?]` in the code.

**Inherited from the docs' assumptions**
- **J1:** flight gravity uses `gravityScale` 0.8 as the entity gravity. All apex heights (â‰ˆ75 u), the slide slope push and the zipline descent term depend on it.
- **J2:** AirMove uses the class `airSpeed`/`airAcceleration`.
- **G1:** the Stim/slow status-effect identities, plus Stim weakening slide decay (**S4**, `pow`). The Stim's 0.25 s fade-out reading is **MI1**.
- **G2 / G3 / G4:** stance 1 = crouched; step size = `stepHeight`; the unnamed accel scale (player+0x1600) = 1.
- **G7:** the ADS move-speed blend identity. The ADS blend time (0.2 s) is a placeholder [?].
- **S1:** a slide needs no explicit sprint. Only the 200 u/s gate (sprint 260 > 200 > walk 173.5) and the angle check apply. The slide is started when crouch begins on the ground, and on a crouched landing.
- **S2:** duck state 2 = fully ducked, so the slide-jump boost strip effectively lasts the 0.2 s slide crouch.
- **W1:** the 0.5 s wallrun gate reads `m_onSlopeTime` as "last time on the ground".
- **W2:** wallrunning is blocked while on a zipline.
- **W3:** WallProbe is a line probe of hull radius + allowed distance; a hit within twice the distance counts as a "near miss" (retry).
- **W4:** the every-4th-tick probes use the core's tick counter.
- **W5:** `wallrun_rotateMaxRate` is in rad/s.
- **W6:** ApplyWallRepel has no known caller, so it is implemented but off (`bApplyWallRepel`) [?].
- **W8 / J5:** weak-run intent; a coyote wall jump uses the last wall normal.
- **M1:** the mantle follows the documented `traversal_anim 0` reading (vertical beginâ†’mid, then horizontal midâ†’end). The shipped animation root motion is model data [?].
- **M2:** early weapon lowering before a mantle (anticipation) is not done [?].
- **M3:** the code snaps the hang to `automantle_rest_frac` even for BELOW. This implementation keeps 0.3 for BELOW because the literal snap would oscillate (a deliberate deviation).
- **M4:** eye-height classes, and `mv+0x84` = hull radius.
- **GR1:** the 50 u/s initial impulse cap, as decompiled.
- **GR2:** the weapon keys behind range and power.
- **GR3:** the pull's exempt normal (vfunc 0x998) is taken as `m_upDir`.
- **GR4:** the grapple slow fraction is 0.
- **Z1:** per-rope `m_ziplineMoveSpeedScale` is unknown [?]; `MoveSpeedScale` defaults to 1.0.
- **Z2:** the real mount time is the viewmodel animation length [?]; the 0.5 s fallback is used.
- **Z3:** gravity slows descending riders. The sign was verified; the intent was not.
- **P4:** sprint input rules. Sprint while held; stop with no forward input (< 0.1), when ADS or crouching, or while ziplining or mantling.
- **X7:** the wallrun camera roll (15Â° at speed 6), sprint view offset (âˆ’6 u after 0.2 s over 0.8 s) and sprint tilt degrees are client-side in the game. They are approximated for the camera only.

**Added by this implementation (the docs give no rule)**
- **Wallrun:**
  - The in-surface axes: A follows the facing along the wall, and B = A Ã— n.
  - Losing the wall in CategorizePosition ends the run without the push.
  - The fall-away "position test" is a short hull sweep.
  - The wallrun gravity ramp only applies while wallrunning.
  - The wallhang probe is at hull top.
- **Mantle:**
  - The dangle / climb-floor fraction comes from a downward hull sweep along the face.
  - `horiz = d âˆ’ mindist + radius`.
  - The ledge-top settle is a simplified fit test.
  - A jump-off from above midFrac is a ground jump.
- **Grapple:**
  - Firing needs `grapple_power_required` (50) power.
  - The hook flies outward at 3000 u/s.
  - Pressing grapple again releases (with speed loss).
  - "Push under" construction for the gravity fraction.
  - "Looking down" = pitch below horizontal.
  - Line wrapping takes the corner from the two hit planes (midpoint fallback), with no unwrapping.
- **Zipline:**
  - The attach trigger (Use, or automatic in the air).
  - Find picks the candidate closest to the view ray; the 200 u anchor trims are replaced by the final hull trace.
  - Sag is fitted through each chord's spline midpoint.
  - Ziplining does not refresh the double jump [?].
- **Core:**
  - In-air ducking tucks the legs (stock Source).
  - Keyboard-grace key presses are derived from the analogue axes (Â±0.5 edges).
  - The stuck check tries small offsets.
  - The steep-landing re-probe (`stepTestDist`) is folded into the slope-ratio test.
  - On slopes the stored velocity stays horizontal unless the move hit something. This avoids a per-tick cos loss after the up-strip.
- **Engine:**
  - The hull is a capsule, not a Source box. Normals use the more upward of ImpactNormal/Normal.
  - The core runs at a fixed 60 Hz.

## Not implemented (and why)

- **Window traversal** and **grapple window hints / ledge aim-assist** need map window-hint entities.
- Several features need other players, NPCs or titans:
  - **Grappling players/NPCs** (reel-in, CanBePulled rules), **auto-melee**, **rodeo impulse** and **titan embark** detach.
  - **Repel from players/NPCs** and **head-slide**.
  - Hooks attach to world positions only (`HitEntityId` is always 0), so the "target dies" rule never fires in Unreal. It is implemented in the core for engines that supply ids.
- **ADS hover glide meter drain:** the drain rate is not documented, so the meter never empties.
- **Titan movement, melee lunge, noclip/observer, moving-platform base velocity, fall damage, sounds and view punch.** These are out of scope; events are exposed for FX.
- **Networking / prediction:** this is a single-player example.

## Verified behaviour (core simulation)

These numbers come from running the core (60 Hz, flat box world) in the wasm harness:

| Scenario | Result |
|---|---|
| Walk / sprint / crouch-walk | 173.5 / 260 / 80 u/s, eye 60 â†’ 32 |
| Standing jump | apex 72.5 u (discrete 60 Hz vs â‰ˆ75 analytic) |
| Double jump at apex | +75 u; a third air jump is refused |
| Coyote jump 0.1 s after walking off a ledge | counts as a ground jump (double jump still available) |
| Sprint â†’ crouch | slide at 396.8 u/s (260 + 150 capped at 400), 309 at 0.5 s, ends â‰ˆ2 s |
| Slide-jump at 309 u/s | snapped to 350 u/s; bunny-hops then keep â‰ˆ350 |
| Wallrun from a sprint jump | strong run, 1.75 s, â‰ˆ375 u/s steady speed |
| Walk off a ledge into a wall | weak run (no up-boost) |
| Wall-to-wall jumps | outward 205, up 230, next run strong |
| Wallhang (kit) | speed 0 by 0.5 s, slip starts at 3 s |
| 70 u ledge, jump + forward | HIGH mantle, 0.32 s, ends standing on top |
| Grapple to a wall 900 u away | ramps to â‰ˆ794 u/s, detaches at the wall, 487 u/s after the 300 loss, 50 power used |
| Zipline (3 nodes, sagging) | mount 0.5 s, then 600 u/s through the bend to the end |
| Stim + sprint | 468 u/s |
| 12 u stairs | climbed at 173.5 u/s |

## Weapons

The same plugin also contains the weapon-side companion (written separately):

- `UTFWeaponLibrary` exposes Blueprint functions such as `GetShotsToKill`, `GetTimeToKill`,
  `GetDamageAtDistance`, `GetDamagePerTriggerPull`, `GetDamagePerSecond`, `GetPilotWeaponInfo`, ...
  (`TFWeaponLibrary.h`). Distances are in cm.
- `TFWeaponMath.h/.cpp` holds the damage math and `TFWeaponTable.h` the shipped weapon table.
  `TFWeaponTable.h` is generated by [`tools/node/weaponheader.js`](../../tools/node/weaponheader.js).
- `FTFWeaponInfo::AdsMoveSpeedScale` can be passed to
  `UTFPilotMovementComponent::SetWeaponSpeedScales` for the ADS movement slowdown.

# Titanfall 2 movement and combat record: assumptions and unknowns

This register lists everything in the movement record ([TITANFALL2_MOVEMENT.md](TITANFALL2_MOVEMENT.md) and its detailed notes) and the combat record ([TITANFALL2_COMBAT.md](TITANFALL2_COMBAT.md)) that was assumed, inferred or not located. Each item names its source note so a reader can check the claim in context.

**Legend** (repo-wide):
- **[R]**: read directly from server.dll (decompiled code, disassembly, or binary data such as ConVar registrations and the player-settings table).
- **[D]**: read directly from shipped game data files: `.set` player files, weapon `.txt` keyvalues, `.nut`/`.gnut` scripts extracted from the VPK.
- **[C]**: computed from [R]/[D] values (e.g. jump launch speed sqrt(2·750·60), apex heights, TTK).
- **[I]**: inferred or assumed: naming, meaning of an unnamed field, design intent, interpolation shape, anything not directly read.
- **[?]**: unknown or not located.

**What is solid and what is assumed.** The code paths, formulas and constants of every pilot movement system were read from server.dll [R], and every shipped multiplayer pilot value comes from the CRC-verified `.set` files [D]. The most important results were confirmed two independent ways (see the last section): the settings-table offsets against the code that reads them, double-jump `t = 0`, the absence of edge friction, the grapple detach speed loss and the zipline gravity sign. What rests on inference falls into a few groups. All function names and most field names are inferred. The link from the class `gravityScale` to the gravity actually applied is inferred, and every apex height depends on it. So are the identities of the status effects behind Stim's speed boost, some un-decompiled helpers, and the meaning of several button bits and player fields. Unknowns include anything client-side (view tilt, view punch, sprint view), per-map and per-model data (zipline speed scale, mount and mantle animations), playlist overrides, and the shape of combat damage falloff. Nothing was measured in a running game, so every behaviour is "as decompiled".

Each item gives: the claim, its status, why it is not certain, how to verify it, and the source.

---

## 1. Cross-cutting

**X1. Function, struct and field names are inferred.** [I]
- *Why:* Ghidra gives `FUN_` addresses. Names such as CheckJumpButton, WalkMove, WallRunThink, Grapple_ShouldDetach, `mv` offsets (velocity, wishdir, forwardmove, hull radius `mv+0x84`), `gpGlobals` curtime/frametime, trace_t layout, the player-settings struct identity at `this+0x18`, and the class-mod player offsets 0x1d3c–0x1d67 (decoded by parser order) come from usage, the Source SDK 2013 layout or key order. Exceptions that are [R]: datamap/netvar names (`m_upDir`, `m_airSpeed`, `m_traversal*`, `m_grapple*`, `m_zipline*`), settings-table field names, ConVar names, and functions named by their own debug strings (`TraversalMove`, `Zipline_MoveUpdateSliding`, `RepelFromOtherPlayers`).
- *Verify:* compare call patterns with Source SDK 2013 `CGameMovement`; dump RTTI/vtables; check datamap strings with `StrRefs8.java`.
- *Source:* every note's §0 "Conventions"; [jump_air_friction.md §5](jump_air_friction.md), [slide_mantle.md §5](slide_mantle.md), [ground_misc.md §6](ground_misc.md).

**X2. Button bits and input fields.** [I]
- *Claim:* mv+0x18/0x1c/0x20 are held/old/pressed buttons; 0x2 = jump, 0x4 = duck, 0x10000 = ADS/zoom, 0x4000000 = duck toggle, 0x10000000 = dodge; 0x40000 forces grapple auto-melee (meaning unknown [?]); mv+0x24/0x28 are normalised forward/side move.
- *Why:* taken from Source `IN_*` conventions and context. Everything that says "crouch ends the wallrun", "jump dismounts the zipline" or "pressed this frame" depends on it.
- *Verify:* client.dll input code (not analysed); in-game with a bound key and `cl_showpos`.
- *Source:* [jump_air_friction.md §0](jump_air_friction.md), [wallrun.md §5](wallrun.md), [grapple_zipline.md §0](grapple_zipline.md).

**X3. Static analysis only.** [I]
- *Why:* the game was never run. Some Ghidra output uses comma expressions and garbled stack arguments; Denuvo may virtualise blocks in the shipped binary.
- *Verify:* in-game measurements with `cl_showpos` or the shipped speedometer script (`cl_pilot_speedometer.gnut` exists in the VPK); disassembly for any formula whose sign or clamp order matters (as was done for the zipline).
- *Source:* [wallrun.md §5](wallrun.md); [TITANFALL2_MOVEMENT.md intro](TITANFALL2_MOVEMENT.md).

**X4. "Not used" claims cover only the analysed functions.** [?]
- *Claim:* these ConVars or fields have no server reader: `sv_backspeed`, `sv_airaccelerate`, `wallrun_slipvel`, `wallrun_hangslipvel`, `wallrun_slipslowdown`, `wallrun_maxViewTilt`, `wallrun_viewTiltSpeed`, `wallrun_repelSoftness`, `automantle_min_frac`/`max_frac`, `superjump_limit`, `superjump_disabled_from_water`, `dodge_viewTilt*`, `dodge_cockpit*`, `traversal_window_finish_angle`, `traversal_hand_debug`, `player_movementBounds_predictionShare`, `grapple_autoMeleeViewRotateSpeedNear/Far`, `dodgeHeightMax`. `superjump_drain_power_onfail` is listed for CheckJumpButton in the index, but no read is visible in the decompiled text.
- *Why:* only `sv_edgefriction` got the whole-binary pointer/RVA/rel32 scan. The others were checked against the dumped function index.
- *Verify:* run `RefsRange8.java` and `RelScan8.java` on each ConVar's value pointer (object + 0x38), as was done for `sv_edgefriction`; client.dll for the view-related ones.
- *Source:* [jump_air_friction.md §4](jump_air_friction.md), [wallrun.md §3.3, §3.10](wallrun.md), [slide_mantle.md §2.7](slide_mantle.md), [ground_misc.md §5](ground_misc.md), [grapple_zipline.md §1.7](grapple_zipline.md).

**X5. Playlist and server-script overrides were not checked.** [?]
- *Why:* ConVar defaults and `.set` values can be changed by playlists (`playlists_v2.txt`) or by server scripts writing `player.kv.*` or applying class mods. Most extracted scripts are client/shared; the full server script tree was not searched.
- *Verify:* extract the playlists file and the server `.gnut` scripts; grep for each movement ConVar name, `kv.gravity`, `kv.airSpeed`, `kv.airAcceleration` and `SetPlayerSettings`.
- *Source:* [TITANFALL2_MOVEMENT.md §0, §13](TITANFALL2_MOVEMENT.md).

**X6. `.set` syntax semantics.** [I]
- *Claim:* ClassMods operators mean "add" (`"2.5++"`, `"21++"`) and "multiply" (`*1.35`); keys and option strings are matched case-insensitively (`wallrunAdsType "ADS"` selects option "ads"; `jumpheight` sets `jumpHeight`).
- *Why:* the parser for mod operators was not read. Case-insensitivity is strongly implied: `jumpHeight` is a required field and pilot_mp supplies only lowercase `jumpheight`, so a case-sensitive lookup would fail to load the class.
- *Verify:* find the keyvalues lookup used by the settings loader; in-game, time a wallrun with and without the Wall Runner kit (expect 1.75 s vs 4.25 s).
- *Source:* [TITANFALL2_MOVEMENT.md §0](TITANFALL2_MOVEMENT.md).

**X7. Client-side behaviour was not analysed.** [?]
- *Claim:* wallrun view roll, dodge view tilt, sprint view offset, sprint body roll in degrees, slide FOV and view-punch springs are computed in client.dll.
- *Why:* their ConVars and settings have no server reader; client.dll was never opened.
- *Verify:* analyse client.dll with the same Ghidra scripts.
- *Source:* [wallrun.md §3.10](wallrun.md), [ground_misc.md §3.4](ground_misc.md), [TITANFALL2_MOVEMENT.md §11, §13](TITANFALL2_MOVEMENT.md).

---

## 2. Ground and friction

**G1. Status-effect identities in SpeedScale.** [I]
- *Claim:* `SpeedScale = weapon × m_playerMoveSpeedScale × (1 − slow) × (1 + 2·boost)`, where the `slow` term (`DAT_180c91c10`) is the `move_slow` effect and the `boost` term (`DAT_180c91564`) is `speed_boost`. The same `DAT_180c91564` also weakens slide velocity decay (`1 − 0.75·effect` in the exponent).
- *Why:* the formula is [R], but the lookup helpers `FUN_180222070`, `FUN_1802220b0` and `FUN_180221c50` were not decompiled, so the mapping from global to script enum is inferred. Support: `sh_stim.gnut` says "assuming 'movement_speedboost_extraScale' is 2.0", which matches the hard-coded 2; no such ConVar is registered in server.dll. The Stim 1.8× figure rests on this.
- *Verify:* decompile those three helpers and match the indices against `eStatusEffect`; in-game, sprint with Stim and read speed (expect ≈ 468 u/s).
- *Source:* [ground_misc.md §2.1](ground_misc.md), [jump_air_friction.md §2 Friction](jump_air_friction.md), [TITANFALL2_MOVEMENT.md §2, §13](TITANFALL2_MOVEMENT.md).

**G2. Stance indexing and per-stance speed defaults.** [I]
- *Claim:* `m_perPosValues` stance 0 is standing and 1 is crouched; the parser default `speed` 120 applies to each stance.
- *Why:* the layout is [R], but the stance meaning comes from WalkMove's crouch ratio. The copy into `m_perPosValues` was not traced: parser `FUN_180554280` has no direct caller.
- *Verify:* find the parser's indirect caller (pointer scan); in-game, crouch-walk and expect 80 u/s.
- *Source:* [ground_misc.md §2.1, §4](ground_misc.md).

**G3. Step size is a copy of `stepHeight`.** [I]
- *Why:* StepMove reads player+0x1610; its writer was not found.
- *Verify:* find writers of player+0x1610 (`Scan8.java` displacement scan); in-game, walk onto 18 u and 19 u boxes.
- *Source:* [ground_misc.md §0, §2.4](ground_misc.md).

**G4. Unnamed player multipliers.** [I]/[?]
- *Claim:* player+0x1600 scales all ground and air accel and ground decel; +0x1604 scales suit-power regen; +0x1608 scales the dodge regen delay. All are presumably 1.0 for pilots.
- *Why:* only their reads were seen; no writer or datamap name was found.
- *Verify:* find writers; in-game, time a 0 → 173.5 u/s start (expect ≈ 0.07 s at 2500 u/s²).
- *Source:* [jump_air_friction.md §0](jump_air_friction.md), [ground_misc.md §0](ground_misc.md).

**G5. Ground-friction ramp timestamp (player+0x1f40).** [R] mechanism / [I] name
- *Claim:* ground and wallrun friction are zero until 0.5 s before this time, then ramp in. ground_misc.md names it `m_damageImpulseNoDecelEndTime`; jump_air_friction.md says no datamap name was found.
- *Why:* the two notes disagree on whether the name was read. The mechanism is [R]; what sets the timestamp (presumably damage impulses) is not traced.
- *Verify:* `StrRefs8.java` on the datamap entry for offset 0x1f40; find its writers.
- *Source:* [ground_misc.md §0, §2.3](ground_misc.md), [jump_air_friction.md §2 GroundDecel](jump_air_friction.md).

**G6. Helpers that were not decompiled.** [?]
- *Claim:* `FUN_1801b8730` (a velocity hook called in AirMove and WalkMove), `FUN_1805d4200` (applies slow-mo), `FUN_180176190` (a forced 75 u/s backward push when linked to player+0x2904), movetypes 0xd/0xe (`FUN_180645bf0`, `FUN_180629a70`), and the movement-bounds caller. The movement-bounds box is read as a collision prefetch, not a gameplay limit [I].
- *Verify:* decompile each (the addresses are listed).
- *Source:* [ground_misc.md §1, §3.6, §4](ground_misc.md).

**G7. ADS slowdown.** [I]
- *Claim:* `MaxSpeedScale = lerp(1, weapon.f1484, ADSFraction)`, where f1484 is the weapon's `ads_move_speed_scale` and `FUN_1805d3bf0` is the zoom fraction. The combat record's "ADS move ×" column and its ≈ 87 u/s example depend on this.
- *Why:* the formula is [R]; both identities are inferred from shape and use.
- *Verify:* map weapon struct offset 0x1484 through the weapon-settings table (same method as the player table); in-game, walk while fully aimed.
- *Source:* [ground_misc.md §2.1](ground_misc.md), [jump_air_friction.md §2](jump_air_friction.md), [TITANFALL2_COMBAT.md "ADS move ×"](TITANFALL2_COMBAT.md).

---

## 3. Jump and air

**J1. Flight gravity uses `gravityScale`.** [I]
- *Claim:* gravity applied in flight is `sv_gravity × entity gravity`, and for MP pilots entity gravity is the class `gravityScale` 0.8, giving 600 u/s².
- *Why:* FinishGravity multiplies by an entity gravity from vfunc 0x478 (0 means 1). No code path from settings+0x265c (`gravityScale`) to that value was traced. Support: the field's help text is "How affected the player is by gravity", and `mp_titanability_hover.nut` restores `player.kv.gravity = player.GetPlayerSettingsField( "gravityScale" )` [D].
- *Impact:* the ≈ 75 u ground-jump and double-jump apexes [C] (60 u if entity gravity were 1.0), the "at least H more apex" double-jump result, slide slope push, and the zipline descent term.
- *Verify:* `Scan8.java` for reads of displacement 0x265c and writers of the entity gravity field; in-game, jump on flat ground with `cl_showpos` and read the peak z (expect ≈ 75 u).
- *Source:* [TITANFALL2_MOVEMENT.md §0, §4](TITANFALL2_MOVEMENT.md), [jump_air_friction.md §2 FinishGravity](jump_air_friction.md), [grapple_zipline.md §2](grapple_zipline.md).

**J2. Air control reads player netvars, assumed to hold the class values.** [I]
- *Claim:* AirMove uses `m_airSpeed`/`m_airAcceleration` (player+0x25a0/+0x25a4), assumed equal to the class `airSpeed` 60 / `airAcceleration` 500.
- *Why:* the writer was not traced. Support: `sh_titan.gnut` and `mp_titanability_hover.nut` restore `player.kv.airSpeed = player.GetPlayerSettingsField( "airSpeed" )` [D]. Server scripts could change them for pilots (see X5).
- *Verify:* find writers of 0x25a0/0x25a4; grep server scripts for `kv.airSpeed`.
- *Source:* [jump_air_friction.md §0, §2 AirMove](jump_air_friction.md).

**J3. How much speed air-strafing adds.** [?]
- *Claim:* AirAccelerate has no total-speed clamp while below wishspeed along the input direction [R], so classic air-strafing can gain speed; the gain at airSpeed 60 and accel 500 was not quantified. The summary's earlier wording ("not from air-strafing") was an overstatement and was corrected.
- *Verify:* simulate the [R] formula at 60 Hz; in-game strafe test with a speedometer.
- *Source:* [jump_air_friction.md §2 AirAccelerate, §3](jump_air_friction.md).

**J4. The jump tick gets no ground friction.** [I]
- *Claim:* because the jump calls SetGroundEntity(NULL) before the move step, a jump on the first grounded tick skips friction; the benefit of a skip (bunny-hop) is avoiding friction.
- *Why:* inferred from call order, not traced through a full tick.
- *Verify:* in-game bunny-hop chain above wishspeed, logging speed per landing.
- *Source:* [jump_air_friction.md §1](jump_air_friction.md).

**J5. Wall jump detection and coyote wall jump.** [I]
- *Claim:* a jump is a wall jump when `m_upDir` is tilted (z < 1), which happens only while wallrunning; a jump within 0.2 s of leaving a wall should still be a wall jump because coyote time stores the last up-direction.
- *Verify:* in-game, jump just after a wallrun ends.
- *Source:* [jump_air_friction.md §1](jump_air_friction.md).

**J6. "superjump" is the double jump.** [I, strong]
- *Why:* supported by CanDoubleJump checking the doubleJump flag and `superjumpLimit`, the decoy recomputing the same formula, the script natives `IsDoubleJumping`/`ConsumeDoubleJump`, and pilot_mp setting `doubleJump 1` with `superjumpMinHeight 60`. The doubleJump flag offset (player+0x1d58) itself comes from parser order.
- *Source:* [jump_air_friction.md §1, §2](jump_air_friction.md).

**J7. Minor jump unknowns.** [?]
- *Claim:* the meaning of `FUN_1805e25f0` (decides the fail sound and a power drain on a failed double jump), and `FUN_1805d10e0` read as "time since last touched ground" [I].
- *Verify:* decompile both.
- *Source:* [jump_air_friction.md §2 CheckJumpButton](jump_air_friction.md).

---

## 4. Slide

**S1. Whether a slide needs sprint.** [?]
- *Why:* TryStartSlide has no sprint check; the help text for `slide_max_angle_dot` mentions sprinting, so a caller may require it. One caller (SetGroundEntity on a crouched landing) does not check sprint.
- *Verify:* find all callers of `FUN_180183180`; in-game, crouch while walking fast but not sprinting.
- *Source:* [slide_mantle.md §1.1](slide_mantle.md), [jump_air_friction.md §2 SetGroundEntity](jump_air_friction.md).

**S2. Duck-state and crouch-mode meanings.** [I]
- *Claim:* `m_duckState` 0/1/2/3 = standing/ducking/ducked/unducking; player+0x15cc == 3 is forced slide; player+0x15c8 is the duck-toggle latch.
- *Impact:* the slide-jump boost strip only runs while `m_duckState ≠ 2`. If 2 is "fully ducked" and the slide crouch takes 0.2 s, the effective strip window is about 0.2 s, not the 0.4 s constant.
- *Verify:* find writers of m_duckState in Duck/FinishDuck; in-game, slide-jump at 0.1 s and 0.3 s after slide start and compare speed.
- *Source:* [slide_mantle.md §1.5, §5](slide_mantle.md), [ground_misc.md §0, §2.6](ground_misc.md).

**S3. Slope push uses the ground normal.** [I]
- *Why:* `FUN_1805d2c80(player)` is assumed to return the ground normal.
- *Verify:* decompile it; in-game, slide down a ramp.
- *Source:* [slide_mantle.md §1.3](slide_mantle.md).

**S4. Slide velocity decay uses `pow`.** [I]
- *Claim:* `|rest| *= slideVelocityDecay ^ ((1 − 0.75·boostEffect)·dt)` with `FUN_1807dc0b0` = pow; with Stim active, slides decay more slowly (see G1).
- *Verify:* disassemble `FUN_1807dc0b0`; in-game, compare slide distance with and without Stim.
- *Source:* [jump_air_friction.md §2 Friction](jump_air_friction.md).

**S5. `slideMaxJumpSpeed` help text vs code.** [R] / [?]
- *Claim:* the help text says speed "is reduced to this", but the code only snaps speeds in 85–100% of the value up to it. A wider clamp elsewhere was not found.
- *Verify:* search for other reads of settings+0x274.
- *Source:* [slide_mantle.md §1.5](slide_mantle.md).

---

## 5. Mantle

**M1. The mantle path follows animation data.** [?]
- *Claim:* with `traversal_anim` 1 (default) the origin follows the first-person-proxy mantle animation's root motion, which is model data not in server.dll. The begin → mid → end path for `traversal_anim` 0 is itself an inference [I].
- *Verify:* extract the proxy model's `ACT_VM_MANTLE_*` sequences; decompile `FUN_1805d1e50`.
- *Source:* [slide_mantle.md §2.7](slide_mantle.md).

**M2. Anticipation lookahead source.** [?]
- *Why:* the lookahead that lowers the gun early comes from `FUN_1800e37d0(player)->vfunc_0x238()+0x17e8`, unresolved.
- *Source:* [slide_mantle.md §2.2 B](slide_mantle.md).

**M3. Rest snap for BELOW.** [I]
- *Claim:* at the rest target, progress snaps to `automantle_rest_frac` (0.4) even for BELOW (rest 0.3); possibly a decompiler artefact.
- *Verify:* disassemble that branch of `FUN_180180780`; in-game, hang on a high ledge.
- *Source:* [slide_mantle.md §2.6](slide_mantle.md).

**M4. Mantle geometry inputs and intent.** [I]
- *Claim:* `eyeOffset` is the view offset z (it drives the BELOW/LEVEL/ABOVE/HIGH classes, with eye height 60 [D]); `mv+0x84` is the hull radius; the dangle trace means "room to hang"; `FUN_180166de0` is force-duck; `FUN_180174810` is "can mantle on".
- *Source:* [slide_mantle.md §2.2–§2.4](slide_mantle.md).

---

## 6. Wallrun

**W1. The 0.5 s start gate reads `m_onSlopeTime`.** [I]
- *Claim:* "after walking off a ledge you need 0.5 s airborne; a jump clears this".
- *Why:* CanWallRun rejects while `curtime − player+0x2588 < 0.5`, and a ground jump zeroes that field [R]. wallrun.md reads it as a last-ground timestamp, but the datamap names 0x2588 `m_onSlopeTime`, and CategorizePosition stamps it on non-walkable surfaces "under some conditions". The gate may be "0.5 s after touching a steep slope", not "after leaving the ground".
- *Verify:* find all writers of player+0x2588; in-game, walk off a ledge straight into a wall.
- *Source:* [wallrun.md §2 CanWallRun](wallrun.md), [jump_air_friction.md §0](jump_air_friction.md), [ground_misc.md §2.5](ground_misc.md).

**W2. `FUN_1805e6af0` is read two different ways.** [I]
- *Claim:* wallrun.md calls it IsDodging (CanWallRun rejects when it is true); grapple_zipline.md and ground_misc.md use it as the on-zipline test that sends movetypes 2/7 to ZiplineMove. The dispatcher use is stronger evidence, so CanWallRun probably rejects while on a zipline, not while dodging.
- *Verify:* decompile `FUN_1805e6af0`.
- *Source:* [wallrun.md §2, §5](wallrun.md), [grapple_zipline.md §2](grapple_zipline.md).

**W3. WallProbe was not decompiled.** [I]
- *Claim:* `FUN_180186840` returns 0/1/2 (fail / near miss, schedule a retry / ok); its argument order is inferred from garbled stack arguments.
- *Verify:* decompile `FUN_180186840`.
- *Source:* [wallrun.md §2, §5](wallrun.md).

**W4. Wall-loss probe every 4th tick.** [I]
- *Why:* the `% 4` phase is [R]; that `*(player+0x21d8)` (`m_pCurrentCommand`) yields a per-tick command number is inferred.
- *Source:* [wallrun.md §5](wallrun.md).

**W5. Angle units and trig helpers.** [I, strong]
- *Claim:* `wallrun_rotateMaxRate` is in rad/s (≈ 172°/s); `FUN_1807dae00`/`FUN_1807db4c0` are cos/sin; `wallrun_minAngle_air` is a full cone angle.
- *Source:* [wallrun.md §2, §3.9](wallrun.md).

**W6. Wall repel may be inactive.** [?]
- *Claim:* `ApplyWallRepel` (`FUN_18017eb80`) has no caller among the analysed functions; `wallrun_repelSoftness` is probably its softness argument [I]. The repel times (0.2 / 0.4 s) are only real if something calls it.
- *Verify:* `RefsRange8.java`/`RelScan8.java` on `0x18017eb80`.
- *Source:* [wallrun.md §2, §3.6](wallrun.md).

**W7. View-tilt prediction.** [?]
- *Claim:* the server stores a predicted wall normal for the view roll, but its ballistic step mixes units: the drop uses `0.5·T²·g` without `sv_gravity`, the velocity uses `g·sv_gravity`. Either `FUN_18048c400` is not a plain scale, or this is a game bug.
- *Verify:* decompile `FUN_18048c400`.
- *Source:* [wallrun.md §3.10](wallrun.md).

**W8. Weak-run design intent.** [I]
- *Claim:* the up-boost and low-gravity start are meant to happen only after a jump, never after dropping off a ledge or a wall. The flag logic is [R]; the intent is a reading. Whether a wall jump's own wallrun end re-sets the weak flag was not traced.
- *Source:* [wallrun.md §3.4](wallrun.md).

**W9. Denuvo wallrun trap.** [I]/[?]
- *Claim:* on a copy judged tampered, CanWallRun fails after 120 s on maps whose name contains "_ti".
- *Why:* the flag-setting bytes are [R]; the tamper condition and its runtime value are not. Denuvo may replace the block at runtime.
- *Source:* [wallrun.md §5](wallrun.md), [TITANFALL2_MOVEMENT.md §12](TITANFALL2_MOVEMENT.md).

**W10. Curved-wall entity.** [?]
- *Claim:* `func_wallrun_curve` (`wallrun_curveEnable`, `FUN_1806073f0`, `FUN_180606820`) was not analysed.
- *Source:* [wallrun.md §4](wallrun.md).

---

## 7. Grapple

**GR1. Pilot initial impulse is capped at 50 u/s.** [R] as decompiled / unverified in game
- *Claim:* the 350 u/s attach impulse is clamped to `(1 − slow)·speedRampMin` = 50 u/s, so pilots start the pull almost from rest. The clamp order (max first, then min) was read from Ghidra output.
- *Verify:* disassemble the clamp in `FUN_18060c0c0`; in-game, grapple from a standstill and log speed over the first 0.3 s.
- *Source:* [grapple_zipline.md §1.4, §3](grapple_zipline.md).

**GR2. Weapon keys behind range and power.** [I, strong]
- *Claim:* weapon+0x1f3c/+0x1f40 are `grapple_maxLength`/`grapple_maxLengthVert`, and +0x1f38/+0x1f34 are `grapple_power_use_rate`/`grapple_power_required`. Shipped values are 1100 / 1100 and 50 / 50 [D] (`mp_ability_grapple.txt`), so a world grapple drains 50 power/s after the 0.25 s grace, up to 50 per grapple, and a reel-in drains 50 at once.
- *Why:* the range mapping is confirmed by the ellipse formula; the power slots by key order and use.
- *Verify:* map the weapon-settings descriptor table like the player table; in-game, count grapples on a full meter.
- *Source:* [grapple_zipline.md §0.2](grapple_zipline.md).

**GR3. Virtual-function and entity identities.** [I]
- *Claim:* vfuncs +0x2a0 IsTitan, +0x298 IsPlayer, +0x218 IsNPC, +0x9c8 grapple data, +0x478 gravity scale, +0x998 a surface normal (plausibly up); NPC AI type numbers in the CanBePulled mask; `FUN_180418e10` = team; `FUN_1806304d0` = enemy check; `FUN_1806458b0` = IsRodeoing.
- *Source:* [grapple_zipline.md §0, §1.4, §1.7](grapple_zipline.md).

**GR4. Grapple slow fraction.** [I]
- *Why:* `FUN_180222030` (a 0..1 slow that scales the ramp) is not dumped; the string `grapple_slow` exists.
- *Verify:* decompile it.
- *Source:* [grapple_zipline.md §0](grapple_zipline.md).

**GR5. Detach edge cases.** [I]/[?]
- *Claim:* detach rule 1 (an "invalid owner state" from `FUN_1800e2d90`/`FUN_1800e2e30`), the `mv+0xa4` vector in the passed-the-point test, surface flag 0x800 meaning no-grapple [I]; `FUN_1805db610` called above two wrap points [?].
- *Source:* [grapple_zipline.md §1.5 item 13, §1.6](grapple_zipline.md).

**GR6. Auto-melee details.** [I]
- *Claim:* player+0x18f0 is the queued grapple-melee time; button 0x40000 forces melee. The view-rotate ConVars are probably client-side [?].
- *Source:* [grapple_zipline.md §1.7](grapple_zipline.md).

---

## 8. Zipline

**Z1. Per-rope speed scale.** [?]
- *Claim:* the ride cap is `600 × m_ziplineMoveSpeedScale`; the per-rope scale is map or script data.
- *Verify:* read the map entity lumps for the rope keyvalue; in-game, ride a long rope with a speedometer.
- *Source:* [grapple_zipline.md §2](grapple_zipline.md), [TITANFALL2_MOVEMENT.md §13](TITANFALL2_MOVEMENT.md).

**Z2. Mount duration.** [?]
- *Claim:* the mount takes the length of viewmodel activity 0x136; `mountZiplineTime` 0.5 s is only a fallback.
- *Verify:* extract the pilot viewmodel's zipline-mount sequence.
- *Source:* [grapple_zipline.md §2](grapple_zipline.md).

**Z3. Gravity slows a descending rider.** [R] sign / [I] intent
- *Claim:* the sign is verified in the disassembly; whether it was intended is unknown. The magnitude uses entity gravity, assumed to be 0.8 (J1).
- *Source:* [grapple_zipline.md §2](grapple_zipline.md).

**Z4. "Steady-state speed is the cap on any slope".** [C]
- *Claim:* holds because accel 400 exceeds the gravity drag `375·eg·dir.z²` for any entity gravity up to about 1.06. It also assumes ropes with scale ≤ 1 hand over at the cap.
- *Source:* [grapple_zipline.md §2](grapple_zipline.md).

**Z5. Zipline names and timing.** [I]
- *Claim:* `m_ziplineNext`/`Prev` accessors, script callback names, the cooldown being decremented elsewhere, dismount bits (crouch drops, jump launches along view), and `FUN_180162ff0` as a detach condition.
- *Source:* [grapple_zipline.md §2](grapple_zipline.md).

---

## 9. Dodge and power

**P1. Dodge code for titans.** [I]
- *Claim:* MP pilots have `dodge 0` [D]; the dodge branch of CheckJumpButton is assumed to be what titan dashes use, with titan `.set` values (combat record dash speed/cost).
- *Verify:* in-game titan dash speed with a speedometer.
- *Source:* [ground_misc.md §3.1](ground_misc.md), [TITANFALL2_COMBAT.md "Chassis"](TITANFALL2_COMBAT.md).

**P2. Power penalties not decompiled.** [?]
- *Claim:* `FUN_180221ff0` (reduces dodge speed) and `FUN_180221fb0` (reduces regen) are not dumped; probably status effects.
- *Source:* [ground_misc.md §3.1, §3.5, §4](ground_misc.md).

**P3. Second dodge/drain user.** [?]
- *Claim:* `FUN_18062b3f0` has a dodge path and a continuous drain path (when player+0x24d0 is set); its context is unknown.
- *Verify:* find its callers.
- *Source:* [ground_misc.md §3.5](ground_misc.md), [TITANFALL2_MOVEMENT.md §13](TITANFALL2_MOVEMENT.md).

**P4. Sprint start caller.** [?]
- *Claim:* the input logic that calls StartSprint (and any sprint conditions it adds) was not found.
- *Source:* [ground_misc.md §6](ground_misc.md).

**P5. ADS hover and air slow-mo.** [I]
- *Claim:* hover starts on the ADS/zoom button bit (X2), uses the glide meter, and drives air slow-mo for pilots with `pas_ads_hover`; `FUN_1805d3bf0` is the ADS fraction (G7).
- *Verify:* in-game with the kit: aim in mid-air and time a fall.
- *Source:* [ground_misc.md §3.2](ground_misc.md).

---

## 10. Misc

**MI1. Stim timing.** [D] / [I]
- *Claim:* `sh_stim.gnut` adds `speed_boost` 0.4 for `duration + 0.5` s with a final argument 0.25 [D]; reading that argument as a 0.25 s fade-out is [I]. The 1.8× rests on G1.
- *Verify:* read `StatusEffect_AddTimed`'s native signature; in-game speed log through the end of a Stim.
- *Source:* [TITANFALL2_MOVEMENT.md §13](TITANFALL2_MOVEMENT.md).

**MI2. Phase Shift exit slow applies only to Frontier Defense variants.** [D]
- *Claim:* the movement and combat records stated `move_slow 0.75 for 0.75 s` on Phase Shift exit. In `mp_ability_shifter.nut` that call sits inside `if ( weaponOwner.IsPlayer() && (pmLevel >= 0) )`, and `GetPVEAbilityLevel` returns −1 unless the weapon has a `pm0`–`pm2` mod. Normal MP Phase Shift applies no exit slow in the extracted scripts. The movement summary was corrected; the combat record still states it.
- *Verify:* grep the server scripts for other `move_slow` uses on phase exit; in-game, measure speed right after a Phase Shift ends.
- *Source:* [TITANFALL2_MOVEMENT.md §13](TITANFALL2_MOVEMENT.md), [TITANFALL2_COMBAT.md "Phase Shift"](TITANFALL2_COMBAT.md).

**MI3. The role of `FUN_18016dd00`.** [?]
- *Claim:* the notes give it four readings: a position test or unstick (wallrun.md), the window-hint jump assist (slide_mantle.md), an "automantle probe" (jump_air_friction.md), and "purpose not established" after the grapple detach (grapple_zipline.md). The slide_mantle reading is the most detailed.
- *Verify:* decompile it fully and check its return value at each call site.
- *Source:* [slide_mantle.md §3](slide_mantle.md), [wallrun.md §2](wallrun.md), [jump_air_friction.md §2](jump_air_friction.md), [grapple_zipline.md §1.6a](grapple_zipline.md).

**MI4. Window traversal hint finder.** [I]
- *Claim:* `FUN_1806079b0` finds window hints (only its use of `window_hint_debug` and hint fields is seen).
- *Source:* [ground_misc.md §3.3](ground_misc.md), [grapple_zipline.md §0](grapple_zipline.md).

**MI5. Melee lunge movetype.** [I]
- *Claim:* movetype 0xf / `FUN_1801750f0` is the melee lunge, named from the `melee_lunge_*` ConVars it reads.
- *Source:* [jump_air_friction.md §2](jump_air_friction.md), [ground_misc.md §1](ground_misc.md).

**MI6. Denuvo speed trap.** [I]
- *Claim:* `Denuvo_SlowSpeed` multiplies move speed by 0.1 after 600 s on a copy judged tampered; as with W9, only the multiplier bytes are [R].
- *Source:* [jump_air_friction.md §3](jump_air_friction.md), [ground_misc.md §2.1](ground_misc.md).

---

## 11. Combat

**C1. Damage falloff is linear between the near, far and very-far points.** [I]
- *Why:* the engine's `CalcWeaponDamage` was not analysed; linear interpolation is the Source convention.
- *Verify:* decompile `CalcWeaponDamage` in server.dll; in-game, read hit markers at measured distances between near and far.
- *Source:* [TITANFALL2_COMBAT.md "How damage works"](TITANFALL2_COMBAT.md).

**C2. A direct hit lands impact damage plus full splash.** [I]
- *Why:* splash falloff from inner to outer radius, and how far the victim's hull is from the explosion, were not modelled. STK for Cold War, EPG, Sidewinder, Softball, Archer, MGL and Thunderbolt depends on this.
- *Verify:* decompile the radius-damage path; in-game direct-hit tests.
- *Source:* [TITANFALL2_COMBAT.md "How damage works"](TITANFALL2_COMBAT.md).

**C3. TTK and DPS are idealised.** [I]
- *Claim:* TTK = `(STK − 1)/shots per second` ignores projectile travel time, ADS time, misses, recoil and the near-to-far transition; "sustained DPS" assumes one magazine plus an empty reload; burst RPM uses `count/(count/fire_rate + burst_fire_delay)`.
- *Source:* [TITANFALL2_COMBAT.md "How the derived columns are computed"](TITANFALL2_COMBAT.md).

**C4. Titan dash count assumes 100 max power.** [I]
- *Why:* dash count = 100 / dash cost. Suit power regen stops at 100 [R] (UpdateSuitPower), which supports a 100 maximum, but titan kits and class mods are not applied.
- *Verify:* in-game, count dashes from full.
- *Source:* [TITANFALL2_COMBAT.md "Chassis"](TITANFALL2_COMBAT.md), [ground_misc.md §3.5](ground_misc.md).

**C5. Pilot `healthShield 200` has no known effect.** [?]
- *Why:* set in pilot_base [D], but no pilot shield mechanic was found in scripts or code.
- *Verify:* grep server scripts for shield-health use on pilots; in-game, check whether pilots ever show shield.
- *Source:* [TITANFALL2_COMBAT.md "How damage works"](TITANFALL2_COMBAT.md).

**C6. Offhand and core RPM/DPS are not meaningful.** [I]
- *Why:* those weapons use `fire_rate` and `ammo` as charge or script parameters; their damage is often script-driven.
- *Source:* [TITANFALL2_COMBAT.md "Titan weapons"](TITANFALL2_COMBAT.md).

**C7. Pellet counts come from script constants.** [D] / [I]
- *Claim:* Mastiff 8, Mozambique 3, Double-barrel 8, D-2 Double Take 3 come from the weapon scripts and `projectiles_per_shot`; that these are the counts fired in MP, with no mod or code-side change, is [I].
- *Verify:* read each weapon's `.nut` fire function; in-game, count impact decals.
- *Source:* [TITANFALL2_COMBAT.md "How damage works"](TITANFALL2_COMBAT.md).

**C8. Wingman B3 deals 40 near and far.** [D], surprising
- *Claim:* `mp_weapon_wingman.txt` `MP_BASE` sets `damage_near_value`/`damage_far_value` 40/40 (SP_BASE 90/45), which makes the B3 weaker than the Wingman Elite (75/55). Read as-is.
- *Verify:* check whether a playlist or default mod changes it; in-game hit markers.
- *Source:* [TITANFALL2_COMBAT.md "Pilot weapons"](TITANFALL2_COMBAT.md).

**C9. Combat record tag usage differs from the repo legend.** [I]
- *Claim:* TITANFALL2_COMBAT.md tags script reads (headshot range rule, ShotgunBlast, Stim, Holo Pilot) as [R]; under the repo legend they are [D]. Its Stim "1.8× [R]" is [C] resting on G1 [I], and its Phase Shift exit slow is limited to Frontier Defense variants (MI2).
- *Source:* [TITANFALL2_COMBAT.md](TITANFALL2_COMBAT.md).

**C10. Scope of the combat tables.** [D] / [I]
- *Claim:* only `MP_BASE` blocks are applied; attachments, Amped mods, kits and class mods are not. Tactical cooldown is taken as `ammo_per_shot / regen_ammo_refill_rate` [I].
- *Source:* [TITANFALL2_COMBAT.md](TITANFALL2_COMBAT.md).

---

## 12. Stale or conflicting statements in the detailed notes

These are not new assumptions; they are places where a detailed note is out of date or disagrees with a later pass. A reader should prefer the later source.

- **slide_mantle.md §1.3 and §5** say `slideDecel`, `slideWantToStopDecel`, `slideVelocityDecay` and `slideAccel` have no consumer and the slide deceleration "cannot be stated". The later logic3/logic4 passes found them: [jump_air_friction.md §2](jump_air_friction.md) (Friction, GroundDecel) and [ground_misc.md §2.3](ground_misc.md).
- **slide_mantle.md §1.4** says `slideMaxStopSpeed` has no consumer. [ground_misc.md §2.6](ground_misc.md) found it: Duck forces the crouch while sliding faster than it.
- **slide_mantle.md §0 and §2.6** tag the slide-key offsets and `mantleAngleScale` (0x114) as [I] and say the mantle jump-off kick fields settings+0x228/+0x230 are unresolved. The settings table ([player_settings_table.md](player_settings_table.md)) gives all of them [R]: the kick uses `wallrunJumpUpSpeed`/`wallrunJumpOutwardSpeed`, which are 230 / 205 for MP pilots [D].
- **jump_air_friction.md §2** says the meaning of settings.b110 is [?]. The settings table names it `superjumpCanUseAfterWallrun` (default 1) and names b118/f130 `boostEnabled`/`boostOnGroundSafety` (0 / 0.2), so the "second coyote window" belongs to the jetpack-boost class and is off for MP pilots [R][D]. The "Resolved by logic4" list in §5 also lists these meanings and "all per-class numbers" without saying where they were resolved.
- **wallrun.md §2** marks the `pl.0x2250` condition in CanWallRun "meaning unknown"; the datamap names 0x2250 `m_duckState` [R] ([jump_air_friction.md §0](jump_air_friction.md)). It also reads player+0x2588 as a last-ground time (see W1) and `FUN_1805e6af0` as IsDodging (see W2).
- **player_settings_table.md, ground_misc.md, wallrun.md §5 and grapple_zipline.md** say the `.set` files and weapon `.txt` files are unavailable. They have since been extracted; shipped values are in [TITANFALL2_MOVEMENT.md §0](TITANFALL2_MOVEMENT.md). player_settings_table.md also says the walk/sprint code defaults were not recovered, while [ground_misc.md §2.1](ground_misc.md) gives 120 / 0 [R].
- **wallrun.md and player_settings_table.md** contained double-encoded UTF-8 (mojibake). This was repaired before publication with `tools/node/fixmojibake.js`.

---

## 13. Verified against shipped data or a second source

Each of these was confirmed by two independent sources.

1. **Settings-table offsets match the code that reads them.** The `.rdata` descriptor table [R] gives wallrun friction at 0x224, wall-jump speeds at 0x228/0x22c/0x230, wallstick at 0x218, slide stop speed at 0x26c, side/back scales at 0x98/0x9c and the sound-name strings, and the decompiled consumers [R] read exactly those offsets for those purposes. ([player_settings_table.md](player_settings_table.md) step 5, [wallrun.md §4](wallrun.md))
2. **Slide and grapple key offsets.** The key order in `.rdata`, the help strings and the consumer offsets agree with the descriptor table for every slide key (0x258–0x280) and grapple key (0x284–0x2cc). ([slide_mantle.md §0](slide_mantle.md), [grapple_zipline.md §0.3](grapple_zipline.md))
3. **Shipped `.set` keys match server.dll field names.** Every movement key in pilot_base/pilot_mp checked (`jumpHeight`, `gravityScale`, `stepHeight`, `superjump*`, `wallrunJump*`, `wallrunMaxSpeedVertical`, `slideDecel`, `slideVelocityDecay`, `grapple_detachSpeedLoss(Min)`, `grapple_power_regen_*`, `ziplineSpeed`, `sprintViewOffset`, `sprinttiltMaxRoll`, `impactSpeed`, `hoverEnabled`) exists in the settings table or the class-mod parser [R][D].
4. **Double-jump `t = 0`.** The decompiled stub, its raw bytes `0f 57 c0 c3` (`xorps xmm0,xmm0; ret`), and the Holo-Pilot decoy recomputing the same formula with H = settings+0x100 all agree. ([jump_air_friction.md §2, §3](jump_air_friction.md))
5. **No edge friction.** The Ghidra xrefs, raw 8-byte pointer and 4-byte RVA scans, and a rel32 brute force over `.text` all find no caller of `0x18015fe70` and no read of `sv_edgefriction`. The same scan finds the known reference of `sprint_powerdrain`, so the method works. ([jump_air_friction.md §2](jump_air_friction.md))
6. **Grapple detach speed loss.** The decompile and the disassembly agree, it is the only reader of +0x2a0/+0x2a4, and the shipped files set exactly those two keys (300 / 460). ([grapple_zipline.md §1.6a](grapple_zipline.md))
7. **Zipline gravity sign and acceleration reader.** The decompile and instruction-level disassembly agree on the sign. An all-instruction displacement scan shows `Zipline_MoveUpdateSliding` is the only movement reader of `ziplineAcceleration`. ([grapple_zipline.md §0.4, §2](grapple_zipline.md))
8. **Sprint is the only suit-power device, and dodge charges exactly `dodgePowerDrain`.** The `.data` device table bytes agree with the decompiled UpdateSuitPower. The dodge drain argument is read from the register load (`XMM1 = [player+0x1d34]`) in the disassembly as well as the decompile. ([ground_misc.md §3.5](ground_misc.md))
9. **Stim's factor of 2.** The hard-coded `1 + 2·effect` in SpeedScale [R] matches the `sh_stim.gnut` comment assuming an extra scale of 2.0 [D]. This confirms the factor, not the effect identity (G1).
10. **Power Cell location.** `mp_ability_grapple.txt` marks its own `pas_power_cell` mod "not used, actual setting in pilot_mp.set" [D], and pilot_mp.set carries `grapple_power_regen_rate *1.35` [D].
11. **All tacticals move identically.** No pilot class file other than pilot_base/pilot_mp (and the non-MP coliseum, solo and boost sets) sets any movement field [D].
12. **Datamap names agree with usage.** `m_upDir`, `m_airSpeed`/`m_airAcceleration`, `m_flTimeLastLanded`, `m_sliding`, `m_traversal*` and `m_grapple*` come from datamap strings [R] and match how the code uses each offset [R]. ([jump_air_friction.md §0](jump_air_friction.md), [slide_mantle.md §0](slide_mantle.md), [grapple_zipline.md §0.1](grapple_zipline.md))

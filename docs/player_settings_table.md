# Titanfall 2 player-class settings table (server.dll, code defaults)

> **Errata:** some statements in this note were superseded by later analysis passes or by the extracted `.set`/weapon files. See [ASSUMPTIONS.md §12](ASSUMPTIONS.md) for the list, and [TITANFALL2_MOVEMENT.md §0](TITANFALL2_MOVEMENT.md) for shipped values. Prefer those sources where they disagree.


> **Important: these are CODE DEFAULTS, not shipped pilot values.**
> Each value below is the default string compiled into server.dll for a player-class (`.set` / playersettings)
> field. The game uses it only when a class file does not set the field. The shipped pilot `.set` files in the VPK
> (LZHAM-compressed, not available here) can and very likely do override many of them: speeds, jump heights,
> wallrun/slide tuning, gravityScale, and so on.
> Fields marked **req** with no default must come from the class file. Their real values are unknown here
> (e.g. `jumpHeight`, `stepHeight`, `fov`).

## How the table was located and decoded (re-checkable)

1. **Locate.** I searched server.dll's raw bytes for the ASCII string `wallrunJumpUpSpeed`, which sits at VA
   0x18094c100 in `.rdata`. I then searched the whole file for an 8-byte little-endian pointer to that VA. Exactly
   one hit came back, at VA 0x180b824f0 in `.data`. Its neighbours are pointers to other setting names in a regular
   0x30-byte stride.
   - PE sections (raw-offset mapping used): `.text` VA 0x1000 / raw 0x400; `.rdata` VA 0x866000 / raw 0x865400
     (size 0x289e00); `.data` VA 0xaf0000 / raw 0xaef400. Image base 0x180000000.
   - The table lives in `.data`. To read it from the file, use file offset = VA − 0x180000000 − 0xaf0000 + 0xaef400.
2. **Entry layout (0x30 bytes, little-endian).**
   ```
   +0x00  char* name            (.rdata string)
   +0x08  char* defaultString   (.rdata string, or NULL = no code default)
   +0x10  char* helpText        (.rdata string, or NULL)
   +0x18  int32 type            1=bool, 4=float, 12=vec3 (3 floats), 16/32/64 = char buffer of that size
   +0x1c  int32 structOffset    byte offset of the field inside the settings struct
   +0x20  uint32 kind           1=float, 2=string, 3=vector, 5=float (unknown variant), 6=bool  [kind meaning inferred]
   +0x24  uint32 flagBits       bit0 (0x1) = required (no default; must be in class file)
                                bit8 (0x100) = range-checked                                   [meanings inferred]
   +0x28  int32/float min, +0x2c int32/float max
          0x80000001 / 0x7fffffff when unused; float range when bit8 set (fov 0..179, dodgeHeight 0..FLT_MAX)
   ```
   Evidence for this reading: every entry with the `required` bit has a NULL default pointer. Four non-required
   entries (melee_cone_trace_range/angle, viewPunchSpring, footstepInterval) also have NULL defaults, so the match
   is not exact. The range reading comes from the 3 entries that have float min/max values.
3. **Extent.** Walking forward from VA 0x180b80ff0 (`printName`, offset 0x20) in 0x30 steps gives 421 valid entries
   up to VA 0x180b85f10. Two records in that span are not field entries (VA 0x180b85a90 and 0x180b85c10: small
   integer instead of a name pointer, likely sub-table separators). There are no entries after 0x180b85f10. The 0x30
   bytes before `printName` belong to a different, non-matching layout.
   - A separate scan of VA 0x180b7e000–0x180b88000 at 8-byte alignment confirmed this. It looked for {identifier
     string ptr, string/NULL, string/NULL, valid type, offset < 0x10000}. It found no entries outside the range above.
   - The scan rejected 5 entries only because their names contain brackets (`gibAttachmentIdx[0..4]`); those are
     included below.
4. **Three runs of entries.**
   - VA 0x180b80ff0–0x180b85a60: main struct.
   - VA 0x180b85ac0–0x180b85be0: 7 more main-struct fields (doFaceAnim, bodyModel, armsModel, cockpitModel,
     weaponClass, class, viewPunchSpring). Their offsets fill gaps in the main struct, so they are merged into table A.
   - VA 0x180b85c40–0x180b85f10: 16 fields at offsets 0x0–0x5c (viewheight, hull_min/max, footsteps,
     lowSpeed/acceleration/deceleration…). This is a separate **per-stance** block; the help texts say "for current
     stance". It is listed as table B.
5. **Cross-check against code.** The wallrun code reads `settings+0x224` as friction, `+0x228/0x22c/0x230` in the
   wall-jump function, `+0x218` as the wallstick switch, `+0x26c` as slide stop speed, `+0x98/0x9c` as side/back
   input scales, and `+0xbcc…` as sound names. These all agree with the table (see `wallrun.md`).

Scripts used (scratchpad, PowerShell): `refs.ps1` (string → pointer search), `dumptable.ps1` (walk the table to CSV),
`scan.ps1` (independent scan), `tomd.ps1` (render). The raw CSV has entry VA, type, kind/flags and min/max for every
row.

## Not in this table
- **Base walk / sprint / crouch move speeds** are not fields here. The string `sprintspeed` exists in server.dll next
  to the class-field strings parsed by `fn_180553b60` (healthShield, dodgeSpeed, wallrunAccelerate*,
  wallrunMaxSpeedHorizontal, wallrun_timeLimit, wallrun_hangTimeLimit, airSlowMoSpeed, …). Those fields are parsed by
  separate code with defaults supplied there, not in this table.
- Their code defaults were not recovered. The parser passes default pointers such as `&DAT_1808884d4`; reading those
  would be the next step.
- `dodgeSpeed`, `wallrunAccelerateVertical/Horizontal`, `wallrunMaxSpeedHorizontal`, `wallrun_timeLimit` and
  `wallrun_hangTimeLimit` are also in that parser, not here.
- Per-stance `speed` does not appear. Table B only has acceleration and deceleration related fields.

## Movement quick reference (code defaults; see full tables for help text)
| Area | Fields (offset: default) |
|---|---|
| Jump / air | jumpHeight 0x94: *req*; airSpeed 0xbc: 60; airAcceleration 0xc0: 500; gravityScale 0x265c: 1.0; stepHeight 0xa0: *req*; stepTestDist 0xa4: 10; speedScaleSide 0x98: 1; speedScaleBack 0x9c: 1 |
| Double jump ("superjump") | superjumpMinHeight 0x100: 80; superjumpMaxHeight 0x104: 80; superjumpLimit 0x108: 1; superjumpHorzSpeed 0x10c: 240; superjumpCanUseAfterWallrun 0x110: 1 |
| Wallrun | wallstickEnabled 0x218: 0; wallrunMaxSpeedVertical 0x21c: 300; wallrunMaxSpeedHorizontalBackward 0x220: 50; wallrunFriction 0x224: 4; wallrunJumpUpSpeed 0x228: 320; wallrunJumpInputDirSpeed 0x22c: 100; wallrunJumpOutwardSpeed 0x230: 250; wallrunSameWallAllowed 0x234: 1 |
| Dodge | 0x238–0x254 (dodgeAnimDuration 0.5, dodgeInterval 0.6, dodgeStopSpeed 300, dodgeHeight 25, dodgeHeightMax 25, dodgeVerticalHeight 200, dodgePowerDelay 0, dodgeKeepSpeedFrac 0) |
| Slide | slideDecel 0x258: 160; slideWantToStopDecel 0x25c: 400; slideVelocityDecay 0x260: 0.85; slideAccel 0x264: 50; slideRequiredStartSpeed 0x268: 200; slideStopSpeed 0x26c: 125; slideMaxStopSpeed 0x270: 350; slideMaxJumpSpeed 0x274: 350; slideSpeedBoost 0x278: 150; slideSpeedBoostCap 0x27c: 400; slideJumpHeight 0x280: 50 |
| Grapple | 0x284–0x2c8 grapple_* (see table A) |
| Per-stance (table B) | acceleration +0x48: 2500; deceleration +0x4c: −1; lowSpeed +0x44: −1; lowAcceleration +0x50: −1; sprintAcceleration +0x54: −1; sprintDeceleration +0x58: −1. A −1 presumably means "fall back to another value" [inf] |
| Sprint view | sprintViewOffset 0x2660: −10; sprintStartDelay 0x2664: 0.2; sprintStartDuration 0x2668: 0.3; sprintStartFastDuration 0x266c: 0.2; sprintEndDuration 0x2670: 0.15; sprinttiltMaxRoll 0xf8: 10 |


## A. Main player-class settings struct (405 fields, sorted by offset)

| Offset | Field | Type | Kind/flags | Code default | Range | Help text (verbatim from binary) | Entry VA |
|---|---|---|---|---|---|---|---|
| 0x20 | printName | str[32] | 2 | "" |  | The printable name of this class | 0x180b80ff0 |
| 0x40 | shortPrintName | str[32] | 2 | "" |  | The printable short name of this class | 0x180b81020 |
| 0x60 | description | str[32] | 2 | "" |  | The printable description of this class | 0x180b81050 |
| 0x80 | automantle | bool | 6, req | *(none)* |  | Enables climbing onto objects while jumping | 0x180b81080 |
| 0x81 | healthpacks | bool | 6, req | *(none)* |  | Enables usage of health packs | 0x180b810b0 |
| 0x82 | jump | bool | 6, req | *(none)* |  | Enables basic jump | 0x180b810e0 |
| 0x83 | crouch | bool | 6 | `1` |  | Enables crouching | 0x180b81110 |
| 0x84 | slide | bool | 6 | `1` |  | Enables sliding | 0x180b81140 |
| 0x85 | mechanical | bool | 6 | `1` |  | Spawn mechanical gib instead of fleshy gib | 0x180b81170 |
| 0x88 | fov | float | 1, req | *(none)* | 0 .. 179 | Vision field of view (angle) | 0x180b811a0 |
| 0x8c | viewmodelfov | float | 1, req | *(none)* | 0 .. 179 | Vision field of view (angle) for viewmodel | 0x180b811d0 |
| 0x90 | viewmodelDuckOffset | float | 1 | `0` |  | Distance to translate the player's viewmodel up as they duck. | 0x180b81200 |
| 0x94 | jumpHeight | float | 1, req | *(none)* |  | How high the player can jump | 0x180b81230 |
| 0x98 | speedScaleSide | float | 1 | `1` |  | Scales sideways movement speed | 0x180b81260 |
| 0x9c | speedScaleBack | float | 1 | `1` |  | Scales backward movement speed | 0x180b81290 |
| 0xa0 | stepHeight | float | 1, req | *(none)* |  | Height that can be stepped up automatically (for stairs) | 0x180b812c0 |
| 0xa4 | stepTestDist | float | 1 | `10` |  | Steep slopes shorter than this will be ignored when stepping onto an object | 0x180b812f0 |
| 0xa8 | physicsMass | float | 1 | `100` |  | Mass of the player for physics calculations | 0x180b81320 |
| 0xac | physicsIgnoreBelowMass | float | 1 | `10` |  | Mass below which physics objects don't affect the player | 0x180b81350 |
| 0xb0 | physicsSoftBelowMass | float | 1 | `300` |  | Mass below which physics objects use soft collision with the player (not real physics) | 0x180b81380 |
| 0xb4 | physicsPushMassLimit | float | 1 | `5000` |  | Maximum mass the player can push | 0x180b813b0 |
| 0xb8 | physicsPushSpeedLimit | float | 1 | `50` |  | Maximum speed the player can push physics objects | 0x180b813e0 |
| 0xbc | airSpeed | float | 1 | `60` |  | Max speed player can adjust to in the air | 0x180b81410 |
| 0xc0 | airAcceleration | float | 1 | `500` |  | Acceleration when in the air (units/sec/sec) | 0x180b81440 |
| 0xc8 | impactSpeed | float | 1 | `580` |  | Collision velocity needed to consider it fast enough to play impact effects | 0x180b81470 |
| 0xcc | jumpNoiseDuration | float | 1 | `1` |  | Duration of noise created by jumping | 0x180b814a0 |
| 0xd0 | jumpNoiseRadius | float | 1 | `300` |  | Radius of noise created by jumping | 0x180b814d0 |
| 0xd4 | superJumpNoiseRadius | float | 1 | `400` |  | Radius of noise created by super jumping | 0x180b81500 |
| 0xd8 | dodgeNoiseRadius | float | 1 | `500` |  | Radius of noise created by dodging | 0x180b81530 |
| 0xdc | landingNoiseDuration | float | 1 | `1` |  | Duration of noise created by landing from a fall | 0x180b81560 |
| 0xe0 | landingNoiseRadius | float | 1 | `300` |  | Radius of noise created by landing from a fall | 0x180b81590 |
| 0xe4 | hardLandingNoiseRadius | float | 1 | `400` |  | Radius of noise created by heavily landing from a fall | 0x180b815c0 |
| 0xe8 | hardFallDist | float | 1 | `20` |  | Approximate distance the player must fall to trigger a 'heavy landing' sound. Converted into and compared against impact velocity. | 0x180b815f0 |
| 0xec | viewkickFallDistMin | float | 1 | `10` |  | Start of the range for figuring viewkick from a fall. Converted into and compared against impact velocity. | 0x180b81620 |
| 0xf0 | viewkickFallDistMax | float | 1 | `30` |  | End of the range for figuring viewkick from a fall. Converted into and compared against impact velocity. | 0x180b81650 |
| 0xf4 | viewkickFallDistMaxScale | float | 1 | `6.0` |  | Max viewkick from a fall. | 0x180b81680 |
| 0xf8 | sprinttiltMaxRoll | float | 1 | `10` |  | Degrees of view tilt when turning while sprinting | 0x180b816b0 |
| 0xfc | grappleEnabled | bool | 6 | `0` |  | Enables grapple. | 0x180b816e0 |
| 0x100 | superjumpMinHeight | float | 1 | `80` |  | Height of super jump when jump power is down to superjump_min_power_use | 0x180b81710 |
| 0x104 | superjumpMaxHeight | float | 1 | `80` |  | Height of super jump when jump power is down at least superjump_max_power_use | 0x180b81740 |
| 0x108 | superjumpLimit | float | 1 | `1` |  | Number of super jumps allowed before landing | 0x180b81770 |
| 0x10c | superjumpHorzSpeed | float | 1 | `240` |  | Horizontal movement speed when super jumping while pushing in a direction | 0x180b817a0 |
| 0x110 | superjumpCanUseAfterWallrun | bool | 6 | `1` |  | Allows the use of super jump after wallrunning | 0x180b817d0 |
| 0x114 | mantleAngleScale | float | 1 | `1` |  | Scales the maximum mantle angle convar. Allows different mantle angle settings for boost and wallrunner as requested by Haggerty. | 0x180b81800 |
| 0x118 | boostEnabled | bool | 6 | `0` |  | Enables boost | 0x180b81830 |
| 0x11c | boostThrust | float | 1 | `2` |  | Amount of vertical thrust for boost. 0 is no thrust. 1 is 1G acceleration up. 2 is 2G acceleration. | 0x180b81860 |
| 0x120 | boostTightness | float | 1 | `2` |  | Vertical tightness for boost. Enables faster acceleration during boost without jumps going too high. | 0x180b81890 |
| 0x124 | boostDelay | float | 1 | `0` |  | Seconds into boost before boost thrust starts being applied. Meant to provide more consistent jump impulse on tap | 0x180b818c0 |
| 0x128 | boostDuration | float | 1 | `1` |  | Maximum boost duration in seconds. | 0x180b818f0 |
| 0x12c | boostRodeoSafety | float | 1 | `1.0` |  | You can boost if you have rodeo'd within this time. Fixes not being able to boost off a rodeo. | 0x180b81920 |
| 0x130 | boostOnGroundSafety | float | 1 | `0.2` |  | You can boost if you have touched ground within this time. Avoids missing out on boost because you are not on-ground on that exact frame. | 0x180b81950 |
| 0x134 | boostHorizontalDragDelay | float | 1 | `-1` |  | Delay in seconds between jump and horizontal drag applying. | 0x180b819b0 |
| 0x138 | boostHorizontalDragSpeed | float | 1 | `0` |  | Horizontal speed at which drag is applied after drag delay. 0 disables horizontal boost drag. | 0x180b81980 |
| 0x13c | boostHorizontalDragAmount | float | 1 | `10` |  | Amount of drag applied in velocity units per-second. | 0x180b819e0 |
| 0x140 | boostAutoClimbMeterThreshold | float | 1 | `0.1` |  | Threshold of boost meter below which climb starts automatically (without button press) if you continue to hold down jump near the apex of boost. | 0x180b81a10 |
| 0x144 | boostAutoGlideMeterThreshold | float | 1 | `0.0` |  | Threshold of boost meter below which glide starts automatically (without button press) if you continue to hold down jump near the apex of boost. | 0x180b81a40 |
| 0x148 | boostAutoJetpackMeterThreshold | float | 1 | `0.0` |  | Threshold of boost meter below which jetpack starts automatically (without button press) if you continue to hold down jump near the apex of boost. | 0x180b81a70 |
| 0x14c | boostRepeatedBoostsAreShort | bool | 6 | `0` |  | If true then repeated boosts (boosts without full glide meter) are short and immediately transition to jet state | 0x180b81aa0 |
| 0x150 | boostRepeatedBoostMeterCost | float | 1 | `0` |  | Instantaneous meter cost of each repeated boost. Meant to discourage bunny hopping and skipping exploits with the boost class | 0x180b81ad0 |
| 0x154 | jetpackEnabled | bool | 6 | `0` |  | Enables jetpack | 0x180b81b00 |
| 0x158 | jetpackThrust | float | 1 | `1.5` |  | Amount of vertical thrust for jetpack while travelling up. 0 is no thrust. 1 is 1G acceleration up. 2 is 2G acceleration. | 0x180b81b30 |
| 0x15c | jetpackMeterRate | float | 1 | `1.0` |  | Rate multiplier for how much glide meter the jetpack. 2.0 means jetpack uses meter at twice the rate of glide. | 0x180b81b60 |
| 0x160 | jetpackTightnessUp | float | 1 | `0.5` |  | Jetpack tightness applied to reduce up velocity. 0.0 total adjustment, 1.0 no adjustment. Small values are typically good, eg. 0.0 -> 0.1 range | 0x180b81b90 |
| 0x164 | jetpackTightnessDown | float | 1 | `0.5` |  | Jetpack tightness applied to reduce down velocity. 0.0 total adjustment, 1.0 no adjustment. Small values are typically good, eg. 0.0 -> 0.1 range | 0x180b81bc0 |
| 0x168 | jetpackTightnessSideways | float | 1 | `0.5` |  | Jetpack tightness applied to reduce sideways velocity. 0.0 total adjustment, 1.0 no adjustment. Small values are typically good, eg. 0.0 -> 0.1 range | 0x180b81bf0 |
| 0x16c | jetpackStrafe | float | 1 | `600` |  | Jetpack strafe acceleration applied according to left stick. | 0x180b81c20 |
| 0x170 | jetpackStrafeTaperStart | float | 1 | `150` |  | Start tapering jetpack strafe acceleration towards zero at this speed. | 0x180b81c50 |
| 0x174 | jetpackStrafeTaperFinish | float | 1 | `300` |  | Jetpack strafe acceleration is tapered to zero at this speed, eg. max jetpack strafe speed | 0x180b81c80 |
| 0x178 | jetpackBlendToGlideThreshold | float | 1 | `0` |  | Vertical speed threshold below which jetpack parameters (thrust, tightness, redirect) are blended towards glide values | 0x180b81cb0 |
| 0x17c | glideEnabled | bool | 6 | `0` |  | Enables glide | 0x180b81ce0 |
| 0x180 | glideThrust | float | 1 | `1` |  | Amount of vertical thrust for glide. 0 is no thrust. 1 is 1G acceleration up. 2 is 2G acceleration. | 0x180b81d10 |
| 0x184 | glideThreshold | float | 1 | `0` |  | Threshold velocity z below which glide kicks in instead of jetpack. | 0x180b81d40 |
| 0x188 | glideTightnessUp | float | 1 | `0` |  | Glide tightness applied to reduce up velocity. 0.0 total adjustment, 1.0 no adjustment. Small values are typically good, eg. 0.0 -> 0.1 range | 0x180b81d70 |
| 0x18c | glideTightnessDown | float | 1 | `0` |  | Glide tightness applied to reduce down velocity. 0.0 total adjustment, 1.0 no adjustment. Small values are typically good, eg. 0.0 -> 0.1 range | 0x180b81da0 |
| 0x190 | glideTightnessSideways | float | 1 | `0` |  | Glide tightness applied to reduce sideways velocity. 0.0 total adjustment, 1.0 no adjustment. Small values are typically good, eg. 0.0 -> 0.1 range | 0x180b81dd0 |
| 0x194 | glideRedirectDown | float | 1 | `0` |  | Amount of down movement that is redirected to forward when glide is applied. 0 is no redirection. 1 is 100% redirect. | 0x180b81e00 |
| 0x198 | glideRedirectSideways | float | 1 | `0` |  | Amount of sideways movement that is redirected to forward when glide is applied. 0 is no redirection. 1 is 100% redirect. | 0x180b81e30 |
| 0x19c | glideDuration | float | 1 | `1` |  | Maximum glide duration in seconds. | 0x180b81e60 |
| 0x1a0 | glideRechargeRate | float | 1 | `0` |  | Rate at which the glide meter recharges. 0 disables recharge. 1 recharges at 1:1 mapping to real time, 2 recharges twice as fast and so on. | 0x180b81e90 |
| 0x1a4 | glideRechargeDelay | float | 1 | `0` |  | Delay after last use of glide before the meter starts recharging. | 0x180b81ec0 |
| 0x1a8 | glideStrafe | float | 1 | `0` |  | Glide strafe acceleration applied according to left stick. | 0x180b81ef0 |
| 0x1ac | glideStrafeTaperStart | float | 1 | `0` |  | Start tapering glide strafe acceleration towards zero at this speed. | 0x180b81f20 |
| 0x1b0 | glideStrafeTaperFinish | float | 1 | `0` |  | Glides strafe acceleration is tapered to zero at this speed, eg. max glide strafe speed | 0x180b81f50 |
| 0x1b4 | glideRedirectBelowLastJumpHeight | bool | 6 | `0` |  | If true then glide redirect is only applied below the height of the last point jumped from plus jump height offset | 0x180b81f80 |
| 0x1b8 | glideRedirectJumpHeightStartOffset | float | 1 | `0` |  | Offset added to the ground height at the last point of jump to get the height below which glide redirect starts being faded in | 0x180b81fb0 |
| 0x1bc | glideRedirectJumpHeightFinishOffset | float | 1 | `0` |  | Offset added to the ground height at the last point of jump to get the height below which glide redirect finishes being faded in | 0x180b81fe0 |
| 0x1c0 | glideRedirectFadeOutStartSpeed | float | 1 | `0` |  | Offset added to the ground height at the last point of jump to get the height below which glide redirect applies | 0x180b82010 |
| 0x1c4 | glideRedirectFadeOutFinishSpeed | float | 1 | `0` |  | Offset added to the ground height at the last point of jump to get the height below which glide redirect applies | 0x180b82040 |
| 0x1c8 | airStrafeEnabled | bool | 6 | `0` |  | Enables air strafe. | 0x180b82070 |
| 0x1cc | airStrafeAcceleration | float | 1 | `0` |  | Amount of air strafe acceleration to apply while in the air. | 0x180b820a0 |
| 0x1d0 | airStrafeTaperStart | float | 1 | `0` |  | Start tapering air strafe acceleration towards zero at this speed. | 0x180b820d0 |
| 0x1d4 | airStrafeTaperFinish | float | 1 | `0` |  | Air strafe acceleration is tapered to zero at this speed, eg. max air strafe speed | 0x180b82100 |
| 0x1d8 | hoverEnabled | bool | 6 | `0` |  | Enables ADS hover | 0x180b82130 |
| 0x1dc | hoverTightnessVertical | float | 1 | `0.15` |  | Vertical tightness of velocity ease-in to hover. Eases player velocity towards zero. 0 doesn't apply any easing-in. 1 instantly reduces vertical velocity to zero. | 0x180b82160 |
| 0x1e0 | hoverTightnessHorizontal | float | 1 | `0.08` |  | Horizontal tightness of velocity ease-in to hover. Eases player velocity towards zero. 0 doesn't apply any easing-in. 1 instantly reduces horizontal velocity to zero. | 0x180b82190 |
| 0x1e4 | hoverMeterRate | float | 1 | `1.0` |  | Rate multiplier for how much boost meter the hover uses per-second. 0.1 means use 1/10th of the regular boost meter rate. | 0x180b821c0 |
| 0x1e8 | hoverStrafeScale | float | 1 | `0.0` |  | Scale the air strafe while hovering. Allows tuning of ADS tune speed. 0 disables strafing while hovering. | 0x180b821f0 |
| 0x1ec | hoverSafety | float | 1 | `0.25` |  | Hover engages only if you have been in the air longer than this amount of time. Stops hover kicking in when you ADS sprint across bumpy terrain. | 0x180b82220 |
| 0x1f0 | climbEnabled | bool | 6 | `0` |  | Enables climb | 0x180b82250 |
| 0x1f4 | climbThrust | float | 1 | `2` |  | Amount of vertical thrust while climbing. | 0x180b82280 |
| 0x1f8 | climbSafety | float | 1 | `0.25` |  | Amount of time after touching a wall for which climb continues. | 0x180b822b0 |
| 0x1fc | climbThreshold | float | 1 | `0.5` |  | Normal z absolute value threshold below which a surface is considered a climbable wall. | 0x180b822e0 |
| 0x200 | climbMeterRate | float | 1 | `1.0` |  | While climbing scale glide meter usage by this amount. | 0x180b82310 |
| 0x204 | climbTaperStart | float | 1 | `0` |  | Start tapering climb thrust to zero at this vertical speed. | 0x180b82340 |
| 0x208 | climbTaperFinish | float | 1 | `0` |  | Climb thrust is tapered to zero at this speed | 0x180b82370 |
| 0x20c | climbStrafeScale | float | 1 | `1.0` |  | Air strafe is scaled by this amount while climbing | 0x180b823a0 |
| 0x210 | climbTightness | float | 1 | `0` |  | Tightness that reduces horizontal motion while climbing | 0x180b823d0 |
| 0x214 | climbMeterMinimum | float | 1 | `0` |  | Minimum amount of meter required to start a climb | 0x180b82400 |
| 0x218 | wallstickEnabled | bool | 6 | `0` |  | Enables wall stick. You must also have wall running enabled for this to work. | 0x180b82430 |
| 0x21c | wallrunMaxSpeedVertical | float | 1 | `300` |  | Maximum vertical speed while wall running | 0x180b82460 |
| 0x220 | wallrunMaxSpeedHorizontalBackward | float | 1 | `50` |  | Maximum reverse horizontal speed while wall running | 0x180b82490 |
| 0x224 | wallrunFriction | float | 1 | `4` |  | Base friction applied while wall running. Use CPlayer.SetWallRunFriction to modify friction per-player. | 0x180b824c0 |
| 0x228 | wallrunJumpUpSpeed | float | 1 | `320` |  | Vertical speed applied when jumping out of a wall run. | 0x180b824f0 |
| 0x22c | wallrunJumpInputDirSpeed | float | 1 | `100` |  | Speed applied in the direction of player input when jumping out of a wall run. | 0x180b82520 |
| 0x230 | wallrunJumpOutwardSpeed | float | 1 | `250` |  | Speed away from the wall when jumping out of a wall run. | 0x180b82550 |
| 0x234 | wallrunSameWallAllowed | bool | 6 | `1` |  | Allows wallrun on the same wall twice in a row, using wallrun_sameWallDist and wallrun_sameWallSlope. | 0x180b82580 |
| 0x238 | dodgeAnimDuration | float | 1 | `0.5` |  | Minimum length of third person dodge float animation | 0x180b825b0 |
| 0x23c | dodgeInterval | float | 1 | `0.6` |  | Time before another dodge is allowed | 0x180b825e0 |
| 0x240 | dodgeStopSpeed | float | 1 | `300` |  | Speed below which dodging ends early | 0x180b82610 |
| 0x244 | dodgeHeight | float | 1 | `25` | 0 .. 3.402823E+38 | Height attained when dodging | 0x180b82640 |
| 0x248 | dodgeHeightMax | float | 1 | `25` |  | Max height allowed when dodging (vertical velocity will be decreased if necessary) | 0x180b82670 |
| 0x24c | dodgeVerticalHeight | float | 1 | `200` |  | Height attained when dodging without horizontal motion | 0x180b826a0 |
| 0x250 | dodgePowerDelay | float | 1 | `0` |  | Time before suit power recharges after dodging | 0x180b826d0 |
| 0x254 | dodgeKeepSpeedFrac | float | 1 | `0` |  | Fraction of old speed kept when dodging | 0x180b82700 |
| 0x258 | slideDecel | float | 1 | `160` |  | deceleration while sliding (fixed velocity lost per second) | 0x180b82730 |
| 0x25c | slideWantToStopDecel | float | 1 | `400` |  | deceleration while sliding when the player is trying to stand up | 0x180b82760 |
| 0x260 | slideVelocityDecay | float | 1 | `0.85` |  | deceleration while sliding (percentage of velocity retained per second; has a stronger effect at faster speeds) | 0x180b82790 |
| 0x264 | slideAccel | float | 1 | `50` |  | allowed acceleration (from player input) while sliding | 0x180b827c0 |
| 0x268 | slideRequiredStartSpeed | float | 1 | `200` |  | required speed to begin a slide | 0x180b827f0 |
| 0x26c | slideStopSpeed | float | 1 | `125` |  | slide ends when below this speed | 0x180b82820 |
| 0x270 | slideMaxStopSpeed | float | 1 | `350` |  | slide may not end while above this speed | 0x180b82850 |
| 0x274 | slideMaxJumpSpeed | float | 1 | `350` |  | velocity is reduced to this when jumping from slide | 0x180b82880 |
| 0x278 | slideSpeedBoost | float | 1 | `150` |  | extra speed given when a slide starts | 0x180b828b0 |
| 0x27c | slideSpeedBoostCap | float | 1 | `400` |  | extra slide speed doesn't bring player speed above this | 0x180b828e0 |
| 0x280 | slideJumpHeight | float | 1 | `50` |  | Jump height when sliding | 0x180b82910 |
| 0x284 | grapple_detachLengthMin | float | 1 | `33` |  | Minimum length of grapple line (always detaches when shorter than this) | 0x180b82940 |
| 0x288 | grapple_detachLengthMax | float | 1 | `50` |  | Minimum length of grapple line (detaches when shorter than this and at full speed) | 0x180b82970 |
| 0x28c | grapple_detachAwaySpeed | float | 1 | `300` |  | Grapple detaches when moving away from it at this speed | 0x180b829a0 |
| 0x290 | grapple_gravityFracMin | float | 1 | `0.0` |  | Fraction of gravity used when grappling; fights with grapple_accel | 0x180b829d0 |
| 0x294 | grapple_gravityFracMax | float | 1 | `0.75` |  | Fraction of gravity used when grappling and pulling away from the grapple or pushing under it; fights with grapple_accel | 0x180b82a00 |
| 0x298 | grapple_detachVerticalBoost | float | 1 | `200` |  | Vertical boost given when detaching after passing the grapple point | 0x180b82a30 |
| 0x29c | grapple_detachVerticalMaxSpeed | float | 1 | `200` |  | Limits vertical boost from grapple_detachVerticalBoost | 0x180b82a60 |
| 0x2a0 | grapple_detachSpeedLoss | float | 1 | `400` |  | Reduces speed when grapple detaches | 0x180b82a90 |
| 0x2a4 | grapple_detachSpeedLossMin | float | 1 | `430` |  | grapple_detachSpeedLoss doesn't reduce speed lower than this | 0x180b82ac0 |
| 0x2a8 | grapple_detachLowSpeedThreshold | float | 1 | `250` |  | Grapple detaches when below this speed for grapple_detachLowSpeed(Ground)Time | 0x180b82af0 |
| 0x2ac | grapple_detachLowSpeedMeleeThreshold | float | 1 | `100` |  | Grapple detaches when below this speed for grapple_detachLowSpeed(Melee)Time | 0x180b82b20 |
| 0x2b0 | grapple_detachLowSpeedTime | float | 1 | `1.5` |  | Grapple detaches when below grapple_detachSpeedThreshold for this length of time in the air | 0x180b82b50 |
| 0x2b4 | grapple_detachLowSpeedWallTime | float | 1 | `1.2` |  | Grapple detaches when below grapple_detachSpeedThreshold for this length of time when wall running | 0x180b82b80 |
| 0x2b8 | grapple_detachLowSpeedGroundTime | float | 1 | `0.7` |  | Grapple detaches when below grapple_detachSpeedThreshold for this length of time on the ground | 0x180b82bb0 |
| 0x2bc | grapple_detachLowSpeedMeleeTime | float | 1 | `1.0` |  | Grapple detaches when below grapple_detachSpeedMeleeThreshold for this length of time on the ground | 0x180b82be0 |
| 0x2c0 | grapple_impactVerticalBoost | float | 1 | `300` |  | Vertical boost given when hitting the grapple point | 0x180b82c10 |
| 0x2c4 | grapple_impactVerticalMaxSpeed | float | 1 | `300` |  | Limits vertical boost from grapple_impactVerticalBoost | 0x180b82c40 |
| 0x2c8 | grapple_airSpeedMax | float | 1 | `420` |  | Max speed from player movement controls while grappling | 0x180b82c70 |
| 0x2cc | grapple_airAccel | float | 1 | `1000` |  | Acceleration from player movement controls while grappling (fights with grapple_decel) | 0x180b82ca0 |
| 0x2d0 | sharedEnergyRegenDelay | float | 1 | `0.0` |  | How long before this entity starts regening its shared energy after firing | 0x180b82cd0 |
| 0x2d4 | sharedEnergyNotUsableSound | str[64] | 2 | "" |  |  | 0x180b82d00 |
| 0x314 | sharedEnergyRegenSound | str[64] | 2 | "" |  |  | 0x180b82d30 |
| 0x354 | deathcamRotateSpeed | float | 1 | `4` |  | How fast deathcam lookat camera movement interpolates to the killer | 0x180b82d60 |
| 0x358 | deathcamDistanceMin | float | 1 | `120` |  | Minimum distance the camera can be away from viewing the players killer | 0x180b82d90 |
| 0x35c | deathcamDistanceMax | float | 1 | `200` |  | Maximum distance the camera can be away from viewing the players killer | 0x180b82dc0 |
| 0x360 | deathcamDistanceGrowRate | float | 1 | `30` |  | Rate at which the camera distance from the players killer changes per second | 0x180b82df0 |
| 0x364 | deathcamExtraHeight | float | 1 | `55` |  | Additional height above the players ragdoll which the camera will center its view | 0x180b82e20 |
| 0x368 | deathcamMinHeight | float | 1 | `15` |  | Minimum height the camera should be above the players ragdoll | 0x180b82e50 |
| 0x36c | deathcamLookdownTime | float | 1 | `2` |  | Time taken to look at ragdoll if the player was not killed by a different valid game entity(other player, npc, etc) | 0x180b82e80 |
| 0x370 | deathcamLookdownPitch | float | 1 | `52` |  | Eventual pitch down the camera will interpolate to if the player was not killed by a different valid game entity(other player, npc, etc) | 0x180b82eb0 |
| 0x374 | chasecamDistanceMax | float | 1 | `75` |  | Distance a chase camera observer will be at | 0x180b82ee0 |
| 0x378 | chasecamMaxOrbitDepth | float | 1 | `20` |  | Maximum depth below the chase camera orbit origin that the camera can go | 0x180b82f10 |
| 0x37c | chasecamMaxPitchUp | float | 1 | `-45` |  | Maximum pitch angle upward that chase camera will use. Works similar to 'cl_pitchup' but with negative values. Should be in interval (-90, 90) | 0x180b82f40 |
| 0x380 | chasecamOffsetUp | float | 1 | `0` |  | Extra distance the camera is moved upward from abs origin | 0x180b82f70 |
| 0x384 | chasecamOffsetRight | float | 1 | `18` |  | Extra distance the camera is moved to the right | 0x180b82fa0 |
| 0x388 | chasecamOffsetForward | float | 1 | `0` |  |  | 0x180b82fd0 |
| 0x38c | leech_range | float | 1 | `0` |  | Range of leech action. 0 to disable. | 0x180b83000 |
| 0x390 | context_action_can_melee | bool | 6, req | *(none)* |  | Can this guy melee? | 0x180b83030 |
| 0x391 | context_action_can_use | bool | 6, req | *(none)* |  | Can this guy use? | 0x180b83060 |
| 0x394 | melee_cone_trace_range | float | 1 | *(none)* |  | 400 | 0x180b83090 |
| 0x398 | melee_cone_trace_angle | float | 1 | *(none)* |  | 40 | 0x180b830c0 |
| 0x39c | mountZiplineTime | float | 1 | `0.5` |  | Time taken for player to mount up to a zipline and begin sliding | 0x180b830f0 |
| 0x3a0 | useZiplineCooldown | float | 1 | `1.0` |  | Time before player can use the zipline again after detaching | 0x180b83120 |
| 0x3a4 | ziplineSpeed | float | 1 | `800` |  | Maximum speed when moving along zipline | 0x180b83150 |
| 0x3a8 | ziplineAcceleration | float | 1 | `400` |  | Acceleration to zipline speed when moving along zipline | 0x180b83180 |
| 0x3ac | ziplineJumpOffSpeed | float | 1 | `400` |  | Speed player will move at when they jump off a zipline | 0x180b831b0 |
| 0x3b0 | ziplineJumpOffDampScale | float | 1 | `1` |  | When the player detaches from a zipline, the current velocity is immediately scaled by this amount (i.e., 0 for instant stop, 1 for no change in speed) | 0x180b831e0 |
| 0x3b4 | magneticRange | float | 1 | `0` |  | Range for magnetic grenade attraction. 0 for off | 0x180b83210 |
| 0x3b8 | rodeoCooldownTime | float | 1 | `1.5` |  | Time before player can initiate rodeo again | 0x180b83240 |
| 0x3bc | rodeoViewdriftMinTitanSpeed | float | 1 | `80` |  | Apply minimum drift when Titan is moving at this speed or below | 0x180b83270 |
| 0x3c0 | rodeoViewdriftMaxTitanSpeed | float | 1 | `300` |  | Apply maximum drift when Titan is moving at this speed or above | 0x180b832a0 |
| 0x3c4 | rodeoViewdriftPitchScaleMin | float | 1 | `0.5` |  | Minimum amount of vertical drift while in rodeo | 0x180b832d0 |
| 0x3c8 | rodeoViewdriftPitchScaleMax | float | 1 | `2.0` |  | Maximum amount of vertical drift while in rodeo | 0x180b83300 |
| 0x3cc | rodeoViewdriftPitchSpeedMin | float | 1 | `0.5` |  | Minimum speed of vertical drift while in rodeo | 0x180b83330 |
| 0x3d0 | rodeoViewdriftPitchSpeedMax | float | 1 | `3.0` |  | Maximum speed of vertical drift while in rodeo | 0x180b83360 |
| 0x3d4 | rodeoViewdriftYawScaleMin | float | 1 | `0.5` |  | Minimum amount of horizontal drift while in rodeo | 0x180b83390 |
| 0x3d8 | rodeoViewdriftYawScaleMax | float | 1 | `1.0` |  | Maximum amount of horizontal drift while in rodeo | 0x180b833c0 |
| 0x3dc | rodeoViewdriftYawSpeedMin | float | 1 | `0.5` |  | Minimum speed of horizontal drift while in rodeo | 0x180b833f0 |
| 0x3e0 | rodeoViewdriftYawSpeedMax | float | 1 | `1.0` |  | Maximum speed of horizontal drift while in rodeo | 0x180b83420 |
| 0x3e4 | aimAngleBackwardEnd | float | 1 | `-90` |  | Angle when backward animation stops being used | 0x180b83450 |
| 0x3e8 | aimAngleForwardStart | float | 1 | `-90` |  | Angle when forward animation starts being used | 0x180b83480 |
| 0x3ec | aimAngleForwardEnd | float | 1 | `90` |  | Angle when forward animation stops being used | 0x180b834b0 |
| 0x3f0 | aimAngleBackwardStart | float | 1 | `90` |  | Angle when backward anim starts being used again | 0x180b834e0 |
| 0x3f4 | cockpitSwayGain | float | 1 | `3.0` |  | Scale applied to all cockpit sway values | 0x180b83510 |
| 0x3f8 | cockpitSwayMinOriginX | float | 1 | `-1` |  | Min cockpit sway translation in X direction | 0x180b83540 |
| 0x3fc | cockpitSwayMinOriginY | float | 1 | `-1` |  | Min cockpit sway translation in Y direction | 0x180b83570 |
| 0x400 | cockpitSwayMinOriginZ | float | 1 | `-1` |  | Min cockpit sway translation in Z direction | 0x180b835a0 |
| 0x404 | cockpitSwayMaxOriginX | float | 1 | `1` |  | Max cockpit sway translation in X direction | 0x180b835d0 |
| 0x408 | cockpitSwayMaxOriginY | float | 1 | `1` |  | Max cockpit sway translation in Y direction | 0x180b83600 |
| 0x40c | cockpitSwayMaxOriginZ | float | 1 | `1` |  | Max cockpit sway translation in Z direction | 0x180b83630 |
| 0x410 | cockpitSwayMinAnglePitch | float | 1 | `-0.5` |  | Min cockpit sway rotation in pitch direction | 0x180b83660 |
| 0x414 | cockpitSwayMinAngleYaw | float | 1 | `-0.5` |  | Min cockpit sway rotation in yaw direction | 0x180b83690 |
| 0x418 | cockpitSwayMinAngleRoll | float | 1 | `-0.5` |  | Min cockpit sway rotation in roll direction | 0x180b836c0 |
| 0x41c | cockpitSwayMaxAnglePitch | float | 1 | `0.5` |  | Max cockpit sway rotation in pitch direction | 0x180b836f0 |
| 0x420 | cockpitSwayMaxAngleYaw | float | 1 | `0.5` |  | Max cockpit sway rotation in yaw direction | 0x180b83720 |
| 0x424 | cockpitSwayMaxAngleRoll | float | 1 | `0.5` |  | Max cockpit sway rotation in roll direction | 0x180b83750 |
| 0x428 | cockpitSwayTurnAngleFactor | float | 1 | `-4.0` |  | Amount of cockpit angle when turning | 0x180b83780 |
| 0x42c | cockpitSwayTurnAngleRollFactor | float | 1 | `0.0` |  | Amount of cockpit roll when turning | 0x180b837b0 |
| 0x430 | cockpitSwayTurnOriginFactor | float | 1 | `-1` |  | Amount of cockpit translation when turning | 0x180b837e0 |
| 0x434 | cockpitSwayMoveAngleFactor | float | 1 | `-0.3` |  | Amount of cockpit angle when moving | 0x180b83810 |
| 0x438 | cockpitSwayMoveAngleRollFactor | float | 1 | `-0.2` |  | Amount of cockpit roll when moving | 0x180b83840 |
| 0x43c | cockpitSwayMoveOriginFactor | float | 1 | `-0.15` |  | Amount of cockpit translation when moving | 0x180b83870 |
| 0x440 | cockpit_spring_originDamping | float | 1 | `9.0` |  | Rate of spring's energy loss. | 0x180b838a0 |
| 0x444 | cockpit_spring_originConstant | float | 1 | `65.0` |  | Strength of spring's return to center. | 0x180b838d0 |
| 0x448 | cockpit_spring_pitchDamping | float | 1 | `9.0` |  | Rate of spring's energy loss. | 0x180b83960 |
| 0x44c | cockpit_spring_pitchConstant | float | 1 | `65.0` |  | Strength of spring's return to center. | 0x180b83990 |
| 0x450 | cockpit_spring_yawDamping | float | 1 | `9.0` |  | Rate of spring's energy loss. | 0x180b83900 |
| 0x454 | cockpit_spring_yawConstant | float | 1 | `65.0` |  | Strength of spring's return to center. | 0x180b83930 |
| 0x458 | cockpit_spring_rollDamping | float | 1 | `9.0` |  | Rate of spring's energy loss. | 0x180b839c0 |
| 0x45c | cockpit_spring_rollConstant | float | 1 | `65.0` |  | Strength of spring's return to center. | 0x180b839f0 |
| 0x460 | cockpit_stepJolt_origin | vec3 | 3 | *(none)* |  | Cockpit jolt per footstep. | 0x180b83a20 |
| 0x46c | cockpit_stepJolt_angles | vec3 | 3 | *(none)* |  | Cockpit jolt per footstep. | 0x180b83a50 |
| 0x478 | damageImpulseScale | float | 1 | `1` |  | Scales velocity gained from bullet impacts | 0x180b83a80 |
| 0x47c | damageImpulseScaleAir | float | 1 | `1` |  | Scales velocity gained from bullet impacts when in the air | 0x180b83ab0 |
| 0x480 | damageImpulseSpeedBoostLimit | float | 1 | `400` |  | Maximum velocity bullet impacts can give you | 0x180b83ae0 |
| 0x484 | damageImpulseSpeedLossLimit | float | 1 | `50` |  | Minimum velocity bullet impacts will reduce you to in the direction you are pushing | 0x180b83b10 |
| 0x488 | doFaceAnim | bool | 6 | `0` |  | Should do face animation for model | 0x180b85ac0 |
| 0x489 | quadMoveAnims | bool | 6 | `0` |  | Splits run and walk animations into _LEFT/_RIGHT/_FORWARD/_BACK activities | 0x180b83b40 |
| 0x48a | sprintAnimOrientToMoveDir | bool | 6 | `1` |  | Orients player body to face the move direction while sprinting, instead of the aim direction | 0x180b83b70 |
| 0x48b | forceAimYawToEye | bool | 6 | `0` |  | Always snap the player's feet direction with their eye direction | 0x180b83ba0 |
| 0x48c | bodyModel | str[64] | 2, req | *(none)* |  | Model used when part of the red team | 0x180b85af0 |
| 0x4cc | armsModel | str[64] | 2, req | *(none)* |  | Viewmodel arms used when part of the red team | 0x180b85b20 |
| 0x50c | cockpitModel | str[64] | 2, req | *(none)* |  | Cockpit viewmodel | 0x180b85b50 |
| 0x54c | class | str[32] | 2, req | *(none)* |  | General class of the player, such as "titan" | 0x180b85bb0 |
| 0x56c | subclass | str[32] | 2 | *(none)* |  | An optional subclass field for this class | 0x180b84380 |
| 0x58c | weaponClass | str[32] | 2, req | *(none)* |  | Class of weapons that may be used | 0x180b85b80 |
| 0x5ac | headshotFX | str[64] | 2 | "" |  |  | 0x180b843b0 |
| 0x5ec | gibModel0 | str[64] | 2 | "" |  |  | 0x180b843e0 |
| 0x62c | gibModel1 | str[64] | 2 | "" |  |  | 0x180b84410 |
| 0x66c | gibModel2 | str[64] | 2 | "" |  |  | 0x180b84440 |
| 0x6ac | gibModel3 | str[64] | 2 | "" |  |  | 0x180b84470 |
| 0x6ec | gibModel4 | str[64] | 2 | "" |  |  | 0x180b844a0 |
| 0x72c | gibModelSoftened0 | str[64] | 2 | "" |  |  | 0x180b844d0 |
| 0x76c | gibModelSoftened1 | str[64] | 2 | "" |  |  | 0x180b84500 |
| 0x7ac | gibModelSoftened2 | str[64] | 2 | "" |  |  | 0x180b84530 |
| 0x7ec | gibModelSoftened3 | str[64] | 2 | "" |  |  | 0x180b84560 |
| 0x82c | gibModelSoftened4 | str[64] | 2 | "" |  |  | 0x180b84590 |
| 0x86c | gibAttachment0 | str[64] | 2 | "" |  |  | 0x180b845c0 |
| 0x8ac | gibAttachment1 | str[64] | 2 | "" |  |  | 0x180b845f0 |
| 0x8ec | gibAttachment2 | str[64] | 2 | "" |  |  | 0x180b84620 |
| 0x92c | gibAttachment3 | str[64] | 2 | "" |  |  | 0x180b84650 |
| 0x96c | gibAttachment4 | str[64] | 2 | "" |  |  | 0x180b84680 |
| 0x9ac | gibAttachmentIdx[0] | float | 5 | `-1` |  |  | 0x180b846b0 |
| 0x9b0 | gibAttachmentIdx[1] | float | 5 | `-1` |  |  | 0x180b846e0 |
| 0x9b4 | gibAttachmentIdx[2] | float | 5 | `-1` |  |  | 0x180b84710 |
| 0x9b8 | gibAttachmentIdx[3] | float | 5 | `-1` |  |  | 0x180b84740 |
| 0x9bc | gibAttachmentIdx[4] | float | 5 | `-1` |  |  | 0x180b84770 |
| 0x9c0 | gibFX | str[64] | 2 | "" |  |  | 0x180b847a0 |
| 0xa00 | gibSound | str[64] | 2 | "" |  |  | 0x180b847d0 |
| 0xa40 | gibSpeed | float | 1 | `400` |  |  | 0x180b84800 |
| 0xa44 | gibAngularSpeed | float | 1 | `30` |  |  | 0x180b84830 |
| 0xa48 | gibMaxDist | float | 1 | `1024` |  |  | 0x180b84860 |
| 0xa4c | sound_superJump | str[64] | 2 | "" |  | Sound when player double-jumps. | 0x180b84890 |
| 0xa8c | sound_superJumpFail | str[64] | 2 | "" |  | Sound when player fails a double-jump. | 0x180b848c0 |
| 0xacc | sound_dodge_1p | str[64] | 2 | "" |  | Sound when player dodges. | 0x180b848f0 |
| 0xb0c | sound_dodge_3p | str[64] | 2 | "" |  | Sound when player dodges. | 0x180b84920 |
| 0xb4c | sound_dodgeFail | str[64] | 2 | "" |  | Sound when player fails a dodge. | 0x180b84950 |
| 0xb8c | sound_groundImpact | str[64] | 2 | "" |  | Sound when player lands. | 0x180b84980 |
| 0xbcc | sound_wallrunImpact | str[64] | 2 | "" |  | Sound when player starts a wallrun. | 0x180b849b0 |
| 0xc0c | sound_wallHangStart | str[64] | 2 | "" |  | Sound when starting to wall hang. | 0x180b849e0 |
| 0xc4c | sound_wallHangComplete | str[64] | 2 | "" |  | Sound when finished getting into wall hang state. | 0x180b84a10 |
| 0xc8c | sound_wallrunSlip | str[64] | 2 | "" |  | Sound when starting to slip out of a wallrun. | 0x180b84a40 |
| 0xccc | sound_wallHangSlip | str[64] | 2 | "" |  | Sound when starting to slip out of a wall hang. | 0x180b84a70 |
| 0xd0c | sound_wallrunFall | str[64] | 2 | "" |  | Sound when falling out of a wallrun. | 0x180b84aa0 |
| 0xd4c | sound_wallHangFall | str[64] | 2 | "" |  | Sound when falling out of a wall hang. | 0x180b84ad0 |
| 0xd8c | sound_standToCrouch | str[64] | 2 | "" |  | Sound when crouching. | 0x180b84b00 |
| 0xdcc | sound_crouchToStand | str[64] | 2 | "" |  | Sound when standing. | 0x180b84b30 |
| 0xe0c | sound_freefall_start_1p | str[64] | 2 | "" |  | Sound when player starts freefalling in 1p. | 0x180b84b60 |
| 0xe4c | sound_freefall_start_3p | str[64] | 2 | "" |  | Sound when player starts freefalling in 3p. | 0x180b84b90 |
| 0xe8c | sound_freefall_body_1p | str[64] | 2 | "" |  | Sound played while player is freefalling in 1p. | 0x180b84bc0 |
| 0xecc | sound_freefall_body_3p | str[64] | 2 | "" |  | Sound played while player is freefalling in 3p. | 0x180b84bf0 |
| 0xf0c | sound_freefall_finish_1p | str[64] | 2 | "" |  | Sound when player finishes freefalling in 1p. | 0x180b84c20 |
| 0xf4c | sound_freefall_finish_3p | str[64] | 2 | "" |  | Sound when player finishes freefalling in 3p. | 0x180b84c50 |
| 0xf8c | sound_jumpjet_jump_start_1p | str[64] | 2 | "" |  | Sound when player starts jumping in 1p. | 0x180b84c80 |
| 0xfcc | sound_jumpjet_jump_start_3p | str[64] | 2 | "" |  | Sound when player starts jumping in 3p. | 0x180b84cb0 |
| 0x100c | sound_jumpjet_jump_body_1p | str[64] | 2 | "" |  | Sound played while player is jumping in 1p. | 0x180b84ce0 |
| 0x104c | sound_jumpjet_jump_body_3p | str[64] | 2 | "" |  | Sound played while player is jumping in 3p. | 0x180b84d10 |
| 0x108c | sound_jumpjet_jump_finish_1p | str[64] | 2 | "" |  | Sound when player finishes jumping in 1p. | 0x180b84d40 |
| 0x10cc | sound_jumpjet_jump_finish_3p | str[64] | 2 | "" |  | Sound when player finishes jumping in 3p. | 0x180b84d70 |
| 0x110c | sound_jumpjet_jet_start_1p | str[64] | 2 | "" |  | Sound when player starts double-jumping in 1p. | 0x180b84da0 |
| 0x114c | sound_jumpjet_jet_start_3p | str[64] | 2 | "" |  | Sound when player starts double-jumping in 3p. | 0x180b84dd0 |
| 0x118c | sound_jumpjet_jet_body_1p | str[64] | 2 | "" |  | Sound played while player is double-jumping in 1p. | 0x180b84e00 |
| 0x11cc | sound_jumpjet_jet_body_3p | str[64] | 2 | "" |  | Sound played while player is double-jumping in 3p. | 0x180b84e30 |
| 0x120c | sound_jumpjet_jet_finish_1p | str[64] | 2 | "" |  | Sound when player finishes double-jumping in 1p. | 0x180b84e60 |
| 0x124c | sound_jumpjet_jet_finish_3p | str[64] | 2 | "" |  | Sound when player finishes double-jumping in 3p. | 0x180b84e90 |
| 0x128c | sound_jumpjet_wallrun_start_1p | str[64] | 2 | "" |  | Sound when player starts wallrunning in 1p. | 0x180b84ec0 |
| 0x12cc | sound_jumpjet_wallrun_start_3p | str[64] | 2 | "" |  | Sound when player starts wallrunning in 3p. | 0x180b84ef0 |
| 0x130c | sound_jumpjet_wallrun_body_1p | str[64] | 2 | "" |  | Sound played while player is wallrunning in 1p. | 0x180b84f20 |
| 0x134c | sound_jumpjet_wallrun_body_3p | str[64] | 2 | "" |  | Sound played while player is wallrunning in 3p. | 0x180b84f50 |
| 0x138c | sound_jumpjet_wallrun_finish_1p | str[64] | 2 | "" |  | Sound when player finishes wallrunning in 1p. | 0x180b84f80 |
| 0x13cc | sound_jumpjet_wallrun_finish_3p | str[64] | 2 | "" |  | Sound when player finishes wallrunning in 3p. | 0x180b84fb0 |
| 0x140c | sound_boost_short_1p | str[64] | 2 | "" |  | Sound when player performs a short repeated boost in 1p | 0x180b84fe0 |
| 0x144c | sound_boost_short_3p | str[64] | 2 | "" |  | Sound when player performs a short repeated boost in 3p | 0x180b85010 |
| 0x148c | sound_boost_start_1p | str[64] | 2 | "" |  | Sound when player starts boost in 1p. | 0x180b85040 |
| 0x14cc | sound_boost_start_3p | str[64] | 2 | "" |  | Sound when player starts boost in 3p. | 0x180b85070 |
| 0x150c | sound_boost_body_1p | str[64] | 2 | "" |  | Sound played while player is boosting in 1p. Sound is faded out when boost finishes. | 0x180b850a0 |
| 0x154c | sound_boost_body_3p | str[64] | 2 | "" |  | Sound played while player is boosting in 3p. Sound is faded out when boost finishes. | 0x180b850d0 |
| 0x158c | sound_boost_finish_1p | str[64] | 2 | "" |  | Sound when player finishes boost in 1p. | 0x180b85100 |
| 0x15cc | sound_boost_finish_3p | str[64] | 2 | "" |  | Sound when player finishes boost in 3p. | 0x180b85130 |
| 0x160c | sound_glide_start_1p | str[64] | 2 | "" |  | Sound when player starts glide in 1p. | 0x180b85160 |
| 0x164c | sound_glide_start_3p | str[64] | 2 | "" |  | Sound when player starts glide in 3p. | 0x180b85190 |
| 0x168c | sound_glide_body_1p | str[64] | 2 | "" |  | Sound played while player is gliding in 1p. Sound is faded out when glide finishes. | 0x180b851c0 |
| 0x16cc | sound_glide_body_3p | str[64] | 2 | "" |  | Sound played while player is gliding in 3p. Sound is faded out when glide finishes. | 0x180b851f0 |
| 0x170c | sound_glide_finish_1p | str[64] | 2 | "" |  | Sound when player finishes glide in 1p. | 0x180b85220 |
| 0x174c | sound_glide_finish_3p | str[64] | 2 | "" |  | Sound when player finishes glide in 3p. | 0x180b85250 |
| 0x178c | sound_jetpack_start_1p | str[64] | 2 | "" |  | Sound when player starts jetpack in 1p. | 0x180b85280 |
| 0x17cc | sound_jetpack_start_3p | str[64] | 2 | "" |  | Sound when player starts jetpack in 3p. | 0x180b852b0 |
| 0x180c | sound_jetpack_body_1p | str[64] | 2 | "" |  | Sound played while player is jetting in 1p. Sound is faded out when jetpack finishes. | 0x180b852e0 |
| 0x184c | sound_jetpack_body_3p | str[64] | 2 | "" |  | Sound played while player is jetting in 3p. Sound is faded out when jetpack finishes. | 0x180b85310 |
| 0x188c | sound_jetpack_finish_1p | str[64] | 2 | "" |  | Sound when player finishes jetpack in 1p. | 0x180b85340 |
| 0x18cc | sound_jetpack_finish_3p | str[64] | 2 | "" |  | Sound when player finishes jetpack in 3p. | 0x180b85370 |
| 0x190c | sound_hover_start_1p | str[64] | 2 | "" |  | Sound when player starts hover in 1p. | 0x180b853a0 |
| 0x194c | sound_hover_start_3p | str[64] | 2 | "" |  | Sound when player starts hover in 3p. | 0x180b853d0 |
| 0x198c | sound_hover_body_1p | str[64] | 2 | "" |  | Sound played while player is hovering in 1p. Sound is faded out when hover finishes. | 0x180b85400 |
| 0x19cc | sound_hover_body_3p | str[64] | 2 | "" |  | Sound played while player is hovering in 3p. Sound is faded out when hover finishes. | 0x180b85430 |
| 0x1a0c | sound_hover_finish_1p | str[64] | 2 | "" |  | Sound when player finishes hover in 1p. | 0x180b85460 |
| 0x1a4c | sound_hover_finish_3p | str[64] | 2 | "" |  | Sound when player finishes hover in 3p. | 0x180b85490 |
| 0x1a8c | sound_climb_start_1p | str[64] | 2 | "" |  | Sound when player starts climb in 1p. | 0x180b854c0 |
| 0x1acc | sound_climb_start_3p | str[64] | 2 | "" |  | Sound when player starts climb in 3p. | 0x180b854f0 |
| 0x1b0c | sound_climb_body_1p | str[64] | 2 | "" |  | Sound played while player is climbing in 1p. Sound is faded out when climb finishes. | 0x180b85520 |
| 0x1b4c | sound_climb_body_3p | str[64] | 2 | "" |  | Sound played while player is climbing in 3p. Sound is faded out when climb finishes. | 0x180b85550 |
| 0x1b8c | sound_climb_finish_1p | str[64] | 2 | "" |  | Sound when player finishes climb in 1p. | 0x180b85580 |
| 0x1bcc | sound_climb_finish_3p | str[64] | 2 | "" |  | Sound when player finishes climb in 3p. | 0x180b855b0 |
| 0x1c0c | sound_jetwash_start_1p | str[64] | 2 | "" |  | Initial sound emitted at boost pilot jetwash point in 1p. | 0x180b855e0 |
| 0x1c4c | sound_jetwash_start_3p | str[64] | 2 | "" |  | Initial sound emitted at boost pilot jetwash point in 3p. | 0x180b85610 |
| 0x1c8c | sound_jetwash_body_1p | str[64] | 2 | "" |  | Looping sound emitted at boost pilot jetwash point in 1p. | 0x180b85640 |
| 0x1ccc | sound_jetwash_body_3p | str[64] | 2 | "" |  | Looping sound emitted at boost pilot jetwash point in 3p. | 0x180b85670 |
| 0x1d0c | sound_jetwash_finish_1p | str[64] | 2 | "" |  | Ending sound emitted at boost pilot jetwash point in 1p. | 0x180b856a0 |
| 0x1d4c | sound_jetwash_finish_3p | str[64] | 2 | "" |  | Ending sound emitted at boost pilot jetwash point in 3p. | 0x180b856d0 |
| 0x1d8c | sound_boost_meter_depleted_1p | str[64] | 2 | "" |  | Sound emitted when the boost pilot's meter has been fully depleated in 1p. | 0x180b85700 |
| 0x1dcc | sound_boost_meter_depleted_3p | str[64] | 2 | "" |  | Sound emitted when the boost pilot's meter has been fully depleated in 3p. | 0x180b85730 |
| 0x1e0c | sound_boost_meter_recharged_1p | str[64] | 2 | "" |  | Sound emitted when the boost pilot's meter has been fully recharged in 1p. | 0x180b85760 |
| 0x1e4c | sound_boost_meter_recharged_3p | str[64] | 2 | "" |  | Sound emitted when the boost pilot's meter has been fully recharged in 3p. | 0x180b85790 |
| 0x1e8c | sound_boost_meter_fail_1p | str[64] | 2 | "" |  | Soumd emitted if the player attempts to boost while their meter is empty in 1p. | 0x180b857c0 |
| 0x1ecc | sound_boost_meter_fail_3p | str[64] | 2 | "" |  | Soumd emitted if the player attempts to boost while their meter is empty in 3p. | 0x180b857f0 |
| 0x1f0c | sound_pain_layer1_loop | str[64] | 2 | "" |  |  | 0x180b85820 |
| 0x1f4c | sound_pain_layer1_end | str[64] | 2 | "" |  |  | 0x180b85850 |
| 0x1f8c | sound_pain_layer2_start | str[64] | 2 | "" |  |  | 0x180b85880 |
| 0x1fcc | sound_pain_layer2_loop | str[64] | 2 | "" |  |  | 0x180b858b0 |
| 0x200c | sound_pain_layer3_loop | str[64] | 2 | "" |  |  | 0x180b858e0 |
| 0x204c | sound_pain_layer3_end | str[64] | 2 | "" |  |  | 0x180b85910 |
| 0x208c | sound_slide_prefix | str[16] | 2 | "" |  | Prefix for slide sounds | 0x180b85940 |
| 0x209c | fx_jetwash_enabled | bool | 6 | `1` |  | Indicates if jetwash effects are enabled. | 0x180b85970 |
| 0x20a0 | fx_jetwash_frequency | float | 1 | `0.1` |  | Frequency at which jetwash effects are emitted. | 0x180b859a0 |
| 0x20a4 | fx_jetwash_height | float | 1 | `1000` |  | The maximum height over a surface at which jetwash effects are still emitted. | 0x180b859d0 |
| 0x20a8 | fx_jetwash_impactTable | str[64] | 2 | "" |  | ImpactTable for jetwash effects applied when boosting over a surface. | 0x180b85a00 |
| 0x20e8 | fx_hover_friendly | str[64] | 2 | "" |  | FX played for friendlies when hovering | 0x180b85a30 |
| 0x2128 | fx_hover_enemy | str[64] | 2 | "" |  | FX played for enemies when hovering | 0x180b85a60 |
| 0x2168 | aiEnemy_priority | float | 5 | `10` |  | AI will consider enemy priority when deciding who to shoot. Higher values = more likely to shoot at this class than other classes | 0x180b83bd0 |
| 0x216c | viewPunchSpring | float | 5 | *(none)* |  | View punch spring used for decay | 0x180b85be0 |
| 0x2170 | footstep_type | str[64] | 2 | `human` |  | Class of footstep sounds to use. | 0x180b83c00 |
| 0x21b0 | titan_footstep_damage | float | 1 | `5000` |  | Damage applied each second by Titan footstep damage | 0x180b83c30 |
| 0x21b4 | titan_footstep_damage_min_speed | float | 1 | `10.0` |  | Minimum speed Titan must be moving at to apply footstep damage (<= 0 to always do it) | 0x180b83c60 |
| 0x21b8 | titan_footstep_damage_height_check_ratio | float | 1 | `0.36` |  | Ratio of Titan's height that victim's box must intersect with for damage to happen | 0x180b83c90 |
| 0x21bc | titan_footstep_damage_interval | float | 1 | `1.0` |  | Amount of time to wait before applying Titan footstep damage again | 0x180b83cc0 |
| 0x21c0 | titan_footstep_damage_dist_interval | float | 1 | `150.0` |  | Amount of distance to move before applying Titan footstep damage again | 0x180b83cf0 |
| 0x21c4 | rumbleOnJump | str[64] | 2 | "" |  |  | 0x180b83d20 |
| 0x2204 | rumbleOnDoubleJump | str[64] | 2 | "" |  |  | 0x180b83d50 |
| 0x2244 | rumbleOnWallrunStart | str[64] | 2 | "" |  |  | 0x180b83d80 |
| 0x2284 | rumbleOnWallrunTimeout | str[64] | 2 | "" |  |  | 0x180b83db0 |
| 0x22c4 | rumbleOnWallHangStart | str[64] | 2 | "" |  |  | 0x180b83de0 |
| 0x2304 | rumbleOnLandingSoft | str[64] | 2 | "" |  |  | 0x180b83e10 |
| 0x2344 | rumbleOnLandingHard | str[64] | 2 | "" |  |  | 0x180b83e40 |
| 0x2384 | rumbleOnSlideBegin | str[64] | 2 | "" |  |  | 0x180b83e70 |
| 0x23c4 | rumbleOnZiplineAttach | str[64] | 2 | "" |  |  | 0x180b83ea0 |
| 0x2404 | rumbleOnZiplineDetach | str[64] | 2 | "" |  |  | 0x180b83ed0 |
| 0x2444 | rumbleOnDodge | str[64] | 2 | "" |  |  | 0x180b83f00 |
| 0x2484 | landingImpactTable | str[64] | 2 | "" |  |  | 0x180b83f30 |
| 0x24c4 | footstepImpactTable | str[64] | 2 | "" |  |  | 0x180b83f60 |
| 0x2504 | dodgeImpactTable | str[64] | 2 | "" |  |  | 0x180b83f90 |
| 0x2544 | slideEffectTable | str[64] | 2 | "" |  |  | 0x180b83fc0 |
| 0x2594 | ui_targetinfo | str[64] | 2 | "" |  |  | 0x180b83ff0 |
| 0x25d4 | camera_lcd | str[64] | 2 | "" |  |  | 0x180b84020 |
| 0x2614 | camera_lcdStartup | str[64] | 2 | "" |  |  | 0x180b84050 |
| 0x2654 | camera_lcdStartupTime | float | 1 | `0.0` |  |  | 0x180b84080 |
| 0x2658 | pitchOffsetScale | float | 1 | `0` |  | Max distance to translate the player's view forward as he looks down. | 0x180b840b0 |
| 0x265c | gravityScale | float | 1 | `1.0` |  | How affected the player is by gravity. | 0x180b840e0 |
| 0x2660 | sprintViewOffset | float | 1 | `-10.0` |  | How far to slide the player's down as they enter a sprinting state. | 0x180b84110 |
| 0x2664 | sprintStartDelay | float | 1 | `0.2` |  | How long to wait before transitioning into sprintViewOffset. | 0x180b84140 |
| 0x2668 | sprintStartDuration | float | 1 | `0.3` |  | How long to take to transition into sprintViewOffset. | 0x180b84170 |
| 0x266c | sprintStartFastDuration | float | 1 | `0.2` |  | How long to take to transition into sprintViewOffset after landing from a sprinting jump. | 0x180b841a0 |
| 0x2670 | sprintEndDuration | float | 1 | `0.15` |  | How long to take to transition out of sprintViewOffset. | 0x180b841d0 |
| 0x2674 | aimassist_bounds_override | float | 1 | `-1.0` |  |  | 0x180b84200 |
| 0x2678 | aimassist_use_short_inner_bounds | bool | 6 | `0` |  |  | 0x180b84230 |
| 0x2679 | aimassist_adspull_centerAttachmentName | str[64] | 2 | "" |  |  | 0x180b84260 |
| 0x26bc | aimassist_adspull_centerRadius | float | 1 | `0.0` |  |  | 0x180b84290 |
| 0x26c0 | aimassist_adspull_headshotAttachmentName | str[64] | 2 | "" |  |  | 0x180b842c0 |
| 0x2700 | aimassist_adspull_headshotRadius | float | 1 | `0.0` |  |  | 0x180b842f0 |
| 0x2704 | aimassist_adspull_noPitchUp | bool | 6 | `0` |  |  | 0x180b84320 |
| 0x2708 | passThroughThickness | float | 5 | `32` |  | The thickness of a player for pass through bullets | 0x180b84350 |

## B. Per-stance sub-struct (16 fields; offsets are relative to the stance block, not the main struct)

| Offset | Field | Type | Kind/flags | Code default | Range | Help text (verbatim from binary) | Entry VA |
|---|---|---|---|---|---|---|---|
| 0x0 | viewheight | vec3 | 3 | *(none)* |  | View height for current stance | 0x180b85c40 |
| 0xc | firstpersonproxyoffset | vec3 | 3 | *(none)* |  | Offset to apply to the first person proxy, relative to the player's eye position and angle | 0x180b85c70 |
| 0x18 | hull_min | vec3 | 3 | *(none)* |  | Minimum collision hull bounds for current stance | 0x180b85ca0 |
| 0x24 | hull_max | vec3 | 3 | *(none)* |  | Maximum collision hull bounds for current stance | 0x180b85cd0 |
| 0x30 | footstepInterval | float | 5 | *(none)* |  | Interval between footsteps | 0x180b85d00 |
| 0x34 | footstepIntervalSprint | float | 5 | `-1` |  | Interval between footsteps when sprinting | 0x180b85d30 |
| 0x38 | footstepRunSoundRadius | float | 5 | `256` |  | How far NPC's can hear footstep sound player makes when running | 0x180b85d60 |
| 0x3c | footstepWalkSoundRadius | float | 5 | `64` |  | How far NPC's can hear footstep sound player makes when walking | 0x180b85d90 |
| 0x40 | footstepMinSpeed | float | 1 | `10` |  | Minimum speed for playing footstep sound | 0x180b85dc0 |
| 0x44 | lowSpeed | float | 1 | `-1` |  | Speed below which extra acceleration is used | 0x180b85df0 |
| 0x48 | acceleration | float | 1 | `2500` |  | Affects how quickly the player can get to full speed (units/sec/sec) | 0x180b85e20 |
| 0x4c | deceleration | float | 1 | `-1` |  | Affects how quickly the player can come to a stop (units/sec/sec) | 0x180b85e50 |
| 0x50 | lowAcceleration | float | 1 | `-1` |  | Affects how quickly the player can get to low speed (units/sec/sec) | 0x180b85e80 |
| 0x54 | sprintAcceleration | float | 1 | `-1` |  | Affects how quickly the player can get to sprint speed (units/sec/sec) | 0x180b85eb0 |
| 0x58 | sprintDeceleration | float | 1 | `-1` |  | Affects how quickly the player can come to a stop (units/sec/sec) | 0x180b85ee0 |
| 0x5c | operatorHoverHeight | float | 1 | `0` |  | How much higher abover the operator floor the operator will hover at | 0x180b85f10 |

## Table C: class-file fields (fn_180553b60 and neighbours)

These fields are **not** in the 0x30-byte table above. They are parsed by explicit calls
`FUN_1805542f0(slot, "name", &defaultString, kind, ctx)`. The default arguments resolved below are the `.rdata`
strings at the `&DAT_…` addresses in `decomp\fn_180553b60.c`; I read them from server.dll bytes.
- The 4th argument is 2 for every float. It is 1 only for `sharedEnergyTotal`, so it is presumably an int/float
  kind [inf].
- Each field occupies a 16-byte "settable value" slot: byte +0 = type, +2 = is-set flag, value at +8. This is
  inferred from the bool fields, e.g. `wallrun` has type at 0xe8, set flag at 0xea, and value at 0xf0.
- The mod-able struct offset given is the slot start (`param_1 + N*8`). The "player copy" column gives offsets in the
  player entity where the wallrun code reads these values [inf, from the matching field order; see wallrun.md].
- **Same caveat applies:** these are code defaults, and the shipped pilot .set files may override them.

| Slot offset | Field | Kind | Code default | Default ptr | Player copy | Notes |
|---|---|---|---|---|---|---|
| 0x08 | health | float | *(NULL, none)* | 0 | | must come from class file |
| 0x18 | healthShield | float | `0` | 0x1809da22c | | |
| 0x28 | healthDoomed | float | `0` | 0x1809da22c | | |
| 0x38 | healthPerSegment | float | `0` | 0x1809da22c | | |
| 0x48 | powerRegenRate | float | `10` | 0x18089568c | | |
| 0x58 | dodgeDuration | float | `0` | 0x1809da22c | | |
| 0x68 | dodgeSpeed | float | `750` | 0x180888984 | | |
| 0x78 | dodgePowerDrain | float | `33` | 0x18089c9b4 | | |
| 0x88 | smartAmmoLockTimeModifier | float | `1` | 0x1809d7dbc | | |
| 0x98 | wallrunAccelerateVertical | float | `450` | 0x1808884d4 | 0x1d3c | vertical accel on wall |
| 0xa8 | wallrunAccelerateHorizontal | float | `1500` | 0x180911698 | 0x1d40 | horizontal accel on wall |
| 0xb8 | wallrunMaxSpeedHorizontal | float | `450` | 0x1808884d4 | 0x1d44 | horizontal wallrun speed cap |
| 0xc8 | wallrun_timeLimit | float | `8` | 0x18087ebac | 0x1d48 | seconds before a wallrun times out |
| 0xd8 | wallrun_hangTimeLimit | float | `9` | 0x180951504 | 0x1d4c | max wallhang time (s) |
| 0xe8 | wallrun | bool | *(none; error "must have a global field" in strict mode)* | n/a | 0x1d50 | wallrun allowed |
| 0xf8 | wallrunAdsType | enum (stored as float index) | `wallhang` in strict mode; options `none`/`wallhang`/`ads` (table 0x180b80e48) | n/a | 0x1d54 | 1 = wallhang |
| 0x108 | doubleJump | bool | *(none; required in strict mode)* | n/a | 0x1d58 [inf] | |
| 0x118 | pitchMaxUp | float | `89` | 0x18095153c | 0x1d5c [inf] | |
| 0x128 | pitchMaxDown | float | `89` | 0x18095153c | 0x1d60 [inf] | |
| 0x138 | mantlePitchLeveling | bool | `0` | inline | 0x1d64 [inf] | |
| 0x148 | dodge | bool | `0` | inline | 0x1d65 [inf] | (code at 180172870 reads 0x1d65 as the dodge enable) |
| 0x158 | sprint | bool | `1` | inline | 0x1d66 [inf] | |
| 0x168 | stealthSounds | bool | `0` | inline | 0x1d67 | selects "%s.stealth" sounds |
| 0x178 | hoverEnabled | bool | `0` | inline | | |
| 0x188 | grapple_power_regen_delay | float | `1` | 0x1809d7dbc | | |
| 0x198 | grapple_power_regen_rate | float | `17` | 0x1809515b4 | | |
| 0x1a8 | slideFOVScale | float | `1` | 0x1809d7dbc | | |
| 0x1b8 | slideFOVLerpInTime | float | `0.5` | 0x18089a544 | | |
| 0x1c8 | slideFOVLerpOutTime | float | `0.5` | 0x18089a544 | | |
| 0x1d8 | airSlowMoSpeed | float | `1.0` | 0x18087dd50 | | |
| 0x1e8 | sharedEnergyTotal | int? (kind 1) | `0` | 0x1809da22c | | |
| 0x1f8 | sharedEnergyRegenRate | float | `0.0` | 0x1808afa08 | | |

"Strict mode" is the parser's `param_3` flag. When it is set, a missing required field raises the
`Class file for '%s' must have a global field '%s'` error. When it is clear, missing fields are left unset
(`FUN_1806cd750`), presumably to inherit from a base class [inf].

### Base move speeds: `speed` / `sprintspeed`
- Only two speed keys exist: `speed` (string at 0x180880614) and `sprintspeed` (0x180951628). There is **no**
  `walkspeed`, `crouchspeed` or `crouchSpeed` string in server.dll. Crouch, and any other stance, presumably uses its
  own `speed`/`sprintspeed` inside the per-pose block [inf].
- Code defaults [bin, decoded from x64 `lea` instructions]:
  - **`speed` = `120`** (default string at 0x180889008)
  - **`sprintspeed` = `0`** (0x1809da22c). A 0 probably means "no sprint / use another value" [inf].
- They are parsed in three places, found by scanning `.text` for RIP-relative references to the two strings:

| Code location | Context | Slots (`speed` / `sprintspeed`) |
|---|---|---|
| `FUN_180554280` (0x180554280–0x1805542ee) | Small dedicated parser. No direct `call`/`jmp` or pointer reference found, so it is likely reached indirectly or is unused. | `this+0x08` / `this+0x18` (16-byte slots, values at +0x10 / +0x20 [inf]) |
| Inside `ClassMod_ParseKeyValuesForClassSettings` (refs at 0x180554a06 / 0x180554a66) | Per-mod overrides. Mod entries have stride 0x28 within the mod array, name at entry+0x68. | entry+0x70 / entry+0x80 |
| Inside `ClassPosMod_ParseKeyValuesForClassSettings` / "Player class pose" (refs at 0x18055569b / 0x1805556d9) | Per-pose (stance) values | parsed into stack temporaries, then copied into the pose block; final offset not resolved |

- The absolute offset of the base (non-mod) speed slots inside the class-settings object was **not** resolved,
  because `FUN_180554280` has no direct caller.
- Table B (per-stance) has no `speed` field, so the stance speed is not part of the 0x30-byte table.
- The shipped pilot walk and sprint speeds come from the .set files and are unknown here. The 120 / 0 defaults are
  almost certainly overridden.

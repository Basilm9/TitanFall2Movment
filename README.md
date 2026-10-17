# TitanFall2Movment

A reverse-engineered record of **Titanfall 2 pilot movement** plus a **combat value reference**: weapons, ADS, DPS/TTK, abilities and titans. It was built by static analysis of the PC game's `server.dll` and the game's own data files. The game was never run or modified.

> Not affiliated with or endorsed by Electronic Arts or Respawn Entertainment. This repository contains **no game files and no decompiled game code**. It contains our own analysis write-ups, derived numeric tables, and the tools used to produce them. To reproduce, you need your own legal copy of Titanfall 2.

## Start here

| Document | What it covers |
|---|---|
| [docs/TITANFALL2_MOVEMENT.md](docs/TITANFALL2_MOVEMENT.md) | Movement summary for reimplementation. **Section 0 = the values the game actually ships for MP pilots**; Sections 1–13 = mechanics and code defaults |
| [docs/TITANFALL2_COMBAT.md](docs/TITANFALL2_COMBAT.md) | Every pilot weapon (damage/falloff, headshots, shots and time to kill, DPS, magazine/reload, ADS time/FOV/move speed, spread), tacticals and ordnance, titan chassis and weapons |
| [docs/ASSUMPTIONS.md](docs/ASSUMPTIONS.md) | **What is verified and what is assumed**: every inferred or unknown item, why, and how to check it |
| [docs/wallrun.md](docs/wallrun.md) | Wallrun/wallhang, function by function (pseudocode, constants, offsets) |
| [docs/jump_air_friction.md](docs/jump_air_friction.md) | Jump, coyote time, double jump, skip/bunny-hop, air accel, ground friction, Accelerate |
| [docs/slide_mantle.md](docs/slide_mantle.md) | Slide (boost, decay, slide-jump) and auto-mantle |
| [docs/grapple_zipline.md](docs/grapple_zipline.md) | Grapple lifecycle and zipline ride |
| [docs/ground_misc.md](docs/ground_misc.md) | Per-tick dispatcher, walk/sprint/crouch, stairs, dodge, sprint tilt, air slow-mo, window traversal |
| [docs/player_settings_table.md](docs/player_settings_table.md) | All 421 per-class player settings fields in server.dll (offset, type, code default, help text) |

## Code examples: Unreal Engine 5 C++ reimplementation
[examples/unreal/](examples/unreal/README.md) is a drop-in UE5 plugin (`Plugins/TFMovement`). It is a clean-room rewrite, built from these docs, with the same functionality as the game. It contains no game code.

| Part | Files | What it is |
|---|---|---|
| Movement core | `TFMovementCore.h/.cpp` | Plain C++ (no Unreal headers) with every pilot system:<br>- ground, sprint, crouch, stairs<br>- jump, coyote time, skip, double jump, keyboard grace<br>- air control<br>- slide (boost, decay, slide-jump)<br>- wallrun and wallhang<br>- auto-mantle<br>- grapple<br>- zipline<br>- Stim speed boost<br>- dodge (disabled for MP pilots, as shipped)<br><br>It runs on Titanfall units. Every tunable is a field defaulting to the shipped MP value, tagged [D]/[R]/[C]/[I]/[?]. |
| Unreal glue | `TFPilotMovementComponent`, `TFPilotSettingsUE.h`, `TFZiplineActor`, `TFPilotCharacterExample` | `UPawnMovementComponent` (capsule sweeps, 1 u = 2.54 cm, Source↔Unreal axis conversion, fixed 60 Hz step), editor-exposed settings, spline zipline, example pawn with input bindings |
| Weapons | `TFWeaponTable.h`, `TFWeaponMath`, `UTFWeaponLibrary` | All 32 pilot weapons and 16 ordnance items; damage by distance, headshot range rule, EVA blast rule, pellets, splash, STK/TTK/DPS; Blueprint functions |

**What has been verified:**
- **Clean compile:** the engine-independent parts (`TFMovementCore`, `TFWeaponMath`) compile with zero warnings under clang (`-std=c++17 -Wall -Wextra -Wshadow -Wpedantic`).
- **Behavior test:** the movement core was run in a test world and reproduces the documented numbers:
  - walk 173.5 / sprint 260 / crouch 80;
  - double jump +75 u;
  - slide boost then slide-jump snap to 350;
  - 1.75 s wallrun;
  - grapple ramp, and the detach loss;
  - zipline 600 u/s;
  - Stim 468 u/s.
- **Not verified:** the Unreal-specific files have **not** been built with UnrealBuildTool, because Unreal was not available.
- **Missing features and guesses:** see the example README's "Fidelity" section.

Generators keep the code in sync with the data:
- `tools/node/weaponheader.js` writes `TFWeaponTable.h` from `data/`.
- `tools/node/gen_settings_ue.js` writes `TFPilotSettingsUE.h` from `TFMovementCore.h`.

## Confidence tags
Every value and claim in the docs carries one of these:

| Tag | Meaning |
|---|---|
| **[R]** | Read directly from `server.dll`: decompiled code, disassembly, or binary data (ConVar registrations, the settings table) |
| **[D]** | Read directly from shipped game data files (`scripts/players/mp/*.set`, `scripts/weapons/*.txt`, `.nut`/`.gnut` scripts) |
| **[C]** | Computed by us from [R]/[D] values (e.g. jump launch speed, TTK, DPS) |
| **[I]** | Inferred or assumed (names, intent, interpolation shape, anything not directly read) |
| **[?]** | Unknown or not located |

Function and field names (e.g. `CGameMovement::WallRun`, `m_wallRunWeak`) are our labels for a stripped binary unless a doc says the name came from a string in the DLL.

## Data
| File | Contents |
|---|---|
| [data/pilot_weapons.csv](data/pilot_weapons.csv) | One row per pilot weapon with all damage, rate, ADS, reload, spread and handling fields, plus computed STK/TTK/DPS and mod lists |
| [data/ordnance_abilities.csv](data/ordnance_abilities.csv) | Grenades and tacticals: damage, radii, fuse, charges, cooldown, duration |
| [data/titan_weapons.csv](data/titan_weapons.csv) | Titan weapons, abilities and cores vs titan armor and vs pilots |
| [data/titans_resolved.json](data/titans_resolved.json) | Resolved MP titan chassis and `pilot_mp` settings (global/stand/crouch) |
| [data/convars.csv](data/convars.csv) | All 909 ConVar registrations in server.dll: name, default, flags, help text |
| [docs/player_settings_table.csv](docs/player_settings_table.csv) | The player-class settings table (code defaults) |

Titanfall 2 units are inches (1 u ≈ 2.54 cm). Speeds are in u/s, and gravity is `sv_gravity` 750 u/s² × the class `gravityScale` (0.8 for MP pilots).

## Tools (to reproduce)
These steps need your own copy of the game, so nothing here touches the internet or modifies the game.

**1. Extract the game's data files.** Use `tools/node/vpkdir.js`. It is a Node reader for Respawn VPKs, and it decompresses with pg9182's [tf2lzham](https://github.com/pg9182/tf2lzham) WebAssembly build (MIT, vendored in `tools/node/vendor/` with its license). Every file is CRC32-checked.

```bash
node tools/node/vpkdir.js "<Titanfall2>/vpk/englishclient_mp_common.bsp.pak000_dir.vpk" "^scripts/(players|weapons|vscripts)/" extracted
```

**2. Rebuild the data tables.** `weapons.js` writes the pilot, ordnance and titan weapon CSVs. `setresolve.js` resolves the `.set` inheritance (`#base`, `[$mp]`). `combatmd.js` regenerates `TITANFALL2_COMBAT.md` from the CSVs.

```bash
node tools/node/weapons.js extracted/scripts/weapons data
node tools/node/setresolve.js extracted/scripts/players/mp data/titans_resolved.json titan_atlas_stickybomb titan_atlas_tracker titan_atlas_vanguard titan_ogre_meteor titan_ogre_minigun titan_stryder_leadwall titan_stryder_sniper pilot_mp
node tools/node/combatmd.js data docs/TITANFALL2_COMBAT.md
```

**3. Analyze server.dll.** `tools/ghidra/*.java` are Ghidra 12.1 headless scripts.
- **Import it first:** `analyzeHeadless <proj> tf2 -import server.dll`, a copy of the game's `server.dll`.
- **Then run a script** with `analyzeHeadless <proj> tf2 -process server.dll -noanalysis -readOnly -scriptPath tools/ghidra -postScript <Script>.java <args>`. The scripts:
  - `DumpConVars.java <out.tsv> 0x1807240e0 0x180724420`: all ConVar registrations with defaults.
  - `DumpConVarUsers.java <convars.tsv> <regexFile> <outDir>`: decompiles every function that touches matching ConVars, with ConVar names and `.rdata` float constants inlined. The `*.re` files are the regexes used.
  - `DumpCallees.java <convars.tsv> <outDir> <lo> <hi> <index.txt>...`: callees of already-dumped functions. With `self x <list>` it decompiles specific addresses.
  - `DumpMovement.java`, `Scan8`, `Disasm8`, `RefsRange8`, `RelScan8`, `StrRefs8`: string-reference dumps and instruction/pointer scans used to close the last gaps.
- **Addresses** are for the build analyzed (server.dll SHA-256 `a5ca3a25c8ae56952a26141b0f6cdcb6c19086c8c39013f7cb345d5d723661af`).

## Notes on the build analyzed
- **Inputs:** the PC (EA app) build of Titanfall 2. MP values come from `englishclient_mp_common.bsp.pak000_dir.vpk`.
- **Denuvo trigger blocks** sabotage tampered copies: wallrun disabled after 120 s on `_ti` maps, and move speed ×0.1 after 600 s. They are documented as anti-tamper, not gameplay.
- **client.dll was not analyzed**, so client-only visuals (view tilt, view punch springs) are out of scope.

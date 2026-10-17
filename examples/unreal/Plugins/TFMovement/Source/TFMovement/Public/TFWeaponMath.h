// Titanfall 2 pilot weapon damage math, reimplemented from docs/TITANFALL2_COMBAT.md.
// Engine-independent (no Unreal headers). Distances in Titanfall units (inches), times in seconds.
//
// Confidence tags follow the repo legend:
//   [D] behavior read from shipped game scripts/data, [R] read from server.dll,
//   [C] computed by us, [I] inferred/assumed. See docs/ASSUMPTIONS.md section 11.
#pragma once

#include "TFWeaponTable.h"

namespace TF
{
	// Pilot health. [D] (scripts/players/mp/pilot_mp.set via pilot_base.set: health 100)
	inline constexpr float PilotHealth = 100.0f;

	// Looks up a pilot weapon by its in-game name ("R-201 Carbine") or file name ("mp_weapon_rspn101").
	// Returns nullptr if not found.
	const WeaponStats* FindWeapon(const char* NameOrFile);

	// Damage of ONE bullet/pellet/bolt at a distance, before headshot scaling.
	// Near value up to NearDistance, far value at FarDistance, optional very-far value at VeryFarDistance. [D]
	// The shape between those points is computed by the engine (CalcWeaponDamage, not decompiled);
	// linear interpolation is assumed. [I]
	float DamageAtDistance(const WeaponStats& Weapon, float Distance);

	// The weapon's max damage-far distance: the larger of FarDistance and VeryFarDistance. [I]
	// (The game asks the engine for GetMaxDamageFarDist(); which of the two it returns is not verified.)
	float MaxDamageFarDistance(const WeaponStats& Weapon);

	// Headshots only count within the max damage-far distance; beyond it they deal body damage. [D]
	// (scripts/vscripts/sh_damage_utility.gnut)
	bool HeadshotCounts(const WeaponStats& Weapon, float Distance);

	// Splash damage at a distance from the explosion. Full damage inside InnerRadius, falling linearly
	// to 0 at OuterRadius. [I] (falloff shape not verified). Zero when the radius is 0
	// (e.g. the L-STAR ships explosion_damage 30 with radius 0). [D]
	float SplashDamageAtDistance(float ExplosionDamage, float InnerRadius, float OuterRadius, float Distance);

	// Damage dealt to one target by one trigger pull. [C]
	//  - Pellet weapons: per-pellet damage x PelletsPerShot (all pellets assumed to hit). [I]
	//  - EVA-8 (bShotgunBlast): ONE distance-scaled hit per target, regardless of pellets. [D]
	//  - bHeadshot applies HeadshotScale only if HeadshotCounts(). [D]
	//  - bIncludeDirectHitSplash adds the full ExplosionDamage when ExplosionRadius > 0
	//    (a direct hit is assumed to also take full splash). [I]
	float DamagePerTriggerPull(const WeaponStats& Weapon, float Distance, bool bHeadshot, bool bIncludeDirectHitSplash = true);

	// Effective shots (trigger pulls) per second. [C]
	// Bursts: BurstCount / (BurstCount / FireRate + BurstDelay). Semi-auto: FireRate is the cap at perfect clicking. [D]
	float ShotsPerSecond(const WeaponStats& Weapon);

	// Trigger pulls needed to kill a target with TargetHealth (default 100 = pilot). [C]
	// Returns 0 if the weapon does no damage at that distance.
	int ShotsToKill(const WeaponStats& Weapon, float Distance, bool bHeadshot, float TargetHealth = PilotHealth);

	// Time from the first to the last required shot: (ShotsToKill - 1) / ShotsPerSecond. [C]
	// Ignores projectile travel, ADS time, misses and recoil. [I]
	float TimeToKill(const WeaponStats& Weapon, float Distance, bool bHeadshot, float TargetHealth = PilotHealth);

	// Raw damage per second while firing (no reloads). [C]
	float DamagePerSecond(const WeaponStats& Weapon, float Distance, bool bHeadshot = false);

	// Damage per second over one full magazine plus an empty reload. [C]
	float SustainedDamagePerSecond(const WeaponStats& Weapon, float Distance, bool bHeadshot = false);

	// Pilot movement speed multiplier while fully aimed down sights. [D] value, [I] that this is the field the
	// max-speed ADS blend reads (docs/TITANFALL2_MOVEMENT.md section 2). AdsFraction 0..1 blends from 1.0. [R] blend
	float AdsMoveSpeedMultiplier(const WeaponStats& Weapon, float AdsFraction);
}

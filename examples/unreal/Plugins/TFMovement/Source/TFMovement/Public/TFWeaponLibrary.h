// Blueprint access to the Titanfall 2 pilot weapon table and damage math (TFWeaponMath.h).
// Distances passed in and out of Blueprint are Unreal centimeters; internally they are converted to
// Titanfall units (1 unit = 1 inch = 2.54 cm).
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TFWeaponLibrary.generated.h"

namespace TFUnits
{
	/** 1 Titanfall unit = 1 inch = 2.54 cm. */
	inline constexpr float CmPerUnit = 2.54f;
}

/** Blueprint copy of one row of the shipped weapon table. Values are [D] unless noted. Distances in cm. */
USTRUCT(BlueprintType)
struct TFMOVEMENT_API FTFWeaponInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Weapon") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon") FString File;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon") FString Category;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Fire") float FireRate = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Fire") int32 BurstCount = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Fire") float BurstDelay = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Fire") bool bSemiAuto = false;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Fire") int32 PelletsPerShot = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Fire") bool bShotgunBlast = false;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Damage") float DamageNear = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Damage") float DamageFar = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Damage") float DamageVeryFar = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Damage") float NearDistanceCm = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Damage") float FarDistanceCm = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Damage") float VeryFarDistanceCm = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Damage") float HeadshotScale = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Damage") float DamageNearTitanArmor = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Damage") float ExplosionDamage = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Damage") float ExplosionRadiusCm = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Ammo") int32 MagazineSize = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Ammo") float ReloadTime = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Ammo") float ReloadEmptyTime = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|ADS") float AdsInTime = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|ADS") float AdsOutTime = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|ADS") float AdsFov = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|ADS") float AdsMoveSpeedScale = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Handling") float DeployTime = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Handling") float HolsterTime = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Handling") float RaiseTime = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Handling") float SpreadHip = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Handling") float SpreadHipMaxKick = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Handling") float SpreadAds = 0.f;
	/** 0 = hitscan. cm/s. */
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Handling") float ProjectileSpeedCm = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Handling") float ChargeTime = 0.f;
};

UCLASS()
class TFMOVEMENT_API UTFWeaponLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** In-game names of every pilot weapon in the table. */
	UFUNCTION(BlueprintPure, Category = "Titanfall|Weapons")
	static TArray<FString> GetPilotWeaponNames();

	/** Finds a weapon by in-game name ("R-201 Carbine") or file name ("mp_weapon_rspn101"). */
	UFUNCTION(BlueprintPure, Category = "Titanfall|Weapons")
	static bool GetPilotWeaponInfo(const FString& NameOrFile, FTFWeaponInfo& OutInfo);

	/** Damage of one bullet/pellet at a distance (cm), before headshot scaling. Falloff shape is assumed linear [I]. */
	UFUNCTION(BlueprintPure, Category = "Titanfall|Weapons")
	static float GetDamageAtDistance(const FString& NameOrFile, float DistanceCm);

	/** Damage of one trigger pull to one target (pellets, EVA blast rule, headshot range rule, direct-hit splash). [C] */
	UFUNCTION(BlueprintPure, Category = "Titanfall|Weapons")
	static float GetDamagePerTriggerPull(const FString& NameOrFile, float DistanceCm, bool bHeadshot);

	/** Trigger pulls to kill a target (default 100 HP pilot). 0 = can't damage at this range. [C] */
	UFUNCTION(BlueprintPure, Category = "Titanfall|Weapons")
	static int32 GetShotsToKill(const FString& NameOrFile, float DistanceCm, bool bHeadshot, float TargetHealth = 100.f);

	/** Seconds from first to last required shot. -1 = can't kill. Ignores travel time, ADS and misses. [C] */
	UFUNCTION(BlueprintPure, Category = "Titanfall|Weapons")
	static float GetTimeToKill(const FString& NameOrFile, float DistanceCm, bool bHeadshot, float TargetHealth = 100.f);

	/** Damage per second while firing (no reloads). [C] */
	UFUNCTION(BlueprintPure, Category = "Titanfall|Weapons")
	static float GetDamagePerSecond(const FString& NameOrFile, float DistanceCm, bool bHeadshot);

	/** Damage per second over a full magazine plus an empty reload. [C] */
	UFUNCTION(BlueprintPure, Category = "Titanfall|Weapons")
	static float GetSustainedDamagePerSecond(const FString& NameOrFile, float DistanceCm, bool bHeadshot);

	/** Splash damage at a distance (cm) from the explosion, linear from inner to outer radius [I]. */
	UFUNCTION(BlueprintPure, Category = "Titanfall|Weapons")
	static float GetSplashDamageAtDistance(float ExplosionDamage, float InnerRadiusCm, float OuterRadiusCm, float DistanceCm);
};

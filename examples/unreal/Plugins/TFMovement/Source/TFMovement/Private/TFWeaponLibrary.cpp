#include "TFWeaponLibrary.h"

#include "TFWeaponMath.h"

namespace
{
	const TF::WeaponStats* Find(const FString& NameOrFile)
	{
		return TF::FindWeapon(TCHAR_TO_UTF8(*NameOrFile));
	}

	float ToUnits(float Cm)
	{
		return Cm / TFUnits::CmPerUnit;
	}

	float ToCm(float Units)
	{
		return Units * TFUnits::CmPerUnit;
	}
}

TArray<FString> UTFWeaponLibrary::GetPilotWeaponNames()
{
	TArray<FString> Names;
	Names.Reserve(static_cast<int32>(TF::NumPilotWeapons));
	for (const TF::WeaponStats& Weapon : TF::PilotWeapons)
	{
		Names.Add(UTF8_TO_TCHAR(Weapon.Name));
	}
	return Names;
}

bool UTFWeaponLibrary::GetPilotWeaponInfo(const FString& NameOrFile, FTFWeaponInfo& OutInfo)
{
	const TF::WeaponStats* W = Find(NameOrFile);
	if (W == nullptr)
	{
		return false;
	}

	OutInfo.Name = UTF8_TO_TCHAR(W->Name);
	OutInfo.File = UTF8_TO_TCHAR(W->File);
	OutInfo.Category = UTF8_TO_TCHAR(W->Category);
	OutInfo.FireRate = W->FireRate;
	OutInfo.BurstCount = W->BurstCount;
	OutInfo.BurstDelay = W->BurstDelay;
	OutInfo.bSemiAuto = W->bSemiAuto;
	OutInfo.PelletsPerShot = W->PelletsPerShot;
	OutInfo.bShotgunBlast = W->bShotgunBlast;
	OutInfo.DamageNear = W->DamageNear;
	OutInfo.DamageFar = W->DamageFar;
	OutInfo.DamageVeryFar = W->DamageVeryFar;
	OutInfo.NearDistanceCm = ToCm(W->NearDistance);
	OutInfo.FarDistanceCm = ToCm(W->FarDistance);
	OutInfo.VeryFarDistanceCm = ToCm(W->VeryFarDistance);
	OutInfo.HeadshotScale = W->HeadshotScale;
	OutInfo.DamageNearTitanArmor = W->DamageNearTitanArmor;
	OutInfo.ExplosionDamage = W->ExplosionDamage;
	OutInfo.ExplosionRadiusCm = ToCm(W->ExplosionRadius);
	OutInfo.MagazineSize = W->MagazineSize;
	OutInfo.ReloadTime = W->ReloadTime;
	OutInfo.ReloadEmptyTime = W->ReloadEmptyTime;
	OutInfo.AdsInTime = W->AdsInTime;
	OutInfo.AdsOutTime = W->AdsOutTime;
	OutInfo.AdsFov = W->AdsFov;
	OutInfo.AdsMoveSpeedScale = W->AdsMoveSpeedScale;
	OutInfo.DeployTime = W->DeployTime;
	OutInfo.HolsterTime = W->HolsterTime;
	OutInfo.RaiseTime = W->RaiseTime;
	OutInfo.SpreadHip = W->SpreadHip;
	OutInfo.SpreadHipMaxKick = W->SpreadHipMaxKick;
	OutInfo.SpreadAds = W->SpreadAds;
	OutInfo.ProjectileSpeedCm = ToCm(W->ProjectileSpeed);
	OutInfo.ChargeTime = W->ChargeTime;
	return true;
}

float UTFWeaponLibrary::GetDamageAtDistance(const FString& NameOrFile, float DistanceCm)
{
	const TF::WeaponStats* W = Find(NameOrFile);
	return W ? TF::DamageAtDistance(*W, ToUnits(DistanceCm)) : 0.f;
}

float UTFWeaponLibrary::GetDamagePerTriggerPull(const FString& NameOrFile, float DistanceCm, bool bHeadshot)
{
	const TF::WeaponStats* W = Find(NameOrFile);
	return W ? TF::DamagePerTriggerPull(*W, ToUnits(DistanceCm), bHeadshot) : 0.f;
}

int32 UTFWeaponLibrary::GetShotsToKill(const FString& NameOrFile, float DistanceCm, bool bHeadshot, float TargetHealth)
{
	const TF::WeaponStats* W = Find(NameOrFile);
	return W ? TF::ShotsToKill(*W, ToUnits(DistanceCm), bHeadshot, TargetHealth) : 0;
}

float UTFWeaponLibrary::GetTimeToKill(const FString& NameOrFile, float DistanceCm, bool bHeadshot, float TargetHealth)
{
	const TF::WeaponStats* W = Find(NameOrFile);
	return W ? TF::TimeToKill(*W, ToUnits(DistanceCm), bHeadshot, TargetHealth) : -1.f;
}

float UTFWeaponLibrary::GetDamagePerSecond(const FString& NameOrFile, float DistanceCm, bool bHeadshot)
{
	const TF::WeaponStats* W = Find(NameOrFile);
	return W ? TF::DamagePerSecond(*W, ToUnits(DistanceCm), bHeadshot) : 0.f;
}

float UTFWeaponLibrary::GetSustainedDamagePerSecond(const FString& NameOrFile, float DistanceCm, bool bHeadshot)
{
	const TF::WeaponStats* W = Find(NameOrFile);
	return W ? TF::SustainedDamagePerSecond(*W, ToUnits(DistanceCm), bHeadshot) : 0.f;
}

float UTFWeaponLibrary::GetSplashDamageAtDistance(float ExplosionDamage, float InnerRadiusCm, float OuterRadiusCm, float DistanceCm)
{
	return TF::SplashDamageAtDistance(ExplosionDamage, ToUnits(InnerRadiusCm), ToUnits(OuterRadiusCm), ToUnits(DistanceCm));
}

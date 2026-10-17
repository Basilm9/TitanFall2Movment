#include "TFWeaponMath.h"

#include <cmath>
#include <cstring>

namespace TF
{
	namespace
	{
		float Lerp(float A, float B, float T)
		{
			return A + (B - A) * T;
		}

		float Clamp01(float V)
		{
			return V < 0.0f ? 0.0f : (V > 1.0f ? 1.0f : V);
		}
	}

	const WeaponStats* FindWeapon(const char* NameOrFile)
	{
		if (NameOrFile == nullptr)
		{
			return nullptr;
		}
		for (const WeaponStats& Weapon : PilotWeapons)
		{
			if (std::strcmp(Weapon.Name, NameOrFile) == 0 || std::strcmp(Weapon.File, NameOrFile) == 0)
			{
				return &Weapon;
			}
		}
		return nullptr;
	}

	float DamageAtDistance(const WeaponStats& Weapon, float Distance)
	{
		if (Distance <= Weapon.NearDistance)
		{
			return Weapon.DamageNear;
		}
		if (Distance <= Weapon.FarDistance)
		{
			const float Span = Weapon.FarDistance - Weapon.NearDistance;
			const float T = Span > 0.0f ? (Distance - Weapon.NearDistance) / Span : 1.0f;
			return Lerp(Weapon.DamageNear, Weapon.DamageFar, T); // [I] linear
		}
		const bool bHasVeryFar = Weapon.VeryFarDistance > Weapon.FarDistance && Weapon.DamageVeryFar > 0.0f;
		if (!bHasVeryFar)
		{
			return Weapon.DamageFar;
		}
		if (Distance <= Weapon.VeryFarDistance)
		{
			const float T = (Distance - Weapon.FarDistance) / (Weapon.VeryFarDistance - Weapon.FarDistance);
			return Lerp(Weapon.DamageFar, Weapon.DamageVeryFar, T); // [I] linear
		}
		return Weapon.DamageVeryFar;
	}

	float MaxDamageFarDistance(const WeaponStats& Weapon)
	{
		return Weapon.VeryFarDistance > Weapon.FarDistance ? Weapon.VeryFarDistance : Weapon.FarDistance;
	}

	bool HeadshotCounts(const WeaponStats& Weapon, float Distance)
	{
		return Distance <= MaxDamageFarDistance(Weapon);
	}

	float SplashDamageAtDistance(float ExplosionDamage, float InnerRadius, float OuterRadius, float Distance)
	{
		if (OuterRadius <= 0.0f || ExplosionDamage <= 0.0f || Distance > OuterRadius)
		{
			return 0.0f;
		}
		if (Distance <= InnerRadius)
		{
			return ExplosionDamage;
		}
		const float Span = OuterRadius - InnerRadius;
		return Span > 0.0f ? ExplosionDamage * (1.0f - (Distance - InnerRadius) / Span) : ExplosionDamage; // [I] linear
	}

	float DamagePerTriggerPull(const WeaponStats& Weapon, float Distance, bool bHeadshot, bool bIncludeDirectHitSplash)
	{
		float PerHit = DamageAtDistance(Weapon, Distance);
		if (bHeadshot && HeadshotCounts(Weapon, Distance))
		{
			PerHit *= Weapon.HeadshotScale;
		}

		const int Hits = Weapon.bShotgunBlast ? 1 : (Weapon.PelletsPerShot > 0 ? Weapon.PelletsPerShot : 1);
		float Total = PerHit * static_cast<float>(Hits);

		if (bIncludeDirectHitSplash && Weapon.ExplosionRadius > 0.0f)
		{
			Total += Weapon.ExplosionDamage;
		}
		return Total;
	}

	float ShotsPerSecond(const WeaponStats& Weapon)
	{
		if (Weapon.FireRate <= 0.0f)
		{
			return 0.0f;
		}
		if (Weapon.BurstCount > 1)
		{
			const float Count = static_cast<float>(Weapon.BurstCount);
			return Count / (Count / Weapon.FireRate + Weapon.BurstDelay);
		}
		return Weapon.FireRate;
	}

	int ShotsToKill(const WeaponStats& Weapon, float Distance, bool bHeadshot, float TargetHealth)
	{
		const float Damage = DamagePerTriggerPull(Weapon, Distance, bHeadshot);
		if (Damage <= 0.0f)
		{
			return 0;
		}
		return static_cast<int>(std::ceil(TargetHealth / Damage));
	}

	float TimeToKill(const WeaponStats& Weapon, float Distance, bool bHeadshot, float TargetHealth)
	{
		const int Shots = ShotsToKill(Weapon, Distance, bHeadshot, TargetHealth);
		const float Rate = ShotsPerSecond(Weapon);
		if (Shots <= 0 || Rate <= 0.0f)
		{
			return -1.0f;
		}
		return static_cast<float>(Shots - 1) / Rate;
	}

	float DamagePerSecond(const WeaponStats& Weapon, float Distance, bool bHeadshot)
	{
		return DamagePerTriggerPull(Weapon, Distance, bHeadshot) * ShotsPerSecond(Weapon);
	}

	float SustainedDamagePerSecond(const WeaponStats& Weapon, float Distance, bool bHeadshot)
	{
		const float Rate = ShotsPerSecond(Weapon);
		if (Weapon.MagazineSize <= 0 || Rate <= 0.0f || Weapon.AmmoPerShot <= 0.0f)
		{
			return 0.0f;
		}
		const float Shots = static_cast<float>(Weapon.MagazineSize) / Weapon.AmmoPerShot;
		const float MagDumpTime = Shots / Rate;
		const float CycleTime = MagDumpTime + Weapon.ReloadEmptyTime;
		return CycleTime > 0.0f ? DamagePerTriggerPull(Weapon, Distance, bHeadshot) * Shots / CycleTime : 0.0f;
	}

	float AdsMoveSpeedMultiplier(const WeaponStats& Weapon, float AdsFraction)
	{
		const float Scale = Weapon.AdsMoveSpeedScale > 0.0f ? Weapon.AdsMoveSpeedScale : 1.0f;
		return Lerp(1.0f, Scale, Clamp01(AdsFraction));
	}
}

// TFPilotMovementComponent.cpp

#include "TFPilotMovementComponent.h"

#include "Components/CapsuleComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "TFZiplineActor.h"

// Named (not anonymous) so unity builds cannot collide with other files.
namespace TFPilotMovementImpl
{
	/** Actor or component tag that makes a surface reject grapple hooks (trigger_no_grapple stand-in). */
	const FName NoGrappleTag(TEXT("NoGrapple"));

	/**
	 * ITFWorld implemented with Unreal scene queries. Hull sweeps use the pawn capsule shape on
	 * ECC_Pawn; line traces use ECC_Visibility. Positions are converted with TFMove::ToTF/ToUnreal.
	 */
	struct FTFUnrealWorld final : public ITFWorld
	{
		UTFPilotMovementComponent& Comp;
		UWorld* World;
		FCollisionQueryParams Params;

		explicit FTFUnrealWorld(UTFPilotMovementComponent& InComp)
			: Comp(InComp)
			, World(InComp.GetWorld())
			, Params(SCENE_QUERY_STAT(TFPilotMove), false, InComp.GetOwner())
		{
		}

		static bool IsNoGrapple(const FHitResult& Hit)
		{
			const UPrimitiveComponent* HitComp = Hit.GetComponent();
			const AActor* HitActor = Hit.GetActor();
			return (HitComp && HitComp->ComponentHasTag(NoGrappleTag)) || (HitActor && HitActor->ActorHasTag(NoGrappleTag));
		}

		static TFTraceResult Convert(const FHitResult& Hit, bool bHit, const TFVec3& Start, const TFVec3& End, float CenterOffsetCm)
		{
			TFTraceResult R;
			if (!bHit)
			{
				R.Fraction = 1.f;
				R.EndPos = End;
				return R;
			}
			R.bStartSolid = Hit.bStartPenetrating;
			R.Fraction = Hit.bStartPenetrating ? 0.f : FMath::Clamp(static_cast<float>(Hit.Time), 0.f, 1.f);
			R.EndPos = Hit.bStartPenetrating ? Start : TFMove::ToTF(Hit.Location - FVector(0.0, 0.0, CenterOffsetCm));
			// Source box traces report face normals. A capsule's ImpactNormal is the closest analogue;
			// on edges the capsule-relative Normal is flatter-to-stand-on, so the more upward one wins.
			FVector N = Hit.ImpactNormal;
			if (Hit.bStartPenetrating || Hit.Normal.Z > N.Z)
			{
				N = Hit.Normal;
			}
			R.Normal = TFMove::ToTFDir(N.GetSafeNormal());
			R.HitEntityId = 0; // hooks attach to world positions only (see README)
			R.bNoGrapple = IsNoGrapple(Hit);
			return R;
		}

		virtual TFTraceResult SweepHull(const TFVec3& Start, const TFVec3& End, bool bCrouchedHull) override
		{
			TFTraceResult R;
			if (!World)
			{
				R.EndPos = End;
				return R;
			}
			const float HalfH = Comp.GetHullHalfHeightCm(bCrouchedHull);
			const float Radius = Comp.GetHullRadiusCm();
			const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfH);
			const FVector S = TFMove::ToUnreal(Start) + FVector(0.0, 0.0, HalfH);
			const FVector E = TFMove::ToUnreal(End) + FVector(0.0, 0.0, HalfH);

			if (S.Equals(E, 1e-4))
			{
				// Zero-length sweep: a plain blocking overlap test.
				R.EndPos = End;
				if (World->OverlapBlockingTestByChannel(S, FQuat::Identity, ECC_Pawn, Shape, Params))
				{
					R.bStartSolid = true;
					R.Fraction = 0.f;
					R.EndPos = Start;
				}
				return R;
			}

			FHitResult Hit;
			const bool bHit = World->SweepSingleByChannel(Hit, S, E, FQuat::Identity, ECC_Pawn, Shape, Params);
			return Convert(Hit, bHit, Start, End, HalfH);
		}

		virtual TFTraceResult TraceLine(const TFVec3& Start, const TFVec3& End) override
		{
			TFTraceResult R;
			if (!World)
			{
				R.EndPos = End;
				return R;
			}
			FHitResult Hit;
			const bool bHit = World->LineTraceSingleByChannel(Hit, TFMove::ToUnreal(Start), TFMove::ToUnreal(End), ECC_Visibility, Params);
			R = Convert(Hit, bHit, Start, End, 0.f);
			if (bHit && !Hit.bStartPenetrating)
			{
				R.EndPos = TFMove::ToTF(Hit.ImpactPoint);
				R.Normal = TFMove::ToTFDir(Hit.ImpactNormal.GetSafeNormal());
			}
			return R;
		}

		virtual float Now() override
		{
			return Comp.GetSimTime();
		}

		virtual int QueryZiplines(const TFVec3& Center, float Radius, const TFZiplineRope** OutRopes, int MaxRopes) override
		{
			TArray<ATFZiplineActor*> Ziplines;
			ATFZiplineActor::GetZiplinesInWorld(World, Ziplines);
			const FVector CenterUE = TFMove::ToUnreal(Center);
			const float RadiusCm = Radius * TFMove::CmPerUnit;
			int Count = 0;
			for (ATFZiplineActor* Zip : Ziplines)
			{
				if (Count >= MaxRopes)
				{
					break;
				}
				if (Zip && Zip->IsNearRope(CenterUE, RadiusCm))
				{
					OutRopes[Count++] = &Zip->GetRopeTF();
				}
			}
			return Count;
		}

		virtual const TFZiplineRope* GetZipline(int Id) override
		{
			ATFZiplineActor* Zip = ATFZiplineActor::FindZiplineById(World, Id);
			return Zip ? &Zip->GetRopeTF() : nullptr;
		}

		virtual void OnMoveEvent(ETFMoveEvent Event, float Value) override
		{
			Comp.HandleCoreEvent(Event, Value);
		}
	};
}

UTFPilotMovementComponent::UTFPilotMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	bUpdateOnlyIfRendered = false;
}

void UTFPilotMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	InitPilot();
}

float UTFPilotMovementComponent::GetHullRadiusCm() const
{
	return Settings.HullRadius * TFMove::CmPerUnit;
}

float UTFPilotMovementComponent::GetHullHalfHeightCm(bool bCrouched) const
{
	const float HeightUnits = bCrouched ? Settings.CrouchHullHeight : Settings.StandHullHeight;
	return 0.5f * HeightUnits * TFMove::CmPerUnit;
}

FVector UTFPilotMovementComponent::FeetToCenter(const TFVec3& Feet) const
{
	return TFMove::ToUnreal(Feet) + FVector(0.0, 0.0, GetHullHalfHeightCm(State.bCrouchedHull));
}

TFVec3 UTFPilotMovementComponent::CenterToFeet(const FVector& Center) const
{
	return TFMove::ToTF(Center - FVector(0.0, 0.0, GetHullHalfHeightCm(State.bCrouchedHull)));
}

void UTFPilotMovementComponent::InitPilot()
{
	if (!UpdatedComponent)
	{
		return;
	}
	if (UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(UpdatedComponent))
	{
		Capsule->SetCapsuleSize(GetHullRadiusCm(), GetHullHalfHeightCm(false));
	}
	TFResetPilot(State, TFVec3());
	State.Origin = CenterToFeet(UpdatedComponent->GetComponentLocation());
	State.EyeHeight = Settings.StandEyeHeight;
	if (PawnOwner)
	{
		ViewRotation = PawnOwner->GetControlRotation();
	}
	SimTime = 0.f;
	Accumulator = 0.f;
	bPilotInitialized = true;
}

void UTFPilotMovementComponent::ResetPilotAt(FVector CapsuleCenterLocation)
{
	if (UpdatedComponent)
	{
		UpdatedComponent->SetWorldLocation(CapsuleCenterLocation, false, nullptr, ETeleportType::TeleportPhysics);
	}
	InitPilot();
}

void UTFPilotMovementComponent::SetPilotVelocity(FVector NewVelocity)
{
	State.Velocity = TFMove::ToTF(NewVelocity);
	Velocity = NewVelocity;
}

// ------------------------------------------------------------------------------------------ input

void UTFPilotMovementComponent::SetMoveInput(float Forward, float Right)
{
	MoveForward = FMath::Clamp(Forward, -1.f, 1.f);
	MoveRight = FMath::Clamp(Right, -1.f, 1.f);
}

void UTFPilotMovementComponent::SetViewRotation(FRotator InViewRotation)
{
	ViewRotation = InViewRotation;
}

void UTFPilotMovementComponent::SetJumpPressed(bool bPressed)
{
	if (bPressed && !bJumpHeld) { bJumpLatch = true; }
	bJumpHeld = bPressed;
}

void UTFPilotMovementComponent::SetCrouchPressed(bool bPressed)
{
	if (bPressed && !bCrouchHeld) { bCrouchLatch = true; }
	bCrouchHeld = bPressed;
}

void UTFPilotMovementComponent::SetSprintPressed(bool bPressed)
{
	if (bPressed && !bSprintHeld) { bSprintLatch = true; }
	bSprintHeld = bPressed;
}

void UTFPilotMovementComponent::SetAdsPressed(bool bPressed)
{
	if (bPressed && !bAdsHeld) { bAdsLatch = true; }
	bAdsHeld = bPressed;
}

void UTFPilotMovementComponent::SetGrapplePressed(bool bPressed)
{
	if (bPressed && !bGrappleHeld) { bGrappleLatch = true; }
	bGrappleHeld = bPressed;
}

void UTFPilotMovementComponent::SetUsePressed(bool bPressed)
{
	if (bPressed && !bUseHeld) { bUseLatch = true; }
	bUseHeld = bPressed;
}

void UTFPilotMovementComponent::SetWeaponSpeedScales(float WeaponSpeedScale, float AdsMoveSpeedScale)
{
	WeaponSpeedScaleInput = WeaponSpeedScale;
	AdsMoveSpeedScaleInput = AdsMoveSpeedScale;
}

TFPilotInput UTFPilotMovementComponent::BuildInput(bool bFirstStep) const
{
	TFPilotInput In;
	In.ForwardMove = MoveForward;
	In.SideMove = MoveRight;
	// Unreal: pitch positive = up, yaw toward +Y (right). Source: pitch positive = down, +Y is left.
	In.ViewPitch = static_cast<float>(-FRotator::NormalizeAxis(ViewRotation.Pitch));
	In.ViewYaw = static_cast<float>(-FRotator::NormalizeAxis(ViewRotation.Yaw));
	In.bJumpHeld = bJumpHeld;
	In.bCrouchHeld = bCrouchHeld;
	In.bSprintHeld = bSprintHeld;
	In.bAdsHeld = bAdsHeld;
	In.bGrappleHeld = bGrappleHeld;
	// Presses are delivered on the first fixed step of the frame only.
	In.bJumpPressed = bFirstStep && bJumpLatch;
	In.bCrouchPressed = bFirstStep && bCrouchLatch;
	In.bSprintPressed = bFirstStep && bSprintLatch;
	In.bAdsPressed = bFirstStep && bAdsLatch;
	In.bGrapplePressed = bFirstStep && bGrappleLatch;
	In.bUsePressed = bFirstStep && bUseLatch;
	In.WeaponSpeedScale = WeaponSpeedScaleInput;
	In.AdsMoveSpeedScale = AdsMoveSpeedScaleInput;
	In.bAlive = true;
	return In;
}

// ------------------------------------------------------------------------------------------ tick

void UTFPilotMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!PawnOwner || !UpdatedComponent || ShouldSkipUpdate(DeltaTime))
	{
		return;
	}
	if (!bPilotInitialized)
	{
		InitPilot();
	}
	if (bUseControllerView && PawnOwner->Controller)
	{
		ViewRotation = PawnOwner->GetControlRotation();
	}

	TFPilotSettings CoreSettings;
	Settings.ToCore(CoreSettings);
	TFMovementConVars CoreConVars;
	ConVars.ToCore(CoreConVars);

	const float Step = 1.f / FMath::Clamp(SimulationRate, 10.f, 240.f);
	Accumulator = FMath::Min(Accumulator + DeltaTime, Step * static_cast<float>(FMath::Max(MaxStepsPerFrame, 1)));

	TFPilotMovementImpl::FTFUnrealWorld Adapter(*this);
	bool bFirstStep = true;
	while (Accumulator >= Step)
	{
		Accumulator -= Step;
		const TFPilotInput In = BuildInput(bFirstStep);
		const bool bWasCrouched = State.bCrouchedHull;
		SimTime += Step;
		TFPilotMoveEx(State, In, CoreSettings, CoreConVars, Adapter, Step);
		ApplyStateToComponent(bWasCrouched);
		bFirstStep = false;
	}

	if (!bFirstStep)
	{
		// At least one step consumed the presses.
		bJumpLatch = bCrouchLatch = bSprintLatch = bAdsLatch = bGrappleLatch = bUseLatch = false;
	}

	Velocity = TFMove::ToUnreal(State.Velocity);
	UpdateComponentVelocity();

	if (bDrawDebug)
	{
		DrawDebug();
	}
}

void UTFPilotMovementComponent::ApplyStateToComponent(bool bWasCrouched)
{
	UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(UpdatedComponent);
	const bool bStanceChanged = bWasCrouched != State.bCrouchedHull;
	if (bStanceChanged && Capsule)
	{
		Capsule->SetCapsuleSize(GetHullRadiusCm(), GetHullHalfHeightCm(State.bCrouchedHull));
	}

	const FVector Target = FeetToCenter(State.Origin);
	const FVector Delta = Target - UpdatedComponent->GetComponentLocation();
	if (Delta.IsNearlyZero(1e-3))
	{
		return;
	}

	// The core already resolved collision with the same queries; the sweep only catches
	// disagreements. Mantle / zipline paths and stance changes are placed without sweeping.
	const bool bTeleport = bStanceChanged || State.Mantle.bActive || State.Zipline.State != 0;
	FHitResult Hit;
	SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), !bTeleport, Hit);
	if (Hit.IsValidBlockingHit())
	{
		SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, true);
	}

	// Keep the core in sync with where Unreal actually put the capsule.
	State.Origin = CenterToFeet(UpdatedComponent->GetComponentLocation());
}

// ------------------------------------------------------------------------------------------ queries

float UTFPilotMovementComponent::GetMaxSpeed() const
{
	return State.MaxSpeed * TFMove::CmPerUnit;
}

bool UTFPilotMovementComponent::IsFalling() const
{
	return !State.bOnGround && !State.bWallRunning && !State.Mantle.bActive && State.Zipline.State == 0;
}

bool UTFPilotMovementComponent::IsMovingOnGround() const
{
	return State.bOnGround;
}

bool UTFPilotMovementComponent::IsCrouching() const
{
	return State.bCrouchedHull;
}

float UTFPilotMovementComponent::GetEyeHeightCm() const
{
	float Eye = State.EyeHeight;
	if (State.bSprinting)
	{
		// Sprint view offset: client-side in the game; shape read from the help text [I].
		const float Elapsed = SimTime - State.SprintStartedTime - Settings.SprintStartDelay;
		const float Frac = FMath::Clamp(Elapsed / FMath::Max(Settings.SprintStartDuration, 0.01f), 0.f, 1.f);
		Eye += Settings.SprintViewOffset * Frac;
	}
	return Eye * TFMove::CmPerUnit;
}

FVector UTFPilotMovementComponent::GetWallNormal() const
{
	return State.bWallRunning ? TFMove::ToUnrealDir(State.WallNormal) : FVector::ZeroVector;
}

bool UTFPilotMovementComponent::GetGrappleHookLocation(FVector& OutLocation) const
{
	const TFGrappleState& G = State.Grapple;
	if (G.Phase == ETFHookPhase::None || G.PointCount <= 0)
	{
		OutLocation = FVector::ZeroVector;
		return false;
	}
	OutLocation = TFMove::ToUnreal(G.Points[G.PointCount - 1]);
	return true;
}

void UTFPilotMovementComponent::StartStim(float Duration)
{
	TFPilotSettings CoreSettings;
	Settings.ToCore(CoreSettings);
	TFStartStim(State, CoreSettings, SimTime, FMath::Max(Duration, 0.f));
}

void UTFPilotMovementComponent::HandleCoreEvent(ETFMoveEvent Event, float Value)
{
	OnMoveEvent.Broadcast(static_cast<ETFMoveEventUE>(static_cast<uint8>(Event)), Value);
}

void UTFPilotMovementComponent::DrawDebug() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const FVector Eye = TFMove::ToUnreal(State.Origin) + FVector(0.0, 0.0, State.EyeHeight * TFMove::CmPerUnit);
	const TFGrappleState& G = State.Grapple;
	if (G.Phase != ETFHookPhase::None && G.PointCount > 0)
	{
		FVector Prev = Eye;
		for (int32 I = 0; I < G.PointCount; ++I)
		{
			const FVector P = TFMove::ToUnreal(G.Points[I]);
			DrawDebugLine(World, Prev, P, G.bPulling ? FColor::Green : FColor::Yellow, false, -1.f, 0, 1.5f);
			Prev = P;
		}
	}
	if (State.bWallRunning)
	{
		const FVector Feet = TFMove::ToUnreal(State.Origin);
		DrawDebugLine(World, Feet, Feet + TFMove::ToUnrealDir(State.WallNormal) * 60.0, FColor::Cyan, false, -1.f, 0, 2.f);
	}
}

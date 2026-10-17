// TFPilotMovementComponent.h
//
// Unreal wrapper around the engine-independent Titanfall 2 pilot movement core (TFMovementCore.h).
// The core runs in Titanfall units with Source axes; this component converts at the boundary:
//   * 1 Titanfall unit = 2.54 cm (TFMove::CmPerUnit)
//   * Source is right-handed (+Y left), Unreal is left-handed (+Y right): Y is mirrored, and view
//     yaw / pitch change sign.
// Settings stay in Titanfall units (inches, u/s); only positions and velocities are converted.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "TFMovementCore.h"
#include "TFPilotSettingsUE.h"
#include "TFPilotMovementComponent.generated.h"

namespace TFMove
{
	/** One Titanfall/Source unit (1 inch) in centimetres. The only unit constant used at the boundary. */
	inline constexpr float CmPerUnit = 2.54f;

	/** Position / velocity: Unreal (cm, +Y right) -> Titanfall (units, +Y left). */
	inline TFVec3 ToTF(const FVector& V)
	{
		return TFVec3(static_cast<float>(V.X / CmPerUnit), static_cast<float>(-V.Y / CmPerUnit), static_cast<float>(V.Z / CmPerUnit));
	}

	/** Position / velocity: Titanfall -> Unreal. */
	inline FVector ToUnreal(const TFVec3& V)
	{
		return FVector(V.X * CmPerUnit, -V.Y * CmPerUnit, V.Z * CmPerUnit);
	}

	/** Unit direction (no scaling), Unreal -> Titanfall. */
	inline TFVec3 ToTFDir(const FVector& V)
	{
		return TFVec3(static_cast<float>(V.X), static_cast<float>(-V.Y), static_cast<float>(V.Z));
	}

	/** Unit direction (no scaling), Titanfall -> Unreal. */
	inline FVector ToUnrealDir(const TFVec3& V)
	{
		return FVector(V.X, -V.Y, V.Z);
	}
}

/** Mirror of ETFMoveEvent (same order). */
UENUM(BlueprintType)
enum class ETFMoveEventUE : uint8
{
	Jump,
	SkipJump,
	DoubleJump,
	WallJump,
	SlideJump,
	Land,
	Impact,
	SlideStart,
	SlideEnd,
	WallRunStart,
	WallRunEnd,
	WallHangStart,
	MantleStart,
	MantleEnd,
	GrappleFire,
	GrappleAttach,
	GrapplePullStart,
	GrappleDetach,
	ZiplineAttach,
	ZiplineDetach,
	Dodge
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FTFOnPilotMoveEvent, ETFMoveEventUE, Event, float, Value);

/**
 * Pawn movement component running Titanfall 2 pilot movement (ground, sprint, crouch, slide, jump,
 * double jump, skip, coyote time, keyboard grace, wallrun / wall jump / wallhang, auto-mantle,
 * grapple, zipline, Stim).
 *
 * The owning pawn feeds input through SetMoveInput / SetJumpPressed / ...; the view comes from the
 * controller (or SetViewRotation). The core is stepped at a fixed rate (SimulationRate).
 */
UCLASS(ClassGroup = (Movement), meta = (BlueprintSpawnableComponent))
class TFMOVEMENT_API UTFPilotMovementComponent : public UPawnMovementComponent
{
	GENERATED_BODY()

public:
	UTFPilotMovementComponent();

	//~ UActorComponent / UMovementComponent / UNavMovementComponent
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual float GetMaxSpeed() const override;
	virtual bool IsFalling() const override;
	virtual bool IsMovingOnGround() const override;
	virtual bool IsCrouching() const override;

	/** Pilot class settings, Titanfall units. Defaults are the shipped MP pilot values. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TF Movement")
	FTFPilotSettingsUE Settings;

	/** Server ConVars (registered defaults). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TF Movement", AdvancedDisplay)
	FTFMovementConVarsUE ConVars;

	/** Fixed simulation rate in Hz (the game's usercmd rate is 60 [I]). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TF Movement", meta = (ClampMin = "10", ClampMax = "240"))
	float SimulationRate = 60.f;

	/** Upper bound on fixed steps per frame (prevents a spiral after hitches). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TF Movement", meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaxStepsPerFrame = 8;

	/** Use the owning controller's control rotation as the view each tick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TF Movement")
	bool bUseControllerView = true;

	/** Draws the grapple line and wall normal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TF Movement|Debug")
	bool bDrawDebug = false;

	/** Fired for jumps, landings, slides, wallruns, mantles, grapple and zipline transitions. */
	UPROPERTY(BlueprintAssignable, Category = "TF Movement")
	FTFOnPilotMoveEvent OnMoveEvent;

	// ---------------------------------------------------------------- input (called by the pawn)

	/** Forward / right stick axes, -1..1. */
	UFUNCTION(BlueprintCallable, Category = "TF Movement|Input")
	void SetMoveInput(float Forward, float Right);

	/** Explicit view rotation (used when bUseControllerView is false). */
	UFUNCTION(BlueprintCallable, Category = "TF Movement|Input")
	void SetViewRotation(FRotator InViewRotation);

	/** true on press, false on release. A press is latched until the next simulation step. */
	UFUNCTION(BlueprintCallable, Category = "TF Movement|Input")
	void SetJumpPressed(bool bPressed);

	UFUNCTION(BlueprintCallable, Category = "TF Movement|Input")
	void SetCrouchPressed(bool bPressed);

	UFUNCTION(BlueprintCallable, Category = "TF Movement|Input")
	void SetSprintPressed(bool bPressed);

	UFUNCTION(BlueprintCallable, Category = "TF Movement|Input")
	void SetAdsPressed(bool bPressed);

	/** Fires the grapple; pressing again while attached releases it. */
	UFUNCTION(BlueprintCallable, Category = "TF Movement|Input")
	void SetGrapplePressed(bool bPressed);

	/** Use key (zipline attach). */
	UFUNCTION(BlueprintCallable, Category = "TF Movement|Input")
	void SetUsePressed(bool bPressed);

	/** Weapon move-speed scale and its ADS scale (weapon data, 1 = no change). */
	UFUNCTION(BlueprintCallable, Category = "TF Movement|Input")
	void SetWeaponSpeedScales(float WeaponSpeedScale, float AdsMoveSpeedScale);

	// ---------------------------------------------------------------- queries

	UFUNCTION(BlueprintPure, Category = "TF Movement")
	bool IsWallRunning() const { return State.bWallRunning; }

	UFUNCTION(BlueprintPure, Category = "TF Movement")
	bool IsWallHanging() const { return State.bWallRunning && State.bWallHanging; }

	UFUNCTION(BlueprintPure, Category = "TF Movement")
	bool IsSliding() const { return State.bSliding; }

	UFUNCTION(BlueprintPure, Category = "TF Movement")
	bool IsOnGround() const { return State.bOnGround; }

	UFUNCTION(BlueprintPure, Category = "TF Movement")
	bool IsMantling() const { return State.Mantle.bActive; }

	/** Hook attached and the line is live. */
	UFUNCTION(BlueprintPure, Category = "TF Movement")
	bool IsGrappling() const { return State.Grapple.bActive && State.Grapple.bAttached; }

	/** Hook visible (flying, attached or returning). */
	UFUNCTION(BlueprintPure, Category = "TF Movement")
	bool IsGrappleHookOut() const { return State.Grapple.Phase != ETFHookPhase::None; }

	UFUNCTION(BlueprintPure, Category = "TF Movement")
	bool IsZiplining() const { return State.Zipline.State != 0; }

	UFUNCTION(BlueprintPure, Category = "TF Movement")
	bool IsSprinting() const { return State.bSprinting; }

	/** Crouched hull in use. */
	UFUNCTION(BlueprintPure, Category = "TF Movement")
	bool IsDucked() const { return State.bCrouchedHull; }

	/** 3D speed in cm/s. */
	UFUNCTION(BlueprintPure, Category = "TF Movement")
	float GetSpeed() const { return State.Velocity.Length() * TFMove::CmPerUnit; }

	/** 3D speed in Titanfall units/s (what the game's speedometer would show). */
	UFUNCTION(BlueprintPure, Category = "TF Movement")
	float GetSpeedTF() const { return State.Velocity.Length(); }

	/** Horizontal speed in Titanfall units/s. */
	UFUNCTION(BlueprintPure, Category = "TF Movement")
	float GetHorizontalSpeedTF() const { return State.Velocity.Length2D(); }

	/** Eye height above the feet in cm (duck blend + sprint view offset [I]). */
	UFUNCTION(BlueprintPure, Category = "TF Movement")
	float GetEyeHeightCm() const;

	/** Current wall normal (Unreal axes); zero when not wallrunning. */
	UFUNCTION(BlueprintPure, Category = "TF Movement")
	FVector GetWallNormal() const;

	/** Grapple hook tip in world space (cm). Returns false when the hook is not out. */
	UFUNCTION(BlueprintPure, Category = "TF Movement")
	bool GetGrappleHookLocation(FVector& OutLocation) const;

	/** Camera roll in degrees (wallrun tilt, client-side in the game [I]). */
	UFUNCTION(BlueprintPure, Category = "TF Movement")
	float GetViewRollDegrees() const { return -State.ViewRoll; }

	/** Sprint body-tilt pose value, -1..1 (x sprinttiltMaxRoll for degrees). */
	UFUNCTION(BlueprintPure, Category = "TF Movement")
	float GetSprintTilt() const { return State.SprintTilt; }

	UFUNCTION(BlueprintPure, Category = "TF Movement")
	float GetSuitPower() const { return State.SuitPower; }

	UFUNCTION(BlueprintPure, Category = "TF Movement")
	float GetGrapplePower() const { return State.GrapplePower; }

	UFUNCTION(BlueprintPure, Category = "TF Movement")
	int32 GetAirJumpsUsed() const { return State.AirJumpCount; }

	/** Mantle wants the weapon lowered. */
	UFUNCTION(BlueprintPure, Category = "TF Movement")
	bool IsMantleWeaponDisabled() const { return State.Mantle.bActive && State.Mantle.bWeaponDisabled; }

	// ---------------------------------------------------------------- actions

	/** Stim: speed_boost severity 0.4 for Duration + 0.5 s (SpeedScale x 1.8). */
	UFUNCTION(BlueprintCallable, Category = "TF Movement")
	void StartStim(float Duration);

	/** Places the pilot (feet at the capsule bottom of the given capsule-centre location) and resets state. */
	UFUNCTION(BlueprintCallable, Category = "TF Movement")
	void ResetPilotAt(FVector CapsuleCenterLocation);

	/** Overrides the velocity (cm/s). */
	UFUNCTION(BlueprintCallable, Category = "TF Movement")
	void SetPilotVelocity(FVector NewVelocity);

	// ---------------------------------------------------------------- C++ access

	const TFPilotState& GetPilotState() const { return State; }
	TFPilotState& GetMutablePilotState() { return State; }

	/** Simulation clock in seconds (what the core sees as curtime). */
	float GetSimTime() const { return SimTime; }

	float GetHullRadiusCm() const;
	float GetHullHalfHeightCm(bool bCrouched) const;

	/** Called by the world adapter. */
	void HandleCoreEvent(ETFMoveEvent Event, float Value);

private:
	void InitPilot();
	TFPilotInput BuildInput(bool bFirstStep) const;
	void ApplyStateToComponent(bool bWasCrouched);
	FVector FeetToCenter(const TFVec3& Feet) const;
	TFVec3 CenterToFeet(const FVector& Center) const;
	void DrawDebug() const;

	TFPilotState State;
	float SimTime = 0.f;
	float Accumulator = 0.f;
	bool bPilotInitialized = false;

	// Input
	float MoveForward = 0.f;
	float MoveRight = 0.f;
	FRotator ViewRotation = FRotator::ZeroRotator;
	float WeaponSpeedScaleInput = 1.f;
	float AdsMoveSpeedScaleInput = 1.f;
	bool bJumpHeld = false, bJumpLatch = false;
	bool bCrouchHeld = false, bCrouchLatch = false;
	bool bSprintHeld = false, bSprintLatch = false;
	bool bAdsHeld = false, bAdsLatch = false;
	bool bGrappleHeld = false, bGrappleLatch = false;
	bool bUseHeld = false, bUseLatch = false;
};

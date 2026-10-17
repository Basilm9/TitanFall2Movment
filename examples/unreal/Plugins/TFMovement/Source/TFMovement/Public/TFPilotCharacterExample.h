// TFPilotCharacterExample.h
//
// Minimal pawn using UTFPilotMovementComponent: capsule (72 u stand / 47 u crouch, radius 16 u),
// first-person camera at eye height (60 u stand / 32 u crouch) and legacy input bindings.
// Axis mappings: MoveForward, MoveRight, Turn, LookUp.
// Action mappings: Jump, Crouch, Sprint, ADS, Grapple (and optional Use for ziplines).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "TFPilotCharacterExample.generated.h"

class UCameraComponent;
class UCapsuleComponent;
class UInputComponent;
class UPawnMovementComponent;
class UTFPilotMovementComponent;

UCLASS(Blueprintable)
class TFMOVEMENT_API ATFPilotCharacterExample : public APawn
{
	GENERATED_BODY()

public:
	ATFPilotCharacterExample();

	//~ APawn / AActor
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual UPawnMovementComponent* GetMovementComponent() const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pilot")
	TObjectPtr<UCapsuleComponent> Capsule;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pilot")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pilot")
	TObjectPtr<UTFPilotMovementComponent> PilotMovement;

	/** Mouse / stick look scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pilot")
	float LookScale = 1.f;

	/** Applies the wallrun camera roll [I] (client-side in the game). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pilot")
	bool bApplyViewRoll = true;

private:
	void MoveForwardAxis(float Value);
	void MoveRightAxis(float Value);
	void TurnAxis(float Value);
	void LookUpAxis(float Value);
	void JumpPressed();
	void JumpReleased();
	void CrouchPressed();
	void CrouchReleased();
	void SprintPressed();
	void SprintReleased();
	void AdsPressed();
	void AdsReleased();
	void GrapplePressed();
	void GrappleReleased();
	void UsePressed();
	void UseReleased();

	float ForwardInput = 0.f;
	float RightInput = 0.f;
};

// TFPilotCharacterExample.cpp

#include "TFPilotCharacterExample.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Engine/CollisionProfile.h"
#include "GameFramework/Controller.h"
#include "TFPilotMovementComponent.h"

ATFPilotCharacterExample::ATFPilotCharacterExample()
{
	PrimaryActorTick.bCanEverTick = true;

	// Hull: 32 x 32 x 72 u standing (47 u crouched), as a capsule of radius 16 u. 1 u = 2.54 cm.
	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(16.f * TFMove::CmPerUnit, 0.5f * 72.f * TFMove::CmPerUnit);
	Capsule->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
	RootComponent = Capsule;

	// Eye height 60 u above the feet = 60 - 36 u above the capsule centre.
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Capsule);
	Camera->SetRelativeLocation(FVector(0.0, 0.0, (60.f - 36.f) * TFMove::CmPerUnit));
	Camera->bUsePawnControlRotation = false; // rotation is set in Tick so the wallrun roll can be added
	Camera->SetFieldOfView(70.f);            // pilot fov 70 [D] (horizontal FOV mapping not verified [I])

	PilotMovement = CreateDefaultSubobject<UTFPilotMovementComponent>(TEXT("PilotMovement"));
	PilotMovement->UpdatedComponent = Capsule;

	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	AutoPossessPlayer = EAutoReceiveInput::Disabled;
}

UPawnMovementComponent* ATFPilotCharacterExample::GetMovementComponent() const
{
	return PilotMovement;
}

void ATFPilotCharacterExample::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!PilotMovement || !Capsule || !Camera)
	{
		return;
	}

	// Eye height relative to the capsule centre (capsule half height follows the stance).
	const float EyeCm = PilotMovement->GetEyeHeightCm();
	const float HalfHeightCm = Capsule->GetUnscaledCapsuleHalfHeight();
	Camera->SetRelativeLocation(FVector(0.0, 0.0, EyeCm - HalfHeightCm));

	const FRotator Control = Controller ? Controller->GetControlRotation() : GetActorRotation();
	const float Roll = bApplyViewRoll ? PilotMovement->GetViewRollDegrees() : 0.f;
	Camera->SetWorldRotation(FRotator(Control.Pitch, Control.Yaw, Roll));
}

void ATFPilotCharacterExample::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	check(PlayerInputComponent);

	PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &ATFPilotCharacterExample::MoveForwardAxis);
	PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &ATFPilotCharacterExample::MoveRightAxis);
	PlayerInputComponent->BindAxis(TEXT("Turn"), this, &ATFPilotCharacterExample::TurnAxis);
	PlayerInputComponent->BindAxis(TEXT("LookUp"), this, &ATFPilotCharacterExample::LookUpAxis);

	PlayerInputComponent->BindAction(TEXT("Jump"), IE_Pressed, this, &ATFPilotCharacterExample::JumpPressed);
	PlayerInputComponent->BindAction(TEXT("Jump"), IE_Released, this, &ATFPilotCharacterExample::JumpReleased);
	PlayerInputComponent->BindAction(TEXT("Crouch"), IE_Pressed, this, &ATFPilotCharacterExample::CrouchPressed);
	PlayerInputComponent->BindAction(TEXT("Crouch"), IE_Released, this, &ATFPilotCharacterExample::CrouchReleased);
	PlayerInputComponent->BindAction(TEXT("Sprint"), IE_Pressed, this, &ATFPilotCharacterExample::SprintPressed);
	PlayerInputComponent->BindAction(TEXT("Sprint"), IE_Released, this, &ATFPilotCharacterExample::SprintReleased);
	PlayerInputComponent->BindAction(TEXT("ADS"), IE_Pressed, this, &ATFPilotCharacterExample::AdsPressed);
	PlayerInputComponent->BindAction(TEXT("ADS"), IE_Released, this, &ATFPilotCharacterExample::AdsReleased);
	PlayerInputComponent->BindAction(TEXT("Grapple"), IE_Pressed, this, &ATFPilotCharacterExample::GrapplePressed);
	PlayerInputComponent->BindAction(TEXT("Grapple"), IE_Released, this, &ATFPilotCharacterExample::GrappleReleased);
	PlayerInputComponent->BindAction(TEXT("Use"), IE_Pressed, this, &ATFPilotCharacterExample::UsePressed);
	PlayerInputComponent->BindAction(TEXT("Use"), IE_Released, this, &ATFPilotCharacterExample::UseReleased);
}

void ATFPilotCharacterExample::MoveForwardAxis(float Value)
{
	ForwardInput = Value;
	if (PilotMovement) { PilotMovement->SetMoveInput(ForwardInput, RightInput); }
}

void ATFPilotCharacterExample::MoveRightAxis(float Value)
{
	RightInput = Value;
	if (PilotMovement) { PilotMovement->SetMoveInput(ForwardInput, RightInput); }
}

void ATFPilotCharacterExample::TurnAxis(float Value)
{
	AddControllerYawInput(Value * LookScale);
}

void ATFPilotCharacterExample::LookUpAxis(float Value)
{
	AddControllerPitchInput(Value * LookScale);
}

void ATFPilotCharacterExample::JumpPressed() { if (PilotMovement) { PilotMovement->SetJumpPressed(true); } }
void ATFPilotCharacterExample::JumpReleased() { if (PilotMovement) { PilotMovement->SetJumpPressed(false); } }
void ATFPilotCharacterExample::CrouchPressed() { if (PilotMovement) { PilotMovement->SetCrouchPressed(true); } }
void ATFPilotCharacterExample::CrouchReleased() { if (PilotMovement) { PilotMovement->SetCrouchPressed(false); } }
void ATFPilotCharacterExample::SprintPressed() { if (PilotMovement) { PilotMovement->SetSprintPressed(true); } }
void ATFPilotCharacterExample::SprintReleased() { if (PilotMovement) { PilotMovement->SetSprintPressed(false); } }
void ATFPilotCharacterExample::AdsPressed() { if (PilotMovement) { PilotMovement->SetAdsPressed(true); } }
void ATFPilotCharacterExample::AdsReleased() { if (PilotMovement) { PilotMovement->SetAdsPressed(false); } }
void ATFPilotCharacterExample::GrapplePressed() { if (PilotMovement) { PilotMovement->SetGrapplePressed(true); } }
void ATFPilotCharacterExample::GrappleReleased() { if (PilotMovement) { PilotMovement->SetGrapplePressed(false); } }
void ATFPilotCharacterExample::UsePressed() { if (PilotMovement) { PilotMovement->SetUsePressed(true); } }
void ATFPilotCharacterExample::UseReleased() { if (PilotMovement) { PilotMovement->SetUsePressed(false); } }

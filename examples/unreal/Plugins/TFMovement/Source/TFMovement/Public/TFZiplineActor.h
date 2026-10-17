// TFZiplineActor.h
//
// A zipline rope for UTFPilotMovementComponent. The rope is a USplineComponent; at BeginPlay (and
// on construction) it is sampled into a polyline of nodes in Titanfall units, with the rope height
// at each chord middle so the core can rebuild the game's per-segment sag curve A*t^2 + B*t + C.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TFMovementCore.h"
#include "TFZiplineActor.generated.h"

class USplineComponent;

UCLASS(Blueprintable)
class TFMOVEMENT_API ATFZiplineActor : public AActor
{
	GENERATED_BODY()

public:
	ATFZiplineActor();

	//~ AActor
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** The rope. Edit its points in the level; the pilot hangs one hull height below it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Zipline")
	TObjectPtr<USplineComponent> Rope;

	/**
	 * m_ziplineMoveSpeedScale: ride cap = ziplineSpeed (600) x this. [?] The shipped per-rope value is
	 * map data that the docs could not recover; 1.0 is a placeholder.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline", meta = (ClampMin = "0.01"))
	float MoveSpeedScale = 1.f;

	/** m_ziplineAutoDetachDistance of the end nodes, in cm: the rider lets go this far from the end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline", meta = (ClampMin = "0"))
	float AutoDetachDistanceCm = 0.f;

	/** Chords per spline segment (curvy splines are approximated by straight chords, like rope nodes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline", meta = (ClampMin = "1", ClampMax = "32"))
	int32 ChordsPerSegment = 4;

	/** Draws the rope polyline with debug lines at runtime (no mesh is provided by this example). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline")
	bool bDrawRopeInGame = true;

	/** Re-samples the spline (call after moving the actor or editing points at runtime). */
	UFUNCTION(BlueprintCallable, Category = "Zipline")
	void RebuildRope();

	/** Rope as seen by the movement core (Titanfall units). */
	const TFZiplineRope& GetRopeTF() const { return RopeTF; }

	/** True when any part of the rope's bounds is within RadiusCm of LocationCm. */
	bool IsNearRope(const FVector& LocationCm, float RadiusCm) const;

	/** All registered ziplines in the given world. */
	static void GetZiplinesInWorld(const UWorld* World, TArray<ATFZiplineActor*>& OutZiplines);

	/** Registered zipline with the given rope id, or nullptr. */
	static ATFZiplineActor* FindZiplineById(const UWorld* World, int32 RopeId);

private:
	TFZiplineRope RopeTF;
	FBox RopeBoundsCm = FBox(ForceInit);

	static TArray<TWeakObjectPtr<ATFZiplineActor>> Registry;
};

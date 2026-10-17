// TFZiplineActor.cpp

#include "TFZiplineActor.h"

#include "Components/SplineComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "TFPilotMovementComponent.h"

TArray<TWeakObjectPtr<ATFZiplineActor>> ATFZiplineActor::Registry;

ATFZiplineActor::ATFZiplineActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Rope = CreateDefaultSubobject<USplineComponent>(TEXT("Rope"));
	RootComponent = Rope;
	Rope->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// A default 10 m rope that drops 1 m, so a freshly placed actor is usable.
	Rope->ClearSplinePoints(false);
	Rope->AddSplinePoint(FVector(0.0, 0.0, 0.0), ESplineCoordinateSpace::Local, false);
	Rope->AddSplinePoint(FVector(1000.0, 0.0, -100.0), ESplineCoordinateSpace::Local, true);
}

void ATFZiplineActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildRope();
}

void ATFZiplineActor::BeginPlay()
{
	Super::BeginPlay();
	RebuildRope();
	Registry.AddUnique(this);
	SetActorTickEnabled(bDrawRopeInGame);
}

void ATFZiplineActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Registry.RemoveAll([this](const TWeakObjectPtr<ATFZiplineActor>& Ptr) { return !Ptr.IsValid() || Ptr.Get() == this; });
	Super::EndPlay(EndPlayReason);
}

void ATFZiplineActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bDrawRopeInGame || RopeTF.Nodes.size() < 2)
	{
		return;
	}
	for (size_t I = 0; I + 1 < RopeTF.Nodes.size(); ++I)
	{
		DrawDebugLine(GetWorld(), TFMove::ToUnreal(RopeTF.Nodes[I]), TFMove::ToUnreal(RopeTF.Nodes[I + 1]), FColor(40, 40, 40), false, -1.f, 0, 1.5f);
	}
}

void ATFZiplineActor::RebuildRope()
{
	RopeTF = TFZiplineRope();
	RopeTF.Id = static_cast<int>(GetUniqueID());
	RopeTF.MoveSpeedScale = MoveSpeedScale;
	RopeTF.AutoDetachDistance = AutoDetachDistanceCm / TFMove::CmPerUnit;
	RopeBoundsCm = FBox(ForceInit);

	if (!Rope)
	{
		return;
	}
	const int32 NumPoints = Rope->GetNumberOfSplinePoints();
	if (NumPoints < 2)
	{
		return;
	}
	const int32 NumSegments = Rope->IsClosedLoop() ? NumPoints : NumPoints - 1;
	const int32 Chords = FMath::Max(ChordsPerSegment, 1);

	auto AddNode = [this](float Distance)
	{
		const FVector P = Rope->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
		RopeTF.Nodes.push_back(TFMove::ToTF(P));
		RopeBoundsCm += P;
	};

	for (int32 Seg = 0; Seg < NumSegments; ++Seg)
	{
		const float D0 = Rope->GetDistanceAlongSplineAtSplinePoint(Seg);
		const float D1 = (Seg + 1 < NumPoints) ? Rope->GetDistanceAlongSplineAtSplinePoint(Seg + 1) : Rope->GetSplineLength();
		for (int32 C = 0; C < Chords; ++C)
		{
			const float A = FMath::Lerp(D0, D1, static_cast<float>(C) / Chords);
			const float B = FMath::Lerp(D0, D1, static_cast<float>(C + 1) / Chords);
			AddNode(A);
			const FVector Mid = Rope->GetLocationAtDistanceAlongSpline(0.5f * (A + B), ESplineCoordinateSpace::World);
			RopeTF.SegmentMidZ.push_back(TFMove::ToTF(Mid).Z);
		}
	}
	AddNode(Rope->GetSplineLength());
}

bool ATFZiplineActor::IsNearRope(const FVector& LocationCm, float RadiusCm) const
{
	if (!RopeBoundsCm.IsValid || RopeTF.Nodes.size() < 2)
	{
		return false;
	}
	return RopeBoundsCm.ComputeSquaredDistanceToPoint(LocationCm) <= FMath::Square(RadiusCm);
}

void ATFZiplineActor::GetZiplinesInWorld(const UWorld* World, TArray<ATFZiplineActor*>& OutZiplines)
{
	OutZiplines.Reset();
	for (const TWeakObjectPtr<ATFZiplineActor>& Ptr : Registry)
	{
		ATFZiplineActor* Zip = Ptr.Get();
		if (Zip && Zip->GetWorld() == World)
		{
			OutZiplines.Add(Zip);
		}
	}
}

ATFZiplineActor* ATFZiplineActor::FindZiplineById(const UWorld* World, int32 RopeId)
{
	for (const TWeakObjectPtr<ATFZiplineActor>& Ptr : Registry)
	{
		ATFZiplineActor* Zip = Ptr.Get();
		if (Zip && Zip->GetWorld() == World && Zip->GetRopeTF().Id == RopeId)
		{
			return Zip;
		}
	}
	return nullptr;
}

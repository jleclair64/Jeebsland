#include "WorldEdge.h"
#include "DeepCaves.h"
#include "WorldMap.h"
#include "Terrain.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace
{
	// Bounds of the walkable rectangle (the walls' inner faces)
	FBox2D Inner()
	{
		const float I = AWorldEdge::Inset;
		return FBox2D(FVector2D(JeebWorld::MinX + I, JeebWorld::MinY + I), FVector2D(JeebWorld::MaxX - I, JeebWorld::MaxY - I));
	}
	constexpr float UnderGround = 10000.f;   // 100 m under the ground counts as fallen through (nothing in the world is built that deep)
}

AWorldEdge::AWorldEdge()
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false);   // every machine builds its own from the same numbers
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	const FBox2D B = Inner();
	const FVector2D C = B.GetCenter(), E = B.GetExtent();
	struct FWall { const TCHAR* Name; FVector At, Half; };
	const float T = Thick * 0.5f;
	const FWall Walls[] = {
		{ TEXT("North"), FVector(B.Max.X + T, C.Y, 0.f), FVector(T, E.Y + Thick, HalfHeight) },
		{ TEXT("South"), FVector(B.Min.X - T, C.Y, 0.f), FVector(T, E.Y + Thick, HalfHeight) },
		{ TEXT("East"),  FVector(C.X, B.Max.Y + T, 0.f), FVector(E.X + Thick, T, HalfHeight) },
		{ TEXT("West"),  FVector(C.X, B.Min.Y - T, 0.f), FVector(E.X + Thick, T, HalfHeight) },
	};
	for (const FWall& W : Walls)
	{
		UBoxComponent* Box = CreateDefaultSubobject<UBoxComponent>(W.Name);
		Box->SetupAttachment(Root);
		Box->SetWorldLocation(W.At);
		Box->SetBoxExtent(W.Half);
		Box->SetHiddenInGame(true);
		Box->SetCanEverAffectNavigation(false);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Ignore);
		Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	}
}

void AWorldEdge::BeginPlay()
{
	Super::BeginPlay();
	const FBox2D B = Inner();
	UE_LOG(LogTemp, Log, TEXT("JEEBEDGE walls inner X %.0f..%.0f Y %.0f..%.0f (map edge X %.0f..%.0f Y %.0f..%.0f)"), B.Min.X, B.Max.X, B.Min.Y, B.Max.Y,
		JeebWorld::MinX, JeebWorld::MaxX, JeebWorld::MinY, JeebWorld::MaxY);
	if (HasAuthority() && GetWorld())
		GetWorld()->GetTimerManager().SetTimer(CheckTimer, this, &AWorldEdge::CheckPawns, 1.f, true, 1.f);
}

bool AWorldEdge::IsOutOfBounds(const FVector& P)
{
	const FBox2D B = Inner();
	if (P.X < B.Min.X || P.X > B.Max.X || P.Y < B.Min.Y || P.Y > B.Max.Y) return true;
	if (JeebDeep::Field(P) < 500.f) return false;   // (in the Deeps, the cave maze under Silver Point's mountain, up to 150 m under its slopes, 2026-11-28)
	return P.Z < JeebTerrain::HeightAt(P.X, P.Y) - UnderGround;
}

FVector AWorldEdge::SafeSpot(const FVector& P)
{
	const FBox2D B = Inner();
	const float X = FMath::Clamp(P.X, B.Min.X + 300.f, B.Max.X - 300.f), Y = FMath::Clamp(P.Y, B.Min.Y + 300.f, B.Max.Y - 300.f);
	// (above the ground, and above the sea where the ground is under water: the knight swims from there)
	return FVector(X, Y, FMath::Max(JeebTerrain::HeightAt(X, Y), JeebWorld::SeaLevel) + 150.f);
}

void AWorldEdge::CheckPawns()
{
	UWorld* World = GetWorld();
	if (!World) return;
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Pawn = *It;
		if (!Pawn || Pawn->IsPendingKillPending()) continue;
		const FVector P = Pawn->GetActorLocation();
		if (!IsOutOfBounds(P)) continue;
		const FVector To = SafeSpot(P);
		UE_LOG(LogTemp, Warning, TEXT("JEEBEDGE %s was out of bounds at (%.0f, %.0f, %.0f); put back at (%.0f, %.0f, %.0f)"), *Pawn->GetName(), P.X, P.Y, P.Z, To.X, To.Y, To.Z);
		Pawn->TeleportTo(To, Pawn->GetActorRotation(), false, true);
		if (ACharacter* Ch = Cast<ACharacter>(Pawn))
			if (UCharacterMovementComponent* Mv = Ch->GetCharacterMovement()) Mv->StopMovementImmediately();
	}
}

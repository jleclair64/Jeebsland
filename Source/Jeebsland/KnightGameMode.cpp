#include "KnightGameMode.h"
#include "KnightGameState.h"
#include "KnightCharacter.h"
#include "KnightHUD.h"
#include "Terrain.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"

AKnightGameMode::AKnightGameMode()
{
	DefaultPawnClass = AKnightCharacter::StaticClass();
	HUDClass = AKnightHUD::StaticClass();
	GameStateClass = AKnightGameState::StaticClass();
}

APawn* AKnightGameMode::SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot)
{
	// The first player stands at the old starting point; the next ones on a 2 m ring round it, 60 degrees apart
	FVector2D At(0.f, 0.f);
	if (SpawnCount > 0)
	{
		const float A = FMath::DegreesToRadians(60.f * (SpawnCount - 1));
		At = FVector2D(FMath::Cos(A), FMath::Sin(A)) * 200.f;
	}
	++SpawnCount;

	FActorSpawnParameters Params;
	Params.Instigator = GetInstigator();
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;   // players are never saved into the map
	const FVector Loc(At.X, At.Y, JeebTerrain::HeightAt(At.X, At.Y) + 130.f);
	return GetWorld()->SpawnActor<APawn>(GetDefaultPawnClassForController(NewPlayer), Loc, FRotator::ZeroRotator, Params);
}

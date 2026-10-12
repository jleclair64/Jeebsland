#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "KnightGameState.generated.h"

// Builds the world. The game state exists on the server AND on every client (the game mode lives only on the server), so
// each machine builds its own copy of the procedural world here, the same way every time (fixed random seeds).
// Rule for multiplayer: anything built here is LOCAL to each machine. Things players change or fight (rocks, trees, enemies,
// drops, the horse) must later become server-owned replicated actors; for now each machine has its own copy of those.
UCLASS()
class AKnightGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AKnightGameState();

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	void BuildWorld();

	TWeakObjectPtr<class AExponentialHeightFog> FogActor;
	float FogNow = 0.012f;

	// Test screenshot mode (-JeebShot, single player only)
	bool bShotMode = false;
	bool bShotTaken = false;
	float ShotClock = 0.f;
	FRotator ShotRot = FRotator::ZeroRotator;
	FString ShotName = TEXT("shot");
};

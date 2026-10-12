#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "KnightGameMode.generated.h"

// The server's rules: plays as the knight, and decides where each player appears. Lives ONLY on the server (or in a
// single-player game); the world itself is built by AKnightGameState, which every machine has.
UCLASS()
class AKnightGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AKnightGameMode();

protected:
	// Every player starts on the meadow by the cottage, spread round a small ring so two never land on each other
	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;

private:
	int32 SpawnCount = 0;
};

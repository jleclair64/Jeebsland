#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "KnightGameMode.generated.h"

// Plays as the knight and spawns the meadow automatically.
UCLASS()
class AKnightGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AKnightGameMode();

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	TWeakObjectPtr<class AExponentialHeightFog> FogActor;
	float FogNow = 0.012f;

	// Test screenshot mode (-JeebShot)
	bool bShotMode = false;
	bool bShotTaken = false;
	float ShotClock = 0.f;
	FRotator ShotRot = FRotator::ZeroRotator;
	FString ShotName = TEXT("shot");
};

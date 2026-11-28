#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PvpSigns.generated.h"

// The warning signs along the edge of every PvP zone (owner, 2026-11-23; PvpZones.h, Content/Data/PvpZones.csv): since 2026-11-27 a painted
// wooden board on two posts, crossed swords on a red diamond and the zone name on both faces (was: a dark post with a red
// board and a white cross), 3 m outside the line, facing out, a sign every SignEvery metres where the edge lies inside the map and on dry
// land. Players' machines only (they have no collision: nobody trips on them), the same on every machine (no random numbers).
UCLASS()
class APvpSigns : public AActor
{
	GENERATED_BODY()

public:
	APvpSigns();

#if !UE_BUILD_SHIPPING
	virtual void Tick(float DeltaSeconds) override;   // (only with -JeebPvpTest: PvpTest.cpp)
#endif

protected:
	virtual void BeginPlay() override;
};

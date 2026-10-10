#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Terrain.h"
#include "Castle.generated.h"

// The castle: a moat, a bridge and gate, a square wall with corner towers and a walkway (reached by ladders in the
// courtyard), and in the middle the spire tower: a hollow keep with a spiral staircase up to a roofed terrace and beacon.
UCLASS()
class ACastle : public AActor
{
	GENERATED_BODY()

public:
	ACastle();
	void Build();
};

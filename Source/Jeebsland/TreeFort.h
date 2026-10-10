#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Terrain.h"
#include "TreeFort.generated.h"

// Wooden terraces built around the trunk of a great redwood: four railed decks, wider at the bottom, joined by rope
// ladders, with a roof and lanterns on the top one. The ladders are climbed like the village ladders (see Terrain.h FortLadders).
UCLASS()
class AFort : public AActor
{
	GENERATED_BODY()

public:
	AFort();
	void Build(const JeebTerrain::FFort& Def);
};

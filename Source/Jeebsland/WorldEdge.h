#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldEdge.generated.h"

// Invisible walls round the playable world (owner, 2026-11-21: "so the knight can't fall off the map"). Four tall boxes just
// inside JeebWorld::MinX/MaxX/MinY/MaxY (WorldMap.h). They block pawns only (knight, troll, horse, animals); every other
// channel (camera, visibility, projectiles, traces) passes through. Built from the fixed world bounds, so identical on the
// server and every client. On the server a once-a-second check puts any pawn that still ends up outside the walls, or far
// under the ground, back on the nearest safe spot inside. Log: JEEBEDGE.
UCLASS()
class AWorldEdge : public AActor
{
	GENERATED_BODY()

public:
	AWorldEdge();

	static constexpr float Inset = 1500.f;     // the walls' inner face, this far inside the map's edge (cm)
	static constexpr float Thick = 1000.f;     // wall thickness (cm), so a fast horse can't tunnel through
	static constexpr float HalfHeight = 40000.f;   // walls reach 400 m up and 400 m down from the sea (higher than any peak, jump or leap)

	// The nearest safe spot for a pawn at P: inside the walls, above the ground (and above the sea).
	static FVector SafeSpot(const FVector& P);
	static bool IsOutOfBounds(const FVector& P);

protected:
	virtual void BeginPlay() override;

private:
	void CheckPawns();
	FTimerHandle CheckTimer;
};

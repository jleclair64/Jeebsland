#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OpenWorldActor.generated.h"

class UInstancedStaticMeshComponent;

// The open world's two visible pieces that are not ground, roads or trees (OpenWorld.h has those):
//  - a placeholder house at each town still to be built (JeebOpen::Sites: Silver Point, Anchorage, Birch Grove, Palmer Square), the
//    TownLab house with its solid shape, on every machine (the server decides where players walk); log JEEBOPEN houses hash;
//  - the snowfall (owner, 2026-11-21: "replace the falling leave animation with a falling snow animation that's sometimes clear and
//    sometimes constantly snowing (not intermittent like the leaves). I want there to be more snow the higher up you go"): flakes in a
//    box that follows the camera, players' machines only. Whether it snows comes from the shared clock (JeebOpen::Snowing), so every
//    player sees the same weather; how many flakes fall grows with the camera's height. Each flake carries its previous-frame
//    transform, so TSR has motion vectors and nothing smears.
UCLASS()
class AOpenWorld : public AActor
{
	GENERATED_BODY()

public:
	AOpenWorld();
	virtual void Tick(float DeltaSeconds) override;

	static float SnowNow;   // 0 clear .. 1 snowing: the weather this frame (for anything else that wants to follow it)

protected:
	virtual void BeginPlay() override;

private:
	void BuildHouses();
	UPROPERTY() UInstancedStaticMeshComponent* Flakes = nullptr;
	struct FFlake { FVector Pos; float Fall, Phase, Size, Spin; bool bUnderRoof; };
	TArray<FFlake> Flake;
	TArray<FTransform> NewT, PrevT;
	TArray<uint8> Seen;       // (each flake drawn last frame: its previous transform is real)
	FRandomStream Rand;
	float Time = 0.f;
	int32 Shown = 0;
};

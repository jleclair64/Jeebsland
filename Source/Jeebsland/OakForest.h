#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OakForest.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

// Oak Hill's trees and the forest round it (owner, 2026-11-16: "replace all the trees in oak hill that we created with trees we've added
// from the packs ... a mix of colors ... roughly a 2-1 ratio of fat to skinny trees", and "fill in the trees for the 500 meters surrounding
// oak hill with dense forest using combinations of all trees (including the spruce) ... boulders, roads, and other natural elements").
//
//  - The town: DZ cork oaks (fat) and columnar aspens (skinny) at 2:1, spruces where a conifer suits, at FillTrees.csv's marked spots and
//    in the gaps between the plots (the spots the old woods' oaks and pines used), each crown kept off the buildings.
//  - The forest: everything within JeebOakForest::Reach of the plaza except the town and its fringe farmland, the original square round
//    the start (Blackrock, which keeps its own woods), the rivers and their banks, steep ground and the roads: cork oaks, aspens and the
//    Spruce Forest's spruces in stands and glades, thicker in hollows, with bushes, saplings, fallen logs, stumps and boulders.
//  - Three dirt roads (JeebOakForest::RoadLines) from the town's road ends out through the forest: north toward Silver Point, east toward
//    Anchorage, south-east to the river's east branch (where a ford to Birch Grove would go). They are ordinary paths (JeebTerrain::PathDefs).
// Colours: the four DZ colours of the sample groves (jeeb.Trees.DzGreen/DzRed/DzOrange/DzYellow, the LOOK tab's TREES sliders), by
// neighbourhood. Every tree is one instance of a shared instanced mesh per kind and colour; the trunks, logs, stumps and rocks are hidden
// instanced collision shapes, built on every machine (the server too: it decides where players walk). Deterministic from fixed seeds:
// log JEEBFOREST hash to compare machines. Not replicated.
namespace JeebOakForest
{
	constexpr float Reach = 50000.f;   // cm from the plaza: the forest's outer edge
	// The forest roads' centrelines (every 60 cm), worked out once from the land; JeebTerrain::PathDefs makes them paths. Never calls PathDefs.
	const TArray<TArray<FVector2D>>& RoadLines();
}

UCLASS()
class AOakForest : public AActor
{
	GENERATED_BODY()

public:
	AOakForest();
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() TArray<UInstancedStaticMeshComponent*> Drawn;    // (visual sets: one per kind and colour)
	TArray<float> DrawnReach;                                    // (each one's own draw distance, cm, before jeeb.Forest.Reach)
	TArray<bool> DrawnIsTree;
	TArray<bool> DrawnShadow;                                    // (whether that set casts its own shadow, before jeeb.Forest.Shadows: not the trees)
	UPROPERTY() TArray<UInstancedStaticMeshComponent*> Solids;   // (hidden collision: trunks, logs, stumps, rocks)
	// The trees' shadows (2026-11-16: thousands of leafy trees in every shadow cascade cost 7 ms): the drawn trees cast none; each tree set
	// has a hidden twin that casts the shadows of just the trees within jeeb.Forest.ShadowReach of the camera, refilled as the camera moves
	UPROPERTY() TArray<UInstancedStaticMeshComponent*> ShadowSets;
	TArray<TArray<FTransform>> ShadowAll;                        // (each tree set's instances)
	TMap<FIntPoint, TArray<FIntPoint>> ShadowCells;              // (20 m cells: set and instance)
	TArray<uint32> ShadowHash;                                   // (what each twin holds now)
	FVector2D ShadowAt = FVector2D(1.0e9f, 1.0e9f);
	// Past jeeb.Forest.ShadowReach (2026-11-16, owner: far trees and the ground under them looked too bright, then went dark as he came
	// closer): a cheap hidden stand-in per tree (a plain crown and trunk, about a hundred triangles) casts its shadow out to
	// jeeb.Forest.FarShadeReach, so the far forest is shaded like the near one at every time of day
	UPROPERTY() TArray<UInstancedStaticMeshComponent*> ShadeSets;   // (one per tree kind)
	TArray<int32> ShadeOfTwin;                                   // (each shadow twin's stand-in set)
	TArray<int32> ShadeKind;                                     // (each stand-in set's tree kind)
	TArray<FBox> ShadeBounds;                                    // (and that tree mesh's bounds at scale 1)
	uint32 ShadeHash = 0u;
	float ShadeSizeNow = -1.f;
	void BuildShadeMeshes();
	void UpdateShadows(bool bForce);
	void ApplyReach();
	friend struct FOakForestBuild;
};

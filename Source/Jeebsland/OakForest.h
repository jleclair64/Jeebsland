#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChoppableTree.h"
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
//    the start (the old village site, which keeps its own woods), the rivers and their banks, steep ground and the roads: cork oaks, aspens and the
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

class AOakForest;
struct FNodeCharge;

// One of the forest's (and Oak Hill's) pack trees as a choppable node (owner, 2026-11-17: "Can we make the trees choppable?"). It is only
// the gameplay part, like the old village site's AChoppableTree but without its trunk capsule (the forest's hidden trunk solids block already): the
// server runs the chop as for any tree; when its charges run out every machine fells it (AOakForest::SetFelled) and it grows back.
// Cork oaks and aspens give oak logs, spruces pine logs. Spawned on every machine at the tree's foot, in the same order: nodes are matched
// online by where they stand (AKnightGameState::NodeCharges).
UCLASS()
class AOakTreeNode : public AChoppableTree
{
	GENERATED_BODY()

public:
	AOakTreeNode(const FObjectInitializer& ObjectInitializer);
	TWeakObjectPtr<AOakForest> Forest;
	int32 Index = INDEX_NONE;
	// Removable when nobody is working it and it is whole (a felled tree's node holds its regrow timer, so it stays)
	bool CanRemove() const { return !bDepleted && !IsBeingGathered(); }

protected:
	virtual void OnChargesChanged(int32 Remaining, int32 Max) override;
	bool bDepleted = false;
};

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
	// The trees' shadows (2026-11-16: thousands of leafy trees in every shadow cascade cost 7 ms): the drawn trees cast none. A cheap hidden
	// stand-in per tree (a plain see-through crown and a trunk, about a hundred triangles, in M_TreeShade) casts its shadow instead, for
	// the trees within jeeb.Forest.FarShadeReach of the camera, refilled as the camera moves. (Until 2026-11-21 the trees within 20 m
	// cast their own leafy shadows from hidden twins, with a faded hand-over to the stand-ins; the owner had them taken out)
	TArray<TArray<FTransform>> ShadowAll;                        // (each tree set's instances)
	TMap<FIntPoint, TArray<FIntPoint>> ShadowCells;              // (20 m cells: tree set and instance)
	UPROPERTY() TArray<UInstancedStaticMeshComponent*> ShadeSets;   // (one per tree kind)
	TArray<int32> ShadeOfTwin;                                   // (each tree set's stand-in set)
	TArray<int32> ShadeKind;                                     // (each stand-in set's tree kind)
	TArray<FBox> ShadeBounds;                                    // (and that tree mesh's bounds at scale 1)
	uint32 ShadeHash = 0u;
	float ShadeSizeNow = -1.f;
	FVector2D ShadeAt = FVector2D(1.0e9f, 1.0e9f);
	UPROPERTY() UMaterialInstanceDynamic* ShadeMid = nullptr;    // (the stand-ins' material: jeeb.Forest.ShadeDensity, jeeb.Forest.Dapple)
	float DensityNow = -1.f, DappleNow = -1.f;
	bool CameraNow(FVector& Out) const;
	void BuildShadeMeshes();
	void UpdateShadows(bool bForce);
	void ApplyReach();
	friend struct FOakForestBuild;

public:
	// A choppable tree chopped down (bFelled) or grown back: its drawn tree and shadow go, its trunk solid drops to a stump's height and a
	// stump is drawn in its place, and back again. Every machine (the server: the solid only).
	void SetFelled(int32 Index, bool bFelled);
	// A player's machine: the server's list of felled and part-used nodes (AKnightGameState::OnRep_Nodes) puts every tree it names in its
	// felled state and grows back the rest, with or without a node actor (they are lazy: a late joiner has none yet)
	void SyncNodeCharges(const TArray<FNodeCharge>& List);
	static AOakForest* Instance(UWorld* World);
	// (server, eagles) the top of a whole tree within Radius of At, picked with Rand; false if none
	bool RandomTreeTop(const FVector2D& At, float Radius, FRandomStream& Rand, FVector& Out) const;

private:
	struct FFellable
	{
		int32 Key = -1, KeyIndex = -1;     // (its visual set's key, kind * 4 + colour, and its place in it, while building)
		int32 Drawn = -1, Shadow = -1;     // (its drawn set and its place in ShadowAll; -1 on the server)
		int32 Trunk = -1;                  // (its instance in the trunk solids)
		FTransform Tree, TrunkSolid;
		float Radius = 20.f, Top = 0.f;      // (Top: the drawn tree's height above its foot, cm)
		int32 Stump = -1;                  // (its instance in FelledStumps, once felled)
		bool bFelled = false;
		uint8 Kind = 0;                   // (its tree kind, for its node)
	};
	TArray<FFellable> Fellable;
	UPROPERTY() UInstancedStaticMeshComponent* TrunkSolids = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* FelledStumps = nullptr;
	FBox StumpBounds = FBox(ForceInit);
	// The choppable trees' nodes, lazily: a node actor exists only while a player is near its tree (UpdateNodes, every tick), or while it is
	// felled (on the server: it holds the regrow timer; the clients keep the felled look in Fellable and FelledSet)
	TMap<FIntPoint, TArray<int32>> NodeCells;                    // (20 m cells: Fellable indices)
	TMap<int32, TWeakObjectPtr<AOakTreeNode>> Nodes;             // (the live ones)
	TSet<int32> FelledSet;
	bool bLazyNodes = false;
	int32 NodesSpawned = 0, NodesRemoved = 0, NodesLogged = -1;
	double NodesLogAt = 0.0;
	AOakTreeNode* SpawnNode(int32 Index);
	void UpdateNodes();
};

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChoppableTree.h"
#include "Forest.generated.h"

class UInstancedStaticMeshComponent;

// All the woods. Every trunk, branch, leaf and needle tier is an instance of a handful of shared
// meshes (so thousands of trees stay cheap); each tree also gets a small AChoppableTree for gameplay.
UCLASS()
class AForest : public AActor
{
	GENERATED_BODY()

public:
	AForest();
	// Leaves cast shadows (off on the Lowest graphics setting: swaying leaf shadows flicker there)
	void SetLeafShadows(bool bOn);

	// Plants one tree (random shape comes from R). Used by the woods and by the game mode.
	AChoppableTree* AddTree(ETreeKind Kind, const FVector& Pos, float Scale, FRandomStream& R);
	bool bNoWoods = false;   // (MoveLab: only what AddTree is asked for; set before it begins play)

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() UInstancedStaticMeshComponent* Trunk = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* RedTrunk = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* RedTaper = nullptr;      // redwood trunks: wide flared foot, narrowing to almost nothing at the top
	UPROPERTY() UInstancedStaticMeshComponent* RedFortTrunk = nullptr;  // the fort redwoods: a gentler taper, so the decks still fit round them
	UPROPERTY() TArray<UInstancedStaticMeshComponent*> NeedleFill;      // the in-between needle boughs (pine A, pine B, redwood): no shadows, to keep shadows cheap
	UPROPERTY() UInstancedStaticMeshComponent* Branch = nullptr;
	UPROPERTY() TArray<UInstancedStaticMeshComponent*> Leaf;
	UPROPERTY() TArray<UInstancedStaticMeshComponent*> Pine;
	UPROPERTY() UInstancedStaticMeshComponent* RedCone = nullptr;
	// Far stand-ins (M_TreeFar): a handful of soft leafy puffs per broad-leaf crown and one tiered cone per pine or redwood, shown
	// where the leaf cards and needle boughs fade out, so the woods stay full to the horizon. 0-4 the five leaf colours, 5-6 the
	// two pine greens, 7 the redwood green. No shadows. (Not named Leaf or Pine: the world map counts those as crowns.)
	UPROPERTY() TArray<UInstancedStaticMeshComponent*> FarCrown;
	bool bFarCrowns = false;
	// Where the near crowns hand over to the far ones (cm from the camera): x = fade starts, y = fade ends. Both sides dither
	// against the same screen noise, so each pixel shows one or the other (FadeStart / FadeEnd in M_TreeLeaf, M_PineNeedle, M_TreeFar).
	FVector2D BandOak = FVector2D(7000.f, 9000.f), BandPine = FVector2D(9000.f, 12000.f), BandRed = FVector2D(20000.f, 25000.f);
	int32 FarCount = 0, NearLeafCount = 0;   // (log JEEBTREES)
	double BuildSeconds = 0.0;   // (time spent planting, all trees)
	int32 TreeCount = 0;

			bool bBark = false;        // trunks and branches use the shaped bark meshes (M_BarkTint)
	UPROPERTY() TArray<UInstancedStaticMeshComponent*> TrunkVar;   // four trunk shapes
	UPROPERTY() TArray<UInstancedStaticMeshComponent*> BranchVar;  // branches in the same four bark colours, so they match their trunk
	bool bLeafCards = false;
	bool bNeedleCards = false; // pines and redwoods wear painted needle boughs (M_PineNeedle) rather than stacked cones   // leaves are painted cards (M_TreeLeaf) rather than plain diamonds

	UInstancedStaticMeshComponent* MakeISM(const TCHAR* Name, UStaticMesh* Mesh, bool bShadow, int32 CullStart, int32 CullEnd, bool bHierarchical = false);
	void Tint(UInstancedStaticMeshComponent* C, const FLinearColor& Color);

	UPROPERTY(EditAnywhere, Category = "Forest") int32 MaxTrees = 1500;
	UPROPERTY(EditAnywhere, Category = "Forest") float CellSize = 300.f;
	UPROPERTY(EditAnywhere, Category = "Forest") float FieldRadius = 9000.f;
};

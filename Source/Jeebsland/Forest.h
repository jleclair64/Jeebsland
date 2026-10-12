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

	// Plants one tree (random shape comes from R). Used by the woods and by the game mode.
	AChoppableTree* AddTree(ETreeKind Kind, const FVector& Pos, float Scale, FRandomStream& R);

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() UInstancedStaticMeshComponent* Trunk = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* RedTrunk = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* Branch = nullptr;
	UPROPERTY() TArray<UInstancedStaticMeshComponent*> Leaf;
	UPROPERTY() TArray<UInstancedStaticMeshComponent*> Pine;
	UPROPERTY() UInstancedStaticMeshComponent* RedCone = nullptr;

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

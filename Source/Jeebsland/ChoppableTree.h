#pragma once

#include "CoreMinimal.h"
#include "ResourceNode.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "ChoppableTree.generated.h"

class UCapsuleComponent;

enum class ETreeKind : uint8 { Oak, Pine, Redwood };

// Which instances in the forest's shared meshes make up one tree
struct FTreePart
{
	TWeakObjectPtr<UInstancedStaticMeshComponent> Comp;
	int32 Start = 0;
	int32 Count = 0;
	bool bTrunk = false; // trunks shrink to a stump; everything else disappears
};

// A tree you can chop for logs. The looks (trunk, branches, leaves) live in the shared AForest meshes;
// this actor is the gameplay part: collision, XP, logs, and hiding its instances while it regrows.
UCLASS()
class AChoppableTree : public AResourceNode
{
	GENERATED_BODY()

public:
	AChoppableTree();

	// Called by the forest right after spawning
	void Setup(ETreeKind InKind, float Radius, float Height);

	ETreeKind GetKind() const { return Kind; }
	float GetTreeHeight() const { return TreeHeight; }

	TArray<FTreePart> Parts;

protected:
	virtual void OnChargesChanged(int32 Remaining, int32 Max) override;

private:
	UPROPERTY() USceneComponent* TreeRoot = nullptr;
	UPROPERTY() UCapsuleComponent* Capsule = nullptr;

	ETreeKind Kind = ETreeKind::Oak;
	float TreeHeight = 0.f;
	bool bHidden = false;
	TArray<FTransform> Saved;
};

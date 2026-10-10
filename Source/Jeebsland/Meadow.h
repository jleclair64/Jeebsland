#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Meadow.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInstanceDynamic;

// Storybook grass over the whole land. Tufts grow in square cells around the player (denser close by, thinner further out)
// and the cells are built and dropped as you walk. Bushes and flowers are placed once across the land.
UCLASS()
class JEEBSLAND_API AMeadow : public AActor
{
	GENERATED_BODY()

public:
	AMeadow();
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	struct FCell { UInstancedStaticMeshComponent* Comp = nullptr; int32 Tier = 0; };
	TMap<FIntPoint, FCell> Cells;

	UPROPERTY() TArray<UMaterialInstanceDynamic*> BladeMats;   // root, middle, tip
	UPROPERTY() UStaticMesh* NearMesh = nullptr;
	UPROPERTY() UStaticMesh* FarMesh = nullptr;

		// A square is built a little each frame (a time budget) and only appears when it is finished, so walking never hitches
	bool bJob = false;
	FIntPoint JobId;
	int32 JobTier = 0, JobCount = 0, JobDone = 0;
	bool bJobNearRoad = true;
	FRandomStream JobRand;
	TArray<FTransform> JobXf;
	void StartJob(const FIntPoint& Id, int32 Tier);
	void StepJob(double BudgetSeconds);
	void FinishJob();
	void DropCell(const FIntPoint& Id);
	void PlaceBushesAndFlowers();
};

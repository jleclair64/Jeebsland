#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Meadow.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInstanceDynamic;

// Storybook grass over the whole land (the whole world's grassy ground since 2026-11-12: none on rock, mud or sand, shorter where
// trampled, GroundMask.h). Tufts grow in square cells around the player (denser close by, thinner further out)
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
	// The ground's mask over the square being built, on a 5 x 5 grid (GroundMask.h), worked out once per square: how much grass
	// grows (none on rock, mud or sand) and how trampled it is round buildings; each tuft reads it between the grid points
	float JobAmount[25] = {}, JobWear[25] = {};
	bool bJobExactWear = false;   // (near a building: each tuft checks the doorway wear itself)
	void SampleJob(float X, float Y, float& OutAmount, float& OutWear) const;
	void StartJob(const FIntPoint& Id, int32 Tier);
	void StepJob(double BudgetSeconds);
	void FinishJob();
	void DropCell(const FIntPoint& Id);
	void PlaceBushesAndFlowers();
	void Prewarm();
	bool bPrewarmed = false;
};

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Meadow.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInstanceDynamic;

// Storybook grass over the whole land (the whole world's grassy ground since 2026-11-12: none on rock, mud or sand, shorter where
// trampled, GroundMask.h). Tufts grow in square cells around the player (denser close by, thinner further out)
// and the cells are built and dropped as you walk. (Its leaf-card bushes went 2026-11-18: ABushes, Bushes.h.)
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
	struct FCell { UInstancedStaticMeshComponent* Comp = nullptr; UInstancedStaticMeshComponent* Flowers = nullptr; TArray<UInstancedStaticMeshComponent*> Packed; int32 Tier = 0; };
	TMap<FIntPoint, FCell> Cells;

	// Realistic grass from the owner's Fab packs (2026-11-15, owner: "lean towards realism with the grass"): jeeb.Grass.Pack 0 is our
	// painted cards, 1 the Grass Library with Meadow Flowers, 2 Open World Foliage. Where the grass grows, how dense, the long patches
	// and the field colour stay ours; only the near tufts' meshes and material change. The meshes are listed in Content/Data/GrassPacks.csv.
	struct FKind { UStaticMesh* Mesh = nullptr; UMaterialInstanceDynamic* Mid = nullptr; int32 Role = 0; float Weight = 1.f, Height = 1.f, Width = 1.f, NaturalH = 50.f; };
	TArray<FKind> Kinds;                 // the grown pack's kinds (Role 0 short field grass, 1 long-patch grass, 2 flowers)
	UPROPERTY() TArray<UObject*> KeepAlive;
	int32 Pack = 0;                      // the pack the grass now standing was grown with
	float PackChance = 0.f;              // the share of tuft spots that get a pack clump
	float PackFadeStart = 1800.f, PackFadeEnd = 2600.f;
	bool bFarCards = true;               // our painted cards still carry the meadow past the pack's reach
	void LoadPack(int32 P);
	void RegrowAround(const FVector2D& Me);
	FVector2D LastMe = FVector2D(1e9f, 1e9f);
	TArray<TArray<FTransform>> JobKindXf;

	UPROPERTY() TArray<UMaterialInstanceDynamic*> BladeMats;   // root, middle, tip
	UPROPERTY() UStaticMesh* NearMesh = nullptr;
	UPROPERTY() UStaticMesh* FarMesh = nullptr;
	UPROPERTY() UStaticMesh* FlowerMesh = nullptr;
	UPROPERTY() UStaticMesh* HeatherMesh = nullptr;   // (2026-11-25: the moor's heather clumps, MapRework.h)
	UPROPERTY() UMaterialInstanceDynamic* GrassMid = nullptr;   // M_Grass with the low sun's Dusk set each frame (cooler blades at the golden sun)

		// A square is built a little each frame (a time budget) and only appears when it is finished, so walking never hitches
	bool bJob = false;
	FIntPoint JobId;
	int32 JobTier = 0, JobCount = 0, JobDone = 0;
	bool bJobNearRoad = true;
	FRandomStream JobRand;
	TArray<FTransform> JobXf;
	TArray<float> JobTall;            // each tuft's long-grass share (0..1, per-instance data 0: M_Grass warms its tips)
	TArray<FTransform> JobFlowerXf;   // the flowers in this square (tiers 0 and 1)
	TArray<FLinearColor> JobFlowerCol;
	TArray<FTransform> JobHeatherXf;   // the heather in this square (the moor)
	TArray<FLinearColor> JobHeatherCol;
	// The ground's mask over the square being built, on a 5 x 5 grid (GroundMask.h), worked out once per square: how much grass
	// grows (none on rock, mud or sand) and how trampled it is round buildings; each tuft reads it between the grid points
	float JobAmount[25] = {}, JobWear[25] = {};
	bool bJobExactWear = false;   // (near a building: each tuft checks the doorway wear itself)
	void SampleJob(float X, float Y, float& OutAmount, float& OutWear) const;
	void StartJob(const FIntPoint& Id, int32 Tier);
	void StepJob(double BudgetSeconds);
	void FinishJob();
	void DropCell(const FIntPoint& Id);
	void Prewarm();
	bool bPrewarmed = false;
	// One way everywhere since 2026-11-14 (the rebuilt Oak Hill's, first tried in TownLab): dense grass only within 40 m, in 15 m squares (about
	// 25 draws instead of about 500), thinning past 15 m and sinking into the painted ground by 39 m (M_Grass FadeStart..FadeEnd)
	// (2026-11-21, owner: the grass "growing in" ahead of him was too obvious) Six steps of detail instead of three, each square's clumps
	// a fixed set per spot so a nearer step only adds clumps among the ones already there (and widens them a little): it fills in by
	// degrees instead of a square swapping to a new, twice as dense crop. Edges in cm from the square's middle (MeadowTiers in Meadow.cpp).
	float CellSize = 1500.f, Reach = 5000.f;
	TMap<uint64, float> JobGround;   // (the drawn ground's grid-point heights in the square being built)
	float GroundAt(float X, float Y);
	bool GrowsHere(float X, float Y, float PadCm = 0.f) const;
	int32 TierFor(float Dist) const;
	float Clearance(float X, float Y) const;
	float WearAt(float X, float Y, float PathClearanceCm) const;
	bool NoCover(float X, float Y) const;
	float Nearness(float X, float Y) const;
};

// (test only, -GrassLineup in MoveLab: every grass and flower type of the Fab packs in rows, for choosing them; Meadow.cpp)
void JeebGrassLineup(UWorld* World);

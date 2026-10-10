#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AutumnDecor.generated.h"

class UInstancedStaticMeshComponent;

// The Autumn Realm's finishing touches: carved pumpkins, hay bales, scarecrows, lamp posts, a graveyard,
// candle string lights in the village, fallen leaves and leaf piles under the trees, and a few leaves drifting down around the player.
// Everything is drawn with shared instanced meshes, so it stays cheap.
UCLASS()
class AAutumnDecor : public AActor
{
	GENERATED_BODY()

public:
	AAutumnDecor();
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() UInstancedStaticMeshComponent* PumpkinA = nullptr;   // orange bodies
	UPROPERTY() UInstancedStaticMeshComponent* PumpkinB = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* PumpkinC = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* Stem = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* Glow = nullptr;       // glowing carved faces and lantern glass (cubes)
	UPROPERTY() UInstancedStaticMeshComponent* GlowCone = nullptr;   // glowing triangles (eyes, nose) and flames
	UPROPERTY() UInstancedStaticMeshComponent* Wood = nullptr;       // posts (cylinders)
	UPROPERTY() UInstancedStaticMeshComponent* WoodCone = nullptr;   // lantern caps, witch hats
	UPROPERTY() UInstancedStaticMeshComponent* Cloth = nullptr;      // scarecrow shirts
	UPROPERTY() UInstancedStaticMeshComponent* HayRoll = nullptr;    // round bales (cylinders)
	UPROPERTY() UInstancedStaticMeshComponent* HayBox = nullptr;     // straw tufts (cubes)
	UPROPERTY() UInstancedStaticMeshComponent* StoneBox = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* StoneCyl = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* CandleBody = nullptr;   // candle wax (cylinders)
	UPROPERTY() UInstancedStaticMeshComponent* CandleCup = nullptr;    // little brass holders
	UPROPERTY() UInstancedStaticMeshComponent* Flame = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* Rope = nullptr;         // string-light cord, wires and the poles' finials
	UPROPERTY() TArray<UInstancedStaticMeshComponent*> GroundLeaf;
	UPROPERTY() TArray<UInstancedStaticMeshComponent*> FallLeaf;
	UPROPERTY() TArray<UInstancedStaticMeshComponent*> GustLeaf;

	UInstancedStaticMeshComponent* MakeISM(const TCHAR* Name, UStaticMesh* Mesh, const FLinearColor& Color, bool bShadow, int32 CullStart = 0, int32 CullEnd = 0);

	// Candle flames on the village string lights (they flicker)
	TArray<FVector> FlameBase;
	TArray<float> FlamePhase;
	float FlameTimer = 0.f;
	// Leaves drifting down around the player
	struct FFallingLeaf { FVector Pos; float Fall; float Phase; float Spin; int32 Type; int32 Index; };
	TArray<FFallingLeaf> Falling;
	// A fast gust of wind that sweeps a flurry of leaves past every ten seconds
	struct FGustLeaf { float Along; float Across; float Height; float Phase; float Spin; int32 Type; int32 Index; };
	TArray<FGustLeaf> GustLeaves;
	float GustTimer = 0.f;      // first gust a few seconds after the start
	float GustT = 0.f;
	bool bGusting = false;
	bool bGustHidden = false;
	FVector GustOrigin = FVector::ZeroVector;
	FVector2D GustDir = FVector2D(1.f, 0.f);

	float LeafZ = 0.012f;   // thickness scale of a loose leaf (the painted card is flat, so 1)
	float LeafXY = 0.22f;   // size scale of a flying leaf
	void SetGustLeaves(float Fade);

	FRandomStream Rand;
	float Time = 0.f;

	TArray<TPair<UInstancedStaticMeshComponent*, FLinearColor>> Tints;
};

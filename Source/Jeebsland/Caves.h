#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Caves.generated.h"

class UProceduralMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

// Caves in the mountains and the sea cliffs (owner, 2026-11-26: "Let's add some caves in the mountains and on the cliffs").
// Docs/CavesPlan.md has the plan, Docs/MapRework.md the log. The ground is one height field and cannot have a hole under it, so each
// cave is its own mesh: a passage of rings (a flat floor, rough walls, a vaulted roof) winding into the hill to a chamber, carried on
// out past its first ring as a rock frame that fans over the ground round the mouth and sinks into it, so the whole is one closed sheet.
// The 2 m ground cells the passage or the frame would cut are left out (HoleCell). Everything is worked out once from the finished height
// (JeebTerrain::HeightAt), the same on the server and every player's machine: log JEEBCAVE (hash).
namespace JeebCaves
{
	struct FStation
	{
		FVector2D P = FVector2D::ZeroVector;     // the floor's middle line (cm)
		FVector2D Dir = FVector2D(1.f, 0.f);     // heading, into the hill
		float A = 0.f;                           // cm along from the mouth
		float Floor = 0.f, HalfW = 200.f, Height = 400.f;
		FVector2D Lat() const { return FVector2D(-Dir.Y, Dir.X); }
	};
	struct FCrystal { FVector At = FVector::ZeroVector; FVector Up = FVector::UpVector; float Size = 1.f; };
	struct FCave
	{
		FString Name;
		bool bSea = false;
		FVector2D Mouth = FVector2D::ZeroVector, D = FVector2D(1.f, 0.f);   // the mouth's floor middle, and the heading in
		float Floor0 = 0.f;
		int32 Turn = 1;                       // which hand the first bend takes
		TArray<FStation> St;
		FBox2D Box = FBox2D(ForceInit);       // everything of it in plan, with the frame
		FBox2D Fine = FBox2D(ForceInit);      // its own 2 m ground patch (mountain caves; empty for the sea caves, which lie in the cliffs' patches)
		TSet<int64> Holes;                    // 2 m ground cells left out (CellKey)
		FLinearColor Glow = FLinearColor::White;
		int32 Ore = -1;                       // an EItemType for its mining rock, -1 none
		FVector RockAt = FVector::ZeroVector;
		TArray<FCrystal> Crystals;
		TArray<FVector> Lights;
		float Score = 0.f, MinCover = 0.f;
	};

	constexpr int32 RingN = 24, FloorN = 5;   // points round a ring; the first FloorN are the flat floor, left to right
	constexpr float Step = 150.f;              // cm between rings
	constexpr float HoodCover = 380.f, HoodEnd = 520.f;   // cm out from the rim the frame covers the ground, and where it has sunk into it

	bool On();                         // (-NoCaves, test builds only: none; give every machine of a test the same switch)
	void Prepare(UWorld* World);       // KnightGameState::BuildWorld, after JeebOpen::Prepare; only the real map (Field) gets caves
	const TArray<FCave>& All();
	uint32 Hash();
	inline int64 CellKey(float X0, float Y0) { return (int64(FMath::FloorToInt(X0 / 200.f + 0.5f)) << 32) ^ int64(uint32(FMath::FloorToInt(Y0 / 200.f + 0.5f))); }
	bool HoleCell(float X0, float Y0);  // the 2 m ground cell with its low corner here is left out (a mountain cave's mouth)
	bool CutsAt(const FVector& P);     // a sea cliff wall's face here is left out (inside a sea mouth's opening, under its roof)
	bool Covered(float X, float Y);     // under a cave's rock frame: no grass, trees, bushes or small life
	float InsideAt(const FVector& P);   // 0 outside or at the mouth .. 1 from 10 m in (this machine's looks: the sky, exposure, storm)
	const TArray<FBox2D>& FineBoxes();  // the mountain caves' 2 m ground patches (empty until Prepare)
	// A point of a ring, in the world (cm), with its noise; Phi: its angle round the mouth's middle (0 straight down, PI the roof)
	FVector RingPoint(const FCave& C, int32 Station, int32 K);
	// Builds every cave's shell (the passage, the chamber and the mouth's frame) on Owner, with collision; one mesh per cave
	void BuildShells(AActor* Owner, UMaterialInterface* Mat, bool bServer);

	// Looks, read by ADayNight and AOpenWorld (cvars jeeb.Cave.*)
	float SkyKeep();        // the sky light's share kept with the camera all the way inside
	float ExposureLift();   // stops added to the exposure all the way inside
}

// The caves' crystals and their lights, and the inside factor for this machine's camera (looks only: a player's machine; nothing here on a
// dedicated server). Spawned by AKnightGameState after the world's ground.
UCLASS()
class ACaves : public AActor
{
	GENERATED_BODY()
public:
	ACaves();
	virtual void Tick(float Dt) override;
	static float CameraInside;   // 0 .. 1 (eased), this machine's camera
protected:
	virtual void BeginPlay() override;
private:
	UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> Gems;
	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> Lamps;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> GemMats;
	TArray<int32> LampCave;
	TArray<float> LampBase;
	float Pulse = 0.f;
	float PawnCheck = 0.f;
};

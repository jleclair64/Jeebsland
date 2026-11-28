#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DeepCaves.generated.h"

class UProceduralMeshComponent;
class UPointLightComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;

// The Deeps: the cave maze inside Silver Point's mountain (2026-11-28, agent mountain; Docs/CavePlan.md). Owner: "a massive cave system that
// can be entered from the summit, a couple exits at the sheer cliff midway up ... one midway down ... off the winding paths, and one more at
// the base of the mountain ... a maze of tunnels to get lost in with rooms ... about half of the tunnels lit by torches".
//  - Five levels of tunnels on jittered grids (a perfect maze from a fixed seed, plus a few loops), joined by spiral ramps round rock pillars;
//    ten rooms of different kinds; five mouths, each in a rock frame (the "hood") that stands out of the slope or the cliff.
//  - The rock is one signed field (Field: < 0 in the air of the tunnels and rooms, noise on the walls), meshed by surface nets on a 75 cm grid
//    in 12 m chunks. Near a mouth the field also holds the hood and the ground (1.5 m under the real ground), so the frame and the passage
//    meet the hill without a seam; the ground's own triangles inside a hood or a mouth's passage are left out (CutsTri).
//  - Everything is worked out from the finished height (JeebTerrain::HeightAt) and fixed numbers: the same on the server and every machine
//    (log JEEBDEEP all / mesh, hash).
namespace JeebDeep
{
	enum class ERoom : uint8 { Crystal, Lake, Collapsed, Mine, Mushroom, Shrine, Lair, Library, Great, Crystal2 };
	const TCHAR* RoomName(ERoom K);

	struct FMouth
	{
		FString Name;
		int32 Kind = 0;                                // 0 the summit, 1 a cliff mouth, 2 off the road, 3 the foot
		FVector At = FVector::ZeroVector;              // the floor's middle where the passage meets the hill's face
		FVector2D D = FVector2D(1.f, 0.f);             // the way in
		float HoodLen = 800.f, Lip = 100.f, Below = 300.f;   // the frame: how far it runs into the hill, out in front (the lip), and under the floor
		FBox Box = FBox(ForceInit);                    // the frame's box, with room round it (the ground term of the field is on inside it)
		int32 Node = -1;
		FVector Outside = FVector::ZeroVector;         // a spot just outside (tests): on the lip for a cliff mouth
		FVector2D Lat() const { return FVector2D(-D.Y, D.X); }
	};
	struct FRoom { ERoom Kind = ERoom::Crystal; FVector C = FVector::ZeroVector; float Rx = 1000.f, Ry = 800.f, Yaw = 0.f, Ht = 800.f, Basin = 0.f; bool bLit = false; int32 Level = 0, Node = -1; FBox Box = FBox(ForceInit); };
	struct FTunnel { TArray<FVector> P; float W = 240.f, Ht = 500.f; bool bLit = false; int32 A = -1, B = -1; uint8 Kind = 0; /* 0 level, 1 spiral, 2 mouth */ };
	struct FNode { FVector P = FVector::ZeroVector; int32 Level = 0, Room = -1, Mouth = -1; bool bHelix = false; TArray<int32> Links; TArray<int32> Tun; };
	struct FSpot { FString Room, Kind, Note; FVector At = FVector::ZeroVector; float Yaw = 0.f; };
	struct FLightSrc { FVector At = FVector::ZeroVector; FLinearColor Col = FLinearColor::White; float Lumens = 400.f, Radius = 900.f; bool bFlicker = true; };
	struct FProp { FString Mesh, Mat; FTransform Xf; FLinearColor Col = FLinearColor::White; bool bCollide = false; float Glow = 0.f; };

	bool On();                    // (-NoDeeps, test builds: none; give every machine of a test the same switch)
	void Prepare(UWorld* World);  // KnightGameState::BuildWorld, after JeebOpen::SilverPrepare and before any ground is built
	bool Ready();
	float Field(const FVector& P);       // the caves' rock: < 0 in their air (cm, roughly a distance), wall noise included
	bool CutsTri(const FVector& A, const FVector& B, const FVector& C);   // a ground triangle a mouth's frame or passage stands in for
	bool Covered(float X, float Y);      // under a mouth's frame: no grass, trees or small life
	float InsideAt(const FVector& P);    // 0 outside .. 1 well inside (this machine's looks: the sky light, the exposure)
	const TArray<FMouth>& Mouths();
	const TArray<FRoom>& Rooms();
	const TArray<FTunnel>& Tunnels();
	const TArray<FSpot>& Spots();
	// The floor's middle line from one mouth to another through the maze (tests: -DeepWalk), from a spot just outside the first
	bool Route(int32 FromMouth, int32 ToMouth, TArray<FVector>& Out);
	uint32 Hash();
}

// The Deeps' rock (meshed here on every machine: with collision on the server, drawn on the others), the wall torches, the rooms' things
// and chests, and the lights near this machine's camera (a small pool moved from torch to torch). Spawned by AKnightGameState.
UCLASS()
class ADeepCaves : public AActor
{
	GENERATED_BODY()
public:
	ADeepCaves();
	virtual void Tick(float Dt) override;
protected:
	virtual void BeginPlay() override;
private:
	void BuildRock(bool bServer);
	void BuildThings(bool bServer);
	UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> Rock;
	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> Pool;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Sets;
	TArray<FVector> RockCentres;
	TArray<bool> RockInside;
	TArray<JeebDeep::FLightSrc> Srcs;
	TArray<int32> PoolSrc;
	float Clock = 0.f, PickIn = 0.f;
};

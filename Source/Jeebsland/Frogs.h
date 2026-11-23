#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Frogs.generated.h"

class UStaticMeshComponent;
class USoundAttenuation;
class USoundWaveProcedural;

// Little green frogs (2026-11-22, agent critters; owner: "groups of 2-3 on river and pond banks and on lily pads, sparse; they hop away when the
// player comes near; quiet croaks at night"). Visual only: players' machines build it, never a dedicated server, and nothing here is ever sent.
//  - Layout is by fixed hashes of 25 m squares (the same on every machine): a square near water may hold a group of 2-3 on the dry bank, and a lily
//    pad that the bushes (Bushes.cpp) put on the water may hold a group on it and the pads beside it. Squares are worked out as the player comes
//    within 70 m, and forgotten past 140 m. A frog only becomes a mesh (one /Game/Critters/SM_Frog component) within 55 m.
//  - A sitting frog breathes, turns its head now and then, and when the player's pawn comes within about 4 m it crouches and hops away from them
//    (an arc, squashed and stretched, toward water if there is some); a frog that lands in water, or on water between pads, dives and is gone until
//    the player is 20 m away, when it is back on its spot.
//  - At night (ADayNight::Night) a frog within 40 m of the player croaks now and then, softly (a procedural two-pulse "rib-bit", no sound files).
// `jeeb.Critters 0` removes the frogs; `jeeb.Frog.Density` (live) scales how many groups there are; `jeeb.Frog.Scare` (cm) is how near the player
// must come to make a frog hop; `jeeb.Frog.Croak` (0..) the croaks' loudness. Log JEEBFROG.
UCLASS()
class AFrogs : public AActor
{
	GENERATED_BODY()

public:
	AFrogs();
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	enum class EState : uint8 { Sit, Crouch, Hop, Land, Sink, Gone };
	struct FFrog
	{
		int64 Cell = 0;
		FVector Home = FVector::ZeroVector;     // where it sits (on a pad: the pad's top)
		int32 HomePad = -1, Pad = -1;            // the pad it started on, and the pad it is on now (-1: ground)
		float HomeYaw = 0.f, Yaw = 0.f;
		FVector Pos = FVector::ZeroVector;
		EState State = EState::Sit;
		float T = 0.f, Dur = 1.f;
		FVector From = FVector::ZeroVector, To = FVector::ZeroVector;
		bool bToWet = false;
		float Seed = 0.f, Scale = 1.f;
		float Idle = 3.f, TurnTo = 0.f, Croak = 0.f;
		float Away = 0.f;                        // seconds the player has been far
		UStaticMeshComponent* Comp = nullptr;
	};
	struct FPad { FVector Top; int64 Cell = 0; bool bTaken = false; };
	TMap<int64, TArray<int32>> PadsByCell;

	TArray<FFrog> Frogs;
	TArray<FPad> Pads;
	TSet<int64> Done;                  // squares already worked out
	bool bPadsRead = false;
	float CellTimer = 0.f, CroakTimer = 6.f, LastDensity = 1.f;
	int32 Groups = 0;
	UPROPERTY() UStaticMesh* Mesh = nullptr;
	UPROPERTY() USoundAttenuation* Att = nullptr;
	UPROPERTY() TArray<USoundWaveProcedural*> Croaks;

	void ReadPads();
	void WorkOutCell(int32 I, int32 J);
	bool BankSpot(FRandomStream& R, float X0, float Y0, FVector& Out);
	void AddFrog(int64 Cell, const FVector& Home, int32 Pad, FRandomStream& R);
	void Wake(FFrog& F);
	void Sleep(FFrog& F);
	void Place(FFrog& F, const FVector& Pl, float Dt);
	void StartHop(FFrog& F, const FVector& Pl);
	float GroundZ(float X, float Y, bool& bWet) const;
	void PlayCroak(const FVector& At);
};

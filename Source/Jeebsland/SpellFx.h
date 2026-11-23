#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpellFx.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

// Short-lived spell effects (2026-11-22, owner: bolts "more like the projectiles in the Hogwarts game", and a teleport like a portkey warp
// "into a point and warp out at the next point"). Built from a few glowing balls (/Game/Props/M_GlowOrb, unlit additive), each moved and
// faded by code, then the actor goes. Players' machines only, never replicated: whatever starts them (the bolt, MultiBlink) already runs
// on every machine. Cheap: at most about 16 small meshes, gone within half a second.
UENUM()
enum class ESpellFx : uint8
{
	Muzzle,    // a quick flash at the wand's tip or the staff's orb as the bolt leaves
	Burst,     // where a bolt lands: a flash, a thin expanding shell and sparks flying out
	WarpOut,   // the teleport's start: a dark-blue swirl of wisps spiralling in and a column squeezing down to a point
	WarpIn,    // the teleport's end: a point that unfolds into a column, the swirl spiralling out
	Spark,     // a spell's trail (2026-11-22, "bottle rocket"): a few bright sparks in its colour, drifting and falling, gone in 0.6 s
	Smoke,     // a spell's trail: a soft dim puff that swells and fades over 0.8 s
	FireBurst, // where a Fire Bolt lands (2026-11-22): a big orange flash, a fireball shell swelling out, puffs of flame and embers drifting up
};

UCLASS()
class ASpellFx : public AActor
{
	GENERATED_BODY()
public:
	ASpellFx();
	static void Play(UWorld* World, ESpellFx Kind, const FVector& At, const FVector& Dir, const FLinearColor& Color);
	virtual void Tick(float Dt) override;

private:
	struct FBit { TObjectPtr<UStaticMeshComponent> C; TObjectPtr<UMaterialInstanceDynamic> M; FVector V, A, B; float Ang = 0.f, R = 0.f, Z = 0.f, Size = 1.f; int32 Kind = 0; };   // Kind 1: a lightning segment A..B (offsets from the middle; Z its share along the tendril)
	TArray<FBit> Bits;
	ESpellFx Kind = ESpellFx::Muzzle;
	FLinearColor Color = FLinearColor::White;
	FVector Dir = FVector::ForwardVector;
	float T = 0.f, Life = 0.3f;
	FBit& Add(float Size);
	void Start();
};

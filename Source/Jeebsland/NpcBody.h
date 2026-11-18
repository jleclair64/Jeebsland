#pragma once

// The townspeople's bodies (2026-11-18): skinned Meshy characters in /Game/People (Tools/ImportPeople.ps1), each with three clips
// (Idle, Walk, Talk) played by a small code-driven animator with cross-fades: no Blueprint, no anim graph. Which body each person
// wears and the colour of their main garment come from Content/Data/People.csv and PeopleWho.csv. `jeeb.Npc.Models` 0 brings
// back the old bodies built from simple shapes. Looks only: nothing here is replicated or used by gameplay.

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "NpcBody.generated.h"

class UAnimSequence;
class USkeletalMesh;

namespace JeebPeople
{
	// One body (People.csv)
	struct FModel
	{
		FString Name;
		float WalkSpeed = 100.f;   // cm/s the walk clip's feet travel at (play rate = real speed / this, so feet don't slide)
		float WalkSink = 0.f;      // cm the walk clip lifts the feet above the idle's; the body is lowered by this while walking
		float GarmentLum = 0.1f;   // the main garment's mean brightness in the texture (linear): the dye keeps its shading
		float Yaw = -90.f;         // turns the mesh to face the actor's forward
		bool bLoaded = false;
		USkeletalMesh* Mesh = nullptr;
		UAnimSequence* Clips[3] = { nullptr, nullptr, nullptr };   // Idle, Walk, Talk
	};
	// Who wears what (PeopleWho.csv): Model (empty: the old shapes) and the garment's colour (a negative R: the role's own colour)
	struct FWho { FString Model; FLinearColor Tint = FLinearColor(-1.f, 0.f, 0.f); bool bFound = false; };

	bool Enabled();                          // jeeb.Npc.Models, and never on a dedicated server
	int32 Setting();                         // the console variable's value (watched by every person to swap bodies live)
	FWho Find(const TArray<FString>& Keys);  // the first PeopleWho.csv row matching one of the keys (names, quest keys, roles)
	const FModel* Model(const FString& Name);   // loaded on first use; null if missing
}

// Plays Idle / Walk / Talk on a person with 0.3 s cross-fades, and turns the head toward the player
UCLASS(Transient, NotBlueprintable)
class UNpcAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	void SetClips(UAnimSequence* Idle, UAnimSequence* Walk, UAnimSequence* Talk, float StartAt);
	// 0 idle, 1 walk, 2 talk; WalkRate scales the walk clip; HeadYaw degrees (positive: toward the right)
	void Drive(int32 State, float WalkRate, float HeadYaw) { Want = State; Rate = WalkRate; Head = HeadYaw; }

	UPROPERTY(Transient) TObjectPtr<UAnimSequence> Clip[3];
	int32 Want = 0;
	float Rate = 1.f, Head = 0.f, Start = 0.f;
	bool bNewClips = false;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
};

USTRUCT()
struct FNpcAnimProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()
	FNpcAnimProxy() {}
	FNpcAnimProxy(UAnimInstance* In) : FAnimInstanceProxy(In) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

	UAnimSequence* Clip[3] = { nullptr, nullptr, nullptr };
	float Time[3] = { 0.f, 0.f, 0.f };
	float Weight[3] = { 1.f, 0.f, 0.f };
	float Head = 0.f;
};

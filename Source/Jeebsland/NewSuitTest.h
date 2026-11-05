#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NewSuitTest.generated.h"

class USkeletalMeshComponent;
class USkinnedMeshComponent;
class UPoseableMeshComponent;
class UAnimSequence;
class USkeletalMesh;
class FKnightAnimator;

// The new armour suit on the Mixamo knight, for looking at in MoveLab (2026-11-05): nothing else in the game uses it yet.
// Every machine makes its own; it is only something to look at and changes nothing in the game. Three stand in a row showing the
// rest pose, the walk and the run, and a fourth is the local player's: N puts it on him. On the player it is driven by FKnightAnimator
// (KnightAnimator.h), which plays clips by what he is doing, and his sword, shield and bow move to its hands. Only when playing alone
// (editor Play): online the server's rules read the old joints, so N does nothing there.
UCLASS()
class ANewSuitTest : public AActor
{
	GENERATED_BODY()

public:
	ANewSuitTest();
	virtual ~ANewSuitTest();
	enum EShow { Show_Stand, Show_Walk, Show_Run, Show_Player };
	int32 Show = Show_Stand;   // set before BeginPlay
	virtual void Tick(float Dt) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Why) override;

private:
	void Play(int32 Clip, float Rate);
	void Paint(USkinnedMeshComponent* C);
	void SetWorn(bool bOn, class AKnightCharacter* Me);
	void TestInput(class AKnightCharacter* Me, float Dt);
	void TestAct(class AKnightCharacter* Me);
	bool bTestActed = false;

	UPROPERTY() TObjectPtr<USkeletalMeshComponent> Body;
	UPROPERTY() TObjectPtr<UPoseableMeshComponent> Pose;   // Show_Player: what the animator writes to
	UPROPERTY() TObjectPtr<UAnimSequence> Walking;
	UPROPERTY() TObjectPtr<UAnimSequence> Running;
	UPROPERTY() TObjectPtr<UAnimSequence> Resting;
	UPROPERTY() TObjectPtr<USkeletalMesh> SuitMesh;
	TSharedPtr<FKnightAnimator> Anim;
	bool bAnimFailed = false;
	int32 Playing = -1;
	int32 Forced = -1;     // test pictures: -SuitShow=stand, walk or run
	bool bWorn = false;    // Show_Player: on the player now
	class AKnightCharacter* Tied = nullptr;   // the knight this actor's tick runs after
	float TestT = 0.f;
};

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NewSuitTest.generated.h"

class USkeletalMeshComponent;
class UAnimSequence;

// The new armour suit on the Mixamo knight, for looking at in MoveLab (2026-11-05): nothing else in the game uses it yet.
// Every machine makes its own; it is only something to look at and changes nothing in the game. Three stand in a row showing the
// reference pose, the walk and the run, and a fourth is the local player's: N puts it on him (his old knight is hidden, and
// it plays the walk or the run by his speed). Only the walk and run clips exist, so swings and jumps don't show in it.
UCLASS()
class ANewSuitTest : public AActor
{
	GENERATED_BODY()

public:
	ANewSuitTest();
	enum EShow { Show_Stand, Show_Walk, Show_Run, Show_Player };
	int32 Show = Show_Stand;   // set before BeginPlay
	virtual void Tick(float Dt) override;

protected:
	virtual void BeginPlay() override;

private:
	void Play(int32 Clip, float Rate);

	UPROPERTY() TObjectPtr<USkeletalMeshComponent> Body;
	UPROPERTY() TObjectPtr<UAnimSequence> Walking;
	UPROPERTY() TObjectPtr<UAnimSequence> Running;
	int32 Playing = -1;
	int32 Forced = -1;     // test pictures: -SuitShow=stand, walk or run
	bool bWorn = false;    // Show_Player: on the player now
};

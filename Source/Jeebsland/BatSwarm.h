#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BatSwarm.generated.h"

class UInstancedStaticMeshComponent;

// Every minute or so a cluster of bats swoops in, circles and flutters across the sky near the player, then is gone.
UCLASS()
class ABatSwarm : public AActor
{
	GENERATED_BODY()

public:
	ABatSwarm();
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() UInstancedStaticMeshComponent* Body = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* Wing = nullptr;

	struct FBat { float Radius; float Speed; float Phase; float HeightOff; float Flap; float Size; float Lag; };
	TArray<FBat> Bats;

	FRandomStream Rand;
	bool bActive = false;
	float WaitTimer = 12.f;     // first swarm soon after the start
	float T = 0.f;
	FVector Target = FVector::ZeroVector;   // roughly where it passes
	FVector2D Dir = FVector2D(1.f, 0.f);

	FVector BatPos(int32 Index, float Time) const;
	void StartSwarm();
	void HideAll();
};

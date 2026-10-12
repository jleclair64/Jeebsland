#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Fireflies.generated.h"

// Fireflies that drift in and out, a little like the leaf gusts but far slower: a swarm appears in an open spot near the
// player, drifts about blinking for one to two minutes, fades away, and then there are none for about as long before
// the next swarm turns up somewhere else.
UCLASS()
class AFireflies : public AActor
{
	GENERATED_BODY()

public:
	AFireflies();
	virtual void Tick(float DeltaSeconds) override;

	static constexpr int32 Count = 36;
	static constexpr float MinLife = 60.f, MaxLife = 120.f;   // how long a swarm stays
	static constexpr float MinGap = 60.f, MaxGap = 120.f;     // and how long before the next one

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() UInstancedStaticMeshComponent* Bugs = nullptr;
	struct FBug { FVector Off; float Phase; float Rate; float Blink; float Radius; };
	TArray<FBug> BugData;
	FVector2D Centre = FVector2D::ZeroVector;
	float Timer = 20.f;      // the first swarm turns up about 20 s in
	float Age = 0.f, Life = 0.f;
	bool bActive = false;
	FRandomStream Rand;
	void StartSwarm();
};

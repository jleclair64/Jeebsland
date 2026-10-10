#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "NightSky.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

// Stars, the Milky Way, shooting stars and the odd comet. Everything sits on a huge dome that travels with the camera,
// fades in as the sun goes down (ADayNight::Night) and twinkles in the material.
UCLASS()
class ANightSky : public AActor
{
	GENERATED_BODY()

public:
	ANightSky();
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() UInstancedStaticMeshComponent* Stars = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* Streaks = nullptr;     // shooting stars
	UPROPERTY() UInstancedStaticMeshComponent* CometTail = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* CometHead = nullptr;
	UPROPERTY() UProceduralMeshComponent* MilkyWay = nullptr;
	UPROPERTY() TArray<UMaterialInstanceDynamic*> Mids;              // every material here has a "Night" parameter

	static constexpr float Radius = 21000.f;   // beyond the moon (20000), so the moon sits in front of the stars
	static constexpr int32 NumShooting = 3;

	struct FShooting { FVector Start; FVector Dir; float Speed = 0.f; float Age = 0.f; float Life = 0.f; bool bLive = false; };
	FShooting Shooting[NumShooting];
	float NextShooting = 4.f;

	bool bComet = false;
	bool bNightCounted = false;
	FVector CometDir = FVector::UpVector;
	FVector CometDrift = FVector::ZeroVector;
	float CometFade = 0.f;

	FRandomStream Rand;
	void BuildMilkyWay();
	FTransform StreakTransform(const FVector& HeadDir, const FVector& TailDir, float Length, float Width) const;
};

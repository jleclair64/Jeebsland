#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "NightSky.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

// The sky dressing that travels with the camera: stars, shooting stars and the odd comet (they come out as the sun
// sinks toward the horizon, ADayNight::Dusk), and soft puffy clouds that turn sunset colours.
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
	UPROPERTY() USceneComponent* CloudRoot = nullptr;
	UPROPERTY() TArray<UInstancedStaticMeshComponent*> CloudSprites;
	UPROPERTY() TArray<UMaterialInstanceDynamic*> StarMids;          // every star-like material has a "Night" parameter
	UPROPERTY() UMaterialInstanceDynamic* TailMid = nullptr;
	UPROPERTY() UMaterialInstanceDynamic* HeadMid = nullptr;
	UPROPERTY() UMaterialInstanceDynamic* CloudMid = nullptr;

	static constexpr float Radius = 21000.f;   // beyond the moon (20000), so the moon sits in front of the stars
	static constexpr float CloudRadius = 19000.f;
	static constexpr int32 NumShooting = 3;

	struct FShooting { FVector Start; FVector Dir; float Speed = 0.f; float Age = 0.f; float Life = 0.f; bool bLive = false; };
	FShooting Shooting[NumShooting];
	float NextShooting = 4.f;

	// One comet at a time, blue or red
	bool bComet = false;
	bool bDuskCounted = false;
	FVector CometDir = FVector::UpVector;
	FVector CometDrift = FVector::ZeroVector;
	float CometFade = 0.f;

	FRandomStream Rand;
	void BuildClouds();
	FTransform StreakTransform(const FVector& HeadDir, const FVector& TailDir, float Length, float Width) const;
};

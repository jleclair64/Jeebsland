#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "Moon.generated.h"

// A moon far away in the sky. Its phase slowly goes from a full moon to a thin fingernail and back.
// PhaseLegSeconds is how long full -> fingernail takes (5 minutes).
UCLASS()
class AMoon : public AActor
{
	GENERATED_BODY()

public:
	AMoon();
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() UProceduralMeshComponent* Dark = nullptr;  // the whole disc, very dim
	UPROPERTY() UProceduralMeshComponent* Lit = nullptr;   // the bright part (changes shape with the phase)
	UPROPERTY() class UMaterialInstanceDynamic* LitMid = nullptr;
	UPROPERTY() class UMaterialInstanceDynamic* DarkMid = nullptr;
	UPROPERTY() UProceduralMeshComponent* Halo = nullptr;   // soft glow round the disc
	UPROPERTY() class UMaterialInstanceDynamic* HaloMid = nullptr;

	static constexpr float PhaseLegSeconds = 300.f;   // full moon to fingernail
	static constexpr float SkyDistance = 20000.f;
	static constexpr float MoonRadius = 1100.f;       // about 6 degrees across: a big storybook moon

	float Age = 0.f;
	float LastPhase = -1.f;
	FVector SkyDir;
	void BuildLit(float Phase);
};

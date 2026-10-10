#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "Paths.generated.h"

// Dirt roads: a ribbon of ground-hugging mesh along each path in JeebTerrain::PathDefs().
UCLASS()
class APaths : public AActor
{
	GENERATED_BODY()

public:
	APaths();

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() UProceduralMeshComponent* Mesh = nullptr;
};

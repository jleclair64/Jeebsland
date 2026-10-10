#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "GrassField.generated.h"

// The land: rolling hills (a generated ground mesh), grass tufts, and choppable trees.
UCLASS()
class JEEBSLAND_API AGrassField : public AActor
{
	GENERATED_BODY()

public:
	AGrassField();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, Category = "Field") int32 GrassCount = 0;
	UPROPERTY(EditAnywhere, Category = "Field") int32 TreeCount = 280;
	UPROPERTY(EditAnywhere, Category = "Field") float FieldRadius = 9000.f;

private:
	UPROPERTY() UProceduralMeshComponent* Terrain;
	UPROPERTY() UInstancedStaticMeshComponent* Grass;

	void BuildTerrain();
	void Tint(UPrimitiveComponent* Comp, const FLinearColor& Color);
};

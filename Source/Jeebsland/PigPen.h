#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PigPen.generated.h"

class UInstancedStaticMeshComponent;

// A big fenced pen with a gate gap, hay, a trough and a herd of pigs.
UCLASS()
class APigPen : public AActor
{
	GENERATED_BODY()

public:
	APigPen();

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() UInstancedStaticMeshComponent* Post = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* Rail = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* Hay = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* Mud = nullptr;

	UInstancedStaticMeshComponent* MakeISM(const TCHAR* Name, UStaticMesh* Mesh, const FLinearColor& Color);
	void AddSolid(const FVector& Center, const FVector& HalfExtent);

	TArray<TPair<UInstancedStaticMeshComponent*, FLinearColor>> Tints;
};

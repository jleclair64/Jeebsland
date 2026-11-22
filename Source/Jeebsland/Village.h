#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Village.generated.h"

class UInstancedStaticMeshComponent;

// The original square: the huts scattered outside and the seven villagers (Blackrock and the market went, 2026-11-22).
UCLASS()
class AVillage : public AActor
{
	GENERATED_BODY()

public:
	AVillage();

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() UInstancedStaticMeshComponent* Log = nullptr;    // wall logs and gate posts (cylinders)
	UPROPERTY() UInstancedStaticMeshComponent* Tip = nullptr;    // sharpened tops (cones)
	UPROPERTY() UInstancedStaticMeshComponent* Beam = nullptr;   // rails and lintels (cubes)
	UPROPERTY() UInstancedStaticMeshComponent* Stone = nullptr;  // towers (cubes)
	UPROPERTY() UInstancedStaticMeshComponent* StoneRing = nullptr; // the well (cylinder)
	UPROPERTY() UInstancedStaticMeshComponent* Roof = nullptr;   // tower and well roofs (cones)
	UPROPERTY() UInstancedStaticMeshComponent* Water = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* Plank = nullptr;  // the walkway deck
	UPROPERTY() UInstancedStaticMeshComponent* Awning = nullptr; // the store's red cloth roof
	UPROPERTY() UInstancedStaticMeshComponent* Gold = nullptr;   // ingots and the store sign
	UPROPERTY() UInstancedStaticMeshComponent* Ember = nullptr;  // the forge fire (glows)
	UPROPERTY() UInstancedStaticMeshComponent* Iron = nullptr;   // anvil, coal, rack


	UInstancedStaticMeshComponent* MakeISM(const TCHAR* Name, UStaticMesh* Mesh, const FLinearColor& Color, bool bShadow);
	void AddSolid(const FVector& Center, const FVector& HalfExtent, const FQuat& Rot);

	TArray<TPair<UInstancedStaticMeshComponent*, FLinearColor>> Tints;
};

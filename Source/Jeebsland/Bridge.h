#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BridgeGround.h"
#include "Bridge.generated.h"

class UProceduralMeshComponent;
class UBoxComponent;
class UStaticMeshComponent;

// The great stone bridge over the river between the old village site (west) and Oak Hill (east), "the contested crossing of the war" (roadmap).
// Model: /Game/Bridge/SM_Bridge (Meshy, Art/Bridge/Sources.md; Tools/BuildBridge.ps1), a three-arch hump-backed bridge about 27 m long
// with a 4.6 m deck between low parapets. Collision is made here, never from the model's triangles: a closed solid for the deck and
// arches (a procedural mesh), a box per 3 m of each parapet (each can be mantled: 80 cm over the deck), nothing for the model itself.
// The same on the server and every client (not replicated; spawned by AKnightGameState like the forest ruins); a dedicated server builds
// the collision only. Ground and roads round it: BridgeGround.h / Bridge.cpp. Log lines: JEEBBRIDGE.
UCLASS()
class ABridge : public AActor
{
	GENERATED_BODY()

public:
	ABridge();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float Dt) override;   // (only with -BridgeWalkLog, test builds: logs the first local player walking over it)

private:
	void BuildCollision();
	void BuildDecor();   // lanterns on the end posts, torches and flower boxes on the parapets (2026-11-18, owner); client only, same spots everywhere

	UPROPERTY() UStaticMeshComponent* Visual = nullptr;
	UPROPERTY() UProceduralMeshComponent* Slab = nullptr;
	UPROPERTY() TArray<UBoxComponent*> Rails;
	UPROPERTY() TArray<UActorComponent*> Decor;
};

namespace JeebBridge
{
	// The span, in cm. L runs along the bridge from its middle (west negative, toward the old village site); the deck's height over the world's Z is
	// DeckZ(L) for |L| up to DeckHalf.
	FVector2D Centre();            // where the middle stands (X, Y)
	float DeckZ(float L);          // the deck's walking surface at L (world Z)
	float DeckEndL();              // |L| of the ends of the walking surface
	constexpr float DeckHalfWidth = 230.f;   // the walkway between the parapets
}

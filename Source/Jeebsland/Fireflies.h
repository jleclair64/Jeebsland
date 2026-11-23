#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Fireflies.generated.h"

// Fireflies (rewritten 2026-11-22, agent critters; owner: "bring fireflies back: night only, sparse and spread out, only in woods and grassland,
// never on snow, sand or rock"). Until then a single swarm of 36 turned up in the meadow by day or night for a minute or two and was easy to miss.
// Now the world is divided into 18 m squares; each square, by a fixed hash (the same on every machine, so a firefly lives in the same spot every
// night), holds none, one or two fireflies. They exist only within 45 m of the player's pawn, only while the night is on (ADayNight::Night), and only
// over ground that is grass or woods: not snow, sand, rock, mud, water, roads or roofs. Each drifts in a slow loop round its own home 0.7-2.4 m up
// and pulses on and off. Visual only: never built on a dedicated server. `jeeb.Critters 0` turns the fireflies and the frogs off.
UCLASS()
class AFireflies : public AActor
{
	GENERATED_BODY()

public:
	AFireflies();
	virtual void Tick(float DeltaSeconds) override;

	static constexpr int32 MaxBugs = 48;          // instances (the nearest ones fill them)
	static constexpr float CellSize = 1800.f;     // cm: one square
	static constexpr float Reach = 4500.f;        // cm: squares this near the player are alive

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() UInstancedStaticMeshComponent* Bugs = nullptr;
	struct FBug { FVector Home; float Phase, Rate, Blink, Radius, Rise; };
	TMap<int64, TArray<FBug>> Cells;   // squares worked out so far (their fireflies, maybe none)
	float Fade = 0.f;
	float CellTimer = 0.f;
	float LastDensity = 1.f;
	bool bShown = false;
	void WorkOutCell(int32 I, int32 J);
	static bool GoodGround(float X, float Y, float& OutZ);
};

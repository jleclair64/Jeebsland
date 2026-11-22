#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FoeCamps.generated.h"

class AFoeKnight;
class UStaticMeshComponent;
class UBoxComponent;

// The bandits' camps in the forest round Oak Hill and the old village site (2026-11-18; FoeCamps.csv, the foes in Foes.csv: FoeData.h).
//  - Every machine builds the camps the same way from the data and fixed rules: each camp settles on the nearest good ground to its spot
//    (dry, fairly flat, clear of roads, towns, the forest's trunks and rocks; a Where=ruin camp beside the forest ruin nearest its spot),
//    and gets its props (FillKinds.csv kinds: a campfire with a flame in the middle, a tent, crates ...). Players' machines draw them; every
//    machine has their solids (the server decides where players walk). Log JEEBFOE camp ... and a hash to compare machines.
//  - The server places the foes round each fire (AFoeKnight with a Foes.csv row; their minds are FoeBandit.cpp) and, twice a second, lets
//    a camp's foes sleep while no player is within SleepRange of it: a slow tick and sparse network updates, so camps nobody visits cost
//    next to nothing. Players further than 150 m are never sent a foe at all (net cull distance).
// Switches: jeeb.Foes.Bandits 0 (server, read when the world is built) leaves the foes out (the camps stay); -JeebFoeTest (test builds) runs
// the online test (server and players; FoeCamps.cpp's FFoeTestDriver).
UCLASS()
class AFoeCamps : public AActor
{
	GENERATED_BODY()

public:
	AFoeCamps();
	virtual void Tick(float DeltaSeconds) override;

	struct FCamp
	{
		int32 Def = INDEX_NONE;                    // JeebFoes::Camps() row
		FVector2D At = FVector2D::ZeroVector;      // the fire (world cm)
		TArray<FVector2D> Spots;                   // where each foe stands
		TArray<TWeakObjectPtr<AFoeKnight>> Foes;   // (server)
		bool bAwake = true;
	};
	const TArray<FCamp>& GetCamps() const { return Camps; }

	// The nearest camp to From with a foe a quest's kill step names (a Foes.csv Key or Group): its fire and name. False if none.
	static bool NearestCamp(const UWorld* World, const FString& Target, const FVector2D& From, FVector2D& OutAt, FString& OutName);

	static constexpr float SleepRange = 9000.f;   // cm from a camp's fire: no player nearer, its foes sleep

protected:
	virtual void BeginPlay() override;

private:
	TArray<FCamp> Camps;
	UPROPERTY() TArray<UStaticMeshComponent*> Drawn;
	UPROPERTY() TArray<UBoxComponent*> Solids;
	float SleepClock = 0.f;
	void Build();
	void SpawnFoes();
	void TickSleep();
#if !UE_BUILD_SHIPPING
	friend struct FFoeTestDriver;
	int32 TestStage = 0;
	float TestClock = 0.f, TestNext = 0.f;
#endif
};

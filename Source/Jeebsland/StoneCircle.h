#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StoneCircle.generated.h"

// The standing stones (owner, 2026-11-26; Docs/StoneCirclePlan.md): an ancient ring of weathered standing stones round a flat centre
// stone near the middle of each loot-event PvP zone (Content/Data/StoneCircles.csv: the moor's in the middle of the map and the snowy,
// frost-touched one in The Frozen North). Every machine builds the same stones in the same spot from the file.
//
// The event: every [StoneCircle] IntervalDays game days per circle (each circle on its own day of the cycle, DayOffset in the csv, so the
// two never fall on the same day), at that game day's dawn every player is told which circle will wake; CountdownSeconds before high
// noon a countdown shows on every screen; at high noon the circle wakes (its runes and centre stone glow, a soft light) and ONE loot stack
// (Content/Data/StoneCircleLoot.csv) lies on the centre stone. The first player to take it (E) gets it: the server spawns the stack (an
// ALootDrop), checks reach and empties it once.
//
// The schedule is a pure function of the shared server clock (AGameStateBase::GetServerWorldTimeSeconds; one game day is 1200 s, noon at
// 1200 s x day), so every machine shows the same thing at the same moment with nothing sent. Only the test switch (a wake now) is sent:
// AKnightGameState::StoneTestNoon / StoneTestCircle, set by the server (jeeb.Stones.WakeNow, -StoneWakeIn=<s> -StoneWakeCircle=<n>; not Shipping).
namespace JeebStones
{
	struct FCircle
	{
		FString Name, Zone;      // the circle's name, and the PvP zone it stands in (for the messages)
		FVector2D Pos;           // cm
		float Radius = 900.f;    // cm from the middle to the ring's stones
		bool bFrost = false;     // snowy, frost-touched stones
		int32 DayOffset = 0;     // which day of the IntervalDays cycle it wakes on
	};
	const TArray<FCircle>& Circles();
	// Is this spot (cm) within Extra cm of a circle's ring (trees and props keep off)?
	bool KeepOut(float X, float Y, float Extra);
	// Under a circle's flat centre stone (no grass through it)
	bool UnderCentre(float X, float Y);

	constexpr double DaySeconds = 1200.0;   // one game day (DayNight.h: 2 x 600 s, noon at clock 0)
	constexpr double DawnBefore = 348.0;    // the sun rises at phase 0.71: 348 s before noon
	inline int64 DayOf(double T) { return int64(FMath::FloorToDouble((T + 0.5 * DaySeconds) / DaySeconds)); }   // midnight to midnight
	inline double NoonOf(int64 Day) { return double(Day) * DaySeconds; }

	struct FEvent { int32 Circle = -1; double Dawn = 0.0, CountFrom = 0.0, Noon = 0.0, End = 0.0; bool bTest = false; };
	// The event that is on at server time T (from its dawn until WakeSeconds after its noon), if any. TestNoon > 0: the test wake.
	bool EventAt(double T, double TestNoon, int32 TestCircle, FEvent& Out);
	// The next natural wake of circle C after time T (its noon); for the logs and HowToTest
	double NextNoon(int32 C, double T);
	// Reads the shared clock and the test wake from the world's game state
	bool EventNow(const UWorld* World, FEvent& Out, double* OutT = nullptr);
}

UCLASS()
class AStoneCircles : public AActor
{
	GENERATED_BODY()

public:
	AStoneCircles();
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	struct FLook
	{
		TArray<TWeakObjectPtr<class UMaterialInstanceDynamic>> Runes;
		TWeakObjectPtr<class UPointLightComponent> Light;
		FVector Top = FVector::ZeroVector;   // where the loot lies (the centre stone's top)
		float Glow = -1.f;                   // last strength set
	};
	TArray<FLook> Looks;
	void Build(int32 Ci);
	double Spawned = -1.0;   // server: the noon whose loot was laid (once per wake)
	int32 SpawnedCircle = -1;
	TWeakObjectPtr<class ALootDrop> Stack;
	float Check = 0.f;
	void Search();   // -JeebStoneSearch: logs the best open fields with woods round them (how the spots were chosen)
#if !UE_BUILD_SHIPPING
	void TickNetTest(double T);   // -JeebStoneNetTest on the server: reach and once-only checks
	int32 TestStep = 0;
#endif
};

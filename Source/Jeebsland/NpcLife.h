#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NpcLife.generated.h"

class ANpc;

// The townsfolk's days (2026-11-18, owner: "walk around or have tracks around houses as if they were the owners of a certain house",
// "add the NPCs to the stores so it looks like they're manning the booths").
//
// Every person in Oak Hill, Blackrock and Steele Creek follows a ROUND: a fixed timeline of stays, turns and walks that repeats every
// Period seconds. Where they are is worked out from the server's clock (AGameStateBase::GetServerWorldTimeSeconds), so every machine
// shows them in the same place with nothing sent over the network. The rounds are built once on every machine from the towns' own
// data (plots, doors, counters, streets) by ATownLife, after everything they walk round has been built (the fill props too), the
// same way everywhere.
//   - Villagers (Content/Data/Townsfolk.csv): a home (a house's doorstep), pottering in its yard, one or two errands (the well, a
//     stall, the church...) and back. Two who share their first errand arrive together and chat. A round that starts at night
//     is spent by the door.
//   - Keepers: behind their counter or in their doorway, facing the customers, now and then a step along the counter and back.
//   - Quest givers: a few steps round their post (the quest marker and the map still find them), meeting a neighbour halfway.
//   - Gate knights and the far villagers: their old beats, now on the clock too.
// Locally (looks only) a person holds still while you talk to them or stand in their way and catches up afterwards during their stays.
namespace JeebLife
{
	enum class ESeg : uint8 { Stay, Walk, Turn };
	struct FSeg
	{
		ESeg Kind = ESeg::Stay;
		FVector2D A = FVector2D::ZeroVector, B = FVector2D::ZeroVector;   // (game cm) Stay and Turn at A; Walk from A to B
		float YawA = 0.f, YawB = 0.f;   // Stay faces YawA; Turn goes from YawA to YawB
		float T0 = 0.f, Dur = 0.f;      // seconds into the round
		bool bChat = false;             // a stay where they meet their partner (they talk if the partner is there too)
	};
	struct FRound
	{
		TArray<FSeg> Segs;
		float Period = 60.f;   // seconds; Segs end at Period, where the round starts again from its first spot
		float Offset = 0.f;    // seconds added to the clock (so neighbours aren't in step)
		float Speed = 95.f;    // cm/s walking
		bool bNightHome = false;   // a round that starts at night is spent standing at its first spot (the doorstep)
		FVector2D Anchor = FVector2D::ZeroVector;   // where they belong (quest givers: their post)
		float Roam = 0.f;      // the furthest the round takes them from Anchor (cm)
		TWeakPtr<FRound> Partner;   // who they chat with at their bChat stays
		FString Label;         // (log lines)
	};
	struct FPose
	{
		FVector2D At = FVector2D::ZeroVector;
		float Yaw = 0.f;
		bool bWalk = false, bStay = true, bChat = false, bTurn = false;
		FVector2D ChatAt = FVector2D::ZeroVector;   // the partner, when chatting
	};
	// Where the round has them at this clock time
	FPose Eval(const FRound& R, double Time, bool bCheckPartner = true);
	// The clock the rounds run on: the server's world time (the same on every machine, within the net's lag), plus jeeb.Life.Skip
	double Now(const UWorld* World);
	// Night for the townsfolk: the sun's 20-minute loop (DayNight.h) between about sunset and sunrise, from the clock alone
	bool IsNight(double Time);
	// The ground the people stand on as drawn here (no traces): Oak Hill's 1 m ground, the square's 2 m mesh or the world's 8 m mesh
	float DrawnGround(float X, float Y);
	// A beat (a list of points walked in turn, back to the first) as a round: straight legs, a turn and a pause at each point
	// (Pause seconds, give or take a little by Seed)
	TSharedPtr<FRound> MakeBeat(const TArray<FVector2D>& Pts, float Pause, float Speed, uint32 Seed, bool bNightHome);
}

// Builds every round (after the towns and their props), spawns Oak Hill's and Steele Creek's keepers and Oak Hill's townsfolk.
// On every machine, the server too; not replicated.
UCLASS()
class ATownLife : public AActor
{
	GENERATED_BODY()
public:
	ATownLife();
	virtual void Tick(float DeltaSeconds) override;   // (only with -LifeLog: JEEBLIFE positions every 5 s of the clock)
protected:
	virtual void BeginPlay() override;
	void Build();
	int64 LastLogSlot = -1;
};

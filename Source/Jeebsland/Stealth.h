#pragma once

// Stealth (2026-11-26, Docs/StealthPlan.md): how every NPC that notices players does it. One component on each such NPC (the animals in
// Deer.cpp, the camp bandits in FoeBandit.cpp). The SERVER keeps a detection meter per player: it fills while the NPC sees (fast) or only
// hears (slow, behind cover) a player and drains when it does not; full = noticed. Crouching shrinks the ranges by the Stealth skill, a
// 90-degree cone in front still sees a crouched player, every range halves at night, trunks, walls and rocks block sight (one throttled line
// trace). The mark over the NPC (a yellow "!" while suspicious, red when it turns to a full alert) is one replicated byte, drawn by the HUD.
// Numbers: Content/Data/Tuning.ini [Stealth].

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Stealth.generated.h"

class AKnightCharacter;

// The NPC's own senses, handed in every update (cm)
struct FStealthSenses
{
	float Sight = 3000.f;     // how far it sees a standing player in front of it
	float Behind = 0.35f;     // the share of that behind it
	float FrontDot = -0.45f;  // how far round "in front" reaches (dot product with its facing; -0.45 = most of the way round)
	float HearRun = 0.f;      // how far it hears a sprinting player (through cover)
	float HearWalk = 0.f;     // ... a walking or running one
	float HearStill = 0.f;    // ... one standing still (not crouched)
	float SightMul = 1.f;     // e.g. 0.75 with a deer's head down in the grass
	float EyeZ = 130.f;       // its eyes above its feet
	bool bSkipTowns = false;  // (the bandits never notice anyone standing in a town)
};

UCLASS()
class JEEBSLAND_API UStealthSense : public UActorComponent
{
	GENERATED_BODY()
public:
	UStealthSense();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Why) override;

	// (server) call every tick the NPC is awake; scans every StealthScanSeconds. Returns the nearest player it has noticed (alert), or null
	AKnightCharacter* Update(float Dt, const FStealthSenses& S, float& OutDist);
	// (server) it was hurt or the herd bolted: straight to alert on this player
	void ForceAlert(AKnightCharacter* K);
	// (server) asleep or dead: forget everyone, no mark
	void Clear();
	// (server) the meter on one player (tests and logs)
	float MeterOf(const AKnightCharacter* K) const;
	bool IsAlertOn(const AKnightCharacter* K) const;

	uint8 Mark() const { return RepMark; }   // 0 nothing, 1 suspicious, 2 just alerted
	FString LogName;                         // what the log calls it

	// every component in this world, for the HUD's marks (clients)
	static const TArray<TWeakObjectPtr<UStealthSense>>& All();

	// the crouched range share and the cone's share at a Stealth level (also for the skill tips)
	static float CrouchShare(int32 Level);
	static float ConeShare(int32 Level);

private:
	struct FEntry { float Meter = 0.f; bool bAlert = false; uint8 Last = 0; };
	TMap<TWeakObjectPtr<AKnightCharacter>, FEntry> Seen;
	float ScanT = 0.f, Acc = 0.f, AlertMarkT = 0.f;
	TWeakObjectPtr<AKnightCharacter> Best;
	float BestD = 1.0e9f;
	UPROPERTY(Replicated) uint8 RepMark = 0;
};

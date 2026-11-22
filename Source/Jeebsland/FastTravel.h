#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "FastTravel.generated.h"

class USkillComponent;
class AKnightCharacter;

// Fast travel (2026-11-18). A player can jump to any town their ACCOUNT has visited: Oak Hill, Steele Creek and, as they are
// built, the four future towns (add a row to JeebTravel::Towns() below; nothing else needs to change).
//
// ONLINE the server owns all of it. The server marks a town visited when the knight first comes within the town's radius, saves that
// per account (Saved/Accounts/<key>.towns, shared by the account's characters, and in the character's own save as a backup) and tells the
// owning player only (VisitedMask replicates COND_OwnerOnly; ClientDiscovered shows the "DISCOVERED" title). Travelling: the map's
// markers ask (ServerTravel); the server checks the town is visited, the knight is free (AKnightCharacter::TravelBlocker: alive, on the
// ground, not swimming, climbing, mounted, stunned, knocked down or busy), out of combat, past the cooldown, not asking too fast and
// not in a PvP area (PvpHook below); then it runs a 3 s channel that movement, an attack or a blow cancels, moves the knight to a
// free arrival spot (ArrivalSpot) and tells the player's machine, which fades its screen to black and back around the jump.
// A refusal arrives as a one-line message.
namespace JeebTravel
{
	struct FTown
	{
		const TCHAR* Name;     // as in JeebWorld::Towns(): the centre is read from there so the two never disagree
		float Radius;          // cm from the centre within which the town counts as visited (the walls / the streets and a little more)
		bool bHome;            // always known (Oak Hill, where new characters appear)
		bool bOpen;            // false: the arrival spots aren't made yet, so it can't be travelled to (and is not marked visited)
		FVector2D Anchor;      // where arrivals gather (cm; zero: the town's centre) and the way they face (degrees, Unreal yaw)
		float Yaw;
	};
	// THE TABLE. The order is the saved bit number only in memory: saves store names, so rows can be added or moved freely.
	const TArray<FTown>& Towns();
	int32 Count();
	int32 IndexOf(const FString& Name);
	FVector2D Centre(int32 Index);
	// The town whose radius holds this spot, or -1
	int32 TownAt(const FVector2D& P);

	// A free place to put a knight arriving in this town: open ground (nothing solid where a knight stands, no other knight, not in water),
	// the anchor first and then rings round it, always facing the town's way. Oak Hill uses AKnightGameMode::OakWakeSpot. Server only.
	FVector ArrivalSpot(UWorld* World, int32 Index, const AActor* Self, float& OutYaw);

	// "Never into or out of PvP areas" (Roadmap): there are no PvP areas yet. When they exist, return true here for a spot inside one;
	// the server refuses a travel that starts or ends in one. ONE place to change.
	inline bool PvpHook(const FVector2D& /*P*/) { return false; }
}

// The travel part of a knight (beside USkillComponent and UQuestComponent).
UCLASS()
class UTravelComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTravelComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ---- what the player's machine asks for ----
	void Ask(int32 Town);   // (single player: decided here; online: sent)
	UFUNCTION(Server, Reliable) void ServerTravel(uint8 Town);
	UFUNCTION(Client, Reliable) void ClientTravelStart(uint8 Town, float Seconds);   // the 3 s channel began
	UFUNCTION(Client, Reliable) void ClientTravelCancel();
	UFUNCTION(Client, Reliable) void ClientTravelArrived(FVector_NetQuantize Dest, float Yaw);
	UFUNCTION(Client, Reliable) void ClientDiscovered(uint8 Town);

	// ---- questions (every machine, from the replicated state) ----
	bool IsVisited(int32 Town) const { return Town >= 0 && Town < 32 && ((VisitedMask >> Town) & 1u) != 0; }
	// The screen's state (this machine): the count, the fade and the title
	bool Channelling(int32& OutTown, float& OutLeft) const;
	float TickFade(float Dt);   // the black over the screen, 0 to 1, moved toward where it should be (the HUD calls this each frame)
	FString DiscoveredTitle; double DiscoveredTime = -100.0;

	// ---- the server's hooks ----
	static void NoteCombat(AActor* By);   // this actor (or whoever shot it) dealt a blow (HealthComponent)
	double LastDealt = -1000.0;

	// ---- saving ----
	void Capture(TArray<FString>& OutNames) const;
	void Restore(const TArray<FString>& Names, bool bFromOldSave);   // (an old save has no list: towns whose centre the map fog shows explored count as visited)
	void LoadAccount(const FString& Key);   // online: the account's file, joined to what the save said
	FString AccountKey;

	UPROPERTY(ReplicatedUsing = OnRep_Visited) uint32 VisitedMask = 0;

private:
	bool HasAuth() const;
	AKnightCharacter* Knight() const;
	void DoTravel(int32 Town);
	bool Refuse(const FString& Why) const;
	void CancelChannel(const TCHAR* Why);
	void Arrive();
	void MarkVisited(int32 Town, bool bAnnounce);
	void SaveAccount() const;
	void TickServer(float Dt);
	UFUNCTION() void OnRep_Visited() {}

	// server
	int32 ChanTown = -1;
	double ChanStart = 0.0, ChanEnd = 0.0, CooldownUntil = -1000.0, LastAsk = -1000.0;
	FVector ChanPos = FVector::ZeroVector;
	double CombatMark = 0.0;
	float VisitClock = 0.f;
	TArray<double> AskTimes;
	// client (the screen)
	int32 ClChanTown = -1;
	double ClChanEnd = 0.0, ClBlackUntil = -1000.0;
	float ClFade = 0.f;
#if !UE_BUILD_SHIPPING
public:
	void TickTest(float Dt);   // -JeebTravelTest (FastTravelTest.cpp)
private:
	float TestClock = 0.f;
	int32 TestStage = 0;
	friend struct FTravelTestDriver;
#endif
};

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "QuestData.h"
#include "QuestComponent.generated.h"

class ANpc;
class USkillComponent;

// One quest a character has started: which step it is on, a counter for that step (kills), and whether it is finished
USTRUCT()
struct FQuestProgress
{
	GENERATED_BODY()
	UPROPERTY() FName Key;
	UPROPERTY() uint8 Step = 0;
	UPROPERTY() int32 Count = 0;
	UPROPERTY() bool bDone = false;
	bool operator==(const FQuestProgress& O) const { return Key == O.Key && Step == O.Step && Count == O.Count && bDone == O.bDone; }
};

// A character's quests (2026-11-18). Lives on the knight beside USkillComponent and is saved with it (JeebSave).
// ONLINE: the server owns it. The player's machine only asks (ServerQuest: take a quest, talk to a person); the server checks that the
// knight stands beside that person, that the quest's needs are met, that the items are in the pack, then changes the state, which
// replicates to the owning player only. Kills (AFoeKnight) and places reached (checked twice a second) are counted on the server.
// What people say is worked out on the player's machine from the same state and the same data files, so it matches what the server
// will do; when it doesn't (lag), the server's refusal arrives as a message.
UCLASS()
class UQuestComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UQuestComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(ReplicatedUsing = OnRep_Quests) TArray<FQuestProgress> Quests;

	// ---- what the player's machine asks for ----
	enum class EOp : uint8 { Accept, Talk };
	void Ask(EOp Op, FName Key);
	UFUNCTION(Server, Reliable) void ServerQuest(uint8 Op, FName Key);
	UFUNCTION(Client, Reliable) void ClientQuestDone(FName Key);   // the "QUEST COMPLETE" title on the player's screen

	// ---- the server's hooks ----
	static void OnFoeBeaten(AActor* By, const TCHAR* Foe);   // a foe yielded to this actor (AFoeKnight)

	// ---- questions (every machine, from the replicated state) ----
	enum class EState : uint8 { Locked, Available, Active, Done };
	EState StateOf(const JeebQuests::FQuestDef& Q) const;
	bool MeetsNeeds(const JeebQuests::FQuestDef& Q, FString* OutWhy = nullptr, bool bQuestsOnly = false) const;
	const FQuestProgress* Find(FName Key) const;
	// How far along the current step is: Have of Need (bring: the items in the pack; kill: the count; talk and visit: 0 of 1)
	void StepProgress(const FQuestProgress& P, int32& OutHave, int32& OutNeed) const;
	FString StepLine(const FQuestProgress& P) const;   // the tracker's line for the current step, with its count
	// What a giver's head shows: nothing, a quest to offer (!), something to hand in (? lit), or a quest of theirs under way (? dim)
	enum class EMark : uint8 { None, Offer, HandIn, Busy };
	EMark MarkFor(FName Person) const;
	// Where the tracked quest's map marker goes (world cm) and what it is
	bool Objective(FVector2D& OutAt, FString& OutWhat) const;

	// ---- conversations (the player's machine) ----
	// What this person says now, one entry per click; sends the server what the talk does (a step handed in). Ends with EndTalk.
	TArray<FString> MakeTalk(const ANpc* Npc);
	void EndTalk(bool bAgreed);   // bAgreed: the player clicked through to the end (an offer is then accepted)
	FName PendingOffer;

	// The quest shown in the tracker (this machine's choice; the newest one by default)
	FName Tracked;
	FName GetTracked() const;
	// "QUEST COMPLETE" on the screen (ClientQuestDone)
	FString DoneTitle;
	double DoneTime = -100.0;

	// ---- saving (JeebSave) ----
	void Capture(TArray<FString>& OutKeys, TArray<int32>& OutSteps, TArray<int32>& OutCounts, TArray<uint8>& OutDone) const;
	void Restore(const TArray<FString>& Keys, const TArray<int32>& Steps, const TArray<int32>& Counts, const TArray<uint8>& Done);

private:
	USkillComponent* Skills() const;
	bool HasAuth() const;
	void DoQuest(EOp Op, FName Key);
	bool NearPerson(FName Person) const;
	void Advance(FQuestProgress& P, const JeebQuests::FQuestDef& Q);   // the current step is done: on to the next, or the reward
	void Finish(FQuestProgress& P, const JeebQuests::FQuestDef& Q);
	bool RewardFits(const JeebQuests::FQuestDef& Q) const;
	void TickVisits();
	UFUNCTION() void OnRep_Quests();
	TArray<FName> KnownKeys;   // (the keys this machine had last time: a new one becomes the tracked quest)
	double LastAsk = -10.0;     // (server: requests closer than this are ignored)
	float VisitClock = 0.f;
	int32 GreetIdx = 0;
#if !UE_BUILD_SHIPPING
	void TickTest(float Dt);   // -JeebQuestTest (QuestTest.cpp)
	float TestClock = 0.f;
	int32 TestStage = 0;
#endif
	friend struct FQuestTestDriver;
	friend struct FFoeTestDriver;   // (-JeebFoeTest, FoeCamps.cpp)
};

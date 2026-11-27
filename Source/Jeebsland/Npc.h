#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Npc.generated.h"

class UCapsuleComponent;
namespace JeebLife { struct FRound; }
class UStaticMeshComponent;
class USkeletalMeshComponent;
namespace JeebPeople { struct FModel; }

UENUM()
enum class ENpcRole : uint8
{
	Shopkeeper,   // the store
	Blacksmith,   // the smithy
	GateKnight,   // walks the road outside the village and chats
	Villager,     // one of ten villagers who stroll about the village and tell their stories
	Banker,       // keeps the vault
	QuestGiver,   // (2026-11-18) one of Oak Hill's people from Content/Data/QuestsPeople.csv: quests, else a greeting (QuestComponent.h)
	Townsfolk     // (2026-11-18) an ordinary townsperson from Content/Data/Townsfolk.csv (NpcLife.h): a greeting of their own, no lore, no trade
};

// How a quest giver looks (QuestsPeople.csv): the villagers' body with these colours and hat
struct FNpcLook
{
	FString Name;
	FLinearColor Tunic = FLinearColor(0.4f, 0.3f, 0.2f), Hair = FLinearColor(0.3f, 0.2f, 0.1f);
	int32 Hat = -1;        // a villager's hat: -1 none, 0 herbalist, 3 cap, 4 baker, 7 cowl, 8 feather
	bool bApron = false;
	float Scale = 1.f;
};

// A townsperson (a skinned Meshy body since 2026-11-18, NpcBody.h; the old one built from simple shapes with jeeb.Npc.Models 0). Talk to them with E: they greet you and tell a story about Jeebsland,
// and the shopkeeper and blacksmith then open their trading screens. Nobody replies to what you say.
UCLASS()
class ANpc : public AActor
{
	GENERATED_BODY()

public:
	ANpc();

	// Called right after spawning: builds the body for this role. Patrol points (if any) are set separately.
	void Setup(ENpcRole InRole, int32 InVillagerIdx = 0);
	void SetPatrol(const TArray<FVector2D>& Points) { Patrol = Points; }
	// A quest giver (QuestPeople.cpp): who they are in the quest data, and how they look
	void SetupPerson(FName InKey, const FNpcLook& InLook) { PersonKey = InKey; Look = InLook; Setup(ENpcRole::QuestGiver); }
	FName GetPersonKey() const { return PersonKey; }
	bool IsQuestGiver() const { return Role == ENpcRole::QuestGiver; }
	FString TestModel;   // (test pictures only, -PeopleLineup: this body whoever they are)
	float TestWalkNear = -1.f;   // (test pictures only, -PeopleWalk: they keep walking their beat while the knight is further than this, cm)
	// Steele Creek's people (2026-11-18), set before Setup: Folk 0 townsfolk and knights as before, 1 a troll, 2 a wizard; a name
	// and a first greeting of their own (empty: the role's)
	void SetFolk(uint8 InFolk, const FString& InName, const FString& InHello) { Folk = InFolk; NameOverride = InName; FirstHello = InHello; }

	// Their day (NpcLife.h): where the clock has them; set by ATownLife on every machine (a beat set with SetPatrol becomes one too)
	void SetRound(const TSharedPtr<JeebLife::FRound>& InRound);
	const TSharedPtr<JeebLife::FRound>& GetRound() const { return Round; }
	const TArray<FVector2D>& GetPatrol() const { return Patrol; }
	// How much further than usual the server lets a quest request count, for someone this far from their post (their round's roam, cm)
	float RoamSlack() const;
	FVector2D RoamAnchor() const;
	// Where and which way they were placed (Setup): the same on every machine, unlike where they face a moment later (they turn to you)
	FVector2D PlacedAt = FVector2D::ZeroVector; float PlacedYaw = 0.f;
	// A body and garment colour when PeopleWho.csv has no row for this person by name (Townsfolk.csv); RebuildBody applies it
	void SetLookHint(const FString& InModel, const FLinearColor& InTint, float InScale = 1.f) { HintModel = InModel; HintTint = InTint; HintScale = InScale; }
	void RebuildBody();

	ENpcRole GetRole() const { return Role; }
	FString GetNpcName() const;
	// World height (cm) of the top of the head of the body shown: a model's own height (its mesh bounds, at its scale), the old
	// shapes' 105 cm over the actor (HUD only: name tags, quest marks and the E prompt sit a set distance above it)
	float HeadTopZ() const;
	bool OffersTrade() const { return Role == ENpcRole::Shopkeeper || Role == ENpcRole::Blacksmith || Role == ENpcRole::Banker; }
	bool IsTalking() const { return bTalking; }
	int32 SpeakerIdx() const;   // which storyteller this is (see Lore.h)
	// What they say on this visit, one entry per click
	TArray<FString> MakeConversation(int32* OutLore = nullptr);   // OutLore: the fragment of lore told this time (or untouched)
	void SetTalking(bool bOn) { bTalking = bOn; }

	// The alarm (2026-11-26, Guards.h; players' machines, looks only): stop, turn to LookAt for Seconds; bShout: the one who heard the
	// thief, with Line in a speech bubble for BubbleSeconds and a red "!", else a yellow "!" (they noticed)
	void Alarm(const FVector& LookAt, float Seconds, float BubbleSeconds, const FString& Line, bool bShout);
	// (HUD) false when calm; else the bubble's line ("" none), whether they shouted, and the bubble's and the mark's fade (0..1)
	bool AlarmShown(FString& OutLine, bool& bOutShout, float& OutBubble, float& OutMark) const;

	virtual void Tick(float DeltaSeconds) override;

private:
	double AlarmUntil = -1.0, BubbleUntil = -1.0;
	FVector AlarmLook = FVector::ZeroVector;
	FString AlarmLine;
	bool bAlarmShout = false;
	UPROPERTY() UCapsuleComponent* Capsule = nullptr;
	UPROPERTY() USceneComponent* BodyRoot = nullptr;
	UPROPERTY() USceneComponent* HeadPivot = nullptr;
	UPROPERTY() USceneComponent* ArmRPivot = nullptr;
	UPROPERTY() USceneComponent* ArmLPivot = nullptr;
	UPROPERTY() USceneComponent* LegLPivot = nullptr;
	UPROPERTY() USceneComponent* LegRPivot = nullptr;

	ENpcRole Role = ENpcRole::Shopkeeper;
	bool bTalking = false;
	float TalkT = 0.f;        // seconds since the talk (or the shop menu) opened; keepers gesture at its start and now and then after (KeeperGestures)
	bool KeeperGestures() const;
	int32 Visits = 0;
	float HomeYaw = 0.f;
	float AnimT = 0.f;
	TArray<FVector2D> Patrol;
	int32 PatrolIdx = 0;
	float PauseT = 0.f;
	float WalkBlend = 0.f;
	int32 VillagerIdx = 0;
	float HalfH = 92.f;       // capsule half height (smaller for the boy)
	float StuckT = 0.f;
	FName PersonKey;          // (quest givers)
	FNpcLook Look;
	uint8 Folk = 0;
	FString NameOverride, FirstHello;

	// The body (2026-11-18): a skinned Meshy character (NpcBody.h) or the old shapes; jeeb.Npc.Models swaps them live
	void BuildBody();
	bool BuildModel();
	void BuildShapes();
	void ClearBody();
	FLinearColor OwnColour() const;
	UPROPERTY() USkeletalMeshComponent* Body = nullptr;
	const JeebPeople::FModel* ModelDef = nullptr;
	int32 BuiltSetting = -1;
	FVector LastLoc = FVector::ZeroVector;
	float MoveSpeed = 0.f;
	bool bShadowOn = true;

	// (2026-11-18) the round (NpcLife.h) and how they stand on the ground
	TSharedPtr<JeebLife::FRound> Round;
	float Lag = 0.f;              // seconds behind the clock (looks only: held while you talk to them or block their way)
	bool bPoseWalk = false;       // walking this frame (as shown)
	FVector2D PrevAt = FVector2D::ZeroVector;
	FString HintModel; FLinearColor HintTint = FLinearColor(-1.f, 0.f, 0.f); float HintScale = 1.f;
	float FootZ = 0.f; bool bFootSet = false;   // the ground under them as shown (cm), eased
	float TraceOff = 0.f;         // the last trace's height over the drawn ground (steps, bridges, floors), cm
	FVector2D TraceAt = FVector2D(1.0e9f, 1.0e9f);
	float TraceAge = 0.f;
	float TickPlan = -1.f;        // the tick interval set last
	void UpdateTickRate(float NearestPlayerCm);
	float GroundUnder(const FVector2D& P, bool bTrace, float DeltaSeconds);
};

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Npc.generated.h"

class UCapsuleComponent;
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
	QuestGiver    // (2026-11-18) one of Oak Hill's people from Content/Data/QuestsPeople.csv: quests, else a greeting (QuestComponent.h)
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

	ENpcRole GetRole() const { return Role; }
	FString GetNpcName() const;
	bool OffersTrade() const { return Role == ENpcRole::Shopkeeper || Role == ENpcRole::Blacksmith || Role == ENpcRole::Banker; }
	int32 SpeakerIdx() const;   // which storyteller this is (see Lore.h)
	// What they say on this visit, one entry per click
	TArray<FString> MakeConversation(int32* OutLore = nullptr);   // OutLore: the fragment of lore told this time (or untouched)
	void SetTalking(bool bOn) { bTalking = bOn; }

	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY() UCapsuleComponent* Capsule = nullptr;
	UPROPERTY() USceneComponent* BodyRoot = nullptr;
	UPROPERTY() USceneComponent* HeadPivot = nullptr;
	UPROPERTY() USceneComponent* ArmRPivot = nullptr;
	UPROPERTY() USceneComponent* ArmLPivot = nullptr;
	UPROPERTY() USceneComponent* LegLPivot = nullptr;
	UPROPERTY() USceneComponent* LegRPivot = nullptr;

	ENpcRole Role = ENpcRole::Shopkeeper;
	bool bTalking = false;
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
};

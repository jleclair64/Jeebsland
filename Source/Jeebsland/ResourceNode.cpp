#include "ResourceNode.h"
#include "Messages.h"
#include "KnightCharacter.h"
#include "Inventory.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "KnightGameState.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"
#include "JeebTuning.h"

// TEMPORARY (testing, owner 2026-11-06): [Jeebsland.Testing] UnlimitedNodes=True in DefaultGame.ini. Trees, rocks and every other
// resource node still give their yield and XP but never lose a charge, so they never empty, vanish or need to respawn. The server
// does the gathering (FinishGathering runs from its timer), so it decides. Turn it off before real play; remove with the switch.
static bool UnlimitedNodes()
{
	static const bool b = []() { bool v = false; if (GConfig) GConfig->GetBool(TEXT("Jeebsland.Testing"), TEXT("UnlimitedNodes"), v, GGameIni); return v; }();
	return b;
}

AResourceNode::AResourceNode()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AResourceNode::BeginPlay()
{
	Super::BeginPlay();
	Charges = MaxCharges;
}

void AResourceNode::Tell(AActor* Who, float Seconds, const FColor& Color, const FString& Text)
{
	if (USkillComponent* Sk = Who ? Who->FindComponentByClass<USkillComponent>() : nullptr) Sk->Say(Seconds, Color, Text);
	else JeebMsg::Post(Seconds, Color, Text);
}

void AResourceNode::ChargesChanged()
{
	OnChargesChanged(Charges, MaxCharges);
	if (AKnightGameState* GS = GetWorld() ? GetWorld()->GetGameState<AKnightGameState>() : nullptr) GS->SetNodeCharges(GetActorLocation(), Charges, MaxCharges);
}

void AResourceNode::ApplyCharges(int32 N)
{
	if (bLockedNode || HasAuthority()) return;
	const int32 C = N < 0 ? MaxCharges : FMath::Clamp(N, 0, MaxCharges);
	if (C == Charges) return;
	Charges = C;
	OnChargesChanged(Charges, MaxCharges);
}

int32 AResourceNode::RequiredLevel() const
{
	if (Skill == ESkillType::Mining) return JeebItems::MiningLevelFor(ItemType);
	if (Skill == ESkillType::Woodcutting) return JeebItems::WoodcuttingLevelFor(ItemType);
	return 1;
}

bool AResourceNode::StartGathering(AActor* Gatherer)
{
	if (!GEngine || !Gatherer) return false;

	if (Charges <= 0)
	{
		Tell(Gatherer, 2.f, FColor::Orange, EmptyText);
		return true;
	}
	if (bBeingGathered) return true;

	// Level check
	if (USkillComponent* Skills = Gatherer->FindComponentByClass<USkillComponent>())
	{
		const int32 Need = RequiredLevel();
		if (Skills->GetLevel(Skill) < Need)
		{
			Tell(Gatherer, 3.f, FColor::Orange,
				FString::Printf(TEXT("You need %s level %d for this."), *USkillComponent::SkillName(Skill), Need));
			return true;
		}
	}

	// Tool check: the right tool is put in your hand, and better tools work faster
	float TimeMult = 1.f;
	AKnightCharacter* Knight = Cast<AKnightCharacter>(Gatherer);
	if (Knight && !Knight->PrepareTool(Skill, TimeMult)) return true;
	EffectiveSeconds = GatherSeconds * TimeMult;

	bBeingGathered = true;
	CurrentGatherer = Gatherer;
	if (Knight) Knight->BeginGathering(this, Skill);
	// (owner 2026-11-06) One click is about ten swings of the axe or pick, a yield every EffectiveSeconds, unless he moves off (TickGatherReach). A cast of the rod is still one catch.
	WorkLeft = Knight && Skill != ESkillType::Fishing ? JeebTune::V().GatherSwings * Knight->GatherSwingSeconds() : 0.f;
	Tell(Gatherer, EffectiveSeconds, FColor::Yellow, StartText);
	GetWorldTimerManager().SetTimer(GatherTimer, this, &AResourceNode::FinishGathering, EffectiveSeconds, false);
	UE_LOG(LogTemp, Log, TEXT("JEEBGATHER start: %.1f s a yield, %.1f s of work"), EffectiveSeconds, WorkLeft);
	return true;
}

void AResourceNode::CancelGathering()
{
	if (!bBeingGathered) return;

	GetWorldTimerManager().ClearTimer(GatherTimer);
	bBeingGathered = false;
	if (AKnightCharacter* Knight = Cast<AKnightCharacter>(CurrentGatherer.Get()))
	{
		Knight->EndGathering();
	}
	Tell(CurrentGatherer.Get(), 2.f, FColor::Orange, TEXT("You stop."));
}

void AResourceNode::FinishGathering()
{
	bBeingGathered = false;

	AActor* Gatherer = CurrentGatherer.Get();
	AKnightCharacter* Knight = Cast<AKnightCharacter>(Gatherer);
	WorkLeft -= EffectiveSeconds;
	if (!Gatherer || FVector::Dist2D(Gatherer->GetActorLocation(), GetActorLocation()) > ReachRadius() + 150.f)
	{
		if (Knight) Knight->EndGathering();
		Tell(Gatherer, 2.f, FColor::Orange, TEXT("You wandered off and stopped."));
		return;
	}

	bool bPackFull = false;
	if (USkillComponent* Skills = Gatherer->FindComponentByClass<USkillComponent>())
	{
		EItemType Item = ItemType;
		int32 XP = XPReward;
		RollYield(Skills, Item, XP);
		if (!Skills->AddItem(Item, ItemsPerGather))
		{
			// (owner 2026-11-07: he goes on swinging until he moves or the click's work is over. Nothing is won meanwhile, and the node keeps its charge.)
			Tell(Gatherer, 3.f, FColor::Red, TEXT("Your inventory is full! Sell or drop something at the store."));
			bPackFull = true;
		}
		else Skills->AddXP(Skill, XP);
	}

	if (!bPackFull)
	{
		if (!UnlimitedNodes())   // (TEMPORARY, testing: a node that is never used up)
		{
			--Charges;
			ChargesChanged();
		}

		FString Msg = SuccessText;
		if (MaxCharges > 1 && Charges > 0)
		{
			Msg += FString::Printf(TEXT(" (%d left)"), Charges);
		}
		Tell(Gatherer, 2.f, FColor::Green, Msg);
	}

	if (!bPackFull && Charges <= 0)
	{
		GetWorldTimerManager().SetTimer(RespawnTimer, this, &AResourceNode::Respawn, RespawnSeconds, false);
	}

	// Still swinging: the animation never stopped, the next yield is just on its way
	if (Knight && WorkLeft > 0.01f && Charges > 0 && Knight->WorkingOn(this))
	{
		bBeingGathered = true;
		GetWorldTimerManager().SetTimer(GatherTimer, this, &AResourceNode::FinishGathering, EffectiveSeconds, false);
		UE_LOG(LogTemp, Log, TEXT("JEEBGATHER yield, pack full %d, %.1f s of work left"), bPackFull ? 1 : 0, WorkLeft);
	}
	else if (Knight)
	{
		UE_LOG(LogTemp, Log, TEXT("JEEBGATHER the work is over: %.1f s left, %d charges, working on it %d"), WorkLeft, Charges, Knight->WorkingOn(this) ? 1 : 0);
		Knight->EndGathering();
	}
}

void AResourceNode::Respawn()
{
	Charges = MaxCharges;
	ChargesChanged();
}

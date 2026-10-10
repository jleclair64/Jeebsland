#include "ResourceNode.h"
#include "KnightCharacter.h"
#include "Inventory.h"
#include "Engine/Engine.h"
#include "TimerManager.h"

AResourceNode::AResourceNode()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AResourceNode::BeginPlay()
{
	Super::BeginPlay();
	Charges = MaxCharges;
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
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Orange, EmptyText);
		return true;
	}
	if (bBeingGathered) return true;

	// Level check
	if (USkillComponent* Skills = Gatherer->FindComponentByClass<USkillComponent>())
	{
		const int32 Need = RequiredLevel();
		if (Skills->GetLevel(Skill) < Need)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Orange,
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
	GEngine->AddOnScreenDebugMessage(-1, EffectiveSeconds, FColor::Yellow, StartText);
	GetWorldTimerManager().SetTimer(GatherTimer, this, &AResourceNode::FinishGathering, EffectiveSeconds, false);
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
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Orange, TEXT("You stop."));
}

void AResourceNode::FinishGathering()
{
	bBeingGathered = false;

	AActor* Gatherer = CurrentGatherer.Get();
	if (AKnightCharacter* Knight = Cast<AKnightCharacter>(Gatherer))
	{
		Knight->EndGathering();
	}
	if (!Gatherer || FVector::Dist2D(Gatherer->GetActorLocation(), GetActorLocation()) > ReachRadius() + 150.f)
	{
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Orange, TEXT("You wandered off and stopped."));
		return;
	}

	if (USkillComponent* Skills = Gatherer->FindComponentByClass<USkillComponent>())
	{
		EItemType Item = ItemType;
		int32 XP = XPReward;
		RollYield(Skills, Item, XP);
		if (!Skills->AddItem(Item, ItemsPerGather))
		{
			if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, TEXT("Your inventory is full! Sell or drop something at the store."));
			return;
		}
		Skills->AddXP(Skill, XP);
	}

	--Charges;
	OnChargesChanged(Charges, MaxCharges);

	if (GEngine)
	{
		FString Msg = SuccessText;
		if (MaxCharges > 1 && Charges > 0)
		{
			Msg += FString::Printf(TEXT(" (%d left)"), Charges);
		}
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Green, Msg);
	}

	if (Charges <= 0)
	{
		GetWorldTimerManager().SetTimer(RespawnTimer, this, &AResourceNode::Respawn, RespawnSeconds, false);
	}
}

void AResourceNode::Respawn()
{
	Charges = MaxCharges;
	OnChargesChanged(Charges, MaxCharges);
}

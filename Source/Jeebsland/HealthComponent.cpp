#include "HealthComponent.h"
#include "Combat.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UHealthComponent::UHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UHealthComponent, Max);
	DOREPLIFETIME(UHealthComponent, Cur);
}

void UHealthComponent::OnRep_Cur(int32 Old)
{
	if (Cur >= Old || !GetWorld()) return;
	LastHitTime = GetWorld()->GetTimeSeconds();   // (the bar shows for a while after a hit)
	if (OnHitSeen) OnHitSeen();
}

TArray<UHealthComponent*>& UHealthComponent::All()
{
	static TArray<UHealthComponent*> List;
	return List;
}

void UHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	All().AddUnique(this);
}

void UHealthComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	All().Remove(this);
	Super::EndPlay(Reason);
}

FVector UHealthComponent::HitCentre() const
{
	return GetOwner()->GetActorLocation() + FVector(0.f, 0.f, HitHeight);
}

bool UHealthComponent::ShowBar(float Now) const
{
	if (Cur <= 0 || LastHitTime < 0.f) return false;
	return (Now - LastHitTime < 15.f) || (Cur * 2 < Max);
}

int32 UHealthComponent::ApplyDamage(int32 Amount, AActor* From, bool bCrit, bool bHitBoxes, uint8 Zone, bool bHeadshot)
{
	if (Cur <= 0 || Amount <= 0 || !GetOwner() || !GetOwner()->HasAuthority()) return 0;   // (damage is the server's to deal)
	if (ModifyDamage && !bHitBoxes) { Amount = ModifyDamage(Amount, From); if (Amount <= 0) { if (OnDamaged) OnDamaged(From); return 0; } }
	if (ZoneTake && From) Amount = FMath::Max(1, FMath::RoundToInt(Amount * ZoneTake(Zone)));   // (his armour where it landed; hazards with no one behind them skip it)
	const int32 Dealt = FMath::Min(Amount, Cur);
	Cur -= Dealt;
	if (UWorld* W = GetWorld())
	{
		LastHitTime = W->GetTimeSeconds();
		JeebCombat::AddNumber(W, GetOwner()->GetActorLocation() + FVector(0.f, 0.f, BarHeight + 20.f), Amount,
			bHeadshot ? JeebCombat::N_Headshot : bCrit ? JeebCombat::N_Crit : JeebCombat::N_Damage);
	}
	if (OnDamaged) OnDamaged(From);
	return Dealt;
}

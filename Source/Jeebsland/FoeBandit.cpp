// The bandits' minds (2026-11-18): AFoeKnight with a row of Foes.csv (FoeData.h) and a camp (FoeCamps.h). Everything here runs on the server;
// players' machines only draw him from what replicates (where he is, RepAct, bRepBlock, RepKnock, bRepDown), as for the pen knight.
//  - At home he stands by his camp's fire or strolls round it (within StrollR), and looks round four times a second for a player within
//    his Sight in front of him (Behind x that behind him), never one standing in a town.
//  - Noticed, or hit: he fights that player with the pen knight's moves (the raised sword's tell, the cut, the shield now and then). The
//    player who hit him last (within RecentHit s) is the one he fights; with nobody hitting him he keeps to whom he was fighting.
//  - He gives up past his Leash from the camp, when his foe is down, in a town, gone, or out of his sight for LoseAfter s, and walks home,
//    healing fully when he gets there. Hit on the way back while still well inside his leash, he turns and fights again.
//  - He walks over the ground as drawn, steps round trees, rocks, ruins and props (a swept capsule above the ground; no navmesh), and never
//    into a town or water deeper than wading.
//  - Beaten: he falls, his loot drops (Foes.csv Loot, the server's roll), whoever beat him gets his XP and quest credit, he sinks away and
//    stands at camp again after Respawn s.
#include "FoeKnight.h"
#include "FoeData.h"
#include "KnightCharacter.h"
#include "HealthComponent.h"
#include "Loot.h"
#include "QuestComponent.h"
#include "SkillComponent.h"
#include "Inventory.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

namespace
{
	constexpr float StrollR = 600.f;     // cm round the fire he strolls
	constexpr float StrollSpeed = 110.f; // cm/s
	constexpr float RecentHit = 4.f;     // s: a blow this recent makes its giver the one he fights
	constexpr float LoseAfter = 3.f;     // s out of sight (1.6 x Sight away) before he gives up
	constexpr float HomeSpeedMul = 0.75f;// x his chase speed on the way home
	constexpr float RegenShare = 0.08f;  // of his health a second, at home after RegenDelay s without a blow
	constexpr float RegenDelay = 6.f;
}

AKnightCharacter* AFoeKnight::Hitter(AActor* From)
{
	if (!From) return nullptr;
	if (AKnightCharacter* K = Cast<AKnightCharacter>(From)) return K;
	if (AKnightCharacter* K = Cast<AKnightCharacter>(From->GetInstigator())) return K;   // (an arrow: whoever shot it)
	return Cast<AKnightCharacter>(From->GetOwner());
}

void AFoeKnight::SetupFoe(int32 InKind, int32 InCamp, const FVector2D& Spot, const FVector2D& Centre, int32 Seed)
{
	if (!JeebFoes::Foes().IsValidIndex(InKind) || InKind >= 255) return;
	RepKind = uint8(InKind);
	Camp = InCamp;
	CampAt = Centre;
	PenCenter = Spot;
	PenHalf = FVector2D(StrollR, StrollR);
	WanderTo = Spot;
	Rand.Initialize(Seed);
	StepSide = (Seed & 1) ? 1.f : -1.f;
	ApplyKind();
	SetNetCullDistanceSquared(FMath::Square(15000.f));   // (players further than 150 m are never sent him)
}

void AFoeKnight::ApplyKind()
{
	if (RepKind == 255) return;
	if (!JeebFoes::Foes().IsValidIndex(RepKind))
	{
		UE_LOG(LogTemp, Error, TEXT("JEEBFOE %s: kind %d is not in this machine's Foes.csv (does it match the server's?); drawn as the pen knight"), *GetName(), int32(RepKind));
		RepKind = 255;
		return;
	}
	const JeebFoes::FFoeDef& D = JeebFoes::Foes()[RepKind];
	FoeTier = D.Tier;
	FoeShield = D.Shield;
	FoeHitMin = D.HitMin; FoeHitMax = D.HitMax;
	FoeTakes = D.Takes;
	ChaseSpeed = D.Speed;
	WindupT = D.Windup;
	CoolMin = D.CoolMin; CoolMax = D.CoolMax;
	BlockChance = D.Shield >= 0 ? D.Block : 0.f;
	RespawnSecs = D.Respawn;
	if (HasAuthority() && Hp) { Hp->Max = D.Hp; Hp->Cur = D.Hp; }
}

AActor* AFoeKnight::TargetNow() const { return Target.Get(); }   // (tests and logs)

FString AFoeKnight::FoeName() const
{
	return (IsBandit() && JeebFoes::Foes().IsValidIndex(RepKind)) ? JeebFoes::Foes()[RepKind].Name : FString();
}

void AFoeKnight::SetSleeping(bool bSleep)
{
	if (bSleeping == bSleep) return;
	bSleeping = bSleep;
	SetActorTickInterval(bSleep ? 1.f : 0.f);
	SetNetUpdateFrequency(bSleep ? 1.f : 30.f);
	if (!bSleep) ForceNetUpdate();
}

void AFoeKnight::SetMind(EMind M, const TCHAR* Why)
{
	LostT = 0.f; StuckT = 0.f;
	if (Mind == M) return;
	Mind = M;
	static const TCHAR* const Names[] = { TEXT("home"), TEXT("fight"), TEXT("return") };
	UE_LOG(LogTemp, Log, TEXT("JEEBFOE %s (%s, camp %d) %s -> %s%s%s, %.0f m from camp, health %d/%d"), *FoeName(), *GetName(), Camp, Why, Names[uint8(M)],
		Target.IsValid() ? TEXT(", foe ") : TEXT(""), Target.IsValid() ? *Target->GetName() : TEXT(""),
		FVector2D::Distance(FVector2D(GetActorLocation()), CampAt) / 100.f, Hp ? Hp->Cur : 0, Hp ? Hp->Max : 0);
}

// Whom he would notice now: the nearest player standing within his sight (less behind him), outside every town
static AKnightCharacter* NoticeSomeone(const AFoeKnight* Me, const JeebFoes::FFoeDef& D, const FVector2D& CampAt)
{
	AKnightCharacter* Best = nullptr;
	float BestD = 1.0e9f;
	const FVector2D At(Me->GetActorLocation());
	const FVector2D Fwd(Me->GetActorForwardVector().GetSafeNormal2D());
	for (TActorIterator<AKnightCharacter> It(Me->GetWorld()); It; ++It)
	{
		AKnightCharacter* K = *It;
		if (K->GetHealth() <= 0.f || K->IsHidden()) continue;
		const FVector2D P(K->GetActorLocation());
		const float Dd = FVector2D::Distance(P, At);
		if (Dd > D.Sight || Dd >= BestD) continue;
		const bool bFront = FVector2D::DotProduct(Fwd, (P - At) / FMath::Max(Dd, 1.f)) > -0.17f;   // (about 100 degrees either side counts as in front)
		if (!bFront && Dd > D.Sight * D.Behind) continue;
		if (FVector2D::Distance(P, CampAt) > D.Leash + D.Sight) continue;   // (never one he would give up on straight away)
		if (JeebFoes::InTown(P.X, P.Y)) continue;
		Best = K; BestD = Dd;
	}
	return Best;
}

AKnightCharacter* AFoeKnight::BanditTarget(float& OutDist, float Dt)
{
	OutDist = 1.0e9f;
	if (Mind == EMind::Return) return nullptr;
	const JeebFoes::FFoeDef& D = JeebFoes::Foes()[FoeKind()];
	const float Now = GetWorld()->GetTimeSeconds();
	const FVector2D At(GetActorLocation());
	auto Fit = [](const AKnightCharacter* K) { return K && K->GetHealth() > 0.f && !K->IsHidden() && !JeebFoes::InTown(K->GetActorLocation().X, K->GetActorLocation().Y); };

	if (Mind == EMind::Home)
	{
		SenseT -= Dt;
		if (SenseT > 0.f) return nullptr;
		SenseT = 0.25f;
		AKnightCharacter* K = NoticeSomeone(this, D, CampAt);
		if (!K) return nullptr;
		Target = K;
		SetMind(EMind::Fight, TEXT("notices a player"));
		AttackCool = FMath::Max(AttackCool, 0.6f);
		OutDist = FVector2D::Distance(FVector2D(K->GetActorLocation()), At);
		return K;
	}

	// Fighting
	if (FVector2D::Distance(At, CampAt) > D.Leash) { SetMind(EMind::Return, TEXT("is past his leash")); Target = nullptr; return nullptr; }
	AKnightCharacter* T = Target.Get();
	if (!Fit(T)) T = nullptr;
	AKnightCharacter* A = LastAttacker.Get();
	if (A && A != T && Now - LastAttackT < RecentHit && Fit(A))   // the one hitting him now
	{
		T = A;
		UE_LOG(LogTemp, Log, TEXT("JEEBFOE %s (%s) turns on %s, who hit him last"), *FoeName(), *GetName(), *A->GetName());
	}
	if (!T)
	{
		SenseT -= Dt;
		if (SenseT <= 0.f) { SenseT = 0.25f; T = NoticeSomeone(this, D, CampAt); }   // anyone else near?
		if (!T) { const bool bDown = Target.IsValid() && Target->GetHealth() <= 0.f; Target = nullptr; SetMind(EMind::Return, bDown ? TEXT("beat his foe") : TEXT("lost his foe")); return nullptr; }
	}
	Target = T;
	const float Dd = FVector2D::Distance(FVector2D(T->GetActorLocation()), At);
	LostT = Dd > D.Sight * 1.6f ? LostT + Dt : 0.f;
	if (LostT > LoseAfter) { Target = nullptr; SetMind(EMind::Return, TEXT("lost sight of his foe")); return nullptr; }
	OutDist = Dd;
	return T;
}

void AFoeKnight::BanditRoam(float Dt, float& Yaw)
{
	const JeebFoes::FFoeDef& D = JeebFoes::Foes()[FoeKind()];
	const FVector2D At(GetActorLocation());
	if (Mind == EMind::Fight) return;   // (fighting with nobody to fight this frame: BanditTarget sends him home next)
	if (Mind == EMind::Return)
	{
		const FVector2D Home = PenCenter;
		if (FVector2D::Distance(At, Home) < 60.f || StuckT > 8.f)
		{
			if (StuckT > 8.f) SetActorLocation(FVector(Home.X, Home.Y, FloorZ(Home.X, Home.Y)));   // (wedged somewhere on the way: back at camp)
			Hp->Reset(); GuardPool = GuardMax; bGuardBroken = false; BlockT = -1.f;
			SetMind(EMind::Home, StuckT > 8.f ? TEXT("was stuck; put back at camp, healed") : TEXT("is home and heals"));
			WanderWait = Rand.FRandRange(2.f, 5.f);
			return;
		}
		const float Before = FVector2D::Distance(At, Home);
		MoveTowards(Home, ChaseSpeed * HomeSpeedMul, Dt);
		const float After = FVector2D::Distance(FVector2D(GetActorLocation()), Home);
		StuckT = (Before - After < ChaseSpeed * HomeSpeedMul * Dt * 0.2f) ? StuckT + Dt : FMath::Max(0.f, StuckT - Dt);
		const FVector2D W = Home - At;
		if (!W.IsNearlyZero()) Yaw = FMath::FixedTurn(Yaw, FMath::RadiansToDegrees(FMath::Atan2(W.Y, W.X)), 360.f * Dt);
		return;
	}
	// At home: his health comes back after a quiet while
	const float Now = GetWorld()->GetTimeSeconds();
	if (Hp->Cur < Hp->Max && Now - LastHurtT > RegenDelay)
	{
		RegenAcc += Hp->Max * RegenShare * Dt;
		const int32 Add = FMath::FloorToInt(RegenAcc);
		if (Add > 0) { RegenAcc -= Add; Hp->Cur = FMath::Min(Hp->Max, Hp->Cur + Add); }
	}
	// Stroll round the fire: a few steps somewhere near it, a wait, now and then back to his own spot
	WanderWait -= Dt;
	if (WanderWait > 0.f) return;
	if (MoveTowards(WanderTo, StrollSpeed, Dt) || StuckT > 3.f)
	{
		StuckT = 0.f;
		const float A = Rand.FRandRange(0.f, 2.f * PI), R = Rand.FRandRange(250.f, StrollR);
		WanderTo = Rand.FRand() < 0.35f ? PenCenter : CampAt + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R;
		WanderWait = Rand.FRandRange(3.f, 8.f);
		return;
	}
	const FVector2D Before = At;
	const FVector2D W = WanderTo - At;
	if (!W.IsNearlyZero()) Yaw = FMath::FixedTurn(Yaw, FMath::RadiansToDegrees(FMath::Atan2(W.Y, W.X)), 180.f * Dt);
	StuckT = FVector2D::Distance(FVector2D(GetActorLocation()), Before) < StrollSpeed * Dt * 0.2f ? StuckT + Dt : 0.f;
	(void)D;
}

bool AFoeKnight::BanditMove(const FVector2D& To, float Speed, float Dt)
{
	const FVector Me = GetActorLocation();
	const FVector2D At(Me);
	const FVector2D Dv = To - At;
	const float Len = Dv.Size();
	if (Len < 8.f) return true;
	const float StepLen = FMath::Min(Len, Speed * Dt);
	const FVector2D Dir = Dv / Len;
	// A capsule well above the ground (so slopes never catch it) sweeps the step: trunks, rocks, logs' tops, ruins, props, players, other foes
	FCollisionObjectQueryParams Obj;
	Obj.AddObjectTypesToQuery(ECC_WorldStatic);
	Obj.AddObjectTypesToQuery(ECC_WorldDynamic);
	Obj.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Q(SCENE_QUERY_STAT(FoeStep), false, this);
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(30.f, 45.f);
	const bool bInTown = JeebFoes::InTown(At.X, At.Y);
	static const float Turns[] = { 0.f, 35.f, 70.f, 110.f };
	for (int32 i = 0; i < 7; ++i)
	{
		const float Ang = (i == 0) ? 0.f : Turns[(i + 1) / 2] * ((i & 1) ? StepSide : -StepSide);
		const FVector2D Dd = Dir.GetRotated(Ang);
		const FVector2D N = At + Dd * StepLen;
		if (!bInTown && JeebFoes::InTown(N.X, N.Y)) continue;      // never into a town (one already in one may walk out)
		if (JeebFoes::DeepWater(N.X, N.Y) && !JeebFoes::DeepWater(At.X, At.Y)) continue;   // nor into water deeper than wading
		const FVector From(At.X, At.Y, JeebFoes::Ground(At.X, At.Y) + 115.f), Dest(N.X, N.Y, JeebFoes::Ground(N.X, N.Y) + 115.f);
		FHitResult Hit;
		if (GetWorld()->SweepSingleByObjectType(Hit, From, Dest, FQuat::Identity, Obj, Shape, Q) && !Hit.bStartPenetrating) continue;
		if (i >= 2 && i % 2 == 0) StepSide = -StepSide;   // (the other way round got him through: keep stepping that way)
		SetActorLocation(FVector(N.X, N.Y, FloorZ(N.X, N.Y)));
		return false;
	}
	StepSide = -StepSide;   // (walled in this way: try the other side next frame)
	return false;
}

void AFoeKnight::BanditHurt(AActor* From)
{
	const float Now = GetWorld()->GetTimeSeconds();
	LastHurtT = Now;
	AKnightCharacter* K = Hitter(From);
	if (!K || DeathT > 0.f) return;
	LastAttacker = K;
	LastAttackT = Now;
	const JeebFoes::FFoeDef& D = JeebFoes::Foes()[FoeKind()];
	if (Mind == EMind::Home) { Target = K; SetMind(EMind::Fight, TEXT("is hit")); }
	else if (Mind == EMind::Return && FVector2D::Distance(FVector2D(GetActorLocation()), CampAt) < D.Leash * 0.8f) { Target = K; SetMind(EMind::Fight, TEXT("is hit on the way home and turns")); }
}

void AFoeKnight::BanditBeaten(AActor* From)
{
	const JeebFoes::FFoeDef& D = JeebFoes::Foes()[FoeKind()];
	AKnightCharacter* K = Hitter(From);
	Target = nullptr; LastAttacker = nullptr; DownFor = 0.f;
	Mind = EMind::Home;
	// Loot: the server's roll from Foes.csv; ingots in a purse where he fell, each item beside it
	TArray<FInvSlot> Items;
	int32 Ingots = 0;
	JeebFoes::RollLoot(D, Rand, Items, Ingots);
	const FVector Floor = GetActorLocation() - FVector(0.f, 0.f, 90.f);
	if (Ingots > 0)
		if (ALootDrop* Drop = GetWorld()->SpawnActor<ALootDrop>(Floor, FRotator::ZeroRotator)) Drop->Setup(ELootLook::Item, TArray<FInvSlot>(), Ingots);
	FString What = FString::Printf(TEXT("%d ingots"), Ingots);
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const float A = 2.f * PI * (i + 0.5f) / FMath::Max(1, Items.Num());
		const FVector2D P = FVector2D(Floor) + FVector2D(FMath::Cos(A), FMath::Sin(A)) * 80.f;
		if (ALootDrop* Drop = GetWorld()->SpawnActor<ALootDrop>(FVector(P.X, P.Y, JeebFoes::Ground(P.X, P.Y)), FRotator::ZeroRotator))
			Drop->Setup(ELootLook::Item, TArray<FInvSlot>{ Items[i] }, 0);
		What += FString::Printf(TEXT(", %d %s"), Items[i].Count, *JeebItems::ItemName(Items[i].Id));
	}
	// XP and a word to whoever beat him (the XP for every blow came with the blows)
	if (USkillComponent* Sk = K ? K->FindComponentByClass<USkillComponent>() : nullptr)
	{
		if (D.XP > 0) Sk->AddXP(ESkillType::Attack, D.XP);
		Sk->Say(4.f, FColor(255, 210, 120), D.XP > 0 ? FString::Printf(TEXT("You beat %s! (+%d Attack XP)"), *D.Name, D.XP) : FString::Printf(TEXT("You beat %s!"), *D.Name));
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBFOE %s (%s, camp %d) beaten by %s at %.0f %.0f: drops %s; +%d Attack XP; back in %.0f s"), *D.Name, *GetName(), Camp,
		K ? *K->GetName() : TEXT("nobody"), GetActorLocation().X, GetActorLocation().Y, *What, K ? D.XP : 0, RespawnSecs);
	// Quests: a kill step may name him (his Key) or any of his kind (his Group)
	AActor* By = K ? (AActor*)K : From;
	UQuestComponent::OnFoeBeaten(By, *D.Key.ToString());
	if (!D.Group.IsNone() && D.Group != D.Key) UQuestComponent::OnFoeBeaten(By, *D.Group.ToString());
}

void AFoeKnight::BanditDownTick(float Dt)
{
	DownFor += Dt;
	RespawnT -= Dt;
	if (DownFor > 4.5f && DownFor < 6.f) SetActorLocation(GetActorLocation() - FVector(0.f, 0.f, 50.f * FMath::Min(Dt, 6.f - DownFor + Dt)));   // sinks into the ground ...
	if (DownFor >= 6.f && !IsHidden()) SetActorHiddenInGame(true);   // ... and is gone
	if (RespawnT <= 0.f) Rise();
}

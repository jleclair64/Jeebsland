#include "Guards.h"
#include "KnightCharacter.h"
#include "SkillComponent.h"
#include "Inventory.h"
#include "Npc.h"
#include "KnightAnimator.h"
#include "KnightDesign.h"
#include "WeaponDesign.h"
#include "JeebProps.h"
#include "JeebTuning.h"
#include "OakHill.h"
#include "FoeData.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogJeebGuard, Log, All);

static TAutoConsoleVariable<int32> CVarGuards(TEXT("jeeb.Guards"), 1, TEXT("Oak Hill's town guards (Guards.h): 1 on, 0 left out. Server, read when the world is built."), ECVF_Default);

double ATownGuard::CostUs = 0.0;
int32 ATownGuard::CostTicks = 0;
double ATownGuard::CostLogAt = 0.0;

namespace
{
	const JeebTune::FValues& T() { return JeebTune::V(); }

	FString Who(const AKnightCharacter* K)
	{
		const APlayerState* PS = K ? K->GetPlayerState() : nullptr;
		return PS ? PS->GetPlayerName() : (K ? K->GetName() : FString(TEXT("?")));
	}

	// (server) what each player took from houses lately, and their grace after a catch
	struct FTheft
	{
		struct FGot { int32 Id = 0; int32 Count = 0; double At = 0.0; };
		TArray<FGot> Items;   // Id 0: ingots
		double GraceUntil = -1.0;
	};
	TMap<TWeakObjectPtr<AKnightCharacter>, FTheft>& Thefts() { static TMap<TWeakObjectPtr<AKnightCharacter>, FTheft> M; return M; }

	bool InOakHill(const FVector& P) { return JeebOakHill::Enabled() && JeebOakHill::InBox(P.X, P.Y); }

	constexpr float HalfH = 96.f;

	const TCHAR* const ShoutLines[] = { TEXT("Thief! Guards, guards!"), TEXT("Hey! Stop, thief!"), TEXT("Help! A thief in the house!"), TEXT("Hands off! Guards!") };
}

// ------------------------------------------------------------------ the server's side

namespace JeebGuards
{
	void SpawnAll(UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client || !JeebOakHill::Enabled() || CVarGuards.GetValueOnGameThread() == 0) return;
		const int32 N = FMath::Clamp(FMath::RoundToInt(T().GuardCount), 0, 12);
		// Beats: the plaza's ring road, then the longest lanes (only their part inside Oak Hill)
		struct FB { TArray<FVector2D> Pts; bool bLoop; float Len; };
		TArray<FB> Beats;
		for (const JeebOakHill::FRoad& R : JeebOakHill::Roads())
		{
			FB B; B.bLoop = R.bClosed; B.Len = 0.f;
			for (const FVector2D& P : R.Pts) if (JeebOakHill::InBox(P.X, P.Y, -500.f)) { if (B.Pts.Num()) B.Len += FVector2D::Distance(B.Pts.Last(), P); B.Pts.Add(P); }
			if (B.Pts.Num() >= 2 && B.Len > 1500.f) Beats.Add(B);
		}
		Beats.Sort([](const FB& A, const FB& B) { return A.bLoop != B.bLoop ? A.bLoop : A.Len > B.Len; });
		if (Beats.Num() == 0) { UE_LOG(LogJeebGuard, Warning, TEXT("JEEBGUARD no roads in Oak Hill: no guards")); return; }
		for (int32 i = 0; i < N; ++i)
		{
			const FB& B = Beats[i % Beats.Num()];
			const int32 Start = (B.Pts.Num() / 2 + i * 3) % B.Pts.Num();
			const FVector2D P = B.Pts[Start];
			ATownGuard* G = World->SpawnActorDeferred<ATownGuard>(ATownGuard::StaticClass(), FTransform::Identity);
			if (!G) continue;
			G->SetBeat(B.Pts, B.bLoop, i);
			G->FinishSpawning(FTransform(FRotator::ZeroRotator, FVector(P.X, P.Y, JeebFoes::Ground(P.X, P.Y) + HalfH + 2.f)));
			UE_LOG(LogJeebGuard, Log, TEXT("JEEBGUARD guard %d on a %s of %d points, %.0f m, at %.0f %.0f"), i, B.bLoop ? TEXT("loop") : TEXT("lane"), B.Pts.Num(), B.Len / 100.f, P.X, P.Y);
		}
	}

	void NoteStolen(AKnightCharacter* Thief, const TArray<FInvSlot>& Items, int32 Ingots)
	{
		if (!Thief || !Thief->HasAuthority()) return;
		FTheft& Th = Thefts().FindOrAdd(Thief);
		const double Now = Thief->GetWorld()->GetTimeSeconds();
		Th.Items.RemoveAll([&](const FTheft::FGot& G) { return Now - G.At > T().GuardStolenMemory; });
		if (Ingots > 0) Th.Items.Add({ 0, Ingots, Now });
		for (const FInvSlot& S : Items) if (S.Id && S.Count > 0) Th.Items.Add({ S.Id, S.Count, Now });
	}

	void RaiseAlarm(AKnightCharacter* Thief, ANpc* Heard)
	{
		if (!Thief || !Thief->HasAuthority()) return;
		UWorld* W = Thief->GetWorld();
		const double Now = W->GetTimeSeconds();
		ATownGuard* Any = nullptr;
		ATownGuard* Best = nullptr;
		float BestD = 1e12f;
		bool bAlready = false;
		for (TActorIterator<ATownGuard> It(W); It; ++It)
		{
			if (!Any) Any = *It;
			if (It->ChaseTarget() == Thief) bAlready = true;
			if (!It->IsFree()) continue;
			const float D = FVector::DistSquared2D(It->GetActorLocation(), Thief->GetActorLocation());
			if (D < BestD) { BestD = D; Best = *It; }
		}
		if (Heard && Any)
		{
			const FString Line = ShoutLines[(GetTypeHash(Heard->GetNpcName()) + uint32(Now)) % UE_ARRAY_COUNT(ShoutLines)];
			Any->MulticastAlarm(Heard->GetNpcName(), Heard->GetActorLocation(), Thief->GetActorLocation(), Line);
		}
		USkillComponent* Sk = Thief->GetSkills();
		if (!InOakHill(Thief->GetActorLocation())) { UE_LOG(LogJeebGuard, Log, TEXT("JEEBGUARD alarm for %s: outside Oak Hill, no guard"), *Who(Thief)); return; }
		if (const FTheft* Th = Thefts().Find(Thief); Th && Now < Th->GraceUntil) { UE_LOG(LogJeebGuard, Log, TEXT("JEEBGUARD alarm for %s: in grace, no guard"), *Who(Thief)); return; }
		if (bAlready) { UE_LOG(LogJeebGuard, Log, TEXT("JEEBGUARD alarm for %s: already chased"), *Who(Thief)); return; }
		if (!Best) { UE_LOG(LogJeebGuard, Log, TEXT("JEEBGUARD alarm for %s: every guard busy"), *Who(Thief)); return; }
		if (Best->StartChase(Thief) && Sk) Sk->Say(4.f, FColor::Red, TEXT("A town guard is coming for you! Run out of town, or hide."));
	}

	bool IsChased(const AKnightCharacter* K)
	{
		if (!K) return false;
		for (TActorIterator<ATownGuard> It(K->GetWorld()); It; ++It) if (It->Mode() == ATownGuard::EMode::Chase && It->ChaseTarget() == K) return true;
		return false;
	}
}

// ------------------------------------------------------------------ the guard

ATownGuard::ATownGuard()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;   // (three of them; the alarm's shout travels on them)
	SetReplicateMovement(true);
	SetNetUpdateFrequency(20.f);

	// No collision with anything: nobody can block or trap him, and no blow, arrow or spell can touch him
	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(34.f, HalfH);
	Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetRootComponent(Capsule);
	WristR = CreateDefaultSubobject<USceneComponent>(TEXT("WristR"));
	WristR->SetupAttachment(Capsule);
}

void ATownGuard::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATownGuard, RepMode);
	DOREPLIFETIME(ATownGuard, RepTarget);
}

void ATownGuard::BeginPlay()
{
	Super::BeginPlay();
	LastLoc = GetActorLocation();
	FeetZ = GetActorLocation().Z - HalfH;
	Yaw = GetActorRotation().Yaw;
	if (IsNetMode(NM_DedicatedServer)) return;   // (nothing to draw, and he has no hurt boxes to pose)
	Anim = MakeShared<FKnightAnimator>();
	constexpr int32 Tier = 1;   // iron
	if (Anim->MakeMesh(this, GetRootComponent()))
	{
		for (int32 p = 0; p < JeebAnim::Piece_Count; ++p) Anim->Wear(p, Tier);
		Anim->SetShade(JeebAnim::Piece_Chest, FLinearColor(0.32f, 0.48f, 1.15f));   // Oak Hill's blue (bandits wear red)
		Anim->SetShade(JeebAnim::Piece_Legs, FLinearColor(0.55f, 0.6f, 0.8f));
	}
	if (UStaticMeshComponent* S = JeebProps::Make(this, WristR, JeebItems::K_Sword, Tier)) S->SetRelativeTransform(JeebProps::FistPlace());
}

float ATownGuard::HeadZ() const { return GetActorLocation().Z + HalfH + 10.f; }

FString ATownGuard::SayingNow(float& OutAlpha) const
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now >= SayUntil) { OutAlpha = 0.f; return FString(); }
	OutAlpha = FMath::Clamp(float(SayUntil - Now) / 0.4f, 0.f, 1.f);
	return SayLine;
}

void ATownGuard::MulticastSay_Implementation(const FString& Line)
{
	SayLine = Line;
	SayUntil = GetWorld()->GetTimeSeconds() + 3.0;
	if (!IsNetMode(NM_DedicatedServer)) UE_LOG(LogJeebGuard, Log, TEXT("JEEBGUARDSEE guard %s says \"%s\""), *GetName(), *Line);
}

void ATownGuard::MulticastAlarm_Implementation(const FString& NpcName, FVector_NetQuantize NpcAt, FVector_NetQuantize ThiefAt, const FString& Line)
{
	if (IsNetMode(NM_DedicatedServer)) return;
	UWorld* W = GetWorld();
	ANpc* Shouter = nullptr;
	float BestD = 1500.f * 1500.f;
	for (TActorIterator<ANpc> It(W); It; ++It)
	{
		if (It->GetNpcName() != NpcName) continue;
		const float D = FVector::DistSquared2D(It->GetActorLocation(), NpcAt);
		if (D < BestD) { BestD = D; Shouter = *It; }
	}
	int32 Noticed = 0;
	const FVector Mid = Shouter ? Shouter->GetActorLocation() : FVector(NpcAt);
	const float R = T().GuardNoticeRange;
	for (TActorIterator<ANpc> It(W); It; ++It)
	{
		if (*It == Shouter || FVector::Dist2D(It->GetActorLocation(), Mid) > R) continue;
		It->Alarm(ThiefAt, T().GuardNoticeSeconds * (0.8f + 0.4f * FMath::FRand()), 0.f, FString(), false);
		++Noticed;
	}
	if (Shouter) Shouter->Alarm(ThiefAt, T().GuardNoticeSeconds, T().GuardShoutSeconds, Line, true);
	UE_LOG(LogJeebGuard, Log, TEXT("JEEBGUARDSEE alarm: %s %s \"%s\", %d townsfolk notice"), *NpcName, Shouter ? TEXT("shouts") : TEXT("(not found here)"), *Line, Noticed);
}

bool ATownGuard::StartChase(AKnightCharacter* K)
{
	if (!HasAuthority() || !K || !IsFree()) return false;
	Thief = K;
	RepTarget = K;
	LastSeen = K->GetActorLocation();
	ChaseT = OutsideT = HiddenT = BlockedT = 0.f;
	SetMode(EMode::Chase);
	MulticastSay(TEXT("Stop, thief!"));
	UE_LOG(LogJeebGuard, Log, TEXT("JEEBGUARD guard %d chases %s, %.1f m away"), GuardIdx, *Who(K), FVector::Dist2D(K->GetActorLocation(), GetActorLocation()) / 100.f);
	return true;
}

void ATownGuard::EndChase(const TCHAR* Why, bool bCaught)
{
	AKnightCharacter* K = Thief.Get();
	UE_LOG(LogJeebGuard, Log, TEXT("JEEBGUARD guard %d ends the chase of %s after %.1f s: %s"), GuardIdx, *Who(K), ChaseT, Why);
	if (!bCaught)
	{
		MulticastSay(TEXT("Where did they go?"));
		if (K && K->GetSkills()) K->GetSkills()->Say(3.f, FColor::Green, TEXT("The guard has lost you."));
	}
	Thief = nullptr;
	RepTarget = nullptr;
	SetMode(EMode::Return);
	// back to the nearest point of his beat
	float Best = 1e12f;
	for (int32 i = 0; i < Beat.Num(); ++i) { const float D = FVector2D::DistSquared(Beat[i], FVector2D(GetActorLocation())); if (D < Best) { Best = D; BeatAt = i; } }
}

void ATownGuard::Catch(AKnightCharacter* K)
{
	USkillComponent* Sk = K->GetSkills();
	if (!Sk) { EndChase(TEXT("no skills"), true); return; }
	const double Now = GetWorld()->GetTimeSeconds();
	const int32 Before = Sk->GetIngots();
	TArray<FString> Took;
	int32 IngotsBack = 0;
	FTheft& Th = Thefts().FindOrAdd(K);
	for (const FTheft::FGot& G : Th.Items)
	{
		if (Now - G.At > T().GuardStolenMemory) continue;
		if (G.Id == 0) { IngotsBack += G.Count; continue; }
		const int32 N = FMath::Min(G.Count, Sk->CountItem(G.Id));
		if (N > 0 && Sk->RemoveItems(G.Id, N)) Took.Add(N > 1 ? FString::Printf(TEXT("%d %s"), N, *JeebItems::ItemName(G.Id)) : JeebItems::ItemName(G.Id));
	}
	IngotsBack = FMath::Min(IngotsBack, Sk->GetIngots());
	if (IngotsBack > 0) { Sk->SpendIngots(IngotsBack); Took.Add(FString::Printf(TEXT("%d ingots"), IngotsBack)); }
	const int32 Fine = FMath::Min(FMath::RoundToInt(T().GuardFine), Sk->GetIngots());
	if (Fine > 0) Sk->SpendIngots(Fine);
	Th.Items.Empty();
	Th.GraceUntil = Now + T().GuardGraceSeconds;
	if (T().GuardKnockDown > 0.5f) K->KnockDownBy(this);
	const FString Back = Took.Num() ? FString::Printf(TEXT(" He takes back what you stole (%s)"), *FString::Join(Took, TEXT(", "))) : FString(TEXT(" You had nothing stolen on you"));
	Sk->Say(6.f, FColor::Red, FString::Printf(TEXT("The town guard catches you!%s and fines you %d ingots."), *Back, Fine));
	MulticastSay(TEXT("Got you, thief!"));
	UE_LOG(LogJeebGuard, Log, TEXT("JEEBGUARD guard %d caught %s: took back [%s], fine %d, ingots %d -> %d"), GuardIdx, *Who(K), *FString::Join(Took, TEXT(", ")), Fine, Before, Sk->GetIngots());
	EndChase(TEXT("caught"), true);
}

float ATownGuard::FloorAt(const FVector2D& P)
{
	// whatever solid floor is under him near his feet (streets, steps, house floors), else the ground as drawn
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(GuardFloor), false, this);
	FCollisionObjectQueryParams Obj(ECC_WorldStatic);
	const FVector From(P.X, P.Y, FeetZ + 120.f), To(P.X, P.Y, FeetZ - 300.f);
	if (GetWorld()->LineTraceSingleByObjectType(Hit, From, To, Obj, Q)) return Hit.ImpactPoint.Z;
	return JeebFoes::Ground(P.X, P.Y);
}

bool ATownGuard::Step(const FVector2D& To, float Speed, float Dt)
{
	const FVector2D At(GetActorLocation());
	const FVector2D Dv = To - At;
	const float Len = Dv.Size();
	if (Len < 25.f) return true;
	const float StepLen = FMath::Min(Len, Speed * Dt);
	const FVector2D Dir = Dv / Len;
	FCollisionObjectQueryParams Obj;
	Obj.AddObjectTypesToQuery(ECC_WorldStatic);
	Obj.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Q(SCENE_QUERY_STAT(GuardStep), false, this);
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(28.f, 40.f);
	const float Side = (GuardIdx & 1) ? 1.f : -1.f;
	static const float Turns[] = { 0.f, 35.f, -35.f, 70.f, -70.f };
	FVector2D Go = FVector2D::ZeroVector;
	for (float A : Turns)
	{
		const FVector2D D = Dir.GetRotated(A * Side);
		const FVector2D N = At + D * StepLen;
		if (!JeebOakHill::InBox(N.X, N.Y, 500.f)) continue;   // never out of Oak Hill
		const FVector From(At.X, At.Y, FeetZ + 110.f), Dest(N.X, N.Y, FeetZ + 110.f);
		FHitResult Hit;
		if (GetWorld()->SweepSingleByObjectType(Hit, From, Dest, FQuat::Identity, Obj, Shape, Q) && !Hit.bStartPenetrating) continue;
		Go = D;
		break;
	}
	if (Go.IsZero())
	{
		BlockedT += Dt;
		if (BlockedT < 1.f) return false;
		if (!JeebOakHill::InBox(At.X + Dir.X * StepLen, At.Y + Dir.Y * StepLen, 500.f)) return false;
		Go = Dir;   // (walled in for a second: he steps through, so nothing can trap him)
	}
	else BlockedT = 0.f;
	const FVector2D N = At + Go * StepLen;
	const float Floor = FloorAt(N);
	FeetZ = FMath::Abs(Floor - FeetZ) > 150.f ? Floor : FMath::FInterpTo(FeetZ, Floor, Dt, 14.f);
	SetActorLocation(FVector(N.X, N.Y, FeetZ + HalfH));
	Yaw = FMath::FixedTurn(Yaw, FMath::RadiansToDegrees(FMath::Atan2(Go.Y, Go.X)), 540.f * Dt);
	SetActorRotation(FRotator(0.f, Yaw, 0.f));
	return false;
}

#if !UE_BUILD_SHIPPING
namespace { void GuardTest(ATownGuard* G, float Dt); }
#endif

void ATownGuard::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double T0 = FPlatformTime::Seconds();
	const float Dt = FMath::Min(DeltaSeconds, 0.1f);
	if (HasAuthority())
	{
		switch (Mode())
		{
		case EMode::Patrol:
			if (BeatWait > 0.f) { BeatWait -= Dt; break; }
			if (Beat.Num() > 0 && Step(Beat[BeatAt], T().GuardWalkSpeed, Dt))
			{
				const int32 Next = BeatAt + BeatDir;
				if (bBeatLoop) BeatAt = (Next + Beat.Num()) % Beat.Num();
				else if (Next < 0 || Next >= Beat.Num()) { BeatDir = -BeatDir; BeatAt += BeatDir; BeatWait = 3.f; }
				else BeatAt = Next;
				if (FMath::FRand() < 0.08f) BeatWait = 2.f + 2.f * FMath::FRand();   // (now and then a look round)
			}
			break;
		case EMode::Return:
			if (Beat.Num() == 0 || Step(Beat[BeatAt], T().GuardWalkSpeed * 1.4f, Dt)) SetMode(EMode::Patrol);
			break;
		case EMode::Chase:
		{
			AKnightCharacter* K = Thief.Get();
			if (!K || !K->GetController() || K->Health <= 0.f) { EndChase(TEXT("thief gone (logged out or fallen)"), false); break; }
			ChaseT += Dt;
			const FVector KAt = K->GetActorLocation();
			if (FVector::Dist2D(KAt, LastSeen) > 3000.f && HiddenT == 0.f) { EndChase(TEXT("thief travelled"), false); break; }
			if (ChaseT > T().GuardChaseMax) { EndChase(TEXT("out of time"), false); break; }
			const bool bIn = InOakHill(KAt);
			OutsideT = bIn ? 0.f : OutsideT + Dt;
			if (OutsideT > T().GuardLoseOutside) { EndChase(TEXT("thief left town"), false); break; }
			SeeT -= Dt;
			if (SeeT <= 0.f)
			{
				SeeT = 0.2f;
				const float Dist = FVector::Dist(KAt, GetActorLocation());
				FCollisionQueryParams Q(SCENE_QUERY_STAT(GuardSee), false, this);
				Q.AddIgnoredActor(K);
				const bool bLos = !GetWorld()->LineTraceTestByChannel(GetActorLocation() + FVector(0, 0, 70.f), KAt + FVector(0, 0, 40.f), ECC_Visibility, Q);
				const bool bOutOfSight = K->IsInvisible() || (!bLos && (K->IsCrouchedNow() || Dist > T().GuardSightClose));
				HiddenT = bOutOfSight ? HiddenT + 0.2f : 0.f;
				if (!bOutOfSight) LastSeen = KAt;
			}
			if (HiddenT > T().GuardLoseHidden) { EndChase(TEXT("thief hidden"), false); break; }
			if (bIn && FVector::Dist2D(KAt, GetActorLocation()) < T().GuardCatchReach && FMath::Abs(KAt.Z - GetActorLocation().Z) < 200.f) { Catch(K); break; }
			Step(FVector2D(LastSeen), T().GuardChaseSpeed, Dt);
			break;
		}
		}
	}
	TickBody(DeltaSeconds);
#if !UE_BUILD_SHIPPING
	GuardTest(this, DeltaSeconds);
#endif
	CostUs += (FPlatformTime::Seconds() - T0) * 1e6;
	++CostTicks;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now > CostLogAt + 10.0)
	{
		if (CostTicks > 0) UE_LOG(LogJeebGuard, Log, TEXT("JEEBGUARDCOST %s: guards' ticks %.0f us per tick, %.1f us per frame (avg over %d ticks), frame %.2f ms"),
			IsNetMode(NM_DedicatedServer) ? TEXT("server") : TEXT("client"), CostUs / CostTicks, CostUs / FMath::Max(1.0, (Now - CostLogAt) / FMath::Max(1e-3, double(DeltaSeconds))), CostTicks, DeltaSeconds * 1000.f);
		CostLogAt = Now; CostUs = 0.0; CostTicks = 0;
	}
}

void ATownGuard::TickBody(float Dt)
{
	const FVector Moved = GetActorLocation() - LastLoc;
	LastLoc = GetActorLocation();
	SmVel = FMath::VInterpTo(SmVel, (Moved / FMath::Max(Dt, 1e-4f)).GetClampedToMaxSize(900.f), Dt, 10.f);
	if (!Anim || !Anim->IsReady()) return;
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, HalfH);
	Anim->MoveTo(Feet, GetActorRotation().Yaw);
	AnimAccum += Dt;
	const bool bSeen = Anim->Mesh() && Anim->Mesh()->WasRecentlyRendered(0.25f);
	if (AnimAccum < (bSeen ? 0.f : 0.25f)) return;
	JeebAnim::FInput In;
	In.Vel = SmVel;
	In.Rot = GetActorRotation();
	In.Kind = JeebItems::K_Sword;
	In.bShieldArm = false;
	Anim->Update(AnimAccum, In, Feet, In.Rot.Yaw);
	AnimAccum = 0.f;
	WristR->SetWorldTransform(Anim->RightGripWorld());
}

// ------------------------------------------------------------------ the online test (Tools/guards_net.sh)
#if !UE_BUILD_SHIPPING
namespace
{
	struct FGuardTest
	{
		TWeakObjectPtr<AKnightCharacter> A, B;
		float Clock = 0.f, BAt = 0.f, StageAt = 0.f;
		int32 Stage = 0, AIngots0 = 0;
		bool bShotClose = false, bShotAlarm = false, bShotChase = false, bShotCatch = false, bASaw = false;
		float AlarmSeenAt = -1.f, ChaseSeenAt = -1.f, LogAt = 0.f;
		FVector GuardWas = FVector::ZeroVector;
		float GuardRan = 0.f;
	} GT;

	ANpc* NearestNpc(UWorld* W, const FVector& P)
	{
		ANpc* Best = nullptr; float BD = 1e12f;
		for (TActorIterator<ANpc> It(W); It; ++It) { const float D = FVector::DistSquared2D(It->GetActorLocation(), P); if (D < BD) { BD = D; Best = *It; } }
		return Best;
	}

	void Put(AKnightCharacter* K, const FVector2D& P, const FVector2D& Face)
	{
		K->SetActorLocation(FVector(P.X, P.Y, JeebFoes::Ground(P.X, P.Y) + 110.f), false, nullptr, ETeleportType::TeleportPhysics);
	}

	float YawTo(const FVector2D& From, const FVector2D& To) { return FMath::RadiansToDegrees(FMath::Atan2(To.Y - From.Y, To.X - From.X)); }

	void GuardTest(ATownGuard* G, float Dt)
	{
		static const bool bServer = FParse::Param(FCommandLine::Get(), TEXT("GuardNetTest"));
		static const bool bA = FParse::Param(FCommandLine::Get(), TEXT("GuardNetA"));
		static const bool bB = FParse::Param(FCommandLine::Get(), TEXT("GuardNetB"));
		if (!(bServer || bA || bB)) return;
		UWorld* W = G->GetWorld();
		// one guard drives it: the first in the world
		{ TActorIterator<ATownGuard> It(W); if (!It || *It != G) return; }
		GT.Clock += Dt;
		const FVector2D Plaza(JeebOakHill::SpawnX + 400.f, JeebOakHill::SpawnY - 300.f);
		if (bServer)
		{
			for (TActorIterator<AKnightCharacter> It(W); It; ++It)
			{
				if (!It->GetController() || !It->GetSkills()) continue;
				const bool bIsA = Who(*It).Contains(TEXT("guardreva"));   // (by the test accounts' names: -GuardNetA logs in as guardreva)
				if (!GT.A.IsValid() && bIsA) { GT.A = *It; UE_LOG(LogJeebGuard, Log, TEXT("JEEBGUARDTEST player A is %s"), *Who(*It)); }
				else if (!GT.B.IsValid() && !bIsA) { GT.B = *It; GT.BAt = GT.Clock; UE_LOG(LogJeebGuard, Log, TEXT("JEEBGUARDTEST player B is %s"), *Who(*It)); }
			}
			AKnightCharacter* A = GT.A.Get(); AKnightCharacter* B = GT.B.Get();
			if (!A || !B) return;
			const float Since = GT.Clock - GT.StageAt;
			auto Next = [&](const TCHAR* What) { ++GT.Stage; GT.StageAt = GT.Clock; UE_LOG(LogJeebGuard, Log, TEXT("JEEBGUARDTEST stage %d: %s"), GT.Stage, What); };
			ATownGuard* Ring = G;
			switch (GT.Stage)
			{
			case 0:   // B stands 4 m from a guard (a close picture on B's machine)
				if (GT.Clock < GT.BAt + 12.f) return;
				{
					const FVector2D GP(Ring->GetActorLocation());
					const FVector2D Fwd(Ring->GetActorForwardVector());
					Put(B, GP + Fwd * 450.f, GP);
					Put(A, Plaza + FVector2D(0.f, 2500.f), Plaza);
				}
				Next(TEXT("B before a guard"));
				return;
			case 1:   // A on the plaza, with stolen ingots and a potion; the alarm
				if (Since < 7.f) return;
				{
					Put(A, Plaza, Plaza + FVector2D(0, 1));
					Put(B, Plaza + FVector2D(-900.f, -600.f), Plaza);
					USkillComponent* Sk = A->GetSkills();
					Sk->AddIngots(60);
					const int32 Pot = JeebItems::PotionId(0);
					Sk->AddInvItem(Pot, 1);
					JeebGuards::NoteStolen(A, { FInvSlot(Pot, 1) }, 30);
					GT.AIngots0 = Sk->GetIngots();
					ANpc* N = NearestNpc(W, A->GetActorLocation());
					UE_LOG(LogJeebGuard, Log, TEXT("JEEBGUARDTEST alarm 1: A has %d ingots and %d of %s; heard by %s %.1f m away"), GT.AIngots0, Sk->CountItem(Pot), *JeebItems::ItemName(Pot), N ? *N->GetNpcName() : TEXT("nobody"), N ? FVector::Dist2D(N->GetActorLocation(), A->GetActorLocation()) / 100.f : 0.f);
					JeebGuards::RaiseAlarm(A, N);
				}
				Next(TEXT("alarm 1, A stands still"));
				return;
			case 2:   // caught?
			{
				const int32 Now = A->GetSkills()->GetIngots();
				if (Now < GT.AIngots0 && !JeebGuards::IsChased(A))
				{
					UE_LOG(LogJeebGuard, Log, TEXT("JEEBGUARDTEST PASS caught after %.1f s: ingots %d -> %d, potions left %d"), Since, GT.AIngots0, Now, A->GetSkills()->CountItem(JeebItems::PotionId(0)));
					Next(TEXT("caught"));
				}
				else if (Since > 70.f) { UE_LOG(LogJeebGuard, Log, TEXT("JEEBGUARDTEST FAIL not caught in 70 s")); Next(TEXT("gave up on the catch")); }
				return;
			}
			case 3:   // the escape: a second alarm, then A is put outside Oak Hill
				if (Since < 12.f) return;
				Thefts().FindOrAdd(A).GraceUntil = -1.0;   // (the test skips the grace)
				Put(A, Plaza + FVector2D(0.f, 1500.f), Plaza);   // (further from the guard: the escape has time to start)
				GT.AIngots0 = A->GetSkills()->GetIngots();
				JeebGuards::RaiseAlarm(A, NearestNpc(W, A->GetActorLocation()));
				UE_LOG(LogJeebGuard, Log, TEXT("JEEBGUARDTEST alarm 2: chased %d"), JeebGuards::IsChased(A) ? 1 : 0);
				Next(TEXT("alarm 2"));
				return;
			case 4:
				if (Since < 0.5f) return;   // (he runs for half a second, then A is gone)
				{
					const float X = JeebOakHill::BoxX1 + 1500.f, Y = JeebOakHill::PosY;
					A->SetActorLocation(FVector(X, Y, JeebFoes::Ground(X, Y) + 110.f), false, nullptr, ETeleportType::TeleportPhysics);
				}
				Next(TEXT("A runs out of town"));
				return;
			case 5:
				if (!JeebGuards::IsChased(A))
				{
					const int32 Now = A->GetSkills()->GetIngots();
					UE_LOG(LogJeebGuard, Log, TEXT("JEEBGUARDTEST %s lost after %.1f s outside, ingots %d -> %d"), Now == GT.AIngots0 ? TEXT("PASS") : TEXT("FAIL"), Since, GT.AIngots0, Now);
					Next(TEXT("done"));
				}
				else if (Since > 30.f) { UE_LOG(LogJeebGuard, Log, TEXT("JEEBGUARDTEST FAIL still chased 30 s after leaving")); Next(TEXT("done")); }
				return;
			default: return;
			}
		}
		// Players' machines
		APlayerController* Pc = W->GetFirstPlayerController();
		AKnightCharacter* Me = Pc ? Cast<AKnightCharacter>(Pc->GetPawn()) : nullptr;
		if (!Me) return;
		if (GT.Clock > 220.f) { Pc->ConsoleCommand(TEXT("quit")); return; }
		ATownGuard* Chaser = nullptr; ATownGuard* Near = nullptr; float ND = 1e12f;
		for (TActorIterator<ATownGuard> It(W); It; ++It)
		{
			if (It->Mode() == ATownGuard::EMode::Chase) Chaser = *It;
			const float D = FVector::DistSquared2D(It->GetActorLocation(), Me->GetActorLocation());
			if (D < ND) { ND = D; Near = *It; }
		}
		if (bA)
		{
			if (!GT.bASaw && JeebGuards::IsChased(Me)) { GT.bASaw = true; UE_LOG(LogJeebGuard, Log, TEXT("JEEBGUARDDO A: a guard is after me")); }
			if (GT.bASaw && !JeebGuards::IsChased(Me)) { GT.bASaw = false; UE_LOG(LogJeebGuard, Log, TEXT("JEEBGUARDDO A: no guard after me now")); }
			return;
		}
		// B: logs the running guard, aims and takes the pictures
		ANpc* Shouter = nullptr;
		for (TActorIterator<ANpc> It(W); It; ++It) { FString L; bool bS = false; float Ba, Ma; if (It->AlarmShown(L, bS, Ba, Ma) && bS) Shouter = *It; }
		if (Shouter && GT.AlarmSeenAt < 0.f) GT.AlarmSeenAt = GT.Clock;
		if (!Chaser) GT.ChaseSeenAt = -1.f;
		else if (GT.ChaseSeenAt < 0.f) { GT.ChaseSeenAt = GT.Clock; GT.GuardWas = Chaser->GetActorLocation(); GT.GuardRan = 0.f; }
		if (Chaser)
		{
			GT.GuardRan += FVector::Dist2D(Chaser->GetActorLocation(), GT.GuardWas);
			GT.GuardWas = Chaser->GetActorLocation();
			if (GT.Clock > GT.LogAt) { GT.LogAt = GT.Clock + 1.f; UE_LOG(LogJeebGuard, Log, TEXT("JEEBGUARDDO B: guard chasing at %.0f %.0f, ran %.1f m so far"), GT.GuardWas.X, GT.GuardWas.Y, GT.GuardRan / 100.f); }
		}
		AActor* Look = (Shouter && GT.Clock - GT.AlarmSeenAt < 1.6f) ? (AActor*)Shouter : Chaser ? (AActor*)Chaser : (AActor*)Near;
		if (Look)
		{
			const FVector To = Look->GetActorLocation() - Me->GetActorLocation();
			Me->SetActorRotation(FRotator(0.f, To.Rotation().Yaw, 0.f));
			Pc->SetControlRotation(FRotator(-12.f, To.Rotation().Yaw, 0.f));
		}
		auto Shot = [&](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / (FString(Name) + TEXT(".png")), true, false); UE_LOG(LogJeebGuard, Log, TEXT("JEEBGUARDDO B: picture %s"), Name); };
		if (!GT.bShotClose && Near && ND < 700.f * 700.f && GT.Clock > 20.f && !Chaser) { GT.bShotClose = true; Shot(TEXT("guard_close")); }
		if (!GT.bShotAlarm && Shouter && GT.Clock - GT.AlarmSeenAt > 1.2f) { GT.bShotAlarm = true; Shot(TEXT("guard_alarm")); }
		if (!GT.bShotChase && Chaser && GT.Clock - GT.ChaseSeenAt > 2.5f) { GT.bShotChase = true; Shot(TEXT("guard_chase")); }
		if (!GT.bShotCatch && Chaser && Chaser->ChaseTarget() && FVector::Dist2D(Chaser->GetActorLocation(), Chaser->ChaseTarget()->GetActorLocation()) < 400.f) { GT.bShotCatch = true; Shot(TEXT("guard_catch")); }
	}
}
#endif

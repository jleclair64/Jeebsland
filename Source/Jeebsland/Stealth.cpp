// Stealth (2026-11-26): the shared detection meter. See Stealth.h and Docs/StealthPlan.md.
#include "Stealth.h"
#include "KnightCharacter.h"
#include "SkillComponent.h"
#include "HealthComponent.h"
#include "DayNight.h"
#include "FoeData.h"
#include "JeebTuning.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Net/UnrealNetwork.h"
#include "CollisionQueryParams.h"

namespace
{
	TArray<TWeakObjectPtr<UStealthSense>>& Registry() { static TArray<TWeakObjectPtr<UStealthSense>> R; return R; }
	// (one Stealth XP grant per player per scan, however many NPCs are near)
	TMap<TWeakObjectPtr<AKnightCharacter>, double>& LastXPAt() { static TMap<TWeakObjectPtr<AKnightCharacter>, double> M; return M; }
	TMap<TWeakObjectPtr<AKnightCharacter>, float>& XPAcc() { static TMap<TWeakObjectPtr<AKnightCharacter>, float> M; return M; }
	const TCHAR* StateName(uint8 S) { return S == 2 ? TEXT("ALERT") : S == 1 ? TEXT("suspicious") : TEXT("calm"); }
}

UStealthSense::UStealthSense()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UStealthSense::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UStealthSense, RepMark);
}

void UStealthSense::BeginPlay()
{
	Super::BeginPlay();
	Registry().Add(this);
	if (LogName.IsEmpty() && GetOwner()) LogName = GetOwner()->GetName();
}

void UStealthSense::EndPlay(const EEndPlayReason::Type Why)
{
	Registry().RemoveAll([this](const TWeakObjectPtr<UStealthSense>& P) { return !P.IsValid() || P.Get() == this; });
	Super::EndPlay(Why);
}

const TArray<TWeakObjectPtr<UStealthSense>>& UStealthSense::All() { return Registry(); }

float UStealthSense::CrouchShare(int32 Level)
{
	const auto& T = JeebTune::V();
	return FMath::Max(0.f, FMath::Lerp(T.StealthCrouchAt1, T.StealthCrouchAt99, FMath::Clamp((Level - 1) / 98.f, 0.f, 1.f)));
}

float UStealthSense::ConeShare(int32 Level)
{
	const auto& T = JeebTune::V();
	return FMath::Max(0.f, FMath::Lerp(1.f, T.StealthConeAt99, FMath::Clamp((Level - 1) / 98.f, 0.f, 1.f)));
}

void UStealthSense::Clear()
{
	Seen.Reset();
	Best = nullptr; BestD = 1.0e9f;
	AlertMarkT = 0.f;
	if (RepMark != 0) RepMark = 0;
}

float UStealthSense::MeterOf(const AKnightCharacter* K) const
{
	const FEntry* E = Seen.Find(const_cast<AKnightCharacter*>(K));
	return E ? E->Meter : 0.f;
}

bool UStealthSense::IsAlertOn(const AKnightCharacter* K) const
{
	const FEntry* E = Seen.Find(const_cast<AKnightCharacter*>(K));
	return E && E->bAlert;
}

void UStealthSense::ForceAlert(AKnightCharacter* K)
{
	if (!K) return;
	FEntry& E = Seen.FindOrAdd(K);
	if (!E.bAlert) AlertMarkT = JeebTune::V().StealthAlertMarkSeconds;
	E.Meter = 1.f; E.bAlert = true; E.Last = 2;
}

AKnightCharacter* UStealthSense::Update(float Dt, const FStealthSenses& S, float& OutDist)
{
	OutDist = 1.0e9f;
	AActor* Me = GetOwner();
	UWorld* W = GetWorld();
	if (!Me || !W || !Me->HasAuthority()) return nullptr;
	const auto& T = JeebTune::V();
	Acc += Dt;
	AlertMarkT = FMath::Max(0.f, AlertMarkT - Dt);
	ScanT -= Dt;
	if (ScanT <= 0.f)
	{
		ScanT = FMath::Max(0.05f, T.StealthScanSeconds);
		const float Step = Acc;
		Acc = 0.f;
		const double Now = W->GetTimeSeconds();
		const FVector At3 = Me->GetActorLocation();
		const FVector2D At(At3);
		const FVector2D Fw(Me->GetActorForwardVector().GetSafeNormal2D());
		const float NightM = FMath::Lerp(1.f, T.StealthNightMul, FMath::Clamp(ADayNight::Night, 0.f, 1.f));
		const float ConeCos = FMath::Cos(FMath::DegreesToRadians(T.StealthConeHalfAngle));
		const float Reach = FMath::Max(S.Sight, S.HearRun) + 200.f;
		Best = nullptr; BestD = 1.0e9f;
		TSet<AKnightCharacter*> Touched;
		for (TActorIterator<AKnightCharacter> It(W); It; ++It)
		{
			AKnightCharacter* K = *It;
			if (K->GetHealth() <= 0.f || K->IsHidden()) continue;
			const FVector2D P(K->GetActorLocation());
			const float Dist = FVector2D::Distance(P, At);
			if (Dist > Reach) continue;
			if (S.bSkipTowns && JeebFoes::InTown(P.X, P.Y)) continue;
			Touched.Add(K);
			FEntry& E = Seen.FindOrAdd(K);

			const USkillComponent* Sk = K->GetSkills();
			const int32 Lv = Sk ? Sk->GetLevel(ESkillType::Stealth) : 1;
			const bool bCrouch = K->IsCrouchedNow();
			const float V = K->GetVelocity().Size2D();
			const float CosA = FVector2D::DotProduct(Fw, (P - At) / FMath::Max(Dist, 1.f));
			const bool bFront = CosA > S.FrontDot, bCone = CosA > ConeCos;
			const float StandR = (bFront ? S.Sight : S.Sight * S.Behind) * S.SightMul * NightM;   // a standing player: as the NPC always saw
			float SeeR = StandR;
			bool bByCone = false;
			if (bCrouch)
			{
				SeeR = StandR * CrouchShare(Lv);
				const float ConeR = bCone ? S.Sight * S.SightMul * NightM * ConeShare(Lv) : 0.f;
				if (ConeR > SeeR) { SeeR = ConeR; bByCone = true; }
			}
			const float HearR = bCrouch ? 0.f : (V > 600.f ? S.HearRun : V > 260.f ? S.HearWalk : S.HearStill) * NightM;

			bool bSeen = false, bLos = false;
			if (Dist <= SeeR)
			{
				// one line trace: trunks, walls, rocks, the ground's own humps
				FCollisionQueryParams Q(SCENE_QUERY_STAT(JeebStealth), false, Me);
				Q.AddIgnoredActor(K);
				FCollisionObjectQueryParams Obj; Obj.AddObjectTypesToQuery(ECC_WorldStatic);
				const FVector Eye(At.X, At.Y, JeebFoes::Ground(At.X, At.Y) + S.EyeZ);
				const FVector Body = K->GetActorLocation() + FVector(0.f, 0.f, bCrouch ? 0.f : 50.f);
				bLos = !W->LineTraceTestByObjectType(Eye, Body, Obj, Q);
				bSeen = bLos;
			}
			const bool bHeard = Dist <= HearR;

			if (bSeen)
			{
				const float Secs = FMath::Lerp(T.StealthSeeFillNear, T.StealthSeeFillFar, FMath::Clamp(Dist / FMath::Max(SeeR, 1.f), 0.f, 1.f));
				E.Meter += Step / FMath::Max(0.05f, Secs);
			}
			else if (bHeard) E.Meter += Step / FMath::Max(0.05f, T.StealthHeardFill);
			else E.Meter -= Step / FMath::Max(0.05f, T.StealthDrainSeconds);
			E.Meter = FMath::Clamp(E.Meter, 0.f, 1.f);
			if (E.Meter >= 1.f) { if (!E.bAlert) AlertMarkT = T.StealthAlertMarkSeconds; E.bAlert = true; }
			if (E.Meter <= 0.f) E.bAlert = false;

			const uint8 State = E.bAlert ? 2 : E.Meter > 0.f ? 1 : 0;
			if (State != E.Last || T.StealthLog >= 0.5f)
			{
				UE_LOG(LogTemp, Log, TEXT("JEEBSTEALTH %s %s -> %s: %s %.1f m, %s, Stealth %d, night %.2f, %s%s, see %.1f m hear %.1f m, los %d seen %d heard %d, meter %.2f"),
					*LogName, *K->GetName(), StateName(State), State != E.Last ? TEXT("CHANGE") : TEXT("scan"), Dist / 100.f, bCrouch ? TEXT("crouched") : TEXT("standing"), Lv, ADayNight::Night,
					bCone ? TEXT("in cone") : bFront ? TEXT("front/side") : TEXT("behind"), bByCone ? TEXT(" (cone sees)") : TEXT(""), SeeR / 100.f, HearR / 100.f, bLos ? 1 : 0, bSeen ? 1 : 0, bHeard ? 1 : 0, E.Meter);
				E.Last = State;
			}

			// Stealth XP: crouched and moving within the NPC's normal reach, unnoticed (once per player per scan, whatever the number of NPCs)
			if (bCrouch && V > 40.f && Dist <= StandR && !E.bAlert && E.Meter < 0.1f && Sk && T.StealthXPPerSec > 0.f)
			{
				double& Last = LastXPAt().FindOrAdd(K);
				if (Now - Last >= ScanT * 0.9)
				{
					Last = Now;
					float& A = XPAcc().FindOrAdd(K);
					A += T.StealthXPPerSec * Step;
					if (A >= 5.f) { const int32 N = FMath::FloorToInt(A); A -= N; K->GetSkills()->AddXP(ESkillType::Stealth, N); }
				}
			}
			if (E.bAlert && Dist < BestD) { BestD = Dist; Best = K; }
		}
		// players out of reach: their meters drain away
		for (auto It = Seen.CreateIterator(); It; ++It)
		{
			if (!It->Key.IsValid()) { It.RemoveCurrent(); continue; }
			if (Touched.Contains(It->Key.Get())) continue;
			It->Value.Meter -= Step / FMath::Max(0.05f, T.StealthDrainSeconds);
			if (It->Value.Meter <= 0.f) { It.RemoveCurrent(); continue; }
			if (It->Value.bAlert && FVector2D::Distance(FVector2D(It->Key->GetActorLocation()), At) < BestD) { BestD = FVector2D::Distance(FVector2D(It->Key->GetActorLocation()), At); Best = It->Key.Get(); }
		}
		uint8 Mk = 0;
		for (const auto& P : Seen) if (!P.Value.bAlert && P.Value.Meter > 0.f) Mk = 1;
		if (AlertMarkT > 0.f) Mk = 2;
		if (Mk != RepMark) RepMark = Mk;
	}
	else if (AlertMarkT <= 0.f && RepMark == 2) RepMark = 0;
	AKnightCharacter* B = Best.Get();
	if (B && B->GetHealth() > 0.f) { OutDist = FVector2D::Distance(FVector2D(B->GetActorLocation()), FVector2D(Me->GetActorLocation())); return B; }
	return nullptr;
}

// ---------------------------------------------------------------------------------------------- the off-screen test (-JeebStealthTest)
#if !UE_BUILD_SHIPPING
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Deer.h"

// A standalone game with -JeebStealthTest: a watcher with the deer's senses stands in open ground; the player is placed round it standing or
// crouched, at Stealth 1 or 99, by day and night, and behind a stand-in tree trunk. Each stage logs JEEBSTEALTHTEST PASS/FAIL.
struct FStealthTestDriver
{
	struct FStage { const TCHAR* Name; FVector2D Off; bool bCrouch; int32 Level; bool bNight; bool bBlock; float Secs; int32 Want; bool bThenCrouch; };
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<AActor> Watcher, Trunk;
	TWeakObjectPtr<UStealthSense> Sense;
	FVector2D W0;
	float Clock = 0.f, StageT = 0.f;
	int32 Stage = -2, Pass = 0, Fail = 0, MaxState = 0;
	bool bCrouchedLate = false;
	TArray<FStage> Stages;

	static void Log(const FString& S) { UE_LOG(LogTemp, Log, TEXT("JEEBSTEALTHTEST %s"), *S); }

	FStealthSenses Senses() const
	{
		FStealthSenses S; S.Sight = 3000.f; S.Behind = 0.35f; S.FrontDot = -0.45f; S.HearRun = 2400.f; S.HearWalk = 1300.f; S.HearStill = 400.f; S.EyeZ = 130.f;
		return S;
	}

	AKnightCharacter* Knight() const
	{
		for (TActorIterator<AKnightCharacter> It(World.Get()); It; ++It) return *It;
		return nullptr;
	}

	void Place(AKnightCharacter* K, const FVector2D& P, bool bCrouch, int32 Level)
	{
		K->TeleportTo(FVector(P.X, P.Y, JeebFoes::Ground(P.X, P.Y) + 100.f), FRotator(0.f, 180.f, 0.f), false, true);
		K->bCrouched = bCrouch;
		if (K->GetSkills()) K->GetSkills()->SetAllLevels(Level);
	}

	bool Tick(float Dt)
	{
		UWorld* Wd = nullptr;
		if (GEngine) for (const FWorldContext& Ctx : GEngine->GetWorldContexts()) if (Ctx.WorldType == EWorldType::Game && Ctx.World() && Ctx.World()->GetName() == TEXT("Field")) Wd = Ctx.World();
		if (Wd != World.Get()) { World = Wd; if (Stage != -2) return false; }
		if (!Wd) return true;
		Clock += Dt;
		AKnightCharacter* K = Knight();
		if (Stage == -2)
		{
			if (Clock < 15.f || !K) return true;
			// open ground: a deer herd's meadow if there is one, else 60 m from where the player stands
			FVector2D C(K->GetActorLocation());
			for (TActorIterator<ADeer> It(Wd); It; ++It) { C = FVector2D(It->GetActorLocation()) + FVector2D(0.f, 4000.f); break; }
			W0 = C;
			FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* A = Wd->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FVector(C.X, C.Y, JeebFoes::Ground(C.X, C.Y))), SP);
			USceneComponent* Root = NewObject<USceneComponent>(A, TEXT("Root")); A->SetRootComponent(Root); Root->RegisterComponent();
			A->SetActorLocationAndRotation(FVector(C.X, C.Y, JeebFoes::Ground(C.X, C.Y)), FRotator::ZeroRotator);   // faces +X
			UStealthSense* Sn = NewObject<UStealthSense>(A, TEXT("Stealth")); Sn->LogName = TEXT("TestWatcher"); Sn->RegisterComponent();
			Watcher = A; Sense = Sn;
			// the stand-in tree trunk (a 1 m wide, 4 m tall engine cylinder, WorldStatic), parked far away until a stage wants it
			AStaticMeshActor* Tr = Wd->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(FVector(C.X, C.Y, -100000.f)), SP);
			Tr->SetMobility(EComponentMobility::Movable);
			Tr->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
			Tr->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			Tr->GetStaticMeshComponent()->SetCollisionObjectType(ECC_WorldStatic);
			Tr->SetActorScale3D(FVector(1.f, 1.f, 4.f));
			Trunk = Tr;
			Log(FString::Printf(TEXT("watcher at %.0f %.0f ground %.0f, facing +X, deer senses: sight 30 m (35%% behind), hear sprint 24 / walk 13 / still 4 m"), C.X, C.Y, JeebFoes::Ground(C.X, C.Y)));
			Stages = {
				{ TEXT("standing, in front, 20 m, day, Stealth 1"),            FVector2D(2000, 0),  false, 1,  false, false, 3.f, 2, false },
				{ TEXT("crouched, in the cone, 20 m, day, Stealth 1"),         FVector2D(2000, 0),  true,  1,  false, false, 3.f, 2, false },
				{ TEXT("crouched, to the side, 12 m, day, Stealth 1"),         FVector2D(0, 1200),  true,  1,  false, false, 3.f, 2, false },
				{ TEXT("crouched, to the side, 20 m, day, Stealth 1"),         FVector2D(0, 2000),  true,  1,  false, false, 3.f, 0, false },
				{ TEXT("crouched, to the side, 3 m, day, Stealth 99"),         FVector2D(0, 300),   true,  99, false, false, 3.f, 0, false },
				{ TEXT("crouched, in the cone, 12 m, day, Stealth 99"),        FVector2D(1200, 0),  true,  99, false, false, 3.f, 0, false },
				{ TEXT("crouched, in the cone, 7 m, day, Stealth 99"),         FVector2D(700, 0),   true,  99, false, false, 3.f, 2, false },
				{ TEXT("standing, to the side, 20 m, NIGHT, Stealth 1"),       FVector2D(0, 2000),  false, 1,  true,  false, 3.f, 0, false },
				{ TEXT("standing, to the side, 12 m, NIGHT, Stealth 1"),       FVector2D(0, 1200),  false, 1,  true,  false, 3.f, 2, false },
				{ TEXT("crouched, to the side, 12 m, NIGHT, Stealth 1"),       FVector2D(0, 1200),  true,  1,  true,  false, 3.f, 0, false },
				{ TEXT("standing, in front, 10 m, behind a trunk, day"),       FVector2D(1000, 0),  false, 1,  false, true,  3.f, 0, false },
				{ TEXT("standing up 3 m away behind a trunk (heard, not seen), stays up"), FVector2D(300, 0), false, 1, false, true, 3.5f, 2, false },
				{ TEXT("standing up 3 m away behind a trunk, crouches again after 1 s"),   FVector2D(300, 0), false, 1, false, true, 4.f, 1, true },
			};
			Stage = -1; StageT = 0.f;
			return true;
		}
		if (!K || !Sense.IsValid()) return true;
		UStealthSense* Sn = Sense.Get();
		const float Night = Stage >= 0 && Stage < Stages.Num() && Stages[Stage].bNight ? 1.f : 0.f;
		StageT += Dt;
		if (Stage >= 0 && Stage < Stages.Num())
		{
			const FStage& S = Stages[Stage];
			if (S.bThenCrouch && StageT > 1.f && !bCrouchedLate) { K->bCrouched = true; bCrouchedLate = true; Log(TEXT("  (crouches again)")); }
			float D = 0.f;
			Sn->Update(Dt, Senses(), D);
			const int32 St = Sn->IsAlertOn(K) ? 2 : Sn->MeterOf(K) > 0.f ? 1 : 0;
			MaxState = FMath::Max(MaxState, St);
			if (StageT >= S.Secs)
			{
				const bool bOk = S.bThenCrouch ? (MaxState == 1 && (St == 0 || Sn->MeterOf(K) < 0.5f)) : (MaxState == S.Want);
				Log(FString::Printf(TEXT("%s %s: highest %s, now %s (meter %.2f), wanted %s, night %.2f"), bOk ? TEXT("PASS") : TEXT("FAIL"), S.Name,
					MaxState == 2 ? TEXT("ALERT") : MaxState == 1 ? TEXT("suspicious") : TEXT("calm"), St == 2 ? TEXT("ALERT") : St == 1 ? TEXT("suspicious") : TEXT("calm"), Sn->MeterOf(K),
					S.Want == 2 ? TEXT("ALERT") : S.Want == 1 ? TEXT("suspicious then fade") : TEXT("calm"), ADayNight::Night));
				bOk ? ++Pass : ++Fail;
				StageT = 0.f; Stage = -1 - 1000 - Stage;   // (a pause before the next)
			}
			return true;
		}
		// between stages: the player far away, the watcher forgets, the clock set
		const int32 Next = Stage == -1 ? 0 : (-Stage - 1000);
		if (StageT < 0.05f)
		{
			Place(K, W0 + FVector2D(-15000.f, 0.f), false, 1);
			Sn->Clear();
		}
		if (Next >= Stages.Num())
		{
			Log(FString::Printf(TEXT("DONE %d passed, %d failed"), Pass, Fail));
			ADayNight::FixedPhase = -1.f;
			FPlatformMisc::RequestExit(false);
			return false;
		}
		ADayNight::FixedPhase = Stages[Next].bNight ? 0.5f : 0.f;
		if (StageT > 1.5f)
		{
			const FStage& S = Stages[Next];
			if (AActor* Tr = Trunk.Get())
			{
				const FVector2D Mid = W0 + S.Off * 0.5f;
				Tr->SetActorLocation(S.bBlock ? FVector(Mid.X, Mid.Y, JeebFoes::Ground(Mid.X, Mid.Y) + 200.f) : FVector(W0.X, W0.Y, -100000.f));
			}
			Place(K, W0 + S.Off, S.bCrouch, S.Level);
			Sn->Clear();
			Stage = Next; StageT = 0.f; MaxState = 0; bCrouchedLate = false;
		}
		(void)Night;
		return true;
	}
};

static struct FStealthTestHook
{
	FStealthTestHook()
	{
		FWorldDelegates::OnPostWorldInitialization.AddLambda([](UWorld* W, const UWorld::InitializationValues)
		{
			if (!W || !W->IsGameWorld() || !FParse::Param(FCommandLine::Get(), TEXT("JeebStealthTest"))) return;
			static TSharedPtr<FStealthTestDriver> Driver;
			if (Driver.IsValid()) return;
			Driver = MakeShared<FStealthTestDriver>();
			Driver->World = W;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float Dt) { return Driver.IsValid() ? Driver->Tick(Dt) : false; }));
			UE_LOG(LogTemp, Log, TEXT("JEEBSTEALTHTEST armed"));
		});
	}
} GStealthTestHook;
#endif

#include "Horse.h"
#include "PvpZones.h"
#include "KnightCharacter.h"
#include "HealthComponent.h"
#include "SkillComponent.h"
#include "QuestComponent.h"
#include "QuestData.h"
#include "Inventory.h"
#include "JeebTuning.h"
#include "JeebTuning.h"
#include "FoeData.h"
#include "Terrain.h"
#include "WorldMap.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Materials/MaterialInstanceDynamic.h"

static TAutoConsoleVariable<float> CVarHorseMeshYaw(TEXT("jeeb.Horse.MeshYaw"), -90.f, TEXT("Degrees the horse's model is turned to face the way it walks (looks only)."), ECVF_Default);
static TAutoConsoleVariable<FString> CVarSaddle(TEXT("jeeb.Horse.Saddle"), TEXT("50 116 -90 0"),
	TEXT("Where the saddle sits on the horse's model (looks only): forward, up (cm in the unscaled model), yaw and pitch (degrees). Live."), ECVF_Default);

const FName JeebHorse::QuestKey(TEXT("StableHorse"));

// ---------------------------------------------------------------------------------------------- data

namespace JeebHorse
{
	const TArray<FStableDef>& Stables()
	{
		static TArray<FStableDef> All;
		static bool bRead = false;
		if (bRead) return All;
		bRead = true;
		FString Text;
		const FString Path = FPaths::ProjectContentDir() / TEXT("Data/Stables.csv");
		if (!FFileHelper::LoadFileToString(Text, *Path)) { UE_LOG(LogTemp, Error, TEXT("Jeebsland horses: cannot read %s"), *Path); return All; }
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines);
		TArray<FString> Head;
		for (const FString& L : Lines)
		{
			if (L.TrimStart().StartsWith(TEXT("#")) || L.TrimStartAndEnd().IsEmpty()) continue;
			TArray<FString> C;
			L.ParseIntoArray(C, TEXT(","), false);
			for (FString& S : C) S.TrimStartAndEndInline();
			if (Head.Num() == 0) { Head = C; continue; }
			auto Col = [&](const TCHAR* Name) -> FString { const int32 i = Head.IndexOfByKey(FString(Name)); return C.IsValidIndex(i) ? C[i] : FString(); };
			FStableDef D;
			D.Key = FName(*Col(TEXT("Key")));
			D.Name = Col(TEXT("Name"));
			// (metres north and east of Oak Hill's plaza, as QuestsPeople.csv and QuestsSteps.csv write places)
			if (!JeebQuests::MarkSpot(Col(TEXT("North")) + TEXT(" ") + Col(TEXT("East")), D.Pos)) { UE_LOG(LogTemp, Error, TEXT("Jeebsland horses: Stables.csv: bad place for %s"), *D.Key.ToString()); continue; }
			JeebQuests::MarkSpot(Col(TEXT("WildNorth")) + TEXT(" ") + Col(TEXT("WildEast")), D.Wild);
			D.Yaw = FCString::Atof(*Col(TEXT("Yaw")));
			D.WildCount = FMath::Clamp(FCString::Atoi(*Col(TEXT("WildCount"))), 0, 8);
			All.Add(D);
		}
		UE_LOG(LogTemp, Log, TEXT("JEEBHORSE %d stables"), All.Num());
		return All;
	}
	const FStableDef* FindStable(FName Key) { return Stables().FindByPredicate([&](const FStableDef& S) { return S.Key == Key; }); }
	const FStableDef* NearestStable(const FVector2D& At, float MaxDist)
	{
		const FStableDef* Best = nullptr;
		for (const FStableDef& S : Stables())
		{
			const float D = FVector2D::Distance(S.Pos, At);
			if (D < MaxDist) { MaxDist = D; Best = &S; }
		}
		return Best;
	}
	int32 SaddleId()
	{
		static int32 Id = -2;
		if (Id == -2) { Id = FMath::Max(0, JeebQuests::ItemFromName(TEXT("Leather Saddle"))); if (Id == 0) UE_LOG(LogTemp, Error, TEXT("Jeebsland horses: no \"Leather Saddle\" in Accessories.csv")); }
		return Id;
	}
	bool WearsSaddle(const AKnightCharacter* K)
	{
		const USkillComponent* S = K ? K->GetSkills() : nullptr;
		const int32 Id = SaddleId();
		return S && Id != 0 && (S->Worn[JeebItems::K_Ring] == Id || S->RingB == Id || S->Worn[JeebItems::K_Amulet] == Id);
	}
	float SpeedMul(int32 Level)
	{
		const float A = FMath::Clamp((Level - 1) / 98.f, 0.f, 1.f);
		return FMath::Lerp(1.f, JeebTune::V().RideSpeedAt99, A);
	}
	float GallopSecs(int32 Level)
	{
		const float A = FMath::Clamp((Level - 1) / 98.f, 0.f, 1.f);
		return FMath::Lerp(JeebTune::V().GallopAt1, JeebTune::V().GallopAt99, A);
	}
}

namespace
{
	enum EHorseClip { HC_Idle, HC_Idle2, HC_Walk, HC_Run, HC_Back, HC_Death, HC_Count };
	// The body and its clips, loaded on first use and kept for the run (as ModelOf in Deer.cpp)
	struct FHorseModel { bool bTried = false; USkeletalMesh* Mesh = nullptr; UStaticMesh* Saddle = nullptr; UAnimSequence* Clips[JeebDeer::C_Count] = {}; };
	const FHorseModel* HorseModel()
	{
		static FHorseModel M;
		if (!M.bTried)
		{
			M.bTried = true;
			M.Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Animals/SK_Horse.SK_Horse"));
			M.Saddle = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Animals/SM_Saddle.SM_Saddle"));
			// the stag's animator has seven slots: idle, look, walk, run, graze, alert, death. The horse fills them with its own
			// clips (look and graze: the second idle; alert: walking backwards, played when it is backed up)
			auto Clip = [](const TCHAR* K) { UAnimSequence* A = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/Animals/A_Horse_%s.A_Horse_%s"), K, K)); if (A) A->AddToRoot(); return A; };
			M.Clips[JeebDeer::C_Idle] = Clip(TEXT("Idle"));
			M.Clips[JeebDeer::C_Look] = Clip(TEXT("Idle2"));
			M.Clips[JeebDeer::C_Walk] = Clip(TEXT("Walk"));
			M.Clips[JeebDeer::C_Run] = Clip(TEXT("Run"));
			M.Clips[JeebDeer::C_Graze] = M.Clips[JeebDeer::C_Look];
			M.Clips[JeebDeer::C_Alert] = Clip(TEXT("Back"));
			M.Clips[JeebDeer::C_Death] = Clip(TEXT("Death"));
			if (M.Mesh) M.Mesh->AddToRoot();
			if (M.Saddle) M.Saddle->AddToRoot();
			int32 Got = 0;
			for (UAnimSequence* A : M.Clips) Got += A ? 1 : 0;
			UE_LOG(LogTemp, Log, TEXT("JEEBHORSE model: mesh %s, saddle %s, %d of 7 clip slots"), M.Mesh ? TEXT("ok") : TEXT("MISSING"), M.Saddle ? TEXT("ok") : TEXT("MISSING"), Got);
		}
		return (M.Mesh && M.Clips[JeebDeer::C_Idle]) ? &M : nullptr;
	}
	// the planted hoof's speed in each moving clip at scale 1 (deer_export.py's DEER lines for the horse): the clips are played at the
	// speed the horse really goes / this, so the hooves don't slide (until the riding speeds outrun the canter: the rate is capped)
	constexpr float WalkHoof = 106.f, RunHoof = 397.f;
	constexpr float CallSpeed = 1700.f, TrotSpeed = 650.f, WalkSpeed = 160.f;

	UStaticMesh* YardMesh(const TCHAR* Name)
	{
		static TMap<FString, UStaticMesh*> Cache;
		if (UStaticMesh** F = Cache.Find(Name)) return *F;
		UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/TownDecor/SM_%s.SM_%s"), Name, Name));
		if (M) M->AddToRoot();
		Cache.Add(Name, M);
		return M;
	}
	void Tell(AKnightCharacter* K, const FString& Text, const FColor& C = FColor(235, 205, 150))
	{
		if (K && K->GetSkills()) K->GetSkills()->Say(3.5f, C, Text);
	}
}

// ---------------------------------------------------------------------------------------------- one horse

AHorse::AHorse()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false);   // (RepMove instead, as the stag; while ridden every machine carries it under its rider)
	SetNetUpdateFrequency(20.f);
	SetNetCullDistanceSquared(FMath::Square(30000.f));
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Hp = CreateDefaultSubobject<UHealthComponent>(TEXT("Hp"));
	Hp->Max = Hp->Cur = 120;
	Rand.Initialize(4242);
}

void AHorse::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AHorse, RepMove);
	DOREPLIFETIME(AHorse, RepState);
	DOREPLIFETIME(AHorse, RepRider);
	DOREPLIFETIME(AHorse, RepOwner);
	DOREPLIFETIME(AHorse, bRepWild);
	DOREPLIFETIME(AHorse, bRepSaddled);
	DOREPLIFETIME(AHorse, bFollow);
}

void AHorse::SetupHorse(AStable* InStable, bool bInWild, AKnightCharacter* InOwner, bool bInSaddled, int32 Seed)
{
	Stable = InStable;
	bRepWild = bInWild;
	RepOwner = InOwner;
	bRepSaddled = bInSaddled;
	Rand.Initialize(Seed);
	Home = InStable ? (bInWild ? InStable->GetDef().Wild : InStable->StallSpot()) : FVector2D(GetActorLocation());
	Goal = bInWild ? EGoal::Wander : EGoal::Stay;
}

void AHorse::BeginPlay()
{
	Super::BeginPlay();
	const float S = JeebHorse::Scale;
	Hp->BarHeight = 215.f * S; Hp->HitHeight = 100.f * S; Hp->HitRadius = 80.f * S;
	Hp->GatherHurt = [this](TArray<JeebHit::FCapsule>& Out) { GatherHurt(Out); };
	if (HasAuthority())
	{
		Hp->Max = Hp->Cur = FMath::Max(1, FMath::RoundToInt(JeebTune::V().HorseHealth));
		// Only players inside a PvP zone hurt a horse (owner, 2026-11-22). Blows that come through hit boxes are already held back by
		// GatherHurt (no boxes outside a zone); anything else is stopped here.
		Hp->ModifyDamage = [this](int32 Amount, AActor* From) -> int32
		{
			const bool bPlayer = From && (From->IsA<AKnightCharacter>() || Cast<AKnightCharacter>(From->GetInstigator()) || Cast<AKnightCharacter>(From->GetOwner()));
			return (bPlayer && JeebPvp::IsInPvpZone(GetWorld(), GetActorLocation())) ? Amount : 0;
		};
		Hp->CanBeHurt = [this](AActor* From)
		{
			const bool bPlayer = From && (From->IsA<AKnightCharacter>() || Cast<AKnightCharacter>(From->GetInstigator()) || Cast<AKnightCharacter>(From->GetOwner()));
			return bPlayer && JeebPvp::IsInPvpZone(GetWorld(), GetActorLocation());
		};
		Hp->OnDamaged = [this](AActor* From) { OnHurt(From); };
		const FVector L = GetActorLocation();
		SetActorLocation(FVector(L.X, L.Y, JeebFoes::Ground(L.X, L.Y)));
		SetState(bRepWild && Rand.FRand() < 0.5f ? EHorseState::Graze : EHorseState::Idle);
		SendMove();
	}
	BuildBody();
	TickLook(0.f, 0.f, false);
}

void AHorse::BuildBody()
{
	if (IsRunningDedicatedServer() || Body) return;
	const FHorseModel* M = HorseModel();
	if (!M) return;
	USkeletalMeshComponent* C = NewObject<USkeletalMeshComponent>(this, TEXT("Body"));
	C->SetupAttachment(Root);
	C->SetRelativeRotation(FRotator(0.f, CVarHorseMeshYaw.GetValueOnGameThread(), 0.f));
	C->SetRelativeScale3D(FVector(JeebHorse::Scale));
	C->SetSkeletalMesh(M->Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetCanEverAffectNavigation(false);
	C->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	C->bEnableUpdateRateOptimizations = true;
	C->LDMaxDrawDistance = 20000.f;
	C->CachedMaxDrawDistance = 20000.f;
	C->bAffectDynamicIndirectLighting = false;
	C->SetAnimInstanceClass(UDeerAnimInstance::StaticClass());
	C->RegisterComponent();
	Anim = Cast<UDeerAnimInstance>(C->GetAnimInstance());
	if (Anim) { Anim->SetClips(M->Clips, Rand.FRandRange(0.f, 4.f)); Anim->Drive(JeebDeer::C_Idle, 1.f); }
	Body = C;
	AddTickPrerequisiteComponent(C);
	if (M->Saddle)
	{
		UStaticMeshComponent* Sd = NewObject<UStaticMeshComponent>(this, TEXT("Saddle"));
		Sd->SetStaticMesh(M->Saddle);
		Sd->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Sd->SetCanEverAffectNavigation(false);
		Sd->SetupAttachment(C);
		Sd->RegisterComponent();
		Sd->SetVisibility(false);
		SaddleMesh = Sd;
	}
}

void AHorse::OnRep_Move()
{
	MoveAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (!bPlaced)
	{
		bPlaced = true;
		SetActorLocationAndRotation(FVector(RepMove.Loc.X, RepMove.Loc.Y, JeebFoes::Ground(RepMove.Loc.X, RepMove.Loc.Y)), FRotator(0.f, RepMove.Yaw * (360.f / 65536.f), 0.f));
	}
}

void AHorse::SendMove()
{
	RepMove.Loc = GetActorLocation();
	RepMove.Yaw = uint16(FMath::RoundToInt(FRotator::ClampAxis(GetActorRotation().Yaw) * (65536.f / 360.f)) & 0xFFFF);
	RepMove.Speed = uint16(FMath::Clamp(FMath::RoundToInt(Speed), 0, 65535));
}

void AHorse::SetState(EHorseState S)
{
	if (RepState == uint8(S)) return;
	RepState = uint8(S);
	StateT = 0.f;
	ForceNetUpdate();
}

// Hurt boxes (only inside a PvP zone; outside one a horse can't be struck at all): body, neck and head, two pairs of legs
void AHorse::GatherHurt(TArray<JeebHit::FCapsule>& Out) const
{
	if (IsDown() || !JeebPvp::IsInPvpZone(GetWorld(), GetActorLocation())) return;
	const float S = JeebHorse::Scale;
	const FTransform Xf(FRotator(0.f, GetActorRotation().Yaw, 0.f), GetActorLocation());
	auto Cap = [&](const FVector& A, const FVector& B, float R, uint8 Zone)
	{
		JeebHit::FCapsule C; C.A = Xf.TransformPosition(A * S); C.B = Xf.TransformPosition(B * S); C.R = R * S; C.Zone = Zone;
		Out.Add(C);
	};
	Cap(FVector(45.f, 0.f, 105.f), FVector(-50.f, 0.f, 105.f), 30.f, JeebHit::Z_Torso);
	Cap(FVector(55.f, 0.f, 120.f), FVector(80.f, 0.f, 150.f), 15.f, JeebHit::Z_Torso);
	Cap(FVector(92.f, 0.f, 145.f), FVector(92.f, 0.f, 145.f), 16.f, JeebHit::Z_Head);
	Cap(FVector(40.f, 0.f, 75.f), FVector(40.f, 0.f, 8.f), 12.f, JeebHit::Z_Leg);
	Cap(FVector(-48.f, 0.f, 75.f), FVector(-48.f, 0.f, 8.f), 12.f, JeebHit::Z_Leg);
}

void AHorse::OnHurt(AActor* From)
{
	UE_LOG(LogTemp, Log, TEXT("JEEBHORSE %s hurt by %s: %d/%d"), *GetName(), From ? *From->GetName() : TEXT("?"), Hp->Cur, Hp->Max);
	if (!Hp->IsAlive() && !IsDown()) Die(From);
}

void AHorse::Die(AActor* From)
{
	UE_LOG(LogTemp, Log, TEXT("JEEBHORSE %s fell (%s)"), *GetName(), bRepWild ? TEXT("wild") : TEXT("owned"));
	if (AKnightCharacter* R = RepRider) R->ServerSideDismount();
	bFollow = false;
	Goal = EGoal::Stay;
	Speed = 0.f;
	DeadT = 0.f;
	SetState(EHorseState::Dead);
	if (AKnightCharacter* O = RepOwner) Tell(O, TEXT("Your horse has fallen. It will be back at its stable to rest."), FColor(255, 150, 120));
}

float AHorse::RestLeft() const
{
	const UWorld* W = GetWorld();
	return W ? FMath::Max(0.f, float(RestUntil - W->GetTimeSeconds())) : 0.f;
}

void AHorse::SendHome(AStable* To, float RestSecs)
{
	if (AKnightCharacter* R = RepRider) R->ServerSideDismount();
	if (To) Stable = To;
	Home = Stable.IsValid() ? Stable->StallSpot() : Home;
	const FVector P(Home.X, Home.Y, JeebFoes::Ground(Home.X, Home.Y));
	SetActorLocationAndRotation(P, FRotator(0.f, Stable.IsValid() ? Stable->GetDef().Yaw + 180.f : 0.f, 0.f));
	Hp->Reset();
	bFollow = false;
	Goal = EGoal::Stay;
	Speed = 0.f;
	RestUntil = RestSecs > 0.f ? GetWorld()->GetTimeSeconds() + RestSecs : -1.0;
	SetState(EHorseState::Idle);
	SendMove();
	ForceNetUpdate();
	UE_LOG(LogTemp, Log, TEXT("JEEBHORSE %s home at %s, resting %.0f s"), *GetName(), Stable.IsValid() ? *Stable->GetKey().ToString() : TEXT("?"), RestSecs);
}

void AHorse::CallTo(AKnightCharacter* K)
{
	if (!K || IsDown()) return;
	if (AKnightCharacter* R = RepRider) { if (R == K) return; R->ServerSideDismount(); Tell(R, TEXT("The horse's owner whistles: it throws you off and gallops away.")); }
	Target = K;
	Goal = EGoal::Call;
	ProgressT = 0.f; ProgressD = FVector::Dist2D(K->GetActorLocation(), GetActorLocation()); StuckT = 0.f;
	// very far: it comes round the corner at once rather than crossing half the world (Lego Star Wars catch-up)
	if (ProgressD > 15000.f) CatchUp(K);
	UE_LOG(LogTemp, Log, TEXT("JEEBHORSE %s called by %s from %.0f m"), *GetName(), *K->GetName(), ProgressD / 100.f);
}

void AHorse::SetFollow(AKnightCharacter* K, bool bOn)
{
	if (!K || IsDown()) return;
	bFollow = bOn;
	Target = K;
	Goal = bOn ? EGoal::Follow : EGoal::Stay;
	if (bOn && FVector::Dist2D(K->GetActorLocation(), GetActorLocation()) > 15000.f) CatchUp(K);
	ForceNetUpdate();
}

void AHorse::SetRider(AKnightCharacter* K)
{
	RepRider = K;
	TameT = 0.f;
	if (K)
	{
		Goal = EGoal::Stay;
		SetState(EHorseState::Ridden);
		if (JeebHorse::WearsSaddle(K) && !bRepSaddled) { bRepSaddled = true; Tell(K, TEXT("You put your saddle on the horse.")); }
		if (bRepWild) Tell(K, TEXT("The wild horse bucks and snorts. Stay on it to calm it."));
	}
	else
	{
		Speed = 0.f;
		Goal = bFollow ? EGoal::Follow : (bRepWild ? EGoal::Wander : EGoal::Stay);
		if (!bRepWild) Home = FVector2D(GetActorLocation());   // (it waits where it was left)
		const FVector L = GetActorLocation();
		SetActorLocation(FVector(L.X, L.Y, JeebFoes::Ground(L.X, L.Y)));
		SetState(EHorseState::Idle);
		SendMove();
	}
	ForceNetUpdate();
}

void AHorse::MakeOwned(AKnightCharacter* K)
{
	if (AStable* S = Stable.Get()) S->Wild.Remove(this);
	bRepWild = false;
	RepOwner = K;
	bRepSaddled = true;   // (the stablemaster gives it a saddle of its own)
	Home = Stable.IsValid() ? Stable->StallSpot() : FVector2D(GetActorLocation());
	if (!RepRider) Goal = EGoal::Stay;
	ForceNetUpdate();
}

bool AHorse::Clear(const FVector& At) const
{
	// a horse-sized ball at chest height touches nothing solid (buildings, walls, fences, rocks, trees)
	FCollisionQueryParams Q(SCENE_QUERY_STAT(HorseClear), false, this);
	// (well clear of the ground, so a slope or a bump in the terrain never reads as a wall)
	return !GetWorld()->OverlapAnyTestByChannel(At + FVector(0.f, 0.f, 175.f), FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeSphere(80.f), Q);
}

bool AHorse::CatchUp(const AKnightCharacter* K)
{
	if (!K) return false;
	const FVector P = K->GetActorLocation();
	const float Yaw = K->GetActorRotation().Yaw;
	// behind and to the side of them first (out of sight), then round them
	for (int32 i = 0; i < 12; ++i)
	{
		const float A = FMath::DegreesToRadians(Yaw + 180.f + (i % 2 ? 1.f : -1.f) * (25.f + 30.f * (i / 2)));
		const float R = i < 6 ? 1100.f : 600.f;
		const FVector2D At(P.X + FMath::Cos(A) * R, P.Y + FMath::Sin(A) * R);
		if (JeebWorld::InWater(At.X, At.Y)) continue;
		const FVector G(At.X, At.Y, JeebFoes::Ground(At.X, At.Y));
		if (FMath::Abs(G.Z - P.Z) > 600.f || !Clear(G)) continue;
		SetActorLocationAndRotation(G, FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan2(P.Y - G.Y, P.X - G.X)), 0.f));
		ProgressT = 0.f; ProgressD = FVector::Dist2D(P, G); StuckT = 0.f;
		SendMove();
		ForceNetUpdate();
		UE_LOG(LogTemp, Log, TEXT("JEEBHORSE %s caught up: appeared %.0f m from %s"), *GetName(), ProgressD / 100.f, *K->GetName());
		return true;
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBHORSE %s could not find room to appear near %s"), *GetName(), *K->GetName());
	return false;
}

bool AHorse::StepToward(const FVector2D& To, float Want, float Dt)
{
	const FVector L = GetActorLocation();
	const FVector2D Me(L);
	FVector2D Dir = (To - Me).GetSafeNormal();
	if (Dir.IsNearlyZero()) return true;
	Speed = FMath::FInterpConstantTo(Speed, Want, Dt, Want > Speed ? 1400.f : 2600.f);
	// feel ahead for anything solid; turn round it if one side is open
	const float Look = FMath::Clamp(Speed * 0.45f, 220.f, 700.f);
	bool bFound = false;
	for (float Turn : { 0.f, 35.f, -35.f, 70.f, -70.f, 105.f, -105.f })
	{
		const FVector2D D = Dir.GetRotated(Turn);
		const FVector2D Ahead = Me + D * Look;
		if (JeebWorld::InWater(Ahead.X, Ahead.Y)) continue;
		if (!Clear(FVector(Ahead.X, Ahead.Y, JeebFoes::Ground(Ahead.X, Ahead.Y)))) continue;
		Dir = D; bFound = true; break;
	}
	if (!bFound) { Speed = 0.f; return false; }
	const FVector2D N = Me + Dir * Speed * Dt;
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
	SetActorLocationAndRotation(FVector(N.X, N.Y, JeebFoes::Ground(N.X, N.Y)), FRotator(0.f, FMath::FixedTurn(GetActorRotation().Yaw, Yaw, 400.f * Dt), 0.f));
	return true;
}

void AHorse::TickMind(float Dt)
{
	StateT += Dt;
	AKnightCharacter* K = Target.Get();
	if ((Goal == EGoal::Call || Goal == EGoal::Follow) && (!K || K->GetHealth() <= 0.f)) { Goal = EGoal::Stay; bFollow = false; Home = FVector2D(GetActorLocation()); }
	// while its owner rides another horse or it is being followed by... nothing: following waits while the owner rides
	const bool bOwnerRiding = K && K->IsMounted();
	FVector2D To = Home;
	float Want = 0.f;
	float Near = 120.f;
	switch (Goal)
	{
	case EGoal::Call:
	{
		To = FVector2D(K->GetActorLocation());
		const float D = FVector2D::Distance(To, FVector2D(GetActorLocation()));
		if (D < 420.f || bOwnerRiding) { Goal = bFollow ? EGoal::Follow : EGoal::Stay; Home = FVector2D(GetActorLocation()); Speed = 0.f; UE_LOG(LogTemp, Log, TEXT("JEEBHORSE %s arrived beside %s"), *GetName(), *K->GetName()); break; }
		Want = FMath::Min(CallSpeed, 300.f + D * 2.2f);
		Near = 0.f;
		break;
	}
	case EGoal::Follow:
	{
		To = FVector2D(K->GetActorLocation());
		const float D = FVector2D::Distance(To, FVector2D(GetActorLocation()));
		if (bOwnerRiding || D < 500.f) { Want = 0.f; break; }
		Want = D > 2500.f ? CallSpeed : D > 1200.f ? TrotSpeed * 1.4f : D > 700.f ? TrotSpeed : WalkSpeed * 2.f;
		if (D > 15000.f) CatchUp(K);
		Near = 0.f;
		break;
	}
	case EGoal::Wander:
	{
		if (StateT > 6.f && Rand.FRand() < Dt * 0.12f)
		{
			WanderTo = Home + FVector2D(Rand.FRandRange(-1.f, 1.f), Rand.FRandRange(-1.f, 1.f)) * 2200.f;
			StateT = 0.f;
		}
		To = WanderTo.IsZero() ? Home : WanderTo;
		Want = FVector2D::Distance(To, FVector2D(GetActorLocation())) > 150.f ? WalkSpeed : 0.f;
		break;
	}
	default:
		To = Home;
		Want = FVector2D::Distance(To, FVector2D(GetActorLocation())) > 400.f && !bRepWild ? WalkSpeed : 0.f;
		break;
	}
	bool bMoved = false;
	if (Want > 0.f)
	{
		bMoved = StepToward(To, Want, Dt);
		// stuck (walled in, or circling without getting nearer): when called or following, it appears near them instead
		if (Goal == EGoal::Call || Goal == EGoal::Follow)
		{
			ProgressT += Dt;
			if (!bMoved) StuckT += Dt; else StuckT = FMath::Max(0.f, StuckT - Dt * 0.5f);
			if (ProgressT > 3.f)
			{
				const float D = FVector2D::Distance(To, FVector2D(GetActorLocation()));
				const bool bNoGain = D > ProgressD - 250.f && D > 900.f;
				ProgressT = 0.f; ProgressD = D;
				if (bNoGain && K) { UE_LOG(LogTemp, Log, TEXT("JEEBHORSE %s made no headway toward %s (%.0f m): catching up"), *GetName(), *K->GetName(), D / 100.f); CatchUp(K); }
			}
			if (StuckT > 1.5f && K) { StuckT = 0.f; UE_LOG(LogTemp, Log, TEXT("JEEBHORSE %s walled in: catching up"), *GetName()); CatchUp(K); }
		}
		else if (!bMoved) { WanderTo = Home; }
	}
	else Speed = FMath::FInterpConstantTo(Speed, 0.f, Dt, 2600.f);
	(void)Near;
	if (!bMoved && Speed < 30.f)
	{
		// standing: face whoever it came to; now and then lower its head to graze
		if (K && (Goal == EGoal::Stay || Goal == EGoal::Follow) && FVector::Dist2D(K->GetActorLocation(), GetActorLocation()) < 900.f)
		{
			const FVector D = K->GetActorLocation() - GetActorLocation();
			SetActorRotation(FRotator(0.f, FMath::FixedTurn(GetActorRotation().Yaw, D.Rotation().Yaw, 90.f * Dt), 0.f));
		}
		const bool bGraze = EHorseState(RepState) == EHorseState::Graze;
		if (StateT > (bGraze ? 9.f : 7.f) && Rand.FRand() < Dt * 0.25f) SetState(bGraze ? EHorseState::Idle : EHorseState::Graze);
		else if (EHorseState(RepState) != EHorseState::Graze && EHorseState(RepState) != EHorseState::Idle) SetState(EHorseState::Idle);
	}
	else SetState(Speed > TrotSpeed * 0.9f ? EHorseState::Run : EHorseState::Walk);
	// healing: 10 s after the last hit, a full bar in 10 s (never inside a PvP zone)
	if (Hp->Cur < Hp->Max && GetWorld()->GetTimeSeconds() - Hp->LastHitTime > 10.f && !JeebPvp::IsInPvpZone(GetWorld(), GetActorLocation()))
		Hp->Cur = FMath::Min(Hp->Max, Hp->Cur + FMath::Max(1, FMath::RoundToInt(Hp->Max * 0.1f * Dt)));
}

// Ridden: carried under its rider on every machine; the server also counts the taming
void AHorse::TickRidden(float Dt)
{
	AKnightCharacter* R = RepRider;
	const FVector L = R->GetActorLocation();
	const FVector V = R->GetVelocity();
	Speed = V.Size2D();
	bBacking = Speed > 20.f && FVector::DotProduct(V.GetSafeNormal2D(), R->GetActorForwardVector()) < -0.3f;
	const FRotator Turned = FMath::RInterpTo(GetActorRotation(), FRotator(0.f, R->GetActorRotation().Yaw, 0.f), Dt, 7.f);
	SetActorLocationAndRotation(FVector(L.X, L.Y, L.Z - R->GetSimpleCollisionHalfHeight()), Turned);
	if (!HasAuthority()) return;
	SendMove();
	if (bRepWild)
	{
		TameT += Dt;
		if (TameT >= JeebTune::V().HorseTameSecs && TamedBy.Get() != R)
		{
			TamedBy = R;
			Tell(R, TEXT("The wild horse calms under you. It trusts you now."), FColor(150, 230, 150));
			UE_LOG(LogTemp, Log, TEXT("JEEBHORSE %s tamed by %s"), *GetName(), *R->GetName());
			UQuestComponent::OnHorseTamed(R, this);
		}
	}
}

void AHorse::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = FMath::Min(DeltaSeconds, 0.25f);
	if (RepRider && IsValid(RepRider))
	{
		TickRidden(Dt);
		TickLook(FMath::Min(Dt, 0.1f), Speed, bBacking);
		return;
	}
	if (HasAuthority())
	{
		if (IsDown())
		{
			DeadT += Dt;
			Speed = 0.f;
			if (DeadT > 5.f)
			{
				if (bRepWild) { Destroy(); return; }
				SendHome(Stable.Get(), JeebTune::V().HorseRestSecs);
			}
		}
#if !UE_BUILD_SHIPPING
		else if (LabRole >= 1)   // (-HorseLab) laps between two spots
		{
			const FVector2D Me(GetActorLocation());
			if (FVector2D::Distance(Me, LabB) < 200.f) Swap(LabA, LabB);
			StepToward(LabB, LabRole == 1 ? TrotSpeed * 1.2f : WalkSpeed * 1.5f, Dt);
			SetState(LabRole == 1 ? EHorseState::Run : EHorseState::Walk);
		}
		else if (LabRole == 0)
		{
			StateT += Dt;
			Speed = 0.f;
			const int32 Part = int32(StateT / 6.f) % 3;
			SetState(Part == 1 ? EHorseState::Graze : EHorseState::Idle);
			if (StateT > 6.f * 3.f) StateT = 0.f;
		}
#endif
		else TickMind(Dt);
		SendMove();
		TickLook(FMath::Min(Dt, 0.1f), Speed, false);
		return;
	}
	// A player's machine: glide toward where the server says it is by now, on this machine's own ground (as the stag)
	if (!bPlaced) return;
	if (!Body) BuildBody();
	const float Yaw = RepMove.Yaw * (360.f / 65536.f);
	const float Since = FMath::Min(float(GetWorld()->GetTimeSeconds() - MoveAt), 0.25f);
	const FVector2D Fw(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)));
	const FVector2D TargetP = FVector2D(RepMove.Loc.X, RepMove.Loc.Y) + Fw * (float(RepMove.Speed) * Since);
	FVector2D Me(GetActorLocation());
	if (FVector2D::DistSquared(Me, TargetP) > FMath::Square(900.f)) Me = TargetP;   // (it appeared somewhere else: no gliding across the map)
	else Me += (TargetP - Me) * (1.f - FMath::Exp(-14.f * Dt));
	SetActorLocationAndRotation(FVector(Me.X, Me.Y, JeebFoes::Ground(Me.X, Me.Y)), FRotator(0.f, FMath::FixedTurn(GetActorRotation().Yaw, Yaw, 720.f * Dt), 0.f));
	TickLook(FMath::Min(Dt, 0.1f), float(RepMove.Speed), false);
}

// Every machine: the clip, the saddle, the lean on a slope, the fall
void AHorse::TickLook(float Dt, float MoveSpeed, bool bBack)
{
	const EHorseState St = StateNow();
	if (St == EHorseState::Dead) Fall = FMath::FInterpConstantTo(Fall, 1.f, Dt, 2.f);
	else Fall = FMath::FInterpConstantTo(Fall, 0.f, Dt, 4.f);
	if (!Body) return;
	const float S = JeebHorse::Scale;
	int32 Clip = St == EHorseState::Graze ? JeebDeer::C_Graze : JeebDeer::C_Idle;
	float Rate = 1.f;
	if (St == EHorseState::Dead) Clip = JeebDeer::C_Death;
	else if (bBack) Clip = JeebDeer::C_Alert;
	else if (MoveSpeed > 25.f)
	{
		const bool bRun = MoveSpeed > 380.f;
		Clip = bRun ? JeebDeer::C_Run : JeebDeer::C_Walk;
		Rate = FMath::Clamp(MoveSpeed / ((bRun ? RunHoof : WalkHoof) * S), 0.5f, bRun ? 3.2f : 2.6f);
	}
	if (Anim) Anim->Drive(Clip, Rate);
	if (SaddleMesh)
	{
		const bool bShow = bRepSaddled && St != EHorseState::Dead;
		if (SaddleMesh->IsVisible() != bShow) SaddleMesh->SetVisibility(bShow);
		if (bShow)
		{
			// the saddle rides on the back bone, placed from the model's rest pose (jeeb.Horse.Saddle)
			static FString Last; static FTransform Rel;
			const FString Now = CVarSaddle.GetValueOnGameThread();
			static FName Bone(TEXT("joint_11"));
			if (Now != Last || SaddleMesh->GetAttachSocketName() != Bone)
			{
				Last = Now;
				TArray<FString> P; Now.ParseIntoArrayWS(P);
				const float Fwd = P.IsValidIndex(0) ? FCString::Atof(*P[0]) : 0.f, Up = P.IsValidIndex(1) ? FCString::Atof(*P[1]) : 100.f;
				const float SYaw = P.IsValidIndex(2) ? FCString::Atof(*P[2]) : 0.f, SPitch = P.IsValidIndex(3) ? FCString::Atof(*P[3]) : 0.f;
				const FReferenceSkeleton& RS = Body->GetSkeletalMeshAsset()->GetRefSkeleton();
				auto CompRef = [&](int32 i) { FTransform T = FTransform::Identity; while (i != INDEX_NONE) { T = T * RS.GetRefBonePose()[i]; i = RS.GetParentIndex(i); } return T; };
				const int32 Bi = RS.FindBoneIndex(Bone), Hi = RS.FindBoneIndex(TEXT("joint_29")), Ri = RS.FindBoneIndex(TEXT("root"));
				FVector Fw(0.f, 1.f, 0.f);
				if (Hi != INDEX_NONE && Ri != INDEX_NONE) Fw = (CompRef(Hi).GetLocation() - CompRef(Ri).GetLocation()).GetSafeNormal2D();
				const FVector Mid = Ri != INDEX_NONE ? CompRef(Ri).GetLocation() : FVector::ZeroVector;
				const FTransform Want(FRotator(SPitch, Fw.Rotation().Yaw + SYaw, 0.f), FVector(Mid.X, Mid.Y, 0.f) + Fw * Fwd + FVector(0.f, 0.f, Up));
				Rel = Bi != INDEX_NONE ? Want.GetRelativeTransform(CompRef(Bi)) : Want;
				SaddleMesh->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Bi != INDEX_NONE ? Bone : NAME_None);
				SaddleMesh->SetRelativeTransform(Rel);
			}
		}
	}
	const FVector L = GetActorLocation();
	const FVector Fw = GetActorForwardVector();
	if (!HasRider())
	{
		const float Ahead = JeebFoes::Ground(L.X + Fw.X * 80.f * S, L.Y + Fw.Y * 80.f * S), Back = JeebFoes::Ground(L.X - Fw.X * 80.f * S, L.Y - Fw.Y * 80.f * S);
		SlopePitch = FMath::FInterpTo(SlopePitch, FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(Ahead - Back, 160.f * S)), -20.f, 20.f), Dt, 6.f);
	}
	else SlopePitch = FMath::FInterpTo(SlopePitch, 0.f, Dt, 6.f);
	Body->SetRelativeLocation(FVector(0.f, 0.f, -40.f * S * FMath::SmoothStep(0.f, 1.f, Fall)));
	const float MeshYaw = CVarHorseMeshYaw.GetValueOnGameThread();
	Body->SetRelativeRotation((FRotator(SlopePitch, 0.f, 0.f).Quaternion() * FRotator(0.f, MeshYaw, 0.f).Quaternion()).Rotator());
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())   // (looks only: this machine's own camera)
	{
		FVector CamL; FRotator CamR;
		PC->GetPlayerViewPoint(CamL, CamR);
		const bool bShadow = FVector::DistSquared(CamL, L) < FMath::Square(7000.f);
		if (Body->CastShadow != bShadow) Body->SetCastShadow(bShadow);
	}
}

// ---------------------------------------------------------------------------------------------- the stable

AStable::AStable()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 1.f;
	bReplicates = false;   // (every machine builds its own from Stables.csv; the server alone keeps the wild horses)
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AStable::SetupStable(const JeebHorse::FStableDef& D)
{
	Def = D;
	// Stables.csv gives the place; if the yard (12 x 9 m) would sit on a wall, in water or on a slope, it moves to the nearest open, level
	// ground (the same answer on every machine: the towns are built from fixed seeds before the stables)
	if (UWorld* W = GetWorld())
	{
		auto Open = [&](const FVector2D& P)
		{
			if (JeebWorld::InWater(P.X, P.Y)) return false;
			float Lo = JeebFoes::Ground(P.X, P.Y), Hi = Lo;
			for (int32 a = 0; a < 8; ++a)
			{
				const FVector2D Q = P + FVector2D(FMath::Cos(a * PI / 4.f), FMath::Sin(a * PI / 4.f)) * 700.f;
				if (JeebWorld::InWater(Q.X, Q.Y)) return false;
				const float H = JeebFoes::Ground(Q.X, Q.Y); Lo = FMath::Min(Lo, H); Hi = FMath::Max(Hi, H);
			}
			if (Hi - Lo > 180.f) return false;
			FCollisionObjectQueryParams Obj; Obj.AddObjectTypesToQuery(ECC_WorldStatic);
			return !W->OverlapAnyTestByObjectType(FVector(P.X, P.Y, Lo + 200.f), FQuat::Identity, Obj, FCollisionShape::MakeSphere(750.f));
		};
		const FVector2D Was = Def.Pos;
		for (int32 i = 0; i < 400 && !Open(Def.Pos); ++i)
		{
			const float R = 300.f * FMath::Sqrt(float(i + 1)), A = i * 2.39996f;
			Def.Pos = Was + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R;
		}
		if (Def.Pos != Was) { Def.Wild += Def.Pos - Was; UE_LOG(LogTemp, Log, TEXT("JEEBHORSE stable %s moved %.0f m to open ground: %.0f %.0f (%.1f m north, %.1f m east of its Stables.csv spot)"), *Def.Key.ToString(), FVector2D::Distance(Was, Def.Pos) / 100.f, Def.Pos.X, Def.Pos.Y, (Def.Pos.X - Was.X) / 100.f, (Def.Pos.Y - Was.Y) / 100.f); }
	}
	SetActorLocationAndRotation(FVector(Def.Pos.X, Def.Pos.Y, JeebFoes::Ground(Def.Pos.X, Def.Pos.Y)), FRotator(0.f, Def.Yaw, 0.f));
}

AStable* AStable::Find(const UWorld* World, FName Key)
{
	if (World) for (TActorIterator<AStable> It(World); It; ++It) if (It->GetKey() == Key) return *It;
	return nullptr;
}

AStable* AStable::Nearest(const UWorld* World, const FVector2D& At, float MaxDist)
{
	AStable* Best = nullptr;
	if (World) for (TActorIterator<AStable> It(World); It; ++It)
	{
		const float D = FVector2D::Distance(FVector2D(It->GetActorLocation()), At);
		if (D < MaxDist) { MaxDist = D; Best = *It; }
	}
	return Best;
}

FVector2D AStable::StallSpot() const
{
	// inside the yard, toward its back rail
	const FVector2D Fw(FMath::Cos(FMath::DegreesToRadians(Def.Yaw)), FMath::Sin(FMath::DegreesToRadians(Def.Yaw)));
	return Def.Pos - Fw * 150.f;
}

void AStable::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocationAndRotation(FVector(Def.Pos.X, Def.Pos.Y, JeebFoes::Ground(Def.Pos.X, Def.Pos.Y)), FRotator(0.f, Def.Yaw, 0.f));   // (the yard's place, whatever the spawn did)
	BuildYard();
	WildSeed = int32(GetTypeHash(Def.Key) % 1000u) * 10;
}

// The yard, 12 x 9 m, open toward Yaw: picket fence on three sides with a gap, hay, a water tub, a hay wagon, a hitching rail
void AStable::BuildYard()
{
	if (IsRunningDedicatedServer()) return;   // (looks only: nothing here is solid)
	int32 N = 0;
	auto Put = [&](const TCHAR* Mesh, float Fwd, float Side, float Yaw, float Scale = 1.f)
	{
		UStaticMesh* M = YardMesh(Mesh);
		if (!M) { UE_LOG(LogTemp, Warning, TEXT("JEEBHORSE stable: no /Game/TownDecor/SM_%s"), Mesh); return; }
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("Yard%d"), N++));
		C->SetStaticMesh(M);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCanEverAffectNavigation(false);
		C->SetupAttachment(GetRootComponent());
		const FVector2D Fw(FMath::Cos(FMath::DegreesToRadians(Def.Yaw)), FMath::Sin(FMath::DegreesToRadians(Def.Yaw)));
		const FVector2D Rt(-Fw.Y, Fw.X);
		const FVector2D P = Def.Pos + Fw * Fwd + Rt * Side;
		C->SetWorldLocationAndRotation(FVector(P.X, P.Y, JeebFoes::Ground(P.X, P.Y)), FRotator(0.f, Def.Yaw + Yaw, 0.f));
		C->SetWorldScale3D(FVector(Scale));
		C->RegisterComponent();
	};
	const float HalfL = 450.f, HalfW = 600.f;   // (forward half-length, side half-width)
	const UStaticMesh* Pk = YardMesh(TEXT("FencePicket"));
	const float Seg = Pk ? FMath::Max(80.f, Pk->GetBounds().BoxExtent.GetMax() * 2.f) : 200.f;
	// back rail (along the side axis) and the two sides; the front has a wide gate in the middle
	for (float s = -HalfW + Seg * 0.5f; s < HalfW; s += Seg) Put(TEXT("FencePicket"), -HalfL, s, 90.f);
	for (float f = -HalfL + Seg * 0.5f; f < HalfL; f += Seg) { Put(TEXT("FencePicket"), f, -HalfW, 0.f); Put(TEXT("FencePicket"), f, HalfW, 0.f); }
	for (float s = -HalfW + Seg * 0.5f; s < -200.f; s += Seg) { Put(TEXT("FencePicket"), HalfL, s, 90.f); Put(TEXT("FencePicket"), HalfL, -s, 90.f); }
	Put(TEXT("FenceStickPost"), HalfL, -200.f, 0.f);
	Put(TEXT("FenceStickPost"), HalfL, 200.f, 0.f);
	Put(TEXT("HayBale"), -330.f, -430.f, 10.f);
	Put(TEXT("HayBale"), -330.f, -300.f, -6.f);
	Put(TEXT("HayBaleRound"), -300.f, 430.f, 0.f);
	Put(TEXT("WoodTub"), -320.f, 120.f, 0.f);
	Put(TEXT("Pail"), -280.f, 230.f, 30.f);
	Put(TEXT("HayWagon"), 150.f, HalfW + 320.f, 90.f);
	Put(TEXT("Signpost"), HalfL + 120.f, -330.f, 180.f);
	UE_LOG(LogTemp, Log, TEXT("JEEBHORSE stable %s built at %.0f %.0f (%d props), wild meadow %.0f %.0f"), *Def.Key.ToString(), Def.Pos.X, Def.Pos.Y, N, Def.Wild.X, Def.Wild.Y);
}

void AStable::SpawnWild()
{
	const FVector2D C = Def.Wild;
	for (int32 Try = 0; Try < 8; ++Try)
	{
		FRandomStream R(WildSeed++);
		const FVector2D P = C + FVector2D(R.FRandRange(-1.f, 1.f), R.FRandRange(-1.f, 1.f)) * 1500.f;
		if (JeebWorld::InWater(P.X, P.Y)) continue;
		const FTransform Xf(FRotator(0.f, R.FRandRange(0.f, 360.f), 0.f), FVector(P.X, P.Y, JeebFoes::Ground(P.X, P.Y)));
		AHorse* H = GetWorld()->SpawnActorDeferred<AHorse>(AHorse::StaticClass(), Xf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!H) return;
		H->SetupHorse(this, true, nullptr, false, WildSeed * 7 + 3);
		H->FinishSpawning(Xf);
		Wild.Add(H);
		UE_LOG(LogTemp, Log, TEXT("JEEBHORSE wild horse %s at %.0f %.0f (%s)"), *H->GetName(), P.X, P.Y, *Def.Key.ToString());
		return;
	}
}

#if !UE_BUILD_SHIPPING
void JeebHorseTestTick(AStable* St, float Dt);   // (-JeebHorseTest, HorseTest.cpp)
#endif
void AStable::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
	JeebHorseTestTick(this, DeltaSeconds);
#endif
	if (GetNetMode() == NM_Client) return;   // (each machine has its own stable actor; only the server keeps horses)
	Wild.RemoveAll([](const TWeakObjectPtr<AHorse>& H) { return !H.IsValid() || !H->IsWild(); });
	WildClock += DeltaSeconds;
	// keep the meadow stocked: at first all at once, then one at a time, every minute, while nobody stands right there
	const bool bFirst = WildClock < 5.f;
	if (Wild.Num() < Def.WildCount && (bFirst || WildClock > 60.f))
	{
		bool bWatched = false;
		for (TActorIterator<AKnightCharacter> It(GetWorld()); It && !bFirst; ++It) bWatched |= FVector2D::Distance(FVector2D(It->GetActorLocation()), Def.Wild) < 2500.f;
		if (!bWatched)
		{
			if (!bFirst) WildClock = 5.f;
			if (bFirst) while (Wild.Num() < Def.WildCount) { const int32 Was = Wild.Num(); SpawnWild(); if (Wild.Num() == Was) break; }
			else SpawnWild();
		}
	}
}

// ---------------------------------------------------------------------------------------------- the rider's ownership

URiderComponent::URiderComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 1.f;
	SetIsReplicatedByDefault(true);
}

void URiderComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(URiderComponent, bOwnsHorse, COND_OwnerOnly);
}

void URiderComponent::Restore(bool bIn, const FString& InStable)
{
	bOwnsHorse = bIn;
	StableKey = InStable.IsEmpty() ? NAME_None : FName(*InStable);
	if (bOwnsHorse && !JeebHorse::FindStable(StableKey) && JeebHorse::Stables().Num() > 0) StableKey = JeebHorse::Stables()[0].Key;
}

void URiderComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AKnightCharacter* K = Cast<AKnightCharacter>(GetOwner());
	if (!K || !K->HasAuthority() || !bOwnsHorse || Horse.IsValid()) return;
	if (!K->GetController()) return;   // (a body nobody plays yet: the login hasn't finished)
	// the owned horse comes into the world at its stable (the stable last used, else the first)
	AStable* S = AStable::Find(GetWorld(), StableKey);
	if (!S && JeebHorse::Stables().Num() > 0) S = AStable::Find(GetWorld(), JeebHorse::Stables()[0].Key);
	if (!S) return;
	StableKey = S->GetKey();
	const FVector2D P = S->StallSpot();
	const FTransform Xf(FRotator(0.f, S->GetDef().Yaw + 180.f, 0.f), FVector(P.X, P.Y, JeebFoes::Ground(P.X, P.Y)));
	AHorse* H = GetWorld()->SpawnActorDeferred<AHorse>(AHorse::StaticClass(), Xf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!H) return;
	H->SetupHorse(S, false, K, true, int32(GetTypeHash(K->GetName()) % 10000u));
	H->FinishSpawning(Xf);
	Horse = H;
	UE_LOG(LogTemp, Log, TEXT("JEEBHORSE %s's horse %s waits at %s"), *K->GetName(), *H->GetName(), *StableKey.ToString());
}

void URiderComponent::GrantHorse(AHorse* Tamed)
{
	AKnightCharacter* K = Cast<AKnightCharacter>(GetOwner());
	if (!K) return;
	if (AHorse* Old = Horse.Get()) { if (Old != Tamed) Old->Destroy(); }
	bOwnsHorse = true;
	if (Tamed && !Tamed->IsDown() && Tamed->IsWild())
	{
		Tamed->MakeOwned(K);
		Horse = Tamed;
		if (AStable* S = Tamed->GetStable()) StableKey = S->GetKey();
	}
	if (StableKey.IsNone() && JeebHorse::Stables().Num() > 0) StableKey = JeebHorse::Stables()[0].Key;
	UE_LOG(LogTemp, Log, TEXT("JEEBHORSE %s now owns a horse (%s), stabled at %s"), *K->GetName(), Horse.IsValid() ? *Horse->GetName() : TEXT("a new one at the stable"), *StableKey.ToString());
}

void URiderComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	// the owner left: the horse goes too (it comes back at the stable next time they play)
	if (AHorse* H = Horse.Get()) { if (AKnightCharacter* R = H->GetRider()) if (R != GetOwner()) R->ServerSideDismount(); H->Destroy(); }
	Super::EndPlay(Reason);
}

// ---------------------------------------------------------------------------------------------- test pictures

#if !UE_BUILD_SHIPPING
// -HorseLab=X_Y_Yaw: three horses there: one standing (idle, graze, idle, 6 s each) with a saddle, one cantering laps, one walking laps
void AHorse::BuildLab(UWorld* World)
{
	FString V;
	if (!World || !FParse::Value(FCommandLine::Get(), TEXT("HorseLab="), V, false)) return;
	TArray<FString> P;
	V.ParseIntoArray(P, TEXT("_"));
	if (P.Num() < 2) return;
	const FVector2D At(FCString::Atof(*P[0]), FCString::Atof(*P[1]));
	const float Yaw = P.Num() > 2 ? FCString::Atof(*P[2]) : 0.f;
	const FVector2D Fw(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw))), Rt(-Fw.Y, Fw.X);
	const FVector2D Spots[3] = { At, At + Rt * 600.f - Fw * 1800.f, At + Rt * 1000.f - Fw * 900.f };
	for (int32 i = 0; i < 3; ++i)
	{
		const FTransform Xf(FRotator(0.f, i == 0 ? Yaw + 90.f : Yaw, 0.f), FVector(Spots[i].X, Spots[i].Y, JeebFoes::Ground(Spots[i].X, Spots[i].Y)));
		AHorse* H = World->SpawnActorDeferred<AHorse>(AHorse::StaticClass(), Xf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!H) continue;
		H->SetupHorse(nullptr, false, nullptr, i != 2, 900 + i);
		H->LabRole = i;
		H->LabA = Spots[i]; H->LabB = Spots[i] + Fw * 3600.f;
		H->FinishSpawning(Xf);
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBHORSE lab: three horses at %.0f %.0f (ground %.0f), laps toward yaw %.0f"), At.X, At.Y, JeebFoes::Ground(At.X, At.Y), Yaw);
}
#endif

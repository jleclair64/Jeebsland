#include "Eagle.h"
#include "OakForest.h"
#include "FoeData.h"
#include "HealthComponent.h"
#include "KnightCharacter.h"
#include "SkillComponent.h"
#include "Inventory.h"
#include "Loot.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"

static TAutoConsoleVariable<int32> CVarEagles(TEXT("jeeb.Eagle.Count"), 5,
	TEXT("How many eagles the server places over the wild herds when the world is built (0: none). Read once, on the server."), ECVF_Default);
static TAutoConsoleVariable<float> CVarEagleYaw(TEXT("jeeb.Eagle.MeshYaw"), 0.f,
	TEXT("Degrees the eagle's two models are turned to face the way it flies (looks only)."), ECVF_Default);

namespace
{
	// the numbers (2026-11-26): health within the arrow rule (an early arrow 6.5-9: two; from about Archery 25 one)
	constexpr int32 EagleHealth = 12;
	constexpr float CruiseSpeed = 1100.f, LandSpeed = 500.f;   // cm/s
	constexpr float PeckMin = 5.f, PeckMax = 9.f;              // seconds on a kill
	constexpr float PerchMin = 25.f, PerchMax = 60.f;
	constexpr float KillReach = 30000.f;                       // a kill this near (300 m) may bring one down
	constexpr float Startle = 450.f;                           // a player this near a pecking or perched eagle sends it off
	constexpr float RespawnSecs = 480.f;
	constexpr int32 HuntXP = 30, ArcheryXP = 15;

	UStaticMesh* MeshOf(const TCHAR* Name)
	{
		static TMap<FString, UStaticMesh*> All;   // (outlives worlds, as the animals' cache)
		if (UStaticMesh** M = All.Find(Name)) return *M;
		UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Animals/%s.%s"), Name, Name));
		if (M) M->AddToRoot();
		UE_LOG(LogTemp, Log, TEXT("JEEBEAGLE model %s: %s"), Name, M ? TEXT("ok") : TEXT("MISSING"));
		All.Add(Name, M);
		return M;
	}
	AKnightCharacter* HitterOf(AActor* From)
	{
		if (!From) return nullptr;
		if (AKnightCharacter* K = Cast<AKnightCharacter>(From)) return K;
		if (AKnightCharacter* K = Cast<AKnightCharacter>(From->GetInstigator())) return K;
		return Cast<AKnightCharacter>(From->GetOwner());
	}
	float NearestPlayer(UWorld* W, const FVector& P)
	{
		float Best = 1.0e9f;
		for (TActorIterator<AKnightCharacter> It(W); It; ++It) if (It->GetHealth() > 0.f) Best = FMath::Min(Best, float(FVector::Dist(It->GetActorLocation(), P)));
		return Best;
	}
	const TCHAR* const StateNames[] = { TEXT("circle"), TEXT("land"), TEXT("perch"), TEXT("peck"), TEXT("leave"), TEXT("fall"), TEXT("down") };
}

AEagle::AEagle()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetNetUpdateFrequency(10.f);
	SetNetCullDistanceSquared(FMath::Square(30000.f));
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	JBody = CreateDefaultSubobject<USceneComponent>(TEXT("JBody"));
	JBody->SetupAttachment(Root);
	for (int32 i = 0; i < 2; ++i)
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(i ? TEXT("SitBody") : TEXT("FlyBody"));
		C->SetupAttachment(Root);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetCanEverAffectNavigation(false);
		C->SetMobility(EComponentMobility::Movable);
		C->bAffectDynamicIndirectLighting = false;
		C->CastShadow = false;
		C->bAllowCullDistanceVolume = false;   // (the map's cull volume hides anything this small)
		(i ? SitBody : FlyBody) = C;
	}
	Hp = CreateDefaultSubobject<UHealthComponent>(TEXT("Hp"));
	Hp->Max = Hp->Cur = EagleHealth;
	Hp->BarHeight = 70.f; Hp->HitHeight = 10.f; Hp->HitRadius = 100.f;
	Rand.Initialize(4411);
}

void AEagle::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AEagle, RepLoc);
	DOREPLIFETIME(AEagle, RepVel);
	DOREPLIFETIME(AEagle, RepYaw);
	DOREPLIFETIME(AEagle, RepState);
}

void AEagle::SpawnAll(UWorld* World, const TArray<FVector2D>& Homes)
{
	const int32 N = FMath::Min(CVarEagles.GetValueOnGameThread(), Homes.Num());
	if (!World || N <= 0) { UE_LOG(LogTemp, Log, TEXT("JEEBEAGLE no eagles (%d homes)"), Homes.Num()); return; }
	// spread: every (Homes / N)th home, in the herds' fixed order (the same world every time)
	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D H = Homes[(i * Homes.Num()) / N];
		const FTransform Xf(FRotator::ZeroRotator, FVector(H.X, H.Y, JeebFoes::Ground(H.X, H.Y) + 4500.f));
		AEagle* E = World->SpawnActorDeferred<AEagle>(AEagle::StaticClass(), Xf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!E) continue;
		E->Home = H; E->Rand.Initialize(4411 + i * 97);
		E->Radius = E->Rand.FRandRange(3500.f, 6500.f); E->Height = E->Rand.FRandRange(3800.f, 5500.f);
		E->CircleA = E->Rand.FRandRange(0.f, 2.f * PI); E->CircleDir = E->Rand.FRand() < 0.5f ? -1.f : 1.f;
		E->PerchClock = E->Rand.FRandRange(40.f, 120.f);
		E->FinishSpawning(Xf);
		UE_LOG(LogTemp, Log, TEXT("JEEBEAGLE %s circles over %.0f %.0f (radius %.0f m, %.0f m up)"), *E->GetName(), H.X, H.Y, E->Radius / 100.f, E->Height / 100.f);
	}
}

void AEagle::SpawnLab(UWorld* World, const FVector2D& At, const FVector2D& Fw)
{
	// two low birds for pictures: one circling close over the lab, one that goes to perch at once
	for (int32 i = 0; i < 2; ++i)
	{
		const FVector2D H = At + Fw * (i == 0 ? 1500.f : 600.f);
		const FTransform Xf(FRotator::ZeroRotator, FVector(H.X, H.Y, JeebFoes::Ground(H.X, H.Y) + 1500.f));
		AEagle* E = World->SpawnActorDeferred<AEagle>(AEagle::StaticClass(), Xf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!E) continue;
		E->Home = H; E->bLab = true; E->Rand.Initialize(77 + i);
		E->Radius = 1600.f; E->Height = 1200.f; E->PerchClock = i == 0 ? 1.0e6f : 2.f;
		E->FinishSpawning(Xf);
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBEAGLE lab: two eagles over %.0f %.0f"), At.X, At.Y);
}

void AEagle::OnKill(UWorld* World, const FVector& Body)
{
	if (!World) return;
	AEagle* Best = nullptr;
	float BestD = KillReach;
	for (TActorIterator<AEagle> It(World); It; ++It)
	{
		const EEagleState S = It->StateNow();
		if (S != EEagleState::Circle && S != EEagleState::Perch) continue;
		const float D = FVector::Dist2D(It->GetActorLocation(), Body);
		if (D < BestD) { BestD = D; Best = *It; }
	}
	if (!Best) return;
	if (!Best->bLab && Best->Rand.FRand() > 0.7f) { UE_LOG(LogTemp, Log, TEXT("JEEBEAGLE %s ignores a kill %.0f m off"), *Best->GetName(), BestD / 100.f); return; }
	// it lands beside the body, on the ground
	const float A = Best->Rand.FRandRange(0.f, 2.f * PI);
	const FVector2D P = FVector2D(Body) + FVector2D(FMath::Cos(A), FMath::Sin(A)) * 160.f;   // (beside the body, not inside it: a lying stag is 2 m long)
	Best->KillAt = Body;
	Best->Goal = FVector(P.X, P.Y, JeebFoes::Ground(P.X, P.Y));
	Best->SetState(EEagleState::Land, *FString::Printf(TEXT("a kill %.0f m off"), BestD / 100.f));
}

void AEagle::BeginPlay()
{
	Super::BeginPlay();
	Hp->GatherHurt = [this](TArray<JeebHit::FCapsule>& Out) { GatherHurt(Out); };
	if (HasAuthority())
	{
		Hp->OnDamaged = [this](AActor* From) { OnHurt(From); };
		Vel = FVector(FMath::Cos(CircleA), FMath::Sin(CircleA), 0.f) * CruiseSpeed;
		RepLoc = GetActorLocation(); RepVel = Vel;
	}
	if (!IsRunningDedicatedServer())
	{
		FlyBody->SetStaticMesh(MeshOf(TEXT("SM_EagleFly")));
		SitBody->SetStaticMesh(MeshOf(TEXT("SM_EaglePerch")));
#if !UE_BUILD_SHIPPING
		if (FParse::Param(FCommandLine::Get(), TEXT("EaglePlain")))   // (test: the engine's cube and plain material, to tell the model from the drawing)
		{
			UMaterialInterface* Plain = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
			SitBody->SetMaterial(0, Plain); FlyBody->SetMaterial(0, Plain);
			if (FParse::Param(FCommandLine::Get(), TEXT("EagleCube"))) SitBody->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		}
#endif
		LastYaw = GetActorRotation().Yaw;
		TickLook(0.f);
	}
}

void AEagle::OnRep_State() { }

void AEagle::SetState(EEagleState S, const TCHAR* Why)
{
	if (StateNow() == S) return;
	UE_LOG(LogTemp, Log, TEXT("JEEBEAGLE %s %s -> %s: %s; at %.0f %.0f %.0f (%.0f m over the ground)"), *GetName(), StateNames[RepState], StateNames[uint8(S)], Why,
		GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z, (GetActorLocation().Z - JeebFoes::Ground(GetActorLocation().X, GetActorLocation().Y)) / 100.f);
	RepState = uint8(S);
	StateT = 0.f;
	if (S == EEagleState::Peck) StateFor = Rand.FRandRange(PeckMin, PeckMax);
	if (S == EEagleState::Perch) StateFor = Rand.FRandRange(PerchMin, PerchMax);
	ForceNetUpdate();
}

// The bird's hit boxes: a body along its heading and its spread wings in the air; an upright body sitting
void AEagle::GatherHurt(TArray<JeebHit::FCapsule>& Out) const
{
	const EEagleState S = StateNow();
	if (S == EEagleState::Down || S == EEagleState::Fall) return;
	const FTransform Xf(FRotator(0.f, GetActorRotation().Yaw, 0.f), GetActorLocation());
	auto Cap = [&](const FVector& A, const FVector& B, float R, uint8 Zone) { JeebHit::FCapsule C; C.A = Xf.TransformPosition(A); C.B = Xf.TransformPosition(B); C.R = R; C.Zone = Zone; Out.Add(C); };
	if (Flying())
	{
		Cap(FVector(40.f, 0.f, 0.f), FVector(-35.f, 0.f, 0.f), 16.f, JeebHit::Z_Torso);
		Cap(FVector(45.f, 0.f, 4.f), FVector(45.f, 0.f, 4.f), 10.f, JeebHit::Z_Head);
		Cap(FVector(0.f, -90.f, 6.f), FVector(0.f, 90.f, 6.f), 13.f, JeebHit::Z_Leg);   // (the wings: "leg" damage)
	}
	else
	{
		Cap(FVector(0.f, 0.f, 18.f), FVector(-6.f, 0.f, 52.f), 16.f, JeebHit::Z_Torso);
		Cap(FVector(4.f, 0.f, 68.f), FVector(4.f, 0.f, 68.f), 9.f, JeebHit::Z_Head);
	}
}

void AEagle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = FMath::Min(DeltaSeconds, 1.f);
	if (HasAuthority())
	{
		TickServer(Dt);
		RepLoc = GetActorLocation(); RepVel = Vel;
		RepYaw = uint16(FMath::RoundToInt(FRotator::ClampAxis(GetActorRotation().Yaw) * (65536.f / 360.f)) & 0xFFFF);
	}
	else
	{
		// glide toward where the server says it is by now
		const FVector Target = FVector(RepLoc) + FVector(RepVel) * 0.1f;
		FVector Me = GetActorLocation();
		if (FVector::DistSquared(Me, Target) > FMath::Square(3000.f)) Me = Target;
		else Me += (Target - Me) * (1.f - FMath::Exp(-6.f * Dt)) + FVector(RepVel) * Dt * 0.5f;
		Vel = RepVel;
		SetActorLocationAndRotation(Me, FRotator(0.f, FMath::FixedTurn(GetActorRotation().Yaw, RepYaw * (360.f / 65536.f), 360.f * Dt), 0.f));
	}
	if (FlyBody->GetStaticMesh()) TickLook(Dt);
	if (bLab && FlyBody && FMath::Fmod(GetWorld()->GetTimeSeconds(), 3.0) < Dt)   // (test lab: where it is and what shows)
		UE_LOG(LogTemp, Log, TEXT("JEEBEAGLE lab %s %s at %.0f %.0f %.0f, fly body %s r %.0f, sit body %s r %.0f"), *GetName(), StateNames[RepState], GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z,
			FlyBody->IsVisible() ? (FlyBody->WasRecentlyRendered(0.5f) ? TEXT("shown, drawn") : FlyBody->IsRegistered() ? TEXT("shown, not drawn") : TEXT("unregistered")) : TEXT("hidden"), FlyBody->Bounds.SphereRadius, SitBody->IsVisible() ? (SitBody->WasRecentlyRendered(0.5f) ? TEXT("shown, drawn") : TEXT("shown, not drawn")) : TEXT("hidden"), SitBody->Bounds.SphereRadius); if (bLab) UE_LOG(LogTemp, Log, TEXT("JEEBEAGLE lab sit at %s scale %s bounds %s proxy %d reg %d hidden %d/%d mat %s maxdraw %.0f"), *SitBody->GetComponentLocation().ToString(), *SitBody->GetComponentScale().ToString(), *SitBody->Bounds.Origin.ToString(), SitBody->SceneProxy != nullptr, SitBody->IsRegistered(), IsHidden(), SitBody->bHiddenInGame, *GetNameSafe(SitBody->GetMaterial(0)), SitBody->CachedMaxDrawDistance);
}

bool AEagle::Fly(const FVector& To, float Want, float Dt)
{
	const FVector Me = GetActorLocation();
	const FVector D = To - Me;
	const float Dist = D.Size();
	if (Dist < FMath::Max(60.f, Want * Dt * 1.5f)) { SetActorLocation(To); Vel = FVector::ZeroVector; return true; }
	// turn the heading toward the goal at a bird's rate, and climb or sink toward its height
	const FVector Flat = FVector(D.X, D.Y, 0.f).GetSafeNormal();
	FVector H = FVector(Vel.X, Vel.Y, 0.f).GetSafeNormal();
	if (H.IsNearlyZero()) H = Flat;
	const float Ang = FMath::Atan2(FVector::CrossProduct(H, Flat).Z, FVector::DotProduct(H, Flat));
	const float Turn = FMath::Clamp(Ang, -1.4f * Dt, 1.4f * Dt);
	H = H.RotateAngleAxisRad(Turn, FVector::UpVector);
	const float Spd = FMath::Min(Want, FMath::Max(LandSpeed, Dist * 0.9f));
	const float Climb = FMath::Clamp(D.Z * 0.8f, -500.f, 300.f);
	// close in: straight at it (a swoop, not an orbit round the goal)
	FVector V = FVector::DistSquared2D(Me, To) < FMath::Square(1500.f) ? D.GetSafeNormal() * Spd : H * Spd + FVector(0.f, 0.f, Climb);
	Vel = V;
	FVector N = Me + V * Dt;
	const float Floor = JeebFoes::Ground(N.X, N.Y) + 150.f;
	if (N.Z < Floor && FVector::DistSquared2D(N, To) > FMath::Square(1500.f)) N.Z = Floor;
	SetActorLocationAndRotation(N, FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan2(V.Y, V.X)), 0.f));
	return false;
}

bool AEagle::FindPerch(FVector& Out)
{
	// a tree top near home: straight down from high, and the first solid thing well over the ground is a tree
	// the forest's own tree list first (2026-11-26: the trunk solids are hidden collision, so the straight-down trace found nothing)
	if (const AOakForest* F = AOakForest::Instance(GetWorld()))
		if (F->RandomTreeTop(Home, bLab ? 4000.f : 9000.f, Rand, Out))
		{
			UE_LOG(LogTemp, Log, TEXT("JEEBEAGLE %s found a perch on a forest tree, %.1f m up"), *GetName(), (Out.Z - JeebFoes::Ground(Out.X, Out.Y)) / 100.f);
			return true;
		}
	FCollisionObjectQueryParams Obj;
	Obj.AddObjectTypesToQuery(ECC_WorldStatic);
	for (int32 t = 0; t < 16; ++t)
	{
		const float A = Rand.FRandRange(0.f, 2.f * PI);
		const FVector2D P = Home + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rand.FRandRange(0.f, bLab ? 4000.f : 9000.f);
		const float G = JeebFoes::Ground(P.X, P.Y);
		FHitResult Hit;
		FCollisionQueryParams Q(TEXT("EaglePerch"), false, this);
		if (!GetWorld()->LineTraceSingleByObjectType(Hit, FVector(P.X, P.Y, G + 6000.f), FVector(P.X, P.Y, G - 100.f), Obj, Q)
			&& !GetWorld()->LineTraceSingleByChannel(Hit, FVector(P.X, P.Y, G + 6000.f), FVector(P.X, P.Y, G - 100.f), ECC_Visibility, Q)) continue;
		if (Hit.ImpactPoint.Z < G + 500.f || Hit.ImpactNormal.Z < 0.3f) continue;   // the ground, a rock, a wall
		Out = Hit.ImpactPoint;
		UE_LOG(LogTemp, Log, TEXT("JEEBEAGLE %s found a perch on %s, %.1f m up"), *GetName(), Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("?"), (Out.Z - G) / 100.f);
		return true;
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBEAGLE %s found no tree to perch on near %.0f %.0f"), *GetName(), Home.X, Home.Y);
	return false;
}

void AEagle::TickServer(float Dt)
{
	StateT += Dt;
	// asleep when nobody is near: a slow tick and sparse updates (it keeps circling, roughly)
	SleepClock -= Dt;
	if (SleepClock <= 0.f)
	{
		SleepClock = 1.f;
		const bool bSleep = !bLab && NearestPlayer(GetWorld(), GetActorLocation()) > 25000.f;
		if (bSleep != bSleeping)
		{
			bSleeping = bSleep;
			SetActorTickInterval(bSleep ? 0.5f : 0.f);
			SetNetUpdateFrequency(bSleep ? 1.f : 10.f);
		}
	}
	const FVector Me = GetActorLocation();
	const float GHome = JeebFoes::Ground(Home.X, Home.Y);
	switch (StateNow())
	{
	case EEagleState::Circle:
	{
		const float AngSpeed = CruiseSpeed / Radius;
		CircleA += AngSpeed * Dt * CircleDir;
		const FVector2D C = Home + FVector2D(FMath::Cos(CircleA), FMath::Sin(CircleA)) * Radius;
		const float Z = FMath::Max(GHome, JeebFoes::Ground(C.X, C.Y)) + Height + 400.f * FMath::Sin(CircleA * 1.7f);
		Fly(FVector(C.X, C.Y, Z), CruiseSpeed, Dt);
		PerchClock -= Dt;
		if (PerchClock <= 0.f)
		{
			PerchClock = Rand.FRandRange(60.f, 150.f);
			FVector P;
			if ((bLab || Rand.FRand() < 0.45f) && FindPerch(P)) { Goal = P; SetState(EEagleState::Land, TEXT("goes to perch")); }
		}
		break;
	}
	case EEagleState::Land:
		if (Fly(Goal, CruiseSpeed, Dt))
		{
			const bool bTree = Goal.Z > JeebFoes::Ground(Goal.X, Goal.Y) + 300.f;
			if (!bTree) SetActorRotation(FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan2(KillAt.Y - Goal.Y, KillAt.X - Goal.X)), 0.f));   // (facing the body)
			SetState(bTree ? EEagleState::Perch : EEagleState::Peck, bTree ? TEXT("lands on a tree") : TEXT("lands on the kill"));
		}
		else if (StateT > 40.f) SetState(EEagleState::Leave, TEXT("could not get there"));
		break;
	case EEagleState::Perch:
	case EEagleState::Peck:
		Vel = FVector::ZeroVector;
		if (StateT > StateFor) SetState(EEagleState::Leave, StateNow() == EEagleState::Peck ? TEXT("has eaten") : TEXT("has rested"));
		else if (StateT > 1.5f && NearestPlayer(GetWorld(), Me) < Startle) SetState(EEagleState::Leave, TEXT("a player came too near"));
		break;
	case EEagleState::Leave:
	{
		// up and away, then back to its circle
		const FVector Up = Me + FVector(FVector2D(Me - FVector(Home, 0.f)).GetSafeNormal() * -600.f, 0.f) + FVector(0.f, 0.f, 700.f);
		Fly(Up, CruiseSpeed, Dt);
		if (Me.Z > GHome + Height * 0.8f || StateT > 12.f) SetState(EEagleState::Circle, TEXT("back up"));
		break;
	}
	case EEagleState::Fall:
	{
		Vel.Z -= 980.f * Dt;
		Vel.X *= FMath::Exp(-0.8f * Dt); Vel.Y *= FMath::Exp(-0.8f * Dt);
		FVector N = Me + Vel * Dt;
		const float G = JeebFoes::Ground(N.X, N.Y);
		if (N.Z <= G) { N.Z = G; Vel = FVector::ZeroVector; SetState(EEagleState::Down, TEXT("hits the ground")); }
		SetActorLocation(N);
		break;
	}
	case EEagleState::Down:
		RespawnT -= Dt;
		if (StateT > 25.f && !IsHidden()) SetActorHiddenInGame(true);
		if (RespawnT <= 0.f)
		{
			Hp->Reset(); Hp->Cur = Hp->Max;
			SetActorHiddenInGame(false);
			SetActorLocation(FVector(Home.X, Home.Y, GHome + Height));
			SetState(EEagleState::Circle, TEXT("a new one has come"));
		}
		break;
	}
}

void AEagle::OnHurt(AActor* From)
{
	if (Hp->Cur <= 0) { Die(From); return; }
	if (!Flying() || StateNow() == EEagleState::Land) SetState(EEagleState::Leave, TEXT("is hurt"));
}

void AEagle::Die(AActor* From)
{
	if (StateNow() == EEagleState::Fall || StateNow() == EEagleState::Down) return;
	AKnightCharacter* K = HitterOf(From);
	RespawnT = RespawnSecs;
	const bool bAir = Flying();
	SetState(bAir ? EEagleState::Fall : EEagleState::Down, TEXT("is brought down"));
	// the feathers lie where it falls (worked out now: it drops almost straight)
	const FVector Me = GetActorLocation();
	const FVector2D P = FVector2D(Me) + FVector2D(Vel) * (bAir ? 0.6f : 0.f) + FVector2D(80.f, 0.f);
	const int32 N = Rand.RandRange(1, 3);
	const FInvSlot Feathers(JeebItems::MaterialId(EItemType::EagleFeather), N);
	if (ALootDrop* Drop = GetWorld()->SpawnActor<ALootDrop>(FVector(P.X, P.Y, JeebFoes::Ground(P.X, P.Y)), FRotator::ZeroRotator))
		Drop->Setup(ELootLook::Item, TArray<FInvSlot>{ Feathers }, 0);
	const bool bRanged = K && FVector::Dist(K->GetActorLocation(), Me) > 450.f;
	if (USkillComponent* Sk = K ? K->FindComponentByClass<USkillComponent>() : nullptr)
	{
		Sk->AddXP(ESkillType::Hunting, HuntXP);
		Sk->AddXP(bRanged ? ESkillType::Archery : ESkillType::Attack, ArcheryXP);
		Sk->Say(4.f, FColor(255, 210, 120), FString::Printf(TEXT("You brought down an eagle%s! (+%d %s XP)"), bAir ? TEXT(" in flight") : TEXT(""), ArcheryXP, bRanged ? TEXT("Archery") : TEXT("Attack")));
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBEAGLE %s brought down by %s %s, %.0f m up: drops %d Eagle Feathers; +%d Hunting XP"), *GetName(), K ? *K->GetName() : TEXT("nobody"),
		bAir ? TEXT("in flight") : TEXT("sitting"), (Me.Z - JeebFoes::Ground(Me.X, Me.Y)) / 100.f, N, K ? HuntXP : 0);
}

// Every machine: which body shows, the bank in turns and the pitch in climbs and dives, the pecking bob, the bird lying when down
void AEagle::TickLook(float Dt)
{
	const EEagleState S = StateNow();
	const bool bFly = Flying();
	FlyBody->SetVisibility(bFly);
	SitBody->SetVisibility(!bFly);
	const float MeshYaw = CVarEagleYaw.GetValueOnGameThread();
	const float Yaw = GetActorRotation().Yaw;
	const float YawRate = Dt > 0.f ? FMath::FindDeltaAngleDegrees(LastYaw, Yaw) / Dt : 0.f;
	LastYaw = Yaw;
	const float T = GetWorld()->GetTimeSeconds() + float(GetUniqueID() % 97u);
	if (bFly)
	{
		Bank = FMath::FInterpTo(Bank, FMath::Clamp(YawRate * 0.6f, -35.f, 35.f), Dt, 3.f);
		const float Hs = FVector2D(Vel).Size();
		float Pitch = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(Vel.Z, FMath::Max(Hs, 100.f))), -40.f, 25.f);
		if (S == EEagleState::Fall) Pitch = -60.f;
		const float Glide = FMath::Sin(T * 1.3f) * 3.f;   // (a slow rock on the air)
		FlyBody->SetRelativeRotation(FRotator(Pitch, MeshYaw, Bank + Glide));
	}
	else
	{
		float Pitch = 0.f, Roll = 0.f;
		if (S == EEagleState::Peck) Pitch = -35.f * FMath::Max(0.f, FMath::Sin(T * 7.f)) - 12.f;   // head down to the kill, again and again
		if (S == EEagleState::Perch) Pitch = 2.f * FMath::Sin(T * 0.7f);
		if (S == EEagleState::Down) Roll = 85.f;
		SitBody->SetRelativeRotation(FRotator(Pitch, MeshYaw + (S == EEagleState::Perch ? 25.f * FMath::Sin(T * 0.21f) : 0.f), Roll));
		SitBody->SetRelativeLocation(FVector(0.f, 0.f, S == EEagleState::Down ? 15.f : 0.f));
	}
	JBody->SetRelativeLocation(bFly ? FVector::ZeroVector : FVector(0.f, 0.f, 35.f));
}

#include "Goblins.h"
#include "KnightCharacter.h"
#include "SkillComponent.h"
#include "HealthComponent.h"
#include "NpcBody.h"
#include "Projectile.h"
#include "FoeData.h"
#include "SteeleCreek.h"
#include "JeebWater.h"
#include "JeebTuning.h"
#include "Messages.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogJeebGoblin, Log, All);

static TAutoConsoleVariable<int32> CVarGoblins(TEXT("jeeb.Goblins"), 1, TEXT("Steele Creek's goblin pickpockets (Goblins.h): 1 on, 0 left out. Server, read when the world is built."), ECVF_Default);

namespace
{
	const JeebTune::FValues& T() { return JeebTune::V(); }
	float TopSpeed() { return T().SprintSpeed * T().GoblinSpeedMul; }
	FString Who(const AKnightCharacter* K)
	{
		const APlayerState* PS = K ? K->GetPlayerState() : nullptr;
		return PS ? PS->GetPlayerName() : (K ? K->GetName() : FString(TEXT("?")));
	}
	AKnightCharacter* HitterOf(AActor* From)
	{
		if (!From) return nullptr;
		if (AKnightCharacter* K = Cast<AKnightCharacter>(From)) return K;
		if (AKnightCharacter* K = Cast<AKnightCharacter>(From->GetInstigator())) return K;
		return Cast<AKnightCharacter>(From->GetOwner());
	}
	bool Alive(const AKnightCharacter* K) { return K && K->GetController() && K->GetHealth() > 0.f && K->GetSkills(); }
	float SegDist2D(const FVector2D& A, const FVector2D& B, const FVector2D& P)
	{
		const FVector2D AB = B - A;
		const float L2 = AB.SizeSquared();
		const float t = L2 > 1.f ? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / L2, 0.f, 1.f) : 0.f;
		return FVector2D::Distance(A + AB * t, P);
	}
	constexpr float BodyScale = 1.f;
#if !UE_BUILD_SHIPPING
	void GoblinTestTick(UWorld* W, float Dt, AGoblinBand* Band);
#endif
	bool Lab() { return FCString::Strifind(FCommandLine::Get(), TEXT("GoblinLab=")) != nullptr; }
}

// ------------------------------------------------------------------ the band (server)

FVector2D AGoblinBand::Centre() { return FVector2D(JeebSteele::PosX, JeebSteele::PosY); }
bool AGoblinBand::InSafe(const FVector2D& P, float Pad) { return FVector2D::Distance(P, Centre()) < T().GoblinSafeRadius + Pad; }
bool AGoblinBand::InRange(const FVector2D& P)
{
	const float D = FVector2D::Distance(P, Centre());
	return D >= T().GoblinSafeRadius && D <= T().GoblinOuterRadius;
}
bool AGoblinBand::StealableSpot(const FVector2D& P)
{
	if (!InRange(P)) return false;
	for (const JeebSteele::FPlot& Pl : JeebSteele::Plots()) if (JeebSteele::WallSD(Pl, P.X, P.Y) < 50.f) return false;   // (inside a building: the bank, a shop)
	return true;
}

AGoblinBand::AGoblinBand()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
}

void AGoblinBand::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_Client || CVarGoblins.GetValueOnGameThread() == 0 || !JeebSteele::Enabled()) { SetActorTickEnabled(false); return; }
}

void AGoblinBand::Tick(float Dt)
{
	Super::Tick(Dt);
	if (!bBuilt)
	{
		BuildClock += Dt;
		if (BuildClock > 2.f) { bBuilt = true; Build(); }   // (after the world's trees and bushes are in)
		return;
	}
	TickShots();
#if !UE_BUILD_SHIPPING
	GoblinTestTick(GetWorld(), Dt, this);
#endif
}

void AGoblinBand::Build()
{
	UWorld* W = GetWorld();
	const FVector2D C = Centre();
	const float In = T().GoblinSafeRadius + 400.f, Out = T().GoblinOuterRadius - 300.f;
	TArray<FVector2D> Got;
	auto Consider = [&](const FVector& L)
	{
		const FVector2D P(L);
		const float D = FVector2D::Distance(P, C);
		if (D < In || D > Out) return;
		float S;
		if (JeebWater::SurfaceAt(P.X, P.Y, S) && S > JeebFoes::Ground(P.X, P.Y) - 30.f) return;
		for (const JeebSteele::FPlot& Pl : JeebSteele::Plots()) if (JeebSteele::WallSD(Pl, P.X, P.Y) < 300.f) return;
		Got.Add(P);
	};
	// The trees and bushes the world built here (whatever the layout): any instanced or plain mesh on a forest, bush or tree actor
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		const FString CN = It->GetClass()->GetName();
		if (!(CN.Contains(TEXT("Forest")) || CN.Contains(TEXT("Bush")) || CN.Contains(TEXT("Tree")) || CN.Contains(TEXT("Grove")) || CN.Contains(TEXT("Hedge")))) continue;
		if (!JeebSteele::InBox(It->GetActorLocation().X, It->GetActorLocation().Y, 60000.f) && !CN.Contains(TEXT("Forest")) && !CN.Contains(TEXT("Bush"))) continue;
		TInlineComponentArray<UStaticMeshComponent*> Comps(*It);
		for (UStaticMeshComponent* SM : Comps)
		{
			if (UInstancedStaticMeshComponent* I = Cast<UInstancedStaticMeshComponent>(SM))
			{
				const int32 N = I->GetInstanceCount();
				for (int32 i = 0; i < N; ++i) { FTransform Xf; if (I->GetInstanceTransform(i, Xf, true)) Consider(Xf.GetLocation()); }
			}
			else if (SM) Consider(SM->GetComponentLocation());
		}
	}
	// keep at most 600, spread out (one per 4 m cell)
	TSet<FIntPoint> Cells;
	for (const FVector2D& P : Got) { const FIntPoint Cl(FMath::FloorToInt(P.X / 400.f), FMath::FloorToInt(P.Y / 400.f)); if (!Cells.Contains(Cl)) { Cells.Add(Cl); Spots.Add(P); } }
	const int32 FromWorld = Spots.Num();
	FRandomStream R(9371);
	if (Spots.Num() < 30)   // (no trees found: points round the ring)
		for (int32 i = 0; i < 60; ++i) { const float A = R.FRandRange(0.f, 2.f * PI), D = R.FRandRange(In, Out); Spots.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * D); }
	while (Spots.Num() > 600) Spots.RemoveAtSwap(R.RandRange(0, Spots.Num() - 1));
	UE_LOG(LogJeebGoblin, Log, TEXT("JEEBGOBLIN band: %d hiding spots (%d trees and bushes from the world), safe circle %.0f m, outer %.0f m, top speed %.0f cm/s (knight sprint %.0f x %.2f)"),
		Spots.Num(), FromWorld, T().GoblinSafeRadius / 100.f, T().GoblinOuterRadius / 100.f, TopSpeed(), T().SprintSpeed, T().GoblinSpeedMul);

	for (int32 i = 0; i < FMath::Min(6, Spots.Num()); ++i) UE_LOG(LogJeebGoblin, Log, TEXT("JEEBGOBLIN spot %d at %.0f %.0f"), i, Spots[i].X, Spots[i].Y);
	const bool bLab = Lab();
	const int32 Count = bLab ? 2 : FMath::Clamp(FMath::RoundToInt(T().GoblinCount), 0, 20);
	for (int32 i = 0; i < Count; ++i)
	{
		FVector2D P = Spots[R.RandRange(0, Spots.Num() - 1)];
		AGoblin* G = W->SpawnActorDeferred<AGoblin>(AGoblin::StaticClass(), FTransform::Identity);
		if (!G) continue;
		G->Idx = i; G->Band = this; G->Rand.Initialize(4100 + i * 77);
#if !UE_BUILD_SHIPPING
		if (bLab)
		{
			FString V; FParse::Value(FCommandLine::Get(), TEXT("GoblinLab="), V, false);
			TArray<FString> Pp; V.ParseIntoArray(Pp, TEXT("_"));
			const FVector2D L(Pp.Num() > 0 ? FCString::Atof(*Pp[0]) : 0.f, Pp.Num() > 1 ? FCString::Atof(*Pp[1]) : 0.f);
			G->LabRole = i;
			if (i == 0)
			{
				// the nearest bush or tree to the lab spot; it crouches on the side away from the camera (south of it)
				FVector2D Best = L; float BD = 1e12f;
				for (const FVector2D& S : Spots) { const float D = FVector2D::DistSquared(S, L); if (D < BD) { BD = D; Best = S; } }
				P = Best + FVector2D(70.f, 0.f);
				UE_LOG(LogJeebGoblin, Log, TEXT("JEEBGOBLIN lab: hider behind the spot at %.0f %.0f (%.1f m from the lab spot)"), Best.X, Best.Y, FMath::Sqrt(BD) / 100.f);
			}
			else { G->LabA = L + FVector2D(0.f, -1500.f); G->LabB = L + FVector2D(0.f, 1500.f); P = G->LabA; }
		}
#endif
		G->FinishSpawning(FTransform(FRotator::ZeroRotator, FVector(P.X, P.Y, JeebFoes::Ground(P.X, P.Y))));
		Goblins.Add(G);
		UE_LOG(LogJeebGoblin, Log, TEXT("JEEBGOBLIN goblin %d at %.0f %.0f"), i, P.X, P.Y);
	}
}

FVector2D AGoblinBand::HideSpot(const FVector2D& Near, const FVector2D& Away, float MinFromPlayers, FRandomStream& R) const
{
	if (Spots.Num() == 0) return Near;
	TArray<FVector2D> Players;
	for (TActorIterator<AKnightCharacter> It(GetWorld()); It; ++It) if (It->GetController()) Players.Add(FVector2D(It->GetActorLocation()));
	FVector2D Best = Spots[0]; float BestS = -1e12f;
	for (int32 k = 0; k < 40; ++k)
	{
		const FVector2D S = Spots[R.RandRange(0, Spots.Num() - 1)];
		float MinP = 1e12f;
		for (const FVector2D& P : Players) MinP = FMath::Min(MinP, float(FVector2D::Distance(S, P)));
		const float DNear = FVector2D::Distance(S, Near);
		float Score = -DNear * 0.5f + FMath::Min(float(FVector2D::Distance(S, Away)), 3000.f);
		if (MinP < MinFromPlayers) Score -= 1e6f;
		if (DNear > 6000.f) Score -= 4000.f;
		if (Score > BestS) { BestS = Score; Best = S; }
	}
	// behind it, seen from the player it hides from
	const FVector2D Dir = (Best - Away).GetSafeNormal();
	return Best + Dir * 80.f;
}

void AGoblinBand::TickShots()
{
	// arrows and bolts that pass a goblin close: it runs (server; a handful of projectiles fly at once)
	TMap<TWeakObjectPtr<AActor>, FVector> Now;
	for (TActorIterator<AProjectile> It(GetWorld()); It; ++It)
	{
		const FVector L = It->GetActorLocation();
		Now.Add(*It, L);
		const FVector* Was = ProjLast.Find(*It);
		if (!Was || FVector::DistSquared(*Was, L) < 4.f) continue;   // (new, or lying stuck)
		for (const TWeakObjectPtr<AGoblin>& GW : Goblins)
		{
			AGoblin* G = GW.Get();
			if (!G || G->IsDown()) continue;
			const FVector GL = G->GetActorLocation();
			if (FMath::Abs(L.Z - GL.Z) > 500.f) continue;
			if (SegDist2D(FVector2D(*Was), FVector2D(L), FVector2D(GL)) < T().GoblinShotRange) G->Startle(*Was, TEXT("shot at"));
		}
	}
	ProjLast = MoveTemp(Now);
}

// ------------------------------------------------------------------ the goblin

AGoblin::AGoblin()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(30.f);
	SetNetCullDistanceSquared(FMath::Square(16000.f));
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Hp = CreateDefaultSubobject<UHealthComponent>(TEXT("Hp"));
	Hp->Max = Hp->Cur = 1;
	Hp->BarHeight = 140.f; Hp->HitHeight = 60.f; Hp->HitRadius = 45.f;
}

void AGoblin::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGoblin, RepMove);
	DOREPLIFETIME(AGoblin, RepState);
}

void AGoblin::BeginPlay()
{
	Super::BeginPlay();
	Hp->GatherHurt = [this](TArray<JeebHit::FCapsule>& Out) { GatherHurt(Out); };
	if (HasAuthority())
	{
		Hp->Max = Hp->Cur = FMath::Max(1, FMath::RoundToInt(T().GoblinHealth));
		Hp->OnDamaged = [this](AActor* From) { if (!Hp->IsAlive()) Die(From); else Startle(From ? From->GetActorLocation() : GetActorLocation(), TEXT("hit")); };
		Yaw = Rand.FRandRange(-180.f, 180.f);
		SetState(EGoblinState::Lurk, TEXT("spawned"));
		StateT = -Rand.FRandRange(0.f, 6.f);
		SendMove();
	}
	BodyYaw = Yaw;
	BuildBody();
}

void AGoblin::BuildBody()
{
	if (!JeebPeople::Enabled()) return;
	const JeebPeople::FModel* M = JeebPeople::Model(TEXT("Goblin"));
	if (!M || !M->Mesh) { UE_LOG(LogJeebGoblin, Warning, TEXT("JEEBGOBLIN no Goblin body (People.csv)")); return; }
	USkeletalMeshComponent* S = NewObject<USkeletalMeshComponent>(this);
	S->SetupAttachment(Root);
	S->SetRelativeRotation(FRotator(0.f, M->Yaw, 0.f));
	S->SetSkeletalMesh(M->Mesh);
	S->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	S->SetGenerateOverlapEvents(false);
	S->SetCanEverAffectNavigation(false);
	S->bEnableUpdateRateOptimizations = true;
	S->LDMaxDrawDistance = 9000.f; S->CachedMaxDrawDistance = 9000.f;
	S->SetAnimInstanceClass(UNpcAnimInstance::StaticClass());
	S->RegisterComponent();
	if (UMaterialInstanceDynamic* D = S->CreateDynamicMaterialInstance(0))
	{
		D->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.16f, 0.11f, 0.06f));   // (a dark brown rag: wild, not Silver Point's)
		D->SetScalarParameterValue(TEXT("GarmentLum"), M->GarmentLum);
	}
	if (UNpcAnimInstance* A = Cast<UNpcAnimInstance>(S->GetAnimInstance())) A->SetClips(M->Clips[0], M->Clips[1], M->Clips[2], Idx * 0.37f);
	Body = S;
}

void AGoblin::GatherHurt(TArray<JeebHit::FCapsule>& Out) const
{
	if (IsDown()) return;
	const FVector L = GetActorLocation();
	const float Cr = FMath::Lerp(1.f, 0.75f, Crouch);
	JeebHit::FCapsule C; C.A = L + FVector(0, 0, 25.f); C.B = L + FVector(0, 0, 85.f * Cr); C.R = 26.f; C.Zone = JeebHit::Z_Torso; Out.Add(C);
	JeebHit::FCapsule H; H.A = H.B = L + FVector(0, 0, 112.f * Cr); H.R = 17.f; H.Zone = JeebHit::Z_Head; Out.Add(H);
}

void AGoblin::SetState(EGoblinState S, const TCHAR* Why)
{
	if (RepState != uint8(S)) UE_LOG(LogJeebGoblin, Log, TEXT("JEEBGOBLIN %d %d -> %d (%s)"), Idx, RepState, uint8(S), Why);
	RepState = uint8(S);
	StateT = 0.f;
	LastWhy = Why;
}

void AGoblin::SendMove()
{
	RepMove.Loc = GetActorLocation();
	RepMove.Yaw = uint16(FMath::RoundToInt(FRotator::ClampAxis(Yaw) / 360.f * 65535.f));
	RepMove.Speed = uint16(FMath::Clamp(Speed, 0.f, 65000.f));
}

void AGoblin::OnRep_Move() { MoveAt = GetWorld()->GetTimeSeconds(); }

void AGoblin::Startle(const FVector& From, const TCHAR* Why)
{
	if (!HasAuthority() || IsDown()) return;
	ThreatAt = FVector2D(From);
	if (StateNow() != EGoblinState::Flee) SetState(EGoblinState::Flee, Why);
	else { StateT = 0.f; LastWhy = Why; }
	Target = nullptr;
}

bool AGoblin::SeenBy(const AKnightCharacter* K) const
{
	const FVector2D Me(GetActorLocation()), KP(K->GetActorLocation());
	const float D = FVector2D::Distance(Me, KP);
	if (D > T().GoblinSpottedRange) return false;
	const FVector2D Fwd = FVector2D(FRotator(0.f, K->GetBaseAimRotation().Yaw, 0.f).Vector()).GetSafeNormal();
	const float Cos = FVector2D::DotProduct(Fwd, (Me - KP).GetSafeNormal());
	return Cos > FMath::Cos(FMath::DegreesToRadians(T().GoblinViewHalfAngle));
}

AKnightCharacter* AGoblin::PickTarget() const
{
	AKnightCharacter* Best = nullptr; float BD = FMath::Square(T().GoblinSenseRange);
	for (TActorIterator<AKnightCharacter> It(GetWorld()); It; ++It)
	{
		AKnightCharacter* K = *It;
		if (!Alive(K) || K->GetSkills()->GetIngots() <= 0 || K->IsInvisible()) continue;
		if (!AGoblinBand::StealableSpot(FVector2D(K->GetActorLocation()))) continue;
		const float D = FVector::DistSquared2D(K->GetActorLocation(), GetActorLocation());
		if (D < BD) { BD = D; Best = K; }
	}
	return Best;
}

bool AGoblin::Step(const FVector2D& To, float Want, float Dt)
{
	Speed = FMath::FInterpConstantTo(Speed, Want, Dt, Want > Speed ? 4000.f : 6000.f);
	const FVector2D At(GetActorLocation());
	const FVector2D Dv = To - At;
	const float Len = Dv.Size();
	if (Len < 30.f) { Speed = FMath::Min(Speed, 100.f); return true; }
	const float StepLen = FMath::Min(Len, Speed * Dt);
	const FVector2D Dir = Dv / Len;
	FCollisionObjectQueryParams Obj; Obj.AddObjectTypesToQuery(ECC_WorldStatic); Obj.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Q(SCENE_QUERY_STAT(GoblinStep), false, this);
	const FCollisionShape Shape = FCollisionShape::MakeSphere(25.f);
	const float Z = JeebFoes::Ground(At.X, At.Y) + 60.f;
	const float Side = (Idx & 1) ? 1.f : -1.f;
	static const float Turns[] = { 0.f, 30.f, -30.f, 60.f, -60.f, 90.f, -90.f };
	FVector2D Go = FVector2D::ZeroVector;
	for (float A : Turns)
	{
		const FVector2D D = Dir.GetRotated(A * Side);
		const FVector2D N = At + D * FMath::Max(StepLen, 40.f);
		if (AGoblinBand::InSafe(N) && !AGoblinBand::InSafe(At)) continue;   // never into the safe circle
		if (FVector2D::Distance(N, AGoblinBand::Centre()) > T().GoblinOuterRadius + 1500.f) continue;
		float S; if (JeebWater::SurfaceAt(N.X, N.Y, S) && S > JeebFoes::Ground(N.X, N.Y) + 40.f) continue;   // no deep water
		FHitResult Hit;
		if (GetWorld()->SweepSingleByObjectType(Hit, FVector(At, Z), FVector(N, Z), FQuat::Identity, Obj, Shape, Q) && !Hit.bStartPenetrating) continue;
		Go = D; break;
	}
	if (Go.IsZero())
	{
		BlockedT += Dt;
		if (BlockedT < 0.6f) return false;
		Go = AGoblinBand::InSafe(At + Dir * StepLen) && !AGoblinBand::InSafe(At) ? -Dir : Dir;   // (walled in: steps through, never into the safe circle)
	}
	else BlockedT = 0.f;
	const FVector2D N = At + Go * StepLen;
	SetActorLocation(FVector(N.X, N.Y, JeebFoes::Ground(N.X, N.Y)));
	Yaw = FMath::FixedTurn(Yaw, FMath::RadiansToDegrees(FMath::Atan2(Go.Y, Go.X)), 900.f * Dt);
	MaxSpeedSeen = FMath::Max(MaxSpeedSeen, StepLen / FMath::Max(Dt, 1e-4f));
	return false;
}

void AGoblin::PickHide(const FVector2D& AwayFrom)
{
	if (AGoblinBand* B = Band.Get()) Goal = B->HideSpot(FVector2D(GetActorLocation()), AwayFrom, 1200.f, Rand);
	else Goal = FVector2D(GetActorLocation());
}

void AGoblin::Steal(AKnightCharacter* K)
{
	USkillComponent* Sk = K->GetSkills();
	const int32 N = FMath::Min(Rand.RandRange(FMath::RoundToInt(T().GoblinStealMin), FMath::RoundToInt(T().GoblinStealMax)), Sk->GetIngots());
	if (N <= 0 || !Sk->SpendIngots(N)) { Startle(K->GetActorLocation(), TEXT("empty pockets")); return; }
	Stolen += N;
	UE_LOG(LogJeebGoblin, Log, TEXT("JEEBGOBLIN %d steal %d from %s (now %d left; carries %d)"), Idx, N, *Who(K), Sk->GetIngots(), Stolen);
	TWeakObjectPtr<AKnightCharacter> WK = K;
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WK, N]()
	{
		if (AKnightCharacter* V = WK.Get()) if (USkillComponent* S = V->GetSkills())
			S->Say(5.f, FColor(255, 110, 60), FString::Printf(TEXT("A goblin picked your pocket! -%d ingots"), N));
	}), FMath::Max(0.05f, T().GoblinMsgDelay), false);
	Startle(K->GetActorLocation(), TEXT("stole"));
}

void AGoblin::Die(AActor* From)
{
	AKnightCharacter* K = HitterOf(From);
	const int32 Pay = FMath::FloorToInt(Stolen * T().GoblinKillShare);
	UE_LOG(LogJeebGoblin, Log, TEXT("JEEBGOBLIN %d killed by %s: carried %d, pays %d"), Idx, K ? *Who(K) : TEXT("nobody"), Stolen, Pay);
	if (K && K->GetSkills())
	{
		if (Pay > 0) K->GetSkills()->AddIngots(Pay);
		K->GetSkills()->Say(5.f, FColor(255, 215, 90), Pay > 0 ? FString::Printf(TEXT("You caught a goblin! +%d ingots"), Pay) : FString(TEXT("You caught a goblin! Its pockets were empty.")));
	}
	Stolen = 0;
	Speed = 0.f;
	Target = nullptr;
	SetState(EGoblinState::Dead, TEXT("killed"));
	SendMove();
}

void AGoblin::Rise()
{
	// back at a hiding spot no player is near
	FVector2D P(GetActorLocation());
	if (AGoblinBand* B = Band.Get()) P = B->HideSpot(P, P, 2500.f, Rand);
	SetActorLocation(FVector(P.X, P.Y, JeebFoes::Ground(P.X, P.Y)));
	Hp->Reset();
	SetState(EGoblinState::Lurk, TEXT("respawned"));
	SendMove();
}

void AGoblin::TickMind(float Dt)
{
	StateT += Dt;
	const FVector2D Me(GetActorLocation());
	if (StateNow() == EGoblinState::Dead)
	{
		if (StateT > T().GoblinRespawnSeconds) Rise();
		return;
	}
	// swung at: any knight swinging close by
	SenseT -= Dt;
	if (SenseT <= 0.f)
	{
		SenseT = 0.1f;
		for (TActorIterator<AKnightCharacter> It(GetWorld()); It; ++It)
			if (It->IsSwingingNow() && FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) < T().GoblinSwingRange) { Startle(It->GetActorLocation(), TEXT("swung at")); break; }
	}
	if (bTestHardHold || (bTestHold && StateNow() != EGoblinState::Flee)) { Speed = 0.f; return; }

	AKnightCharacter* K = Target.Get();
	switch (StateNow())
	{
	case EGoblinState::Lurk:   // crouched behind its tree, waiting for a pocket
	{
		Speed = FMath::FInterpConstantTo(Speed, 0.f, Dt, 3000.f);
		if (StateT < 1.f) break;
		if (FMath::Fmod(StateT, 0.5f) > Dt) break;
		AKnightCharacter* P = PickTarget();
		if (P) { Target = P; SetState(EGoblinState::Creep, TEXT("a pocket")); }
		else if (StateT > 25.f) { PickHide(Me); SetState(EGoblinState::Hide, TEXT("a new tree")); }
		break;
	}
	case EGoblinState::Hide:   // scurrying to a hiding spot
		if (Step(Goal, T().GoblinScurrySpeed, Dt) || StateT > 15.f) SetState(EGoblinState::Lurk, TEXT("hidden"));
		break;
	case EGoblinState::Creep:   // round behind the player, out of their view
	{
		if (!Alive(K) || !AGoblinBand::StealableSpot(FVector2D(K->GetActorLocation())) || K->GetSkills()->GetIngots() <= 0 || StateT > 40.f)
		{ PickHide(Me); SetState(EGoblinState::Hide, TEXT("lost the pocket")); break; }
		if (SeenBy(K) && FVector2D::Distance(Me, FVector2D(K->GetActorLocation())) < T().GoblinSpottedRange * 0.6f)
		{ PickHide(FVector2D(K->GetActorLocation())); SetState(EGoblinState::Hide, TEXT("spotted")); break; }
		const FVector2D KP(K->GetActorLocation());
		const FVector2D Fwd = FVector2D(FRotator(0.f, K->GetBaseAimRotation().Yaw, 0.f).Vector()).GetSafeNormal();
		const FVector2D Behind = KP - Fwd * (T().GoblinDashRange * 0.7f);
		const float D = FVector2D::Distance(Me, KP);
		const bool bBehind = FVector2D::DotProduct(Fwd, (Me - KP).GetSafeNormal()) < -0.3f;
		if (D < T().GoblinDashRange && bBehind) { SetState(EGoblinState::Dash, TEXT("dash")); break; }
		// far off: scurry; close: creep. Go round the side, never through their view
		FVector2D To = Behind;
		if (!bBehind && D < 1500.f) { const FVector2D Side(-Fwd.Y, Fwd.X); To = KP + Side * (FVector2D::DotProduct(Side, Me - KP) >= 0.f ? 900.f : -900.f) - Fwd * 300.f; }
		Step(To, D > 2000.f ? T().GoblinScurrySpeed : T().GoblinCreepSpeed, Dt);
		break;
	}
	case EGoblinState::Dash:
	{
		if (!Alive(K) || StateT > 2.5f) { Startle(FVector(Me, 0.f) - FVector(GetActorForwardVector()), TEXT("missed")); break; }
		const FVector2D KP(K->GetActorLocation());
		Step(KP, TopSpeed(), Dt);
		if (FVector2D::Distance(FVector2D(GetActorLocation()), KP) < T().GoblinStealReach && FMath::Abs(K->GetActorLocation().Z - GetActorLocation().Z) < 250.f)
		{
			if (AGoblinBand::StealableSpot(KP)) Steal(K);
			else Startle(K->GetActorLocation(), TEXT("safe spot"));
		}
		break;
	}
	case EGoblinState::Flee:
	{
		FVector2D Away = (Me - ThreatAt).GetSafeNormal();
		if (Away.IsNearlyZero()) Away = FVector2D(1.f, 0.f);
		// along the ring if fleeing would cross the safe circle
		const FVector2D Out = (Me - AGoblinBand::Centre()).GetSafeNormal();
		if (FVector2D::DotProduct(Away, Out) < 0.f && AGoblinBand::InSafe(Me, 1500.f)) Away = (Away - Out * FVector2D::DotProduct(Away, Out) + Out * 0.5f).GetSafeNormal();
		Step(Me + Away * 600.f, TopSpeed(), Dt);
		if (StateT > T().GoblinFleeSeconds) { PickHide(ThreatAt); SetState(EGoblinState::Hide, TEXT("got away")); }
		break;
	}
	default: break;
	}
}

void AGoblin::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = FMath::Min(DeltaSeconds, 0.1f);
	if (HasAuthority())
	{
#if !UE_BUILD_SHIPPING
		if (LabRole == 0) { Speed = 0.f; RepState = uint8(EGoblinState::Lurk); Yaw = 0.f; }
		else if (LabRole == 1)
		{
			if (Step(LabB, TopSpeed(), Dt)) Swap(LabA, LabB);
			RepState = uint8(EGoblinState::Dash);
		}
		else
#endif
		TickMind(Dt);
		SetActorRotation(FRotator(0.f, Yaw, 0.f));
		SendMove();
	}
	else
	{
#if !UE_BUILD_SHIPPING
		{ TActorIterator<AGoblin> It(GetWorld()); if (It && *It == this) GoblinTestTick(GetWorld(), DeltaSeconds, nullptr); }
#endif
		// glide toward where the server said, plus its speed since then, on this machine's ground
		const float Since = FMath::Min(float(GetWorld()->GetTimeSeconds() - MoveAt), 0.25f);
		const float RYaw = RepMove.Yaw / 65535.f * 360.f;
		const FVector2D Want = FVector2D(RepMove.Loc) + FVector2D(FRotator(0.f, RYaw, 0.f).Vector()) * (RepMove.Speed * Since);
		FVector2D Cur(GetActorLocation());
		Cur = FVector2D::Distance(Cur, Want) > 600.f ? Want : FMath::Vector2DInterpTo(Cur, Want, Dt, 14.f);
		Speed = RepMove.Speed;
		Yaw = FMath::FixedTurn(GetActorRotation().Yaw, RYaw, 900.f * Dt);
		SetActorLocationAndRotation(FVector(Cur.X, Cur.Y, JeebFoes::Ground(Cur.X, Cur.Y)), FRotator(0.f, Yaw, 0.f));
	}
	TickLook(Dt);
}

void AGoblin::TickLook(float Dt)
{
	const EGoblinState S = StateNow();
	const bool bDead = S == EGoblinState::Dead;
	if (Body)
	{
		Body->SetVisibility(!bDead);
		const bool bLow = S == EGoblinState::Lurk || S == EGoblinState::Creep;
		Crouch = FMath::FInterpTo(Crouch, bLow ? 1.f : 0.f, Dt, 8.f);
		Lean = FMath::FInterpTo(Lean, FMath::Clamp(Speed / 1100.f, 0.f, 1.f) * 22.f + Crouch * 14.f, Dt, 8.f);
		const JeebPeople::FModel* M = JeebPeople::Model(TEXT("Goblin"));
		const float WalkSpeed = M ? M->WalkSpeed : 225.f;
		Body->SetRelativeScale3D(FVector(BodyScale, BodyScale, BodyScale * FMath::Lerp(1.f, 0.78f, Crouch)));
		Body->SetWorldRotation(FRotator(-Lean, Yaw, 0.f).Quaternion() * FRotator(0.f, M ? M->Yaw : -90.f, 0.f).Quaternion());
		if (UNpcAnimInstance* A = Cast<UNpcAnimInstance>(Body->GetAnimInstance()))
		{
			const bool bMove = Speed > 20.f;
			// (crouched, the stride is shorter: the clip plays faster for the same speed)
			const float Rate = Speed / (WalkSpeed * FMath::Lerp(1.f, 0.7f, Crouch));
			A->Drive(bMove ? 1 : 0, FMath::Clamp(Rate, 0.3f, 5.5f), 0.f);
		}
	}
}

// ------------------------------------------------------------------ the world build hook

namespace JeebGoblins
{
	void SpawnBand(UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client || CVarGoblins.GetValueOnGameThread() == 0 || !JeebSteele::Enabled()) return;
		World->SpawnActor<AGoblinBand>(FVector::ZeroVector, FRotator::ZeroRotator);
	}
}

// ------------------------------------------------------------------ the online test (Tools/goblin_net.sh)
#if !UE_BUILD_SHIPPING
struct FGoblinTest
{
	TWeakObjectPtr<AKnightCharacter> A, B;
	float Clock = 0.f, BAt = 0.f, StageAt = 0.f;
	int32 Stage = 0, AIng0 = 0, BIng0 = 0, Carry = 0;
	TWeakObjectPtr<AGoblin> G;
	bool bMsgLogged = false;

	static void Put(AKnightCharacter* K, const FVector2D& P, float FaceYaw)
	{
		K->SetActorLocation(FVector(P.X, P.Y, JeebFoes::Ground(P.X, P.Y) + 110.f), false, nullptr, ETeleportType::TeleportPhysics);
		K->SetActorRotation(FRotator(0.f, FaceYaw, 0.f));
		if (APlayerController* PC = Cast<APlayerController>(K->GetController())) { PC->SetControlRotation(FRotator(0.f, FaceYaw, 0.f)); PC->ClientSetRotation(FRotator(0.f, FaceYaw, 0.f)); }
	}
	static void PlaceGoblin(AGoblin* Gb, const FVector2D& P, EGoblinState S)
	{
		Gb->SetActorLocation(FVector(P.X, P.Y, JeebFoes::Ground(P.X, P.Y)));
		Gb->SetState(S, TEXT("test placed"));
		Gb->Speed = 0.f;
	}

	void Client(UWorld* W, float Dt)
	{
		static const bool bA = FParse::Param(FCommandLine::Get(), TEXT("GoblinNetA"));
		static const bool bB = FParse::Param(FCommandLine::Get(), TEXT("GoblinNetB"));
		if (!(bA || bB)) return;
		Clock += Dt;
		for (const JeebMsg::FMsg& M : JeebMsg::All())
			if (M.Text.Contains(TEXT("goblin")) && !bMsgLogged) { bMsgLogged = true; UE_LOG(LogJeebGoblin, Log, TEXT("JEEBGOBLINMSG %s saw: %s"), bA ? TEXT("A") : TEXT("B"), *M.Text); }
		static float LogAt = 0.f;
		if (Clock > LogAt + 5.f)
		{
			LogAt = Clock;
			int32 N = 0;
			for (TActorIterator<AGoblin> It(W); It; ++It) ++N;
			UE_LOG(LogJeebGoblin, Log, TEXT("JEEBGOBLINSEE %s: %d goblins replicated here"), bA ? TEXT("A") : TEXT("B"), N);
		}
	}

	void Server(UWorld* W, AGoblinBand* Band, float Dt)
	{
		Clock += Dt;
		for (TActorIterator<AKnightCharacter> It(W); It; ++It)
		{
			if (!It->GetController() || !It->GetSkills()) continue;
			const bool bIsA = Who(*It).Contains(TEXT("goblinreva"));
			if (!A.IsValid() && bIsA) { A = *It; UE_LOG(LogJeebGoblin, Log, TEXT("JEEBGOBLINTEST player A is %s"), *Who(*It)); }
			else if (!B.IsValid() && !bIsA) { B = *It; BAt = Clock; UE_LOG(LogJeebGoblin, Log, TEXT("JEEBGOBLINTEST player B is %s"), *Who(*It)); }
		}
		AKnightCharacter* KA = A.Get(); AKnightCharacter* KB = B.Get();
		if (!KA || !KB || Band->Goblins.Num() == 0) return;
		const float Since = Clock - StageAt;
		auto Next = [&](const TCHAR* What) { ++Stage; StageAt = Clock; UE_LOG(LogJeebGoblin, Log, TEXT("JEEBGOBLINTEST stage %d: %s"), Stage, What); };
		const FVector2D C = AGoblinBand::Centre();
		const float Ring = (T().GoblinSafeRadius + T().GoblinOuterRadius) * 0.5f;
		const FVector2D PA = C + FVector2D(0.f, Ring), PB = C + FVector2D(0.f, -Ring);   // A east, B west of the town
		auto Gob = [&](int32 i) { return Band->Goblins.IsValidIndex(i) ? Band->Goblins[i].Get() : nullptr; };
		switch (Stage)
		{
		case 0:
			if (Clock < BAt + 12.f) return;
			Put(KA, PA, 0.f); Put(KB, PB, 0.f);
			KA->GetSkills()->AddIngots(100);
			AIng0 = KA->GetSkills()->GetIngots();
			for (int32 i = 1; i < Band->Goblins.Num(); ++i) if (AGoblin* O = Gob(i)) { O->bTestHold = true; PlaceGoblin(O, C + FVector2D(-Ring, 0.f) + FVector2D(0.f, i * 300.f), EGoblinState::Lurk); }   // (the rest wait far off, north)
			if (AGoblin* G0 = Gob(0)) { G0->MaxSpeedSeen = 0.f; PlaceGoblin(G0, PA - FVector2D(1800.f, 0.f), EGoblinState::Lurk); }   // 18 m behind A (A faces north, +X)
			UE_LOG(LogJeebGoblin, Log, TEXT("JEEBGOBLINTEST A at %.0f %.0f with %d ingots facing north; goblin 0 18 m behind"), PA.X, PA.Y, AIng0);
			Next(TEXT("A waits for a pickpocket"));
			return;
		case 1:
		{
			AGoblin* G0 = Gob(0);
			const int32 Now = KA->GetSkills()->GetIngots();
			if (G0 && Now < AIng0)
			{
				const int32 Took = AIng0 - Now;
				Carry = G0->StolenSinceDeath();
				UE_LOG(LogJeebGoblin, Log, TEXT("JEEBGOBLINTEST %s steal after %.1f s: %d ingots (5-20), goblin carries %d, state %d (%s)"), (Took >= 5 && Took <= 20) ? TEXT("PASS") : TEXT("FAIL"), Since, Took, Carry, int32(G0->StateNow()), *G0->LastWhy);
				Next(TEXT("stolen"));
			}
			else if (Since > 60.f) { UE_LOG(LogJeebGoblin, Log, TEXT("JEEBGOBLINTEST FAIL no steal in 60 s (goblin 0 state %d)"), G0 ? int32(G0->StateNow()) : -1); Next(TEXT("no steal")); }
			return;
		}
		case 2:   // it flees; measure its top speed
			if (Since < 3.f) return;
			if (AGoblin* G0 = Gob(0))
			{
				const float Top = G0->MaxSpeedSeen, Want = T().SprintSpeed * 1.5f;
				UE_LOG(LogJeebGoblin, Log, TEXT("JEEBGOBLINTEST %s flee after steal: state %d; top speed %.0f cm/s vs knight sprint %.0f x1.5 = %.0f"), (G0->StateNow() == EGoblinState::Flee && FMath::Abs(Top - Want) < 60.f) ? TEXT("PASS") : TEXT("FAIL"), int32(G0->StateNow()), Top, T().SprintSpeed, Want);
			}
			Next(TEXT("swing test"));
			if (AGoblin* G1 = Gob(1)) { PlaceGoblin(G1, PB + FVector2D(-300.f, 0.f), EGoblinState::Lurk); G1->bTestHold = true; }
			return;
		case 3:
			if (Since < 1.f) return;
			KB->SwingForTest();
			UE_LOG(LogJeebGoblin, Log, TEXT("JEEBGOBLINTEST B swings 3 m from goblin 1 (swinging now %d)"), KB->IsSwingingNow() ? 1 : 0);
			Next(TEXT("swung"));
			return;
		case 4:
			if (Since < 0.6f) return;
			if (AGoblin* G1 = Gob(1)) UE_LOG(LogJeebGoblin, Log, TEXT("JEEBGOBLINTEST %s flee on swing: state %d (%s)"), G1->StateNow() == EGoblinState::Flee ? TEXT("PASS") : TEXT("FAIL"), int32(G1->StateNow()), *G1->LastWhy);
			if (AGoblin* G2 = Gob(2)) { PlaceGoblin(G2, PB + FVector2D(0.f, 800.f), EGoblinState::Lurk); G2->bTestHold = true; }
			Next(TEXT("shot test"));
			return;
		case 5:
			if (Since < 1.f) return;
			if (AGoblin* G2 = Gob(2))
			{
				// a miss: the arrow passes 1.5 m beside it
				const FVector From = KB->GetActorLocation() + FVector(0, 0, 60.f);
				const FVector To = G2->GetActorLocation() + FVector(150.f, 0.f, 60.f);
				FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				const FVector Dir = (To - From).GetSafeNormal();
				if (AProjectile* P = W->SpawnActor<AProjectile>(AProjectile::StaticClass(), From + Dir * 80.f, Dir.Rotation(), SP)) P->Launch(KB, Dir, 3500.f, 0.f, 1, false, ESkillType::Archery, false, 0, 0, 0.f, 0.f);
			}
			Next(TEXT("shot past"));
			return;
		case 6:
			if (Since < 1.f) return;
			if (AGoblin* G2 = Gob(2)) UE_LOG(LogJeebGoblin, Log, TEXT("JEEBGOBLINTEST %s flee on shot: state %d (%s)"), G2->StateNow() == EGoblinState::Flee ? TEXT("PASS") : TEXT("FAIL"), int32(G2->StateNow()), *G2->LastWhy);
			// the kill: the thief stands 8 m before B, an arrow of 1 damage (the weakest there is)
			if (AGoblin* G0 = Gob(0))
			{
				Carry = G0->StolenSinceDeath();
				PlaceGoblin(G0, PB + FVector2D(800.f, 0.f), EGoblinState::Lurk);
				G0->bTestHardHold = true;
				BIng0 = KB->GetSkills()->GetIngots();
			}
			Next(TEXT("kill test"));
			return;
		case 7:
			if (Since < 1.f) return;
			if (AGoblin* G0 = Gob(0))
			{
				const FVector From = KB->GetActorLocation() + FVector(0, 0, 60.f);
				const FVector To = G0->GetActorLocation() + FVector(0, 0, 60.f);
				FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				const FVector Dir = (To - From).GetSafeNormal();
				if (AProjectile* P = W->SpawnActor<AProjectile>(AProjectile::StaticClass(), From + Dir * 80.f, Dir.Rotation(), SP)) P->Launch(KB, Dir, 3500.f, 0.f, 1, false, ESkillType::Archery, false, 0, 0, 0.f, 0.f);
			}
			Next(TEXT("arrow away"));
			return;
		case 8:
			if (Since < 1.5f) return;
			if (AGoblin* G0 = Gob(0))
			{
				const int32 Got = KB->GetSkills()->GetIngots() - BIng0;
				UE_LOG(LogJeebGoblin, Log, TEXT("JEEBGOBLINTEST %s one-shot: dead %d; B got %d of %d carried (half %d); carry now %d"), (G0->IsDown() && Got == Carry / 2) ? TEXT("PASS") : TEXT("FAIL"), G0->IsDown() ? 1 : 0, Got, Carry, Carry / 2, G0->StolenSinceDeath());
				G0->bTestHardHold = false;
			}
			{
				int32 N = 0, Up = 0;
				for (const TWeakObjectPtr<AGoblin>& Gw : Band->Goblins) if (AGoblin* Gb = Gw.Get()) { ++N; if (!Gb->IsDown()) ++Up; Gb->bTestHold = false; }
				UE_LOG(LogJeebGoblin, Log, TEXT("JEEBGOBLINTEST %s goblins: %d in the band, %d up now (one waits to respawn in %.0f s)"), N == FMath::RoundToInt(T().GoblinCount) ? TEXT("PASS") : TEXT("FAIL"), N, Up, T().GoblinRespawnSeconds);
			}
			Next(TEXT("done"));
			return;
		default: return;
		}
	}
};
static FGoblinTest GTest;
#endif

// hooks for the test: the band drives the server side, the first goblin on each player's machine the clients'
#if !UE_BUILD_SHIPPING
namespace
{
	void GoblinTestTick(UWorld* W, float Dt, AGoblinBand* Band)
	{
		static const bool bServer = FParse::Param(FCommandLine::Get(), TEXT("GoblinNetTest"));
		if (Band) { if (bServer) GTest.Server(W, Band, Dt); }
		else GTest.Client(W, Dt);
	}
}
#endif

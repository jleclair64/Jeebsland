#include "Pond.h"
#include "Terrain.h"
#include "Inventory.h"
#include "WaterLook.h"
#include "WorldMap.h"
#include "JeebRecords.h"
#include "JeebTuning.h"
#include "KnightCharacter.h"
#include "ItemModels.h"
#include "Messages.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Camera/PlayerCameraManager.h"
#include "ProceduralMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"

// ======================================================================================= fishing spot
AFishingSpot::AFishingSpot()
{
	PrimaryActorTick.bCanEverTick = true;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Skill = ESkillType::Fishing;
	GatherSeconds = 5.f;
	RespawnSeconds = 1.f;
	ItemType = EItemType::Perch;
	XPReward = 8;
	MaxCharges = 1000000;
	StartText = TEXT("You cast your line...");
	SuccessText = TEXT("You caught a fish!");
	EmptyText = TEXT("The fish are not biting.");

	ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	auto Make = [&](const TCHAR* Name, UStaticMesh* Mesh, const FVector& Scale)
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(Root);
		C->SetStaticMesh(Mesh);
		C->SetRelativeScale3D(Scale);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetVisibility(false);
		return C;
	};
	Bobber = Make(TEXT("Bobber"), Sphere.Object, FVector(0.14f, 0.14f, 0.14f));
	Line = Make(TEXT("Line"), Cyl.Object, FVector(0.008f, 0.008f, 1.f));
	Ripple = Make(TEXT("Ripple"), Cyl.Object, FVector(1.f, 1.f, 0.01f));
	Line->SetUsingAbsoluteLocation(true);
	Line->SetUsingAbsoluteRotation(true);
	Line->SetUsingAbsoluteScale(true);
}

void AFishingSpot::BeginPlay()
{
	Super::BeginPlay();
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	auto Tint = [&](UStaticMeshComponent* C, const FLinearColor& Col)
	{
		if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), Col);
	};
	Tint(Bobber, FLinearColor(0.9f, 0.1f, 0.08f));
	Tint(Line, FLinearColor(0.9f, 0.9f, 0.85f));
	// Two thin rings always spread out from the spot on the water, so you can see where to fish (2026-11-13: were a solid pale
	// disc, which read as a white blob on the new water). Each sinks a little as it grows, so the water dims it as it fades.
	if (!IsNetMode(NM_DedicatedServer))
	{
		const int32 Segs = 48;
		TArray<FVector> V; TArray<int32> T; TArray<FVector> N; TArray<FLinearColor> Col;
		for (int32 k = 0; k < Segs; ++k)
		{
			const float A = 2.f * PI * k / Segs;
			V.Add(FVector(88.f * FMath::Cos(A), 88.f * FMath::Sin(A), 0.f));
			V.Add(FVector(100.f * FMath::Cos(A), 100.f * FMath::Sin(A), 0.f));
		}
		for (int32 k = 0; k < Segs; ++k)
		{
			const int32 I0 = 2 * k, O0 = 2 * k + 1, I1 = 2 * ((k + 1) % Segs), O1 = I1 + 1;
			T.Append({ I0, O1, O0, I0, I1, O1 });
		}
		N.Init(FVector::UpVector, V.Num()); Col.Init(FLinearColor::White, V.Num());
		for (int32 r = 0; r < 2; ++r)
		{
			UProceduralMeshComponent* Ring = NewObject<UProceduralMeshComponent>(this);
			Ring->SetupAttachment(GetRootComponent());
			Ring->SetUsingAbsoluteScale(true);
			Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Ring->SetCastShadow(false);
			Ring->RegisterComponent();
			Ring->CreateMeshSection_LinearColor(0, V, T, N, TArray<FVector2D>(), Col, TArray<FProcMeshTangent>(), false);
			if (UMaterialInstanceDynamic* M = Ring->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.20f, 0.25f, 0.28f));
			Rings.Add(Ring);
		}
		// (2026-11-26) three fish under the water near the spot: the bass model in one dark, murky colour (seen through the water),
		// one mesh each, circling on their own paths (Tick). Players' machines only; hidden when the camera is far off.
		TArray<JeebItemModel::FPart> Parts;
		JeebItemModel::Build(JeebItems::MaterialId(EItemType::Bass), Parts);
		JeebItemModel::Turn(Parts, FVector(-90.f, 0.f, 0.f));   // (back up, as it swims)
		KM::FMD One;
		for (const JeebItemModel::FPart& P : Parts)
		{
			const int32 Off = One.V.Num();
			One.V.Append(P.Mesh.V);
			for (int32 Ix : P.Mesh.I) One.I.Add(Ix + Off);
		}
		FVector Mid(0.f);
		for (const FVector& Vx : One.V) Mid += Vx;
		if (One.V.Num() > 0) Mid /= float(One.V.Num());
		for (FVector& Vx : One.V) Vx -= Mid;
		TArray<FVector> FN;
		KM::ComputeNormals(One, FN);
		TArray<int32> FT;
		for (int32 t = 0; t + 2 < One.I.Num(); t += 3) { FT.Add(One.I[t]); FT.Add(One.I[t + 1]); FT.Add(One.I[t + 2]); FT.Add(One.I[t]); FT.Add(One.I[t + 2]); FT.Add(One.I[t + 1]); }
		FRandomStream R(int32(GetActorLocation().X * 7.f + GetActorLocation().Y * 13.f));
		for (int32 f = 0; f < 3; ++f)
		{
			UProceduralMeshComponent* Fish = NewObject<UProceduralMeshComponent>(this);
			Fish->SetupAttachment(GetRootComponent());
			Fish->SetUsingAbsoluteLocation(true);
			Fish->SetUsingAbsoluteRotation(true);
			Fish->SetUsingAbsoluteScale(true);
			Fish->SetWorldScale3D(FVector(R.FRandRange(1.4f, 2.2f)));
			Fish->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Fish->SetCastShadow(false);
			Fish->RegisterComponent();
			Fish->CreateMeshSection_LinearColor(0, One.V, FT, FN, TArray<FVector2D>(), TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
			if (UMaterialInstanceDynamic* M = Fish->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.035f, 0.045f, 0.03f));
			Swimmers.Add(Fish);
			SwimPlan.Add(FVector4f(R.FRandRange(120.f, 300.f), R.FRandRange(0.25f, 0.55f) * (R.FRand() < 0.5f ? -1.f : 1.f), R.FRandRange(0.f, 2.f * PI), R.FRandRange(18.f, 35.f)));
		}
	}
	else SetActorTickEnabled(false);   // (the server draws nothing: the bite and the reel run on timers)
}

// The local player, while he fishes here (players' machines: only for what this machine draws)
AActor* AFishingSpot::LocalFisher() const
{
	if (IsBeingGathered()) return GetCurrentGatherer();
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	AKnightCharacter* K = PC ? Cast<AKnightCharacter>(PC->GetPawn()) : nullptr;
	return (K && K->WorkingOn(this)) ? K : nullptr;
}

FVector AFishingSpot::RodTip(const AActor* G) const
{
	return G->GetActorLocation() + G->GetActorForwardVector() * 150.f + G->GetActorRightVector() * 25.f + FVector(0.f, 0.f, 75.f);
}

void AFishingSpot::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	const FVector Spot = GetActorLocation();
	const double Now = GetWorld()->GetTimeSeconds();

	// Far from the camera: nothing to draw (the river has many spots)
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const bool bNear = PC && PC->PlayerCameraManager && FVector::DistSquared(PC->PlayerCameraManager->GetCameraLocation(), Spot) < FMath::Square(6000.f);
	for (UProceduralMeshComponent* R : Rings) if (R && R->IsVisible() != bNear) R->SetVisibility(bNear);
	for (UProceduralMeshComponent* S : Swimmers) if (S && S->IsVisible() != bNear) S->SetVisibility(bNear);
	if (!bNear && CatchAt < Now - 3.0) { if (Bobber->IsVisible()) { Bobber->SetVisibility(false); Line->SetVisibility(false); } return; }

	// Ripples spread out from the spot all the time, so you can see where to fish
	for (int32 r = 0; r < Rings.Num(); ++r)
	{
		const float R = FMath::Frac(Clock / 2.4f + 0.5f * r);
		const float S = 0.2f + 0.85f * R;   // radius 20 cm to 1.05 m
		Rings[r]->SetWorldScale3D(FVector(S, S, 1.f));
		Rings[r]->SetWorldLocation(Spot + FVector(0.f, 0.f, 0.6f - 9.f * R * R));
	}
	// Fish under the water: dark shapes circling slowly near the spot, each with its own path and a little tail sway
	for (int32 i = 0; i < Swimmers.Num(); ++i)
	{
		const FVector4f& P = SwimPlan[i];
		const float A = P.Z + P.Y * Clock;
		const float Wob = 0.25f * FMath::Sin(Clock * 1.3f + P.Z * 3.f);
		const FVector At = Spot + FVector(P.X * (1.f + Wob) * FMath::Cos(A), P.X * FMath::Sin(A), -P.W);
		const float Head = FMath::RadiansToDegrees(A) + (P.Y > 0.f ? 90.f : -90.f) + 12.f * FMath::Sin(Clock * 6.f + P.Z * 5.f);
		Swimmers[i]->SetWorldLocationAndRotation(At, FRotator(0.f, Head, 0.f));
	}

	// The float and line, and a caught fish on the line
	AActor* G = LocalFisher();
	const bool bCatch = Now - CatchAt < 2.2 && CatchBy.IsValid();
	if (bCatch) G = CatchBy.Get();
	const bool bOn = G != nullptr;
	if (Bobber->IsVisible() != (bOn && !bCatch)) Bobber->SetVisibility(bOn && !bCatch);
	if (Line->IsVisible() != bOn) Line->SetVisibility(bOn);
	if (CatchRoot && CatchRoot->IsVisible() != bCatch) CatchRoot->SetVisibility(bCatch, true);
	if (!bOn) return;

	const FVector Tip = RodTip(G);
	FVector End;
	if (bCatch && CatchRoot)
	{
		// up out of the water in 0.4 s, flopping hard and then weaker for 1.3 s, then hanging limp under the tip until it goes into the pack
		const float T = float(Now - CatchAt);
		const float Up = FMath::SmoothStep(0.f, 0.4f, T);
		End = FMath::Lerp(Spot, Tip - FVector(0.f, 0.f, 55.f), Up);
		const float Flop = T < 1.7f ? FMath::Clamp(1.7f - T, 0.f, 1.f) : 0.f;
		const float Roll = 38.f * Flop * FMath::Sin(T * 17.f);
		const float Bend = 22.f * Flop * FMath::Sin(T * 11.f + 1.f);
		CatchRoot->SetWorldLocationAndRotation(End, FRotator(-90.f + Bend, G->GetActorRotation().Yaw + 90.f, Roll));   // (head up, hung by the mouth)
	}
	else
	{
		// bobbing; a bite pulls it under, shaking
		const float SinceBite = float(Now - LocalBiteAt);
		const float Dip = SinceBite < 0.9f ? -14.f * FMath::Sin(FMath::Min(SinceBite / 0.12f, 1.f) * HALF_PI) + 2.5f * FMath::Sin(SinceBite * 45.f) : 0.f;
		End = Spot + FVector(0.f, 0.f, 5.f + 3.f * FMath::Sin(Clock * 3.f) + Dip);
		Bobber->SetWorldLocation(End);
	}
	// The line runs from the tip of the pole to the float (or the fish)
	const FVector D = End - Tip;
	const float Len = FMath::Max(D.Size(), 1.f);
	Line->SetWorldLocationAndRotation((Tip + End) * 0.5f, FRotationMatrix::MakeFromZ(D / Len).Rotator());
	Line->SetWorldScale3D(FVector(0.008f, 0.008f, Len / 100.f));
}

// ---- the server's side of a cast ----
bool AFishingSpot::BeginOwnGather(AKnightCharacter* Knight)
{
	bBiting = false;
	ScheduleBite();
	return true;
}

void AFishingSpot::ScheduleBite()
{
	const int32 Id = ++CastId;
	USkillComponent* Sk = GetCurrentGatherer() ? GetCurrentGatherer()->FindComponentByClass<USkillComponent>() : nullptr;
	const int32 L = Sk ? Sk->GetLevel(ESkillType::Fishing) : 1;
	const JeebTune::FValues& V = JeebTune::V();
	const float MaxWait = FMath::Lerp(V.FishBiteMaxAt1, V.FishBiteMaxAt99, FMath::Clamp((L - 1) / 98.f, 0.f, 1.f));
	const float PoleMul = FMath::Clamp(EffectiveSeconds / FMath::Max(GatherSeconds, 0.01f), 0.3f, 2.f);   // (a better pole bites sooner)
	const float Wait = FMath::FRandRange(V.FishBiteMin, FMath::Max(V.FishBiteMin, MaxWait)) * PoleMul;
	UE_LOG(LogTemp, Log, TEXT("JEEBFISH cast %d: Fishing %d, a bite in %.1f s (longest at this level %.1f, pole x%.2f)"), Id, L, Wait, MaxWait, PoleMul);
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this, Id]()
	{
		if (Id != CastId || !IsBeingGathered()) return;
		AKnightCharacter* K = Cast<AKnightCharacter>(GetCurrentGatherer());
		if (!K) return;
		bBiting = true;
		BiteAt = GetWorld()->GetTimeSeconds();
		// online the click needs a round trip: the server allows the ping on top, up to FishPingAllowance
		float Ping = 0.f;
		if (!K->IsLocallyControlled()) if (const APlayerState* PS = K->GetPlayerState()) Ping = FMath::Min(PS->ExactPing / 1000.f, JeebTune::V().FishPingAllowance);
		const float Window = JeebTune::V().FishReelWindow + Ping;
		K->FishBite(this);
		UE_LOG(LogTemp, Log, TEXT("JEEBFISH cast %d: a bite, %.2f s to reel (ping allowance %.2f)"), Id, Window, Ping);
		FTimerHandle M;
		GetWorldTimerManager().SetTimer(M, FTimerDelegate::CreateWeakLambda(this, [this, Id]()
		{
			if (Id != CastId || !bBiting || !IsBeingGathered()) return;
			bBiting = false;
			bBeingGathered = false;
			UE_LOG(LogTemp, Log, TEXT("JEEBFISH cast %d: it got away"), Id);
			Tell(GetCurrentGatherer(), 2.5f, FColor::Orange, TEXT("It got away! Click the moment the float goes under."));
			if (AKnightCharacter* Kn = Cast<AKnightCharacter>(GetCurrentGatherer())) Kn->EndGathering();
		}), Window, false);
	}), Wait, false);
}

void AFishingSpot::Reel(AKnightCharacter* Knight)
{
	if (!IsBeingGathered() || GetCurrentGatherer() != Knight) return;
	if (!bBiting)
	{
		// too soon: the splash scares it off, and the wait starts again
		Tell(Knight, 2.f, FColor::Orange, TEXT("Too soon! The fish are spooked. Wait for the float to go under."));
		UE_LOG(LogTemp, Log, TEXT("JEEBFISH cast %d: reeled too soon"), CastId);
		ScheduleBite();
		return;
	}
	bBiting = false;
	++CastId;   // (the miss timer is void now)
	UE_LOG(LogTemp, Log, TEXT("JEEBFISH reeled %.2f s after the bite"), GetWorld()->GetTimeSeconds() - BiteAt);
	FinishGathering();
}

static int32 SpeciesOf(EItemType T)
{
	return T == EItemType::Trout ? JeebRecords::S_Trout : T == EItemType::Salmon ? JeebRecords::S_Salmon : T == EItemType::Bass ? JeebRecords::S_Bass : -1;
}

void AFishingSpot::RollYield(USkillComponent* Skills, EItemType& OutItem, int32& OutXP)
{
	const int32 L = Skills ? Skills->GetLevel(ESkillType::Fishing) : 1;
	const float Roll = FMath::FRand();
	if (bRiver)
	{
		// the river: bass at any level, trout from Fishing 5, salmon from 15 (Materials.csv GatherLevel)
		const float SalmonChance = (L >= JeebItems::FishingLevelFor(EItemType::Salmon)) ? 0.25f : 0.f;
		const float TroutChance = (L >= JeebItems::FishingLevelFor(EItemType::Trout)) ? 0.35f : 0.f;
		OutItem = Roll < SalmonChance ? EItemType::Salmon : Roll < SalmonChance + TroutChance ? EItemType::Trout : EItemType::Bass;
	}
	else
	{
		const float CarpChance = (L >= JeebItems::FishingLevelFor(EItemType::GoldenCarp)) ? 0.12f : 0.f;
		const float TroutChance = (L >= JeebItems::FishingLevelFor(EItemType::Trout)) ? 0.35f : 0.f;
		OutItem = Roll < CarpChance ? EItemType::GoldenCarp : Roll < CarpChance + TroutChance ? EItemType::Trout : EItemType::Perch;
	}
	OutXP = JeebItems::FishXP(OutItem);
	const int32 S = SpeciesOf(OutItem);
	LastSize = S >= 0 ? JeebRecords::RollSize(S) : 0.f;
	const FString Name = FString(JeebItems::MaterialName(OutItem)).ToLower();
	SuccessText = S >= 0 ? FString::Printf(TEXT("You caught a %s %s! (+%d XP)"), *JeebRecords::SizeText(S, LastSize), *Name, OutXP)
		: FString::Printf(TEXT("You caught a %s! (+%d XP)"), *Name, OutXP);
}

void AFishingSpot::OnYield(AActor* Gatherer, EItemType Item)
{
	const int32 S = SpeciesOf(Item);
	if (S >= 0)
	{
		FString Key, Name, Note;
		JeebRecords::WhoIs(Gatherer, Key, Name);
		JeebRecords::Record(GetWorld(), Key, Name, S, LastSize, Note);
		if (!Note.IsEmpty()) Tell(Gatherer, 5.f, FColor(255, 215, 90), Note);
	}
	if (AKnightCharacter* K = Cast<AKnightCharacter>(Gatherer)) K->FishCaught(this, Item, S >= 0 ? LastSize : 10.f);
}

// ---- what players' machines draw ----
void AFishingSpot::ShowBite()
{
	LocalBiteAt = GetWorld()->GetTimeSeconds();
	JeebMsg::Post(1.2f, FColor(255, 235, 120), TEXT("A bite! Click now!"));
}

void AFishingSpot::ShowCatch(AActor* Knight, EItemType Item, float Size)
{
	if (IsNetMode(NM_DedicatedServer) || !Knight) return;
	CatchAt = GetWorld()->GetTimeSeconds();
	CatchBy = Knight;
	for (UProceduralMeshComponent* C : CatchParts) if (C) C->DestroyComponent();
	CatchParts.Reset();
	if (!CatchRoot)
	{
		CatchRoot = NewObject<USceneComponent>(this);
		CatchRoot->SetupAttachment(GetRootComponent());
		CatchRoot->SetUsingAbsoluteLocation(true);
		CatchRoot->SetUsingAbsoluteRotation(true);
		CatchRoot->RegisterComponent();
	}
	// the pack's own fish model (ItemModels.h), grown to the fish's real length (inches), hung by its mouth
	TArray<JeebItemModel::FPart> Parts;
	JeebItemModel::Build(JeebItems::MaterialId(Item), Parts);
	JeebItemModel::Turn(Parts, FVector(-90.f, 0.f, 0.f));   // (back upright: the model lies on its side on the ground)
	FBox Bx(ForceInit);
	for (const JeebItemModel::FPart& P : Parts) for (const FVector& V : P.Mesh.V) Bx += V;
	if (!Bx.IsValid) return;
	const float Grow = FMath::Clamp(Size * 2.54f / FMath::Max(float(Bx.GetSize().X), 1.f), 0.5f, 6.f);
	const FVector Mouth(Bx.Max.X, 0.f, Bx.GetCenter().Z);
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	for (const JeebItemModel::FPart& P : Parts)
	{
		UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(this);
		C->SetupAttachment(CatchRoot);
		C->SetRelativeScale3D(FVector(Grow));
		C->SetRelativeLocation(-Mouth * Grow);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(true);
		C->RegisterComponent();
		TArray<FVector> N;
		KM::ComputeNormals(P.Mesh, N);
		TArray<int32> Tris;
		for (int32 t = 0; t + 2 < P.Mesh.I.Num(); t += 3) { Tris.Add(P.Mesh.I[t]); Tris.Add(P.Mesh.I[t + 1]); Tris.Add(P.Mesh.I[t + 2]); Tris.Add(P.Mesh.I[t]); Tris.Add(P.Mesh.I[t + 2]); Tris.Add(P.Mesh.I[t + 1]); }
		C->CreateMeshSection_LinearColor(0, P.Mesh.V, Tris, N, TArray<FVector2D>(), TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
		if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), P.Color);
		CatchParts.Add(C);
	}
}

// ---- the river's spots: every 120 m along each river, near alternate banks, in the water ----
void AFishingSpot::SpawnRiverSpots(UWorld* World)
{
	if (!World) return;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	int32 N = 0;
	const TArray<JeebWorld::FRiver>& Rivers = JeebWorld::Rivers();
	for (int32 r = 0; r < Rivers.Num(); ++r)
	{
		const JeebWorld::FRiver& Rv = Rivers[r];
		int32 Side = 1;
		for (int32 i = 8; i + 8 < Rv.Pts.Num() && N < 80; i += 24)
		{
			const FVector2D T = (Rv.Pts[i + 1] - Rv.Pts[i - 1]).GetSafeNormal();
			const FVector2D Nrm(-T.Y, T.X);
			const float Half = Rv.Halfs.IsValidIndex(i) ? Rv.Halfs[i] : Rv.HalfWidth;
			Side = -Side;
			if (Half < 250.f) continue;
			const FVector2D At = Rv.Pts[i] + Nrm * float(Side) * FMath::Max(0.f, Half - 230.f);
			if (JeebWorld::WaterEdge(At.X, At.Y) > -120.f) continue;   // (not in open water here)
			if (FVector2D::Distance(At, JeebWorld::BridgeAt()) < 2500.f) continue;
			if (AFishingSpot* S = World->SpawnActor<AFishingSpot>(FVector(At.X, At.Y, JeebWorld::RiverLevel + 2.f), FRotator::ZeroRotator, P))
			{
				S->bRiver = true;
				++N;
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBFISH %d river fishing spots"), N);

	// -JeebFishTest (server or single player): check the size curves and the records file once, in the log
	if (FParse::Param(FCommandLine::Get(), TEXT("JeebFishTest")) && World->GetNetMode() != NM_Client)
	{
		for (int32 s = 0; s < JeebRecords::S_Num; ++s)
		{
			const int32 Count = 2000000;
			double Sum = 0.0, Sq = 0.0; int32 Board = 0;
			for (int32 i = 0; i < Count; ++i) { const float X = JeebRecords::RollSize(s); Sum += X; Sq += double(X) * X; if (X >= JeebRecords::BoardAt(s)) ++Board; }
			const double M = Sum / Count;
			UE_LOG(LogTemp, Log, TEXT("JEEBFISHTEST %s: mean %.2f, sd %.3f, at or over %.0f: %.4f%%"), JeebRecords::SpeciesName(s), M, FMath::Sqrt(Sq / Count - M * M), JeebRecords::BoardAt(s), 100.0 * Board / Count);
		}
		FString Note;
		JeebRecords::Record(World, TEXT("fishtest"), TEXT("Fish Test"), JeebRecords::S_Trout, 29.3f, Note);
		JeebRecords::Record(World, TEXT("fishtest"), TEXT("Fish Test"), JeebRecords::S_Bass, 21.4f, Note);
		JeebRecords::Record(World, TEXT("fishtest"), TEXT("Fish Test"), JeebRecords::S_Antlers, 14.f, Note);
		TArray<FString> Lines;
		JeebRecords::Report(World, TEXT("fishtest")).ParseIntoArrayLines(Lines);
		for (const FString& L : Lines) UE_LOG(LogTemp, Log, TEXT("JEEBFISHTEST %s"), *L);
	}
}

// ======================================================================================= the pond
AFishingPond::AFishingPond()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AFishingPond::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(FVector::ZeroVector);

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	auto MakeISM = [&](UStaticMesh* Mesh, const FLinearColor& Color, bool bShadow)
	{
		UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this);
		C->SetStaticMesh(Mesh);
		C->SetupAttachment(RootComponent);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(bShadow);
		C->bAffectDynamicIndirectLighting = false;
		C->bAffectDistanceFieldLighting = false;
		C->RegisterComponent();
		if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), Color);
		return C;
	};
	UInstancedStaticMeshComponent* Plank = MakeISM(Cube, FLinearColor(0.36f, 0.22f, 0.10f), true);
	UInstancedStaticMeshComponent* Post = MakeISM(Cyl, FLinearColor(0.20f, 0.11f, 0.05f), true);
	UInstancedStaticMeshComponent* Stem = MakeISM(Cyl, FLinearColor(0.14f, 0.22f, 0.05f), false);
	UInstancedStaticMeshComponent* Cap = MakeISM(Cyl, FLinearColor(0.24f, 0.12f, 0.05f), false);
	UInstancedStaticMeshComponent* Rock = MakeISM(Sphere, FLinearColor(0.30f, 0.30f, 0.33f), true);
	UInstancedStaticMeshComponent* Pad = MakeISM(Cyl, FLinearColor(0.10f, 0.30f, 0.08f), false);
	UInstancedStaticMeshComponent* Flower = MakeISM(Sphere, FLinearColor(0.9f, 0.4f, 0.6f), false);
	UInstancedStaticMeshComponent* Lamp = MakeISM(Cube, FLinearColor(5.0f, 2.0f, 0.4f), false);

	const FVector2D C = JeebTerrain::PondCenter();
	const float Rim = JeebTerrain::PondRim();
	const float WZ = JeebTerrain::PondWaterZ();
	auto At = [&](float Ang, float D) { return FVector2D(C.X + D * FMath::Cos(Ang), C.Y + D * FMath::Sin(Ang)); };

	// ---- The water (2026-11-13): a flat disc of M_Water (WaterLook.h), drawn on players' machines only, no collision. Its surface
	// is where the old 4 cm cylinder's top was, PondWaterZ + 2 (swimming reads the level from JeebWater.h). Still water: white.
	if (!IsNetMode(NM_DedicatedServer))
	{
		const int32 Segs = 64;
		const float Zs = WZ + 2.f, Rw = JeebTerrain::PondWaterR;
		TArray<FVector> V; TArray<int32> T; TArray<FVector> N; TArray<FLinearColor> Col;
		V.Add(FVector(C.X, C.Y, Zs));
		for (int32 k = 0; k < Segs; ++k) { const float A = 2.f * PI * k / Segs; V.Add(FVector(C.X + Rw * FMath::Cos(A), C.Y + Rw * FMath::Sin(A), Zs)); }
		for (int32 k = 0; k < Segs; ++k) { const int32 P1 = 1 + k, P2 = 1 + (k + 1) % Segs; T.Append({ 0, P2, P1, 0, P1, P2 }); }   // (both windings)
		N.Init(FVector::UpVector, V.Num()); Col.Init(FLinearColor::White, V.Num());
		UProceduralMeshComponent* Water = NewObject<UProceduralMeshComponent>(this);
		Water->SetupAttachment(RootComponent);
		Water->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Water->SetCastShadow(false);
		Water->RegisterComponent();
		Water->CreateMeshSection_LinearColor(0, V, T, N, TArray<FVector2D>(), Col, TArray<FProcMeshTangent>(), false);
		JeebWaterLook::Apply(Water, 0, FLinearColor(0.06f, 0.24f, 0.36f));
		// The Field map still holds the UE template's grid floor (Floor_0, SM_Template_Map_Floor, MI_ProcGrid) at Z -0.5, hidden
		// under the land everywhere except in this pond's bowl, where it drew as a grey checker over the water. Hide it on screen
		// (its collision is left alone: it is not ours to remove here).
		for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
		{
			const UStaticMeshComponent* SM = It->GetStaticMeshComponent();
			if (SM && SM->GetStaticMesh() && SM->GetStaticMesh()->GetName().Contains(TEXT("Template_Map_Floor"))) It->SetActorHiddenInGame(true);
		}
	}
	// That template floor's collision goes too, on EVERY machine, the server included (2026-11-14, swimming): everywhere else it lies under
	// the land and touches nothing, but in this bowl it was an invisible floor at the waterline that held the knight on top of the water
	// (a trace probe found him standing on Floor_0 at Z 0), so the pond could never be swum in.
	for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
	{
		const UStaticMeshComponent* SM = It->GetStaticMeshComponent();
		if (SM && SM->GetStaticMesh() && SM->GetStaticMesh()->GetName().Contains(TEXT("Template_Map_Floor"))) It->SetActorEnableCollision(false);
	}

	// ---- The pier: from the north bank out over the water ----
	{
		const float D0 = 770.f, D1 = 320.f;
		const float Mid = (D0 + D1) * 0.5f, Len = D0 - D1;
		const float DeckZ = Rim + 2.f;   // the walking surface
		Plank->AddInstance(FTransform(FQuat::Identity, FVector(C.X, C.Y + Mid, DeckZ - 5.f), FVector(1.5f, Len / 100.f, 0.1f)));
		for (float Y = D1 + 20.f; Y < D0; Y += 36.f)   // darker boards laid across, for the look of planks
			Plank->AddInstance(FTransform(FQuat::Identity, FVector(C.X, C.Y + Y, DeckZ - 0.5f), FVector(1.56f, 0.30f, 0.02f)));
		for (int32 sd = -1; sd <= 1; sd += 2)
		{
			for (float Y = D1 + 10.f; Y <= D0; Y += 110.f)
			{
				const float Px = C.X + sd * 68.f, Py = C.Y + Y;
				const float G = JeebTerrain::HeightAt(Px, Py);
				const float PL = DeckZ - G;
				Post->AddInstance(FTransform(FQuat::Identity, FVector(Px, Py, G + PL * 0.5f), FVector(0.14f, 0.14f, PL / 100.f)));
			}
			// a taller post with a lantern at the end
			Post->AddInstance(FTransform(FQuat::Identity, FVector(C.X + sd * 68.f, C.Y + D1 + 10.f, DeckZ + 55.f), FVector(0.12f, 0.12f, 1.1f)));
			Lamp->AddInstance(FTransform(FQuat::Identity, FVector(C.X + sd * 68.f, C.Y + D1 + 10.f, DeckZ + 118.f), FVector(0.14f, 0.14f, 0.2f)));
		}
		UBoxComponent* B = NewObject<UBoxComponent>(this);
		B->SetupAttachment(RootComponent);
		B->SetBoxExtent(FVector(75.f, Len * 0.5f, 6.f));
		B->SetRelativeLocation(FVector(C.X, C.Y + Mid, DeckZ - 6.f));
		B->SetCollisionProfileName(TEXT("BlockAll"));
		B->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		B->RegisterComponent();
	}

	// ---- Reeds (cattails) around the edge, leaving the pier clear ----
	FRandomStream R(5151);
	for (int32 i = 0; i < 46; ++i)
	{
		const float Ang = R.FRandRange(0.f, 2.f * PI);
		if (FMath::Abs(FMath::FindDeltaAngleRadians(Ang, 0.5f * PI)) < 0.28f) continue;
		const FVector2D P = At(Ang, R.FRandRange(560.f, 700.f));
		const float G = JeebTerrain::HeightAt(P.X, P.Y);
		for (int32 k = 0; k < 3; ++k)
		{
			const FVector2D Q = P + FVector2D(R.FRandRange(-30.f, 30.f), R.FRandRange(-30.f, 30.f));
			const float Gq = JeebTerrain::HeightAt(Q.X, Q.Y);
			const float H = R.FRandRange(80.f, 150.f);
			Stem->AddInstance(FTransform(FRotator(R.FRandRange(-6.f, 6.f), 0.f, R.FRandRange(-6.f, 6.f)).Quaternion(), FVector(Q.X, Q.Y, Gq + H * 0.5f), FVector(0.04f, 0.04f, H / 100.f)));
			Cap->AddInstance(FTransform(FQuat::Identity, FVector(Q.X, Q.Y, Gq + H - 8.f), FVector(0.07f, 0.07f, 0.3f)));
		}
		(void)G;
	}

	// ---- Rocks along the shore ----
	for (int32 i = 0; i < 22; ++i)
	{
		const float Ang = R.FRandRange(0.f, 2.f * PI);
		if (FMath::Abs(FMath::FindDeltaAngleRadians(Ang, 0.5f * PI)) < 0.25f) continue;
		const FVector2D P = At(Ang, R.FRandRange(620.f, 800.f));
		const float G = JeebTerrain::HeightAt(P.X, P.Y);
		const float S = R.FRandRange(0.4f, 1.1f);
		Rock->AddInstance(FTransform(FRotator(0.f, R.FRandRange(0.f, 360.f), 0.f).Quaternion(), FVector(P.X, P.Y, G + 6.f), FVector(S * 1.2f, S, S * 0.55f)));
	}

	// ---- Lily pads, some with a pink flower ----
	for (int32 i = 0; i < 20; ++i)
	{
		const FVector2D P = At(R.FRandRange(0.f, 2.f * PI), FMath::Sqrt(R.FRand()) * 430.f);
		const float S = R.FRandRange(0.4f, 0.8f);
		Pad->AddInstance(FTransform(FQuat::Identity, FVector(P.X, P.Y, WZ + 1.5f), FVector(S, S, 0.015f)));
		if (i % 3 == 0) Flower->AddInstance(FTransform(FQuat::Identity, FVector(P.X, P.Y, WZ + 7.f), FVector(0.14f, 0.14f, 0.1f)));
	}

	// ---- Places to fish: a ring of spots in the water, and one off the end of the pier ----
	UWorld* World = GetWorld();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 k = 0; k < 8; ++k)
	{
		const FVector2D P = At(FMath::DegreesToRadians(22.5f + 45.f * k), 450.f);
		World->SpawnActor<AFishingSpot>(FVector(P.X, P.Y, WZ + 2.f), FRotator::ZeroRotator, Params);
	}
	World->SpawnActor<AFishingSpot>(FVector(C.X, C.Y + 180.f, WZ + 2.f), FRotator::ZeroRotator, Params);
}

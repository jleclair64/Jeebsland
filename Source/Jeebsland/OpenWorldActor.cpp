#include "OpenWorldActor.h"
#include "OpenWorld.h"
#include "Terrain.h"
#include "TownLabSwaps.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UObject/ConstructorHelpers.h"

float AOpenWorld::SnowNow = 0.f;

namespace
{
	TAutoConsoleVariable<float> CVarSnowForce(TEXT("jeeb.Snow.Force"), -1.f, TEXT("The snowfall on this machine: -1 the shared weather (clear or snowing for a few minutes at a time), 0 always clear, 1 always snowing (anything between: that share of it). Looks only."));
	TAutoConsoleVariable<float> CVarSnowAmount(TEXT("jeeb.Snow.Amount"), 1.f, TEXT("How many flakes fall when it snows (1 as built: about 2200 round the camera on the peaks; 0 none, 2 at most)"));
	TAutoConsoleVariable<float> CVarSnowLow(TEXT("jeeb.Snow.Low"), 0.45f, TEXT("The share of the snowfall that reaches the low country (under about 15 m; all of it from about 70 m up)"));
	TAutoConsoleVariable<float> CVarSnowSize(TEXT("jeeb.Snow.Size"), 1.f, TEXT("Flake size (1 as built: 1.3 to 2.8 cm)"));

	constexpr int32 NumFlakes = 4400;          // (jeeb.Snow.Amount 2 shows them all; 1 half)
	constexpr float BoxR = 1500.f;             // the flakes' box: 30 m square round a spot 5 m ahead of the camera,
	constexpr float BoxBelow = 500.f, BoxAbove = 1100.f;   // from 5 m under it to 11 m over it
	const FVector Wind(55.f, 24.f, 0.f);       // cm/s: a light steady drift

	// under a town roof (flakes are left out there, so it never snows indoors)
	bool UnderRoof(float X, float Y)
	{
		const FVector2D P(X, Y);
		if (JeebOakHill::Enabled() && JeebOakHill::InBox(X, Y) && JeebOakHill::OnBuilding(X, Y)) return true;
		if (JeebSteele::InBox(X, Y) && JeebSteele::OnBuilding(X, Y)) return true;
		if (FMath::Abs(X) < 9000.f && FMath::Abs(Y) < 9000.f)
		{
			for (const FVector2D& S : JeebTerrain::HutSites()) if (FVector2D::DistSquared(P, S) < 480.f * 480.f) return true;
			for (const JeebTerrain::FHutDef& H : JeebTerrain::VillageHuts()) if (FVector2D::DistSquared(P, H.Pos) < 480.f * 480.f) return true;
			if (FVector2D::DistSquared(P, FVector2D(JeebTerrain::CottagePos().X, JeebTerrain::CottagePos().Y)) < 480.f * 480.f) return true;
		}
		return false;
	}
}

AOpenWorld::AOpenWorld()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Flakes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("SnowFlakes"));
	Flakes->SetupAttachment(Root);
	Flakes->SetStaticMesh(Cube.Object);
	Flakes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Flakes->SetCanEverAffectNavigation(false);
	Flakes->SetCastShadow(false);
	Flakes->bAffectDistanceFieldLighting = false;
	Rand.Initialize(4242);
}

void AOpenWorld::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(FVector::ZeroVector);
	BuildHouses();
	if (IsNetMode(NM_DedicatedServer)) return;   // (no snow to draw on a server)
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color")))
		if (UMaterialInstanceDynamic* M = Flakes->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.5f, 1.52f, 1.58f));
	TArray<FTransform> X;
	Flake.SetNum(NumFlakes); NewT.SetNum(NumFlakes); PrevT.SetNum(NumFlakes); Seen.SetNumZeroed(NumFlakes);
	for (int32 i = 0; i < NumFlakes; ++i)
	{
		FFlake& F = Flake[i];
		F.Pos = FVector(Rand.FRandRange(-BoxR, BoxR), Rand.FRandRange(-BoxR, BoxR), Rand.FRandRange(-BoxBelow, BoxAbove));
		F.Fall = Rand.FRandRange(90.f, 170.f);
		F.Phase = Rand.FRandRange(0.f, 6.28f);
		F.Size = Rand.FRandRange(0.013f, 0.028f);
		F.Spin = Rand.FRandRange(0.5f, 1.6f);
		F.bUnderRoof = false;
		X.Add(FTransform(FQuat::Identity, FVector(0.f, 0.f, -100000.f), FVector(0.001f)));
		NewT[i] = PrevT[i] = X.Last();
	}
	Flakes->AddInstances(X, false, true, false);
	Flakes->SetHasPerInstancePrevTransforms(true);   // motion vectors for TSR: no ghost trails
	SetActorTickEnabled(true);
}

// A placeholder house at each town still to be built: the rebuilt Oak Hill house and its collision model (Steele Creek's houses are the
// same pair), on the site's level ground, its door toward the middle of the site
void AOpenWorld::BuildHouses()
{
	const JeebOakHill::FModel* Model = JeebOakHill::FindModel(TEXT("OakHillHouse"));
	const TCHAR* MeshPath = nullptr;
	for (const JeebTownLab::FLabSwap& S : JeebTownLab::ModelSwaps) if (FCString::Strcmp(S.Model, TEXT("OakHillHouse")) == 0) MeshPath = S.Mesh;
	if (!Model || !MeshPath) { UE_LOG(LogTemp, Warning, TEXT("JEEBOPEN no house model: the town sites stay empty")); return; }
	const bool bServer = IsNetMode(NM_DedicatedServer);
	const FString Path(MeshPath), Name = FPaths::GetBaseFilename(Path);
	UStaticMesh* Mesh = bServer ? nullptr : LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%s.%s"), *Path, *Name), nullptr, LOAD_Quiet | LOAD_NoWarn);
	UStaticMesh* Col = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%s_Col.%s_Col"), *Path, *Name), nullptr, LOAD_Quiet | LOAD_NoWarn);
	UInstancedStaticMeshComponent* Solid = nullptr;
	if (Col)
	{
		Solid = NewObject<UInstancedStaticMeshComponent>(this);
		Solid->SetStaticMesh(Col);
		Solid->SetupAttachment(GetRootComponent());
		Solid->SetMobility(EComponentMobility::Static);
		Solid->SetVisibility(false);
		Solid->SetHiddenInGame(true);
		Solid->SetCastShadow(false);
		Solid->SetCollisionProfileName(TEXT("BlockAll"));
		Solid->SetCanEverAffectNavigation(false);
		Solid->RegisterComponent();
	}
	uint32 Hash = 2166136261u;
	auto HashIn = [&](float V) { const int32 Q = FMath::RoundToInt(V * 10.f); for (int32 b = 0; b < 4; ++b) { Hash ^= uint32((Q >> (8 * b)) & 0xFF); Hash *= 16777619u; } };
	const float ScaleZ = Model->ScaleZ > 0.f ? Model->ScaleZ : Model->Scale;
	int32 Placed = 0;
	for (const JeebOpen::FSite& S : JeebOpen::Sites())
	{
		const float Z = JeebTerrain::HeightAt(S.House.X, S.House.Y);
		const float Yaw = FRotator::NormalizeAxis(S.HouseYaw - Model->DoorYaw);
		const FTransform Xf(FRotator(0.f, Yaw, 0.f), FVector(S.House.X, S.House.Y, Z), FVector(Model->Scale, Model->Scale, ScaleZ));
		if (Mesh)
		{
			UStaticMeshComponent* B = NewObject<UStaticMeshComponent>(this);
			B->SetStaticMesh(Mesh);
			B->SetupAttachment(GetRootComponent());
			B->SetMobility(EComponentMobility::Static);
			B->SetWorldTransform(Xf);
			B->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			B->SetCustomPrimitiveDataFloat(0, FMath::Frac(0.37f * (Placed + 1)));
			B->RegisterComponent();
		}
		if (Solid) Solid->AddInstance(Xf, true);
		else
		{
			// (no collision model: a box over the walls. Unreal mirrors Blender's Y: the walls run from (WX0, -WY1) to (WX1, -WY0))
			const FVector2D Lo(Model->WX0 * Model->Scale, -Model->WY1 * Model->Scale), Hi(Model->WX1 * Model->Scale, -Model->WY0 * Model->Scale);
			const FVector2D M = (Lo + Hi) * 0.5f, Half = (Hi - Lo) * 0.5f;
			UBoxComponent* Walls = NewObject<UBoxComponent>(this);
			Walls->SetupAttachment(GetRootComponent());
			Walls->SetBoxExtent(FVector(Half.X, Half.Y, 300.f));
			Walls->SetWorldLocationAndRotation(Xf.TransformPositionNoScale(FVector(M.X, M.Y, 300.f)), FRotator(0.f, Yaw, 0.f));
			Walls->SetCollisionProfileName(TEXT("BlockAll"));
			Walls->SetMobility(EComponentMobility::Static);
			Walls->SetCanEverAffectNavigation(false);
			Walls->RegisterComponent();
		}
		HashIn(S.House.X); HashIn(S.House.Y); HashIn(Z); HashIn(Yaw);
		++Placed;
		UE_LOG(LogTemp, Log, TEXT("JEEBOPEN house for %s at %.0f %.0f %.0f yaw %.0f (%s)"), S.Name, S.House.X, S.House.Y, Z, Yaw, Solid ? TEXT("its collision model") : TEXT("a box over the walls"));
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBOPEN houses: %d placeholder houses; hash=%08x%s"), Placed, Hash, bServer ? TEXT(" (server: solids only)") : TEXT(""));
}

void AOpenWorld::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;   // (this machine's own view: looks only)
	if (!PC || !PC->PlayerCameraManager || !Flakes || Flake.Num() == 0) return;
	Time += DeltaSeconds;
	const AGameStateBase* GS = W->GetGameState();
	const double Clock = GS ? GS->GetServerWorldTimeSeconds() : double(W->GetTimeSeconds());
	const float Force = CVarSnowForce.GetValueOnGameThread();
	SnowNow = Force >= 0.f ? FMath::Min(Force, 1.f) : JeebOpen::Snowing(Clock);
	const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
	const FVector Fwd = PC->PlayerCameraManager->GetCameraRotation().Vector();
	const float High = JeebWorld::Smooth(1500.f, 7000.f, Cam.Z);
	const float Amount = SnowNow * FMath::Lerp(FMath::Clamp(CVarSnowLow.GetValueOnGameThread(), 0.f, 1.f), 1.f, High) * 0.5f * FMath::Clamp(CVarSnowAmount.GetValueOnGameThread(), 0.f, 2.f);
	const int32 Want = FMath::Clamp(FMath::RoundToInt(NumFlakes * Amount), 0, NumFlakes);
	if (Want == 0 && Shown == 0) return;
	const float SizeK = FMath::Clamp(CVarSnowSize.GetValueOnGameThread(), 0.2f, 4.f);
	const FVector C = Cam + FVector(Fwd.X, Fwd.Y, 0.f).GetSafeNormal() * 500.f;
	const int32 Upto = FMath::Max(Want, Shown);
	for (int32 i = 0; i < Upto; ++i)
	{
		FFlake& F = Flake[i];
		if (i >= Want)   // (the snow is easing off: this one is put away)
		{
			NewT[i] = FTransform(FQuat::Identity, FVector(C.X, C.Y, -100000.f), FVector(0.001f));
			PrevT[i] = NewT[i]; Seen[i] = 0;
			continue;
		}
		bool bJumped = !Seen[i];
		F.Pos += (Wind + FVector(32.f * FMath::Sin(Time * 0.8f + F.Phase), 32.f * FMath::Cos(Time * 0.65f + F.Phase * 1.7f), -F.Fall)) * DeltaSeconds;
		// kept inside the box round the camera: a flake that leaves one side comes back in on the other
		FVector Rel = F.Pos - C;
		if (FMath::Abs(Rel.X) > BoxR) { Rel.X = FMath::Fmod(FMath::Fmod(Rel.X + BoxR, 2.f * BoxR) + 2.f * BoxR, 2.f * BoxR) - BoxR; bJumped = true; }
		if (FMath::Abs(Rel.Y) > BoxR) { Rel.Y = FMath::Fmod(FMath::Fmod(Rel.Y + BoxR, 2.f * BoxR) + 2.f * BoxR, 2.f * BoxR) - BoxR; bJumped = true; }
		if (Rel.Z < -BoxBelow || Rel.Z > BoxAbove) { const float Hgt = BoxBelow + BoxAbove; Rel.Z = FMath::Fmod(FMath::Fmod(Rel.Z + BoxBelow, Hgt) + Hgt, Hgt) - BoxBelow; bJumped = true; }
		if (bJumped) { F.Pos = C + Rel; F.bUnderRoof = UnderRoof(F.Pos.X, F.Pos.Y); }
		// (never in the camera's face: a flake within a couple of metres of it shrinks away, or it reads as a white block)
		const float S = F.bUnderRoof ? 0.001f : F.Size * SizeK * FMath::Max(0.03f, JeebWorld::Smooth(120.f, 320.f, float(FVector::Dist(F.Pos, Cam))));
		const FTransform Now(FRotator(Time * F.Spin * 70.f + F.Phase * 40.f, Time * F.Spin * 50.f, F.Phase * 30.f).Quaternion(), F.Pos, FVector(S));
		PrevT[i] = bJumped ? Now : NewT[i];
		NewT[i] = Now;
		Seen[i] = 1;
	}
	if (Upto > 0)
	{
		const TArray<FTransform> N(NewT.GetData(), Upto), P(PrevT.GetData(), Upto);
		Flakes->BatchUpdateInstancesTransforms(0, N, P, true, false, true);
	}
	Shown = Want;
}

#if !UE_BUILD_SHIPPING
// Test only (single player, or a listen server's own knight): jeeb.Test.GoTown N puts your knight at a town site (1 Silver Point,
// 2 Anchorage, 3 Birch Grove, 4 Palmer Square: the order of JeebOpen::Sites), 12 m short of its middle so the name shows on walking in;
// jeeb.Test.GoPeak puts him on a snowy shoulder of the northern range, looking south over the map
namespace
{
	void GoTo(UWorld* W, const FVector2D& At, float Yaw)
	{
		if (!W) return;
		for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
			if (APlayerController* PC = It->Get())
				if (APawn* P = PC->GetPawn())
					if (P->HasAuthority())
					{
						P->TeleportTo(FVector(At.X, At.Y, JeebTerrain::HeightAt(At.X, At.Y) + 130.f), FRotator(0.f, Yaw, 0.f));
						PC->SetControlRotation(FRotator(-8.f, Yaw, 0.f));
					}
	}
	FAutoConsoleCommandWithWorldAndArgs GoTown(TEXT("jeeb.Test.GoTown"), TEXT("Test: jeeb.Test.GoTown N puts your knight at a town site still to be built (1 Silver Point, 2 Anchorage, 3 Birch Grove, 4 Palmer Square; single player)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const TArray<JeebOpen::FSite>& S = JeebOpen::Sites();
			const int32 N = Args.Num() ? FCString::Atoi(*Args[0]) - 1 : 0;
			if (!S.IsValidIndex(N)) return;
			const FVector2D Out = (S[N].Pos - S[N].House).GetSafeNormal();   // (away from the house, on the far side of the middle)
			const FVector2D At = S[N].Pos + Out * (S[N].Radius + 1200.f);
			GoTo(W, At, FMath::RadiansToDegrees(FMath::Atan2(-Out.Y, -Out.X)));
		}));
	FAutoConsoleCommandWithWorld GoPeak(TEXT("jeeb.Test.GoPeak"), TEXT("Test: puts your knight high on the northern range, looking south over the map (single player)"),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
		{
			// the highest ground on a coarse sweep of the range
			FVector2D Best(88000.f, 20000.f); float Top = -1.0e9f;
			for (float X = 62000.f; X < JeebWorld::MaxX - 6000.f; X += 1600.f)
				for (float Y = JeebWorld::MinY + 8000.f; Y < 84000.f; Y += 1600.f) { const float Z = JeebTerrain::HeightAt(X, Y); if (Z > Top) { Top = Z; Best = FVector2D(X, Y); } }
			GoTo(W, Best, 180.f);
		}));
}
#endif

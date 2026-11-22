#include "SteeleCreekTown.h"
#include "SteeleCreek.h"
#include "Terrain.h"
#include "TownLabDecor.h"
#include "LanternBrackets.h"
#include "TownLabSwaps.h"
#include "DayNight.h"
#include "LampLook.h"
#include "MiningRock.h"
#include "SkillComponent.h"
#include "Npc.h"
#include "KnightCharacter.h"
#include "Store.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	TAutoConsoleVariable<float> CVarSteeleDensity(TEXT("jeeb.Steele.Density"), 1.f,
		TEXT("How thick the forest round and in Steele Creek is (1 as built, about 1,000 trees; 0.5 half as many; 0 none). Read when the world is built: change it, then load the map again. Online, give the server and every player the same value (the trunks are solid)."), ECVF_Default);

#if !UE_BUILD_SHIPPING
	// (test only: puts the local player's knight in Steele Creek's forge yard; single player or a listen server's own knight)
	FAutoConsoleCommandWithWorld GoSteele(TEXT("jeeb.Test.GoSteele"), TEXT("Test: puts your knight in Steele Creek's forge yard (single player)"),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
		{
			if (!W) return;
			for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
				if (APlayerController* PC = It->Get())
					if (APawn* P = PC->GetPawn())
						if (P->HasAuthority())
						{
							const FVector2D S = JeebSteele::ToWorld(JeebSteele::SpawnE, JeebSteele::SpawnN);
							P->TeleportTo(FVector(S.X, S.Y, JeebTerrain::HeightAt(S.X, S.Y) + 130.f), FRotator(0.f, JeebSteele::SpawnYaw, 0.f));
							PC->SetControlRotation(FRotator(-10.f, JeebSteele::SpawnYaw, 0.f));
						}
		}));
#endif

#if !UE_BUILD_SHIPPING
	// (test only, -SteeleNetTest on a dedicated server and its client: the server puts every knight beside Steele Creek's shopkeeper,
	// the client opens the store, sells and buys through the server's checks, and both log JEEBSTEELE NET lines)
	bool NetTestOn() { static const bool b = FParse::Param(FCommandLine::Get(), TEXT("SteeleNetTest")); return b; }
	ANpc* SteeleShopkeeper(UWorld* W)
	{
		for (TActorIterator<ANpc> It(W); It; ++It)
			if (It->GetRole() == ENpcRole::Shopkeeper && JeebSteele::InBox(It->GetActorLocation().X, It->GetActorLocation().Y)) return *It;
		return nullptr;
	}
	void NetTestStep(ASteeleCreekTown* Town, int32& Stage, double& T0)
	{
		UWorld* W = Town->GetWorld();
		const bool bServer = Town->IsNetMode(NM_DedicatedServer), bClient = Town->IsNetMode(NM_Client);
		ANpc* Keeper = SteeleShopkeeper(W);
		if (!Keeper) return;
		if (bServer && Stage == 0 && !TActorIterator<AKnightCharacter>(W)) { T0 = FPlatformTime::Seconds(); return; }   // (the server's clock starts when the first knight is in)
		const double Since = FPlatformTime::Seconds() - T0;
		auto Report = [&](AKnightCharacter* K, const TCHAR* Who)
		{
			const FVector L = K->GetActorLocation();
			const float G = JeebTerrain::HeightAt(L.X, L.Y);
			const UCharacterMovementComponent* M = K->GetCharacterMovement();
			UE_LOG(LogTemp, Display, TEXT("JEEBSTEELE NET %s %s at E %.1f N %.1f: feet %.0f cm above the ground, on ground %d, %.0f cm from the shopkeeper, ingots %d"), Who, *K->GetName(),
				JeebSteele::EastM(L.Y), JeebSteele::NorthM(L.X), L.Z - K->GetSimpleCollisionHalfHeight() - G, M && M->IsMovingOnGround() ? 1 : 0,
				FVector::Dist2D(L, Keeper->GetActorLocation()), K->GetSkills() ? K->GetSkills()->GetIngots() : -1);
		};
		if (bServer && Stage == 0 && Since > 10.0)
		{
			Stage = 1;
			int32 n = 0;
			for (TActorIterator<AKnightCharacter> It(W); It; ++It)
			{
				const FVector2D F = FVector2D(Keeper->GetActorForwardVector()) * 260.f + FVector2D(Keeper->GetActorRightVector()) * (120.f * n++);
				const FVector2D P = FVector2D(Keeper->GetActorLocation()) + F;
				It->TeleportTo(FVector(P.X, P.Y, JeebTerrain::HeightAt(P.X, P.Y) + 120.f), It->GetActorRotation());
				UE_LOG(LogTemp, Display, TEXT("JEEBSTEELE NET server moved %s beside %s"), *It->GetName(), *Keeper->GetNpcName());
			}
		}
		else if (bServer && Stage == 1 && Since > 16.0) { Stage = 2; for (TActorIterator<AKnightCharacter> It(W); It; ++It) Report(*It, TEXT("server sees")); }
		else if (bServer && Stage == 2 && Since > 50.0) { Stage = 3; for (TActorIterator<AKnightCharacter> It(W); It; ++It) Report(*It, TEXT("server after trading")); }
		else if (bClient)
		{
			APlayerController* PC = W->GetFirstPlayerController();   // (test only: the client's own knight)
			AKnightCharacter* K = PC ? Cast<AKnightCharacter>(PC->GetPawn()) : nullptr;
			if (!K) return;
			if (Stage == 0 && Since > 35.0)
			{
				Stage = 1;
				Report(K, TEXT("client"));
				K->OpenShop();
				UE_LOG(LogTemp, Display, TEXT("JEEBSTEELE NET client store window open %d"), K->IsShopOpen() ? 1 : 0);
				for (int32 t = 0; t <= static_cast<int32>(EItemType::Bones); ++t) K->Inv(EInvOp::SellAll, t);
				const TArray<JeebStore::FWare>& Wares = JeebStore::Stock();
				int32 Pick = -1;
				for (int32 i = 0; i < Wares.Num(); ++i) if (Wares[i].Stock > 0 && (Pick < 0 || Wares[i].Price < Wares[Pick].Price)) Pick = i;
				if (Pick >= 0) { K->Inv(EInvOp::Buy, Pick, Wares[Pick].Id); UE_LOG(LogTemp, Display, TEXT("JEEBSTEELE NET client asked to sell everything and buy ware %d (id %d, %d ingots)"), Pick, Wares[Pick].Id, Wares[Pick].Price); }
				else UE_LOG(LogTemp, Display, TEXT("JEEBSTEELE NET client asked to sell everything (no stock to buy)"));
			}
			else if (Stage == 1 && Since > 41.0) { Stage = 2; Report(K, TEXT("client after trading")); }
		}
	}
#endif

	float Hash01(uint32 N) { N ^= N >> 16; N *= 0x7feb352dU; N ^= N >> 15; N *= 0x846ca68bU; N ^= N >> 16; return float(N & 0xffffff) / float(0x1000000); }
	void HashIn(uint32& H, float V)
	{
		const int32 Q = FMath::RoundToInt(V * 10.f);   // millimetres
		for (int32 b = 0; b < 4; ++b) { H ^= uint32((Q >> (8 * b)) & 0xFF); H *= 16777619u; }
	}
	bool InstancingOk(UStaticMesh* Mesh)
	{
		for (const FStaticMaterial& M : Mesh->GetStaticMaterials())
			if (M.MaterialInterface && M.MaterialInterface->GetMaterial() && !M.MaterialInterface->GetMaterial()->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes)) return false;
		return true;
	}
	UStaticMesh* LoadMesh(const FString& Path)
	{
		// (weak pointers: the cache outlives a world, as in TownLabTown.cpp)
		static TMap<FString, TWeakObjectPtr<UStaticMesh>> Cache;
		if (const TWeakObjectPtr<UStaticMesh>* Got = Cache.Find(Path)) if (Got->IsValid()) return Got->Get();
		FString P = Path;
		if (P.Contains(TEXT("."))) P = P.Left(P.Find(TEXT(".")));
		const FString Name = FPaths::GetBaseFilename(P);
		UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%s.%s"), *P, *Name), nullptr, LOAD_Quiet | LOAD_NoWarn);
		Cache.Add(Path, M);
		return M;
	}
	UStaticMesh* LoadDecor(const TCHAR* FullPath)
	{
		FString P(FullPath);
		P = P.Left(P.Find(TEXT(".")) >= 0 ? P.Find(TEXT(".")) : P.Len());
		if (P.StartsWith(TEXT("/Game/Filler/")))
			if (UStaticMesh* M = LoadMesh(P.Replace(TEXT("/Game/Filler/"), TEXT("/Game/TownDecor/")))) return M;
		return LoadMesh(P);
	}
	const TCHAR* RebuiltMesh(const TCHAR* Model)
	{
		for (const JeebTownLab::FLabSwap& S : JeebTownLab::ModelSwaps) if (FCString::Strcmp(S.Model, Model) == 0) return S.Mesh;
		return nullptr;
	}

	// The kinds of tree the town's forest uses are AOakForest's (OakForest.cpp builds them): 0 cork oak, 1 aspen, 2-4 full spruces, 5 half
	// spruce. Crown and trunk radius at scale 1 (cm), the same numbers as its kind table
	constexpr float CrownOf[6] = { 570.f, 235.f, 250.f, 340.f, 330.f, 210.f };
	constexpr float TrunkOf[6] = { 38.f, 16.f, 22.f, 22.f, 22.f, 20.f };

	// The townsfolk's beats (metres from the centre), in SpawnPeople's order: shared with the town's own trees, which keep off them
	const TArray<TArray<FVector2D>>& Beats()
	{
		static const TArray<TArray<FVector2D>> B = {
			{ FVector2D(-18.f, -8.f), FVector2D(-10.f, -11.f), FVector2D(-7.f, 3.f), FVector2D(-17.f, 6.f) },      // Thessaly
			{ FVector2D(14.f, 12.f), FVector2D(30.f, 30.f), FVector2D(46.f, 48.f), FVector2D(54.f, 58.f) },       // Brug
			{ FVector2D(-14.f, -12.f), FVector2D(-26.f, -28.f), FVector2D(-38.f, -44.f) },                        // Old Marg
			{ FVector2D(4.f, -12.f), FVector2D(12.f, -13.f), FVector2D(21.f, -26.f), FVector2D(32.f, -40.f) },    // Hruk
			{ FVector2D(-12.f, 7.f), FVector2D(-33.f, 21.f), FVector2D(-60.f, 38.f), FVector2D(-88.f, 54.f) },    // Sir Kell
			{ FVector2D(-42.f, -50.f), FVector2D(-55.f, -69.f), FVector2D(-63.f, -84.f) },                        // Dame Rhosyn
			{ FVector2D(4.f, 12.f), FVector2D(9.f, 27.f), FVector2D(-14.f, 31.f) },                               // Sir Oswin
		};
		return B;
	}

	// Steele Creek's own smithing props (PlaceProps), also kept clear by the town's trees
	struct FProp { const TCHAR* Mesh; FVector2D At; float Yaw; float Scale; };
	TArray<FProp> TownProps()
	{
		TArray<FProp> Props;
		if (const JeebSteele::FPlot* S = JeebSteele::FindPlot(TEXT("Smithy")))
		{
			float Yaw = 0.f;
			// (the anvil just in front of where the blacksmith stands, DoorSpot 3.8 m: he faces out of the door, toward it)
			Props.Add({ TEXT("/Game/SteeleCreek/SM_Anvil"), JeebSteele::DoorSpot(*S, 500.f, &Yaw), Yaw + 90.f, 1.f });
			Props.Add({ TEXT("/Game/SteeleCreek/SM_Anvil"), JeebSteele::ToWorld(7.5f, -2.5f), 200.f, 0.9f });
		}
		Props.Add({ TEXT("/Game/SteeleCreek/SM_MineCart"), JeebSteele::ToWorld(4.5f, -6.5f), 30.f, 1.f });     // ore come down from the quarry
		Props.Add({ TEXT("/Game/SteeleCreek/SM_MineCart"), JeebSteele::ToWorld(53.f, 60.5f), 225.f, 1.f });    // at the quarry
		Props.Add({ TEXT("/Game/SteeleCreek/SM_MineCart"), JeebSteele::ToWorld(60.f, 72.f), 160.f, 1.f });
		Props.Add({ TEXT("/Game/SteeleCreek/SM_IngotRack"), JeebSteele::ToWorld(10.5f, 8.8f), 250.f, 1.f });   // the smithy's finished bars
		Props.Add({ TEXT("/Game/SteeleCreek/SM_IngotRack"), JeebSteele::ToWorld(-1.5f, -14.6f), 10.f, 1.f });  // for sale at the ore market
		Props.Add({ TEXT("/Game/SteeleCreek/SM_IngotRack"), JeebSteele::ToWorld(25.5f, -6.2f), 95.f, 0.9f });
		return Props;
	}

	// Where the town fill puts its sets in Steele Creek (FillOakHill.csv rows whose Site is Steele), game cm: the town's trees keep off
	TArray<FVector2D> FillSpots()
	{
		TArray<FVector2D> Out;
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectContentDir() / TEXT("Data/FillOakHill.csv")))) return Out;
		for (const FString& L : Lines)
		{
			if (L.StartsWith(TEXT("#"))) continue;
			TArray<FString> C;
			L.ParseIntoArray(C, TEXT(","), false);
			if (C.Num() < 9 || !C.Last().TrimStartAndEnd().StartsWith(TEXT("st"), ESearchCase::IgnoreCase)) continue;
			Out.Add(JeebSteele::ToWorld(FCString::Atof(*C[1]), FCString::Atof(*C[2])));
		}
		return Out;
	}
	float SegDist(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const float T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / FMath::Max(AB.SizeSquared(), 1.f), 0.f, 1.f);
		return FVector2D::Distance(P, A + AB * T);
	}
}

// ------------------------------------------------------------------ the forest in and round the town (planned here, drawn by AOakForest)
// A jittered 6.5 m lattice (the forest's own) out to 200 m from the forge yard. Every random number of a cell is drawn before any test, so
// every machine plans the same trees (the server and the clients compare JEEBFOREST's and JEEBSTEELE's checksums). How many are kept:
//  - inside the town (buildings on at least three sides within 45 m of the spot): GapWant, tall trees in the gaps between the plots and
//    at street ends, oaks where the roofs leave room;
//  - behind the outer buildings: a dense belt from a few metres off the walls, thinning from 14 m to 85 m out toward the land beyond, which
//    keeps a thin patchy stand to 200 m.
// Kept clear of: the streets (and so the road out), the forge yard and where players land, the river, walls, the space in front of every
// door, the quarry's rocks, the townsfolk's walks, the town's props and fill sets, and each other. jeeb.Steele.Density scales every
// share (read at world build).
const TArray<JeebSteeleWoods::FSpot>& JeebSteeleWoods::Plan()
{
	static TArray<FSpot> Spots;
	static float Built = -1.f;
	const float Density = FMath::Clamp(CVarSteeleDensity.GetValueOnGameThread(), 0.f, 3.f);
	if (Built == Density) return Spots;
	Built = Density;
	Spots.Reset();
	if (!JeebSteele::Enabled() || Density <= 0.f) return Spots;
	const double T0 = FPlatformTime::Seconds();
	const FVector2D Yard = JeebSteele::ToWorld(-5.f, 0.f), Arrival = JeebSteele::ToWorld(JeebSteele::SpawnE, JeebSteele::SpawnN), Mid = JeebSteele::ToWorld(1.f, 0.f);
	const float Reach = 23000.f, Cell = 650.f, GapWant = 0.34f;
	const TArray<FProp> Props = TownProps();
	const TArray<FVector2D> Fill = FillSpots();
	TArray<FVector2D> Doors;
	for (const JeebSteele::FPlot& P : JeebSteele::Plots()) if (!P.bWell) { Doors.Add(JeebSteele::DoorSpot(P, 250.f)); Doors.Add(JeebSteele::DoorSpot(P, 700.f)); }
	auto Noise = [](const FVector2D& P, float Scale, float Ox, float Oy) { return FMath::PerlinNoise2D(FVector2D(P.X / Scale + Ox, P.Y / Scale + Oy)); };
	FRandomStream R(51523);
	const int32 N = FMath::CeilToInt(2.f * Reach / Cell);
	TArray<FVector4f> Planted;   // (X, Y, crown radius)
	int32 Tried = 0, InTown = 0;
	for (int32 i = 0; i < N; ++i)
		for (int32 j = 0; j < N; ++j)
		{
			const float Jx = R.FRand(), Jy = R.FRand(), Keep = R.FRand(), Pick = R.FRand(), Pick2 = R.FRand(), Size = R.FRand(), Yaw = R.FRandRange(0.f, 360.f);
			const float X = Yard.X - Reach + (i + Jx) * Cell, Y = Yard.Y - Reach + (j + Jy) * Cell;
			const FVector2D P(X, Y);
			if (FVector2D::Distance(P, Yard) > Reach) continue;
			++Tried;
			// how enclosed by buildings it is: how many of the four sides (east, west, north, south) have a plot centre within 45 m
			int32 Sides = 0;
			{
				bool bSide[4] = {};
				for (const JeebSteele::FPlot& Pl : JeebSteele::Plots())
				{
					const FVector2D D = Pl.Centre - P;
					if (D.SizeSquared() > 6500.f * 6500.f) continue;
					if (FMath::Abs(D.X) >= FMath::Abs(D.Y)) bSide[D.X > 0.f ? 0 : 1] = true; else bSide[D.Y > 0.f ? 2 : 3] = true;
				}
				Sides = int32(bSide[0]) + int32(bSide[1]) + int32(bSide[2]) + int32(bSide[3]);
			}
			const float Fabric = FMath::Clamp((Sides - 1) * 0.5f, 0.f, 1.f);
			const float Dd = FMath::Min(JeebSteele::BuildingDistance(X, Y), 12000.f) / 100.f;   // metres off the nearest wall
			// (the belt's edge is broken into lobes: its reach is bent by a noise, 0-45 m either way, and a second noise opens clearings)
			const float DdL = FMath::Max(0.f, Dd + 26.f * Noise(P, 4200.f, 17.3f, -5.1f) + 10.f * Noise(P, 1500.f, 2.2f, 9.9f));
			const float Glade = JeebWorld::Smooth(-0.42f, -0.22f, Noise(P, 2600.f, -8.4f, 3.7f));
			const float Belt = FMath::Lerp(1.f, 0.3f, JeebWorld::Smooth(18.f, 120.f, DdL)) * FMath::Lerp(0.3f, 1.f, Glade);
			// a slow noise opens glades in the belt's thin outer part and thickens it in patches
			const float Patch = FMath::Lerp(1.f, FMath::Lerp(0.55f, 1.25f, 0.5f + 0.5f * Noise(P, 9000.f, 3.3f, 8.8f)), JeebWorld::Smooth(30.f, 70.f, Dd));
			const float Want = Density * FMath::Lerp(Belt * Patch, GapWant, Fabric);
			if (Keep >= Want) continue;
			// what grows here: spruce stands and aspen clumps by noise, more broadleaves in the town
			const float ConiferField = 0.5f + 0.5f * Noise(P, 7000.f, -4.4f, 1.7f);
			const float ConiferP = FMath::Clamp(FMath::Lerp(0.4f, 0.85f, JeebWorld::Smooth(0.35f, 0.7f, ConiferField)) - 0.25f * Fabric, 0.05f, 0.9f);
			int32 K; float S;
			if (Pick < ConiferP)
			{
				if (Pick2 < 0.75f) { K = 2 + FMath::Min(int32(Pick2 / 0.25f), 2); S = FMath::Lerp(1.0f, 1.4f, Size); }
				else { K = 5; S = FMath::Lerp(0.95f, 1.2f, Size); }
			}
			else
			{
				const float AspenField = 0.5f + 0.5f * Noise(P, 5000.f, 6.1f, -2.9f);
				const bool bAspen = Pick2 < 0.2f + 0.6f * JeebWorld::Smooth(0.5f, 0.75f, AspenField);
				K = bAspen ? 1 : 0;
				S = bAspen ? FMath::Lerp(0.9f, 1.2f, Size) : FMath::Lerp(0.8f, 1.15f, Size);
			}
			const float Crown = CrownOf[K] * S, Trunk = TrunkOf[K] * S;
			if (JeebWorld::InWater(X, Y) || JeebWorld::NearRiver(X, Y, 600.f + Crown * 0.3f)) continue;
			if (JeebTerrain::PathClearance(X, Y) < 300.f + Crown * 0.3f) continue;   // (streets, the road out, the forge yard's patch)
			if (FVector2D::Distance(P, Mid) < 1500.f || FVector2D::Distance(P, Arrival) < 1500.f) continue;   // (the yard, where players start and land)
			if (JeebSteele::BuildingDistance(X, Y) < 400.f + Crown * 0.6f) continue;   // (a few metres off the walls, the crown clear of the roofs)
			bool bBad = false;
			for (const FVector2D& D : Doors) if (FVector2D::Distance(P, D) < 450.f + Crown * 0.3f) { bBad = true; break; }
			if (bBad) continue;
			for (const JeebSteele::FRockDef& Rk : JeebSteele::RockDefs) if (FVector2D::Distance(JeebSteele::ToWorld(Rk.E, Rk.N), P) < 1400.f + Crown * 0.2f) { bBad = true; break; }
			if (bBad) continue;
			for (const TArray<FVector2D>& B : Beats())
			{
				for (int32 s = 0; s + 1 < B.Num() && !bBad; ++s)
					bBad = SegDist(P, JeebSteele::ToWorld(B[s].X, B[s].Y), JeebSteele::ToWorld(B[s + 1].X, B[s + 1].Y)) < 300.f + Crown * 0.25f + Trunk;
				if (bBad) break;
			}
			if (bBad) continue;
			for (const FProp& Pr : Props) if (FVector2D::Distance(P, Pr.At) < 400.f + Crown * 0.2f) { bBad = true; break; }
			for (const FVector2D& F : Fill) if (!bBad && FVector2D::Distance(P, F) < 450.f + Crown * 0.2f) bBad = true;
			if (bBad) continue;
			for (const FVector4f& T : Planted) if (FVector2D::DistSquared(P, FVector2D(T.X, T.Y)) < FMath::Square(0.42f * (Crown + T.Z))) { bBad = true; break; }
			if (bBad) continue;
			Planted.Add(FVector4f(X, Y, Crown, 0.f));
			Spots.Add({ uint8(K), X, Y, S, Yaw });
			if (Fabric > 0.5f) ++InTown;
		}
	UE_LOG(LogTemp, Log, TEXT("JEEBSTEELE woods plan: %d trees from %d cells (%d in the town's own gaps), density %.2f, planned in %.0f ms"), Spots.Num(), Tried, InTown, Density, (FPlatformTime::Seconds() - T0) * 1000.0);
	return Spots;
}

ASteeleCreekTown::ASteeleCreekTown()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
	bReplicates = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	SetRootComponent(Root);
}

void ASteeleCreekTown::BeginPlay()
{
	Super::BeginPlay();
	if (!GetActorLocation().IsNearlyZero()) SetActorLocation(FVector::ZeroVector);
	if (IsNetMode(NM_DedicatedServer)) SetActorTickEnabled(false);
#if !UE_BUILD_SHIPPING
	if (NetTestOn()) { SetActorTickEnabled(true); NetTestT0 = FPlatformTime::Seconds(); }
#endif
	if (!JeebSteele::Enabled()) return;
	const double T0 = FPlatformTime::Seconds();
	BuildBuildings();
	const double T1 = FPlatformTime::Seconds();
	BuildWoods();
	const double T2 = FPlatformTime::Seconds();
	PlaceProps();
	SpawnRocks();
	SpawnPeople();
	UE_LOG(LogTemp, Log, TEXT("JEEBSTEELE Steele Creek built: buildings %.0f ms, woods %.0f ms, rocks and people %.0f ms, %d drawn sets, %d streets, hash=%08x"),
		(T1 - T0) * 1000.0, (T2 - T1) * 1000.0, (FPlatformTime::Seconds() - T2) * 1000.0, DrawSets, JeebSteele::Streets().Num(), Hash);
}

void ASteeleCreekTown::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
	if (NetTestOn()) NetTestStep(this, NetTestStage, NetTestT0);
	if (IsNetMode(NM_DedicatedServer)) return;
#endif
	const float D = ADayNight::Dusk;
	if (FMath::Abs(D - LastDusk) < 0.005f) return;
	LastDusk = D;
	const float F = FMath::Lerp(0.9f, 1.35f, D);
	for (int32 i = 0; i < GlowMids.Num(); ++i)
	{
		if (GlowBase[i] > 0.f) GlowMids[i]->SetScalarParameterValue(TEXT("GlowStrength"), GlowBase[i] * F);
		GlowMids[i]->SetScalarParameterValue(TEXT("Night"), D);
	}
	for (UPointLightComponent* L : LanternLights) if (L) L->SetIntensity(70.f * FMath::Lerp(0.15f, 1.f, D));
}

// One plain component per building (the TownLab models), its solid shape on every machine, its decor and lanterns
void ASteeleCreekTown::BuildBuildings()
{
	const bool bServer = IsNetMode(NM_DedicatedServer);
	TMap<FString, UInstancedStaticMeshComponent*> Solids, DecorSets;
	TMap<UMaterialInterface*, int32> GlowOf;
	int32 Placed = 0, DecorPieces = 0, Lights = 0, Missing = 0;
	TArray<JeebLanternBrackets::FHook> LanternHooks;
	for (const JeebSteele::FPlot& P : JeebSteele::Plots())
	{
		const TCHAR* MeshPath = RebuiltMesh(P.Def.Model);
		if (!MeshPath) { UE_LOG(LogTemp, Warning, TEXT("JEEBSTEELE %s: no rebuilt model for %s"), P.Def.Name, P.Def.Model); continue; }
		const FString Path(MeshPath), ColPath = Path + TEXT("_Col");
		const FVector At(P.Centre.X, P.Centre.Y, P.Level + P.Def.Lift);
		const FTransform Xf(FRotator(0.f, P.Yaw, 0.f), At, FVector(P.Scale, P.Scale, P.ScaleZ));
		UE_LOG(LogTemp, Log, TEXT("JEEBSTEELE PLOT %s (%s) at %.0f %.0f %.0f yaw %.0f scale %.2f; ground there %.0f"), P.Def.Name, *FPaths::GetBaseFilename(Path), At.X, At.Y, At.Z, P.Yaw, P.Scale, JeebTerrain::HeightAt(At.X, At.Y));
		if (!bServer)
			if (UStaticMesh* Mesh = LoadMesh(Path))
			{
				UStaticMeshComponent* B = NewObject<UStaticMeshComponent>(this);
				B->SetStaticMesh(Mesh);
				B->SetupAttachment(GetRootComponent());
				B->SetMobility(EComponentMobility::Static);
				B->SetWorldTransform(Xf);
				B->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				B->SetCustomPrimitiveDataFloat(0, Hash01(uint32(FMath::RoundToInt(At.X * 0.1f)) * 83492791u ^ uint32(FMath::RoundToInt(At.Y * 0.1f)) * 2654435761u));
				// (2026-11-18 review: the ore market's stall read saturated orange, walls and checkered roof) here it wears its own
				// toned-down copy, MI_SteeleStall (Tools/fixlook_assets.py): rich brown wood and a muted roof; Oak Hill's stays as it was
				if (FCString::Strcmp(P.Def.Name, TEXT("Stall")) == 0)
					if (UMaterialInterface* Toned = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/SteeleCreek/MI_SteeleStall.MI_SteeleStall")))
						for (int32 i = 0; i < B->GetNumMaterials(); ++i) B->SetMaterial(i, Toned);
				for (int32 i = 0; i < B->GetNumMaterials(); ++i)
					if (UMaterialInterface* M = B->GetMaterial(i))
					{
						int32* Got = GlowOf.Find(M);
						if (!Got)
						{
							UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(M, this);
							GlowMids.Add(Mid);
							GlowBase.Add(Mid->K2_GetScalarParameterValue(TEXT("GlowStrength")));
							Got = &GlowOf.Add(M, GlowMids.Num() - 1);
						}
						B->SetMaterial(i, GlowMids[*Got]);
					}
				B->RegisterComponent();
				++DrawSets;
				// the model's own decor (TownLabDecor.h: ivy, moss, stones, lanterns), one instanced set per kind
				const FString MeshName = FPaths::GetBaseFilename(Path);
				const FString Kind = MeshName.StartsWith(TEXT("SM_TL_")) ? MeshName.Mid(3) : FString();
				if (const JeebTownLabDecor::FBuildingDecor* D = Kind.IsEmpty() ? nullptr : JeebTownLabDecor::For(*Kind))
					for (int32 i = 0; i < D->Num; ++i)
					{
						const JeebTownLabDecor::FDecor& Pc = D->Pieces[i];
						UStaticMesh* PM = LoadDecor(Pc.Mesh);
						if (!PM) { if (Missing++ < 10) UE_LOG(LogTemp, Warning, TEXT("JEEBSTEELE missing decor %s"), Pc.Mesh); continue; }
						const FTransform PieceXf = JeebTownLabDecor::Place(Xf, Pc);
						if (!InstancingOk(PM))
						{
							UStaticMeshComponent* Sm = NewObject<UStaticMeshComponent>(this);
							Sm->SetStaticMesh(PM);
							Sm->SetupAttachment(GetRootComponent());
							Sm->SetMobility(EComponentMobility::Static);
							Sm->SetWorldTransform(PieceXf);
							Sm->SetCastShadow(Pc.bCastShadow);
							Sm->SetCollisionEnabled(ECollisionEnabled::NoCollision);
							Sm->SetCachedMaxDrawDistance(9000.f);
							Sm->LDMaxDrawDistance = 9000.f;
							JeebLamp::ApplyToMesh(Sm, this);
							Sm->RegisterComponent();
							++DrawSets;
						}
						else
						{
							const FString Key = FString::Printf(TEXT("%s|%d"), *PM->GetPathName(), Pc.bCastShadow ? 1 : 0);
							UInstancedStaticMeshComponent*& Ism = DecorSets.FindOrAdd(Key);
							if (!Ism)
							{
								Ism = NewObject<UInstancedStaticMeshComponent>(this);
								Ism->SetStaticMesh(PM);
								Ism->SetupAttachment(GetRootComponent());
								Ism->SetMobility(EComponentMobility::Static);
								Ism->SetCastShadow(Pc.bCastShadow);
								Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
								Ism->SetCullDistances(0, 9000);
								JeebLamp::ApplyToMesh(Ism, this);
								Ism->RegisterComponent();
								++DrawSets;
							}
							Ism->AddInstance(PieceXf, true);
						}
						++DecorPieces;
						if (Pc.bLight)
						{
							UPointLightComponent* L = NewObject<UPointLightComponent>(this);
							L->SetupAttachment(GetRootComponent());
							L->SetWorldLocation(PieceXf.GetLocation() + FVector(0.f, 0.f, 25.f));
							L->SetIntensityUnits(ELightUnits::Lumens);
							L->SetIntensity(70.f);
							L->SetAttenuationRadius(300.f);
							JeebLamp::TrackLight(L);
							L->SetCastShadows(false);
							L->RegisterComponent();
							LanternLights.Add(L);
							++Lights;
							LanternHooks.Add(JeebLanternBrackets::HookOf(PM, PieceXf));
						}
					}
			}
		// solid shape on every machine (the server decides where he can walk); without one a box over the walls
		UInstancedStaticMeshComponent*& Solid = Solids.FindOrAdd(ColPath);
		if (!Solid)
			if (UStaticMesh* ColMesh = LoadMesh(ColPath))
			{
				Solid = NewObject<UInstancedStaticMeshComponent>(this);
				Solid->SetStaticMesh(ColMesh);
				Solid->SetupAttachment(GetRootComponent());
				Solid->SetMobility(EComponentMobility::Static);
				Solid->SetVisibility(false);
				Solid->SetHiddenInGame(true);
				Solid->SetCastShadow(false);
				Solid->SetCollisionProfileName(TEXT("BlockAll"));
				Solid->SetCanEverAffectNavigation(false);
				Solid->RegisterComponent();
			}
		if (Solid) { Solid->AddInstance(Xf, true); HashIn(Hash, 1.f); }
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("JEEBSTEELE no collision model %s: a box over the walls instead"), *ColPath);
			UBoxComponent* Walls = NewObject<UBoxComponent>(this);
			Walls->SetupAttachment(GetRootComponent());
			Walls->SetBoxExtent(FVector(P.Half.X, P.Half.Y, 300.f));
			Walls->SetWorldLocationAndRotation(FVector(P.Mid.X, P.Mid.Y, At.Z + 300.f), FRotator(0.f, P.Yaw, 0.f));
			Walls->SetCollisionProfileName(TEXT("BlockAll"));
			Walls->SetMobility(EComponentMobility::Static);
			Walls->SetCanEverAffectNavigation(false);
			Walls->RegisterComponent();
			HashIn(Hash, P.Half.X); HashIn(Hash, P.Half.Y);
		}
		HashIn(Hash, At.X); HashIn(Hash, At.Y); HashIn(Hash, At.Z); HashIn(Hash, P.Yaw); HashIn(Hash, P.Scale);
		++Placed;
	}
	if (!bServer) JeebLanternBrackets::Build(this, LanternHooks, TEXT("JEEBSTEELE"));   // (every hanging lantern on a wall arm or its own lamp post, 2026-11-20)
	UE_LOG(LogTemp, Log, TEXT("JEEBSTEELE %d buildings, %d glow materials, decor %d pieces (%d missing), %d lantern lights"), Placed, GlowMids.Num(), DecorPieces, Missing, Lights);
}

// The forest round and in the town is AOakForest's (2026-11-18 round 3: the 38 town trees and the sparse 250 m ring read as saplings in a
// meadow). JeebSteeleWoods::Plan() above, planned here because it knows the town, is handed to AOakForest, which draws the trees with its
// own colours, tinted leaves, shadow twins, near shadow fade, grass thinning and solid trunks. All this does is fold the plan into the
// town's own checksum and log it.
void ASteeleCreekTown::BuildWoods()
{
	const TArray<JeebSteeleWoods::FSpot>& Spots = JeebSteeleWoods::Plan();
	int32 PerKind[6] = {};
	for (const JeebSteeleWoods::FSpot& S : Spots) { HashIn(Hash, S.X); HashIn(Hash, S.Y); HashIn(Hash, S.Scale); HashIn(Hash, float(S.Kind)); ++PerKind[FMath::Min<int32>(S.Kind, 5)]; }
	UE_LOG(LogTemp, Log, TEXT("JEEBSTEELE woods: %d trees planned for AOakForest (cork oak %d, aspen %d, spruce %d %d %d, half spruce %d), density %.2f"),
		Spots.Num(), PerKind[0], PerKind[1], PerKind[2], PerKind[3], PerKind[4], PerKind[5], CVarSteeleDensity.GetValueOnGameThread());
}

// Steele Creek's own smithing props (Meshy, 2026-11-18: /Game/SteeleCreek, Saved/Steele/steele_prop_*.py): the anvil the blacksmith works
// at, ore carts, crates of ingots. Solid on every machine (box collision), drawn on clients; a missing mesh is skipped.
void ASteeleCreekTown::PlaceProps()
{
	const bool bServer = IsNetMode(NM_DedicatedServer);
	const TArray<FProp> Props = TownProps();
	TMap<UStaticMesh*, UInstancedStaticMeshComponent*> Sets;
	int32 Count = 0;
	for (const FProp& P : Props)
	{
		UStaticMesh* M = LoadMesh(P.Mesh);
		if (!M) continue;
		UInstancedStaticMeshComponent*& I = Sets.FindOrAdd(M);
		if (!I)
		{
			I = NewObject<UInstancedStaticMeshComponent>(this);
			I->SetStaticMesh(M);
			I->SetupAttachment(GetRootComponent());
			I->SetMobility(EComponentMobility::Static);
			I->SetCollisionProfileName(TEXT("BlockAll"));
			I->SetCanEverAffectNavigation(false);
			I->SetCullDistances(0, 9000);
			if (bServer) { I->SetVisibility(false); I->SetHiddenInGame(true); }
			I->RegisterComponent();
			++DrawSets;
		}
		I->AddInstance(FTransform(FRotator(0.f, P.Yaw, 0.f), FVector(P.At.X, P.At.Y, JeebTerrain::HeightAt(P.At.X, P.At.Y) - 3.f), FVector(P.Scale)), true);
		HashIn(Hash, P.At.X); HashIn(Hash, P.At.Y);
		++Count;
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBSTEELE props: %d of %d (Meshy smithing props in /Game/SteeleCreek)"), Count, Props.Num());
}

// The quarry's rocks, on every machine (matched online by where they stand, like the world's other rocks)
void ASteeleCreekTown::SpawnRocks()
{
	UWorld* World = GetWorld();
	const EItemType Ores[5] = { EItemType::IronOre, EItemType::SilverOre, EItemType::GoldOre, EItemType::Emerald, EItemType::Diamond };
	int32 Count = 0;
	for (const JeebSteele::FRockDef& Rk : JeebSteele::RockDefs)
	{
		const FVector2D P = JeebSteele::ToWorld(Rk.E, Rk.N);
		const FTransform Xf(FRotator(0.f, FRandomStream(FMath::RoundToInt(P.X) * 7919 + FMath::RoundToInt(P.Y) * 104729).FRandRange(0.f, 360.f), 0.f), FVector(P.X, P.Y, JeebTerrain::HeightAt(P.X, P.Y) - 8.f));
		if (AMiningRock* Rock = World->SpawnActorDeferred<AMiningRock>(AMiningRock::StaticClass(), Xf))
		{
			Rock->SetOre(Ores[FMath::Clamp(Rk.Ore, 0, 4)]);
			Rock->FinishSpawning(Xf);
			HashIn(Hash, P.X); HashIn(Hash, P.Y);
			++Count;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBSTEELE quarry: %d rocks"), Count);
}

// The town's people, on every machine like the old village site's (the server checks a shop's keeper is near: KnightCharacterNet.cpp NearTrader).
// Owner's mix: 40% knights, 20% wizards, 40% trolls; trolls run the smithy, wizards the bank, knights the store.
void ASteeleCreekTown::SpawnPeople()
{
	UWorld* World = GetWorld();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	int32 Count = 0;
	auto Spawn = [&](ENpcRole InRole, int32 Idx, uint8 Folk, const TCHAR* Name, const TCHAR* Hello, const FVector2D& At, float Yaw, const TArray<FVector2D>& PatrolEN)
	{
		if (ANpc* N = World->SpawnActor<ANpc>(FVector(At.X, At.Y, JeebTerrain::HeightAt(At.X, At.Y) + 92.f), FRotator(0.f, Yaw, 0.f), Params))
		{
			N->SetFolk(Folk, Name, Hello);
			N->Setup(InRole, Idx);
			if (PatrolEN.Num() > 1)
			{
				TArray<FVector2D> Pts;
				for (const FVector2D& EN : PatrolEN) Pts.Add(JeebSteele::ToWorld(EN.X, EN.Y));
				N->SetPatrol(Pts);
			}
			++Count;
		}
	};
	auto Door = [](const TCHAR* Plot, float Out, float& Yaw) { const JeebSteele::FPlot* P = JeebSteele::FindPlot(Plot); return P ? JeebSteele::DoorSpot(*P, Out, &Yaw) : JeebSteele::ToWorld(0.f, 0.f); };
	float Yaw = 0.f;
	FVector2D At = Door(TEXT("Smithy"), 380.f, Yaw);
	Spawn(ENpcRole::Blacksmith, 0, 1, TEXT("Grakka Steelhand"), TEXT("Hrrm. The forge of Steele Creek never cools, small one. Best ore in the land comes down that track."), At, Yaw, TArray<FVector2D>());
	At = Door(TEXT("Bank"), 220.f, Yaw);
	Spawn(ENpcRole::Banker, 0, 2, TEXT("Master Orrin Vale"), TEXT("Welcome to the Steele Creek vault. The runes on its door keep your things safer than any lock."), At, Yaw, TArray<FVector2D>());
	At = Door(TEXT("Store"), 220.f, Yaw);
	Spawn(ENpcRole::Shopkeeper, 0, 0, TEXT("Sir Dunmore, the Trader"), TEXT("Well met! I hung up my sword for a shop counter. Ore, ingots, bread: Steele Creek's store has it."), At, Yaw, TArray<FVector2D>());
	// villagers and knights on their beats (metres from the centre)
	const TArray<TArray<FVector2D>>& Bt = Beats();
	Spawn(ENpcRole::Villager, 0, 2, TEXT("Thessaly the Seer"), TEXT("Ah. I saw you coming, of course."), JeebSteele::ToWorld(-18.f, -8.f), 0.f, Bt[0]);
	Spawn(ENpcRole::Villager, 3, 1, TEXT("Brug the Miner"), TEXT("Up the track's the quarry. Silver, gold, the green stones. Mind your fingers."), JeebSteele::ToWorld(14.f, 12.f), 45.f, Bt[1]);
	Spawn(ENpcRole::Villager, 5, 1, TEXT("Old Marg"), TEXT("Hmph. Another one of you little folk. Mind the mud on the river lane."), JeebSteele::ToWorld(-14.f, -12.f), 220.f, Bt[2]);
	Spawn(ENpcRole::Villager, 7, 1, TEXT("Hruk"), TEXT("Ingots! Fresh ingots! Hah, not really. Grakka keeps the good ones."), JeebSteele::ToWorld(4.f, -12.f), 120.f, Bt[3]);
	Spawn(ENpcRole::GateKnight, 0, 0, TEXT("Sir Kell of the Ridge"), TEXT("Halt! Ah, a traveller. The road north follows the river all the way to Oak Hill."), JeebSteele::ToWorld(-12.f, 7.f), 300.f, Bt[4]);
	Spawn(ENpcRole::GateKnight, 0, 0, TEXT("Dame Rhosyn"), TEXT("Well met. I keep watch on the landing; the river's quiet, mostly."), JeebSteele::ToWorld(-42.f, -50.f), 220.f, Bt[5]);
	Spawn(ENpcRole::GateKnight, 0, 0, TEXT("Sir Oswin"), TEXT("Steele Creek's steel is the best there is. I'd know: I've broken plenty of the other kind."), JeebSteele::ToWorld(4.f, 12.f), 0.f, Bt[6]);
	UE_LOG(LogTemp, Log, TEXT("JEEBSTEELE people: %d"), Count);
}

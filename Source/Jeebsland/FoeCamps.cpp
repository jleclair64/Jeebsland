#include "FoeCamps.h"
#include "FoeKnight.h"
#include "FoeData.h"
#include "OakForest.h"
#include "ForestRuins.h"
#include "KnightCharacter.h"
#include "HealthComponent.h"
#include "LampLook.h"
#include "Terrain.h"
#include "WorldMap.h"
#include "OakHill.h"
#include "TownLab.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#if !UE_BUILD_SHIPPING
#include "QuestComponent.h"
#include "QuestPeople.h"
#include "QuestData.h"
#include "Npc.h"
#include "Loot.h"
#include "SkillComponent.h"
#include "KnightGameMode.h"
#include "KnightPlayerController.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "GameFramework/PlayerController.h"
#endif

static TAutoConsoleVariable<int32> CVarFoesBandits(TEXT("jeeb.Foes.Bandits"), 1,
	TEXT("1: the server places the bandits at their camps (FoeCamps.csv) when the world is built; 0: the camps stand empty. Read once, on the server."), ECVF_Default);

namespace
{
	FIntPoint CellOf(float X, float Y) { return FIntPoint(FMath::FloorToInt(X / 1000.f), FMath::FloorToInt(Y / 1000.f)); }
	bool NearForestRoad(float X, float Y, float Pad)   // a quick box test before PathClearance (as ForestRuins.cpp)
	{
		for (const JeebTerrain::FPathDef& P : JeebTerrain::PathDefs())
			if (P.Bounds.ExpandBy(P.MaxHalf + Pad).IsInside(FVector2D(X, Y))) return true;
		return false;
	}
	constexpr float RoadClear = 2000.f;   // cm from any road's edge to a camp's fire
	constexpr float TownClear = 6000.f;   // ... from any town
	constexpr float WaterClear = 2500.f;  // ... from a river's water
	constexpr float CampsApart = 8000.f;
	constexpr float SpotR = 330.f;        // the foes stand this far from the fire
}

AFoeCamps::AFoeCamps()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
	bReplicates = false;   // (every machine builds the camps itself; the foes replicate on their own)
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Static);
}

void AFoeCamps::BeginPlay()
{
	Super::BeginPlay();
	if (JeebTownLab::Active()) { SetActorTickEnabled(false); return; }   // (the town lab map: no camps)
	Build();
	if (HasAuthority() && GetNetMode() != NM_Client)
	{
		if (CVarFoesBandits.GetValueOnGameThread() != 0) SpawnFoes();
		else UE_LOG(LogTemp, Log, TEXT("JEEBFOE jeeb.Foes.Bandits 0: the camps stand empty"));
	}
	bool bTest = false;
#if !UE_BUILD_SHIPPING
	bTest = FParse::Param(FCommandLine::Get(), TEXT("JeebFoeTest"));
#endif
	if (GetNetMode() == NM_Client && !bTest) SetActorTickEnabled(false);   // (players' machines: nothing to do after building)
}

void AFoeCamps::Build()
{
	const double T0 = FPlatformTime::Seconds();
	const bool bServer = IsNetMode(NM_DedicatedServer);
	// The forest's solids (trunks, logs, stumps, rocks: every machine has them) by 10 m cell, and the ruins
	TMap<FIntPoint, TArray<FVector2D>> Solid;
	for (TActorIterator<AOakForest> It(GetWorld()); It; ++It)
	{
		TArray<UInstancedStaticMeshComponent*> Sets;
		It->GetComponents<UInstancedStaticMeshComponent>(Sets);
		for (UInstancedStaticMeshComponent* C : Sets)
		{
			if (!C || !C->IsCollisionEnabled()) continue;
			for (int32 i = 0; i < C->GetInstanceCount(); ++i)
			{
				FTransform Xf; C->GetInstanceTransform(i, Xf, true);
				Solid.FindOrAdd(CellOf(Xf.GetLocation().X, Xf.GetLocation().Y)).Add(FVector2D(Xf.GetLocation()));
			}
		}
	}
	TArray<FVector2D> Ruins;
	for (TActorIterator<AForestRuins> It(GetWorld()); It; ++It) for (const FVector4& R : It->Spots()) Ruins.Add(FVector2D(R.X, R.Y));
	auto NearSolid = [&](const FVector2D& P, float R) -> bool
	{
		const FIntPoint C = CellOf(P.X, P.Y);
		const int32 N = FMath::CeilToInt(R / 1000.f);
		for (int32 dx = -N; dx <= N; ++dx) for (int32 dy = -N; dy <= N; ++dy)
			if (const TArray<FVector2D>* L = Solid.Find(C + FIntPoint(dx, dy)))
				for (const FVector2D& S : *L) if (FVector2D::DistSquared(S, P) < R * R) return true;
		for (const FVector2D& Ru : Ruins) if (FVector2D::DistSquared(Ru, P) < FMath::Square(R + 450.f)) return true;   // (a ruin reaches about 4.5 m from its middle)
		return false;
	};
	auto Spread = [](const FVector2D& P, float R) -> float
	{
		float Lo = JeebFoes::Ground(P.X, P.Y), Hi = Lo;
		for (int32 a = 0; a < 8; ++a)
		{
			const float H = JeebFoes::Ground(P.X + R * FMath::Cos(a * PI / 4.f), P.Y + R * FMath::Sin(a * PI / 4.f));
			Lo = FMath::Min(Lo, H); Hi = FMath::Max(Hi, H);
		}
		return Hi - Lo;
	};
	auto Wet = [](const FVector2D& P, float R) -> bool
	{
		if (JeebWorld::InWater(P.X, P.Y) || JeebFoes::DeepWater(P.X, P.Y)) return true;
		for (int32 a = 0; a < 8; ++a)
		{
			const FVector2D Q = P + FVector2D(FMath::Cos(a * PI / 4.f), FMath::Sin(a * PI / 4.f)) * R;
			if (JeebWorld::InWater(Q.X, Q.Y) || JeebFoes::DeepWater(Q.X, Q.Y)) return true;
		}
		return false;
	};
	auto Ok = [&](const FVector2D& P, float MaxSpread, float SolidR) -> bool
	{
		if (JeebFoes::InTown(P.X, P.Y, TownClear)) return false;
		float Half = 0.f;
		const float Rd = JeebWorld::RiverDistance(P.X, P.Y, Half);   // (call, then compare: ReviewLessons "Code pitfalls")
		if (Rd - Half < WaterClear) return false;
		if (Wet(P, 1200.f)) return false;
		if (NearForestRoad(P.X, P.Y, RoadClear) && JeebTerrain::PathClearance(P.X, P.Y) < RoadClear) return false;
		if (Spread(P, 600.f) > MaxSpread) return false;
		if (NearSolid(P, SolidR)) return false;
		for (const FCamp& C : Camps) if (FVector2D::Distance(C.At, P) < CampsApart) return false;
		return true;
	};

	uint32 Hash = 2166136261u;
	auto HashIn = [&](float V) { const int32 Qv = FMath::RoundToInt(V * 10.f); for (int32 b = 0; b < 4; ++b) { Hash ^= uint32((Qv >> (8 * b)) & 0xFF); Hash *= 16777619u; } };
	UStaticMesh* FlameMesh = bServer ? nullptr : LoadObject<UStaticMesh>(nullptr, TEXT("/Game/TownDecor/SM_TorchBlazingFlame.SM_TorchBlazingFlame"));
	const FVector2D Plaza(JeebOakHill::PosX, JeebOakHill::PosY);

	const TArray<JeebFoes::FCampDef>& Defs = JeebFoes::Camps();
	for (int32 c = 0; c < Defs.Num(); ++c)
	{
		const JeebFoes::FCampDef& D = Defs[c];
		// Where: the nearest good ground to its spot (a ruin camp: beside the ruin nearest its spot), on fixed rings, so every machine agrees
		FVector2D Base = D.Near;
		float R0 = 0.f, R1 = 6000.f;
		if (D.bRuin)
		{
			float Best = 1.0e18f;
			for (const FVector2D& Ru : Ruins) if (FVector2D::DistSquared(Ru, D.Near) < Best) { Best = FVector2D::DistSquared(Ru, D.Near); Base = Ru; }
			if (Best >= 1.0e18f) { UE_LOG(LogTemp, Warning, TEXT("JEEBFOE camp %s wants a ruin, but there are none: placed by its spot"), *D.Key.ToString()); }
			else { R0 = 1000.f; R1 = 2600.f; }
		}
		bool bFound = false;
		FVector2D At = Base;
		for (int32 Pass = 0; Pass < 2 && !bFound; ++Pass)
			for (float R = R0; R <= R1 && !bFound; R += 300.f)
			{
				const int32 N = R < 1.f ? 1 : FMath::Max(6, FMath::RoundToInt(2.f * PI * R / 400.f));
				for (int32 k = 0; k < N; ++k)
				{
					const float A = 2.f * PI * k / N + R * 0.00037f;
					const FVector2D P = Base + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R;
					if (Ok(P, Pass == 0 ? 140.f : 260.f, Pass == 0 ? 500.f : 320.f)) { At = P; bFound = true; break; }
				}
			}
		if (!bFound) { UE_LOG(LogTemp, Warning, TEXT("JEEBFOE camp %s: no good ground within %.0f m of its spot; left out"), *D.Key.ToString(), R1 / 100.f); continue; }
		FCamp Camp;
		Camp.Def = c;
		Camp.At = At;
		HashIn(At.X); HashIn(At.Y);
		// The foes' spots round the fire, each off any trunk
		for (int32 j = 0; j < D.Foes.Num(); ++j)
		{
			FVector2D S = At;
			for (int32 t = 0; t < 8; ++t)
			{
				const float A = 2.f * PI * j / FMath::Max(1, D.Foes.Num()) + 0.4f + t * 0.45f;
				S = At + FVector2D(FMath::Cos(A), FMath::Sin(A)) * SpotR;
				if (!NearSolid(S, 90.f)) break;
			}
			Camp.Spots.Add(S);
			HashIn(S.X); HashIn(S.Y);
		}
		// The props: the first in the middle (the campfire), the rest round it between the foes' spots
		int32 Placed = 0;
		const int32 Ring = FMath::Max(1, D.Props.Num() - 1);
		for (int32 p = 0; p < D.Props.Num(); ++p)
		{
			FString MeshPath; FVector Size; bool bSolid = false;
			if (!JeebFoes::FillKind(D.Props[p], MeshPath, Size, bSolid)) { UE_LOG(LogTemp, Warning, TEXT("JEEBFOE camp %s: no prop \"%s\" in FillKinds.csv"), *D.Key.ToString(), *D.Props[p].ToString()); continue; }
			const float Reach = 0.5f * FMath::Max(Size.X, Size.Y);
			FVector2D P = At;
			float Yaw = 0.f;
			bool bOk = (p == 0);
			if (p > 0)
			{
				const float Rr = (Size.X > 300.f || Size.Y > 300.f) ? 800.f : 580.f;
				for (int32 t = 0; t < 5 && !bOk; ++t)
				{
					const float Off = (t == 0) ? 0.f : ((t & 1) ? 1.f : -1.f) * 0.26f * ((t + 1) / 2);
					const float A = 2.f * PI * (p - 1) / Ring + 0.4f + PI / FMath::Max(1, D.Foes.Num()) + Off;
					P = At + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rr;
					Yaw = FMath::RadiansToDegrees(A) + 180.f;   // (its front to the fire)
					bOk = !NearSolid(P, Reach + 50.f) && !Wet(P, Reach) && Spread(P, Reach) < 80.f && !(NearForestRoad(P.X, P.Y, 400.f) && JeebTerrain::PathClearance(P.X, P.Y) < Reach + 200.f);
				}
			}
			if (!bOk) continue;
			// Sit it on the ground: the lowest of its corners and middle, a little sunk
			float Z = JeebFoes::Ground(P.X, P.Y);
			const FQuat Q(FVector::UpVector, FMath::DegreesToRadians(Yaw));
			for (int32 ix = -1; ix <= 1; ix += 2) for (int32 iy = -1; iy <= 1; iy += 2)
			{
				const FVector W = FVector(P, 0.f) + Q.RotateVector(FVector(Size.X * 0.45f * ix, Size.Y * 0.45f * iy, 0.f));
				Z = FMath::Min(Z, JeebFoes::Ground(W.X, W.Y));
			}
			Z -= 3.f;
			const FTransform Xf(Q, FVector(P.X, P.Y, Z));
			HashIn(P.X); HashIn(P.Y); HashIn(Z); HashIn(Yaw);
			++Placed;
			if (bSolid)   // (every machine: the server decides where players walk)
			{
				UBoxComponent* B = NewObject<UBoxComponent>(this);
				B->SetupAttachment(RootComponent);
				B->SetBoxExtent(Size * 0.5f);
				B->SetWorldTransform(FTransform(Q, FVector(P.X, P.Y, Z + Size.Z * 0.5f)));
				B->SetCollisionProfileName(TEXT("BlockAll"));
				B->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
				B->SetHiddenInGame(true);
				B->SetCanEverAffectNavigation(false);
				B->RegisterComponent();
				Solids.Add(B);
			}
			if (bServer) continue;
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
			if (!Mesh) { UE_LOG(LogTemp, Warning, TEXT("JEEBFOE camp %s: %s didn't load"), *D.Key.ToString(), *MeshPath); continue; }
			UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(this);
			M->SetupAttachment(RootComponent);
			M->SetStaticMesh(Mesh);
			M->SetWorldTransform(Xf);
			M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			M->SetCanEverAffectNavigation(false);
			M->SetCachedMaxDrawDistance(15000.f);
			M->RegisterComponent();
			JeebLamp::ApplyToMesh(M, this);
			Drawn.Add(M);
			if (p == 0 && FlameMesh)   // the campfire: a flame on the wood, and a warm light round it (no shadows; out of sight past 80 m)
			{
				UStaticMeshComponent* F = NewObject<UStaticMeshComponent>(this);
				F->SetupAttachment(RootComponent);
				F->SetStaticMesh(FlameMesh);
				// (review 2026-11-18: the flame floated 1.5-2 m over the logs) sit the flame's own bottom a little into the top of the
				// wood, from both models' real bounds, and never taller than about 1.1 m
				const FBox LogBox = Mesh->GetBoundingBox(), FlameBox = FlameMesh->GetBoundingBox();
				const float FlameS = FMath::Min(2.4f, 110.f / FMath::Max(1.f, FlameBox.GetSize().Z));
				const float WoodTop = Z + LogBox.Max.Z, WoodH = FMath::Max(1.f, LogBox.GetSize().Z);
				const FVector FlameOff = FVector(FlameBox.GetCenter().X, FlameBox.GetCenter().Y, FlameBox.Min.Z) * FlameS;
				const FVector LogMid = Xf.TransformPosition(FVector(LogBox.GetCenter().X, LogBox.GetCenter().Y, 0.f));
				const FVector FlameTop(LogMid.X, LogMid.Y, WoodTop - 0.3f * WoodH);   // (where the flame's base sits)
				F->SetWorldTransform(FTransform(FQuat::Identity, FlameTop - FlameOff, FVector(FlameS)));
				F->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				F->SetCastShadow(false);
				F->SetCachedMaxDrawDistance(15000.f);
				F->RegisterComponent();
				JeebLamp::ApplyToMesh(F, this);
				Drawn.Add(F);
				UPointLightComponent* L = NewObject<UPointLightComponent>(this);
				L->SetupAttachment(RootComponent);
				L->SetWorldLocation(FlameTop + FVector(0.f, 0.f, 45.f));   // (just over the wood, in the flame)
				UE_LOG(LogTemp, Log, TEXT("JEEBFOE camp %s fire: ground %.0f, wood top %.0f, flame base %.0f, flame scale %.2f (height %.0f cm)"),
					*D.Key.ToString(), Z, WoodTop, FlameTop.Z, FlameS, FlameBox.GetSize().Z * FlameS);
				L->SetIntensity(2600.f);
				L->SetAttenuationRadius(800.f);
				L->SetCastShadows(false);
				L->MaxDrawDistance = 8000.f;
				L->MaxDistanceFadeRange = 2000.f;
				L->RegisterComponent();
				JeebLamp::TrackLight(L);
			}
		}
		UE_LOG(LogTemp, Log, TEXT("JEEBFOE camp %s \"%s\" at %.0f %.0f (%.0f m north, %.0f m east of Oak Hill's plaza; %.0f m from its spot%s): %d foes, %d of %d props; road %.0f m, river %.0f m, ground spread %.0f cm"),
			*D.Key.ToString(), *D.Name, At.X, At.Y, (At.X - Plaza.X) / 100.f, (At.Y - Plaza.Y) / 100.f, FVector2D::Distance(At, Base) / 100.f, D.bRuin ? TEXT(", by the ruin") : TEXT(""),
			D.Foes.Num(), Placed, D.Props.Num(), JeebTerrain::PathClearance(At.X, At.Y) / 100.f,
			[&]() { float H = 0.f; const float Rd = JeebWorld::RiverDistance(At.X, At.Y, H); return (Rd - H) / 100.f; }(), Spread(At, 600.f));
		Camps.Add(Camp);
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBFOE camps: %d built in %.1f ms, hash=%08x%s"), Camps.Num(), (FPlatformTime::Seconds() - T0) * 1000.0, Hash, bServer ? TEXT(" (server: solids only)") : TEXT(""));
}

void AFoeCamps::SpawnFoes()
{
	const TArray<JeebFoes::FCampDef>& Defs = JeebFoes::Camps();
	int32 Total = 0;
	float TestRespawn = 0.f;
#if !UE_BUILD_SHIPPING
	FParse::Value(FCommandLine::Get(), TEXT("FoeRespawn="), TestRespawn);   // (tests: a quicker respawn)
#endif
	for (int32 c = 0; c < Camps.Num(); ++c)
	{
		FCamp& C = Camps[c];
		const JeebFoes::FCampDef& D = Defs[C.Def];
		for (int32 j = 0; j < D.Foes.Num() && j < C.Spots.Num(); ++j)
		{
			const FVector2D S = C.Spots[j];
			const FTransform Xf(FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan2(C.At.Y - S.Y, C.At.X - S.X)), 0.f), FVector(S.X, S.Y, JeebFoes::Ground(S.X, S.Y) + 98.f));
			AFoeKnight* F = GetWorld()->SpawnActorDeferred<AFoeKnight>(AFoeKnight::StaticClass(), Xf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!F) continue;
			F->SetupFoe(D.Foes[j], c, S, C.At, 7001 + C.Def * 37 + j * 11);
			if (TestRespawn > 0.f) F->RespawnSecs = TestRespawn;
			F->FinishSpawning(Xf);
			C.Foes.Add(F);
			++Total;
		}
		C.bAwake = true;   // (TickSleep puts them to sleep straight away if nobody is near)
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBFOE %d foes placed at %d camps"), Total, Camps.Num());
}

void AFoeCamps::TickSleep()
{
	TArray<FVector2D> Players;
	for (TActorIterator<AKnightCharacter> It(GetWorld()); It; ++It) if (It->GetHealth() > 0.f) Players.Add(FVector2D(It->GetActorLocation()));
	for (FCamp& C : Camps)
	{
		float Near = 1.0e9f;
		for (const FVector2D& P : Players) Near = FMath::Min(Near, FVector2D::Distance(P, C.At));
		const bool bAwake = C.bAwake ? Near < SleepRange + 1500.f : Near < SleepRange;   // (a little give, so a player at the edge doesn't flicker it)
		if (bAwake != C.bAwake)
		{
			C.bAwake = bAwake;
			UE_LOG(LogTemp, Log, TEXT("JEEBFOE camp %s %s (nearest player %.0f m)"), *JeebFoes::Camps()[C.Def].Key.ToString(), bAwake ? TEXT("wakes") : TEXT("sleeps"), Near / 100.f);
		}
		for (const TWeakObjectPtr<AFoeKnight>& F : C.Foes) if (F.IsValid()) F->SetSleeping(!bAwake);
	}
}

bool AFoeCamps::NearestCamp(const UWorld* World, const FString& Target, const FVector2D& From, FVector2D& OutAt, FString& OutName)
{
	if (!World) return false;
	float Best = 1.0e18f;
	for (TActorIterator<AFoeCamps> It(const_cast<UWorld*>(World)); It; ++It)
		for (const FCamp& C : It->Camps)
		{
			const JeebFoes::FCampDef& D = JeebFoes::Camps()[C.Def];
			if (!D.Foes.ContainsByPredicate([&](int32 K) { return JeebFoes::Matches(K, Target); })) continue;
			const float Dd = FVector2D::DistSquared(C.At, From);
			if (Dd < Best) { Best = Dd; OutAt = C.At; OutName = D.Name; }
		}
	return Best < 1.0e18f;
}

#if !UE_BUILD_SHIPPING
// ===================================================================================================
// The online test (-JeebFoeTest on a world server and one or two players' machines; test builds only, never Shipping).
//  - The server drives the first player through it: Captain Corin's first quest taken; placed in front of a cutpurse (does he notice, chase,
//    cut?); the cutpurse beaten with the player's own damage call (loot, XP, the quest's count); three more; the quest handed in, the second
//    taken, Red Hal beaten at his ruin; a bruiser drawn out past his leash (does he go home and heal?); the first cutpurse back after his
//    respawn; everyone sent home to Oak Hill (do the camps sleep?). Every step logs JEEBFOETEST, PASS or FAIL.
//  - A second player is stood 28 m from the fight, facing it, so its machine shows what others see.
//  - Every player's machine logs what reaches it (JEEBFOECLIENT: the bandits it has, their act, down and hidden, its quest marker) and takes
//    a few pictures (Saved/Shots/foe_*.png).
// ===================================================================================================
struct FFoeTestDriver
{
	static void Log(const FString& S) { UE_LOG(LogTemp, Log, TEXT("JEEBFOETEST %s"), *S); }

	static int32 CampByKey(const AFoeCamps* C, const TCHAR* Key)
	{
		for (int32 i = 0; i < C->Camps.Num(); ++i) if (JeebFoes::Camps()[C->Camps[i].Def].Key == FName(Key)) return i;
		return INDEX_NONE;
	}
	static AFoeKnight* FoeIn(const AFoeCamps* C, int32 Camp, const TCHAR* Kind)
	{
		if (!C->Camps.IsValidIndex(Camp)) return nullptr;
		for (const TWeakObjectPtr<AFoeKnight>& F : C->Camps[Camp].Foes)
			if (F.IsValid() && !F->IsDown() && JeebFoes::Matches(F->FoeKind(), Kind)) return F.Get();
		return nullptr;
	}
	static void Place(AKnightCharacter* K, const FVector2D& P, const FVector2D& LookAt)
	{
		if (!K) return;
		const FRotator R(0.f, FMath::RadiansToDegrees(FMath::Atan2(LookAt.Y - P.Y, LookAt.X - P.X)), 0.f);
		K->TeleportTo(FVector(P.X, P.Y, JeebFoes::Ground(P.X, P.Y) + 120.f), R, false, true);
		if (APlayerController* PC = Cast<APlayerController>(K->GetController())) PC->ClientSetRotation(FRotator(-12.f, R.Yaw, 0.f));
	}
	static void MoveBeside(AKnightCharacter* K, FName Person)
	{
		const ANpc* N = AQuestPeople::Find(K->GetWorld(), Person);
		if (!N) { Log(FString::Printf(TEXT("FAIL: no %s"), *Person.ToString())); return; }
		const FVector2D At = FVector2D(N->GetActorLocation()) + (FVector2D(JeebOakHill::PosX, JeebOakHill::PosY) - FVector2D(N->GetActorLocation())).GetSafeNormal() * 150.f;
		K->TeleportTo(FVector(At.X, At.Y, N->GetActorLocation().Z + 20.f), K->GetActorRotation(), false, true);
	}
	static FString Quests(const AKnightCharacter* K)
	{
		const UQuestComponent* Q = K ? K->FindComponentByClass<UQuestComponent>() : nullptr;
		if (!Q) return TEXT("(no quests)");
		FString S;
		for (const FQuestProgress& P : Q->Quests) S += FString::Printf(TEXT(" %s(step %d, count %d%s)"), *P.Key.ToString(), P.Step, P.Count, P.bDone ? TEXT(", done") : TEXT(""));
		FVector2D At; FString What;
		if (Q->Objective(At, What)) S += FString::Printf(TEXT(" | marker: %s at %.0f %.0f, %.0f m away"), *What, At.X, At.Y, FVector2D::Distance(At, FVector2D(K->GetActorLocation())) / 100.f);
		return S;
	}
	static FString Skills(const AKnightCharacter* K)
	{
		const USkillComponent* Sk = K ? K->FindComponentByClass<USkillComponent>() : nullptr;
		return Sk ? FString::Printf(TEXT("attack %d xp, ingots %d, health %.0f"), Sk->GetXPTotal(ESkillType::Attack), Sk->GetIngots(), K->GetHealth()) : FString();
	}
	static int32 DropsNear(const UWorld* W, const FVector& P, float R, FString* OutWhat = nullptr)
	{
		int32 N = 0;
		for (TActorIterator<ALootDrop> It(const_cast<UWorld*>(W)); It; ++It)
			if (FVector::Dist2D(It->GetActorLocation(), P) < R)
			{
				++N;
				if (OutWhat) *OutWhat += FString::Printf(TEXT(" [%s]"), *It->Describe());
			}
		return N;
	}
	static int32 Asleep(const AFoeCamps* C, int32& OutAll)
	{
		int32 N = 0; OutAll = 0;
		for (const AFoeCamps::FCamp& Cp : C->Camps) for (const TWeakObjectPtr<AFoeKnight>& F : Cp.Foes) if (F.IsValid()) { ++OutAll; N += F->IsSleeping() ? 1 : 0; }
		return N;
	}

	// The server's run
	static void Server(AFoeCamps* C, float Dt)
	{
		static TWeakObjectPtr<AKnightCharacter> P1, P2;
		static TWeakObjectPtr<AFoeKnight> B0, Cur;
		static float StageT = 0.f, NoticedAt = -1.f, Health0 = 0.f;
		static int32 XP0 = 0, Beaten = 0, Fails = 0, Passes = 0;
		static FVector BeatAt = FVector::ZeroVector;
		C->TestClock += Dt;
		StageT += Dt;
		if (C->TestClock < C->TestNext) return;
		UWorld* W = C->GetWorld();
		auto Next = [&](float Wait) { ++C->TestStage; StageT = 0.f; C->TestNext = C->TestClock + Wait; };
		auto Check = [&](bool bOk, const FString& What) { Log(FString::Printf(TEXT("%s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What)); (bOk ? Passes : Fails)++; };
		AKnightCharacter* K = P1.Get();
		UQuestComponent* Q = K ? K->FindComponentByClass<UQuestComponent>() : nullptr;
		auto Hit = [&](AFoeKnight* F, int32 Dmg) { if (K && F) if (UHealthComponent* H = F->FindComponentByClass<UHealthComponent>()) K->DealDamage(H, Dmg, false, ESkillType::Attack); };
		const int32 South = CampByKey(C, TEXT("South")), North = CampByKey(C, TEXT("North")), Hold = CampByKey(C, TEXT("Ruin"));
		const int32 Want = FMath::Max(1, [](){ int32 N = 1; FParse::Value(FCommandLine::Get(), TEXT("FoeTestPlayers="), N); return N; }());
		switch (C->TestStage)
		{
		case 0:
		{
			TArray<AKnightCharacter*> Ks;
			for (TActorIterator<AKnightCharacter> It(W); It; ++It) if (It->GetController()) Ks.Add(*It);
			if (Ks.Num() < Want && C->TestClock < 120.f) { C->TestNext = C->TestClock + 1.f; return; }
			if (Ks.Num() == 0) { C->TestNext = C->TestClock + 1.f; return; }
			if (StageT < 8.f) return;   // (let the players' machines settle)
			P1 = Ks[0]; P2 = Ks.Num() > 1 ? Ks[1] : nullptr;
			int32 All = 0; const int32 Sl = Asleep(C, All);
			Log(FString::Printf(TEXT("start: %d players (%s%s%s), %d camps, %d foes, %d asleep; P1 at %s"), Ks.Num(), *P1->GetName(), P2.IsValid() ? TEXT(", ") : TEXT(""), P2.IsValid() ? *P2->GetName() : TEXT(""),
				C->Camps.Num(), All, Sl, *P1->GetActorLocation().ToString()));
			Check(All > 0 && Sl == All, FString::Printf(TEXT("with every player in town, %d of %d foes sleep"), Sl, All));
			Check(South != INDEX_NONE && North != INDEX_NONE && Hold != INDEX_NONE, TEXT("the South, North and Ruin camps exist"));
			if (South == INDEX_NONE || North == INDEX_NONE || Hold == INDEX_NONE) { C->TestStage = 900; return; }
			Next(0.5f); return;
		}
		case 1:
			if (!Q) { Log(TEXT("FAIL: P1 has no quests")); C->TestStage = 900; return; }
			MoveBeside(K, TEXT("Corin"));
			Q->DoQuest(UQuestComponent::EOp::Accept, TEXT("Thieves"));
			Check(Q->Find(TEXT("Thieves")) != nullptr, FString::Printf(TEXT("Captain Corin's \"Thieves\" accepted:%s"), *Quests(K)));
			Next(1.f); return;
		case 2:
		{
			Log(FString::Printf(TEXT("quest marker from Corin's side:%s"), *Quests(K)));
			B0 = FoeIn(C, South, TEXT("Cutpurse"));
			if (!B0.IsValid()) { Check(false, TEXT("a cutpurse at the South camp")); C->TestStage = 900; return; }
			const FVector2D F(B0->GetActorLocation()), Fw(B0->GetActorForwardVector().GetSafeNormal2D());
			Place(K, F + Fw * 1100.f, F);
			if (P2.IsValid()) { const FVector2D Cc = C->Camps[South].At; Place(P2.Get(), Cc - Fw * 2800.f, Cc); }
			Health0 = K->GetHealth(); XP0 = K->FindComponentByClass<USkillComponent>()->GetXPTotal(ESkillType::Attack); NoticedAt = -1.f;
			Log(FString::Printf(TEXT("P1 stood 11 m in front of %s (%s) at the South camp; %s"), *B0->FoeName(), *B0->GetName(), *Skills(K)));
			Next(0.25f); return;
		}
		case 3:
			if (!B0.IsValid()) { C->TestStage = 900; return; }
			if (NoticedAt < 0.f && B0->MindNow() == 1) { NoticedAt = StageT; Check(B0->TargetNow() == K, FString::Printf(TEXT("the cutpurse noticed P1 after %.2f s, %.1f m away"), StageT, FVector::Dist2D(B0->GetActorLocation(), K->GetActorLocation()) / 100.f)); }
			if (NoticedAt < 0.f && StageT > 8.f) { Check(false, TEXT("the cutpurse noticed P1 within 8 s")); NoticedAt = StageT; }
			if (NoticedAt >= 0.f && StageT > NoticedAt + 9.f)
			{
				Check(K->GetHealth() < Health0, FString::Printf(TEXT("he chased and cut P1: health %.0f -> %.0f; now %.1f m apart"), Health0, K->GetHealth(), FVector::Dist2D(B0->GetActorLocation(), K->GetActorLocation()) / 100.f));
				Cur = B0; Beaten = 0;
				Next(0.1f); return;
			}
			C->TestNext = C->TestClock + 0.25f; return;
		case 4:   // P1 beats Cur with his own damage call, a blow every 0.4 s
		{
			AFoeKnight* F = Cur.Get();
			if (!F) { C->TestStage = 900; return; }
			if (!F->IsDown())
			{
				if (FVector::Dist2D(F->GetActorLocation(), K->GetActorLocation()) > 400.f) Place(K, FVector2D(F->GetActorLocation()) + FVector2D(F->GetActorForwardVector()) * 200.f, FVector2D(F->GetActorLocation()));
				Hit(F, 14); C->TestNext = C->TestClock + 0.4f; return;
			}
			BeatAt = F->GetActorLocation();
			++Beaten;
			Next(1.0f); return;
		}
		case 5:
		{
			FString What;
			const int32 N = DropsNear(W, BeatAt, 400.f, &What);
			const int32 XP = K->FindComponentByClass<USkillComponent>()->GetXPTotal(ESkillType::Attack);
			Check(N > 0, FString::Printf(TEXT("bandit %d beaten: %d drops where he fell:%s"), Beaten, N, *What));
			Check(XP > XP0, FString::Printf(TEXT("Attack XP %d -> %d; quests:%s"), XP0, XP, *Quests(K)));
			XP0 = XP;
			if (Beaten < 4)
			{
				Cur = FoeIn(C, South, TEXT("Cutpurse"));
				if (!Cur.IsValid()) Cur = FoeIn(C, North, TEXT("Bandit"));
				if (!Cur.IsValid()) { Check(false, TEXT("another bandit to beat")); C->TestStage = 900; return; }
				Place(K, FVector2D(Cur->GetActorLocation()) + FVector2D(Cur->GetActorForwardVector()) * 250.f, FVector2D(Cur->GetActorLocation()));
				C->TestStage = 4; C->TestNext = C->TestClock + 0.5f; return;
			}
			const FQuestProgress* T = Q->Find(TEXT("Thieves"));
			Check(T && T->Step == 1, FString::Printf(TEXT("four bandits beaten: Thieves is on its hand-in step:%s"), *Quests(K)));
			Next(0.5f); return;
		}
		case 6:
			MoveBeside(K, TEXT("Corin"));
			Q->DoQuest(UQuestComponent::EOp::Talk, TEXT("Corin"));
			Check(Q->Find(TEXT("Thieves")) && Q->Find(TEXT("Thieves"))->bDone, FString::Printf(TEXT("Thieves handed in to Corin: %s"), *Skills(K)));
			Q->DoQuest(UQuestComponent::EOp::Accept, TEXT("RedHal"));
			Check(Q->Find(TEXT("RedHal")) != nullptr, FString::Printf(TEXT("\"RedHal\" accepted:%s"), *Quests(K)));
			Next(1.f); return;
		case 7:
		{
			Cur = FoeIn(C, Hold, TEXT("RedHal"));
			if (!Cur.IsValid()) { Check(false, TEXT("Red Hal at his camp")); C->TestStage = 900; return; }
			Log(FString::Printf(TEXT("marker with RedHal taken:%s; Red Hal stands at %s"), *Quests(K), *Cur->GetActorLocation().ToString()));
			Place(K, FVector2D(Cur->GetActorLocation()) + FVector2D(Cur->GetActorForwardVector()) * 300.f, FVector2D(Cur->GetActorLocation()));
			if (P2.IsValid()) Place(P2.Get(), C->Camps[Hold].At + (C->Camps[Hold].At - FVector2D(Cur->GetActorLocation())).GetSafeNormal() * 2800.f, C->Camps[Hold].At);
			Next(1.5f); return;
		}
		case 8:
		{
			AFoeKnight* F = Cur.Get();
			if (!F) { C->TestStage = 900; return; }
			if (!F->IsDown()) { if (FVector::Dist2D(F->GetActorLocation(), K->GetActorLocation()) > 400.f) Place(K, FVector2D(F->GetActorLocation()) + FVector2D(F->GetActorForwardVector()) * 200.f, FVector2D(F->GetActorLocation())); Hit(F, 40); C->TestNext = C->TestClock + 0.3f; return; }
			FString What; const int32 N = DropsNear(W, F->GetActorLocation(), 400.f, &What);
			Check(true, FString::Printf(TEXT("Red Hal beaten; %d drops:%s; quests:%s"), N, *What, *Quests(K)));
			Next(1.f); return;
		}
		case 9:
			MoveBeside(K, TEXT("Corin"));
			Q->DoQuest(UQuestComponent::EOp::Talk, TEXT("Corin"));
			Check(Q->Find(TEXT("RedHal")) && Q->Find(TEXT("RedHal"))->bDone, FString::Printf(TEXT("RedHal handed in:%s; %s"), *Quests(K), *Skills(K)));
			Next(1.f); return;
		case 10:   // the leash: a bruiser drawn out, then the player far past it
		{
			Cur = FoeIn(C, North, TEXT("Bruiser"));
			if (!Cur.IsValid()) { Check(false, TEXT("a bruiser at the North camp")); C->TestStage = 12; return; }
			Place(K, FVector2D(Cur->GetActorLocation()) + FVector2D(Cur->GetActorForwardVector()) * 900.f, FVector2D(Cur->GetActorLocation()));
			if (P2.IsValid()) Place(P2.Get(), C->Camps[North].At - FVector2D(Cur->GetActorForwardVector()) * 2800.f, C->Camps[North].At);
			Next(0.25f); return;
		}
		case 11:
		{
			AFoeKnight* F = Cur.Get();
			if (!F) { C->TestStage = 12; return; }
			const FVector2D Camp = C->Camps[North].At;
			const float FromCamp = FVector2D::Distance(FVector2D(F->GetActorLocation()), Camp) / 100.f;
			static int32 Phase = 0;
			if (Phase == 0)
			{
				if (F->MindNow() == 1)
				{
					const FVector2D Away = (Camp - FVector2D(JeebOakHill::PosX, JeebOakHill::PosY)).GetSafeNormal();
					Place(K, Camp + Away * 6000.f, Camp);   // 60 m out, on the far side from the towns
					Log(FString::Printf(TEXT("the bruiser fights P1; P1 runs 60 m from his camp (%s)"), *Skills(K)));
					Phase = 1; StageT = 0.f;
				}
				else if (StageT > 8.f) { Check(false, TEXT("the bruiser noticed P1 9 m in front of him")); Phase = 3; }
			}
			else if (Phase == 1)
			{
				if (FMath::Fmod(StageT, 2.f) < 0.26f) Log(FString::Printf(TEXT("  bruiser: mind %d, %.1f m from camp, health %d"), F->MindNow(), FromCamp, F->FindComponentByClass<UHealthComponent>()->Cur));
				if (F->MindNow() == 2) { Check(FromCamp <= JeebFoes::Foes()[F->FoeKind()].Leash / 100.f + 4.f, FString::Printf(TEXT("he gave up %.1f m from camp (leash %.0f m)"), FromCamp, JeebFoes::Foes()[F->FoeKind()].Leash / 100.f)); Phase = 2; StageT = 0.f; }
				else if (StageT > 25.f) { Check(false, TEXT("the bruiser gave up the chase within 25 s")); Phase = 3; }
			}
			else if (Phase == 2)
			{
				if (F->MindNow() == 0) { Check(FromCamp < 5.f, FString::Printf(TEXT("he walked home (%.1f m from camp) in %.1f s and healed: %d/%d"), FromCamp, StageT, F->FindComponentByClass<UHealthComponent>()->Cur, F->FindComponentByClass<UHealthComponent>()->Max)); Phase = 3; }
				else if (StageT > 30.f) { Check(false, FString::Printf(TEXT("the bruiser home within 30 s (still %.1f m out, mind %d)"), FromCamp, F->MindNow())); Phase = 3; }
			}
			if (Phase == 3) { Next(0.5f); return; }
			C->TestNext = C->TestClock + 0.25f; return;
		}
		case 12:   // the first cutpurse stands again (the test's -FoeRespawn= or Foes.csv)
		{
			AFoeKnight* F = B0.Get();
			if (F && !F->IsDown() && !F->IsHidden()) { Check(FVector2D::Distance(FVector2D(F->GetActorLocation()), C->Camps[South].At) < 900.f, FString::Printf(TEXT("the first cutpurse stands at his camp again (%.1f m from the fire)"), FVector2D::Distance(FVector2D(F->GetActorLocation()), C->Camps[South].At) / 100.f)); Next(0.5f); return; }
			if (StageT > 70.f) { Check(false, TEXT("the first cutpurse back within 70 s")); Next(0.5f); return; }
			C->TestNext = C->TestClock + 1.f; return;
		}
		case 13:
			Place(K, FVector2D(JeebOakHill::SpawnX, JeebOakHill::SpawnY), FVector2D(JeebOakHill::PosX, JeebOakHill::PosY));
			if (P2.IsValid()) Place(P2.Get(), FVector2D(JeebOakHill::SpawnX + 300.f, JeebOakHill::SpawnY), FVector2D(JeebOakHill::PosX, JeebOakHill::PosY));
			Next(4.f); return;
		case 14:
		{
			int32 All = 0; const int32 Sl = Asleep(C, All);
			Check(Sl == All, FString::Printf(TEXT("everyone back in Oak Hill: %d of %d foes sleep"), Sl, All));
			for (TActorIterator<AFoeKnight> It(W); It; ++It)
				if (!It->IsBandit() && !It->bStill) Check(FVector2D::Distance(FVector2D(It->GetActorLocation()), JeebTerrain::PenCenter()) < 1200.f, FString::Printf(TEXT("the pen knight is still the pen knight, in his pen (%s)"), *It->GetActorLocation().ToString()));
			if (APlayerController* PC = Cast<APlayerController>(K->GetController()))
				if (AKnightGameMode* GM = W->GetAuthGameMode<AKnightGameMode>()) if (AKnightPlayerController* KPC = Cast<AKnightPlayerController>(PC)) GM->SaveCharacter(KPC);
			Log(FString::Printf(TEXT("DONE: %d passed, %d failed;%s; %s"), Passes, Fails, *Quests(K), *Skills(K)));
			C->TestStage = 1000; return;
		}
		case 900:
			Log(FString::Printf(TEXT("STOPPED early: %d passed, %d failed"), Passes, Fails));
			C->TestStage = 1000; return;
		default: return;
		}
	}

	// A player's machine: what reaches it
	static void Client(AFoeCamps* C, float Dt)
	{
		static float Clock = 0.f, NextLog = 0.f;
		static TMap<TWeakObjectPtr<AFoeKnight>, uint8> Seen;   // (down / hidden, last logged)
		static bool bFightShot = false, bCampShot = false, bHalShot = false;
		static float QuitAt = -1.f;
		Clock += Dt;
		APlayerController* PC = C->GetWorld()->GetFirstPlayerController();   // (test pictures and logs only: this machine's own view)
		AKnightCharacter* K = PC ? Cast<AKnightCharacter>(PC->GetPawn()) : nullptr;
		if (QuitAt < 0.f) { float S = 420.f; FParse::Value(FCommandLine::Get(), TEXT("FoeTestClientTime="), S); QuitAt = S; }
		if (Clock > QuitAt) { if (PC) PC->ConsoleCommand(TEXT("quit")); QuitAt = 1.0e9f; return; }
		if (!K) return;
		auto Shot = [](const TCHAR* Name)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / FString::Printf(TEXT("foe_%s.png"), Name), false, false);
			UE_LOG(LogTemp, Log, TEXT("JEEBFOECLIENT picture foe_%s"), Name);
		};
		const FVector Me = K->GetActorLocation();
		int32 N = 0;
		FString Near;
		for (TActorIterator<AFoeKnight> It(C->GetWorld()); It; ++It)
		{
			AFoeKnight* F = *It;
			if (!F->IsBandit()) continue;
			++N;
			const uint8 State = (F->bRepDown ? 1 : 0) | (F->IsHidden() ? 2 : 0);
			uint8& Was = Seen.FindOrAdd(F, 0);
			if (State != Was) { UE_LOG(LogTemp, Log, TEXT("JEEBFOECLIENT %s (%s) %s%s at %s"), *F->FoeName(), *F->GetName(), (State & 1) ? TEXT("down") : TEXT("standing"), (State & 2) ? TEXT(", hidden") : TEXT(""), *F->GetActorLocation().ToString()); Was = State; }
			const float D = FVector::Dist2D(F->GetActorLocation(), Me);
			if (D < 4000.f) Near += FString::Printf(TEXT(" [%s %.1f m act %d%s hp %d/%d]"), *F->FoeName(), D / 100.f, F->RepAct, F->bRepBlock ? TEXT(" block") : TEXT(""), F->Hp ? F->Hp->Cur : 0, F->Hp ? F->Hp->Max : 0);
			if (!bFightShot && D < 400.f && F->RepAct == 1 && !F->bRepDown) { bFightShot = true; Shot(TEXT("fight")); }
			if (!bHalShot && D < 600.f && F->FoeKind() == JeebFoes::FindFoe(TEXT("RedHal")) && !F->bRepDown) { bHalShot = true; Shot(TEXT("hal")); }
		}
		for (const AFoeCamps::FCamp& Cp : C->Camps)
			if (!bCampShot && FVector2D::Distance(Cp.At, FVector2D(Me)) < 3200.f && FVector2D::Distance(Cp.At, FVector2D(Me)) > 2000.f) { bCampShot = true; Shot(TEXT("camp")); }
		if (Clock < NextLog) return;
		NextLog = Clock + 2.f;
		FString Quest;
		if (const UQuestComponent* Q = K->FindComponentByClass<UQuestComponent>())
		{
			FVector2D At; FString What;
			for (const FQuestProgress& P : Q->Quests) Quest += FString::Printf(TEXT(" %s(%d,%d%s)"), *P.Key.ToString(), P.Step, P.Count, P.bDone ? TEXT(",done") : TEXT(""));
			if (Q->Objective(At, What)) Quest += FString::Printf(TEXT(" marker %s %.0f m"), *What, FVector2D::Distance(At, FVector2D(Me)) / 100.f);
		}
		UE_LOG(LogTemp, Log, TEXT("JEEBFOECLIENT t=%.0f %s at %.0f %.0f health %.0f: %d bandits sent here;%s | quests:%s"), Clock, *K->GetName(), Me.X, Me.Y, K->GetHealth(), N, *Near, *Quest);
	}
};
#endif

void AFoeCamps::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && GetNetMode() != NM_Client)
	{
		SleepClock += DeltaSeconds;
		if (SleepClock >= 0.5f) { SleepClock = 0.f; TickSleep(); }
	}
#if !UE_BUILD_SHIPPING
	static const bool bTest = FParse::Param(FCommandLine::Get(), TEXT("JeebFoeTest"));
	if (!bTest) return;
	if (GetNetMode() == NM_DedicatedServer) FFoeTestDriver::Server(this, DeltaSeconds);
	else if (GetNetMode() == NM_Client) FFoeTestDriver::Client(this, DeltaSeconds);
#endif
}

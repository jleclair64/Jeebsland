#include "TownLabDirector.h"
#include "TownLab.h"
#include "TownLabTown.h"
#include "OakHill.h"
#include "OakHillTown.h"
#include "OakHillLife.h"
#include "Terrain.h"
#include "DayNight.h"
#include "KnightGameState.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/HUD.h"
#include "HAL/IConsoleManager.h"
#include "RenderTimer.h"
#include "RHIStats.h"
#include "RHI.h"
#include "WorldTerrain.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UnrealClient.h"

namespace JeebTownLabVars
{
	static TAutoConsoleVariable<int32> NewLook(TEXT("jeeb.TownLab.NewLook"), 1, TEXT("TownLab: 1 the new light and atmosphere (golden haze, aerial perspective, light shafts, lifted shadows), 0 the game's own"));
	static TAutoConsoleVariable<int32> CheapShadows(TEXT("jeeb.TownLab.CheapShadows"), -1, TEXT("TownLab: 1 shorter sun shadows and no shadows from small things far off, 0 the game's, -1 follow NewLook"));
	static TAutoConsoleVariable<int32> CheapClouds(TEXT("jeeb.TownLab.CheapClouds"), -1, TEXT("TownLab: 1 fewer cloud samples and a shorter trace, 0 the game's, -1 follow NewLook"));
	static TAutoConsoleVariable<int32> ShowOld(TEXT("jeeb.TownLab.ShowOld"), 1, TEXT("TownLab: 0 hides the real Oak Hill's buildings and ground (its trees and grass stay)"));
	static TAutoConsoleVariable<int32> ShowNew(TEXT("jeeb.TownLab.ShowNew"), 1, TEXT("TownLab: 0 hides the rebuilt town's buildings and ground (its trees and grass stay)"));
}

ATownLab::ATownLab()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;   // (after the day's own look has been set this frame)
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

FVector ATownLab::SpawnPoint(float& OutYaw)
{
	const float X = JeebOakHill::PosX + 17000.f, Y = (JeebOakHill::PosY + JeebTownLab::PlazaY) * 0.5f;
	OutYaw = 180.f;   // (south: the real Oak Hill to the right, the rebuilt one to the left, both about 40 degrees off)
	return FVector(X, Y, JeebTerrain::HeightAt(X, Y) + 130.f);
}

void ATownLab::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(FVector::ZeroVector);
	if (IsNetMode(NM_DedicatedServer)) { SetActorTickEnabled(false); return; }
	// the shared surroundings: the same boulders round the real town as round the rebuilt one (ATownLabTown places its own)
	// (in Field, where the director comes only for -LabTour, the real town is the rebuilt one and places its own)
	if (JeebOakHill::Enabled() && JeebTownLab::Active()) ATownLabTown::PlaceBoulders(this, JeebOakHill::PosX, JeebOakHill::PosY);
	FString Tour;
	if (FParse::Value(FCommandLine::Get(), TEXT("LabTour="), Tour) || FParse::Param(FCommandLine::Get(), TEXT("LabTour")))
	{
		bTour = true;
		bTourShots = !Tour.Equals(TEXT("stats"), ESearchCase::IgnoreCase);
		BuildTour();
	}
}

void ATownLab::SetVar(const TCHAR* Name, const FString& Value)
{
	if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		if (!Saved.Contains(Name)) Saved.Add(Name, V->GetString());
		V->Set(*Value, ECVF_SetByConsole);
	}
}
void ATownLab::RestoreVar(const TCHAR* Name)
{
	if (const FString* Was = Saved.Find(Name))
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name)) V->Set(**Was, ECVF_SetByConsole);
}

// The new light (the style sheet: Hogwarts Legacy's golden haze and soft light over Breath of the Wild's land): since 2026-11-14 the
// game's own look (its numbers are jeeb.Look.*'s defaults, DayNight.cpp). Off (jeeb.TownLab.NewLook 0) sets the look before it, for
// comparison
void ATownLab::ApplyLook(bool bNew)
{
	struct FV { const TCHAR* Name; const TCHAR* New; const TCHAR* Old; };
	static const FV Vars[] = {
		{ TEXT("jeeb.Look.HazeBlue"), TEXT("0.85"), TEXT("1") },      // the peach-lilac low-sun haze, a touch warmer (0.6 went grey in the first tours)
		{ TEXT("jeeb.Look.Aerial"), TEXT("18"), TEXT("16") },         // strong aerial perspective: far hills fade to the sky
		{ TEXT("jeeb.Look.AerialStart"), TEXT("0.3"), TEXT("0.35") },
		{ TEXT("jeeb.Look.HazeStart"), TEXT("2200"), TEXT("2500") },  // the haze starts nearer, so a street's far end already softens
		{ TEXT("jeeb.Look.SunGlow"), TEXT("1.6"), TEXT("1") },        // the golden glow in the haze toward the sun
		{ TEXT("jeeb.Look.Bloom"), TEXT("1.15"), TEXT("0.8") },       // soft bloom
		{ TEXT("jeeb.Look.ShadowCool"), TEXT("0.02"), TEXT("0.012") },   // lifted, slightly blue shadows: never black
		{ TEXT("jeeb.Look.Sat"), TEXT("1.16"), TEXT("1.1") },
		{ TEXT("jeeb.Look.Warm"), TEXT("0.075"), TEXT("0.04") },      // warm highlights
		{ TEXT("jeeb.Look.Contrast"), TEXT("0.98"), TEXT("1") },      // a little softer: painterly
		{ TEXT("jeeb.Look.LowLift"), TEXT("1.4"), TEXT("0.8") },      // more light at the low sun: golden, not murky (0.95 read dim in the first tour)
		{ TEXT("jeeb.Look.Exposure"), TEXT("0.24"), TEXT("0.16") },
		{ TEXT("jeeb.Look.Vignette"), TEXT("0.3"), TEXT("0.4") },
		{ TEXT("jeeb.Look.Shafts"), TEXT("1"), TEXT("0") },           // gentle light shafts round the sun
	};
	for (const FV& V : Vars) SetVar(V.Name, bNew ? V.New : V.Old);
	AKnightGameState::FogScale = 1.f;   // (thicker fog, 1.15 to 1.7, bleached the noon sky and greyed the low sun in the first tours: the haze comes from the look variables alone)
	UE_LOG(LogTemp, Log, TEXT("JEEBLAB look %s"), bNew ? TEXT("NEW") : TEXT("game's own"));
}

// Cheaper sun shadows: a shorter reach (the far cascade covers less, so trees far off cast none) and nothing small casting into it
void ATownLab::ApplyShadows(bool bCheap)
{
	if (bCheap) { SetVar(TEXT("r.Shadow.RadiusThreshold"), TEXT("0.01")); }   // (only tiny far things lose their shadow: DistanceScale 0.6 with RadiusThreshold 0.03 took the trees' shade off the lawn, 2026-11-14)
	else { RestoreVar(TEXT("r.Shadow.DistanceScale")); RestoreVar(TEXT("r.Shadow.RadiusThreshold")); }
	UE_LOG(LogTemp, Log, TEXT("JEEBLAB shadows %s"), bCheap ? TEXT("cheap") : TEXT("game's own"));
}

void ATownLab::ApplyClouds(bool bCheap)
{
	if (bCheap) { SetVar(TEXT("jeeb.Cloud.Samples"), TEXT("0.6")); SetVar(TEXT("jeeb.Cloud.ShadowSamples"), TEXT("0.5")); SetVar(TEXT("jeeb.Cloud.Trace"), TEXT("40")); }
	else { RestoreVar(TEXT("jeeb.Cloud.Samples")); RestoreVar(TEXT("jeeb.Cloud.ShadowSamples")); RestoreVar(TEXT("jeeb.Cloud.Trace")); }
	UE_LOG(LogTemp, Log, TEXT("JEEBLAB clouds %s"), bCheap ? TEXT("cheap") : TEXT("game's own"));
}

void ATownLab::ShowTown(bool bOld, bool bShow)
{
	if (bOld)
	{
		for (TActorIterator<AOakHillTown> It(GetWorld()); It; ++It) It->SetActorHiddenInGame(!bShow);
		for (TActorIterator<AOakHillLife> It(GetWorld()); It; ++It) It->SetActorHiddenInGame(!bShow);
	}
	else for (TActorIterator<ATownLabTown> It(GetWorld()); It; ++It) It->SetActorHiddenInGame(!bShow);
}

void ATownLab::Tick(float Dt)
{
	Super::Tick(Dt);
	using namespace JeebTownLabVars;
	const int32 L = NewLook.GetValueOnGameThread() != 0;
	const int32 Follow = JeebTownLab::Active() ? L : 0;   // (in Field the game's own shadows and clouds unless asked: neither cheap setting was carried into the game)
	const int32 S = CheapShadows.GetValueOnGameThread() < 0 ? Follow : CheapShadows.GetValueOnGameThread() != 0;
	const int32 C = CheapClouds.GetValueOnGameThread() < 0 ? Follow : CheapClouds.GetValueOnGameThread() != 0;
	if (L != LookNow) { LookNow = L; ApplyLook(L != 0); }
	if (S != ShadowsNow) { ShadowsNow = S; ApplyShadows(S != 0); }
	if (C != CloudsNow) { CloudsNow = C; ApplyClouds(C != 0); }
	const int32 O = ShowOld.GetValueOnGameThread() != 0, N = ShowNew.GetValueOnGameThread() != 0;
	if (O != ShowOldNow) { ShowOldNow = O; ShowTown(true, O != 0); }
	if (N != ShowNewNow) { ShowNewNow = N; ShowTown(false, N != 0); }
	TickWorldGround(Dt);
	if (bTour) TickTour(Dt);
}

// (The world's own ground in this map drew with a lab copy of M_Ground, M_TL_WorldGround, until 2026-11-14: its swathes, brightness
// and low-sun fill are M_Ground's own now, in Field too, set by AWorldTerrain. Nothing left to swap.)
void ATownLab::TickWorldGround(float Dt)
{
}

// ------------------------------------------------------------------ the tour
void ATownLab::BuildTour()
{
	struct FView { const TCHAR* Name; float DX, DY, Up, Yaw, Pitch; bool bStats; };
	const FView Views[] = {
		{ TEXT("plaza"),  0.f,     -1250.f, 170.f,  90.f, -4.f, true },    // west of the oak, looking east across the plaza
		{ TEXT("street"), 1500.f,  50.f,    170.f,  0.f,  -2.f, false },   // the north road, looking up it to the castle knoll
		{ TEXT("edge"),   500.f,   8500.f,  220.f,  -90.f, -3.f, true },   // the open east side, looking back into the town
		{ TEXT("hill"),   10000.f, 8200.f,  3500.f, -140.7f, -14.f, false },   // from the air to the north-east, the whole town
		{ TEXT("house"),  -1373.f, 2565.f,  170.f,  118.f, -6.f, false },    // 14 m from the house on plot 25 (9 m from its wall): the whole front, its foot and decor
		{ TEXT("base"),   -1608.f, 3006.f,  120.f,  118.f, -22.f, false },   // the same house's foot from 4 m: foundation stones, weeds, the ground at the wall
		{ TEXT("store"),  -757.f,  370.f,   250.f,  154.f, -8.f, false },    // 13 m from the store on plot 5, from the plaza side
	};
	const float Phases[2] = { 0.f, 0.5f };
	const TCHAR* PhaseNames[2] = { TEXT("noon"), TEXT("low") };
	// (in Field, for -LabTour: the real town only, which is the rebuilt one, or the old one with -OldOakHill; in whatever look the
	// console variables say, so a "before" run passes -ExecCmds="jeeb.TownLab.NewLook 0"; plus open land, the town's west side
	// looking over the river at Blackrock, and Blackrock itself, to see what the world-wide look and ground cost there)
	const bool bField = !JeebTownLab::Active();
	struct FSpot { const TCHAR* Name; float X, Y, Up, Yaw, Pitch; };
	const FSpot FieldSpots[] = {
		{ TEXT("west"), JeebOakHill::PosX + 300.f, JeebOakHill::PosY - 5200.f, 220.f, 180.f, -3.f },   // the west road at the river, looking at Blackrock
		{ TEXT("open"), JeebOakHill::PosX, JeebOakHill::PosY + 30000.f, 170.f, 20.f, -3.f },             // open rolling land 300 m east
		{ TEXT("openair"), JeebOakHill::PosX + 10000.f, JeebOakHill::PosY + 38200.f, 3500.f, -140.7f, -14.f },
		{ TEXT("blackrock"), JeebTerrain::VillageCenter().X - 1800.f, JeebTerrain::VillageCenter().Y, 170.f, 0.f, -3.f },
	};
	const int32 FieldLook = -1;   // (-1: StartStation leaves the look and shadow settings as they are; -ExecCmds runs after BeginPlay)
	if (bField)
		for (int32 p = 0; p < 2; ++p)
			for (const FSpot& Sp : FieldSpots)
			{
				FString Spots;
				if (FParse::Value(FCommandLine::Get(), TEXT("LabTourSpots="), Spots) && !(TEXT("+") + Spots + TEXT("+")).Contains(FString::Printf(TEXT("+%s+"), Sp.Name))) continue;
				FStation S;
				S.Name = FString::Printf(TEXT("field_%s_%s"), Sp.Name, PhaseNames[p]);
				S.Town = 0; S.Eye = FVector(Sp.X, Sp.Y, JeebTerrain::HeightAt(Sp.X, Sp.Y) + Sp.Up); S.Yaw = Sp.Yaw; S.Pitch = Sp.Pitch; S.Phase = Phases[p];
				S.Look = FieldLook; S.Cheap = -1; S.bShot = bTourShots;
				Stations.Add(S);
			}
	for (int32 Town = 0; Town < (bField ? 1 : 2); ++Town)
		for (int32 p = 0; p < 2; ++p)
			for (const FView& V : Views)
			{
				if (!bTourShots && !V.bStats) continue;
				FString Only;   // (-LabTourOnly=<spot>_<noon|low>: just that spot, in every look and shadow setting: for chasing a look problem)
				const bool bOnly = FParse::Value(FCommandLine::Get(), TEXT("LabTourOnly="), Only);
				if (bOnly && Only != FString::Printf(TEXT("%s_%s"), V.Name, PhaseNames[p])) continue;
				FString Spots;   // (-LabTourSpots=house+base: only these spots, the usual stations)
				if (FParse::Value(FCommandLine::Get(), TEXT("LabTourSpots="), Spots) && !(TEXT("+") + Spots + TEXT("+")).Contains(FString::Printf(TEXT("+%s+"), V.Name))) continue;
				const float PX = Town == 0 ? JeebOakHill::PosX : JeebTownLab::PlazaX, PY = Town == 0 ? JeebOakHill::PosY : JeebTownLab::PlazaY;
				const float X = PX + V.DX, Y = PY + V.DY;
				FStation S;
				S.Name = FString::Printf(TEXT("%s_%s_%s"), bField ? TEXT("field") : Town == 0 ? TEXT("old") : TEXT("new"), V.Name, PhaseNames[p]);
				S.Town = Town; S.Eye = FVector(X, Y, JeebTerrain::HeightAt(X, Y) + V.Up); S.Yaw = V.Yaw; S.Pitch = V.Pitch; S.Phase = Phases[p];
				S.Look = bField ? FieldLook : Town; S.Cheap = bField ? -1 : Town; S.bShot = bTourShots;
				Stations.Add(S);
				if (bOnly)
					for (int32 v = 0; v < 4; ++v)
						if (v != Town * 3) { FStation Var = S; Var.Look = v / 2; Var.Cheap = v % 2; Var.Name += FString::Printf(TEXT("_look%d_cheap%d"), Var.Look, Var.Cheap); Stations.Add(Var); }
				// each town in the other's light too, at the plaza at the low sun; and the look and the cheap settings apart (numbers only)
				if (!bField && FCString::Strcmp(V.Name, TEXT("plaza")) == 0 && p == 1)
				{
					FStation X2 = S; X2.Look = 1 - Town; X2.Cheap = 1 - Town; X2.Name += Town == 0 ? TEXT("_newlook") : TEXT("_oldlook"); Stations.Add(X2);
					FStation X3 = S; X3.Look = 1; X3.Cheap = 0; X3.bShot = false; X3.Name += TEXT("_newlook_fullshadows"); Stations.Add(X3);
					FStation X4 = S; X4.Look = 0; X4.Cheap = 1; X4.bShot = false; X4.Name += TEXT("_oldlook_cheap"); Stations.Add(X4);
				}
			}
	UE_LOG(LogTemp, Log, TEXT("JEEBLAB tour of %d stations (%s)"), Stations.Num(), bTourShots ? TEXT("pictures and numbers") : TEXT("numbers only"));
}

void ATownLab::StartStation(int32 i)
{
	StationAt = i; StationClock = 0.f;
	SFrame.Reset(); SGame.Reset(); SRender.Reset(); SRhi.Reset(); SGpu.Reset(); SDraws.Reset(); SPrims.Reset();
	const FStation& S = Stations[i];
	ADayNight::FixedPhase = S.Phase;
	if (S.Look >= 0) 	IConsoleManager::Get().FindConsoleVariable(TEXT("jeeb.TownLab.NewLook"))->Set(S.Look, ECVF_SetByConsole);
	if (S.Cheap >= 0) { 	IConsoleManager::Get().FindConsoleVariable(TEXT("jeeb.TownLab.CheapShadows"))->Set(S.Cheap, ECVF_SetByConsole);
	IConsoleManager::Get().FindConsoleVariable(TEXT("jeeb.TownLab.CheapClouds"))->Set(S.Cheap, ECVF_SetByConsole);
	}
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC) return;
	// (the knight stands under the eye, hidden, so the grass grows round the view as it would round a player there)
	if (APawn* P = PC->GetPawn())
	{
		P->SetActorHiddenInGame(true);
		P->SetActorLocation(FVector(S.Eye.X, S.Eye.Y, JeebTerrain::HeightAt(S.Eye.X, S.Eye.Y) + 100.f), false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (!Eye) Eye = GetWorld()->SpawnActor<ACameraActor>(S.Eye, FRotator(S.Pitch, S.Yaw, 0.f));
	if (Eye)
	{
		Eye->SetActorLocationAndRotation(S.Eye, FRotator(S.Pitch, S.Yaw, 0.f));
		Eye->GetCameraComponent()->SetFieldOfView(75.f);
		Eye->GetCameraComponent()->bConstrainAspectRatio = false;
		PC->SetViewTarget(Eye);
	}
	if (PC->GetHUD()) PC->GetHUD()->bShowHUD = false;
}

void ATownLab::TickTour(float Dt)
{
	TourClock += Dt;
	if (StationAt < 0) { if (TourClock > 6.f && Stations.Num() > 0) StartStation(0); return; }
	StationClock += Dt;
	const FStation& S = Stations[StationAt];
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) if (Eye && PC->GetViewTarget() != Eye) PC->SetViewTarget(Eye);
	if (StationClock > 5.5f && StationClock < 8.5f)
	{
		SFrame.Add(Dt * 1000.f);
		SGame.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
		SRender.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
		SRhi.Add(FPlatformTime::ToMilliseconds(GRHIThreadTime));
		SGpu.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
		SDraws.Add(float(GNumDrawCallsRHI[0]));
		SPrims.Add(float(GNumPrimitivesDrawnRHI[0]));
	}
	if (StationClock >= 8.5f && SFrame.Num() > 0)
	{
		auto Med = [](TArray<float>& A) { A.Sort(); return A[A.Num() / 2]; };
		UE_LOG(LogTemp, Display, TEXT("JEEBLAB STAT %s look=%d cheap=%d frame %.2f game %.2f render %.2f rhi %.2f gpu %.2f draws %.0f tris %.0f (%d frames)"),
			*S.Name, S.Look, S.Cheap, Med(SFrame), Med(SGame), Med(SRender), Med(SRhi), Med(SGpu), Med(SDraws), Med(SPrims), SFrame.Num());
		SFrame.Reset();
		if (S.bShot) FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / FString::Printf(TEXT("TL_%s.png"), *S.Name), false, false);
	}
	if (StationClock > 9.2f)
	{
		if (StationAt + 1 < Stations.Num()) StartStation(StationAt + 1);
		else if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) { UE_LOG(LogTemp, Display, TEXT("JEEBLAB tour done")); PC->ConsoleCommand(TEXT("quit")); }
	}
}

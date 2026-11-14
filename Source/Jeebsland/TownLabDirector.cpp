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
	if (JeebOakHill::Enabled()) ATownLabTown::PlaceBoulders(this, JeebOakHill::PosX, JeebOakHill::PosY);
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

// The new light (the style sheet: Hogwarts Legacy's golden haze and soft light over Breath of the Wild's land). Everything is the
// game's own look settings (jeeb.Look.*, DayNight.cpp) pushed further, plus light shafts round the sun and thicker haze
void ATownLab::ApplyLook(bool bNew)
{
	struct FV { const TCHAR* Name; const TCHAR* Value; };
	static const FV Vars[] = {
		{ TEXT("jeeb.Look.HazeBlue"), TEXT("0.6") },      // haze leaning warm cream (at the low sun) instead of blue-grey
		{ TEXT("jeeb.Look.Aerial"), TEXT("28") },         // strong aerial perspective: far hills fade to the sky
		{ TEXT("jeeb.Look.AerialStart"), TEXT("0.15") },
		{ TEXT("jeeb.Look.HazeStart"), TEXT("1200") },    // the haze starts nearer, so a street's far end already softens
		{ TEXT("jeeb.Look.SunGlow"), TEXT("2.4") },       // the golden glow in the haze toward the sun
		{ TEXT("jeeb.Look.Bloom"), TEXT("1.15") },        // soft bloom
		{ TEXT("jeeb.Look.ShadowCool"), TEXT("0.032") },  // lifted, slightly blue shadows: never black
		{ TEXT("jeeb.Look.Sat"), TEXT("1.16") },
		{ TEXT("jeeb.Look.Warm"), TEXT("0.075") },        // warm highlights
		{ TEXT("jeeb.Look.Contrast"), TEXT("0.94") },     // a little softer: painterly
		{ TEXT("jeeb.Look.LowLift"), TEXT("0.95") },
		{ TEXT("jeeb.Look.Vignette"), TEXT("0.3") },
	};
	for (const FV& V : Vars) { if (bNew) SetVar(V.Name, V.Value); else RestoreVar(V.Name); }
	AKnightGameState::FogScale = bNew ? 1.7f : 1.f;
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
		if (UDirectionalLightComponent* L = Cast<UDirectionalLightComponent>(It->GetLightComponent()))
		{
			// gentle light shafts round the sun (bloom shafts only: the occlusion pass is left off, it costs and darkens)
			L->bEnableLightShaftBloom = bNew;
			L->BloomScale = 0.12f;
			L->BloomThreshold = 0.35f;
			L->BloomMaxBrightness = 4.f;
			L->BloomTint = FColor(255, 214, 160);
			L->bEnableLightShaftOcclusion = false;
			L->MarkRenderStateDirty();
		}
	UE_LOG(LogTemp, Log, TEXT("JEEBLAB look %s"), bNew ? TEXT("NEW") : TEXT("game's own"));
}

// Cheaper sun shadows: a shorter reach (the far cascade covers less, so trees far off cast none) and nothing small casting into it
void ATownLab::ApplyShadows(bool bCheap)
{
	if (bCheap) { SetVar(TEXT("r.Shadow.DistanceScale"), TEXT("0.45")); SetVar(TEXT("r.Shadow.RadiusThreshold"), TEXT("0.04")); }
	else { RestoreVar(TEXT("r.Shadow.DistanceScale")); RestoreVar(TEXT("r.Shadow.RadiusThreshold")); }
	UE_LOG(LogTemp, Log, TEXT("JEEBLAB shadows %s"), bCheap ? TEXT("cheap") : TEXT("game's own"));
}

void ATownLab::ApplyClouds(bool bCheap)
{
	if (bCheap) { SetVar(TEXT("jeeb.Cloud.Samples"), TEXT("0.5")); SetVar(TEXT("jeeb.Cloud.ShadowSamples"), TEXT("0.4")); SetVar(TEXT("jeeb.Cloud.Trace"), TEXT("25")); }
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
	const int32 S = CheapShadows.GetValueOnGameThread() < 0 ? L : CheapShadows.GetValueOnGameThread() != 0;
	const int32 C = CheapClouds.GetValueOnGameThread() < 0 ? L : CheapClouds.GetValueOnGameThread() != 0;
	if (L != LookNow) { LookNow = L; ApplyLook(L != 0); }
	if (S != ShadowsNow) { ShadowsNow = S; ApplyShadows(S != 0); }
	if (C != CloudsNow) { CloudsNow = C; ApplyClouds(C != 0); }
	const int32 O = ShowOld.GetValueOnGameThread() != 0, N = ShowNew.GetValueOnGameThread() != 0;
	if (O != ShowOldNow) { ShowOldNow = O; ShowTown(true, O != 0); }
	if (N != ShowNewNow) { ShowNewNow = N; ShowTown(false, N != 0); }
	if (bTour) TickTour(Dt);
}

// ------------------------------------------------------------------ the tour
void ATownLab::BuildTour()
{
	struct FView { const TCHAR* Name; float DX, DY, Up, Yaw, Pitch; bool bStats; };
	const FView Views[] = {
		{ TEXT("plaza"),  0.f,     -1250.f, 170.f,  90.f, -4.f, true },    // west of the oak, looking east across the plaza
		{ TEXT("street"), 1500.f,  50.f,    170.f,  0.f,  -2.f, false },   // the north road, looking up it to the castle knoll
		{ TEXT("edge"),   500.f,   9500.f,  220.f,  -90.f, -3.f, true },   // the open east side, looking back into the town
		{ TEXT("hill"),   11000.f, 9000.f,  3500.f, -140.7f, -14.f, false },   // from the air to the north-east, the whole town
	};
	const float Phases[2] = { 0.f, 0.5f };
	const TCHAR* PhaseNames[2] = { TEXT("noon"), TEXT("low") };
	for (int32 Town = 0; Town < 2; ++Town)
		for (int32 p = 0; p < 2; ++p)
			for (const FView& V : Views)
			{
				if (!bTourShots && !V.bStats) continue;
				const float PX = Town == 0 ? JeebOakHill::PosX : JeebTownLab::PlazaX, PY = Town == 0 ? JeebOakHill::PosY : JeebTownLab::PlazaY;
				const float X = PX + V.DX, Y = PY + V.DY;
				FStation S;
				S.Name = FString::Printf(TEXT("%s_%s_%s"), Town == 0 ? TEXT("old") : TEXT("new"), V.Name, PhaseNames[p]);
				S.Town = Town; S.Eye = FVector(X, Y, JeebTerrain::HeightAt(X, Y) + V.Up); S.Yaw = V.Yaw; S.Pitch = V.Pitch; S.Phase = Phases[p];
				S.Look = Town; S.Cheap = Town; S.bShot = bTourShots;
				Stations.Add(S);
				// each town in the other's light too, at the plaza at the low sun; and the look and the cheap settings apart (numbers only)
				if (FCString::Strcmp(V.Name, TEXT("plaza")) == 0 && p == 1)
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
	IConsoleManager::Get().FindConsoleVariable(TEXT("jeeb.TownLab.NewLook"))->Set(S.Look, ECVF_SetByConsole);
	IConsoleManager::Get().FindConsoleVariable(TEXT("jeeb.TownLab.CheapShadows"))->Set(S.Cheap, ECVF_SetByConsole);
	IConsoleManager::Get().FindConsoleVariable(TEXT("jeeb.TownLab.CheapClouds"))->Set(S.Cheap, ECVF_SetByConsole);
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

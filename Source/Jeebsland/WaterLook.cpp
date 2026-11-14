#include "WaterLook.h"
#include "Components/PrimitiveComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/Package.h"
#include "Containers/Ticker.h"
#include "DayNight.h"

namespace
{
	// Overrides for M_Water's parameters while the game runs (empty or negative: the material's own value, Tools/water_material.py)
	TAutoConsoleVariable<FString> CVarAbsorb(TEXT("jeeb.Water.Absorb"), TEXT(""), TEXT("Water absorption per cm, \"R G B\" (empty: M_Water's)"));
	TAutoConsoleVariable<FString> CVarScatter(TEXT("jeeb.Water.Scatter"), TEXT(""), TEXT("Water scattering per cm, \"R G B\" (empty: M_Water's)"));
	TAutoConsoleVariable<FString> CVarTint(TEXT("jeeb.Water.Tint"), TEXT(""), TEXT("Colour scale of what is seen through the water, \"R G B\" (empty: M_Water's)"));
	TAutoConsoleVariable<FString> CVarFoamColor(TEXT("jeeb.Water.FoamColor"), TEXT(""), TEXT("Shore foam colour, \"R G B\" (empty: M_Water's)"));
	TAutoConsoleVariable<float> CVarRipple(TEXT("jeeb.Water.Ripple"), -1.f, TEXT("Ripple strength (negative: M_Water's)"));
	TAutoConsoleVariable<float> CVarRough(TEXT("jeeb.Water.Rough"), -1.f, TEXT("Roughness near (negative: M_Water's)"));
	TAutoConsoleVariable<float> CVarRoughFar(TEXT("jeeb.Water.RoughFar"), -1.f, TEXT("Roughness far away (negative: M_Water's)"));
	TAutoConsoleVariable<float> CVarSpec(TEXT("jeeb.Water.Spec"), -1.f, TEXT("Specular (negative: M_Water's)"));
	TAutoConsoleVariable<float> CVarFlow(TEXT("jeeb.Water.Flow"), -1.f, TEXT("River flow speed, cm/s (negative: M_Water's)"));
	TAutoConsoleVariable<float> CVarFoam(TEXT("jeeb.Water.Foam"), -1.f, TEXT("Shore foam strength (negative: M_Water's)"));
	TAutoConsoleVariable<float> CVarFoamDepth(TEXT("jeeb.Water.FoamDepth"), -1.f, TEXT("Depth in cm under which the foam line runs (negative: M_Water's)"));
	TAutoConsoleVariable<float> CVarPhase(TEXT("jeeb.Water.PhaseG"), -2.f, TEXT("Scattering phase -1..1 (below -1: M_Water's)"));
	TAutoConsoleVariable<float> CVarDuskScatter(TEXT("jeeb.Water.DuskScatter"), 0.10f, TEXT("Share of the water's scattering left at the low sun (1: none taken away)"));

	TAutoConsoleVariable<FString> CVarGlow(TEXT("jeeb.Water.Glow"), TEXT("0.004 0.005 0.006"), TEXT("The water's own faint slate light at noon, linear R G B (emissive: no shadow stripes)"));
	TAutoConsoleVariable<FString> CVarGlowDusk(TEXT("jeeb.Water.GlowDusk"), TEXT("0.008 0.013 0.017"), TEXT("The same at the low sun, so the water stays a dim slate there and never goes black"));

	TWeakObjectPtr<UMaterialInstanceDynamic> Shared;
	FLinearColor BaseScatter(-1.f, -1.f, -1.f, 1.f);   // the scattering before the low sun takes its share (negative: not known yet)
	float LastDusk = -1.f;   // the sun's height the water was last set for

	bool ParseRGB(const FString& S, FLinearColor& Out)
	{
		TArray<FString> P;
		S.Replace(TEXT(","), TEXT(" ")).ParseIntoArrayWS(P);
		if (P.Num() < 3) return false;
		Out = FLinearColor(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]), 1.f);
		return true;
	}

	// The sun lights the water's own body (scattering). At the orange low sun that turned the rivers into a muddy orange floor with
	// hard tree-shadow stripes, so most of it fades out as the sun sinks, and an even faint emissive Glow takes over so the water is
	// a dim slate there (not black) and a wading knight's legs still show.
	void SetScatter(UMaterialInstanceDynamic* M)
	{
		if (!M || BaseScatter.R < 0.f) return;
		const float D = FMath::Clamp(ADayNight::Dusk, 0.f, 1.f);
		if (FMath::Abs(D - LastDusk) < 0.01f) return;
		LastDusk = D;
		const float Share = FMath::Lerp(1.f, FMath::Clamp(CVarDuskScatter.GetValueOnGameThread(), 0.f, 1.f), D);
		M->SetVectorParameterValue(TEXT("Scattering"), FLinearColor(BaseScatter.R * Share, BaseScatter.G * Share, BaseScatter.B * Share, 1.f));
		FLinearColor Noon(0.f, 0.f, 0.f, 1.f), Low(0.f, 0.f, 0.f, 1.f);
		ParseRGB(CVarGlow.GetValueOnGameThread(), Noon);
		ParseRGB(CVarGlowDusk.GetValueOnGameThread(), Low);
		M->SetVectorParameterValue(TEXT("Glow"), FMath::Lerp(Noon, Low, D));
	}

	// Pushes whatever jeeb.Water.* is set to onto the shared instance (when it is made, and when any console variable changes)
	void Push()
	{
		UMaterialInstanceDynamic* M = Shared.Get();
		if (!M) return;
		FLinearColor C;
		if (ParseRGB(CVarAbsorb.GetValueOnGameThread(), C)) M->SetVectorParameterValue(TEXT("Absorption"), C);
		if (ParseRGB(CVarScatter.GetValueOnGameThread(), C)) BaseScatter = C;
		else if (M->Parent) M->Parent->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Scattering")), BaseScatter);
		LastDusk = -1.f;
		SetScatter(M);
		if (ParseRGB(CVarTint.GetValueOnGameThread(), C)) M->SetVectorParameterValue(TEXT("BehindTint"), C);
		if (ParseRGB(CVarFoamColor.GetValueOnGameThread(), C)) M->SetVectorParameterValue(TEXT("FoamColor"), C);
		auto Scalar = [&](TAutoConsoleVariable<float>& V, const TCHAR* Name, float Min)
		{
			const float X = V.GetValueOnGameThread();
			if (X >= Min) M->SetScalarParameterValue(Name, X);
		};
		Scalar(CVarRipple, TEXT("Ripple"), 0.f);
		Scalar(CVarRough, TEXT("Roughness"), 0.f);
		Scalar(CVarRoughFar, TEXT("RoughnessFar"), 0.f);
		Scalar(CVarSpec, TEXT("Specular"), 0.f);
		Scalar(CVarFlow, TEXT("FlowSpeed"), 0.f);
		Scalar(CVarFoam, TEXT("Foam"), 0.f);
		Scalar(CVarFoamDepth, TEXT("FoamDepth"), 0.01f);
		Scalar(CVarPhase, TEXT("PhaseG"), -1.f);
	}

	FAutoConsoleVariableSink WaterSink(FConsoleCommandDelegate::CreateStatic(&Push));

	bool OldWater()
	{
#if !UE_BUILD_SHIPPING
		static const bool bOld = FParse::Param(FCommandLine::Get(), TEXT("OldWater"));   // test pictures: the flat water before 2026-11-13
		return bOld;
#else
		return false;
#endif
	}
}

UMaterialInterface* JeebWaterLook::Material()
{
	if (OldWater()) return nullptr;
	if (UMaterialInstanceDynamic* M = Shared.Get()) return M;
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Water.M_Water"));
	if (!Base)
	{
		UE_LOG(LogTemp, Warning, TEXT("JEEBWATER M_Water is missing (run Tools/BuildWater.ps1): flat water colour instead"));
		return nullptr;
	}
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, GetTransientPackage());
	Shared = M;   // (kept alive by the components that wear it; made again if they are all gone)
	Push();
	static bool bTicking = false;   // follow the sun four times a second (one ticker for the whole game)
	if (!bTicking)
	{
		bTicking = true;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) { SetScatter(Shared.Get()); return true; }), 0.25f);
	}
	return M;
}

void JeebWaterLook::Apply(UPrimitiveComponent* C, int32 Slot, const FLinearColor& OldColor)
{
	if (!C) return;
	if (UMaterialInterface* M = Material())
	{
		C->SetMaterial(Slot, M);
		return;
	}
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	if (UMaterialInstanceDynamic* Mi = C->CreateDynamicMaterialInstance(Slot, Base)) Mi->SetVectorParameterValue(TEXT("Color"), OldColor);
}

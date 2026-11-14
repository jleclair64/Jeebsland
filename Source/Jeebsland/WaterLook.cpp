#include "WaterLook.h"
#include "Components/PrimitiveComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/Package.h"

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

	TWeakObjectPtr<UMaterialInstanceDynamic> Shared;

	bool ParseRGB(const FString& S, FLinearColor& Out)
	{
		TArray<FString> P;
		S.Replace(TEXT(","), TEXT(" ")).ParseIntoArrayWS(P);
		if (P.Num() < 3) return false;
		Out = FLinearColor(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]), 1.f);
		return true;
	}

	// Pushes whatever jeeb.Water.* is set to onto the shared instance (when it is made, and when any console variable changes)
	void Push()
	{
		UMaterialInstanceDynamic* M = Shared.Get();
		if (!M) return;
		FLinearColor C;
		if (ParseRGB(CVarAbsorb.GetValueOnGameThread(), C)) M->SetVectorParameterValue(TEXT("Absorption"), C);
		if (ParseRGB(CVarScatter.GetValueOnGameThread(), C)) M->SetVectorParameterValue(TEXT("Scattering"), C);
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

#include "LampLook.h"
#include "HAL/IConsoleManager.h"
#include "Components/MeshComponent.h"
#include "Components/LightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TreeLook.h"

namespace
{
	void Reapply(IConsoleVariable* = nullptr);
	// (1.0 0.5 0.13 linear is about a 2200 K oil flame; the older warm white was 1.0 0.62 0.3)
	TAutoConsoleVariable<FString> CVarTint(TEXT("jeeb.Lamp.Tint"), TEXT("1.0 0.38 0.06"),
		TEXT("Colour of every town lantern, lamp and sconce (glass and light), \"r g b\" linear; default a deep amber oil flame. Live; looks only."),
		FConsoleVariableDelegate::CreateStatic(&Reapply), ECVF_Default);
	TAutoConsoleVariable<float> CVarGlow(TEXT("jeeb.Lamp.Glow"), 0.7941f,
		TEXT("How hard the lamp glass glows (the glass picture times this; was 5, which clipped to white); default 0.9. Live; looks only."),
		FConsoleVariableDelegate::CreateStatic(&Reapply), ECVF_Default);
	TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> GMids;
	TArray<TWeakObjectPtr<ULightComponent>> GLights;

	void Reapply(IConsoleVariable*)
	{
		const FLinearColor C = JeebLamp::Colour();
		for (const TWeakObjectPtr<UMaterialInstanceDynamic>& M : GMids)
			if (M.IsValid()) { M->SetVectorParameterValue(TEXT("GlowTint"), C); M->SetScalarParameterValue(TEXT("GlowAmount"), CVarGlow.GetValueOnGameThread()); }
		for (const TWeakObjectPtr<ULightComponent>& L : GLights) if (L.IsValid()) L->SetLightColor(JeebLamp::LightColour());
	}
}

FLinearColor JeebLamp::Colour()
{
	return JeebTreeLook::ParseRGB(CVarTint.GetValueOnGameThread(), FLinearColor(1.f, 0.38f, 0.06f));
}

void JeebLamp::ApplyToMesh(UMeshComponent* C, UObject* Outer)
{
	if (!C) return;
	for (int32 i = 0; i < C->GetNumMaterials(); ++i)
	{
		UMaterialInterface* M = C->GetMaterial(i);
		if (!M || !M->GetName().EndsWith(TEXT("_Glow"))) continue;
		UMaterialInstanceDynamic* D = nullptr;
		for (const TWeakObjectPtr<UMaterialInstanceDynamic>& Old : GMids) if (Old.IsValid() && Old->Parent == M) { D = Old.Get(); break; }
		if (!D)
		{
			D = UMaterialInstanceDynamic::Create(M, Outer ? Outer : (UObject*)GetTransientPackage());
			D->SetVectorParameterValue(TEXT("GlowTint"), Colour());
			D->SetScalarParameterValue(TEXT("GlowAmount"), CVarGlow.GetValueOnGameThread());
			GMids.Add(D);
		}
		C->SetMaterial(i, D);
	}
}

void JeebLamp::TrackLight(ULightComponent* L)
{
	if (!L) return;
	L->SetLightColor(LightColour());
	if (GLights.Num() > 512) GLights.RemoveAll([](const TWeakObjectPtr<ULightComponent>& P) { return !P.IsValid(); });
	GLights.Add(L);
}

// (the light is a little paler than the glass, or the walls it falls on turn orange: the default 1 0.38 0.06 glass gives a 1 0.49 0.13 light, about 2300 K)
FLinearColor JeebLamp::LightColour()
{
	const FLinearColor C = Colour();
	return FLinearColor(C.R, FMath::Min(C.G * 1.3f, 1.f), FMath::Min(C.B * 2.2f, 1.f));
}

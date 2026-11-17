#include "TreeLook.h"
#include "HAL/IConsoleManager.h"

namespace
{
	FSimpleMulticastDelegate GChanged;
	TAutoConsoleVariable<float> CVarSaturation(TEXT("jeeb.Trees.Saturation"), 0.7f,
		TEXT("How colourful the autumn tree leaves are (the DZ forests, the town's trees, the fallen leaves): 1 = the colours as the Dz sliders say, 0 = grey; default 0.7 (2026-11-16: toned back toward Skyrim's Rift and Hogwarts Legacy). Live; looks only."),
		FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*) { GChanged.Broadcast(); }), ECVF_Default);
}

float JeebTreeLook::Saturation() { return FMath::Clamp(CVarSaturation.GetValueOnGameThread(), 0.f, 2.f); }

FLinearColor JeebTreeLook::Saturate(const FLinearColor& C)
{
	const float L = 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B;
	const float S = Saturation();
	return FLinearColor(FMath::Max(0.f, L + S * (C.R - L)), FMath::Max(0.f, L + S * (C.G - L)), FMath::Max(0.f, L + S * (C.B - L)), C.A);
}

FLinearColor JeebTreeLook::ParseRGB(const FString& In, const FLinearColor& Def)
{
	FString S = In;
	S.ReplaceCharInline(TEXT(','), TEXT(' ')); S.ReplaceCharInline(TEXT('_'), TEXT(' '));
	TArray<FString> P; S.ParseIntoArrayWS(P);
	float C[3] = { Def.R, Def.G, Def.B };
	for (int32 i = 0; i < 3 && i < P.Num(); ++i) C[i] = FMath::Max(0.f, FCString::Atof(*P[i]));
	return FLinearColor(C[0], C[1], C[2], Def.A);
}

FSimpleMulticastDelegate& JeebTreeLook::OnChanged() { return GChanged; }

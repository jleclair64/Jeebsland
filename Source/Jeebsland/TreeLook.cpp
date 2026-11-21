#include "TreeLook.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInterface.h"

namespace
{
	FSimpleMulticastDelegate GChanged;
	TAutoConsoleVariable<float> CVarSaturation(TEXT("jeeb.Trees.Saturation"), 0.6996f,
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

int32 JeebTreeLook::DarkCones(UMaterialInstanceDynamic* D)
{
	if (!D) return 0;
	static TMap<FName, TWeakObjectPtr<UTexture>> Copies;   // (pack texture name -> its copy; null when there is none)
	static bool bWarned = false;
	TArray<FMaterialParameterInfo> Infos; TArray<FGuid> Ids;
	D->GetAllTextureParameterInfo(Infos, Ids);
	int32 Done = 0;
	for (const FMaterialParameterInfo& Info : Infos)
	{
		UTexture* T = nullptr;
		if (!D->GetTextureParameterValue(Info, T) || !T || !T->GetPathName().StartsWith(TEXT("/Game/PN_interactiveSpruceForest/"))) continue;
		const FString Name = T->GetName();
		if (!Name.StartsWith(TEXT("Leaf_A")) && !Name.Contains(TEXT("_imposter_A"))) continue;   // (the colour textures: the only ones with cones in)
		TWeakObjectPtr<UTexture>* Found = Copies.Find(T->GetFName());
		if (!Found || (!Found->IsValid() && !Found->IsExplicitlyNull()))
		{
			UTexture* Copy = LoadObject<UTexture>(nullptr, *FString::Printf(TEXT("/Game/TreeGroves/SpruceCones/%s_Cones.%s_Cones"), *Name, *Name), nullptr, LOAD_Quiet | LOAD_NoWarn);
			if (!Copy && !bWarned) { bWarned = true; UE_LOG(LogTemp, Warning, TEXT("JEEBSPRUCE no /Game/TreeGroves/SpruceCones/%s_Cones (run Tools/BuildSpruceCones.ps1 once on this machine): the spruces keep the pack's orange cones"), *Name); }
			Found = &Copies.Add(T->GetFName(), Copy);
		}
		if (UTexture* Copy = Found->Get()) { D->SetTextureParameterValueByInfo(Info, Copy); ++Done; }
	}
	return Done;
}

UMaterialInterface* JeebTreeLook::ConeLeaf(UMaterialInterface* M)
{
	if (!M || !M->GetName().EndsWith(TEXT("_leaf")) || !M->GetPathName().StartsWith(TEXT("/Game/PN_interactiveSpruceForest/"))) return M;
	static TMap<FName, TWeakObjectPtr<UMaterialInterface>> Copies;   // (pack material name -> its copy; null when there is none)
	static bool bWarned = false;
	TWeakObjectPtr<UMaterialInterface>* Found = Copies.Find(M->GetFName());
	if (!Found || (!Found->IsValid() && !Found->IsExplicitlyNull()))
	{
		const FString Name = M->GetName();
		UMaterialInterface* Copy = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/TreeGroves/SpruceCones/%s_Cones.%s_Cones"), *Name, *Name), nullptr, LOAD_Quiet | LOAD_NoWarn);
		if (!Copy && !bWarned) { bWarned = true; UE_LOG(LogTemp, Warning, TEXT("JEEBSPRUCE no /Game/TreeGroves/SpruceCones/%s_Cones (run Tools/BuildSpruceCones.ps1 once on this machine): the spruces keep the pack's orange cones"), *Name); }
		Found = &Copies.Add(M->GetFName(), Copy);
	}
	UMaterialInterface* Copy = Found->Get();
	return Copy ? Copy : M;
}

#include "Palette.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	// The knobs: -1..1, 0 is today's look. Set from Palette.ini's [Knobs] when the file is read (unless set on the command line or
	// console first); live in Play.
	TAutoConsoleVariable<float> CVarAutumn(TEXT("jeeb.Palette.Autumn"), 0.f, TEXT("Palette: + greens turn gold-orange (pines a quarter), - warm leaves and dry grass turn green; -1..1 (Content/Data/Palette.ini)"));
	TAutoConsoleVariable<float> CVarWarmth(TEXT("jeeb.Palette.Warmth"), 0.f, TEXT("Palette: + every world surface warmer, - cooler, same brightness; -1..1"));
	TAutoConsoleVariable<float> CVarGreen(TEXT("jeeb.Palette.Greenness"), 0.f, TEXT("Palette: + plants lusher green, - drier olive; -1..1"));
	TAutoConsoleVariable<float> CVarSat(TEXT("jeeb.Palette.Saturation"), 0.f, TEXT("Palette: + world colours richer, - greyer; -1..1"));
	TAutoConsoleVariable<float> CVarBright(TEXT("jeeb.Palette.Brightness"), 0.f, TEXT("Palette: + world surfaces lighter, - darker (not the haze or water); -1..1"));

	const FVector3f Lum(0.3f, 0.59f, 0.11f);   // as the materials' own Desaturation nodes

	struct FEntry { FLinearColor Raw = FLinearColor::Black; JeebPalette::EClass Class = JeebPalette::EClass::World; FLinearColor Graded = FLinearColor::Black; };
	struct FBound { TWeakObjectPtr<UMaterialInstanceDynamic> M; FName Param; FName Key; bool bGraded; };
	struct FMat { FVector3f R = FVector3f(1, 0, 0), G = FVector3f(0, 1, 0), B = FVector3f(0, 0, 1); };

	struct FState
	{
		bool bLoaded = false;
		TMap<FName, FEntry> Colours;
		TMap<FName, float> Knobs;   // the file's knob values
		FVector3f WarmthGain = FVector3f(0.12f, 0.02f, -0.18f), GreenGain = FVector3f(-0.1f, 0.15f, -0.1f);
		float SatRange = 0.5f, BrightRange = 0.25f, EvergreenShare = 0.25f;
		FVector3f AutumnTint = FVector3f(1.55f, 0.93f, 0.3f), SummerTint = FVector3f(0.62f, 1.2f, 0.42f);
		// what the knobs make (as in MPC_JeebPalette)
		float K[5] = { 0, 0, 0, 0, 0 };   // autumn, warmth, green, sat, bright as last applied
		FMat World, Plant, PlantX, Sky;
		FLinearColor Season = FLinearColor(0, 0, 0.25f, 0), SeasonTint = FLinearColor::White;
		TArray<FBound> Bound;
		TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> Graders;   // (BindGrade: materials that take the grade as parameters)
		FSimpleMulticastDelegate Changed;
	};
	FState& S() { static FState St; return St; }

	FString FilePath() { return FPaths::ProjectContentDir() / TEXT("Data") / TEXT("Palette.ini"); }

	// "a, b, c[, class]" (a ';' starts a comment)
	bool ParseValues(const FString& V, TArray<FString>& Out)
	{
		V.ParseIntoArray(Out, TEXT(","));
		for (FString& P : Out) P.TrimStartAndEndInline();
		return Out.Num() > 0;
	}
	FVector3f Vec3(const TArray<FString>& P, const FVector3f& Fallback)
	{
		return P.Num() >= 3 ? FVector3f(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2])) : Fallback;
	}

	void ReadFile()
	{
		FState& St = S();
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *FilePath()))
		{
			UE_LOG(LogTemp, Warning, TEXT("JEEBPALETTE %s is missing: the materials keep their built-in colours"), *FilePath());
			return;
		}
		St.Colours.Reset(); St.Knobs.Reset();
		FString Section;
		for (FString L : Lines)
		{
			int32 Semi;
			if (L.FindChar(TEXT(';'), Semi)) L.LeftInline(Semi);
			L.TrimStartAndEndInline();
			if (L.IsEmpty()) continue;
			if (L.StartsWith(TEXT("[")) && L.EndsWith(TEXT("]"))) { Section = L.Mid(1, L.Len() - 2); continue; }
			FString Key, Val;
			if (!L.Split(TEXT("="), &Key, &Val)) continue;
			Key.TrimStartAndEndInline();
			TArray<FString> P;
			ParseValues(Val, P);
			if (Section == TEXT("Knobs")) St.Knobs.Add(*Key, FCString::Atof(*P[0]));
			else if (Section == TEXT("Grade"))
			{
				if (Key == TEXT("WarmthGain")) St.WarmthGain = Vec3(P, St.WarmthGain);
				else if (Key == TEXT("GreenGain")) St.GreenGain = Vec3(P, St.GreenGain);
				else if (Key == TEXT("SaturationRange")) St.SatRange = FCString::Atof(*P[0]);
				else if (Key == TEXT("BrightnessRange")) St.BrightRange = FCString::Atof(*P[0]);
				else if (Key == TEXT("AutumnTint")) St.AutumnTint = Vec3(P, St.AutumnTint);
				else if (Key == TEXT("SummerTint")) St.SummerTint = Vec3(P, St.SummerTint);
				else if (Key == TEXT("EvergreenShare")) St.EvergreenShare = FCString::Atof(*P[0]);
			}
			else if (Section == TEXT("Colours") && P.Num() >= 3)
			{
				FEntry E;
				const FVector3f V = Vec3(P, FVector3f::ZeroVector);
				E.Raw = FLinearColor(V.X, V.Y, V.Z, 1.f);
				const FString C = P.Num() >= 4 ? P[3].ToLower() : FString(TEXT("world"));
				E.Class = C == TEXT("plant") ? JeebPalette::EClass::Plant : C == TEXT("evergreen") ? JeebPalette::EClass::Evergreen
					: C == TEXT("sky") ? JeebPalette::EClass::Sky : C == TEXT("none") ? JeebPalette::EClass::None : JeebPalette::EClass::World;
				St.Colours.Add(*Key, E);
			}
		}
		UE_LOG(LogTemp, Log, TEXT("JEEBPALETTE read %d colours from %s"), St.Colours.Num(), *FilePath());
	}

	float Knob(TAutoConsoleVariable<float>& V) { return FMath::Clamp(V.GetValueOnGameThread(), -1.f, 1.f); }

	// a luminance-neutral gain: 1 + K * Dir, rescaled so a grey keeps its brightness (exactly 1 at K = 0)
	FVector3f Gain(float K, const FVector3f& Dir)
	{
		if (K == 0.f) return FVector3f(1.f, 1.f, 1.f);
		const FVector3f G = FVector3f(1.f, 1.f, 1.f) + Dir * K;
		return G / FMath::Max(FVector3f::DotProduct(G, Lum), 0.01f);
	}
	// saturation s (1 = none) after a per-channel gain: rows of Sat x diag(Gain)
	FMat SatTimesGain(float Sat, const FVector3f& G)
	{
		FVector3f Rows[3];
		for (int32 i = 0; i < 3; ++i)
		{
			FVector3f R(0.f, 0.f, 0.f);
			R[i] = Sat;
			if (Sat != 1.f) R += Lum * (1.f - Sat);   // (exactly the identity row at Sat 1)
			Rows[i] = R * G;
		}
		FMat M;
		M.R = Rows[0]; M.G = Rows[1]; M.B = Rows[2];
		return M;
	}
	FVector3f Apply(const FMat& M, const FVector3f& C) { return FVector3f(FVector3f::DotProduct(M.R, C), FVector3f::DotProduct(M.G, C), FVector3f::DotProduct(M.B, C)); }

	// the same season step as Tools/palette.py's _season (shader): greens toward the autumn gold (or warm colours toward green)
	FVector3f Season(const FVector3f& C, float Share)
	{
		const FState& St = S();
		const float Up = St.Season.R, Down = St.Season.G;
		if (Up == 0.f && Down == 0.f) return C;
		const float L = FVector3f::DotProduct(C, Lum);
		const float Inv = 2.f / (L + 0.001f);
		const float Green = FMath::Clamp((C.Y - 0.5f * (C.X + C.Z)) * Inv, 0.f, 1.f);
		const float Warm = FMath::Clamp((C.X - 0.5f * (C.Y + C.Z)) * Inv, 0.f, 1.f);
		const float A = (Green * Up + Warm * Down) * Share;
		const FVector3f T = FVector3f(St.SeasonTint.R, St.SeasonTint.G, St.SeasonTint.B) * L;
		return C + (T - C) * A;
	}

	FLinearColor ToLin(const FVector3f& V, float A = 1.f) { return FLinearColor(V.X, V.Y, V.Z, A); }
	FLinearColor Row(const FVector3f& V) { return FLinearColor(V.X, V.Y, V.Z, 0.f); }

	// the knobs into the matrices, and every named colour's graded value
	void Recompute()
	{
		FState& St = S();
		const float Autumn = Knob(CVarAutumn), Warmth = Knob(CVarWarmth), Green = Knob(CVarGreen), Sat = Knob(CVarSat), Bright = Knob(CVarBright);
		St.K[0] = Autumn; St.K[1] = Warmth; St.K[2] = Green; St.K[3] = Sat; St.K[4] = Bright;
		const FVector3f W = Gain(Warmth, St.WarmthGain);
		const float B = 1.f + Bright * St.BrightRange;
		const float Sa = 1.f + Sat * St.SatRange;
		St.World = SatTimesGain(Sa, W * B);
		St.Sky = SatTimesGain(Sa, W);   // (no Brightness: the exposure looks after the haze's and water's light)
		const FVector3f G = Gain(Green, St.GreenGain);
		St.PlantX = SatTimesGain(1.f, G);
		St.Plant = SatTimesGain(Sa, W * B * G);   // (= World x PlantX: both are a gain then the saturation)
		const FVector3f Tint = Autumn >= 0.f ? St.AutumnTint : St.SummerTint;
		const FVector3f TintN = Tint / FMath::Max(FVector3f::DotProduct(Tint, Lum), 0.01f);
		St.Season = FLinearColor(FMath::Max(Autumn, 0.f), FMath::Max(-Autumn, 0.f), St.EvergreenShare, 0.f);
		St.SeasonTint = ToLin(TintN);
		for (TPair<FName, FEntry>& P : St.Colours) P.Value.Graded = JeebPalette::Grade(P.Value.Raw, P.Value.Class);
	}

	TStrongObjectPtr<UMaterialParameterCollection>& Mpc()
	{
		static TStrongObjectPtr<UMaterialParameterCollection> P;
		static bool bTried = false;
		if (!bTried)
		{
			bTried = true;
			P.Reset(LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/MPC_JeebPalette.MPC_JeebPalette")));
			if (!P.IsValid()) UE_LOG(LogTemp, Warning, TEXT("JEEBPALETTE /Game/MPC_JeebPalette is missing (run Tools\\BuildMaterials.ps1): the knobs do nothing in the materials"));
		}
		return P;
	}

	void WriteWorld(UWorld* World)
	{
		UMaterialParameterCollection* C = Mpc().Get();
		if (!World || !C) return;
		UMaterialParameterCollectionInstance* I = World->GetParameterCollectionInstance(C);
		if (!I) return;
		const FState& St = S();
		auto Set = [&](const TCHAR* Name, const FLinearColor& V) { if (C->GetVectorParameterByName(Name)) I->SetVectorParameterValue(Name, V); };
		auto Rows = [&](const TCHAR* Pre, const FMat& M)
		{
			Set(*FString::Printf(TEXT("%sR"), Pre), Row(M.R)); Set(*FString::Printf(TEXT("%sG"), Pre), Row(M.G)); Set(*FString::Printf(TEXT("%sB"), Pre), Row(M.B));
		};
		Rows(TEXT("World"), St.World); Rows(TEXT("Plant"), St.Plant); Rows(TEXT("PlantX"), St.PlantX);
		Set(TEXT("Season"), St.Season);
		Set(TEXT("SeasonTint"), St.SeasonTint);
		for (const TPair<FName, FEntry>& P : St.Colours)
			if (C->GetVectorParameterByName(P.Key)) I->SetVectorParameterValue(P.Key, P.Value.Raw);   // (raw: the materials grade them)
	}

	// the grade as parameters "Pal<name>" (materials that cannot read the collection: palette.py PARAM_MODE)
	void SetGrade(UMaterialInstanceDynamic* M)
	{
		const FState& St = S();
		auto Rows = [&](const TCHAR* Pre, const FMat& Mx)
		{
			M->SetVectorParameterValue(*FString::Printf(TEXT("Pal%sR"), Pre), Row(Mx.R));
			M->SetVectorParameterValue(*FString::Printf(TEXT("Pal%sG"), Pre), Row(Mx.G));
			M->SetVectorParameterValue(*FString::Printf(TEXT("Pal%sB"), Pre), Row(Mx.B));
		};
		Rows(TEXT("World"), St.World); Rows(TEXT("Plant"), St.Plant); Rows(TEXT("PlantX"), St.PlantX);
		M->SetVectorParameterValue(TEXT("PalSeason"), St.Season);
		M->SetVectorParameterValue(TEXT("PalSeasonTint"), St.SeasonTint);
	}

	void PushBound()
	{
		FState& St = S();
		St.Bound.RemoveAll([](const FBound& B) { return !B.M.IsValid(); });
		for (const FBound& B : St.Bound) B.M->SetVectorParameterValue(B.Param, B.bGraded ? JeebPalette::Graded(B.Key) : JeebPalette::Raw(B.Key));
		St.Graders.RemoveAll([](const TWeakObjectPtr<UMaterialInstanceDynamic>& M) { return !M.IsValid(); });
		for (const TWeakObjectPtr<UMaterialInstanceDynamic>& M : St.Graders) SetGrade(M.Get());
	}

	void PushAll()
	{
		if (GEngine)
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
				if (UWorld* W = Ctx.World()) if (W->IsGameWorld()) WriteWorld(W);
		PushBound();
		S().Changed.Broadcast();
		const FState& St = S();
		UE_LOG(LogTemp, Log, TEXT("JEEBPALETTE applied: Autumn %.2f Warmth %.2f Greenness %.2f Saturation %.2f Brightness %.2f"), St.K[0], St.K[1], St.K[2], St.K[3], St.K[4]);
	}

	void EnsureLoaded()
	{
		FState& St = S();
		if (St.bLoaded) return;
		St.bLoaded = true;
		ReadFile();
		// the file's knobs, unless the command line or console already set them
		auto Init = [&](TAutoConsoleVariable<float>& V, const TCHAR* Name)
		{
			const float* F = St.Knobs.Find(Name);
			if (F && *F != 0.f && (V->GetFlags() & ECVF_SetByMask) == ECVF_SetByConstructor) V->Set(*F, ECVF_SetByCode);
		};
		Init(CVarAutumn, TEXT("Autumn")); Init(CVarWarmth, TEXT("Warmth")); Init(CVarGreen, TEXT("Greenness")); Init(CVarSat, TEXT("Saturation")); Init(CVarBright, TEXT("Brightness"));
		Recompute();
	}

	// any knob changed (the sink runs after console changes, batched)
	void OnCvars()
	{
		FState& St = S();
		if (!St.bLoaded) return;
		const float Now[5] = { Knob(CVarAutumn), Knob(CVarWarmth), Knob(CVarGreen), Knob(CVarSat), Knob(CVarBright) };
		if (FMemory::Memcmp(Now, St.K, sizeof(Now)) == 0) return;   // (unchanged: nothing written)
		Recompute();
		PushAll();
	}
	FAutoConsoleVariableSink PaletteSink(FConsoleCommandDelegate::CreateStatic(&OnCvars));

	FAutoConsoleCommand ReloadCmd(TEXT("jeeb.Palette.Reload"), TEXT("Re-read Content/Data/Palette.ini (colours and knobs) and apply it"), FConsoleCommandDelegate::CreateLambda([]()
	{
		FState& St = S();
		St.bLoaded = true;
		ReadFile();
		auto Set = [&](TAutoConsoleVariable<float>& V, const TCHAR* Name) { const float* F = St.Knobs.Find(Name); V->Set(F ? *F : 0.f, ECVF_SetByConsole); };
		Set(CVarAutumn, TEXT("Autumn")); Set(CVarWarmth, TEXT("Warmth")); Set(CVarGreen, TEXT("Greenness")); Set(CVarSat, TEXT("Saturation")); Set(CVarBright, TEXT("Brightness"));
		Recompute();
		PushAll();
	}));

	FAutoConsoleCommand SetCmd(TEXT("jeeb.Palette.Set"), TEXT("jeeb.Palette.Set <Name> <R> <G> <B>: try one palette colour live (Palette.ini keeps the saved value)"), FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		EnsureLoaded();
		if (Args.Num() < 4) { UE_LOG(LogTemp, Warning, TEXT("JEEBPALETTE usage: jeeb.Palette.Set <Name> <R> <G> <B>")); return; }
		FEntry* E = S().Colours.Find(*Args[0]);
		if (!E) { UE_LOG(LogTemp, Warning, TEXT("JEEBPALETTE no colour named %s"), *Args[0]); return; }
		const FLinearColor V(FCString::Atof(*Args[1]), FCString::Atof(*Args[2]), FCString::Atof(*Args[3]), 1.f);
		if (V == E->Raw) return;
		E->Raw = V;
		Recompute();
		PushAll();
	}));
}

FLinearColor JeebPalette::Raw(FName Key)
{
	EnsureLoaded();
	if (const FEntry* E = S().Colours.Find(Key)) return E->Raw;
	UE_LOG(LogTemp, Warning, TEXT("JEEBPALETTE no colour named %s in Palette.ini"), *Key.ToString());
	return FLinearColor::Black;
}

FLinearColor JeebPalette::Graded(FName Key)
{
	EnsureLoaded();
	if (const FEntry* E = S().Colours.Find(Key)) return E->Graded;
	UE_LOG(LogTemp, Warning, TEXT("JEEBPALETTE no colour named %s in Palette.ini"), *Key.ToString());
	return FLinearColor::Black;
}

FLinearColor JeebPalette::Grade(const FLinearColor& In, EClass Class)
{
	const FState& St = S();
	const FVector3f C(In.R, In.G, In.B);
	switch (Class)
	{
	case EClass::Plant: return ToLin(Apply(St.Plant, Season(C, 1.f)), In.A);
	case EClass::Evergreen: return ToLin(Apply(St.Plant, Season(C, St.EvergreenShare)), In.A);
	case EClass::World: return ToLin(Apply(St.World, C), In.A);
	case EClass::Sky: return ToLin(Apply(St.Sky, C), In.A);
	default: return In;
	}
}

void JeebPalette::Bind(UMaterialInstanceDynamic* M, FName Param, FName Key, bool bGraded)
{
	if (!M) return;
	EnsureLoaded();
	M->SetVectorParameterValue(Param, bGraded ? Graded(Key) : Raw(Key));
	FState& St = S();
	if (St.Bound.Num() > 256) St.Bound.RemoveAll([](const FBound& B) { return !B.M.IsValid(); });
	St.Bound.Add({ M, Param, Key, bGraded });
}

void JeebPalette::BindGrade(UMaterialInstanceDynamic* M)
{
	if (!M) return;
	EnsureLoaded();
	SetGrade(M);
	S().Graders.AddUnique(M);
}

void JeebPalette::ApplyToWorld(UWorld* World)
{
	EnsureLoaded();
	WriteWorld(World);
	const FState& St = S();
	UE_LOG(LogTemp, Log, TEXT("JEEBPALETTE world %s: %d colours, Autumn %.2f Warmth %.2f Greenness %.2f Saturation %.2f Brightness %.2f"), World ? *World->GetMapName() : TEXT("?"),
		St.Colours.Num(), St.K[0], St.K[1], St.K[2], St.K[3], St.K[4]);
}

FSimpleMulticastDelegate& JeebPalette::OnChanged()
{
	return S().Changed;
}

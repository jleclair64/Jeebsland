#include "KnightHUD.h"
#include "KnightCharacter.h"
#include "Palette.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"

// ---------------------------------------------------------------------------------------------------
// The LOOK tab of the Escape screen (2026-11-15, non-Shipping builds only): sliders for the console variables that set the
// look, saved as presets in Saved/LookPresets/<name>.ini (plain "cvar=value" lines). The world keeps running behind it.
// ---------------------------------------------------------------------------------------------------
#if !UE_BUILD_SHIPPING

namespace
{
	enum class EK : uint8 { Float, Comp, Scale };   // a float cvar; one of the three numbers of an "r g b" cvar; a brightness multiplier on a whole "r g b" cvar
	struct FLookItem
	{
		int32 Group; const TCHAR* Label; const TCHAR* Cvar; EK Kind; int32 Comp;
		float Min, Max; bool bDeferred;   // deferred: applied when the slider is let go (it regrows the grass)
		float DefaultOverride;            // for cvars whose own default is "use the material's": that value; else -1000
		const TCHAR* Pal;                 // Scale items whose cvar is empty by default ("the palette's colour"): that palette entry
	};
	const TCHAR* Groups[] = { TEXT("PICTURE"), TEXT("SKY + HAZE"), TEXT("CLOUDS"), TEXT("WATER"), TEXT("GRASS"), TEXT("GROUND"), TEXT("TREES") };
	constexpr int32 NumGroups = UE_ARRAY_COUNT(Groups);
	const float NoDef = -1000.f;
	const FLookItem Items[] = {
		{ 0, TEXT("Colour strength (saturation)"), TEXT("jeeb.Look.Sat"), EK::Float, 0, 0.6f, 1.8f, false, NoDef },
		{ 0, TEXT("Contrast"), TEXT("jeeb.Look.Contrast"), EK::Float, 0, 0.7f, 1.3f, false, NoDef },
		{ 0, TEXT("Warm highlights"), TEXT("jeeb.Look.Warm"), EK::Float, 0, 0.f, 0.2f, false, NoDef },
		{ 0, TEXT("Cool shadows"), TEXT("jeeb.Look.ShadowCool"), EK::Float, 0, 0.f, 0.06f, false, NoDef },
		{ 0, TEXT("Bloom (glow of bright things)"), TEXT("jeeb.Look.Bloom"), EK::Float, 0, 0.f, 3.f, false, NoDef },
		{ 0, TEXT("Brightness at noon (stops)"), TEXT("jeeb.Look.Exposure"), EK::Float, 0, -0.8f, 1.f, false, NoDef },
		{ 0, TEXT("Extra brightness at the low sun"), TEXT("jeeb.Look.LowLift"), EK::Float, 0, 0.f, 3.f, false, NoDef },
		{ 0, TEXT("Dark corners (vignette)"), TEXT("jeeb.Look.Vignette"), EK::Float, 0, 0.f, 0.8f, false, NoDef },
		{ 0, TEXT("Ambient shadow strength"), TEXT("jeeb.Look.AO"), EK::Float, 0, 0.f, 1.5f, false, NoDef },
		{ 0, TEXT("Ambient shadow radius"), TEXT("jeeb.Look.AORadius"), EK::Float, 0, 30.f, 300.f, false, NoDef },

		{ 1, TEXT("Blue haze (0 grey fog)"), TEXT("jeeb.Look.HazeBlue"), EK::Float, 0, 0.f, 1.f, false, NoDef },
		{ 1, TEXT("Haze starts at (cm)"), TEXT("jeeb.Look.HazeStart"), EK::Float, 0, 300.f, 8000.f, false, NoDef },
		{ 1, TEXT("Warm glow toward the sun"), TEXT("jeeb.Look.SunGlow"), EK::Float, 0, 0.f, 4.f, false, NoDef },
		{ 1, TEXT("Far hills fade to blue"), TEXT("jeeb.Look.Aerial"), EK::Float, 0, 1.f, 60.f, false, NoDef },
		{ 1, TEXT("Hill fade starts at (km)"), TEXT("jeeb.Look.AerialStart"), EK::Float, 0, 0.05f, 1.f, false, NoDef },

		{ 2, TEXT("Cloud thickness (density)"), TEXT("jeeb.Cloud.Density"), EK::Float, 0, 0.5f, 8.f, false, NoDef },
		{ 2, TEXT("Cloud size"), TEXT("jeeb.Cloud.Scale"), EK::Float, 0, 0.5f, 3.f, false, NoDef },
		{ 2, TEXT("Cloud layer bottom (km)"), TEXT("jeeb.Cloud.Bottom"), EK::Float, 0, 0.8f, 3.f, false, NoDef },
		{ 2, TEXT("Cloud layer depth (km)"), TEXT("jeeb.Cloud.Height"), EK::Float, 0, 1.f, 10.f, false, NoDef },
		{ 2, TEXT("Light kept on the undersides"), TEXT("jeeb.Cloud.Under"), EK::Float, 0, 0.f, 1.f, false, NoDef },
		{ 2, TEXT("Bounce from the ground"), TEXT("jeeb.Cloud.Bounce"), EK::Float, 0, 0.f, 1.f, false, NoDef },
		{ 2, TEXT("Light filling the shaded side"), TEXT("jeeb.Cloud.Fill"), EK::Float, 0, 0.f, 3.f, false, NoDef },
		{ 2, TEXT("Warmth of the sunlit cloud"), TEXT("jeeb.Cloud.Warm"), EK::Float, 0, 0.f, 1.f, false, NoDef },
		{ 2, TEXT("LOW graphics: painted cloud brightness"), TEXT("jeeb.Cloud.LowBright"), EK::Float, 0, 0.5f, 4.f, false, NoDef },

		{ 3, TEXT("Water: scattering left at low sun"), TEXT("jeeb.Water.DuskScatter"), EK::Float, 0, 0.f, 1.f, false, NoDef },
		{ 3, TEXT("Water: ripple strength"), TEXT("jeeb.Water.Ripple"), EK::Float, 0, 0.f, 1.f, false, 0.28f },
		{ 3, TEXT("Water: roughness (blurry reflections)"), TEXT("jeeb.Water.Rough"), EK::Float, 0, 0.02f, 0.6f, false, 0.23f },
		{ 3, TEXT("Water: shine (specular)"), TEXT("jeeb.Water.Spec"), EK::Float, 0, 0.f, 1.f, false, 0.25f },
		{ 3, TEXT("Water: shore foam"), TEXT("jeeb.Water.Foam"), EK::Float, 0, 0.f, 0.4f, false, 0.08f },
		{ 3, TEXT("Water: river flow speed"), TEXT("jeeb.Water.Flow"), EK::Float, 0, 0.f, 200.f, false, 55.f },
		{ 3, TEXT("Water: own glow at noon"), TEXT("jeeb.Water.Glow"), EK::Scale, 0, 0.f, 6.f, false, NoDef, TEXT("WaterGlow") },
		{ 3, TEXT("Water: own glow at low sun"), TEXT("jeeb.Water.GlowDusk"), EK::Scale, 0, 0.f, 6.f, false, NoDef, TEXT("WaterGlowDusk") },

		{ 4, TEXT("Grass: how far it reaches (cm)"), TEXT("jeeb.Grass.PackFade"), EK::Float, 0, 1500.f, 8000.f, true, NoDef },
		{ 4, TEXT("Grass: clumps per square metre"), TEXT("jeeb.Grass.Density1"), EK::Float, 0, 2.f, 30.f, true, NoDef },
		{ 4, TEXT("Grass: yellow stems to green"), TEXT("jeeb.Grass.GreenDry"), EK::Float, 0, 0.f, 1.f, false, NoDef },
		{ 4, TEXT("Grass colour: red"), TEXT("jeeb.Grass.Tint1"), EK::Comp, 0, 0.3f, 1.3f, false, NoDef },
		{ 4, TEXT("Grass colour: green"), TEXT("jeeb.Grass.Tint1"), EK::Comp, 1, 0.3f, 1.3f, false, NoDef },
		{ 4, TEXT("Grass colour: blue"), TEXT("jeeb.Grass.Tint1"), EK::Comp, 2, 0.3f, 1.3f, false, NoDef },
		{ 4, TEXT("Grass: patches of field colour"), TEXT("jeeb.Grass.PackField"), EK::Float, 0, 0.f, 1.f, false, NoDef },
		{ 4, TEXT("Grass: pulled toward grey"), TEXT("jeeb.Grass.PackDesat"), EK::Float, 0, 0.f, 1.f, false, NoDef },
		{ 4, TEXT("Grass: flower clumps"), TEXT("jeeb.Grass.PackFlowers"), EK::Float, 0, 0.f, 1.5f, false, NoDef },

		{ 5, TEXT("Ground: low-sun light past the grass"), TEXT("jeeb.Ground.SkyFill"), EK::Float, 0, 0.f, 1.f, false, NoDef },

		// (added at the end so the test run's item numbers below stay put)
		{ 1, TEXT("Sun: 0 noon, .5 low, -1 moving (reset)"), TEXT("jeeb.Sun.Phase"), EK::Float, 0, 0.f, 1.f, false, NoDef },
		{ 0, TEXT("Armour: plate roughness (0 mirror)"), TEXT("jeeb.Armour.Rough"), EK::Float, 0, 0.f, 0.4f, false, NoDef },
		{ 0, TEXT("Armour: plate brightness"), TEXT("jeeb.Armour.Shine"), EK::Float, 0, 0.5f, 5.f, false, NoDef },
		{ 0, TEXT("Armour: sun glint on plate"), TEXT("jeeb.Armour.Glint"), EK::Float, 0, 0.f, 4.f, false, NoDef },
		{ 0, TEXT("Armour: mirror share (1 pure metal)"), TEXT("jeeb.Armour.Metal"), EK::Float, 0, 0.5f, 1.f, false, NoDef },
		{ 5, TEXT("Ground under the grass at low sun (1 same)"), TEXT("jeeb.Ground.Floor"), EK::Float, 0, 0.4f, 1.2f, false, NoDef },
		{ 5, TEXT("Ground: low-sun light kept under grass"), TEXT("jeeb.Ground.UnderFill"), EK::Float, 0, 0.f, 1.f, false, NoDef },
		{ 1, TEXT("Sky: ground colour below the horizon"), TEXT("jeeb.Sky.Ground"), EK::Float, 0, 0.f, 3.f, false, NoDef },
		{ 0, TEXT("Knight gravity x real (-1 Tuning.ini; reset)"), TEXT("jeeb.Knight.Gravity"), EK::Float, 0, 0.5f, 3.f, false, NoDef },

		// TREES (2026-11-16): the four DZ test forests' leaf colours, "r g b" each (TreeGroves.cpp; live); every bar runs from 0 to twice its default
		{ 6, TEXT("DZ green forest: red"), TEXT("jeeb.Trees.DzGreen"), EK::Comp, 0, 0.f, 0.34f, false, NoDef },
		{ 6, TEXT("DZ green forest: green"), TEXT("jeeb.Trees.DzGreen"), EK::Comp, 1, 0.f, 0.46f, false, NoDef },
		{ 6, TEXT("DZ green forest: blue"), TEXT("jeeb.Trees.DzGreen"), EK::Comp, 2, 0.f, 0.12f, false, NoDef },
		{ 6, TEXT("DZ red forest: red"), TEXT("jeeb.Trees.DzRed"), EK::Comp, 0, 0.f, 0.72f, false, NoDef },
		{ 6, TEXT("DZ red forest: green"), TEXT("jeeb.Trees.DzRed"), EK::Comp, 1, 0.f, 0.1f, false, NoDef },
		{ 6, TEXT("DZ red forest: blue"), TEXT("jeeb.Trees.DzRed"), EK::Comp, 2, 0.f, 0.05f, false, NoDef },
		{ 6, TEXT("DZ orange forest: red"), TEXT("jeeb.Trees.DzOrange"), EK::Comp, 0, 0.f, 1.19f, false, NoDef },
		{ 6, TEXT("DZ orange forest: green"), TEXT("jeeb.Trees.DzOrange"), EK::Comp, 1, 0.f, 0.42f, false, NoDef },
		{ 6, TEXT("DZ orange forest: blue"), TEXT("jeeb.Trees.DzOrange"), EK::Comp, 2, 0.f, 0.07f, false, NoDef },
		{ 6, TEXT("DZ yellow forest: red"), TEXT("jeeb.Trees.DzYellow"), EK::Comp, 0, 0.f, 1.19f, false, NoDef },
		{ 6, TEXT("DZ yellow forest: green"), TEXT("jeeb.Trees.DzYellow"), EK::Comp, 1, 0.f, 0.77f, false, NoDef },
		{ 6, TEXT("DZ yellow forest: blue"), TEXT("jeeb.Trees.DzYellow"), EK::Comp, 2, 0.f, 0.09f, false, NoDef },
		{ 6, TEXT("Spruce forest: red"), TEXT("jeeb.Trees.Spruce"), EK::Comp, 0, 0.f, 2.f, false, NoDef },
		{ 6, TEXT("Spruce forest: green"), TEXT("jeeb.Trees.Spruce"), EK::Comp, 1, 0.f, 2.f, false, NoDef },
		{ 6, TEXT("Spruce forest: blue"), TEXT("jeeb.Trees.Spruce"), EK::Comp, 2, 0.f, 2.f, false, NoDef },
		{ 6, TEXT("All autumn trees: colourfulness (1 = the sliders)"), TEXT("jeeb.Trees.Saturation"), EK::Float, 0, 0.f, 1.5f, false, NoDef },
		{ 6, TEXT("Fallen leaves: brightness"), TEXT("jeeb.Leaves.Brightness"), EK::Float, 0, 0.1f, 1.5f, false, NoDef },
		{ 6, TEXT("Lamps: glass glow"), TEXT("jeeb.Lamp.Glow"), EK::Float, 0, 0.f, 3.f, false, NoDef },
		{ 5, TEXT("Ground past the grass: red"), TEXT("jeeb.Ground.FarTint"), EK::Comp, 0, 0.3f, 1.2f, false, NoDef },
		{ 5, TEXT("Ground past the grass: green"), TEXT("jeeb.Ground.FarTint"), EK::Comp, 1, 0.3f, 1.2f, false, NoDef },
		{ 5, TEXT("Ground past the grass: blue"), TEXT("jeeb.Ground.FarTint"), EK::Comp, 2, 0.3f, 1.2f, false, NoDef },
	};
	constexpr int32 NumItems = UE_ARRAY_COUNT(Items);
	constexpr int32 ShotSatItem = 0, ShotTintGreenItem = 36;   // (the test run's two sliders: saturation, grass green)

	IConsoleVariable* Var(const TCHAR* Name) { return IConsoleManager::Get().FindConsoleVariable(Name); }
	void ParseRgb(const FString& S, float Out[3])
	{
		Out[0] = Out[1] = Out[2] = 1.f;
		TArray<FString> P; S.ParseIntoArrayWS(P);
		for (int32 i = 0; i < 3 && i < P.Num(); ++i) Out[i] = FCString::Atof(*P[i]);
	}
	FString Num(float V)
	{
		FString S = FString::Printf(TEXT("%.4f"), V);
		while (S.EndsWith(TEXT("0")) && S.Contains(TEXT("."))) S.LeftChopInline(1);
		if (S.EndsWith(TEXT("."))) S.LeftChopInline(1);
		return S;
	}

	TMap<FString, FString> Base;   // every cvar's value when the tab was first opened: "the defaults"
	bool bBaseTaken = false;
	TArray<float> Pending;         // deferred sliders: where the handle is before it is let go
	int32 Dragging = -1;
	FString LastName;              // the preset last saved or loaded
	FString Note; double NoteUntil = 0.0;

	FString Dir() { return FPaths::ProjectSavedDir() / TEXT("LookPresets"); }
	void TakeBase()
	{
		if (bBaseTaken) return;
		bBaseTaken = true;
		Pending.Init(-9999.f, NumItems);
		for (const FLookItem& It : Items) if (IConsoleVariable* V = Var(It.Cvar)) Base.Add(It.Cvar, V->GetString());
	}
	float DefaultOf(const FLookItem& It)
	{
		if (It.DefaultOverride > -999.f) return It.DefaultOverride;
		const FString* B = Base.Find(It.Cvar);
		if (!B) return 0.f;
		if (It.Kind == EK::Float) return FCString::Atof(**B);
		if (It.Kind == EK::Scale) return 1.f;
		float C[3]; ParseRgb(*B, C); return C[It.Comp];
	}
	void BaseRgb(const FLookItem& It, float Out[3])   // the colour a Scale slider multiplies: the opening value, or the palette's
	{
		const FString* B = Base.Find(It.Cvar);
		if ((!B || B->TrimStartAndEnd().IsEmpty()) && It.Pal)
		{
			const FLinearColor P = JeebPalette::Graded(It.Pal);
			Out[0] = P.R; Out[1] = P.G; Out[2] = P.B;
			return;
		}
		ParseRgb(B ? *B : FString(), Out);
	}
	float ValueOf(const FLookItem& It)
	{
		IConsoleVariable* V = Var(It.Cvar);
		if (!V) return 0.f;
		if (It.Kind == EK::Float)
		{
			const float X = V->GetFloat();
			return (X < -0.5f && It.DefaultOverride > -999.f) ? It.DefaultOverride : X;   // (negative: the material's own number)
		}
		if (It.Kind == EK::Scale && V->GetString().TrimStartAndEnd().IsEmpty()) return 1.f;   // (empty: the palette's own colour)
		float C[3]; ParseRgb(V->GetString(), C);
		if (It.Kind == EK::Comp) return C[It.Comp];
		float D[3]; BaseRgb(It, D);
		return D[0] > 1e-5f ? C[0] / D[0] : 1.f;
	}
	void SetValue(const FLookItem& It, float X)
	{
		IConsoleVariable* V = Var(It.Cvar);
		if (!V) return;
		X = FMath::Clamp(X, It.Min, It.Max);
		if (It.Kind == EK::Float) { V->Set(*Num(X), ECVF_SetByConsole); return; }
		float C[3];
		if (It.Kind == EK::Comp) { ParseRgb(V->GetString(), C); C[It.Comp] = X; }
		else { float D[3]; BaseRgb(It, D); for (int32 i = 0; i < 3; ++i) C[i] = D[i] * X; }
		V->Set(*FString::Printf(TEXT("%s %s %s"), *Num(C[0]), *Num(C[1]), *Num(C[2])), ECVF_SetByConsole);
	}
	void ResetItem(int32 i)
	{
		const FLookItem& It = Items[i];
		// (back to exactly what the cvar held when the tab opened: an empty water glow means "the palette's colour", and writing
		// "1 1 1" there instead turned the river white, 2026-11-15)
		const FString* B = Base.Find(It.Cvar);
		IConsoleVariable* V = Var(It.Cvar);
		if (V && B && (It.Kind != EK::Comp || B->TrimStartAndEnd().IsEmpty())) V->Set(**B, ECVF_SetByConsole);
		else SetValue(It, DefaultOf(It));
		Pending[i] = -9999.f;
	}
	TArray<FString> CvarNames()
	{
		TArray<FString> Out;
		for (const FLookItem& It : Items) Out.AddUnique(It.Cvar);
		return Out;
	}
	TArray<FString> Presets()
	{
		TArray<FString> Files; IFileManager::Get().FindFiles(Files, *(Dir() / TEXT("*.ini")), true, false);
		for (FString& F : Files) F = FPaths::GetBaseFilename(F);
		Files.Sort();
		return Files;
	}
	void SavePreset(const FString& Name)
	{
		FString Text;
		for (const FString& C : CvarNames()) if (IConsoleVariable* V = Var(*C)) Text += FString::Printf(TEXT("%s=%s\n"), *C, *V->GetString());
		FFileHelper::SaveStringToFile(Text, *(Dir() / Name + TEXT(".ini")));
		UE_LOG(LogTemp, Display, TEXT("JEEBLOOK saved preset %s (%d cvars)"), *Name, CvarNames().Num());
	}
	bool LoadPreset(const FString& Name)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *(Dir() / Name + TEXT(".ini")))) return false;
		TArray<FString> Lines; Text.ParseIntoArrayLines(Lines);
		const TArray<FString> Names = CvarNames();
		int32 Set = 0;
		for (const FString& L : Lines)
		{
			FString K, V;
			if (!L.Split(TEXT("="), &K, &V)) continue;
			K.TrimStartAndEndInline();
			if (!Names.Contains(K)) continue;   // (only the look's own variables)
			if (IConsoleVariable* Cv = Var(*K)) { Cv->Set(*V.TrimStartAndEnd(), ECVF_SetByConsole); ++Set; }
		}
		Pending.Init(-9999.f, NumItems);
		UE_LOG(LogTemp, Display, TEXT("JEEBLOOK loaded preset %s (%d cvars)"), *Name, Set);
		return true;
	}
	void WriteDefaults(const FString& Name)
	{
		FString Text = FString::Printf(TEXT("Look preset \"%s\": every changed setting, old default -> new default (make it permanent in the code's TAutoConsoleVariable defaults)\n\n"), *Name);
		int32 Changed = 0;
		for (const FString& C : CvarNames())
		{
			IConsoleVariable* V = Var(*C);
			const FString* B = Base.Find(C);
			if (!V || !B) continue;
			if (V->GetString() == *B) continue;
			Text += FString::Printf(TEXT("%s: %s -> %s\n"), *C, **B, *V->GetString());
			++Changed;
		}
		if (!Changed) Text += TEXT("(nothing changed)\n");
		FFileHelper::SaveStringToFile(Text, *(Dir() / Name + TEXT("_defaults.txt")));
		UE_LOG(LogTemp, Display, TEXT("JEEBLOOK defaults written for %s (%d changed)"), *Name, Changed);
	}
	FString CleanName(const FString& In)
	{
		FString Out;
		for (const TCHAR C : In) if (FChar::IsAlnum(C) || C == ' ' || C == '-' || C == '_') Out.AppendChar(C);
		return Out.TrimStartAndEnd();
	}
}

void AKnightHUD::DrawLookTab(float PX, float PY, float PW, float PH)
{
	UFont* Font = GEngine->GetMediumFont();
	TakeBase();
	IFileManager::Get().MakeDirectory(*Dir(), true);
	const double Now = GetWorld()->GetTimeSeconds();

	// Test run (-ShotLookPanel): move a slider, save and load a preset, and log what the variables did
	static const bool bShotLook = FParse::Param(FCommandLine::Get(), TEXT("ShotLookPanel"));
	if (bShotLook)
	{
		static double T0 = Now; static int32 Step = 0;
		const double T = Now - T0;
		auto Log = [&](const TCHAR* What) { UE_LOG(LogTemp, Display, TEXT("JEEBLOOK %s: Sat=%s Tint1=%s GrassFade=%s"), What, *Var(TEXT("jeeb.Look.Sat"))->GetString(), *Var(TEXT("jeeb.Grass.Tint1"))->GetString(), *Var(TEXT("jeeb.Grass.PackFade"))->GetString()); };
		if (Step == 0 && T > 0.3) { Log(TEXT("start")); SetValue(Items[ShotSatItem], 1.5f); SetValue(Items[ShotTintGreenItem], 0.6f); Log(TEXT("slider moved")); SavePreset(TEXT("shottest")); WriteDefaults(TEXT("shottest")); Step = 1; }
		else if (Step == 1 && T > 0.6) { SetValue(Items[ShotSatItem], 0.8f); SetValue(Items[ShotTintGreenItem], 0.9f); Log(TEXT("changed again")); Step = 2; }
		else if (Step == 2 && T > 0.9) { LoadPreset(TEXT("shottest")); Log(TEXT("preset loaded")); LastName = TEXT("shottest"); Step = 3; }
		else if (Step == 3 && T > 1.2) { ResetItem(ShotSatItem); Log(TEXT("reset saturation")); SetValue(Items[ShotSatItem], 1.5f); Step = 4; }
	}

	// Group buttons across the top
	const float GY = PY + 54.f;
	// (seven groups since TREES, 2026-11-16: narrower buttons, still clear of the PRESETS heading)
	for (int32 g = 0; g < NumGroups; ++g)
		if (Btn(PX + 20.f + g * 92.f, GY, 88.f, 28.f, Groups[g], true, LookGroup == g, 0.85f)) { LookGroup = g; Dragging = -1; }

	// Sliders, two columns
	// (a tab with more than 12 sliders gets longer columns, rows closer together, instead of a third column over the presets)
	int32 InGroup = 0;
	for (int32 i = 0; i < NumItems; ++i) if (Items[i].Group == LookGroup) ++InGroup;
	const int32 Rows = FMath::Max(6, (InGroup + 1) / 2);
	const float ColW = 300.f, Top = PY + 100.f, RowH = FMath::Min(62.f, (PH - 120.f) / Rows);
	auto ColX = [&](int32 Slot) { return PX + 24.f + (Slot / Rows) * (ColW + 24.f); };
	int32 Shown = 0;
	for (int32 i = 0; i < NumItems; ++i)
	{
		const FLookItem& It = Items[i];
		if (It.Group != LookGroup) continue;
		const float X = ColX(Shown);
		const float Y = Top + (Shown % Rows) * RowH;
		++Shown;
		const float BarW = ColW - 62.f;
		const bool bPend = It.bDeferred && Pending[i] > -9000.f;
		const float Val = bPend ? Pending[i] : ValueOf(It);
		const float Def = DefaultOf(It);
		const float TT = FMath::Clamp((Val - It.Min) / (It.Max - It.Min), 0.f, 1.f);
		const float TD = FMath::Clamp((Def - It.Min) / (It.Max - It.Min), 0.f, 1.f);
		const bool bChanged = !FMath::IsNearlyEqual(Val, Def, (It.Max - It.Min) * 0.002f);
		HDText(It.Label, FLinearColor::White, X, Y, Font, 0.95f);
		const FString VT = Num(Val);
		float TW = 0.f, TH = 0.f; HDTextSize(VT, TW, TH, Font, 0.95f);
		HDText(VT, bChanged ? FLinearColor(1.f, 0.8f, 0.4f) : FLinearColor(0.7f, 0.75f, 0.8f), X + BarW - TW, Y, Font, 0.95f);
		const float BY = Y + 30.f;
		DrawRect(FLinearColor(0.25f, 0.25f, 0.25f, 1.f), X, BY + 4.f, BarW, 8.f);
		DrawRect(FLinearColor(0.9f, 0.55f, 0.15f, 1.f), X, BY + 4.f, BarW * TT, 8.f);
		DrawRect(FLinearColor(0.6f, 0.75f, 0.9f, 1.f), X + BarW * TD - 1.f, BY, 2.f, 16.f);   // where the default sits
		DrawRect(FLinearColor::White, X + BarW * TT - 6.f, BY - 3.f, 12.f, 22.f);
		if (bUIClick && bUIHaveMouse && UIMouseX >= X - 8.f && UIMouseX <= X + BarW + 8.f && UIMouseY >= BY - 8.f && UIMouseY <= BY + 24.f) Dragging = i;
		if (Btn(X + BarW + 8.f, BY - 6.f, 44.f, 26.f, TEXT("reset"), bChanged, false, 0.75f)) { ResetItem(i); Dragging = -1; }
	}
	if (Dragging >= 0)
	{
		const FLookItem& It = Items[Dragging];
		if (bUIWasDown && bUIHaveMouse)
		{
			int32 Slot = 0;
			for (int32 i = 0; i < Dragging; ++i) if (Items[i].Group == It.Group) ++Slot;
			const float X = ColX(Slot), BarW = ColW - 62.f;
			const float V = FMath::Lerp(It.Min, It.Max, FMath::Clamp((UIMouseX - X) / BarW, 0.f, 1.f));
			if (It.bDeferred) Pending[Dragging] = V; else SetValue(It, V);
		}
		if (bUIRelease || !bUIWasDown)
		{
			if (It.bDeferred && Pending[Dragging] > -9000.f) SetValue(It, Pending[Dragging]);
			Pending[Dragging] = -9999.f;
			Dragging = -1;
		}
	}

	// Presets on the right
	const float RX = PX + 24.f + 2.f * (ColW + 24.f), RW = PX + PW - 24.f - RX;
	HDText(TEXT("PRESETS"), FLinearColor(0.85f, 0.85f, 0.9f), RX, GY + 2.f, Font, 1.0f);
	const float BoxY = Top, BoxH = 32.f;
	const bool bOverBox = Over(RX, BoxY, RW, BoxH);
	if (bUIClick) bLookTyping = bOverBox;
	Tile(RX, BoxY, RW, BoxH, FLinearColor(0.8f, 0.82f, 0.9f), bLookTyping ? 0.4f : (bOverBox ? 0.3f : 0.2f));
	const FString Typed = LookName + ((bLookTyping && FMath::FloorToInt(Now * 2.0) % 2 == 0) ? TEXT("|") : TEXT(""));
	HDText(LookName.IsEmpty() && !bLookTyping ? FString(TEXT("click to type a name")) : Typed, LookName.IsEmpty() ? FLinearColor(1.f, 1.f, 1.f, 0.4f) : FLinearColor::White, RX + 8.f, BoxY + 6.f, Font, 0.95f);
	TArray<FString> List = Presets();
	if (Btn(RX, BoxY + 40.f, RW * 0.5f - 3.f, 30.f, TEXT("SAVE AS"), true, false, 0.9f))
	{
		FString Nm = CleanName(LookName);
		if (Nm.IsEmpty()) { int32 K = 1; while (List.Contains(FString::Printf(TEXT("Look %d"), K))) ++K; Nm = FString::Printf(TEXT("Look %d"), K); }
		SavePreset(Nm); LastName = Nm; LookName = Nm; bLookTyping = false;
		Note = FString::Printf(TEXT("Saved %s"), *Nm); NoteUntil = Now + 3.0;
		List = Presets();
	}
	if (Btn(RX + RW * 0.5f + 3.f, BoxY + 40.f, RW * 0.5f - 3.f, 30.f, TEXT("DEFAULTS"), true, false, 0.9f))
	{
		const FString Nm = !CleanName(LookName).IsEmpty() ? CleanName(LookName) : (!LastName.IsEmpty() ? LastName : FString(TEXT("current")));
		WriteDefaults(Nm);
		Note = FString::Printf(TEXT("Wrote %s_defaults.txt"), *Nm); NoteUntil = Now + 4.0;
	}
	const float LY = BoxY + 80.f;
	for (int32 p = 0; p < List.Num() && p < 8; ++p)
	{
		const float Y = LY + p * 36.f;
		if (Btn(RX, Y, RW - 36.f, 30.f, List[p], true, LastName == List[p], 0.85f)) { LoadPreset(List[p]); LastName = List[p]; LookName = List[p]; Note = FString::Printf(TEXT("Loaded %s"), *List[p]); NoteUntil = Now + 3.0; }
		if (Btn(RX + RW - 32.f, Y, 32.f, 30.f, TEXT("x"), true, false, 0.85f)) { IFileManager::Get().Delete(*(Dir() / List[p] + TEXT(".ini"))); if (LastName == List[p]) LastName.Reset(); }
	}
	if (List.Num() == 0) HDText(TEXT("(no presets yet)"), FLinearColor(0.6f, 0.6f, 0.65f), RX, LY + 4.f, Font, 0.9f);
	if (Now < NoteUntil) HDText(Note, FLinearColor(0.6f, 0.9f, 0.6f), RX, PY + PH - 100.f, Font, 0.9f);
	Wrapped(TEXT("Changes apply at once. The blue tick on each bar is where it started. Presets live in Saved/LookPresets."), RX, PY + PH - 80.f, RW, 0.8f, FLinearColor(0.65f, 0.68f, 0.75f));
}

#endif

// (outside the guard: KnightHUDMenu.cpp calls these in every build; in Shipping bLookTyping is never set, so they never run)
void AKnightHUD::LookTypeChar(TCHAR C)
{
	if (C >= 32 && C != 127 && LookName.Len() < 24) LookName.AppendChar(C);
}

void AKnightHUD::LookTypeKey(const FKey& Key)
{
	if (Key == EKeys::BackSpace) { if (LookName.Len() > 0) LookName.LeftChopInline(1); }
	else if (Key == EKeys::Enter || Key == EKeys::Escape || Key == EKeys::Tab) bLookTyping = false;
}

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "KnightHUD.generated.h"

class AKnightCharacter;
class USkillComponent;
class UTexture2D;

// Everything drawn on the screen: minimap, stamina, skills, weapon bar, the inventory / store / smithy / dictionary
// screens and the dialogue box.
UCLASS()
class AKnightHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	// Drawing helpers
	void Poly(const TArray<FVector2D>& P, const FLinearColor& C);
	void Disc(float Cx, float Cy, float Rx, float Ry, const FLinearColor& C);
	void DrawIcon(int32 Id, float X, float Y, float S);
	void DrawIngot(float X, float Y, float S);
	float Wrapped(const FString& Text, float X, float Y, float MaxW, float Scale, const FLinearColor& Col);   // returns the height used
	bool Over(float X, float Y, float W, float H) const { return bUIHaveMouse && UIMouseX >= X && UIMouseX <= X + W && UIMouseY >= Y && UIMouseY <= Y + H; }
	bool Btn(float X, float Y, float W, float H, const FString& Label, bool bEnabled = true, bool bActive = false, float Scale = 1.05f);
	bool CloseBtn(float PX, float PY, float PW);
	void SetTip(const TArray<FString>& Lines) { TipLines = Lines; }
	void ItemTip(int32 Id);
	void DrawTip();
	static FLinearColor Lift(const FLinearColor& C, float F);
	// Liquid-glass drawing: rounded translucent panels, tiles and gauges with a sheen and a rim of light
	UPROPERTY() UTexture2D* QuarterTex = nullptr;
	UPROPERTY() UTexture2D* RingTex[2] = {};
	void EnsureGlassTex();
	void Corner(UTexture2D* Tex, float X, float Y, float R, int32 C, const FLinearColor& Col);
	void FillRR(float X, float Y, float W, float H, float R, TFunctionRef<FLinearColor(float)> ColorAt);
	void RimLight(float X, float Y, float W, float H, float R, float Alpha, const FLinearColor& Tint);
	void Tile(float X, float Y, float W, float H, const FLinearColor& Tint, float Alpha = 0.35f, bool bRim = true);
	void Panel(float X, float Y, float W, float H, float Alpha = 1.f, bool bGlow = false);
	void Gauge(float X, float Y, float W, float H, float F, const FLinearColor& Fill, bool bPulse = false);

	// Screens
	void DrawSettingsScreen(AKnightCharacter* Knight);
	void DrawDictionary(float PX, float PY, float PW, float PH);
	void DrawMinimap(APawn* Pawn);
	void DrawCircle(float X, float Y, float R, const FLinearColor& Col);
	UPROPERTY() UTexture2D* CircleTex = nullptr;
	void DrawInventoryScreen(AKnightCharacter* Knight);
	void DrawShopScreen(AKnightCharacter* Knight);
	void DrawSmithScreen(AKnightCharacter* Knight);
	void DrawDialogue(AKnightCharacter* Knight);
	void DrawWeaponBar(AKnightCharacter* Knight);
	void DrawSkills(const USkillComponent* Skills);
	void DrawSkillsScreen(AKnightCharacter* Knight);
	void DrawSkillIcon(int32 SkillIdx, float X, float Y, float S);
	void DrawSkillPopup(const USkillComponent* Skills);
	bool DrawPackTabs(AKnightCharacter* Knight, float PX, float PY, bool bSkills);
	void DrawLore(float PX, float PY, float PW, float PH);
	void DrawBankScreen(AKnightCharacter* Knight);

	bool bUIWasDown = false;
	bool bUIClick = false;
	float UIMouseX = 0.f, UIMouseY = 0.f;
	bool bUIHaveMouse = false;
	float PolyStep = 1.5f;
	TArray<FString> TipLines;

	// Store and smithy
	int32 ShopCat = 0, ShopKindIdx = 0;
	int32 SmithKindIdx = 0;
	FString ShopMsg;
	float ShopMsgUntil = 0.f;

	// Settings screen
	int32 SettingsTab = 0;       // 0 = settings, 1 = controls, 2 = dictionary, 3 = lore
	int32 BankPage = 0;
	int32 LoreCat = 0;
	int32 LoreSel = -1;
	int32 DictEntry = 0;
	int32 DraggingSlider = -1;   // which slider the mouse is holding (-1 = none)
	bool bWasMouseDown = false;
};

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
	void DrawIconBody(int32 Id, float X, float Y, float S);
	void DrawIngot(float X, float Y, float S);
	float Wrapped(const FString& Text, float X, float Y, float MaxW, float Scale, const FLinearColor& Col);   // returns the height used
	bool Over(float X, float Y, float W, float H) const { return bUIHaveMouse && UIMouseX >= X && UIMouseX <= X + W && UIMouseY >= Y && UIMouseY <= Y + H; }
	bool Btn(float X, float Y, float W, float H, const FString& Label, bool bEnabled = true, bool bActive = false, float Scale = 1.05f);
	bool CloseBtn(float PX, float PY, float PW);
	void SetTip(const TArray<FString>& Lines) { TipLines = Lines; }
	void ItemTip(int32 Id);
	void DrawTip();
	void NorseText(const FString& Text, float Y, float H, float Alpha);   // place names, lettered in a Norse hand (gold, rune-cut strokes), centred
	void DrawLinesAt(const TArray<FString>& Lines, float CX, float BottomY, float Alpha);   // a tooltip-style panel centred over a point (dropped things)
	static FLinearColor Lift(const FLinearColor& C, float F);
	// Liquid-glass drawing: rounded translucent panels, tiles and gauges with a sheen and a rim of light
	UPROPERTY() UTexture2D* QuarterTex = nullptr;
	UPROPERTY() UTexture2D* RingTex[2] = {};
	void Outline(float X, float Y, float W, float H, float R, const FLinearColor& Col);
	float PromptAlpha = 0.f;
	FVector PromptAnchor = FVector::ZeroVector;
	FString PromptText;
	bool bPromptE = false;
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
	UPROPERTY() UTexture2D* AnnulusTex = nullptr;
	void DrawAnnulus(float X, float Y, float R, const FLinearColor& Col);
	void DrawInventoryScreen(AKnightCharacter* Knight);
	void DrawSidePanel(AKnightCharacter* Knight);   // lower left: Inventory, Apparel, Skills
	float PanelTop = 0.f;
	bool bGreyGlass = false;   // while set, Panel and Tile draw a dimmer, smoky-grey glass (the side panel)
	bool bInTown = false; float TownBannerT = -100.f;   // the BLACKROCK banner
	// Drag and drop between the pack (side panel) and the weapon bar
	float InvCellX[32] = {}, InvCellY[32] = {}, InvCellS = 0.f; int32 InvCellCount = 0;   // where the pack cells were drawn this frame
	float HotCellX[5] = {}, HotCellY[5] = {}, HotCellS = 0.f; bool bHotCells = false;
	float RectX[8] = {}, RectY[8] = {}, RectW[8] = {}, RectH[8] = {}; int32 RectCount = 0;   // the windows on screen this frame
	float InvIngotX = 0.f, InvIngotY = 0.f;   // the fixed ingot cell
	float WornCellX[10] = {}, WornCellY[10] = {}, WornCellS = 0.f; int32 WornKind[10] = {}; bool WornSecond[10] = {}; int32 WornCellCount = 0;   // the armour slots drawn this frame
	float PotionCellX = 0.f, PotionCellY = 0.f; bool bPotionCell = false;   // the potion slot
	float DefCellX = 0.f, DefCellY = 0.f; bool bDefCell = false;   // the shield slot at the end of the weapon bar
	// An open sack (E on it): its windows sit to the left of yours. Cells point at entries in the sack (-1 = its ingots).
	float LootCellX[64] = {}, LootCellY[64] = {}, LootCellS = 0.f; int32 LootCellIdx[64] = {}; int32 LootCellCount = 0;
	float ArmorTopY = 0.f;
	void DrawLootWindows(AKnightCharacter* Knight, float IX, float IY, float PW);
	int32 DragKind = 0, DragIdx = -1, DragId = 0;   // 0 none, 1 from the pack, 2 from the weapon bar
	FVector2D DragStart = FVector2D::ZeroVector; bool bDragging = false; bool bUIRelease = false;
	void ProcessDrag(AKnightCharacter* Knight);
	void DrawDragGhost();
	// The "drop how many?" box (right-click a stack in the pack, or drag a stack out onto the world)
	bool bDropOpen = false; int32 DropSlot = -1, DropMax = 0, DropId = 0; FString DropText; bool bRightWasDown = false, bUIRightClick = false;
	void OpenDropBox(int32 Slot, int32 Id, int32 Max);
	void DrawDropBox(AKnightCharacter* Knight);
	// Sharp text: the canvas draws a font at its own size and then scales the picture, which blurs big text. These are
	// copies of the medium font rasterised at 2x, 3x and 4x; HDText picks the one that needs the least scaling.
	UPROPERTY() UFont* HDFonts[4] = {};
	UFont* PickFont(UFont* Font, float& Scale) const;
	void HDText(const FString& Text, const FLinearColor& Color, float X, float Y, UFont* Font = nullptr, float Scale = 1.f);
	void HDTextSize(const FString& Text, float& OutW, float& OutH, UFont* Font = nullptr, float Scale = 1.f) const;
	void ItemBadges(int32 Id, float X, float Y, float S);   // small red (offence) and blue (defence) numbers on an item icon
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
	float IconTiltDeg = 0.f;                   // lean applied to everything Poly draws (the toolbar icons)
	FVector2D IconPivot = FVector2D::ZeroVector;
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

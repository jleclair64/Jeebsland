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
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	// Typing on the starting screen (a small Slate widget passes the keys here: see KnightHUDMenu.cpp)
	void TypeChar(TCHAR C);
	bool TypeKey(const FKey& Key);

	// Chat (KnightHUDChat.cpp): Enter opens the typing line, Enter sends, Escape cancels. The same little Slate widget as the
	// starting screen catches the keys. AddChatLine is called when the server relays a message to this player.
	void AddChatLine(const FString& From, const FString& Text, bool bWorld, AActor* Speaker);
	bool ChatKey(const FKey& Key);
	void ChatChar(TCHAR C);
	bool IsChatOpen() const { return bChatOpen; }
	void ShotOpenLook() { SettingsTab = 5; bSettingsWasOpen = true; }   // (test pictures: -ShotLookPanel)
	void OpenQuestLog() { SettingsTab = 6; bSettingsWasOpen = true; }   // the Escape screen's QUESTS tab (call with the screen open)
	void TestAskTravel(int32 Town) { SettingsTab = 0; TravelAsk = Town; }   // (test pictures and online tests only: FastTravelTest.cpp)

	// The line at the bottom of the store / smithy screen ("Bought ...", "Your pack is full.")
	void SetShopMsg(const FString& Text, float Seconds) { ShopMsg = Text; ShopMsgUntil = GetWorld() ? GetWorld()->GetTimeSeconds() + Seconds : 0.f; }

private:
	// The starting screen (KnightHUDMenu.cpp): name and password, the worlds and their player counts, and the server address
	void DrawNoKnight();
	void DrawStartScreen();
	void DrawTextField(float X, float Y, float W, const TCHAR* Label, const FString& Value, int32 Field, bool bSecret);
	void TryPlay();
	void HideTyping();
	void EnsureTyping();   // puts the typing widget on screen and gives it the keyboard
	TSharedPtr<class SJeebTyping> Typing;

	// Chat
	struct FChatLine { FString From, Text; bool bWorld = false; double Born = 0.0; };
	struct FChatBubble { TWeakObjectPtr<AActor> Who; FString Text; double Until = 0.0; };
	TArray<FChatLine> ChatLog;
	TArray<FChatBubble> ChatBubbles;
	bool bChatOpen = false;
	FString ChatText;
	void DrawChat(AKnightCharacter* Knight, APlayerController* PC);
	void OpenChat();
	void CloseChat();
	void SubmitChat();
	void ChatNote(const FString& Text) { AddChatLine(FString(), Text, false, nullptr); }
	void WrapChat(const FString& Text, float MaxW, float Scale, TArray<FString>& Out) const;
	// Emotes (2026-11-18, KnightHUDEmotes.cpp): B opens the wheel; /wave etc. in chat
	void DrawEmoteWheel(AKnightCharacter* Knight, APlayerController* PC);
	void CloseEmoteWheel(AKnightCharacter* Knight);
	bool EmoteChat(const FString& Cmd);   // (a chat command naming an emote: played, true)
	bool bEmoteWheel = false, bEmoteCursor = false;
	FString TypeName, TypePass, TypePass2, TypeAddr;
	int32 TypeFocus = 0;              // 0 name, 1 password, 2 password again, 3 server address
	bool bTypeInit = false, bTypeEnter = false, bCreateMode = false;
	int32 PickedWorld = 0;
	FString LocalNote;                // a problem found before asking the server ("Passwords don't match.")

	// Drawing helpers
	void Poly(const TArray<FVector2D>& P, const FLinearColor& C);
	void Disc(float Cx, float Cy, float Rx, float Ry, const FLinearColor& C);
	void DrawIcon(int32 Id, float X, float Y, float S);
	void DrawIconIn(int32 Id, float X, float Y, float S, float CX, float CY, float CW, float CH);   // the icon plus the class frame round its cell
	void ClassFrame(int32 Id, float X, float Y, float W, float H);
	void DrawIconBody(int32 Id, float X, float Y, float S);
	void DrawIngot(float X, float Y, float S);
	float Wrapped(const FString& Text, float X, float Y, float MaxW, float Scale, const FLinearColor& Col);   // returns the height used
	bool Over(float X, float Y, float W, float H) const { return bUIHaveMouse && UIMouseX >= X && UIMouseX <= X + W && UIMouseY >= Y && UIMouseY <= Y + H; }
	bool Btn(float X, float Y, float W, float H, const FString& Label, bool bEnabled = true, bool bActive = false, float Scale = 1.05f);
	bool CloseBtn(float PX, float PY, float PW);
	void SetTip(const TArray<FString>& Lines) { TipLines = Lines; }
	void ItemTip(int32 Id);
	void DrawTip();
	// Place names and the game's name (owner, 2026-11-06, after Skyrim's): the game's own font in widely spaced capitals, warm ivory with a soft shadow, a thin fading rule either side. Centred; its capitals' top at Y, H tall.
	void TitleBanner(const FString& Text, float Y, float H, float Alpha);
	// The same lettering at X (X < 0 centres it across the screen); returns its width.
	float TitleTextAt(const FString& Text, float X, float Y, float H, float Alpha = 1.f);
	// A window title in that lettering, sized to stand in for HDText at Scale with the medium font (top-left at X, Y)
	float WindowTitle(const FString& Text, float X, float Y, float Scale) { return TitleTextAt(Text, X, Y + 3.f * Scale, 12.f * Scale); }
	UPROPERTY() UFont* TitleFonts[3] = {};   // rasters big enough for titles (HDFonts stop at 40)
	void DrawLinesAt(const TArray<FString>& Lines, float CX, float BottomY, float Alpha, int32 IconId = 0);   // a tooltip-style panel centred over a point (dropped things)
	static FLinearColor Lift(const FLinearColor& C, float F);
	// Liquid-glass drawing: rounded translucent panels, tiles and gauges with a sheen and a rim of light
	UPROPERTY() UTexture2D* QuarterTex = nullptr;
	UPROPERTY() UTexture2D* RingTex[2] = {};
	void Outline(float X, float Y, float W, float H, float R, const FLinearColor& Col);
	// (2026-11-18 review) Big titles (TitleBanner, centred TitleTextAt) note how strong and how low they reach this frame; the next
	// frame fades the world's marks (name tags, quest marks, the E prompt, labels) that would sit under them. UnderTitle: the factor
	// for a mark at screen height Y
	float TitleCoverA = 0.f, TitleCoverBottom = 0.f, NextTitleA = 0.f, NextTitleBottom = 0.f;
	void NoteTitle(float Bottom, float Alpha) { if (Alpha > 0.01f) { NextTitleA = FMath::Max(NextTitleA, Alpha); NextTitleBottom = FMath::Max(NextTitleBottom, Bottom); } }
	float UnderTitle(float Y) const { return (TitleCoverA > 0.f && Y < TitleCoverBottom + 40.f) ? 1.f - TitleCoverA : 1.f; }
	// The name tags drawn this frame (screen boxes): the E prompt steps beside a name instead of covering it
	TArray<FBox2D> TagRects;
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

	// The menu look (KnightHUDStyle.cpp, 2026-11-18): jeeb.UI.Style 0 = the old liquid glass, 1 = walnut and brass (default),
	// 2 = midnight blue and gold. Panel, Tile, Gauge, Btn, CloseBtn and the tooltip draw in it; FillRR and Outline keep their
	// corners small under styles 1 and 2 so overlays match the square panels.
	static int32 UIStyle();
	void FillRRx(float X, float Y, float W, float H, float R, TFunctionRef<FLinearColor(float)> ColorAt);   // (FillRR without the corner cap)
	void KitPanel(float X, float Y, float W, float H, float Alpha, bool bGlow);
	void KitTile(float X, float Y, float W, float H, const FLinearColor& Tint, float Alpha, bool bRim);
	void KitGauge(float X, float Y, float W, float H, float F, const FLinearColor& Fill, bool bPulse);
	bool KitBtn(float X, float Y, float W, float H, const FString& Label, bool bEnabled, bool bActive, float Scale);
	bool KitCloseBtn(float BX, float BY, float S);
	void KitFrame(float X, float Y, float W, float H, float T, const FLinearColor& Top, const FLinearColor& Side, const FLinearColor& Bottom);
	void KitFittings(float X, float Y, float W, float H, float S, const FLinearColor& Col);
	void KitGrain(float X, float Y, float W, float H, float Alpha);
	void EnsureKitTex();
	UPROPERTY() UTexture2D* GrainTex = nullptr;
	UPROPERTY() UTexture2D* FittingTex = nullptr;
	void CheckBox(float X, float Y, float S, bool bOn);   // a tick box (its top left, its size)
	void SliderBar(float X, float SY, float W, float T, const FLinearColor& Fill);   // the track at SY + 4 (8 tall), the knob over it
	void MapFrame(float X, float Y, float S);   // the frame round the world map
	FLinearColor MapFogTint() const;
	FLinearColor TipLineColor(const FString& Line, bool bFirst, FString& OutText) const;   // a tooltip line's colour (a class line is coloured)
	// Class outlines on item icons (Kinds.csv Class): a coloured edge round the picture
	float IconRim = 0.f;
	FLinearColor IconRimCol = FLinearColor::White;
	static FLinearColor ClassColor(int32 Class);
	void ShotUISequence(AKnightCharacter* Knight);   // (test pictures, not in Shipping: -JeebShot -ShotUI, every screen in turn; KnightHUDShotUI.cpp)
	void ShotUIMenu();                               // (test pictures: -ShotUIMenu, the start screen in each style)
	int32 ShotUIStage = -1; float ShotUIAt = 0.f; bool bShotUITaken = false;

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
	// The wizard's spells (2026-11-22, KnightHUDSpells.cpp): the spell cell on the weapon bar (F), the APPAREL / SPELL BOOK tabs above the
	// inventory, and the spell book page drawn where the apparel window goes. True from DrawSpellTabs: the book is open (skip the apparel).
	void DrawSpellCell(AKnightCharacter* Knight, float X, float Y, float S, float Step);   // the active row's three spells
	bool DrawSpellTabs(AKnightCharacter* Knight, float X, float Y, float W);
	void DrawSpellBook(AKnightCharacter* Knight, float X, float Y, float W, float H);
	bool bSpellBookTab = false;
	float SpellTabsTop = 0.f;
public:
	void ShotOpenSpellBook() { bSpellBookTab = true; }   // (test pictures)
private:
	float PanelTop = 0.f;
	bool bGreyGlass = false;   // while set, Panel and Tile draw a dimmer, smoky-grey glass (the side panel)
	int32 AreaIn = -1; float AreaBannerT = -100.f; FString AreaName;   // the other towns' names on walking in (Oak Hill, Steele Creek, the placeholder sites: OpenWorld.h)
	// PvP zones (owner 2026-11-23): the red title on walking in and the small icon at the top of the screen while inside (KnightHUDPvp.cpp)
	int32 PvpIn = -1; float PvpBannerT = -100.f; FString PvpTitle;
	void DrawPvp(const FVector& Where, float Now);
#if !UE_BUILD_SHIPPING
	friend struct FPvpTestDriver;   // (PvpTest.cpp)
#endif
	TOptional<FLinearColor> TitleTint;   // (while set, TitleBanner and TitleTextAt letter in this colour instead of ivory)
	// Drag and drop between the pack (side panel) and the weapon bar
	float InvCellX[32] = {}, InvCellY[32] = {}, InvCellS = 0.f; int32 InvCellCount = 0;   // where the pack cells were drawn this frame
	float HotCellX[5] = {}, HotCellY[5] = {}, HotCellS = 0.f; bool bHotCells = false;
	float RectX[12] = {}, RectY[12] = {}, RectW[12] = {}, RectH[12] = {}; int32 RectCount = 0;   // the windows on screen this frame
	void AddWindowRect(float X, float Y, float W, float H) { if (RectCount < 12) { RectX[RectCount] = X; RectY[RectCount] = Y; RectW[RectCount] = W; RectH[RectCount] = H; ++RectCount; } }
	float InvIngotX = 0.f, InvIngotY = 0.f;   // the fixed ingot cell
	float WornCellX[10] = {}, WornCellY[10] = {}, WornCellS = 0.f; int32 WornKind[10] = {}; bool WornSecond[10] = {}; int32 WornCellCount = 0;   // the armour slots drawn this frame
	float PotionCellX = 0.f, PotionCellY = 0.f; bool bPotionCell = false;   // the potion slot
	float DefCellX = 0.f, DefCellY = 0.f; bool bDefCell = false;   // the shield slot at the end of the weapon bar
	// The swap popup (owner, 2026-11-05): a click on a slot of the bottom bar lists what in your pack could go there, just above it
	float BarCellX[5] = {}, BarCellY = 0.f;                        // the bar's slots: 0 hand, 1 shield, 2 bow, 3 arrows, 4 potion
	int32 SwapSlot = -1;                                           // the slot whose list is open
	void DrawSwapPopup(AKnightCharacter* Knight);
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
	void DrawReticle(AKnightCharacter* Knight);   // the bow's aim, while drawing
	void DrawCastReticle(AKnightCharacter* Knight);   // the wand's and staff's aim, up and right of the middle (KnightHUDReticle.cpp)
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
	int32 SmithStyle = 0;   // the smithy's style picker: armour classic/light/heavy, shields kite/round/tower
	FString ShopMsg;
	float ShopMsgUntil = 0.f;

	// Settings screen
	int32 SettingsTab = 0;       // 0 = map, 1 = settings, 2 = controls, 3 = dictionary, 4 = lore, 5 = look (not in Shipping), 6 = quests
	// The LOOK tab (KnightHUDLook.cpp)
	void DrawLookTab(float PX, float PY, float PW, float PH);
	// Quests (KnightHUDQuests.cpp): the marks over quest givers, the tracker under the frame rate, "QUEST COMPLETE", the J key,
	// the QUESTS tab of the Escape screen (SettingsTab 6) and the map's marker for the tracked quest
	void DrawQuestHUD(AKnightCharacter* Knight, APlayerController* PC);
	void DrawQuestTitle(AKnightCharacter* Knight);   // ("QUEST COMPLETE": drawn after the world's marks and prompts, so on top of them)
	void DrawQuestLog(AKnightCharacter* Knight, float PX, float PY, float PW, float PH);
	void DrawQuestMapMark(AKnightCharacter* Knight, const FVector2D& Screen, float Size);
	FName QuestSel;
	void LookTypeChar(TCHAR C);
	void LookTypeKey(const FKey& Key);
	int32 LookGroup = 0;
	bool bLookTyping = false;
	FString LookName;
	bool bSettingsWasOpen = false;
	float SavedNoteUntil = 0.f;  // "Saved" shows on the button for a moment
	// The world map (KnightHUDMap.cpp): painted once into a texture; north (+X) is up
	UPROPERTY() UTexture2D* MapTex = nullptr;
	UPROPERTY() UTexture2D* FogTex = nullptr;   // map fog over unexplored ground, repainted when the explored grid changes
	uint32 FogHash = 0;
	// Frame rate counter (Settings checkbox, remembered in the user settings)
	bool bShowFps = false, bFpsLoaded = false;
	float FpsSmooth = 0.f;
	void DrawFps();
	FVector2D MapMin = FVector2D::ZeroVector;
	float MapSize = 1.f;
	void BuildMapTex();
	// X, Y, S: where on screen. With the cursor over it the wheel zooms toward the cursor and a left drag pans (both the Tab
	// screen's map and the Escape screen's share one view). Zoomed in, roads and Oak Hill's buildings are drawn on top.
	void DrawWorldMap(AKnightCharacter* Knight, float X, float Y, float S);
	// Fast travel (KnightHUDTravel.cpp): the gold markers on visited towns, the "Travel to ...?" box, the count, the DISCOVERED title, the fade
	int32 TravelMarkerAt(AKnightCharacter* Knight, float X, float Y, float S, const FVector2D& Mouse) const;
	void DrawTravelMarkers(AKnightCharacter* Knight, float X, float Y, float S, bool bLive);
	void DrawTravelConfirm(AKnightCharacter* Knight);
	void DrawTravelHUD(AKnightCharacter* Knight, APlayerController* PC);
	void DrawTravelFade(AKnightCharacter* Knight);
	int32 TravelAsk = -1;            // the town the box asks about, or -1
	uint64 TravelMapFrame = 0;       // the last frame the map was drawn (the box goes when the map does)
	float MapZoom = 1.f;                                   // 1 = the whole world
	FVector2D MapView = FVector2D::ZeroVector;             // the world point (cm) at the map's middle
	bool bMapPanning = false;
	FVector2D MapPanMouse = FVector2D::ZeroVector, MapPanView = FVector2D::ZeroVector;
	// Oak Hill close up: a finer picture of the land round the town (painted the first time you zoom in), and its box in the world
	UPROPERTY() UTexture2D* TownTex = nullptr;
	FVector2D TownMin = FVector2D::ZeroVector; float TownSize = 1.f;
	void BuildTownTex();
	// The ground's heights and river distances for both pictures are sampled a few rows a frame from the moment the knight is in
	// the world (TickMapSampling, about 2 ms a frame), so neither picture stops the game when it is first shown: a picture still
	// unfinished when it is needed finishes its rows at once.
	struct FMapSamples
	{
		FVector2D Min = FVector2D::ZeroVector; float Size = 1.f; int32 N = 0, Row = 0;
		TArray<float> H, RD, Half;
		bool Started() const { return N > 0; }
		bool Done() const { return N > 0 && Row >= N; }
		void Start(const FVector2D& InMin, float InSize, int32 InN);
		void Step(double Seconds);   // samples rows for this long (everything left when Seconds is huge)
		FVector2D At(int32 U, int32 V) const { const float Cell = Size / N; return FVector2D(Min.X + (N - 1 - V + 0.5f) * Cell, Min.Y + (U + 0.5f) * Cell); }   // (north up)
	};
	FMapSamples WorldSamples, TownSamples;
	void TickMapSampling();
	// TEMPORARY test kit (owner, 2026-11-13; JeebTestKit.h): every item that can be worn or held, and a box setting every skill
	// to one level. Only drawn when the server says this player is a tester (AKnightPlayerController::bTestKit).
	void DrawTestKit(AKnightCharacter* Knight, float X, float Y, float MaxW, float MaxH);
	int32 TestPage = 0;
	bool bTestKitOpen = false;
	bool bLevelTyping = false;
	FString TestLevelText = TEXT("99");
	void SubmitLevel();
	int32 BankPage = 0;
	int32 BankDragHot = -1;   // a weapon being dragged from the weapon bar onto the bank
	int32 LoreCat = 0;
	int32 LoreSel = -1;
	int32 DictEntry = 0;
	int32 DraggingSlider = -1;   // which slider the mouse is holding (-1 = none)
	bool bWasMouseDown = false;
};

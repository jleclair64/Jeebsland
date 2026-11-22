#include "Bridge.h"
#include "Terrain.h"
#include "ProceduralMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/PointLightComponent.h"
#include "LampLook.h"

// ---------------------------------------------------------------------------------------------------------------------------------
// The numbers. Measured on the model (Tools/bridge_profile.py, a ray cast along its middle) at the 1400 cm-per-unit scale it is imported at
// (Tools/bridge_clean.py): deck and soffit heights over the pier bottoms, in cm, L from the middle of the span.
// ---------------------------------------------------------------------------------------------------------------------------------
namespace
{
	constexpr float CentreX = -3550.f;      // the west road's line (JeebOakHill::PosX: the plaza's northing)
	constexpr float CentreY = 12110.f;      // the river's middle where that line crosses it (the log JEEBBRIDGE river line checks it)
	constexpr float BaseZ = -70.f;          // world Z of the pier bottoms (the water is at -20)
	constexpr float WallW = 343.f;          // half the model's width over the parapets
	constexpr float RailH = 80.f;           // the collision parapet over the deck (the model's is about 70 cm): mantle height is 60..145
	constexpr float SlabCap = 1260.f;       // |L| where the collision deck ends (the model's deck runs to about 1330)
	constexpr float ModelEnd = 1332.f;

	// (L, deck height over the pier bottoms)
	const float Deck[][2] = {
		{-1246,203},{-1179,211},{-1113,236},{-1046,263},{-980,284},{-914,296},{-847,309},{-781,321},{-714,332},{-648,343},{-581,355},{-515,366},{-448,376},
		{-382,385},{-315,390},{-249,390},{-182,389},{-116,390},{-49,391},{17,391},{84,389},{150,389},{217,390},{283,391},{350,387},{416,380},{483,368},
		{549,358},{616,350},{682,338},{749,326},{815,315},{882,305},{948,290},{1015,272},{1081,247},{1148,225},{1214,203} };
	// (L, the underside's height over the pier bottoms): 0 on the piers, the arches' curves, 79 under the end masses
	const float Soffit[][2] = {
		{-1330,0},{-1295,79},{-1260,79},{-1190,79},{-1120,79},{-1050,79},{-980,79},{-910,79},{-840,79},{-805,88},{-770,159},{-735,206},{-700,226},{-665,246},{-630,255},
		{-595,256},{-560,246},{-525,235},{-490,206},{-455,177},{-420,104},{-385,0},{-350,0},{-315,0},{-280,0},{-245,0},{-210,1},{-175,190},{-140,231},{-105,266},
		{-70,280},{-35,294},{0,297},{35,295},{70,282},{105,266},{140,231},{175,190},{210,1},{245,0},{280,0},{315,1},{350,1},{385,1},{420,115},{455,189},{490,214},
		{525,239},{560,247},{595,253},{630,249},{665,240},{700,216},{735,193},{770,131},{805,79},{840,79},{910,79},{980,79},{1050,79},{1120,79},{1190,79},{1260,79},{1295,79},{1330,0} };
	constexpr float PierL = 280.f;          // the piers' middles (|L|)

	template <int N> float Lookup(const float (&T)[N][2], float L)
	{
		if (L <= T[0][0]) return T[0][1];
		for (int i = 0; i + 1 < N; ++i)
			if (L <= T[i + 1][0]) return T[i][1] + (T[i + 1][1] - T[i][1]) * (L - T[i][0]) / FMath::Max(T[i + 1][0] - T[i][0], 1.f);
		return T[N - 1][1];
	}
	// the model's deck over the pier bottoms, run on past its table's ends down the end ramps
	float DeckRel(float L)
	{
		const float A = FMath::Abs(L);
		if (A <= 1214.f) return Lookup(Deck, L);
		return 203.f - 0.33f * (A - 1214.f);
	}
	bool bGroundOff = false;   // the probe reads the natural ground with it off
}

FVector2D JeebBridge::Centre() { return FVector2D(CentreX, CentreY); }
float JeebBridge::DeckEndL() { return SlabCap; }
float JeebBridge::DeckZ(float L) { return BaseZ + DeckRel(L); }

// ---------------------------------------------------------------------------------------------------------------------------------
// The ground (JeebTerrain::HeightAt ends with this): an embankment at each end so the walkway meets the land (the deck there is a little higher
// than the banks, and the banks beyond it are brought up to it and eased down over a few metres), and a stone footing under each pier on the
// river bed (the bed is 1.5 m under the pier bottoms). Only ever raises the ground.
// ---------------------------------------------------------------------------------------------------------------------------------
float JeebBridge::ApplyGround(float X, float Y, float H)
{
	if (bGroundOff || !Enabled()) return H;
	const float W = FMath::Abs(X - CentreX), L = Y - CentreY, A = FMath::Abs(L);
	if (W > 1000.f || A > 2600.f) return H;
	float Out = H;
	// the embankment: the ground rises to the deck's own height (8 cm under it) over the last 2.5 m before the model's end, and goes on beyond the end
	// at about 3.5 degrees, down to the land
	if (A > 1000.f)
	{
		const float Lat = 1.f - FMath::SmoothStep(380.f, 950.f, W);   // full over the road, gone 9.5 m from the middle
		const float Along = FMath::SmoothStep(1000.f, 1250.f, A);
		const float T = BaseZ + (A <= ModelEnd ? DeckRel(A) : DeckRel(ModelEnd) - 0.06f * (A - ModelEnd)) - 8.f;
		if (T > Out) Out += (T - Out) * Lat * Along;
	}
	// the footings
	{
		const float Dl = (A - PierL) / 310.f, Dw = W / 470.f;
		const float R = FMath::Sqrt(Dl * Dl + Dw * Dw);
		if (R < 1.f)
		{
			const float Foot = BaseZ - 12.f;
			if (Foot > Out) Out += (Foot - Out) * FMath::SmoothStep(1.f, 0.35f, R);
		}
	}
	return Out;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The roads: the old village site's east side to the bridge's west end, and the span itself (no ribbon: the deck is the road)
// ---------------------------------------------------------------------------------------------------------------------------------
const TArray<JeebBridge::FRoad>& JeebBridge::Roads()
{
	static const TArray<FRoad> R = []()
	{
		TArray<FRoad> A;
		if (!Enabled()) return A;
		FRoad West;
		West.Kind = 3; West.Width = 300.f;
		West.Pts = { FVector2D(-2900.f, 1700.f), FVector2D(-2300.f, 2300.f), FVector2D(-1800.f, 3000.f), FVector2D(-1500.f, 4200.f), FVector2D(-1500.f, 5300.f), FVector2D(-1900.f, 6500.f),
			FVector2D(-2600.f, 7800.f), FVector2D(-3300.f, 9000.f), FVector2D(CentreX, 10100.f), FVector2D(CentreX, CentreY - 2300.f), FVector2D(CentreX, CentreY - ModelEnd) };
		A.Add(West);
		FRoad Span;
		Span.Kind = 4; Span.Width = 400.f;
		Span.Pts = { FVector2D(CentreX, CentreY - ModelEnd), FVector2D(CentreX, CentreY), FVector2D(CentreX, CentreY + ModelEnd) };
		A.Add(Span);
		return A;
	}();
	return R;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The actor
// ---------------------------------------------------------------------------------------------------------------------------------
ABridge::ABridge()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	bReplicates = false;
}

void ABridge::BeginPlay()
{
	Super::BeginPlay();
	const double T0 = FPlatformTime::Seconds();
	SetActorLocationAndRotation(FVector(CentreX, CentreY, BaseZ), FRotator(0.f, 90.f, 0.f));   // (the model's length is its X: it runs east-west)
	const bool bServer = GetNetMode() == NM_DedicatedServer;

	if (!bServer)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Bridge/SM_Bridge.SM_Bridge"));
		if (Mesh)
		{
			Visual = NewObject<UStaticMeshComponent>(this, TEXT("BridgeModel"));
			Visual->SetStaticMesh(Mesh);
			Visual->SetupAttachment(RootComponent);
			Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Visual->SetCanEverAffectNavigation(false);
			Visual->SetCastShadow(true);
			Visual->RegisterComponent();
		}
		else UE_LOG(LogTemp, Warning, TEXT("JEEBBRIDGE no model at /Game/Bridge/SM_Bridge (collision only)"));
	}
	BuildCollision();
	if (!bServer) BuildDecor();
	if (FParse::Param(FCommandLine::Get(), TEXT("BridgeWalkLog")) || FParse::Param(FCommandLine::Get(), TEXT("BridgeWalkNet")) || FParse::Param(FCommandLine::Get(), TEXT("BridgeWalkTest"))) SetActorTickEnabled(true);

#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("BridgeProbe")))
	{
		using namespace JeebWorld;
		// the river where the line crosses it
		float BestY = 0.f, BestD = 1.0e9f, Half = 0.f;
		for (float Y = CentreY - 3000.f; Y < CentreY + 3000.f; Y += 10.f) { float Hw; const float D = RiverDistance(CentreX, Y, Hw); if (D < BestD) { BestD = D; BestY = Y; Half = Hw; } }
		float Hw1, Hw2;
		const float D1 = RiverDistance(CentreX - 500.f, BestY, Hw1), D2 = RiverDistance(CentreX + 500.f, BestY, Hw2);
		UE_LOG(LogTemp, Display, TEXT("JEEBBRIDGE river line: middle at Y %.0f (distance %.0f, half width %.0f; set CentreY %.0f); distance 5 m north %.0f, 5 m south %.0f"), BestY, BestD, Half, CentreY, D1, D2);
		bGroundOff = true;
		for (float W : { 0.f, 300.f, 600.f })
		{
			FString S;
			for (float L = -2400.f; L <= 2400.f; L += 150.f) S += FString::Printf(TEXT(" %.0f"), JeebTerrain::HeightAt(CentreX + W, CentreY + L));
			UE_LOG(LogTemp, Display, TEXT("JEEBBRIDGE natural ground at W=%.0f, L -2400..2400 step 150:%s"), W, *S);
		}
		// the old village road: its steepest grade, on the natural ground and with the bridge's
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			bGroundOff = Pass == 0;
			float Worst = 0.f, WorstAt = 0.f, Len = 0.f, NextPrint = 0.f;
			FString Hs;
			const TArray<FVector2D> Line = JeebTerrain::SmoothPath(JeebBridge::Roads()[0].Pts, false);
			for (int32 i = 0; i + 1 < Line.Num(); ++i)
			{
				const float Dz = JeebTerrain::HeightAt(Line[i + 1].X, Line[i + 1].Y) - JeebTerrain::HeightAt(Line[i].X, Line[i].Y), Dl = FVector2D::Distance(Line[i], Line[i + 1]);
				Len += Dl;
				if (Pass == 0 && Len >= NextPrint) { NextPrint += 500.f; Hs += FString::Printf(TEXT(" %.0f(%.0f,%.0f)"), JeebTerrain::HeightAt(Line[i].X, Line[i].Y), Line[i].X, Line[i].Y); }
				const float Deg = FMath::RadiansToDegrees(FMath::Atan2(FMath::Abs(Dz), Dl));
				if (Deg > Worst) { Worst = Deg; WorstAt = Len; }
			}
			if (Pass == 0) UE_LOG(LogTemp, Display, TEXT("JEEBBRIDGE old village road heights every 5 m:%s"), *Hs);
			UE_LOG(LogTemp, Display, TEXT("JEEBBRIDGE old village road %s: %.0f m long, steepest %.1f degrees at %.0f m"), Pass == 0 ? TEXT("natural") : TEXT("with the bridge's ground"), Len / 100.f, Worst, WorstAt / 100.f);
		}
		bGroundOff = true;
		for (float Gx = -6500.f; Gx <= -1500.f; Gx += 500.f) { FString Row; for (float Gy = 500.f; Gy <= 11500.f; Gy += 500.f) Row += FString::Printf(TEXT(" %.0f"), JeebTerrain::HeightAt(Gx, Gy)); UE_LOG(LogTemp, Display, TEXT("JEEBBRIDGE grid X %.0f, Y 500..11500 step 500:%s"), Gx, *Row); }
		bGroundOff = false;
		FString S;
		for (float L = -2400.f; L <= 2400.f; L += 150.f) S += FString::Printf(TEXT(" %.0f"), JeebTerrain::HeightAt(CentreX, CentreY + L));
		UE_LOG(LogTemp, Display, TEXT("JEEBBRIDGE ground with the bridge at W=0:%s"), *S);
		S.Empty();
		for (float L = -1250.f; L <= 1250.f; L += 250.f) S += FString::Printf(TEXT(" %.0f/%.0f"), JeebBridge::DeckZ(L), JeebTerrain::HeightAt(CentreX, CentreY + L));
		UE_LOG(LogTemp, Display, TEXT("JEEBBRIDGE deck / ground every 2.5 m:%s"), *S);
	}
#endif
	UE_LOG(LogTemp, Log, TEXT("JEEBBRIDGE bridge at %.0f %.0f z %.0f: %d parapet boxes, model %s, in %.1f ms%s"), CentreX, CentreY, BaseZ, Rails.Num(), Visual ? TEXT("yes") : TEXT("no"),
		(FPlatformTime::Seconds() - T0) * 1000.0, bServer ? TEXT(" (server: collision only)") : TEXT(""));
}

void ABridge::BuildCollision()
{
	// ---- the deck and the arches: a closed solid, local frame (X along the bridge, Y across, Z up from the pier bottoms), a station every 35 cm
	TArray<FVector> V;
	TArray<int32> Tri;
	TArray<float> Ls;
	for (float L = -SlabCap; L < SlabCap; L += 35.f) Ls.Add(L);
	Ls.Add(SlabCap);
	const int32 N = Ls.Num();
	auto Top = [](float L) { return DeckRel(L) - 1.f; };
	auto Bot = [](float L) { return FMath::Max(Lookup(Soffit, L), 0.f); };
	// vertices: per station TL, TR, BL, BR (left = -Y)
	for (int32 i = 0; i < N; ++i)
	{
		const float L = Ls[i];
		V.Add(FVector(L, -WallW, Top(L))); V.Add(FVector(L, WallW, Top(L)));
		V.Add(FVector(L, -WallW, Bot(L))); V.Add(FVector(L, WallW, Bot(L)));
	}
	auto Quad = [&](int32 A, int32 B, int32 C, int32 D, const FVector& Out)   // A, B, C, D in order round the quad; wound so Unreal's front faces Out
	{
		auto Tr = [&](int32 P, int32 Q, int32 R)
		{
			const FVector Nrm = FVector::CrossProduct(V[Q] - V[P], V[R] - V[P]);
			if (FVector::DotProduct(Nrm, Out) > 0.f) { Tri.Add(P); Tri.Add(R); Tri.Add(Q); } else { Tri.Add(P); Tri.Add(Q); Tri.Add(R); }
		};
		Tr(A, B, C); Tr(A, C, D);
	};
	for (int32 i = 0; i + 1 < N; ++i)
	{
		const int32 a = i * 4, b = (i + 1) * 4;   // TL TR BL BR
		Quad(a + 0, a + 1, b + 1, b + 0, FVector(0, 0, 1));    // top
		Quad(a + 2, a + 3, b + 3, b + 2, FVector(0, 0, -1));   // underside
		Quad(a + 0, b + 0, b + 2, a + 2, FVector(0, -1, 0));   // the -Y side
		Quad(a + 1, b + 1, b + 3, a + 3, FVector(0, 1, 0));    // the +Y side
	}
	Quad(0, 1, 3, 2, FVector(-1, 0, 0));
	{ const int32 e = (N - 1) * 4; Quad(e + 0, e + 1, e + 3, e + 2, FVector(1, 0, 0)); }
	Slab = NewObject<UProceduralMeshComponent>(this, TEXT("BridgeSlab"));
	Slab->SetupAttachment(RootComponent);
	Slab->bUseAsyncCooking = false;   // (the knight may be on it the moment it exists)
	Slab->RegisterComponent();
	TArray<FVector> Normals; TArray<FVector2D> UV; TArray<FLinearColor> Col;
	Slab->CreateMeshSection_LinearColor(0, V, Tri, Normals, UV, Col, TArray<FProcMeshTangent>(), true);
	Slab->SetCollisionProfileName(TEXT("BlockAll"));
	Slab->SetVisibility(false);
	Slab->SetHiddenInGame(true);
	Slab->SetCastShadow(false);

	// ---- the parapets: a box about 3 m long on each side, following the deck (inner face at the walkway's edge, 80 cm over the deck, 40 cm into it)
	constexpr int32 Segs = 9;
	for (int32 i = 0; i < Segs; ++i)
	{
		const float L0 = -SlabCap + 2.f * SlabCap * i / Segs, L1 = -SlabCap + 2.f * SlabCap * (i + 1) / Segs, Z0 = DeckRel(L0), Z1 = DeckRel(L1);
		const float Len = FMath::Sqrt(FMath::Square(L1 - L0) + FMath::Square(Z1 - Z0));
		const float Pitch = FMath::RadiansToDegrees(FMath::Atan2(Z1 - Z0, L1 - L0));
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			UBoxComponent* B = NewObject<UBoxComponent>(this);
			B->SetupAttachment(RootComponent);
			B->SetBoxExtent(FVector(Len * 0.5f + 6.f, (WallW - JeebBridge::DeckHalfWidth) * 0.5f, (RailH + 40.f) * 0.5f));
			B->SetRelativeLocation(FVector((L0 + L1) * 0.5f, Side * (JeebBridge::DeckHalfWidth + WallW) * 0.5f, (Z0 + Z1) * 0.5f + (RailH - 40.f) * 0.5f - 1.f));
			B->SetRelativeRotation(FRotator(Pitch, 0.f, 0.f));
			B->SetCollisionProfileName(TEXT("BlockAll"));
			B->SetCanEverAffectNavigation(false);
			B->SetHiddenInGame(true);
			B->RegisterComponent();
			Rails.Add(B);
		}
	}
	// ---- the four round posts where the parapets start (2026-11-18 night, owner: he mantled onto the 80 cm parapet box straight through one).
	// Measured on the model (Blender ray casts of Saved/KnightBuild/Bridge/Bridge.fbx): centred at |L| 1000 and 295 across, about 55 cm round,
	// tops 443 cm over the pier bottoms (about 1.7 m over the deck: too tall to mantle). Each is a solid upright capsule, tagged so it is never mantled,
	// and the mantle onto the parapet beside one finds it in the way (PlanMantle's face trace, or no room on top).
	for (int32 End = -1; End <= 1; End += 2)
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			constexpr float PostL = 1000.f, PostW = 295.f, PostR = 56.f, PostTop = 443.f;
			const float Bottom = DeckRel(End * PostL) - 30.f;
			UCapsuleComponent* C = NewObject<UCapsuleComponent>(this);
			C->SetupAttachment(RootComponent);
			C->SetCapsuleSize(PostR, FMath::Max((PostTop - Bottom) * 0.5f, PostR));
			C->SetRelativeLocation(FVector(End * PostL, Side * PostW, (PostTop + Bottom) * 0.5f));
			C->SetCollisionProfileName(TEXT("BlockAll"));
			C->SetCanEverAffectNavigation(false);
			C->SetHiddenInGame(true);
			C->ComponentTags.Add(FName(TEXT("JeebNoMantle")));
			C->RegisterComponent();
		}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Decoration (2026-11-18, owner: "torches or flowers on this bridge"): separate TownDecor props sat on the model's parapets, never baked
// into it. Everything here is a fixed list in the bridge's own frame (L along, W across, both sides), so it is the same on every machine.
// Four oil lanterns on the end posts, two lit torches on the parapets at the middle, flower boxes along both parapets. Lights: six
// shadowless points with a 5 m reach, out of sight past 60 m.
// ---------------------------------------------------------------------------------------------------------------------------------
void ABridge::BuildDecor()
{
	// The squat round posts at the four ends, as measured on the model (same as the posts' capsules above): centred at |L| 1000, 295 across,
	// tops 443 cm over the pier bottoms (2026-11-19: was 140 over the deck, which sat the lantern 30 cm inside the post, a black nub).
	constexpr float PostL = 1000.f, PostW = 295.f, PostTopAbs = 443.f, LanternH = 70.f;
	constexpr float ParapetW = 287.f, ParapetTop = 72.f;                 // the parapet's middle line and top over the deck
	auto Load = [](const TCHAR* Name) { return LoadObject<UStaticMesh>(nullptr, *(FString(TEXT("/Game/TownDecor/SM_")) + Name + TEXT(".SM_") + Name)); };
	UStaticMesh* Lantern = Load(TEXT("LanternBlack"));   // the square black lantern with glowing panes reads from 20 m; the oil lantern was a dark rusty nub
	if (!Lantern) Lantern = Load(TEXT("LanternOil"));
	UStaticMesh* Torch = Load(TEXT("TorchBlazing"));
	UStaticMesh* Flame = Load(TEXT("TorchBlazingFlame"));
	UStaticMesh* Box = Load(TEXT("FlowerBoxLong"));
	// Puts M with the bottom centre of Ref's bounds (Ref is M, or the torch for its flame, which is modelled in the torch's own space) on the
	// spot (L, W, Up over the deck), turned by Yaw and Pitch, scaled so Ref is Target cm tall (0: as modelled)
	auto Put = [&](UStaticMesh* M, UStaticMesh* Ref, float L, float W, float Up, float Yaw, float Pitch, float Target)
	{
		if (!M || !Ref) return;
		const FBox B = Ref->GetBoundingBox();
		const float S = Target > 0.f ? Target / FMath::Max(1.f, B.GetSize().Z) : 1.f;
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(M);
		C->SetupAttachment(RootComponent);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCanEverAffectNavigation(false);
		C->SetCastShadow(false);
		C->SetCachedMaxDrawDistance(7000.f);
		const FVector Foot(B.GetCenter().X, B.GetCenter().Y, B.Min.Z);
		const FQuat Q = FRotator(Pitch, Yaw, 0.f).Quaternion();
		C->SetRelativeTransform(FTransform(Q, FVector(L, W, DeckRel(L) + Up) - Q.RotateVector(Foot * S), FVector(S)));
		C->RegisterComponent();
		JeebLamp::ApplyToMesh(C, this);
		Decor.Add(C);
	};
	auto Light = [&](float L, float W, float Up, float Lumens = 110.f)
	{
		UPointLightComponent* P = NewObject<UPointLightComponent>(this);
		P->SetupAttachment(RootComponent);
		P->SetRelativeLocation(FVector(L, W, DeckRel(L) + Up));
		P->SetIntensityUnits(ELightUnits::Lumens);
		P->SetIntensity(Lumens);
		P->SetAttenuationRadius(500.f);
		P->SetCastShadows(false);
		P->MaxDrawDistance = 6000.f;
		P->MaxDistanceFadeRange = 1500.f;
		P->RegisterComponent();
		JeebLamp::TrackLight(P);
		Decor.Add(P);
	};
	for (int32 Sd = -1; Sd <= 1; Sd += 2)
	{
		for (int32 End = -1; End <= 1; End += 2)   // the lanterns on the four end posts
		{
			const float OverDeck = PostTopAbs - DeckRel(End * PostL);   // Put and Light measure up from the deck
			Put(Lantern, Lantern, End * PostL, Sd * PostW, OverDeck + 0.5f, 45.f, 0.f, LanternH);
			Light(End * PostL, Sd * PostW, OverDeck + LanternH * 0.7f, 45.f);   // (up in the lantern's roof and dimmer: lower down it lit the post's top as a white disc)
		}
		Put(Torch, Torch, 0.f, Sd * ParapetW, ParapetTop, 0.f, 0.f, 95.f);   // the torches at the middle, on the parapet
		Put(Flame, Torch, 0.f, Sd * ParapetW, ParapetTop, 0.f, 0.f, 95.f);
		Light(0.f, Sd * ParapetW, ParapetTop + 95.f);
		// the flower boxes between them, laid along the parapet's slope. 2026-11-20 (owner: "fix these flower boxes so they are sitting
		// inside of the bridge wall"): as modelled the box was wider than the parapet's top and hung over its outer edge, and the ones at
		// |L| 850 ran down onto the end ramps' bend. Now each is scaled so it is no wider than BoxMaxW (the parapet's flat top is about
		// 50 cm), its long side turned along the wall, its slope taken over its own length, sunk BoxSink into the top so its whole
		// bottom rests on stone, and the outer pair moved in to |L| 780, clear of the bend before the lantern posts.
		if (Box)
		{
			constexpr float BoxMaxW = 40.f, BoxMaxLen = 100.f, BoxSink = 3.f;
			const FBox B = Box->GetBoundingBox();
			const bool bLongY = B.GetSize().Y > B.GetSize().X;
			const float Wd = bLongY ? B.GetSize().X : B.GetSize().Y, Ln = bLongY ? B.GetSize().Y : B.GetSize().X;
			const float S = FMath::Min3(1.f, BoxMaxW / FMath::Max(Wd, 1.f), BoxMaxLen / FMath::Max(Ln, 1.f));
			for (float L : { -780.f, -560.f, -330.f, 330.f, 560.f, 780.f })
			{
				const float Half = Ln * S * 0.5f;
				const float Slope = FMath::RadiansToDegrees(FMath::Atan2(DeckRel(L + Half) - DeckRel(L - Half), 2.f * Half));
				UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
				C->SetStaticMesh(Box);
				C->SetupAttachment(RootComponent);
				C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				C->SetCanEverAffectNavigation(false);
				C->SetCastShadow(false);
				C->SetCachedMaxDrawDistance(7000.f);
				const FVector Foot(B.GetCenter().X, B.GetCenter().Y, B.Min.Z);
				const FQuat Q = FRotator(Slope, 0.f, 0.f).Quaternion() * FRotator(0.f, bLongY ? 90.f : 0.f, 0.f).Quaternion();
				const float Under = DeckRel(L);   // (the parapet top follows the deck: the box's middle sits on it, tilted with it)
				C->SetRelativeTransform(FTransform(Q, FVector(L, Sd * ParapetW, Under + ParapetTop - BoxSink) - Q.RotateVector(Foot * S), FVector(S)));
				C->RegisterComponent();
				JeebLamp::ApplyToMesh(C, this);
				Decor.Add(C);
			}
			UE_LOG(LogTemp, Log, TEXT("JEEBBRIDGE flower boxes: model %.0f x %.0f cm, drawn at %.2f: %.0f long, %.0f wide on the parapet"), Ln, Wd, S, Ln * S, Wd * S);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBBRIDGE decor: %d pieces (lantern %s, torch %s, flame %s, flower box %s)"), Decor.Num(), Lantern ? TEXT("yes") : TEXT("NO"), Torch ? TEXT("yes") : TEXT("NO"), Flame ? TEXT("yes") : TEXT("NO"), Box ? TEXT("yes") : TEXT("NO"));
}

void ABridge::Tick(float Dt)
{
	Super::Tick(Dt);
#if !UE_BUILD_SHIPPING
	static float Acc = 0.f, Clock = 0.f;
	Acc += Dt; Clock += Dt;
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	ACharacter* P = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
	if (!P) return;
	// -BridgeWalkTest: a scripted walk for the local player (test builds only): {from L, W, yaw, share, seconds}; L from the span's middle, W across
	struct FLeg { float L, W, Yaw, Share, Secs; const TCHAR* Name; };
	static const FLeg Legs[] = {
		{ -2300.f, 0.f, 90.f, 1.0f, 10.f, TEXT("run west to east") },
		{ 2300.f, 0.f, -90.f, 1.0f, 10.f, TEXT("run east to west") },
		{ -1800.f, 0.f, 90.f, 0.4f, 21.f, TEXT("walk west to east") },
		{ -400.f, 130.f, 0.f, 0.6f, 3.f, TEXT("into the north parapet (mantle)") },
		{ 500.f, -130.f, 180.f, 0.6f, 3.f, TEXT("into the south parapet (mantle)") },
	};
	static const bool bTest = FParse::Param(FCommandLine::Get(), TEXT("BridgeWalkTest"));
	static int32 Leg = -1;
	static float LegT = 0.f;
	if (bTest)
	{
		float Start = 2.f;
		int32 Want = -1;
		for (int32 i = 0; i < UE_ARRAY_COUNT(Legs); ++i) { if (Clock >= Start) Want = i; Start += Legs[i].Secs + 2.f; }
		if (Want != Leg && Want >= 0)
		{
			Leg = Want; LegT = 0.f;
			const FLeg& G = Legs[Leg];
			const float X = CentreX + G.W, Y = CentreY + G.L;
			FHitResult H;
			FCollisionQueryParams Q(SCENE_QUERY_STAT(JeebBridgeTest), true, P);
			const float Top = JeebTerrain::HeightAt(X, Y) + 700.f;
			const float Z = GetWorld()->LineTraceSingleByChannel(H, FVector(X, Y, Top), FVector(X, Y, Top - 2000.f), ECC_Pawn, Q) ? float(H.ImpactPoint.Z) : Top - 700.f;
			P->SetActorLocationAndRotation(FVector(X, Y, Z + P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 3.f), FRotator(0.f, G.Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
			P->GetCharacterMovement()->Velocity = FVector::ZeroVector;
			PC->SetControlRotation(FRotator(0.f, G.Yaw, 0.f));
			UE_LOG(LogTemp, Display, TEXT("JEEBBRIDGEWALK leg %d: %s"), Leg, G.Name);
		}
		if (Leg >= 0 && Leg < UE_ARRAY_COUNT(Legs))
		{
			LegT += Dt;
			if (LegT > 0.5f && LegT < Legs[Leg].Secs) P->AddMovementInput(FRotator(0.f, Legs[Leg].Yaw, 0.f).Vector(), Legs[Leg].Share);
		}
	}
	// -BridgeWalkNet (an online client, no teleport: he starts on Oak Hill's plaza): runs west along the road over the bridge from 8 s for 14 s, waits, runs back east for 14 s; JEEBNETFIX lines count the server's corrections
	static const bool bNet = FParse::Param(FCommandLine::Get(), TEXT("BridgeWalkNet"));
	if (bNet)
	{
		if (Clock > 8.f && Clock < 22.f) P->AddMovementInput(FVector(0.f, -1.f, 0.f), 1.f);
		if (Clock > 26.f && Clock < 40.f) P->AddMovementInput(FVector(0.f, 1.f, 0.f), 1.f);
		if (Clock > 44.f) PC->ConsoleCommand(TEXT("quit"));
	}
	if (bTest && Clock > 62.f) PC->ConsoleCommand(TEXT("quit"));
	if (Acc < 0.2f) return;
	Acc = 0.f;
	const FVector L = P->GetActorLocation(), V = P->GetVelocity();
	const float Half = P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	UE_LOG(LogTemp, Display, TEXT("JEEBBRIDGEWALK t %.1f leg %d L %.0f W %.0f feet z %.0f deck %.0f speed %.0f vz %.0f"), Clock, Leg, L.Y - CentreY, L.X - CentreX, L.Z - Half, JeebBridge::DeckZ(L.Y - CentreY), V.Size2D(), V.Z);
#endif
}

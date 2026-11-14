#include "KnightHUD.h"
#include "KnightCharacter.h"
#include "SkillComponent.h"
#include "Misc/Crc.h"
#include "JeebGameInstance.h"
#include "Terrain.h"
#include "Forest.h"
#include "ChoppableTree.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "OakHill.h"

// ---------------------------------------------------------------------------------------------------
// The world map (top right of the Tab windows, and the Map tab of the Escape screen). Kept simple on purpose: the lie of the land, the woods, the paths,
// the water, Blackrock and the castle, and an arrow for YOU. No villagers, shops or other players.
// It is painted once, from this machine's own copy of the world, into a texture.
// ---------------------------------------------------------------------------------------------------
namespace
{
	constexpr int32 MapN = 512;   // texture size

	FLinearColor Mix(const FLinearColor& A, const FLinearColor& B, float T) { return A + (B - A) * FMath::Clamp(T, 0.f, 1.f); }
}

void AKnightHUD::FMapSamples::Start(const FVector2D& InMin, float InSize, int32 InN)
{
	Min = InMin; Size = InSize; N = InN; Row = 0;
	H.SetNumUninitialized(N * N); RD.SetNumUninitialized(N * N); Half.SetNumUninitialized(N * N);
}

void AKnightHUD::FMapSamples::Step(double Seconds)
{
	const double End = FPlatformTime::Seconds() + Seconds;
	while (Row < N)
	{
		for (int32 U = 0; U < N; ++U)
		{
			const int32 I = Row * N + U;
			const FVector2D P = At(U, Row);
			H[I] = JeebTerrain::HeightAt(P.X, P.Y);
			RD[I] = JeebWorld::RiverDistance(P.X, P.Y, Half[I]);
		}
		++Row;
		if (FPlatformTime::Seconds() >= End) break;
	}
}

// A little of both pictures' sampling each frame (about 2 ms): the world's first, then Oak Hill's close-up
void AKnightHUD::TickMapSampling()
{
	if (!WorldSamples.Started())
	{
		// What the map covers: the whole world (WorldMap.h), as a square
		const FBox2D B(FVector2D(JeebWorld::MinX, JeebWorld::MinY), FVector2D(JeebWorld::MaxX, JeebWorld::MaxY));
		const float Size = FMath::Max(B.GetSize().X, B.GetSize().Y);
		WorldSamples.Start(B.GetCenter() - FVector2D(Size * 0.5f), Size, MapN);
	}
	if (!TownSamples.Started() && JeebOakHill::Enabled())
	{
		// Oak Hill close up: 360 m square round the plaza (the town, about 100 x 140 m, and its fringe of woods and river), 0.7 m a pixel
		constexpr float Size = 36000.f;
		TownSamples.Start(FVector2D(JeebOakHill::PosX, JeebOakHill::PosY) - FVector2D(Size * 0.5f), Size, 512);
	}
	constexpr double Budget = 0.002;
	if (!WorldSamples.Done()) WorldSamples.Step(Budget);
	else if (TownSamples.Started() && !TownSamples.Done()) TownSamples.Step(Budget);
}

void AKnightHUD::BuildMapTex()
{
	UWorld* W = GetWorld();
	if (!W) return;
	const double MapT0 = FPlatformTime::Seconds();

	// What the map covers: the whole world (WorldMap.h), as a square
	const FVector2D VC = JeebTerrain::VillageCenter(), CP = JeebTerrain::CastlePos();
	TArray<FVector2D> Broadleaf, Conifer;
	for (TActorIterator<AForest> It(W); It; ++It)
	{
		TArray<UInstancedStaticMeshComponent*> Comps;
		It->GetComponents(Comps);
		for (UInstancedStaticMeshComponent* C : Comps)
		{
			const bool bPine = C->GetName().StartsWith(TEXT("Pine"));
			const bool bCrown = bPine || C->GetName().StartsWith(TEXT("Leaf"));
			if (!bCrown) continue;
			for (int32 i = 0; i < C->GetInstanceCount(); ++i)
			{
				FTransform T;
				if (!C->GetInstanceTransform(i, T, true)) continue;
				(bPine ? Conifer : Broadleaf).Add(FVector2D(T.GetLocation()));
			}
		}
	}
	for (TActorIterator<AChoppableTree> It(W); It; ++It) Broadleaf.Add(FVector2D(It->GetActorLocation()));
	// (the heights and river distances: sampled a few rows a frame since the knight appeared, TickMapSampling; whatever is left, now)
	if (!WorldSamples.Started()) TickMapSampling();
	WorldSamples.Step(1.0e9);
	MapSize = WorldSamples.Size;
	MapMin = WorldSamples.Min;
	const float Cell = MapSize / MapN;

	// North (+X) is up, east (+Y) is right. Pixel (u, v) <-> world.
	auto ToWorld = [&](int32 U, int32 V) { return WorldSamples.At(U, V); };
	auto ToPixel = [&](const FVector2D& P, float& U, float& V) { U = (P.Y - MapMin.Y) / Cell - 0.5f; V = MapN - 1 - ((P.X - MapMin.X) / Cell - 0.5f); };

	// ---- The ground: green lowland, brown hills, grey mountains, snowy peaks, lit from the north-west so the shapes show ----
	const TArray<float>& H = WorldSamples.H;
	TArray<FLinearColor> Px;
	Px.SetNumUninitialized(MapN * MapN);
	for (int32 V = 0; V < MapN; ++V)
		for (int32 U = 0; U < MapN; ++U)
		{
			const float Z = H[V * MapN + U];
			const float Zl = H[V * MapN + FMath::Max(U - 1, 0)], Zu = H[FMath::Max(V - 1, 0) * MapN + U];
			const float Light = FMath::Clamp(1.f + ((Z - Zl) * -0.5f + (Z - Zu) * 0.5f) / Cell * 1.2f, 0.6f, 1.3f);
			FLinearColor C = Mix(FLinearColor(0.32f, 0.42f, 0.17f), FLinearColor(0.50f, 0.48f, 0.28f), FMath::SmoothStep(300.f, 2500.f, Z));
			C = Mix(C, FLinearColor(0.50f, 0.48f, 0.45f), FMath::SmoothStep(2000.f, 3800.f, Z));
			C = Mix(C, FLinearColor(0.92f, 0.93f, 0.95f), FMath::SmoothStep(4300.f, 5100.f, Z));
			Px[V * MapN + U] = C * Light;
		}

	auto Stamp = [&](const FVector2D& P, float RadiusWorld, const FLinearColor& Col, float Alpha)
	{
		float Uc, Vc;
		ToPixel(P, Uc, Vc);
		const float R = FMath::Max(RadiusWorld / Cell, 0.7f);
		for (int32 V = FMath::FloorToInt(Vc - R); V <= FMath::CeilToInt(Vc + R); ++V)
			for (int32 U = FMath::FloorToInt(Uc - R); U <= FMath::CeilToInt(Uc + R); ++U)
			{
				if (U < 0 || V < 0 || U >= MapN || V >= MapN) continue;
				const float D = FMath::Sqrt(FMath::Square(U - Uc) + FMath::Square(V - Vc));
				const float A = Alpha * FMath::Clamp(R + 0.5f - D, 0.f, 1.f);
				if (A > 0.f) Px[V * MapN + U] = Mix(Px[V * MapN + U], Col, A);
			}
	};

	// ---- Paths ----
	const FLinearColor PathCol(0.66f, 0.55f, 0.36f);
	for (const JeebTerrain::FPathDef& P : JeebTerrain::PathDefs())
		for (int32 i = 0; i < P.Pts.Num(); ++i)
			Stamp(P.Pts[i], P.Width * 0.5f, PathCol, 0.9f);

	// ---- Woods: autumn crowns in rust and orange, pines dark green ----
	for (int32 i = 0; i < Broadleaf.Num(); ++i)
		Stamp(Broadleaf[i], 260.f, (i % 3 == 0) ? FLinearColor(0.62f, 0.30f, 0.08f) : FLinearColor(0.48f, 0.24f, 0.09f), 0.75f);
	for (const FVector2D& P : Conifer) Stamp(P, 200.f, FLinearColor(0.12f, 0.24f, 0.13f), 0.85f);

	// ---- Water: the sea, the rivers, the pond and the castle moat ----
	const float WaterZ = JeebTerrain::PondWaterZ();
	const FLinearColor WaterCol(0.22f, 0.45f, 0.70f);
	for (int32 V = 0; V < MapN; ++V)
		for (int32 U = 0; U < MapN; ++U)
		{
			const FVector2D P = ToWorld(U, V);
			const float Dc = FVector2D::Distance(P, CP);
			const bool bPond = FVector2D::Distance(P, JeebTerrain::PondCenter()) < 3000.f && H[V * MapN + U] < WaterZ;
			const bool bMoat = Dc > JeebTerrain::CastleMoatIn && Dc < JeebTerrain::CastleMoatOut;
			const bool bSea = H[V * MapN + U] < JeebWorld::SeaLevel;
			const bool bRiver = WorldSamples.RD[V * MapN + U] < WorldSamples.Half[V * MapN + U] + Cell * 0.6f;   // (at least a pixel wide on the map)
			if (bPond || bMoat || bRiver) Px[V * MapN + U] = WaterCol;
			else if (bSea) Px[V * MapN + U] = Mix(WaterCol, FLinearColor(0.10f, 0.22f, 0.42f), JeebWorld::Smooth(JeebWorld::SeaLevel, JeebWorld::SeaLevel - 1500.f, H[V * MapN + U]));
		}

	// ---- Blackrock (a walled square) and the castle (a walled square inside its moat) ----
	auto Walls = [&](const FVector2D& C, float Half, float Thick, const FLinearColor& Fill, const FLinearColor& Wall)
	{
		for (int32 V = 0; V < MapN; ++V)
			for (int32 U = 0; U < MapN; ++U)
			{
				const FVector2D D = ToWorld(U, V) - C;
				const float M = FMath::Max(FMath::Abs(D.X), FMath::Abs(D.Y));
				if (M > Half + Thick) continue;
				FLinearColor& O = Px[V * MapN + U];
				O = (M > Half - Thick) ? Wall : Mix(O, Fill, 0.55f);
			}
	};
	Walls(VC, JeebTerrain::VillageHalf(), 60.f, FLinearColor(0.62f, 0.55f, 0.44f), FLinearColor(0.30f, 0.29f, 0.28f));
	Walls(CP, JeebTerrain::CastleWall, JeebTerrain::CastleWallT + 20.f, FLinearColor(0.58f, 0.56f, 0.52f), FLinearColor(0.32f, 0.31f, 0.30f));

	// ---- Into a texture ----
	MapTex = UTexture2D::CreateTransient(MapN, MapN, PF_B8G8R8A8);
	if (!MapTex) return;
	MapTex->SRGB = true;
	MapTex->Filter = TF_Bilinear;
	MapTex->AddressX = TA_Clamp;
	MapTex->AddressY = TA_Clamp;
	FColor* Out = static_cast<FColor*>(MapTex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
	for (int32 i = 0; i < MapN * MapN; ++i) Out[i] = Px[i].ToFColor(true);
	MapTex->GetPlatformData()->Mips[0].BulkData.Unlock();
	MapTex->UpdateResource();
	UE_LOG(LogTemp, Log, TEXT("JEEBMAP world picture painted in %.0f ms"), (FPlatformTime::Seconds() - MapT0) * 1000.0);
}


// Oak Hill close up (2026-11-13): a finer picture of the land round the town, 0.7 m a pixel, painted once its samples are all
// in (about 10 s after the knight appears; the whole world's picture is 4.5 m a pixel). Its edges fade out so it blends into the world's picture.
void AKnightHUD::BuildTownTex()
{
	UWorld* W = GetWorld();
	if (!W) return;
	const double T0 = FPlatformTime::Seconds();
	TownSamples.Step(1.0e9);   // (sampled a few rows a frame since the knight appeared; whatever is left, now)
	const int32 N = TownSamples.N;
	if (N <= 0) return;
	TownSize = TownSamples.Size;
	TownMin = TownSamples.Min;
	const float Cell = TownSize / N;
	auto ToWorld = [&](int32 U, int32 V) { return TownSamples.At(U, V); };
	const TArray<float>& H = TownSamples.H;
	TArray<FLinearColor> Px;
	Px.SetNumUninitialized(N * N);
	for (int32 V = 0; V < N; ++V)
		for (int32 U = 0; U < N; ++U)
		{
			const float Z = H[V * N + U];
			const float Zl = H[V * N + FMath::Max(U - 1, 0)], Zu = H[FMath::Max(V - 1, 0) * N + U];
			// (the same colours as the world's picture, lit from the north-west a little more strongly: the town's hills are low)
			const float Light = FMath::Clamp(1.f + ((Z - Zl) * -0.5f + (Z - Zu) * 0.5f) / Cell * 0.9f, 0.7f, 1.25f);
			FLinearColor C = Mix(FLinearColor(0.32f, 0.42f, 0.17f), FLinearColor(0.50f, 0.48f, 0.28f), FMath::SmoothStep(300.f, 2500.f, Z));
			const FVector2D P = ToWorld(U, V);
			const float Grain = 0.5f + 0.5f * FMath::PerlinNoise2D(P * 0.0011f);   // (a faint meadow mottle)
			C = C * Light * FMath::Lerp(0.94f, 1.05f, Grain);
			const float RD = TownSamples.RD[V * N + U], Half = TownSamples.Half[V * N + U];
			if (RD < Half) C = FLinearColor(0.22f, 0.45f, 0.70f);
			else if (RD < Half + 160.f) C = Mix(C, FLinearColor(0.62f, 0.58f, 0.42f), 0.7f);   // a sandy bank
			// fade out over the outer 12% so it melts into the world's picture
			const float E = FMath::Min(FMath::Min(U, N - 1 - U), FMath::Min(V, N - 1 - V)) / (N * 0.12f);
			C.A = FMath::Clamp(E, 0.f, 1.f);
			Px[V * N + U] = C;
		}
	auto Stamp = [&](const FVector2D& P, float RadiusWorld, const FLinearColor& Col, float Alpha)
	{
		const float Uc = (P.Y - TownMin.Y) / Cell - 0.5f, Vc = N - 1 - ((P.X - TownMin.X) / Cell - 0.5f);
		const float R = RadiusWorld / Cell;
		for (int32 V = FMath::FloorToInt(Vc - R); V <= FMath::CeilToInt(Vc + R); ++V)
			for (int32 U = FMath::FloorToInt(Uc - R); U <= FMath::CeilToInt(Uc + R); ++U)
			{
				if (U < 0 || V < 0 || U >= N || V >= N) continue;
				const float D = FMath::Sqrt(FMath::Square(U - Uc) + FMath::Square(V - Vc));
				const float A = Alpha * FMath::Clamp(R + 0.5f - D, 0.f, 1.f);
				if (A <= 0.f) continue;
				FLinearColor& O = Px[V * N + U];
				const float KeepA = O.A;
				O = Mix(O, Col * FMath::Lerp(0.8f, 1.f, FMath::Clamp(D / FMath::Max(R, 1.f), 0.f, 1.f)), A);   // (a little darker in the middle: a crown seen from above)
				O.A = KeepA;
			}
	};
	// the trees: the same crowns the woods on the world's picture are made of
	const FBox2D Box(TownMin, TownMin + FVector2D(TownSize));
	for (TActorIterator<AForest> It(W); It; ++It)
	{
		TArray<UInstancedStaticMeshComponent*> Comps;
		It->GetComponents(Comps);
		for (UInstancedStaticMeshComponent* C : Comps)
		{
			const bool bPine = C->GetName().StartsWith(TEXT("Pine"));
			if (!bPine && !C->GetName().StartsWith(TEXT("Leaf"))) continue;
			for (int32 i = 0; i < C->GetInstanceCount(); ++i)
			{
				FTransform T;
				if (!C->GetInstanceTransform(i, T, true) || !Box.IsInside(FVector2D(T.GetLocation()))) continue;
				const float R = FMath::Clamp(T.GetScale3D().X * 160.f, 160.f, 520.f);
				Stamp(FVector2D(T.GetLocation()), bPine ? R * 0.8f : R, bPine ? FLinearColor(0.12f, 0.24f, 0.13f) : ((i % 3 == 0) ? FLinearColor(0.62f, 0.30f, 0.08f) : FLinearColor(0.48f, 0.24f, 0.09f)), 0.9f);
			}
		}
	}
	for (TActorIterator<AChoppableTree> It(W); It; ++It)
		if (Box.IsInside(FVector2D(It->GetActorLocation()))) Stamp(FVector2D(It->GetActorLocation()), 300.f, FLinearColor(0.48f, 0.24f, 0.09f), 0.9f);

	TownTex = UTexture2D::CreateTransient(N, N, PF_B8G8R8A8);
	if (!TownTex) return;
	TownTex->SRGB = true;
	TownTex->Filter = TF_Bilinear;
	TownTex->AddressX = TA_Clamp;
	TownTex->AddressY = TA_Clamp;
	FColor* Out = static_cast<FColor*>(TownTex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
	for (int32 i = 0; i < N * N; ++i) Out[i] = Px[i].ToFColor(true);
	TownTex->GetPlatformData()->Mips[0].BulkData.Unlock();
	TownTex->UpdateResource();
	UE_LOG(LogTemp, Log, TEXT("JEEBMAP town picture painted in %.0f ms"), (FPlatformTime::Seconds() - T0) * 1000.0);
}

namespace
{
	// A line from A to B cut to the box [Lo, Hi] (Liang-Barsky); false if none of it is inside
	bool ClipSeg(FVector2D& A, FVector2D& B, const FVector2D& Lo, const FVector2D& Hi)
	{
		float T0 = 0.f, T1 = 1.f;
		const FVector2D D = B - A;
		const float P[4] = { -D.X, D.X, -D.Y, D.Y };
		const float Q[4] = { A.X - Lo.X, Hi.X - A.X, A.Y - Lo.Y, Hi.Y - A.Y };
		for (int32 i = 0; i < 4; ++i)
		{
			if (FMath::IsNearlyZero(P[i])) { if (Q[i] < 0.f) return false; continue; }
			const float R = Q[i] / P[i];
			if (P[i] < 0.f) { if (R > T1) return false; T0 = FMath::Max(T0, R); }
			else { if (R < T0) return false; T1 = FMath::Min(T1, R); }
		}
		const FVector2D A0 = A;
		A = A0 + D * T0;
		B = A0 + D * T1;
		return true;
	}
	// A convex shape cut to the box (Sutherland-Hodgman)
	TArray<FVector2D> ClipPoly(const TArray<FVector2D>& In, const FVector2D& Lo, const FVector2D& Hi)
	{
		TArray<FVector2D> P = In;
		for (int32 Side = 0; Side < 4 && P.Num() > 0; ++Side)
		{
			auto Inside = [&](const FVector2D& V) { return Side == 0 ? V.X >= Lo.X : Side == 1 ? V.X <= Hi.X : Side == 2 ? V.Y >= Lo.Y : V.Y <= Hi.Y; };
			auto Cut = [&](const FVector2D& A, const FVector2D& B)
			{
				const float T = Side < 2 ? ((Side == 0 ? Lo.X : Hi.X) - A.X) / (B.X - A.X) : ((Side == 2 ? Lo.Y : Hi.Y) - A.Y) / (B.Y - A.Y);
				return A + (B - A) * T;
			};
			TArray<FVector2D> O;
			for (int32 i = 0; i < P.Num(); ++i)
			{
				const FVector2D& A = P[i];
				const FVector2D& B = P[(i + 1) % P.Num()];
				const bool bA = Inside(A), bB = Inside(B);
				if (bA) O.Add(A);
				if (bA != bB) O.Add(Cut(A, B));
			}
			P = O;
		}
		return P;
	}
}

void AKnightHUD::DrawWorldMap(AKnightCharacter* Knight, float X, float Y, float S)
{
	if (!MapTex) BuildMapTex();
	if (!MapTex) return;
	UFont* Font = GEngine->GetMediumFont();
	Tile(X - 6.f, Y - 6.f, S + 12.f, S + 12.f, FLinearColor(0.8f, 0.7f, 0.5f), 0.3f);

	// ---- The view: zoom (the wheel, toward the cursor) and pan (a left drag), only with the cursor over the map ----
	constexpr float MaxZoom = 28.f;   // (about 80 m across: the town's streets and every building)
	const FVector2D Mid = MapMin + FVector2D(MapSize * 0.5f);
	if (MapView.IsZero()) MapView = Mid;
#if !UE_BUILD_SHIPPING
	{
		// (test pictures, with -JeebShot -ShotPanel: -ShotMapZoom=<x> zooms the map in on Oak Hill's plaza, or on -ShotMapAt=<X>_<Y>)
		static bool bShotDone = false;
		if (!bShotDone && FParse::Param(FCommandLine::Get(), TEXT("JeebShot")))
		{
			bShotDone = true;
			float Z = 0.f;
			if (FParse::Value(FCommandLine::Get(), TEXT("ShotMapZoom="), Z) && Z > 1.f)
			{
				MapZoom = Z;
				MapView = FVector2D(JeebOakHill::PosX, JeebOakHill::PosY);
				WorldSamples.Step(1.0e9); TownSamples.Step(1.0e9);   // (a still is taken 9 s in: no time to sample at the game's pace)
				FString At;
				TArray<FString> Parts;
				if (FParse::Value(FCommandLine::Get(), TEXT("ShotMapAt="), At) && At.ParseIntoArray(Parts, TEXT("_")) == 2) MapView = FVector2D(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]));
			}
		}
	}
#endif
	auto ClampView = [&]()
	{
		MapZoom = FMath::Clamp(MapZoom, 1.f, MaxZoom);
		const float Half = MapSize * 0.5f / MapZoom;
		MapView.X = FMath::Clamp(MapView.X, MapMin.X + Half, MapMin.X + MapSize - Half);
		MapView.Y = FMath::Clamp(MapView.Y, MapMin.Y + Half, MapMin.Y + MapSize - Half);
	};
	ClampView();
	auto SpanNow = [&]() { return MapSize / MapZoom; };   // world cm across the map
	const bool bLive = bUIHaveMouse && (Knight->IsPanelCursor() || Knight->IsSettingsOpen());
	const bool bOverMap = bLive && Over(X, Y, S, S);
	const FVector2D Mouse(UIMouseX, UIMouseY);
	APlayerController* PC = GetOwningPlayerController();
	if (MapZoom > 1.01f && Btn(X + 8.f, Y + 8.f, 74.f, 26.f, TEXT("Reset"), true, false, 0.8f))
	{
		MapZoom = 1.f; MapView = Mid; bMapPanning = false; bUIClick = false;
	}
	if (bOverMap && PC)
	{
		const int32 Notch = PC->WasInputKeyJustPressed(EKeys::MouseScrollUp) ? 1 : PC->WasInputKeyJustPressed(EKeys::MouseScrollDown) ? -1 : 0;
		if (Notch != 0)
		{
			// keep the spot under the cursor where it is
			const float Span0 = SpanNow();
			const FVector2D Off((Mouse.Y - Y - S * 0.5f) / S, (Mouse.X - X - S * 0.5f) / S);   // (screen down = world -X, screen right = world +Y)
			const FVector2D Under(MapView.X - Off.X * Span0, MapView.Y + Off.Y * Span0);
			MapZoom *= Notch > 0 ? 1.35f : 1.f / 1.35f;
			MapZoom = FMath::Clamp(MapZoom, 1.f, MaxZoom);
			const float Span1 = SpanNow();
			MapView = FVector2D(Under.X + Off.X * Span1, Under.Y - Off.Y * Span1);
			ClampView();
		}
		if (bUIClick) { bMapPanning = true; MapPanMouse = Mouse; MapPanView = MapView; bUIClick = false; }   // (a click on the map is the map's, nothing else's)
	}
	if (bMapPanning)
	{
		if (!bUIWasDown || !bLive) bMapPanning = false;
		else
		{
			const FVector2D D = Mouse - MapPanMouse;
			MapView = FVector2D(MapPanView.X + D.Y / S * SpanNow(), MapPanView.Y - D.X / S * SpanNow());
			ClampView();
		}
	}
	const float Span = SpanNow();
#if UE_BUILD_SHIPPING
	constexpr bool bNoFog = false;
#else
	static const bool bNoFog = FParse::Param(FCommandLine::Get(), TEXT("JeebShot")) && FParse::Param(FCommandLine::Get(), TEXT("ShotNoFog"));   // (test pictures only: no fog)
#endif
	auto Seen = [&](float WX, float WY) { const USkillComponent* Sk = Knight->GetSkills(); return bNoFog || !Sk || Sk->IsExplored(WX, WY); };   // (no names under the fog)
	const FVector2D ViewLo = MapView - FVector2D(Span * 0.5f), ViewHi = MapView + FVector2D(Span * 0.5f);   // (world X, Y)
	auto ToScreen = [&](const FVector2D& P) { return FVector2D(X + S * 0.5f + (P.Y - MapView.Y) / Span * S, Y + S * 0.5f - (P.X - MapView.X) / Span * S); };
	const FVector2D SLo(X, Y), SHi(X + S, Y + S);
	auto Inside = [&](const FVector2D& Sp, float Pad = 0.f) { return Sp.X >= X + Pad && Sp.X <= X + S - Pad && Sp.Y >= Y + Pad && Sp.Y <= Y + S - Pad; };

	// ---- The ground: the world's picture, the part in view ----
	const float U0 = (ViewLo.Y - MapMin.Y) / MapSize, V0 = 1.f - (ViewHi.X - MapMin.X) / MapSize;
	DrawTexture(MapTex, X, Y, S, S, U0, V0, 1.f / MapZoom, 1.f / MapZoom);

	// ---- Oak Hill close up: the finer picture of its land, fading in from 3x ----
	const float Close = FMath::Clamp((MapZoom - 3.f) / 2.f, 0.f, 1.f);
	if (Close > 0.f && JeebOakHill::Enabled())
	{
		if (!TownTex && TownSamples.Done()) BuildTownTex();   // (until its samples are in, the world picture and the lines on top do)
		const FVector2D Lo(FMath::Max(ViewLo.X, TownMin.X), FMath::Max(ViewLo.Y, TownMin.Y));
		const FVector2D Hi(FMath::Min(ViewHi.X, TownMin.X + TownSize), FMath::Min(ViewHi.Y, TownMin.Y + TownSize));
		if (TownTex && Hi.X > Lo.X && Hi.Y > Lo.Y)
		{
			const FVector2D A = ToScreen(FVector2D(Hi.X, Lo.Y)), B = ToScreen(FVector2D(Lo.X, Hi.Y));   // (top-left: north-west; bottom-right: south-east)
			const float TU = (Lo.Y - TownMin.Y) / TownSize, TV = 1.f - (Hi.X - TownMin.X) / TownSize;
			DrawTexture(TownTex, A.X, A.Y, B.X - A.X, B.Y - A.Y, TU, TV, (Hi.Y - Lo.Y) / TownSize, (Hi.X - Lo.X) / TownSize, FLinearColor(1.f, 1.f, 1.f, Close), BLEND_Translucent);
		}
	}

	// ---- Roads, drawn as lines once the map is close enough that the picture's are a blur ----
	const float RoadA = FMath::Clamp((MapZoom - 2.5f) / 2.f, 0.f, 1.f);
	struct FRoadSeg { FVector2D A, B; float HalfW; };
	TArray<FRoadSeg> RoadSegs;   // (the roads' pieces on screen, so names can keep off them)
	if (RoadA > 0.f)
	{
		const float PxPerCm = S / Span;
		for (int32 Pass = 0; Pass < 2; ++Pass)   // (a darker edge under each road first, then the road)
			for (const JeebTerrain::FPathDef& P : JeebTerrain::PathDefs())
			{
				const int32 Num = P.Pts.Num();
				if (Num < 2) continue;
				const float Wd = FMath::Max(P.Width * PxPerCm, 1.5f) + (Pass == 0 ? 2.f : 0.f);
				const FLinearColor Col = Pass == 0 ? FLinearColor(0.25f, 0.2f, 0.12f, 0.55f * RoadA) : FLinearColor(0.80f, 0.69f, 0.50f, RoadA);
				FVector2D Prev = ToScreen(P.Pts[0]);
				const int32 Last = P.bClosed ? Num : Num - 1;
				for (int32 i = 1; i <= Last; ++i)
				{
					const FVector2D Cur = ToScreen(P.Pts[i % Num]);
					if (FVector2D::DistSquared(Cur, Prev) < 9.f && i != Last) continue;   // (about every 3 pixels)
					FVector2D A = Prev, B = Cur;
					if (ClipSeg(A, B, SLo, SHi))
					{
						DrawLine(A.X, A.Y, B.X, B.Y, Col, Wd);
						if (Pass == 1) RoadSegs.Add({ A, B, Wd * 0.5f });
					}
					Prev = Cur;
				}
			}
	}

	// Names on the map: a dark pill behind each so it reads on any ground, and a list of where names already are
	// (the player's arrow is taken from the start, so no name ever sits under it; names keep 3 px apart)
	const FVector2D MeAt = ToScreen(FVector2D(Knight->GetActorLocation()));
	const FBox2D MeBox(MeAt - FVector2D(15.f, 15.f), MeAt + FVector2D(15.f, 15.f));
	TArray<FBox2D> Taken;
	Taken.Add(MeBox);
	auto PillBox = [](const FVector2D& At, float TW, float TH) { return FBox2D(At - FVector2D(6.f, 2.f), At + FVector2D(TW + 6.f, TH + 2.f)); };
	auto Overlaps = [](const FBox2D& R, const TArray<FBox2D>& L) { const FBox2D Rp = R.ExpandBy(3.f); for (const FBox2D& O : L) if (Rp.Intersect(O)) return true; return false; };
	auto OnRoads = [&](const FBox2D& R)   // how many road pieces a name would sit on
	{
		int32 Hits = 0;
		for (const FRoadSeg& G : RoadSegs) { FVector2D A = G.A, B = G.B; if (ClipSeg(A, B, R.Min - FVector2D(G.HalfW), R.Max + FVector2D(G.HalfW))) ++Hits; }
		return Hits;
	};
	auto RectHitsQuad = [](const FBox2D& R, const TArray<FVector2D>& Q)   // (separating axes: the box's own and the quad's edges)
	{
		FBox2D QB(ForceInit);
		for (const FVector2D& V : Q) QB += V;
		if (!R.Intersect(QB)) return false;
		const FVector2D RC[4] = { R.Min, FVector2D(R.Max.X, R.Min.Y), R.Max, FVector2D(R.Min.X, R.Max.Y) };
		for (int32 e = 0; e < Q.Num(); ++e)
		{
			const FVector2D D = Q[(e + 1) % Q.Num()] - Q[e];
			const FVector2D Nrm(-D.Y, D.X);
			float QMin = FLT_MAX, QMax = -FLT_MAX, RMin = FLT_MAX, RMax = -FLT_MAX;
			for (const FVector2D& V : Q) { const float Dp = FVector2D::DotProduct(V, Nrm); QMin = FMath::Min(QMin, Dp); QMax = FMath::Max(QMax, Dp); }
			for (const FVector2D& V : RC) { const float Dp = FVector2D::DotProduct(V, Nrm); RMin = FMath::Min(RMin, Dp); RMax = FMath::Max(RMax, Dp); }
			if (RMax <= QMin || RMin >= QMax) return false;
		}
		return true;
	};
	auto Label = [&](const FString& T, float Sc, const FVector2D& At, float TW, float TH, const FLinearColor& Col, float A)
	{
		const FBox2D R = PillBox(At, TW, TH);
		const FVector2D Sz = R.GetSize();
		FillRR(R.Min.X, R.Min.Y, Sz.X, Sz.Y, Sz.Y * 0.5f, [&](float) { return FLinearColor(0.04f, 0.04f, 0.06f, 0.72f * A); });
		HDText(T, FLinearColor(0.f, 0.f, 0.f, A), At.X + 1.f, At.Y + 1.f, Font, Sc);
		HDText(T, FLinearColor(Col.R, Col.G, Col.B, A), At.X, At.Y, Font, Sc);
	};

	// ---- Oak Hill's buildings (read from the town's own plan, so they are wherever the town puts them) ----
	const float HouseA = FMath::Clamp((MapZoom - 5.f) / 2.f, 0.f, 1.f);
	if (HouseA > 0.f && JeebOakHill::Enabled())
	{
		using namespace JeebOakHill;
		auto Corners = [&](const FPlot& P)
		{
			TArray<FVector2D> C;
			const float HW = P.W * 0.5f * Unit, HH = P.H * 0.5f * Unit;   // (metres, east x north, before turning)
			const FVector2D L[4] = { FVector2D(-HW, HH), FVector2D(HW, HH), FVector2D(HW, -HH), FVector2D(-HW, -HH) };
			for (const FVector2D& Lm : L)
			{
				const float E = P.Ce + Lm.X * P.Cs + Lm.Y * P.Sn, N = P.Cn - Lm.X * P.Sn + Lm.Y * P.Cs;
				C.Add(ToScreen(FVector2D(PosX + N * 100.f, PosY + E * 100.f)));
			}
			return C;
		};
		auto Edge = [&](const TArray<FVector2D>& C, const FLinearColor& Col, float Th)
		{
			for (int32 i = 0; i < C.Num(); ++i) { FVector2D A = C[i], B = C[(i + 1) % C.Num()]; if (ClipSeg(A, B, SLo, SHi)) DrawLine(A.X, A.Y, B.X, B.Y, Col, Th); }
		};
		for (const FPlot& P : Plots())
		{
			const TArray<FVector2D> C = Corners(P);
			if (P.Kind == EPlot::Lawn)
			{
				// the plaza's lawn and the big oak in its middle
				const FVector2D Cc = ToScreen(FVector2D(PosX + P.Cn * 100.f, PosY + P.Ce * 100.f));
				const float R = FMath::Min(P.W, P.H) * 0.5f * Unit * 100.f / Span * S;
				if (Inside(Cc, R + 1.f))
				{
					Disc(Cc.X, Cc.Y, R, R, FLinearColor(0.36f, 0.56f, 0.22f, HouseA));
					Disc(Cc.X, Cc.Y, R * 0.55f, R * 0.55f, FLinearColor(0.16f, 0.32f, 0.12f, HouseA));
				}
				continue;
			}
			if (P.Kind != EPlot::Building) { Edge(C, FLinearColor(1.f, 0.97f, 0.88f, 0.35f * HouseA), 1.f); continue; }   // (kept for what isn't built yet: a faint outline)
			const bool bHouse = FCString::Strcmp(P.Name, TEXT("House")) == 0;
			const bool bStall = IsStall(P.Model);
			const FLinearColor Roof = bHouse ? FLinearColor(0.66f, 0.38f, 0.24f) : bStall ? FLinearColor(0.78f, 0.62f, 0.38f) : FLinearColor(0.46f, 0.50f, 0.60f);
			const TArray<FVector2D> In = ClipPoly(C, SLo, SHi);
			if (In.Num() >= 3) Poly(In, FLinearColor(Roof.R, Roof.G, Roof.B, HouseA));
			Edge(C, FLinearColor(0.12f, 0.09f, 0.07f, 0.8f * HouseA), 1.5f);
		}
		// names of the shops, the hall and the rest (not the houses), once there is room for them: each on a dark pill, beside its
		// own box (below, above, right or left, whichever is free of other names and boxes), never on top of another name
		const float NameA = FMath::Clamp((MapZoom - 9.f) / 0.6f, 0.f, 1.f);   // (a short fade: half-faded names are hard to read)
		if (NameA > 0.f)
		{
			const TArray<FPlot>& All = Plots();
			TArray<TArray<FVector2D>> Quads;   // (every plot's real outline on screen, so a name can keep off the other buildings)
			for (const FPlot& P : All) Quads.Add(P.Kind == EPlot::Lawn ? TArray<FVector2D>() : Corners(P));
			for (int32 Pass = 0; Pass < 2; ++Pass)   // (the buildings' names first, then the plots still to build)
				for (int32 Pi = 0; Pi < All.Num(); ++Pi)
				{
					const FPlot& P = All[Pi];
					if (P.Kind == EPlot::Lawn || FCString::Strcmp(P.Name, TEXT("House")) == 0) continue;
					const bool bBuilt = P.Kind == EPlot::Building;
					if (bBuilt != (Pass == 0) || !Seen(PosX + P.Cn * 100.f, PosY + P.Ce * 100.f)) continue;
					FBox2D B(ForceInit);
					for (const FVector2D& V : Quads[Pi]) B += V;
					const FVector2D BoxMid = B.GetCenter();
					if (!Inside(BoxMid, -60.f)) continue;
					const FString T = P.Name;
					const float Sc = 0.8f;
					float TW = 0.f, TH = 0.f;
					HDTextSize(T, TW, TH, Font, Sc);
					// spots round its own box, the plainest first; the middle of its own box last
					const float Lx = BoxMid.X - TW * 0.5f, Ty = BoxMid.Y - TH * 0.5f;
					const float Bel = B.Max.Y + 4.f, Abv = B.Min.Y - 4.f - TH, Rt = B.Max.X + 9.f, Lf = B.Min.X - 9.f - TW;
					const FVector2D Cand[9] = {
						FVector2D(Lx, Bel), FVector2D(Lx, Abv), FVector2D(Rt, Ty), FVector2D(Lf, Ty),
						FVector2D(Rt, Bel), FVector2D(Lf, Bel), FVector2D(Rt, Abv), FVector2D(Lf, Abv), FVector2D(Lx, Ty) };
					int32 Pick = -1, PickRoads = MAX_int32;
					for (int32 i = 0; i < 9 && PickRoads > 0; ++i)
					{
						const FBox2D R = PillBox(Cand[i], TW, TH);
						if (R.Min.X < X + 2.f || R.Min.Y < Y + 2.f || R.Max.X > X + S - 2.f || R.Max.Y > Y + S - 2.f || Overlaps(R, Taken)) continue;
						bool bOnOther = false;   // (never on another building or plot; on its own only in the last spot)
						const FBox2D Rp = R.ExpandBy(2.f);
						for (int32 Qi = 0; Qi < Quads.Num() && !bOnOther; ++Qi)
							if (Quads[Qi].Num() && (Qi != Pi || i < 8)) bOnOther = RectHitsQuad(Rp, Quads[Qi]);
						if (bOnOther) continue;
						const int32 Roads = OnRoads(R);
						if (Roads < PickRoads) { Pick = i; PickRoads = Roads; }
					}
					if (Pick < 0) continue;   // (no room for it here: zoom in further)
					Taken.Add(PillBox(Cand[Pick], TW, TH));
					Label(T, Sc, Cand[Pick], TW, TH, bBuilt ? FLinearColor(1.f, 0.95f, 0.8f) : FLinearColor(0.82f, 0.82f, 0.78f), NameA);
				}
		}
	}

	// ---- Fog over the ground this character has not seen yet: soft grey cloud, repainted only when the explored grid changes ----
	if (USkillComponent* Sk = bNoFog ? nullptr : Knight->GetSkills())
	{
		constexpr int32 FogPx = 128;
		const uint32 Hash = FCrc::MemCrc32(Sk->Explored.GetData(), Sk->Explored.Num()) ^ uint32(Sk->Explored.Num());
		if (!FogTex || Hash != FogHash)
		{
			FogHash = Hash;
			if (!FogTex)
			{
				FogTex = UTexture2D::CreateTransient(FogPx, FogPx, PF_B8G8R8A8);
				if (FogTex) { FogTex->SRGB = true; FogTex->Filter = TF_Bilinear; FogTex->AddressX = TA_Clamp; FogTex->AddressY = TA_Clamp; }
			}
			if (FogTex)
			{
				const float Cell = MapSize / FogPx;
				FColor* Out = static_cast<FColor*>(FogTex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
				for (int32 V = 0; V < FogPx; ++V)
					for (int32 U = 0; U < FogPx; ++U)
					{
						const float WX = MapMin.X + (FogPx - 1 - V + 0.5f) * Cell, WY = MapMin.Y + (U + 0.5f) * Cell;
						const float Nz = 0.5f + 0.5f * FMath::PerlinNoise2D(FVector2D(U, V) * 0.09f);   // cloudy, not flat
						const uint8 G = uint8(FMath::Lerp(150.f, 190.f, Nz));
						Out[V * FogPx + U] = Sk->IsExplored(WX, WY) ? FColor(G, G, G, 0) : FColor(G, G, uint8(G * 1.03f), 250);
					}
				FogTex->GetPlatformData()->Mips[0].BulkData.Unlock();
				FogTex->UpdateResource();
			}
		}
		if (FogTex) DrawTexture(FogTex, X, Y, S, S, U0, V0, 1.f / MapZoom, 1.f / MapZoom, FLinearColor::White, BLEND_Translucent);
	}

	// ---- Town names (every town, once you have seen it; Blackrock and Oak Hill stand close: their names sit outward) ----
	const float TownNameA = 1.f - FMath::Clamp((MapZoom - 6.f) / 2.f, 0.f, 1.f);   // (close in, the buildings' own names take over)
	if (TownNameA > 0.f)
	{
		// (the arrow counts as taken, so a name never sits under it: it moves above or below the arrow instead)
		for (const JeebWorld::FTown& T : JeebWorld::Towns())
		{
			if (!Seen(T.Pos.X, T.Pos.Y)) continue;   // no names under the fog
			const FVector2D At = ToScreen(T.Pos);
			if (!Inside(At, 4.f)) continue;
			const float Sc = 1.1f;
			float TW = 0.f, TH = 0.f;
			HDTextSize(T.Name, TW, TH, Font, Sc);
			// beside the town with a gap (names that sit outward), or just below its spot
			const float BaseX = T.LabelSide > 0.f ? At.X + 20.f : T.LabelSide < 0.f ? At.X - 20.f - TW : At.X - TW * 0.5f;
			const float BaseY = T.LabelSide != 0.f ? At.Y - TH * 0.5f : At.Y + 12.f;
			const FVector2D Cand[3] = { FVector2D(BaseX, BaseY), FVector2D(BaseX, MeBox.Max.Y + 8.f), FVector2D(BaseX, MeBox.Min.Y - 8.f - TH) };
			for (int32 i = 0; i < 3; ++i)
			{
				const FVector2D P(FMath::Clamp(Cand[i].X, X + 8.f, X + S - TW - 8.f), FMath::Clamp(Cand[i].Y, Y + 4.f, Y + S - TH - 4.f));
				const FBox2D R = PillBox(P, TW, TH);
				if (i < 2 && (R.Intersect(MeBox) || Overlaps(R, Taken))) continue;
				Taken.Add(R);
				Label(T.Name, Sc, P, TW, TH, FLinearColor(1.f, 0.93f, 0.75f), TownNameA);
				break;
			}
		}
	}
	HDText(TEXT("N"), FLinearColor(1.f, 0.93f, 0.75f), X + S - 26.f, Y + 8.f, Font, 1.2f);
	if (bOverMap && MapZoom <= 1.01f && !bMapPanning)
	{
		const FString Hint = TEXT("Scroll to zoom in, drag to move");
		float TW = 0.f, TH = 0.f;
		HDTextSize(Hint, TW, TH, Font, 0.85f);
		Tile(X + (S - TW) * 0.5f - 8.f, Y + S - TH - 14.f, TW + 16.f, TH + 6.f, FLinearColor(0.05f, 0.07f, 0.12f), 0.5f, false);
		HDText(Hint, FLinearColor(0.95f, 0.95f, 0.95f), X + (S - TW) * 0.5f, Y + S - TH - 11.f, Font, 0.85f);
	}

	// ---- You: an arrow pointing the way you face (only you: never other players). Off the map's edge, a smaller one at the edge ----
	const FVector L = Knight->GetActorLocation();
	FVector2D C = ToScreen(FVector2D(L));
	const bool bIn = Inside(C, 12.f);
	if (!bIn) { C.X = FMath::Clamp(C.X, X + 12.f, X + S - 12.f); C.Y = FMath::Clamp(C.Y, Y + 12.f, Y + S - 12.f); }
	const float Yaw = FMath::DegreesToRadians(Knight->GetActorRotation().Yaw);
	const FVector2D F(FMath::Sin(Yaw), -FMath::Cos(Yaw));   // world +X is screen up, +Y is screen right
	const FVector2D Rt(-F.Y, F.X);
	auto Arrow = [&](float Sz, const FLinearColor& Col)
	{
		Poly({ C + F * Sz * 1.3f, C - F * Sz * 0.8f + Rt * Sz, C - F * Sz * 0.4f, C - F * Sz * 0.8f - Rt * Sz }, Col);
	};
	const float Sz = bIn ? 1.f : 0.7f;
	Arrow(13.f * Sz, FLinearColor(0.f, 0.f, 0.f, 0.8f));
	Arrow(10.f * Sz, bIn ? FLinearColor(1.f, 0.85f, 0.25f) : FLinearColor(1.f, 0.85f, 0.25f, 0.7f));
}

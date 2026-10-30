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

// ---------------------------------------------------------------------------------------------------
// The world map (the Map tab of the Escape screen). Kept simple on purpose: the lie of the land, the woods, the paths,
// the water, Blackrock and the castle, and an arrow for YOU. No villagers, shops or other players.
// It is painted once, from this machine's own copy of the world, into a texture.
// ---------------------------------------------------------------------------------------------------
namespace
{
	constexpr int32 MapN = 512;   // texture size

	FLinearColor Mix(const FLinearColor& A, const FLinearColor& B, float T) { return A + (B - A) * FMath::Clamp(T, 0.f, 1.f); }
}

void AKnightHUD::BuildMapTex()
{
	UWorld* W = GetWorld();
	if (!W) return;

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
	const FBox2D B(FVector2D(JeebWorld::MinX, JeebWorld::MinY), FVector2D(JeebWorld::MaxX, JeebWorld::MaxY));
	MapSize = FMath::Max(B.GetSize().X, B.GetSize().Y);
	MapMin = B.GetCenter() - FVector2D(MapSize * 0.5f);
	const float Cell = MapSize / MapN;

	// North (+X) is up, east (+Y) is right. Pixel (u, v) <-> world.
	auto ToWorld = [&](int32 U, int32 V) { return FVector2D(MapMin.X + (MapN - 1 - V + 0.5f) * Cell, MapMin.Y + (U + 0.5f) * Cell); };
	auto ToPixel = [&](const FVector2D& P, float& U, float& V) { U = (P.Y - MapMin.Y) / Cell - 0.5f; V = MapN - 1 - ((P.X - MapMin.X) / Cell - 0.5f); };

	// ---- The ground: green lowland, brown hills, grey mountains, snowy peaks, lit from the north-west so the shapes show ----
	TArray<float> H;
	H.SetNumUninitialized(MapN * MapN);
	for (int32 V = 0; V < MapN; ++V)
		for (int32 U = 0; U < MapN; ++U)
		{
			const FVector2D P = ToWorld(U, V);
			H[V * MapN + U] = JeebTerrain::HeightAt(P.X, P.Y);
		}
	TArray<FLinearColor> Px;
	Px.SetNumUninitialized(MapN * MapN);
	for (int32 V = 0; V < MapN; ++V)
		for (int32 U = 0; U < MapN; ++U)
		{
			const float Z = H[V * MapN + U];
			const float Zl = H[V * MapN + FMath::Max(U - 1, 0)], Zu = H[FMath::Max(V - 1, 0) * MapN + U];
			const float Light = FMath::Clamp(1.f + ((Z - Zl) * -0.5f + (Z - Zu) * 0.5f) / Cell * 1.2f, 0.6f, 1.3f);
			FLinearColor C = Mix(FLinearColor(0.32f, 0.42f, 0.17f), FLinearColor(0.50f, 0.48f, 0.28f), FMath::SmoothStep(300.f, 2500.f, Z));
			C = Mix(C, FLinearColor(0.50f, 0.48f, 0.45f), FMath::SmoothStep(3000.f, 7000.f, Z));
			C = Mix(C, FLinearColor(0.92f, 0.93f, 0.95f), FMath::SmoothStep(10500.f, 13000.f, Z));
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
			float Half;
			const bool bRiver = JeebWorld::RiverDistance(P.X, P.Y, Half) < Half + Cell * 0.6f;   // (at least a pixel wide on the map)
			if (bPond || bMoat || bRiver) Px[V * MapN + U] = WaterCol;
			else if (bSea) Px[V * MapN + U] = Mix(WaterCol, FLinearColor(0.10f, 0.22f, 0.42f), FMath::SmoothStep(JeebWorld::SeaLevel, JeebWorld::SeaLevel - 1500.f, H[V * MapN + U]));
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
}

void AKnightHUD::DrawWorldMap(AKnightCharacter* Knight, float X, float Y, float S)
{
	if (!MapTex) BuildMapTex();
	if (!MapTex) return;
	UFont* Font = GEngine->GetMediumFont();
	Tile(X - 6.f, Y - 6.f, S + 12.f, S + 12.f, FLinearColor(0.8f, 0.7f, 0.5f), 0.3f);
	DrawTexture(MapTex, X, Y, S, S, 0.f, 0.f, 1.f, 1.f);

	// Fog over the ground this character has not seen yet: soft grey cloud, repainted only when the explored grid changes
	if (USkillComponent* Sk = Knight->GetSkills())
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
						const float N = 0.5f + 0.5f * FMath::PerlinNoise2D(FVector2D(U, V) * 0.09f);   // cloudy, not flat
						const uint8 G = uint8(FMath::Lerp(150.f, 190.f, N));
						Out[V * FogPx + U] = Sk->IsExplored(WX, WY) ? FColor(G, G, G, 0) : FColor(G, G, uint8(G * 1.03f), 250);
					}
				FogTex->GetPlatformData()->Mips[0].BulkData.Unlock();
				FogTex->UpdateResource();
			}
		}
		if (FogTex) DrawTexture(FogTex, X, Y, S, S, 0.f, 0.f, 1.f, 1.f, FLinearColor::White, BLEND_Translucent);
	}

	auto ToScreen = [&](const FVector2D& P) { return FVector2D(X + (P.Y - MapMin.Y) / MapSize * S, Y + (1.f - (P.X - MapMin.X) / MapSize) * S); };
	auto Label = [&](const FString& T, const FVector2D& P, float Scale)
	{
		if (Knight->GetSkills() && !Knight->GetSkills()->IsExplored(P.X, P.Y)) return;   // no names under the fog
		const FVector2D At = ToScreen(P);
		float TW = 0.f, TH = 0.f;
		HDTextSize(T, TW, TH, Font, Scale);
		HDText(T, FLinearColor(0.f, 0.f, 0.f, 0.6f), At.X - TW * 0.5f + 1.5f, At.Y - TH * 0.5f + 1.5f, Font, Scale);
		HDText(T, FLinearColor(1.f, 0.93f, 0.75f), At.X - TW * 0.5f, At.Y - TH * 0.5f, Font, Scale);
	};
	for (const JeebWorld::FTown& T : JeebWorld::Towns()) Label(T.Name, T.Pos, 1.1f);   // every town, once you have seen it
	HDText(TEXT("N"), FLinearColor(1.f, 0.93f, 0.75f), X + S - 26.f, Y + 8.f, Font, 1.2f);

	// You: an arrow pointing the way you face (only you: never other players)
	const FVector L = Knight->GetActorLocation();
	const FVector2D C = ToScreen(FVector2D(L));
	const float Yaw = FMath::DegreesToRadians(Knight->GetActorRotation().Yaw);
	const FVector2D F(FMath::Sin(Yaw), -FMath::Cos(Yaw));   // world +X is screen up, +Y is screen right
	const FVector2D Rt(-F.Y, F.X);
	auto Arrow = [&](float Sz, const FLinearColor& Col)
	{
		Poly({ C + F * Sz * 1.3f, C - F * Sz * 0.8f + Rt * Sz, C - F * Sz * 0.4f, C - F * Sz * 0.8f - Rt * Sz }, Col);
	};
	Arrow(13.f, FLinearColor(0.f, 0.f, 0.f, 0.8f));
	Arrow(10.f, FLinearColor(1.f, 0.85f, 0.25f));
}

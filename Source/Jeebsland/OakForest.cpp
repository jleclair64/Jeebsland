#include "OakForest.h"
#include "TreeLook.h"
#include "Terrain.h"
#include "WorldMap.h"
#include "OakHill.h"
#include "TownLab.h"
#include "Canopy.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/StaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// The forest and the town's trees (OakForest.h). Every number that shapes them is here; log JEEBFOREST.
namespace
{
	// ------------------------------------------------------------------ the owner's knobs
	TAutoConsoleVariable<float> CVarDensity(TEXT("jeeb.Forest.Density"), 1.f,
		TEXT("How thick the forest round Oak Hill is (1 as built; 0.5 half the trees and undergrowth, 0 none). Read when the world is built: change it, then load the map again. Online, give the server and every player the same value (the trunks are solid)."), ECVF_Default);
	TAutoConsoleVariable<float> CVarReach(TEXT("jeeb.Forest.Reach"), 0.f,
		TEXT("How far the forest's trees are drawn, metres (0: as far as the haze lets you see). Undergrowth keeps its own shorter reach. Live; looks only."), ECVF_Default);
	TAutoConsoleVariable<int32> CVarShadows(TEXT("jeeb.Forest.Shadows"), 1,
		TEXT("Shadows from the forest and the town's trees: 1 on (trees within jeeb.Forest.ShadowReach of the camera, logs, stumps and rocks; never bushes), 0 none. Live; looks only."), ECVF_Default);
	TAutoConsoleVariable<float> CVarShadowReach(TEXT("jeeb.Forest.ShadowReach"), 20.f,
		TEXT("How far from the camera the trees cast shadows, metres (set by the graphics setting: 20 HIGH, 15 LOW since 2026-11-16, was 40 and 20; past it the stand-ins, jeeb.Forest.FarShade; 2026-11-16: every tree casting cost 7 ms in the forest, 60 m 5 ms, 40 m 3.5, 25 m 1.9). Live; looks only."), ECVF_Default);
	// Past that reach, plain stand-in crowns cast the far trees' shadows (2026-11-16, owner: "far away ... looks too bright then when I
	// get closer it turns dark"): real shadows, so they follow the sun at every time of day, at a small share of a real tree's cost
	TAutoConsoleVariable<int32> CVarFarShade(TEXT("jeeb.Forest.FarShade"), 1,
		TEXT("1: past jeeb.Forest.ShadowReach, each tree's shadow is cast by a plain hidden stand-in (crown and trunk) out to jeeb.Forest.FarShadeReach, so far trees and the ground under them are shaded like the near ones; 0: no shadows past the reach (before 2026-11-16). Live; looks only."), ECVF_Default);
	TAutoConsoleVariable<float> CVarFarShadeReach(TEXT("jeeb.Forest.FarShadeReach"), 200.f,
		TEXT("How far from the camera the trees' stand-ins cast shadows, metres (set by the graphics setting). Past the sun's ordinary shadow range they need its far cascade, jeeb.Shadow.Far. Live; looks only."), ECVF_Default);
	TAutoConsoleVariable<float> CVarFarShadeSize(TEXT("jeeb.Forest.FarShadeSize"), 0.75f,
		TEXT("The stand-in crown's size against the real crown (1 as wide and tall; smaller is a lighter, smaller shade). Live; looks only."), ECVF_Default);

	TAutoConsoleVariable<int32> CVarShadowAll(TEXT("jeeb.Forest.ShadowAll"), 0,
		TEXT("1: every drawn tree casts its own shadow (meant for virtual shadow maps, r.Shadow.Virtual.Enable 1; with ordinary shadow maps it cost about 7 ms); 0: only the near twins and the stand-ins. Live; looks only."), ECVF_Default);
	TAutoConsoleVariable<float> CVarSpruceLod(TEXT("jeeb.Forest.SpruceLod"), 2.f,
		TEXT("How soon the spruces drop to their simpler models: 1 as the pack made them, 2 at twice the size on screen (so at about half the distance). Live; looks only."), ECVF_Default);
	TAutoConsoleVariable<int32> CVarTwinLod(TEXT("jeeb.Forest.TwinLod"), 1,
		TEXT("The spruces' near shadows come from this simpler model (0 the same model as drawn, 1 the next simpler, and so on). Live; looks only."), ECVF_Default);
	TAutoConsoleVariable<float> CVarSway(TEXT("jeeb.Forest.Sway"), 60.f,
		TEXT("How far from the camera the trees sway in the wind, metres (past it they stand still, which also keeps cached shadows valid). Live; looks only."), ECVF_Default);

	// A stand-in for one tree kind at scale 1, in the tree mesh's own frame: an egg-shaped crown (a cone for the spruces) from where the
	// trunk ends to the top, and a six-sided trunk; flat-shaded, about 110 triangles, the engine's plain material (it is only ever a shadow)
	UStaticMesh* MakeShadeMesh(UObject* Outer, const FBox& Bx, float Crown, float TrunkR, float TrunkH, bool bCone, float Size)
	{
		UStaticMesh* SM = NewObject<UStaticMesh>(Outer);
		SM->GetStaticMaterials().Add(FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface), FName("Shade")));
		FMeshDescription MD;
		FStaticMeshAttributes Attr(MD);
		Attr.Register();
		TVertexAttributesRef<FVector3f> Pos = Attr.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Nrm = Attr.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector3f> Tan = Attr.GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<float> Sign = Attr.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
		const FPolygonGroupID Group = MD.CreatePolygonGroup();
		Attr.GetPolygonGroupMaterialSlotNames()[Group] = FName("Shade");
		auto Tri = [&](const FVector3f& A, const FVector3f& B, const FVector3f& C)
		{
			const FVector3f N = ((C - A) ^ (B - A)).GetSafeNormal();
			FVertexInstanceID I[3];
			const FVector3f* P[3] = { &A, &B, &C };
			for (int32 i = 0; i < 3; ++i)
			{
				const FVertexID V = MD.CreateVertex();
				Pos[V] = *P[i];
				I[i] = MD.CreateVertexInstance(V);
				Nrm[I[i]] = N; Tan[I[i]] = FVector3f(1.f, 0.f, 0.f); Sign[I[i]] = 1.f; UVs[I[i]] = FVector2f::ZeroVector;
			}
			MD.CreatePolygon(Group, { I[0], I[1], I[2] });
		};
		const float Top = Bx.Max.Z, Zb = FMath::Clamp(TrunkH, 0.f, Top * 0.8f);
		const FVector3f Mid(Bx.GetCenter().X, Bx.GetCenter().Y, 0.f);
		constexpr int32 Seg = 10;
		auto Ring = [&](int32 s, float R, float Z) { const float A = 2.f * PI * s / Seg; return Mid + FVector3f(R * FMath::Cos(A), R * FMath::Sin(A), Z); };
		const float Rc = Crown * Size;
		if (bCone)
		{
			const float H = Top - Zb, Base = Zb + H * (1.f - Size) * 0.4f, Tip = Top - H * (1.f - Size) * 0.3f;
			const FVector3f Apex = Mid + FVector3f(0.f, 0.f, Tip), Floor = Mid + FVector3f(0.f, 0.f, Base);
			for (int32 s = 0; s < Seg; ++s) { Tri(Ring(s, Rc, Base), Ring(s + 1, Rc, Base), Apex); Tri(Ring(s + 1, Rc, Base), Ring(s, Rc, Base), Floor); }
		}
		else
		{
			const float Zc = 0.5f * (Zb + Top), Rz = 0.5f * (Top - Zb) * Size;
			constexpr int32 Bands = 6;
			for (int32 b = 0; b < Bands; ++b)
			{
				const float A0 = PI * b / Bands - 0.5f * PI, A1 = PI * (b + 1) / Bands - 0.5f * PI;
				const float R0 = Rc * FMath::Cos(A0), R1 = Rc * FMath::Cos(A1), Z0 = Zc + Rz * FMath::Sin(A0), Z1 = Zc + Rz * FMath::Sin(A1);
				for (int32 s = 0; s < Seg; ++s)
				{
					if (b > 0) Tri(Ring(s, R0, Z0), Ring(s + 1, R0, Z0), Ring(s + 1, R1, Z1));
					if (b < Bands - 1) Tri(Ring(s, R0, Z0), Ring(s + 1, R1, Z1), Ring(s, R1, Z1));
				}
			}
		}
		// the trunk, up into the crown
		const float Tr = FMath::Max(TrunkR, 8.f), Th = Zb + 0.3f * (Top - Zb);
		auto Hex = [&](int32 s, float Z) { const float A = 2.f * PI * s / 6; return FVector3f(Tr * FMath::Cos(A), Tr * FMath::Sin(A), Z); };   // (at the tree's own foot)
		for (int32 s = 0; s < 6; ++s) { Tri(Hex(s, 0.f), Hex(s + 1, 0.f), Hex(s + 1, Th)); Tri(Hex(s, 0.f), Hex(s + 1, Th), Hex(s, Th)); }
		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bBuildSimpleCollision = false;
		Params.bFastBuild = true;
		TArray<const FMeshDescription*> Descs;
		Descs.Add(&MD);
		SM->BuildFromMeshDescriptions(Descs, Params);
		return SM;
	}

	// The four colours, shared with the sample groves (TreeGroves.cpp) and the LOOK tab's TREES sliders: found by name, or made here in a
	// build without the groves (Shipping), with the groves' defaults
	const TCHAR* const TintNames[4] = { TEXT("jeeb.Trees.DzGreen"), TEXT("jeeb.Trees.DzRed"), TEXT("jeeb.Trees.DzOrange"), TEXT("jeeb.Trees.DzYellow") };
	const TCHAR* const TintDefaults[4] = { TEXT("0.1714 0.2315 0.0595"), TEXT("0.3613 0.0506 0.0253"), TEXT("0.5935 0.209 0.0334"), TEXT("0.595 0.3825 0.0439") };
	const int32 TintLeaf[4] = { -1, 1, 0, 3 };   // the fallen leaves under a town tree of that colour (AutumnDecor: 0 orange, 1 red, 3 gold; green drops none)
	const TCHAR* const TintTitle[4] = { TEXT("green"), TEXT("red"), TEXT("orange"), TEXT("yellow") };
	TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> GTintMids[4];
	TWeakObjectPtr<AOakForest> GForest;

	IConsoleVariable* TintVar(int32 F)
	{
		IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TintNames[F]);
		if (!V) V = IConsoleManager::Get().RegisterConsoleVariable(TintNames[F], FString(TintDefaults[F]), TEXT("Leaf colour of the DZ trees of this colour round Oak Hill, \"r g b\" (live)"), ECVF_Default);
		return V;
	}
	FLinearColor Tint(int32 F)
	{
		IConsoleVariable* V = TintVar(F);
		FString S = V ? V->GetString() : FString(TintDefaults[F]);
		S.ReplaceCharInline(TEXT(','), TEXT(' ')); S.ReplaceCharInline(TEXT('_'), TEXT(' '));
		TArray<FString> P; S.ParseIntoArrayWS(P);
		float C[3] = { 0.5f, 0.5f, 0.5f };
		for (int32 i = 0; i < 3 && i < P.Num(); ++i) C[i] = FMath::Max(0.f, FCString::Atof(*P[i]));
		return JeebTreeLook::Saturate(FLinearColor(C[0], C[1], C[2]));   // (jeeb.Trees.Saturation on top)
	}
	void ApplyTints(IConsoleVariable* = nullptr)
	{
		for (int32 F = 0; F < 4; ++F)
		{
			const FLinearColor T = Tint(F);
			for (const TWeakObjectPtr<UMaterialInstanceDynamic>& M : GTintMids[F]) if (M.IsValid()) M->SetVectorParameterValue(TEXT("Fall Color Tint"), T);
		}
	}


	// The spruces' needle colour (owner, 2026-11-16: the LOOK tab's jeeb.Trees.Spruce sliders must reach the forest's and the town's spruces too).
	// The same variable and parameters as TreeGroves.cpp's TintSpruce: "Leaf Tint" (_leaf materials), "Branch Tint" (_branch), the imposters' "Tint".
	TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> GSpruceMids;   // (one per distinct spruce material, shared by every spruce of the forest)
	IConsoleVariable* SpruceVar()
	{
		IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("jeeb.Trees.Spruce"));
		if (!V) V = IConsoleManager::Get().RegisterConsoleVariable(TEXT("jeeb.Trees.Spruce"), TEXT("0.8 0.9 0.92"), TEXT("Needle colour multiplier of every spruce, \"r g b\" (1 1 1 = as the pack has it; live)"), ECVF_Default);
		return V;
	}
	FLinearColor SpruceTint()
	{
		IConsoleVariable* V = SpruceVar();
		return JeebTreeLook::Saturate(JeebTreeLook::ParseRGB(V ? V->GetString() : FString(TEXT("0.8 0.9 0.92")), FLinearColor(1.f, 1.f, 1.f, 0.f)));   // (the pack's tints have alpha 0)
	}
	const TCHAR* SpruceParam(const FString& MaterialName)
	{
		if (MaterialName.Contains(TEXT("imposter"))) return TEXT("Tint");
		if (MaterialName.Contains(TEXT("_leaf"))) return TEXT("Leaf Tint");
		if (MaterialName.Contains(TEXT("_branch"))) return TEXT("Branch Tint");
		return nullptr;   // (trunk: bark and moss keep the pack's colour)
	}
	void ApplySpruce(IConsoleVariable* = nullptr)
	{
		const FLinearColor Sp = SpruceTint();
		for (const TWeakObjectPtr<UMaterialInstanceDynamic>& M : GSpruceMids)
			if (M.IsValid() && M->Parent) if (const TCHAR* Param = SpruceParam(M->Parent->GetName())) M->SetVectorParameterValue(Param, Sp);
	}
	// every needle slot of a spruce set gets the shared dynamic instance of its material, in the spruce tint
	int32 TintSpruceSet(UMeshComponent* C, UObject* Outer)
	{
		int32 Done = 0;
		const FLinearColor Sp = SpruceTint();
		for (int32 i = 0; i < C->GetNumMaterials(); ++i)
		{
			UMaterialInterface* M = C->GetMaterial(i);
			const TCHAR* Param = M ? SpruceParam(M->GetName()) : nullptr;
			if (!Param) continue;
			UMaterialInstanceDynamic* D = nullptr;
			for (const TWeakObjectPtr<UMaterialInstanceDynamic>& Old : GSpruceMids) if (Old.IsValid() && Old->Parent == M) { D = Old.Get(); break; }
			if (!D) { D = UMaterialInstanceDynamic::Create(M, Outer); D->SetVectorParameterValue(Param, Sp); GSpruceMids.Add(D); }
			C->SetMaterial(i, D);
			++Done;
		}
		return Done;
	}
	// ------------------------------------------------------------------ the kinds
	enum EKind : uint8
	{
		K_Oak, K_Aspen, K_Full1, K_Full2, K_Full3, K_Half2, K_Sapling,
		K_BushLeafy, K_BushRound, K_BushBerries, K_BushDome, K_ShrubLow, K_Plant, K_FlowersW, K_FlowersY,
		K_Log, K_Stump, K_StumpRooty, K_StumpMush, K_StumpGrass, K_Boulder, K_RockMossy, K_RockPale,
		K_Num
	};
	enum class ESolid : uint8 { None, Trunk, Box, Cyl, Own };   // Own: the mesh's own collision (the town's boulder)
	struct FKindDef
	{
		const TCHAR* Path;
		bool bTint;          // DZ: its leaves take one of the four colours
		bool bNanite;        // drawn by Nanite (no LODs to pick: a plain instanced set)
		ESolid Solid;
		float Crown;         // crown (or footprint) radius at scale 1, cm
		float TrunkR, TrunkH;   // the solid trunk at scale 1 (radius, height to where the crown starts: TreeGroves.cpp's TrunkOf), cm
		float Reach;         // drawn out to (cm; 0: as far as jeeb.Forest.Reach)
		bool bShadow;
	};
	const FKindDef Kinds[K_Num] = {
		{ TEXT("/Game/DZ_Assets/DZ_Trees/Meshes/Cork_Oak/SM_Cork_Oak_1.SM_Cork_Oak_1"), true, true, ESolid::Trunk, 570.f, 38.f, 370.f, 0.f, true },
		{ TEXT("/Game/DZ_Assets/DZ_Trees/Meshes/Aspen/SM_Columnar_Aspen_1.SM_Columnar_Aspen_1"), true, true, ESolid::Trunk, 235.f, 16.f, 510.f, 0.f, true },
		{ TEXT("/Game/PN_interactiveSpruceForest/Meshes/full/high/spruce_full_01.spruce_full_01"), false, false, ESolid::Trunk, 250.f, 22.f, 300.f, 0.f, true },
		{ TEXT("/Game/PN_interactiveSpruceForest/Meshes/full/high/spruce_full_02.spruce_full_02"), false, false, ESolid::Trunk, 340.f, 22.f, 300.f, 0.f, true },
		{ TEXT("/Game/PN_interactiveSpruceForest/Meshes/full/high/spruce_full_03.spruce_full_03"), false, false, ESolid::Trunk, 330.f, 22.f, 300.f, 0.f, true },
		{ TEXT("/Game/PN_interactiveSpruceForest/Meshes/half/high/spruce_half_02.spruce_half_02"), false, false, ESolid::Trunk, 210.f, 20.f, 600.f, 0.f, true },
		{ TEXT("/Game/PN_interactiveSpruceForest/Meshes/small/spruce_small_01.spruce_small_01"), false, false, ESolid::None, 26.f, 0.f, 0.f, 4500.f, false },
		{ TEXT("/Game/TownDecor/SM_BushLeafy.SM_BushLeafy"), false, false, ESolid::None, 72.f, 0.f, 0.f, 6000.f, false },
		{ TEXT("/Game/TownDecor/SM_BushRound.SM_BushRound"), false, false, ESolid::None, 73.f, 0.f, 0.f, 6000.f, false },
		{ TEXT("/Game/TownDecor/SM_BushBerries.SM_BushBerries"), false, false, ESolid::None, 70.f, 0.f, 0.f, 6000.f, false },
		{ TEXT("/Game/TownDecor/SM_BushDome.SM_BushDome"), false, false, ESolid::None, 60.f, 0.f, 0.f, 6000.f, false },
		{ TEXT("/Game/TownDecor/SM_ShrubLow.SM_ShrubLow"), false, false, ESolid::None, 44.f, 0.f, 0.f, 5000.f, false },
		{ TEXT("/Game/TownDecor/SM_PlantClump.SM_PlantClump"), false, false, ESolid::None, 60.f, 0.f, 0.f, 4500.f, false },
		{ TEXT("/Game/TownDecor/SM_BushWhiteFlowers.SM_BushWhiteFlowers"), false, false, ESolid::None, 61.f, 0.f, 0.f, 5000.f, false },
		{ TEXT("/Game/TownDecor/SM_BushBerries.SM_BushBerries"), false, false, ESolid::None, 61.f, 0.f, 0.f, 5000.f, false },   // was BushYellowFlowers (deleted 2026-11-16)
		{ TEXT("/Game/TownDecor/SM_LogMossy.SM_LogMossy"), false, false, ESolid::Box, 145.f, 0.f, 0.f, 9000.f, true },
		{ TEXT("/Game/TownDecor/SM_Stump.SM_Stump"), false, false, ESolid::Cyl, 66.f, 0.f, 0.f, 8000.f, true },
		{ TEXT("/Game/TownDecor/SM_StumpRooty.SM_StumpRooty"), false, false, ESolid::Cyl, 99.f, 0.f, 0.f, 8000.f, true },
		{ TEXT("/Game/TownDecor/SM_StumpMushroom.SM_StumpMushroom"), false, false, ESolid::Cyl, 84.f, 0.f, 0.f, 8000.f, true },
		{ TEXT("/Game/TownDecor/SM_StumpGrass.SM_StumpGrass"), false, false, ESolid::Cyl, 72.f, 0.f, 0.f, 8000.f, true },
		{ TEXT("/Game/TownDecor/SM_BoulderMossy.SM_BoulderMossy"), false, false, ESolid::Cyl, 120.f, 0.f, 0.f, 15000.f, true },
		{ TEXT("/Game/TownDecor/SM_BoulderMossy.SM_BoulderMossy"), false, false, ESolid::Cyl, 120.f, 0.f, 0.f, 9000.f, true },
		{ TEXT("/Game/TownDecor/SM_BoulderMossy.SM_BoulderMossy"), false, false, ESolid::Cyl, 92.f, 0.f, 0.f, 9000.f, true },   // was RockPale (deleted 2026-11-16)
	};
	bool IsTree(int32 K) { return K <= K_Half2; }
	bool IsSpruce(int32 K) { return K >= K_Full1 && K <= K_Half2; }

	// One thing placed: its kind, colour (DZ only), where (on the ground), turn and size
	struct FPut { uint8 Kind; uint8 Col; FVector At; float Yaw; FVector Scale; bool bTown; };

	// The ground as the world's 8 m mesh draws it, read from a grid of heights worked out once (the forest stands only on that mesh)
	struct FHeights
	{
		int32 I0 = 0, J0 = 0, NI = 0, NJ = 0;
		TArray<float> H;
		void Build(const FVector2D& Lo, const FVector2D& Hi)
		{
			const float S = JeebWorld::WorldStep;
			I0 = FMath::FloorToInt((Lo.X - JeebWorld::MinX) / S); J0 = FMath::FloorToInt((Lo.Y - JeebWorld::MinY) / S);
			NI = FMath::CeilToInt((Hi.X - JeebWorld::MinX) / S) - I0 + 2; NJ = FMath::CeilToInt((Hi.Y - JeebWorld::MinY) / S) - J0 + 2;
			H.SetNumUninitialized(NI * NJ);
			for (int32 i = 0; i < NI; ++i)
				for (int32 j = 0; j < NJ; ++j)
					H[i * NJ + j] = JeebTerrain::HeightAt(JeebWorld::MinX + (I0 + i) * S, JeebWorld::MinY + (J0 + j) * S);
		}
		float G(int32 i, int32 j) const { return H[FMath::Clamp(i - I0, 0, NI - 1) * NJ + FMath::Clamp(j - J0, 0, NJ - 1)]; }
		// height on the drawn triangles (each 8 m cell split along its B-C diagonal, as AWorldTerrain), and the slope (rise per run)
		float At(float X, float Y, float* OutSlope = nullptr) const
		{
			const float S = JeebWorld::WorldStep;
			const float GX = (X - JeebWorld::MinX) / S, GY = (Y - JeebWorld::MinY) / S;
			const int32 i = FMath::FloorToInt(GX), j = FMath::FloorToInt(GY);
			const float Fx = GX - i, Fy = GY - j;
			const float A = G(i, j), B = G(i + 1, j), C = G(i, j + 1), D = G(i + 1, j + 1);
			if (OutSlope) *OutSlope = FVector2D(0.5f * ((B - A) + (D - C)), 0.5f * ((C - A) + (D - B))).Size() / S;
			if (Fx + Fy <= 1.f) return A + (B - A) * Fx + (C - A) * Fy;
			return D + (C - D) * (1.f - Fx) + (B - D) * (1.f - Fy);
		}
	};

	// Spacing: every placed thing's footprint, in 10 m buckets
	struct FSpace
	{
		struct FItem { FVector2D P; float R; bool bTree; };
		TArray<FItem> Items;
		TMap<FIntPoint, TArray<int32>> Cells;
		static FIntPoint Key(const FVector2D& P) { return FIntPoint(FMath::FloorToInt(P.X / 1000.f), FMath::FloorToInt(P.Y / 1000.f)); }
		void Add(const FVector2D& P, float R, bool bTree) { Cells.FindOrAdd(Key(P)).Add(Items.Add({ P, R, bTree })); }
		// is P (radius R) clear of every item by Gap times their radii' sum (trees: Gap; props against anything: their footprints)
		bool Clear(const FVector2D& P, float R, float TreeGap, float PropGap) const
		{
			const FIntPoint K = Key(P);
			for (int32 dx = -2; dx <= 2; ++dx)
				for (int32 dy = -2; dy <= 2; ++dy)
					if (const TArray<int32>* L = Cells.Find(FIntPoint(K.X + dx, K.Y + dy)))
						for (int32 i : *L)
						{
							const FItem& It = Items[i];
							const float Want = It.bTree ? TreeGap * (R + It.R) : PropGap + R + It.R;
							if (FVector2D::DistSquared(P, It.P) < Want * Want) return false;
						}
			return true;
		}
	};

	float Noise(const FVector2D& P, float Scale, float Ox, float Oy) { return FMath::PerlinNoise2D(FVector2D(P.X / Scale + Ox, P.Y / Scale + Oy)); }

	// Which of the four colours a neighbourhood takes: two slow noises split the land into patches (about 80-150 m), one tree in eight takes
	// another colour. Owner, 2026-11-16 (the trees were too colourful): green gets about 40% of the land and of the strays, the rest is shared
	// among red (a quarter of it), orange and yellow. Deterministic: the same noise and the same stream draws on every machine.
	int32 ColourAt(const FVector2D& P, FRandomStream& R)
	{
		const float A = Noise(P, 9000.f, 11.3f, 4.1f), B = Noise(P, 11000.f, -7.7f, 19.2f);
		int32 C = A < -0.08f ? 0 : (B < -0.15f ? 1 : B < 0.15f ? 2 : 3);   // 0 green, 1 red, 2 orange, 3 yellow
		if (R.FRand() < 0.125f) C = R.FRand() < 0.4f ? 0 : 1 + R.RandRange(0, 2);
		return C;
	}

	float BoxGap(float X, float Y)   // how far outside Oak Hill's box (cm; 0 inside)
	{
		const float Dx = FMath::Max3(JeebOakHill::BoxX0 - X, 0.f, X - JeebOakHill::BoxX1), Dy = FMath::Max3(JeebOakHill::BoxY0 - Y, 0.f, Y - JeebOakHill::BoxY1);
		return FMath::Sqrt(Dx * Dx + Dy * Dy);
	}
	bool OnForestRoadBox(float X, float Y, float Pad)   // near a forest road at all (a quick box test before PathClearance)
	{
		for (const JeebTerrain::FPathDef& P : JeebTerrain::PathDefs())
			if (P.Kind == 2 && P.Bounds.ExpandBy(P.MaxHalf + Pad).IsInside(FVector2D(X, Y))) return true;
		return false;
	}
}

// ------------------------------------------------------------------ the roads
// A road walks out from a town road's end toward a far town, 15 m a step (6 m inside the town), each step choosing among headings
// within 80 degrees the one that climbs least and turns least while heading on: off the water, the rivers' banks, the original square
// and the town's plots. It stops at the forest's edge or where no step is left (the river). Then smoothed like every path.
const TArray<TArray<FVector2D>>& JeebOakForest::RoadLines()
{
	static const TArray<TArray<FVector2D>> Lines = []()
	{
		TArray<TArray<FVector2D>> A;
		if (!JeebOakHill::Enabled()) return A;
		using namespace JeebOakHill;
		const FVector2D Plaza(PosX, PosY);
		auto TownAt = [](const TCHAR* Name) { for (const JeebWorld::FTown& T : JeebWorld::Towns()) if (FCString::Strcmp(T.Name, Name) == 0) return T.Pos; return FVector2D(PosX, PosY); };
		auto Ok = [&](const FVector2D& Q)
		{
			if (JeebWorld::InWater(Q.X, Q.Y)) return false;
			float Half; if (JeebWorld::RiverDistance(Q.X, Q.Y, Half) - Half < 900.f) return false;
			if (FMath::Max(FMath::Abs(Q.X), FMath::Abs(Q.Y)) < JeebTerrain::GroundHalf + 600.f) return false;
			if (FVector2D::Distance(Q, Plaza) > Reach + 3000.f) return false;
			if (InBox(Q.X, Q.Y, 300.f))
			{
				const float E = EastM(Q.Y), N = NorthM(Q.X);
				for (const FPlot& P : Plots()) if (PlotSD(P, E, N) < (P.Kind == EPlot::Building ? 2.f : 0.5f)) return false;
			}
			return true;
		};
		struct FPlan { float Px, Py; const TCHAR* Toward; };
		const FPlan Plans[] = {
			{ 395.f, 0.f, TEXT("Silver Point") },   // the north road's end, past the castle ruins: north-east
			{ -1.f, 0.f, TEXT("Anchorage") },       // a fork off the north road 40 m past the town (the town has no east road end)
			{ 600.f, 650.f, TEXT("Birch Grove") },  // the road behind the bank, south-east: to the river's east branch
		};
		TArray<FVector2D> First;   // (the north road's steps, for the fork)
		for (const FPlan& Pl : Plans)
		{
			FVector2D Start = FromPx(FMath::Max(Pl.Px, 0.f), Pl.Py);
			if (Pl.Px < 0.f) { for (const FVector2D& F : First) if (BoxGap(F.X, F.Y) > 4000.f) { Start = F; break; } }
			const FVector2D Goal = Plaza + (TownAt(Pl.Toward) - Plaza).GetSafeNormal() * (Reach + 20000.f);
			TArray<FVector2D> Ctrl = { Start };
			FVector2D P = Start, Head = (Goal - Start).GetSafeNormal();
			for (int32 s = 0; s < 120 && FVector2D::Distance(P, Plaza) < Reach + 1500.f; ++s)
			{
				const bool bTown = InBox(P.X, P.Y, 1500.f);
				const float Step = bTown ? 600.f : 1500.f;
				const FVector2D G = (Goal - P).GetSafeNormal();
				const FVector2D Base = (Head * 0.55f + G * 0.45f).GetSafeNormal();
				const float H0 = JeebTerrain::HeightAt(P.X, P.Y, true, 1);   // (the hills before the roads are levelled: Hills.h)
				float Best = 1.0e9f; FVector2D BestQ = P;
				const int32 Fan = (bTown && s < 4) ? 17 : 8;   // (leaving town: any way out, the road end may face a house)
				for (int32 a = -Fan; a <= Fan; ++a)
				{
					const FVector2D D = Base.GetRotated(a * 10.f);
					const FVector2D Q = P + D * Step;
					if (!Ok(Q) || !Ok(P + D * Step * 0.5f) || (bTown && (!Ok(P + D * Step * 0.25f) || !Ok(P + D * Step * 0.75f)))) continue;
					const float Grade = FMath::Abs(JeebTerrain::HeightAt(Q.X, Q.Y, true, 1) - H0) / Step;
					const float Cost = Grade * 6.f + FMath::Abs(a) * 0.06f + (1.f - FVector2D::DotProduct(D, G)) * 0.9f;   // (climb weighed 6, was 14: with the hills 14 sent the north road round them to the south-east; JeebHills levels each road to 5 degrees, cutting through the saddles)
					if (Cost < Best) { Best = Cost; BestQ = Q; }
				}
				if (Best >= 1.0e9f) break;
				Head = (BestQ - P).GetSafeNormal();
				P = BestQ;
				Ctrl.Add(P);
			}
			UE_LOG(LogTemp, Log, TEXT("JEEBFOREST road toward %s: %d steps from %.0f %.0f (start %s), ended %.0f m from the plaza"), Pl.Toward, Ctrl.Num() - 1, Start.X, Start.Y, Ok(Start) ? TEXT("clear") : TEXT("blocked"), FVector2D::Distance(P, Plaza) / 100.f);
			if (First.Num() == 0) First = Ctrl;
			if (Ctrl.Num() >= 3) A.Add(JeebTerrain::SmoothPath(Ctrl, false));
		}
		return A;
	}();
	return Lines;
}

// ------------------------------------------------------------------ the actor
AOakForest::AOakForest()
{
	PrimaryActorTick.bCanEverTick = true;    // (players only, for the near trees' shadows; turned on in BeginPlay)
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickInterval = 0.25f;
	bReplicates = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	SetRootComponent(Root);
}

struct FOakForestBuild
{
	AOakForest* Self;
	UWorld* World;
	bool bServer;
	TArray<FPut> Puts;
	FSpace Space;
	int32 TownTrees = 0, FromCsv = 0, ForestTrees = 0;

	// ---------------------------------------------------------------- the town's trees
	// FillTrees.csv's marked spots first (shade: big cork oaks; orchard: small cork oaks; street: aspens; conifer: spruces), then the gaps
	// between the plots as the old woods' trees stood (the same seed and tests as AOakHillTown's old PlantTrees: few in the middle of town,
	// more toward its edges, never on a road, a plot, the lawn or a river bank, at least 7 m apart): cork oaks and aspens at 2:1, the old
	// pines' places spruces. Each cork oak's crown is kept a metre off the buildings (smaller, or an aspen if even a small one won't fit).
	void Town(float OffY)
	{
		using namespace JeebOakHill;
		if (!Enabled()) return;
		FRandomStream R(5151);
		FSpace Near;   // (this town's own spacing, in plan coordinates: the TownLab copy is checked against itself)
		int32 Oaks = 0, Aspens = 0, Trees = 0, Csv0 = FromCsv;
		auto Put = [&](int32 Kind, const FVector2D& Plan, float Scale, int32 Col)
		{
			const float Yaw = R.FRandRange(0.f, 360.f);
			// crown off the buildings: shrink, then fall back to an aspen
			if (Kind == K_Oak)
			{
				const float Room = BuildingDistance(Plan.X, Plan.Y) - 100.f;
				if (Room < Kinds[K_Oak].Crown * Scale) Scale = Room / Kinds[K_Oak].Crown;
				if (Scale < 0.42f) { Kind = K_Aspen; Scale = 0.55f; }
			}
			if (Kind == K_Oak) ++Oaks; else if (Kind == K_Aspen) ++Aspens;
			const FVector2D P = Plan + FVector2D(0.f, OffY);
			Puts.Add({ uint8(Kind), uint8(Col), FVector(P.X, P.Y, JeebTerrain::HeightAt(P.X, P.Y) - 4.f), Yaw, FVector(Scale), true });
			Near.Add(Plan, Kinds[Kind].Crown * Scale, true);
			Space.Add(P, Kinds[Kind].Crown * Scale, true);
			++Trees;
		};
		auto TownColour = [&](const FVector2D& Plan) { return ColourAt(Plan * 1.6f, R); };   // (smaller patches than the forest's: a street or two each)
		// 1. FillTrees.csv
		TArray<FString> Lines;
		const FString Csv = FPaths::ProjectContentDir() / TEXT("Data/FillTrees.csv");
		if (FFileHelper::LoadFileToStringArray(Lines, *Csv))
		{
			for (const FString& L : Lines)
			{
				if (L.StartsWith(TEXT("#")) || L.StartsWith(TEXT("Site"))) continue;
				TArray<FString> C; L.ParseIntoArray(C, TEXT(","), false);
				if (C.Num() < 4 || !C[0].TrimStartAndEnd().Equals(TEXT("OakHill"), ESearchCase::IgnoreCase)) continue;
				const float E = FCString::Atof(*C[1]), N = FCString::Atof(*C[2]);
				const FString Class = C[3].TrimStartAndEnd().ToLower();
				const FVector2D Plan(PosX + N * 100.f, PosY + E * 100.f);
				const int32 Col = TownColour(Plan);
				if (Class == TEXT("conifer")) Put(R.FRand() < 0.7f ? K_Full1 + R.RandRange(0, 2) : K_Half2, Plan, R.FRandRange(0.6f, 0.85f), 0);
				else if (Class == TEXT("street")) Put(K_Aspen, Plan, R.FRandRange(0.5f, 0.62f), Col);
				else if (Class == TEXT("orchard")) Put(K_Oak, Plan, R.FRandRange(0.42f, 0.5f), Col);
				else Put(K_Oak, Plan, R.FRandRange(0.62f, 0.82f), Col);
				++FromCsv;
			}
		}
		else UE_LOG(LogTemp, Warning, TEXT("JEEBFOREST no %s: the town's marked tree spots are left out"), *Csv);
		// 2. the gaps between the plots (the old woods' spots)
		FRandomStream S(5150);
		TArray<FVector2D> Got;
		int32 Tries = 0;
		while (Got.Num() < 150 && Tries < 4000)
		{
			++Tries;
			const float E = S.FRandRange(-50.f, 84.f), N = S.FRandRange(-110.f, 116.f);
			const float Roll = S.FRand(), Kind = S.FRand(), Sc = S.FRandRange(0.85f, 1.2f);
			const float FromPlaza = FVector2D(E, N).Size();
			const float Want = FMath::Lerp(0.12f, 0.7f, JeebWorld::Smooth(32.f, 66.f, FromPlaza));
			if (Roll > Want) continue;
			const float X = PosX + N * 100.f, Y = PosY + E * 100.f;
			if (JeebWorld::NearRiver(X, Y, 600.f)) continue;
			if (JeebTerrain::PathClearance(X, Y) < 450.f) continue;
			bool bClear = true;
			for (const FPlot& P : Plots()) if (PlotSD(P, E, N) < (P.Kind == EPlot::Building ? 6.f : 3.f)) { bClear = false; break; }
			if (!bClear) continue;
			for (const FVector2D& G : Got) if (FVector2D::DistSquared(G, FVector2D(X, Y)) < 700.f * 700.f) { bClear = false; break; }
			if (!bClear) continue;
			Got.Add(FVector2D(X, Y));
			const FVector2D Plan(X, Y);
			if (!Near.Clear(Plan, 150.f, 0.f, 450.f)) continue;   // (not on top of a marked spot)
			const int32 Col = TownColour(Plan);
			if (FromPlaza > 50.f && Kind < 0.25f) { Put(Kind < 0.17f ? K_Full1 + int32(Kind * 100.f) % 3 : K_Half2, Plan, 0.55f + 0.35f * (Sc - 0.85f) / 0.35f, 0); continue; }
			// cork oaks and aspens at 2:1 (whichever is behind its share; the oak's crown test may still turn one into an aspen)
			const bool bAspen = Aspens * 2 < Oaks;
			if (bAspen) Put(K_Aspen, Plan, 0.5f + 0.25f * (Sc - 0.85f) / 0.35f, Col);
			else Put(K_Oak, Plan, 0.6f + 0.3f * (Sc - 0.85f) / 0.35f, Col);
		}
		TownTrees += Trees;
		UE_LOG(LogTemp, Log, TEXT("JEEBFOREST town%s: %d trees (%d at FillTrees.csv's spots): %d cork oaks, %d aspens (fat:skinny %.2f:1), %d spruces"),
			OffY != 0.f ? TEXT(" (TownLab's)") : TEXT(""), Trees, FromCsv - Csv0, Oaks, Aspens, Aspens > 0 ? float(Oaks) / Aspens : 0.f, Trees - Oaks - Aspens);
	}

	// ---------------------------------------------------------------- the forest
	void Forest()
	{
		using namespace JeebOakHill;
		if (!Enabled()) return;
		const float Density = FMath::Clamp(CVarDensity.GetValueOnGameThread(), 0.f, 3.f);
		const FVector2D Plaza(PosX, PosY);
		const double T0 = FPlatformTime::Seconds();
		FHeights Hs;
		Hs.Build(Plaza - FVector2D(JeebOakForest::Reach + 1600.f), Plaza + FVector2D(JeebOakForest::Reach + 1600.f));
		const bool bLab = JeebTownLab::Active();

		// Where the forest may grow, and how much (0 .. 1): its edges fade over 40 m toward the town, the square and the outer rim, and
		// over 18 m toward the river banks
		auto Edge = [&](float X, float Y) -> float
		{
			const float D = FVector2D::Distance(FVector2D(X, Y), Plaza);
			if (D > JeebOakForest::Reach) return 0.f;
			const float Sq = FMath::Max(FMath::Abs(X), FMath::Abs(Y)) - JeebTerrain::GroundHalf;
			if (Sq < 1200.f) return 0.f;
			const float Town = BoxGap(X, Y);
			if (Town < 2500.f) return 0.f;
			if (bLab && JeebTownLab::InSite(JeebTownLab::LabSite(), X, Y, 2500.f)) return 0.f;
			if (JeebWorld::InWater(X, Y)) return 0.f;
			float Half; const float Rd = JeebWorld::RiverDistance(X, Y, Half) - Half;
			if (Rd < 1200.f) return 0.f;
			return JeebWorld::Smooth(2500.f, 6500.f, Town) * JeebWorld::Smooth(JeebOakForest::Reach, JeebOakForest::Reach - 4000.f, D)
				* JeebWorld::Smooth(1200.f, 3000.f, Rd) * JeebWorld::Smooth(1200.f, 5200.f, Sq);
		};

		// ---- trees: a jittered 6.5 m lattice; each cell's tree is kept by the stand's density and its spacing from the trees before it
		FRandomStream R(9090);
		const float Cell = 650.f;
		const int32 N = FMath::CeilToInt(2.f * JeebOakForest::Reach / Cell);
		int32 Count[K_Num] = {}, ColCount[4] = {}, Seedlings = 0;
		TArray<FVector2D> Open;   // (cells with room for undergrowth)
		for (int32 i = 0; i < N; ++i)
			for (int32 j = 0; j < N; ++j)
			{
				const float X = Plaza.X - JeebOakForest::Reach + (i + R.FRand()) * Cell, Y = Plaza.Y - JeebOakForest::Reach + (j + R.FRand()) * Cell;
				const float Keep = R.FRand(), Pick = R.FRand(), Pick2 = R.FRand(), Sc = R.FRand(), Yaw = R.FRandRange(0.f, 360.f);   // (every number drawn before any skip)
				const float W = Edge(X, Y);
				if (W <= 0.f) continue;
				const FVector2D P(X, Y);
				float Slope;
				const float Z = Hs.At(X, Y, &Slope);
				if (Slope > 0.72f) continue;   // (36 degrees: nothing on steeper ground; 0.5 before the hills, 2026-11-16, whose slopes reach 35 so they stay forested)
				if (OnForestRoadBox(X, Y, 600.f) && JeebTerrain::PathClearance(X, Y) < 400.f) continue;
				// glades: a slow noise opens clearings here and there; hollows (lower than the ground 30 m round) are thicker, rises thinner
				const float Glade = JeebWorld::Smooth(-0.32f, -0.12f, Noise(P, 9000.f, 3.3f, 8.8f));
				const float Around = 0.25f * (Hs.At(X + 3000.f, Y) + Hs.At(X - 3000.f, Y) + Hs.At(X, Y + 3000.f) + Hs.At(X, Y - 3000.f));
				const float Hollow = FMath::Clamp((Around - Z) / 200.f, -1.f, 1.f);   // +1 two metres down in a hollow, -1 two metres up on a rise
				const float RoadSide = OnForestRoadBox(X, Y, 1500.f) ? JeebWorld::Smooth(400.f, 1500.f, JeebTerrain::PathClearance(X, Y)) : 1.f;
				const float Want = Density * W * FMath::Lerp(0.05f, 1.f, Glade) * (0.78f + 0.2f * Hollow) * FMath::Lerp(0.55f, 1.f, RoadSide);
				if (Keep > Want) { if (W > 0.3f) Open.Add(P); continue; }
				// what grows here: conifer stands on the rises and where their noise says, broadleaves in the hollows
				const float ConiferField = 0.5f + 0.5f * Noise(P, 7000.f, -4.4f, 1.7f);
				const float ConiferP = FMath::Clamp(0.08f + 0.85f * JeebWorld::Smooth(0.42f, 0.72f, ConiferField) - 0.15f * Hollow, 0.03f, 0.92f);
				int32 Kind; float Scale; int32 Col = 0;
				if (Pick < ConiferP)
				{
					if (Pick2 < 0.6f) { Kind = K_Full1 + FMath::Min(int32(Pick2 / 0.2f), 2); Scale = FMath::Lerp(0.9f, 1.35f, Sc); }
					else if (Pick2 < 0.8f) { Kind = K_Half2; Scale = FMath::Lerp(0.9f, 1.2f, Sc); }
					else { Kind = K_Full1 + int32((Pick2 - 0.8f) / 0.0667f) % 3; Scale = FMath::Lerp(0.4f, 0.65f, Sc); }   // young spruces
				}
				else
				{
					// cork oaks and aspens: aspens grow in their own clumps (clonal stands), oaks everywhere else
					const float AspenField = 0.5f + 0.5f * Noise(P, 5000.f, 6.1f, -2.9f);
					const bool bAspen = Pick2 < 0.15f + 0.6f * JeebWorld::Smooth(0.5f, 0.75f, AspenField);
					Kind = bAspen ? K_Aspen : K_Oak;
					Scale = bAspen ? FMath::Lerp(0.75f, 1.1f, Sc) : FMath::Lerp(0.72f, 1.12f, Sc);
					Col = ColourAt(P, R);
				}
				const float Crown = Kinds[Kind].Crown * Scale;
				if (!Space.Clear(P, Crown, 0.55f, 150.f)) { if (W > 0.3f) Open.Add(P); continue; }
				Space.Add(P, Crown, true);
				Puts.Add({ uint8(Kind), uint8(Col), FVector(X, Y, Z - 6.f - Slope * 45.f * Scale), Yaw, FVector(Scale), false });   // (on a slope the trunk sinks by its downhill side: the hills, 2026-11-16)
				++Count[Kind]; if (Kinds[Kind].bTint) ++ColCount[Col];
				++ForestTrees;
			}

		// ---- undergrowth: bushes, saplings, young spruces, fallen logs and stumps in the open cells
		FRandomStream U(9191);
		int32 Under = 0;
		const float UnderWant = FMath::Min(Density, 1.5f);
		for (const FVector2D& C : Open)
		{
			const float Roll = U.FRand(), Pick = U.FRand(), Sc = U.FRand(), Yaw = U.FRandRange(0.f, 360.f), Ox = U.FRandRange(-250.f, 250.f), Oy = U.FRandRange(-250.f, 250.f);
			if (Roll > 0.62f * UnderWant) continue;
			const FVector2D P = C + FVector2D(Ox, Oy);
			float Slope;
			const float Z = Hs.At(P.X, P.Y, &Slope);
			if (Slope > 0.75f || Edge(P.X, P.Y) <= 0.f) continue;
			if (OnForestRoadBox(P.X, P.Y, 600.f) && JeebTerrain::PathClearance(P.X, P.Y) < 160.f) continue;
			const float ConiferField = 0.5f + 0.5f * Noise(P, 7000.f, -4.4f, 1.7f);
			const bool bConifer = ConiferField > 0.55f;
			int32 Kind; float Scale = 1.f;
			if (Pick < 0.06f) { if (Slope > 0.2f) continue; Kind = K_Log; Scale = FMath::Lerp(0.9f, 1.5f, Sc); }   // (a long log lies only on gentle ground: on a hillside its ends would float)
			else if (Pick < 0.11f) { const int32 St[4] = { K_Stump, K_StumpRooty, K_StumpMush, K_StumpGrass }; Kind = St[int32(Sc * 3.99f)]; Scale = FMath::Lerp(0.8f, 1.15f, Sc); }
			else if (Pick < (bConifer ? 0.4f : 0.2f)) { Kind = K_Sapling; Scale = FMath::Lerp(1.6f, 3.2f, Sc); }
			else if (Pick < (bConifer ? 0.47f : 0.25f)) { Kind = K_Full1 + int32(Sc * 2.99f); Scale = FMath::Lerp(0.18f, 0.3f, Sc); ++Seedlings; }   // a spruce seedling (in its kind's count below)
			else
			{
				const int32 Bu[8] = { K_BushLeafy, K_BushRound, K_BushBerries, K_BushDome, K_ShrubLow, K_Plant, K_ShrubLow, K_BushLeafy };
				Kind = Bu[int32(Sc * 7.99f)];
				if (JeebWorld::Smooth(-0.32f, -0.12f, Noise(P, 9000.f, 3.3f, 8.8f)) < 0.5f && Sc > 0.75f) Kind = Sc > 0.87f ? K_FlowersW : K_FlowersY;   // flowering bushes in the glades
				Scale = FMath::Lerp(0.8f, 1.5f, U.FRand());
			}
			const float Foot = Kinds[Kind].Crown * Scale;
			const bool bSolid = Kinds[Kind].Solid != ESolid::None;
			// solids keep 60 cm clear of every trunk and solid; bushes may tuck in under the crowns but not into a trunk
			if (!Space.Clear(P, bSolid ? Foot : 40.f, 0.12f, bSolid ? 60.f : 10.f)) continue;
			if (bSolid) Space.Add(P, Foot, false);
			Puts.Add({ uint8(Kind), 0, FVector(P.X, P.Y, Z - (Kind == K_Log ? 8.f : 3.f) - Slope * 0.5f * FMath::Min(Foot, 100.f)), Yaw, FVector(Scale), false });
			++Count[Kind]; ++Under;
		}

		// ---- boulders: clumps of one to four big stones and a few smaller rocks round them
		FRandomStream B(9292);
		int32 Rocks = 0;
		const int32 Clumps = FMath::RoundToInt(240 * FMath::Min(Density, 1.5f));
		for (int32 c = 0; c < Clumps; ++c)
		{
			const float A = B.FRandRange(0.f, 2.f * PI), D = JeebOakForest::Reach * FMath::Sqrt(B.FRand());
			const FVector2D Ctr = Plaza + FVector2D(FMath::Cos(A), FMath::Sin(A)) * D;
			const int32 Big = B.RandRange(1, 4), Small = B.RandRange(1, 4);
			for (int32 k = 0; k < Big + Small; ++k)
			{
				const bool bBig = k < Big;
				const FVector2D P = Ctr + FVector2D(B.FRandRange(-1.f, 1.f), B.FRandRange(-1.f, 1.f)) * (bBig ? 380.f : 650.f);
				const float S = bBig ? B.FRandRange(0.9f, 2.4f) * (k == 0 ? 1.25f : 0.85f) : B.FRandRange(0.5f, 1.0f);
				const float Sx = S * B.FRandRange(0.9f, 1.35f), Sy = S * B.FRandRange(0.8f, 1.2f), Sz = S * B.FRandRange(0.55f, 0.8f), Yaw = B.FRandRange(0.f, 360.f);
				const int32 Kind = bBig ? K_Boulder : (B.FRand() < 0.5f ? K_RockMossy : K_RockPale);
				if (Edge(P.X, P.Y) <= 0.f) continue;
				float Slope;
				const float Z = Hs.At(P.X, P.Y, &Slope);
				if (Slope > 0.8f) continue;
				if (OnForestRoadBox(P.X, P.Y, 800.f) && JeebTerrain::PathClearance(P.X, P.Y) < 250.f) continue;
				const float Foot = Kinds[Kind].Crown * FMath::Max(Sx, Sy);
				if (!Space.Clear(P, Foot, 0.15f, 40.f)) continue;
				Space.Add(P, Foot, false);
				Puts.Add({ uint8(Kind), 0, FVector(P.X, P.Y, Z - (bBig ? 0.18f * Sz * 100.f : 0.15f * Sz * 100.f) - Slope * 0.5f * Foot), Yaw, FVector(Sx, Sy, Sz), false });
				++Count[Kind]; ++Rocks;
			}
		}

		FString List;
		for (int32 k = 0; k < K_Num; ++k) if (Count[k]) List += FString::Printf(TEXT(" %s x%d"), *FPaths::GetBaseFilename(Kinds[k].Path), Count[k]);
		const int32 Fat = Count[K_Oak], Thin = Count[K_Aspen];
		UE_LOG(LogTemp, Log, TEXT("JEEBFOREST forest: %d trees (%d cork oaks, %d aspens, %d spruces; fat:skinny %.2f:1; DZ colours green %d red %d orange %d yellow %d), %d undergrowth (%d of them spruce seedlings, counted with their kind below), %d stones, density %.2f, planned in %.0f ms:%s"),
			ForestTrees, Fat, Thin, ForestTrees - Fat - Thin, Thin ? float(Fat) / Thin : 0.f, ColCount[0], ColCount[1], ColCount[2], ColCount[3], Under, Seedlings, Rocks, Density, (FPlatformTime::Seconds() - T0) * 1000.0, *List);
	}

	// ---------------------------------------------------------------- make it
	void Make()
	{
		const double T0 = FPlatformTime::Seconds();
		USceneComponent* Root = Self->GetRootComponent();
		UStaticMesh* Meshes[K_Num] = {};
		for (int32 k = 0; k < K_Num; ++k)
		{
			Meshes[k] = LoadObject<UStaticMesh>(nullptr, Kinds[k].Path, nullptr, LOAD_Quiet | LOAD_NoWarn);
			if (!Meshes[k]) UE_LOG(LogTemp, Warning, TEXT("JEEBFOREST missing %s: left out"), Kinds[k].Path);
		}
		// ---- the solids, on every machine: hidden instanced shapes (trunks, stumps and small rocks as cylinders, logs as boxes) blocking
		// players and arrows (Visibility) but not the camera; the town's boulder by its own collision
		UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		auto MakeSolid = [&](UStaticMesh* M, const TCHAR* Name) -> UInstancedStaticMeshComponent*
		{
			if (!M) return nullptr;
			UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(Self, Name);
			C->SetStaticMesh(M);
			C->SetupAttachment(Root);
			C->SetMobility(EComponentMobility::Static);
			C->SetCollisionProfileName(TEXT("BlockAll"));
			C->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
			C->SetCanEverAffectNavigation(false);
			C->SetVisibility(false);
			C->SetHiddenInGame(true);
			C->SetCastShadow(false);
			Self->Solids.Add(C);
			return C;
		};
		UInstancedStaticMeshComponent* Trunks = MakeSolid(Cyl, TEXT("ForestTrunks"));
		UInstancedStaticMeshComponent* Logs = MakeSolid(Cube, TEXT("ForestLogs"));
		UInstancedStaticMeshComponent* Cyls = MakeSolid(Cyl, TEXT("ForestStumpsRocks"));
		TArray<FTransform> TrunkX, LogX, CylX;
		uint32 Hash = 2166136261u;
		auto HashIn = [&](float V) { const int32 Q = FMath::RoundToInt(V * 10.f); for (int32 b = 0; b < 4; ++b) { Hash ^= uint32((Q >> (8 * b)) & 0xFF); Hash *= 16777619u; } };
		TMap<int32, TArray<FTransform>> Visual;   // (kind * 4 + colour)
		TArray<FTransform> OwnSolid[K_Num];
		for (const FPut& P : Puts)
		{
			const FKindDef& K = Kinds[P.Kind];
			if (!Meshes[P.Kind]) continue;
			const FTransform Xf(FRotator(0.f, P.Yaw, 0.f), P.At, P.Scale);
			Visual.FindOrAdd(P.Kind * 4 + P.Col).Add(Xf);
			const FBox Bx = Meshes[P.Kind]->GetBoundingBox();
			switch (K.Solid)
			{
			case ESolid::Trunk:
			{
				const float Rad = K.TrunkR * P.Scale.X, H = FMath::Max(K.TrunkH * P.Scale.Z, 300.f) + 40.f;
				if (Rad < 6.f) break;   // (a seedling: walked through)
				TrunkX.Add(FTransform(FRotator::ZeroRotator, P.At + FVector(0.f, 0.f, H * 0.5f - 40.f), FVector(Rad / 50.f, Rad / 50.f, H / 100.f)));
				break;
			}
			case ESolid::Box:
			{
				const FVector Sz = Bx.GetSize() * P.Scale, Ctr = Xf.TransformPosition(Bx.GetCenter());
				LogX.Add(FTransform(FRotator(0.f, P.Yaw, 0.f), Ctr, FVector(Sz.X * 0.92f, Sz.Y * 0.8f, Sz.Z * 0.85f) / 100.f));
				break;
			}
			case ESolid::Cyl:
			{
				const FVector Sz = Bx.GetSize() * P.Scale;
				const float Rad = 0.42f * FMath::Min(Sz.X, Sz.Y), H = Sz.Z * 0.9f;
				CylX.Add(FTransform(FRotator(0.f, P.Yaw, 0.f), P.At + FVector(0.f, 0.f, H * 0.5f), FVector(Rad / 50.f, Rad / 50.f, H / 100.f)));
				break;
			}
			case ESolid::Own: OwnSolid[P.Kind].Add(Xf); break;
			default: break;
			}
			if (K.Solid != ESolid::None) { HashIn(P.At.X); HashIn(P.At.Y); HashIn(P.At.Z); HashIn(P.Scale.X); }
		}
		auto Fill = [](UInstancedStaticMeshComponent* C, const TArray<FTransform>& X) { if (!C) return; C->RegisterComponent(); if (X.Num()) C->AddInstances(X, false, true, false); };
		Fill(Trunks, TrunkX); Fill(Logs, LogX); Fill(Cyls, CylX);
		for (int32 k = 0; k < K_Num; ++k)   // (the boulder: its own collision; drawn on clients, hidden on the server)
			if (OwnSolid[k].Num() && Meshes[k])
			{
				UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(Self, TEXT("ForestBoulders"));
				C->SetStaticMesh(Meshes[k]);
				C->SetupAttachment(Root);
				C->SetMobility(EComponentMobility::Static);
				C->SetCollisionProfileName(TEXT("BlockAll"));
				C->SetCanEverAffectNavigation(false);
				if (bServer) { C->SetVisibility(false); C->SetHiddenInGame(true); }
				else { Self->Drawn.Add(C); Self->DrawnReach.Add(Kinds[k].Reach); Self->DrawnIsTree.Add(false); Self->DrawnShadow.Add(true); }
				C->SetCullDistances(0, int32(Kinds[k].Reach));
				Fill(C, OwnSolid[k]);
			}
		int32 Sets = 0, Instances = 0, Swapped = 0;
		if (!bServer)
		{
			for (int32 F = 0; F < 4; ++F) GTintMids[F].Reset();
			GSpruceMids.Reset();
			for (TPair<int32, TArray<FTransform>>& V : Visual)
			{
				const int32 Kd = V.Key / 4, Col = V.Key % 4;
				if (Kinds[Kd].Solid == ESolid::Own) continue;
				const FKindDef& K = Kinds[Kd];
				// Nanite trees: one plain instanced set (Nanite culls each instance itself); the rest hierarchical, so far clusters cull and pick their LOD together
				UInstancedStaticMeshComponent* C = K.bNanite ? NewObject<UInstancedStaticMeshComponent>(Self) : NewObject<UHierarchicalInstancedStaticMeshComponent>(Self);
				C->SetStaticMesh(Meshes[Kd]);
				C->SetupAttachment(Root);
				C->SetMobility(EComponentMobility::Static);
				C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				C->SetCanEverAffectNavigation(false);
				const bool bTree = IsTree(Kd);
				C->SetCastShadow(K.bShadow && !bTree);
				C->bCastFarShadow = false;
				C->SetWorldPositionOffsetDisableDistance(bTree ? 6000 : 3000);   // (the wind stops swaying them beyond 60 m)
				if (K.bTint) Swapped += TintLeaves(C, Col);
				if (IsSpruce(Kd) || Kd == K_Sapling) Swapped += TintSpruceSet(C, Self);
				Self->Drawn.Add(C); Self->DrawnReach.Add(K.Reach); Self->DrawnIsTree.Add(bTree); Self->DrawnShadow.Add(K.bShadow && !bTree);
				C->RegisterComponent();
				C->AddInstances(V.Value, false, true, false);
				++Sets; Instances += V.Value.Num();
				if (bTree && K.bShadow)   // its hidden shadow twin, filled near the camera (AOakForest::UpdateShadows)
				{
					UInstancedStaticMeshComponent* S = K.bNanite ? NewObject<UInstancedStaticMeshComponent>(Self) : NewObject<UHierarchicalInstancedStaticMeshComponent>(Self);
					S->SetStaticMesh(Meshes[Kd]);
					S->SetupAttachment(Root);
					S->SetMobility(EComponentMobility::Movable);   // (refilled at run time)
					S->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					S->SetCanEverAffectNavigation(false);
					S->SetHiddenInGame(true);
					S->bCastHiddenShadow = true;
					S->SetCastShadow(true);
					S->bCastFarShadow = false;
					S->SetWorldPositionOffsetDisableDistance(6000);
					if (K.bTint) TintLeaves(S, Col);
					if (IsSpruce(Kd) || Kd == K_Sapling) TintSpruceSet(S, Self);
					S->RegisterComponent();
					const int32 Si = Self->ShadowSets.Add(S);
					Self->ShadowAll.Add(V.Value);
					Self->ShadowHash.Add(0u);
					// and its kind's hidden stand-in set (one per kind, whatever the colour), filled past the twins' reach
					int32 Sh = Self->ShadeKind.Find(Kd);
					if (Sh == INDEX_NONE)
					{
						UInstancedStaticMeshComponent* P = NewObject<UInstancedStaticMeshComponent>(Self);
						P->SetupAttachment(Root);
						P->SetMobility(EComponentMobility::Movable);
						P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
						P->SetCanEverAffectNavigation(false);
						P->SetHiddenInGame(true);
						P->bCastHiddenShadow = true;
						P->SetCastShadow(true);
						P->bCastFarShadow = true;   // (the sun's far cascade, jeeb.Shadow.Far, draws only these)
						P->bAffectDistanceFieldLighting = false;
						P->bCastDynamicShadow = true;
						Sh = Self->ShadeSets.Add(P);
						Self->ShadeKind.Add(Kd);
						Self->ShadeBounds.Add(Meshes[Kd]->GetBoundingBox());
					}
					Self->ShadeOfTwin.Add(Sh);
					for (int32 n = 0; n < V.Value.Num(); ++n)
					{
						const FVector L = V.Value[n].GetLocation();
						Self->ShadowCells.FindOrAdd(FIntPoint(FMath::FloorToInt(L.X / 2000.f), FMath::FloorToInt(L.Y / 2000.f))).Add(FIntPoint(Si, n));
					}
				}
			}
			Self->BuildShadeMeshes();
			for (UInstancedStaticMeshComponent* P : Self->ShadeSets) P->RegisterComponent();
			Self->ApplyReach();
			if (Self->ShadowSets.Num()) Self->SetActorTickEnabled(true);
		}
		// the grass thins under every crown; the town's broadleaves also drop leaves in their own colour (AutumnDecor), the forest's none
		// (hundreds of thousands of leaves), nor do conifers
		for (const FPut& P : Puts)
		{
			if (!IsTree(P.Kind)) continue;
			const float Crown = Kinds[P.Kind].Crown * P.Scale.X;
			JeebCanopy::Add(FVector2D(P.At), FMath::Clamp(Crown * (IsSpruce(P.Kind) ? 0.7f : 0.9f), 160.f, 1100.f), (P.bTown && Kinds[P.Kind].bTint) ? TintLeaf[P.Col] : -1);
		}
		UE_LOG(LogTemp, Log, TEXT("JEEBFOREST made in %.0f ms: %d drawn sets (%d instances, %d coloured leaf slots), solids %d trunks, %d logs, %d stumps and rocks, %d boulders; hash=%08x%s"),
			(FPlatformTime::Seconds() - T0) * 1000.0, Sets, Instances, Swapped, TrunkX.Num(), LogX.Num(), CylX.Num(), OwnSolid[K_Boulder].Num(), Hash, bServer ? TEXT(" (server: solids only)") : TEXT(""));
	}

	// The DZ leaf materials' one-colour twins (/Game/TreeGroves, Tools/grove_materials.py), one dynamic copy per leaf kind per colour, shared
	int32 TintLeaves(UMeshComponent* C, int32 F)
	{
		static const struct { const TCHAR* From; const TCHAR* To; } Swaps[] = {
			{ TEXT("MI_Aspen_Leaves_Big_1"), TEXT("MI_Fall_AspenLeavesBig") },
			{ TEXT("MI_Aspen_Leaves_Small_7"), TEXT("MI_Fall_AspenLeavesSmall") },
			{ TEXT("MI_Cork_Oak_Leaves_1"), TEXT("MI_Fall_CorkOakLeaves") },
		};
		int32 Done = 0;
		for (int32 i = 0; i < C->GetNumMaterials(); ++i)
		{
			UMaterialInterface* M = C->GetMaterial(i);
			if (!M) continue;
			for (const auto& S : Swaps)
			{
				if (M->GetName() != S.From) continue;
				UMaterialInterface* Fall = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/TreeGroves/%s.%s"), S.To, S.To), nullptr, LOAD_Quiet | LOAD_NoWarn);
				if (!Fall) break;
				UMaterialInstanceDynamic* D = nullptr;
				for (const TWeakObjectPtr<UMaterialInstanceDynamic>& Old : GTintMids[F]) if (Old.IsValid() && Old->Parent == Fall) { D = Old.Get(); break; }
				if (!D)
				{
					D = UMaterialInstanceDynamic::Create(Fall, Self);
					D->SetVectorParameterValue(TEXT("Fall Color Tint"), Tint(F));
					GTintMids[F].Add(D);
				}
				C->SetMaterial(i, D);
				++Done;
				break;
			}
		}
		return Done;
	}
};

void AOakForest::BeginPlay()
{
	Super::BeginPlay();
	if (!GetActorLocation().IsNearlyZero()) SetActorLocation(FVector::ZeroVector);
	GForest = this;
	static bool bHooked = false;
	if (!bHooked)
	{
		bHooked = true;
		for (int32 F = 0; F < 4; ++F) if (IConsoleVariable* V = TintVar(F)) V->OnChangedDelegate().AddStatic(&ApplyTints);
		if (IConsoleVariable* V = SpruceVar()) V->OnChangedDelegate().AddStatic(&ApplySpruce);
		JeebTreeLook::OnChanged().AddLambda([]() { ApplyTints(); ApplySpruce(); });
		CVarReach->SetOnChangedCallback(FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*) { if (GForest.IsValid()) GForest->ApplyReach(); }));
		CVarShadows->SetOnChangedCallback(FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*) { if (GForest.IsValid()) GForest->ApplyReach(); }));
		CVarShadowReach->SetOnChangedCallback(FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*) { if (GForest.IsValid()) GForest->UpdateShadows(true); }));
		for (IConsoleVariable* V : { CVarFarShade.AsVariable(), CVarFarShadeReach.AsVariable(), CVarFarShadeSize.AsVariable() })
			V->OnChangedDelegate().AddLambda([](IConsoleVariable*) { if (GForest.IsValid()) GForest->UpdateShadows(true); });
		for (IConsoleVariable* V : { CVarShadowAll.AsVariable(), CVarSway.AsVariable(), CVarSpruceLod.AsVariable(), CVarTwinLod.AsVariable() })
			V->OnChangedDelegate().AddLambda([](IConsoleVariable*) { if (GForest.IsValid()) GForest->ApplyReach(); });
	}
	// -NoOakTrees (test pictures only): no town trees and no forest, to measure what they cost (the roads stay)
	if (FParse::Param(FCommandLine::Get(), TEXT("JeebShot")) && FParse::Param(FCommandLine::Get(), TEXT("NoOakTrees"))) { UE_LOG(LogTemp, Log, TEXT("JEEBFOREST left out (-NoOakTrees)")); return; }
	const double T0 = FPlatformTime::Seconds();
	FOakForestBuild B{ this, GetWorld(), IsNetMode(NM_DedicatedServer) };
	// the roads first (PathDefs works them out once), logged
	for (int32 r = 0; r < JeebOakForest::RoadLines().Num(); ++r)
	{
		const TArray<FVector2D>& L = JeebOakForest::RoadLines()[r];
		float Len = 0.f, Worst = 0.f; FVector2D WorstAt = L[0];
		for (int32 i = 1; i < L.Num(); ++i) Len += FVector2D::Distance(L[i], L[i - 1]);
		for (int32 i = 25; i < L.Num(); i += 25)
		{
			const float D = FVector2D::Distance(L[i], L[i - 25]);
			const float G = FMath::RadiansToDegrees(FMath::Atan(FMath::Abs(JeebTerrain::HeightAt(L[i].X, L[i].Y) - JeebTerrain::HeightAt(L[i - 25].X, L[i - 25].Y)) / FMath::Max(D, 1.f)));
			if (G > Worst) { Worst = G; WorstAt = L[i]; }
		}
		const FVector2D Mid = L[L.Num() / 2], Dir = (L[FMath::Min(L.Num() / 2 + 20, L.Num() - 1)] - Mid).GetSafeNormal();
		UE_LOG(LogTemp, Log, TEXT("JEEBFOREST road %d: from %.0f %.0f to %.0f %.0f, %.0f m, steepest %.1f degrees at %.0f %.0f; middle %.0f %.0f heading %.0f"), r, L[0].X, L[0].Y, L.Last().X, L.Last().Y,
			Len / 100.f, Worst, WorstAt.X, WorstAt.Y, Mid.X, Mid.Y, FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)));
	}
	B.Town(0.f);
	if (JeebTownLab::NewOn()) B.Town(JeebTownLab::OffY);   // (the TownLab map: its rebuilt town 300 m east gets the same trees)
	B.Forest();
	B.Make();
	UE_LOG(LogTemp, Log, TEXT("JEEBFOREST built in %.0f ms"), (FPlatformTime::Seconds() - T0) * 1000.0);
}

void AOakForest::ApplyReach()
{
	const float R = FMath::Max(0.f, CVarReach.GetValueOnGameThread()) * 100.f;
	for (int32 i = 0; i < Drawn.Num(); ++i)
	{
		if (!Drawn[i]) continue;
		const float Own = DrawnReach[i];
		const float End = DrawnIsTree[i] ? R : (R > 0.f && Own > 0.f ? FMath::Min(R, Own) : Own);
		Drawn[i]->SetCullDistances(0, int32(End));
		// (jeeb.Forest.ShadowAll: the drawn trees cast their own shadows, for virtual shadow maps, and the twins and stand-ins stay empty)
		const bool bShadow = (DrawnIsTree[i] ? CVarShadowAll.GetValueOnGameThread() != 0 : DrawnShadow[i]) && CVarShadows.GetValueOnGameThread() != 0;
		if (Drawn[i]->CastShadow != bShadow) Drawn[i]->SetCastShadow(bShadow);
		if (DrawnIsTree[i]) Drawn[i]->SetWorldPositionOffsetDisableDistance(int32(FMath::Max(0.f, CVarSway.GetValueOnGameThread()) * 100.f));
	}
	for (UInstancedStaticMeshComponent* S : ShadowSets) if (S) S->SetWorldPositionOffsetDisableDistance(int32(FMath::Max(0.f, CVarSway.GetValueOnGameThread()) * 100.f));
	// (2026-11-16, for speed) the spruces drop to their simpler models sooner (jeeb.Forest.SpruceLod: their switch-over sizes on screen
	// times this; a run-time change to the loaded meshes only, never saved), and their shadow twins can cast from a simpler model
	// (jeeb.Forest.TwinLod)
	{
		static TMap<TWeakObjectPtr<UStaticMesh>, TArray<float>> Orig;
		const float LodK = FMath::Clamp(CVarSpruceLod.GetValueOnGameThread(), 0.25f, 8.f);
		TSet<UStaticMesh*> Done;
		for (UInstancedStaticMeshComponent* C : Drawn)
		{
			UStaticMesh* M = C ? C->GetStaticMesh() : nullptr;
			if (!M || M->IsNaniteEnabled() || !M->GetName().StartsWith(TEXT("spruce_")) || !M->GetRenderData()) continue;
			if (!Done.Contains(M))
			{
				Done.Add(M);
				FStaticMeshRenderData* RD = M->GetRenderData();
				TArray<float>& O = Orig.FindOrAdd(M);
				if (O.Num() == 0)
				{
					FString S;
					for (int32 l = 0; l < RD->LODResources.Num() && l < MAX_STATIC_MESH_LODS; ++l) { O.Add(RD->ScreenSize[l].Default); S += FString::Printf(TEXT(" %.3f/%d"), O.Last(), RD->LODResources[l].GetNumTriangles()); }
					UE_LOG(LogTemp, Log, TEXT("JEEBFOREST %s LODs (screen size/triangles):%s"), *M->GetName(), *S);
				}
				for (int32 l = 1; l < O.Num(); ++l) RD->ScreenSize[l].Default = FMath::Min(O[l] * LodK, l > 0 ? RD->ScreenSize[l - 1].Default * 0.99f : 1.f);
			}
			C->MarkRenderStateDirty();
		}
		const int32 TwinLod = CVarTwinLod.GetValueOnGameThread();
		for (UInstancedStaticMeshComponent* S : ShadowSets)
		{
			UStaticMesh* M = S ? S->GetStaticMesh() : nullptr;
			if (!M || M->IsNaniteEnabled()) continue;
			S->SetForcedLodModel(TwinLod > 0 ? FMath::Min(TwinLod, M->GetNumLODs() - 1) + 1 : 0);
		}
	}
	if (ShadowSets.Num() && HasActorBegunPlay()) UpdateShadows(true);
}

void AOakForest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateShadows(false);
}

// The trees' stand-in shadow casters (jeeb.Forest.FarShade), one mesh per kind, made again when jeeb.Forest.FarShadeSize changes
void AOakForest::BuildShadeMeshes()
{
	ShadeSizeNow = CVarFarShadeSize.GetValueOnGameThread();
	const float Size = FMath::Clamp(ShadeSizeNow, 0.2f, 1.5f);
	for (int32 i = 0; i < ShadeSets.Num(); ++i)
	{
		if (!ShadeSets[i]) continue;
		const FKindDef& K = Kinds[ShadeKind[i]];
		UStaticMesh* M = MakeShadeMesh(this, ShadeBounds[i], K.Crown, K.TrunkR, K.TrunkH, IsSpruce(ShadeKind[i]), Size);
		ShadeSets[i]->SetStaticMesh(M);
		UE_LOG(LogTemp, Log, TEXT("JEEBFOREST stand-in shadow %d (kind %d): crown %.0f cm, trunk %.0f to %.0f cm, mesh %.0f x %.0f x %.0f cm, size %.2f, %d triangles"), i, ShadeKind[i], K.Crown, K.TrunkR, K.TrunkH,
			ShadeBounds[i].GetSize().X, ShadeBounds[i].GetSize().Y, ShadeBounds[i].GetSize().Z, Size, M && M->GetRenderData() && M->GetRenderData()->LODResources.Num() ? M->GetRenderData()->LODResources[0].GetNumTriangles() : -1);
	}
}

// Refills the shadow twins with the trees within jeeb.Forest.ShadowReach of this player's camera (a local look only: each machine its own),
// whenever the camera has moved 8 m; a twin is only touched when what it holds changes
void AOakForest::UpdateShadows(bool bForce)
{
	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;   // (local visuals only)
	if (!PC || !PC->PlayerCameraManager) return;
	const FVector2D Cam(PC->PlayerCameraManager->GetCameraLocation());
	if (!bForce && FVector2D::DistSquared(Cam, ShadowAt) < 800.f * 800.f) return;
	ShadowAt = Cam;
	const bool bOn = CVarShadows.GetValueOnGameThread() != 0 && CVarShadowAll.GetValueOnGameThread() == 0;   // (ShadowAll: the drawn trees cast)
	const float R = FMath::Max(0.f, CVarShadowReach.GetValueOnGameThread()) * 100.f;
	// the stand-ins: from the twins' reach out to jeeb.Forest.FarShadeReach
	const float RF = (bOn && CVarFarShade.GetValueOnGameThread() != 0) ? FMath::Max(0.f, CVarFarShadeReach.GetValueOnGameThread()) * 100.f : 0.f;
	if (ShadeSets.Num() && !FMath::IsNearlyEqual(ShadeSizeNow, CVarFarShadeSize.GetValueOnGameThread())) BuildShadeMeshes();
	TArray<TArray<int32>> Near;
	Near.SetNum(ShadowSets.Num());
	TArray<TArray<FTransform>> Far;
	Far.SetNum(ShadeSets.Num());
	uint32 FarH = 0u;
	const float Scan = FMath::Max(R, RF);
	if (bOn && Scan > 0.f)
	{
		const int32 C0x = FMath::FloorToInt((Cam.X - Scan) / 2000.f), C1x = FMath::FloorToInt((Cam.X + Scan) / 2000.f);
		const int32 C0y = FMath::FloorToInt((Cam.Y - Scan) / 2000.f), C1y = FMath::FloorToInt((Cam.Y + Scan) / 2000.f);
		for (int32 cx = C0x; cx <= C1x; ++cx)
			for (int32 cy = C0y; cy <= C1y; ++cy)
				if (const TArray<FIntPoint>* L = ShadowCells.Find(FIntPoint(cx, cy)))
					for (const FIntPoint& E : *L)
					{
						const float D2 = FVector2D::DistSquared(FVector2D(ShadowAll[E.X][E.Y].GetLocation()), Cam);
						if (D2 < R * R) Near[E.X].Add(E.Y);
						else if (D2 < RF * RF && ShadeOfTwin.IsValidIndex(E.X))
						{
							Far[ShadeOfTwin[E.X]].Add(ShadowAll[E.X][E.Y]);
							FarH += (uint32(E.X) * 2654435761u) ^ (uint32(E.Y) * 2246822519u + 0x9e3779b9u);   // (order does not matter)
						}
					}
	}
	FarH ^= uint32(Far.Num()) + (uint32(RF) << 8);
	for (const TArray<FTransform>& F : Far) FarH = FarH * 31u + uint32(F.Num());
	if (FarH != ShadeHash || bForce)
	{
		ShadeHash = FarH;
		for (int32 s = 0; s < ShadeSets.Num(); ++s)
		{
			if (!ShadeSets[s]) continue;
			ShadeSets[s]->ClearInstances();
			if (Far[s].Num()) ShadeSets[s]->AddInstances(Far[s], false, true, false);
		}
	}
	for (int32 s = 0; s < ShadowSets.Num(); ++s)
	{
		UInstancedStaticMeshComponent* S = ShadowSets[s];
		if (!S) continue;
		Near[s].Sort();
		uint32 H = 2166136261u ^ uint32(Near[s].Num());
		for (int32 n : Near[s]) H = (H ^ uint32(n)) * 16777619u;
		if (H == ShadowHash[s]) continue;
		ShadowHash[s] = H;
		S->ClearInstances();
		if (Near[s].Num())
		{
			TArray<FTransform> X;
			X.Reserve(Near[s].Num());
			for (int32 n : Near[s]) X.Add(ShadowAll[s][n]);
			S->AddInstances(X, false, true, false);
		}
	}
}

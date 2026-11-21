#include "OakForest.h"
#include "TreeLook.h"
#include "Terrain.h"
#include "WorldMap.h"
#include "OakHill.h"
#include "TownLab.h"
#include "SteeleCreekTown.h"
#include "Canopy.h"
#include "KnightGameState.h"
#include "GameFramework/Pawn.h"
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
		TEXT("Shadows from the forest and the town's trees: 1 on (every tree's stand-in shade, logs, stumps and rocks; never bushes), 0 none. Live; looks only."), ECVF_Default);
	// Every tree's shadow is cast by a plain hidden stand-in (a see-through crown and a trunk), from the camera out (2026-11-16: thousands
	// of leafy trees in every shadow cascade cost 7 ms; owner, "far away ... looks too bright then when I get closer it turns dark"). Real
	// shadows, so they follow the sun at every time of day, at a small share of a real tree's cost.
	// (2026-11-21, owner: "I want the expensive shadows taken out of the game. I like how they look, but the transition from cheap to
	// quality shadows doesn't look good") Until then the trees within 20 m cast their own leafy shadows from hidden twins, handed over to
	// the stand-ins across a fade (jeeb.Forest.NearShadows, ShadowReach, ShadowFade, ShadowFadeSize, TwinShrink, TwinLod: all gone). In their
	// place the stand-in's holes are leaf-clump sized (jeeb.Forest.Dapple), the same at every distance, so there is nothing to hand over
	TAutoConsoleVariable<int32> CVarFarShade(TEXT("jeeb.Forest.FarShade"), 1,
		TEXT("1: each tree's shadow is cast by a plain hidden stand-in (crown and trunk) out to jeeb.Forest.FarShadeReach; 0: the trees cast no shadows. Live; looks only."), ECVF_Default);
	TAutoConsoleVariable<float> CVarFarShadeReach(TEXT("jeeb.Forest.FarShadeReach"), 200.f,
		TEXT("How far from the camera the trees' stand-ins cast shadows, metres (set by the graphics setting). Past the sun's ordinary shadow range they need its far cascade, jeeb.Shadow.Far. Live; looks only."), ECVF_Default);
	TAutoConsoleVariable<float> CVarFarShadeSize(TEXT("jeeb.Forest.FarShadeSize"), 1.f,
		TEXT("The stand-in crown's size against the real crown (1 as wide and tall; smaller is a smaller shade; 0.75 and solid until 2026-11-18). Live; looks only."), ECVF_Default);
	TAutoConsoleVariable<int32> CVarShadowLog(TEXT("jeeb.Forest.ShadowLog"), 0,
		TEXT("1: log every refill of the trees' shadow stand-ins (JEEBFOREST refill: frame, camera), for tests; 3: also list lone trees near the camera on a forced refill."), ECVF_Default);
	// (2026-11-18) Measured on one tree: a solid stand-in's ground shade was far darker and wider than a leafy crown's, so it is see-through
	TAutoConsoleVariable<float> CVarShadeDensity(TEXT("jeeb.Forest.ShadeDensity"), 0.6f,
		TEXT("How dense the trees' stand-in shade is (1 solid; lower is lighter, with more and wider sun flecks; 0.7 as an even grain measured within about 10% of a real leafy crown's ground shade at noon, 2026-11-18; 0.6 since 2026-11-21, with the flecks). The trunk stays solid. Live; looks only."), ECVF_Default);
	// (2026-11-21, owner: "If there's any cheap way to imitate the good shadow look please add it") The stand-in's holes are soft blobs this
	// wide instead of a fine 20 cm grain, so the sun comes through in flecks and the shade's edge is ragged like a leafy crown's, most of
	// all round the rim. Fixed in the world and the same at every distance: only the shadow map's own sharpness changes with distance
	TAutoConsoleVariable<float> CVarDapple(TEXT("jeeb.Forest.Dapple"), 70.f,
		TEXT("How wide the sun flecks in the trees' shade are, cm (the stand-in's holes are soft blobs about this size, with smaller ones mixed in; 0: the fine even grain used until 2026-11-21, a flat grey shade). Live; looks only."), ECVF_Default);

	TAutoConsoleVariable<int32> CVarLazyNodes(TEXT("jeeb.Forest.LazyNodes"), 1,
		TEXT("1: a choppable tree's node actor exists only while a player is within jeeb.Forest.NodeReach of it (or it is felled, on the server); 0: all of them are spawned when the world is built (the old way, 0.4-1.2 s slower). Read when the world is built."), ECVF_Default);
	TAutoConsoleVariable<float> CVarNodeReach(TEXT("jeeb.Forest.NodeReach"), 35.f,
		TEXT("How near a player must be (metres) for a choppable tree to get its node actor; it goes again past this plus 10 m. Live."), ECVF_Default);

	TAutoConsoleVariable<int32> CVarShadowAll(TEXT("jeeb.Forest.ShadowAll"), 0,
		TEXT("1: every drawn tree casts its own shadow (meant for virtual shadow maps, r.Shadow.Virtual.Enable 1; with ordinary shadow maps it cost about 7 ms); 0: only the stand-ins. Live; looks only."), ECVF_Default);
	// (2026-11-21, owner: "set both of these as default settings and remove the sliders") 0.5, the left end of the LOOK slider it had
	TAutoConsoleVariable<float> CVarSpruceLod(TEXT("jeeb.Forest.SpruceLod"), 2.f,
		TEXT("How soon the spruces drop to their simpler models: their switch-over sizes on screen times this. 2 (default again since 2026-11-21 evening, once the far cards were softened and matched to the near models); 0.5 (the owner's earlier pick): the fuller models out to about twice the pack's distances; 1 as the pack made them; 2 at about half the distance (2026-11-16 to -19, for speed; the simpler ones read sharp and spiky). Live; looks only."), ECVF_Default);
	TAutoConsoleVariable<float> CVarSway(TEXT("jeeb.Forest.Sway"), 60.f,
		TEXT("How far from the camera the trees sway in the wind, metres (past it they stand still, which also keeps cached shadows valid). Live; looks only."), ECVF_Default);
	TAutoConsoleVariable<int32> CVarSpruceFace(TEXT("jeeb.Forest.SpruceFace"), 1,
		TEXT("1: the drawn spruces keep their world position offset at every distance, so their far card keeps turning to face you; 0: it stops past jeeb.Forest.Sway (before 2026-11-20: a thin edge-on spike). Live; looks only."), ECVF_Default);

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
		FVector2f UV0 = FVector2f::ZeroVector;   // (the trunk's x is 1: M_TreeShade keeps it solid)
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
				Nrm[I[i]] = N; Tan[I[i]] = FVector3f(1.f, 0.f, 0.f); Sign[I[i]] = 1.f; UVs[I[i]] = UV0;
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
		// the trunk, up into the crown (0.7 of the real one's radius since 2026-11-18: as wide, it shaded the real trunk's own sunny side)
		UV0 = FVector2f(1.f, 0.f);
		const float Tr = 0.7f * FMath::Max(TrunkR, 8.f), Th = Zb + 0.3f * (Top - Zb);
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
	const TCHAR* const TintDefaults[4] = { TEXT("0.2017 0.2723 0.07"), TEXT("0.3418 0.0113 0.0347"), TEXT("0.6982 0.2459 0.0393"), TEXT("0.7 0.45 0.0517") };
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
		if (!V) V = IConsoleManager::Get().RegisterConsoleVariable(TEXT("jeeb.Trees.Spruce"), TEXT("0.5126 0.8403 0"), TEXT("Needle colour multiplier of every spruce, \"r g b\" (1 1 1 = as the pack has it; live)"), ECVF_Default);
		return V;
	}
	FLinearColor SpruceTint()
	{
		IConsoleVariable* V = SpruceVar();
		return JeebTreeLook::Saturate(JeebTreeLook::ParseRGB(V ? V->GetString() : FString(TEXT("0.5126 0.8403 0")), FLinearColor(1.f, 1.f, 1.f, 0.f)));   // (the pack's tints have alpha 0)
	}
	const TCHAR* SpruceParam(const FString& MaterialName)
	{
		if (MaterialName.Contains(TEXT("imposter"))) return TEXT("Tint");
		if (MaterialName.Contains(TEXT("_leaf"))) return TEXT("Leaf Tint");
		if (MaterialName.Contains(TEXT("_branch"))) return TEXT("Branch Tint");
		return nullptr;   // (trunk: bark and moss keep the pack's colour)
	}
	// (2026-11-21, owner: the far card "showed up in HD with too much detail and bright orange pinecones", then in the evening "too yellow-green", the near one darker; make it match the near spruce
	// so the faster jeeb.Forest.SpruceLod can be used) the far cards' own Brightness, Saturation and SS Strength (MA_Imposter), times these
	TAutoConsoleVariable<FString> CVarSpruceFar(TEXT("jeeb.Forest.SpruceFar"), TEXT("1.05 0.85 0.8"),
		TEXT("The spruces' far cards against the near models: \"brightness saturation glow\", each times the pack's own (1 1 1 = as the pack made them). Live; looks only."), ECVF_Default);
	void ApplySpruceFar(UMaterialInstanceDynamic* M)
	{
		if (!M || !M->Parent || !M->Parent->GetName().Contains(TEXT("imposter"))) return;
		const FLinearColor K = JeebTreeLook::ParseRGB(CVarSpruceFar.GetValueOnGameThread(), FLinearColor(1.f, 1.f, 1.f));
		const TCHAR* Names[3] = { TEXT("Brightness"), TEXT("Saturation"), TEXT("SS Strength") };
		const float Ks[3] = { K.R, K.G, K.B };
		for (int32 i = 0; i < 3; ++i) { float V0 = 0.f; if (M->Parent->GetScalarParameterValue(FName(Names[i]), V0)) M->SetScalarParameterValue(Names[i], V0 * Ks[i]); }
	}
	void ApplySpruce(IConsoleVariable* = nullptr)
	{
		const FLinearColor Sp = SpruceTint();
		for (const TWeakObjectPtr<UMaterialInstanceDynamic>& M : GSpruceMids)
			if (M.IsValid() && M->Parent) if (const TCHAR* Param = SpruceParam(M->Parent->GetName())) { M->SetVectorParameterValue(Param, Sp); ApplySpruceFar(M.Get()); }
	}
	// every needle slot of a spruce set gets the shared dynamic instance of its material, in the spruce tint
	int32 TintSpruceSet(UMeshComponent* C, UObject* Outer)
	{
		int32 Done = 0;
		const FLinearColor Sp = SpruceTint();
		for (int32 i = 0; i < C->GetNumMaterials(); ++i)
		{
			UMaterialInterface* M = JeebTreeLook::ConeLeaf(C->GetMaterial(i));   // (a needle material: its copy with the dark cones)
			const TCHAR* Param = M ? SpruceParam(M->GetName()) : nullptr;
			if (!Param) { if (M && M != C->GetMaterial(i)) C->SetMaterial(i, M); continue; }   // (the trunk: its copy on the dithered master, no tint)
			UMaterialInstanceDynamic* D = nullptr;
			for (const TWeakObjectPtr<UMaterialInstanceDynamic>& Old : GSpruceMids) if (Old.IsValid() && Old->Parent == M) { D = Old.Get(); break; }
			if (!D) { D = UMaterialInstanceDynamic::Create(M, Outer); D->SetVectorParameterValue(Param, Sp); JeebTreeLook::DarkCones(D); ApplySpruceFar(D); GSpruceMids.Add(D); }
			C->SetMaterial(i, D);
			++Done;
		}
		return Done;
	}
	// ------------------------------------------------------------------ the kinds
	enum EKind : uint8
	{
		K_Oak, K_Aspen, K_Full1, K_Full2, K_Full3, K_Half2, K_Sapling,   // (the bushes went, 2026-11-18: ABushes, Bushes.cpp)
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
	struct FPut { uint8 Kind; uint8 Col; FVector At; float Yaw; FVector Scale; bool bTown; FVector Up = FVector::UpVector; bool bNoChop = false; };   // (Up: the ground's tilt it lies along: logs and stones, 2026-11-17)
	// its turn: the yaw, then tipped from straight up to Up
	FQuat PutRot(const FPut& P) { return FQuat::FindBetweenNormals(FVector::UpVector, P.Up) * FQuat(FVector::UpVector, FMath::DegreesToRadians(P.Yaw)); }

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
		// the ground's up direction over about Span cm round a point, eased toward straight up by Share (0 upright, 1 the ground's full tilt)
		FVector UpAt(float X, float Y, float Span, float Share) const
		{
			const float R = FMath::Max(Span * 0.5f, 50.f);
			const FVector N = FVector(At(X - R, Y) - At(X + R, Y), At(X, Y - R) - At(X, Y + R), 2.f * R).GetSafeNormal();
			return FMath::Lerp(FVector::UpVector, N, Share).GetSafeNormal();
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
			{ 769.3f, 699.3f, TEXT("Birch Grove") },  // the southeast road's end past the allotments (2026-11-18 layout): to the river's east branch
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
				if (InWarFootprint(E, N, 2.f)) continue;   // (a spot marked on a fort's old empty plot would stand inside its walls)
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
			if (const float Isle = JeebWorld::IslandWoods(X, Y); Isle > 0.f) return 1.6f * Isle;   // (the lake's island and the split's: thickly wooded, glades or not, 2026-11-18)
			const float D = FVector2D::Distance(FVector2D(X, Y), Plaza);
			if (D > JeebOakForest::Reach) return 0.f;
			const float Sq =FMath::Max(FMath::Abs(X), FMath::Abs(Y)) - JeebTerrain::GroundHalf;
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
			else { U.FRand(); continue; }   // (the bushes that grew here went, 2026-11-18: ABushes, Bushes.cpp, places the new ones; the draw stays so the logs and stumps keep their places)
			const float Foot = Kinds[Kind].Crown * Scale;
			const bool bSolid = Kinds[Kind].Solid != ESolid::None;
			// solids keep 60 cm clear of every trunk and solid; bushes may tuck in under the crowns but not into a trunk
			if (!Space.Clear(P, bSolid ? Foot : 40.f, 0.12f, bSolid ? 60.f : 10.f)) continue;
			if (bSolid) Space.Add(P, Foot, false);
			// (2026-11-17, owner: logs and stones "have the ends getting out of the hills because they are placed perfectly up and down"; tilt them,
			// but trees and stumps stand straight, the stumps sunk "a little further to hide the edges")
			const bool bLog = Kind == K_Log, bStump = Kind >= K_Stump && Kind <= K_StumpGrass;
			const float Sink = bLog ? 8.f + Slope * 0.15f * FMath::Min(Foot, 100.f) : bStump ? 10.f + Slope * FMath::Min(Foot, 100.f) : 3.f + Slope * 0.5f * FMath::Min(Foot, 100.f);
			Puts.Add({ uint8(Kind), 0, FVector(P.X, P.Y, Z - Sink), Yaw, FVector(Scale), false, bLog ? Hs.UpAt(P.X, P.Y, 2.f * Foot, 1.f) : FVector::UpVector });
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
				// (tipped with the ground, 2026-11-17: an upright stone on a hillside showed its flat underside downhill; so it sinks less by the slope)
				Puts.Add({ uint8(Kind), 0, FVector(P.X, P.Y, Z - (bBig ? 0.18f * Sz * 100.f : 0.15f * Sz * 100.f) - Slope * 0.15f * Foot), Yaw, FVector(Sx, Sy, Sz), false, Hs.UpAt(P.X, P.Y, 2.f * Foot, 0.9f) });
				++Count[Kind]; ++Rocks;
			}
		}

		FString List;
		for (int32 k = 0; k < K_Num; ++k) if (Count[k]) List += FString::Printf(TEXT(" %s x%d"), *FPaths::GetBaseFilename(Kinds[k].Path), Count[k]);
		const int32 Fat = Count[K_Oak], Thin = Count[K_Aspen];
		UE_LOG(LogTemp, Log, TEXT("JEEBFOREST forest: %d trees (%d cork oaks, %d aspens, %d spruces; fat:skinny %.2f:1; DZ colours green %d red %d orange %d yellow %d), %d undergrowth (%d of them spruce seedlings, counted with their kind below), %d stones, density %.2f, planned in %.0f ms:%s"),
			ForestTrees, Fat, Thin, ForestTrees - Fat - Thin, Thin ? float(Fat) / Thin : 0.f, ColCount[0], ColCount[1], ColCount[2], ColCount[3], Under, Seedlings, Rocks, Density, (FPlatformTime::Seconds() - T0) * 1000.0, *List);
	}

	// ---------------------------------------------------------------- Steele Creek's forest (2026-11-18 round 3)
	// The trees SteeleCreekTown.cpp plans in and round Steele Creek (JeebSteeleWoods::Plan, deterministic), put on the drawn ground like the
	// forest's own, in the same four colour patches, with solid trunks and the same shadows; not choppable (bNoChop). The checksum below and
	// JEEBSTEELE's both cover them (the solids' positions are in this actor's hash, the plan's in the town's).
	void Steele()
	{
		const TArray<JeebSteeleWoods::FSpot>& Spots = JeebSteeleWoods::Plan();
		if (!Spots.Num()) return;
		FVector2D Lo(1.0e9f, 1.0e9f), Hi(-1.0e9f, -1.0e9f);
		for (const JeebSteeleWoods::FSpot& S : Spots) { Lo.X = FMath::Min(Lo.X, S.X); Lo.Y = FMath::Min(Lo.Y, S.Y); Hi.X = FMath::Max(Hi.X, S.X); Hi.Y = FMath::Max(Hi.Y, S.Y); }
		FHeights Hs;
		Hs.Build(Lo - FVector2D(1600.f), Hi + FVector2D(1600.f));
		FRandomStream R(51529);
		static const uint8 Kind[6] = { K_Oak, K_Aspen, K_Full1, K_Full2, K_Full3, K_Half2 };
		int32 Placed = 0, Steep = 0;
		for (const JeebSteeleWoods::FSpot& S : Spots)
		{
			const FVector2D P(S.X, S.Y);
			const int32 Col = ColourAt(P, R);   // (drawn for every spot, so the stream stays in step)
			float Slope;
			const float Z = Hs.At(S.X, S.Y, &Slope);
			if (Slope > 0.72f) { ++Steep; continue; }
			const uint8 K = Kind[FMath::Min<int32>(S.Kind, 5)];
			Puts.Add({ K, uint8(Kinds[K].bTint ? Col : 0), FVector(S.X, S.Y, Z - 6.f - Slope * 45.f * S.Scale), S.Yaw, FVector(S.Scale), false, FVector::UpVector, true });
			++Placed;
		}
		UE_LOG(LogTemp, Log, TEXT("JEEBFOREST Steele Creek: %d trees (%d planned, %d left out on steep ground), not choppable"), Placed, Spots.Num(), Steep);
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
		TArray<uint8> FellKind;                   // (each choppable tree's kind)
		TMap<int32, int32> DrawnOfKey, ShadowOfKey;
		TArray<FTransform> OwnSolid[K_Num];
		for (const FPut& P : Puts)
		{
			const FKindDef& K = Kinds[P.Kind];
			if (!Meshes[P.Kind]) continue;
			const FTransform Xf(PutRot(P), P.At, P.Scale);
			Visual.FindOrAdd(P.Kind * 4 + P.Col).Add(Xf);
			const FBox Bx = Meshes[P.Kind]->GetBoundingBox();
			switch (K.Solid)
			{
			case ESolid::Trunk:
			{
				const float Rad = K.TrunkR * P.Scale.X, H = FMath::Max(K.TrunkH * P.Scale.Z, 300.f) + 40.f;
				if (Rad < 6.f) break;   // (a seedling: walked through)
				TrunkX.Add(FTransform(FRotator::ZeroRotator, P.At + FVector(0.f, 0.f, H * 0.5f - 40.f), FVector(Rad / 50.f, Rad / 50.f, H / 100.f)));
				if (IsTree(P.Kind) && !P.bNoChop)   // (a choppable tree, AOakTreeNode; not Steele Creek's)
				{
					AOakForest::FFellable F;
					F.Key = P.Kind * 4 + P.Col; F.KeyIndex = Visual[F.Key].Num() - 1;
					F.Trunk = TrunkX.Num() - 1; F.Tree = Xf; F.TrunkSolid = TrunkX.Last(); F.Radius = Rad;
					Self->Fellable.Add(F);
					FellKind.Add(P.Kind);
				}
				break;
			}
			case ESolid::Box:
			{
				const FVector Sz = Bx.GetSize() * P.Scale, Ctr = Xf.TransformPosition(Bx.GetCenter());
				LogX.Add(FTransform(PutRot(P), Ctr, FVector(Sz.X * 0.92f, Sz.Y * 0.8f, Sz.Z * 0.85f) / 100.f));
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
			if (K.Solid != ESolid::None) { HashIn(P.At.X); HashIn(P.At.Y); HashIn(P.At.Z); HashIn(P.Scale.X); HashIn(P.Up.X * 100.f); HashIn(P.Up.Y * 100.f); }
		}
		auto Fill = [](UInstancedStaticMeshComponent* C, const TArray<FTransform>& X) { if (!C) return; C->RegisterComponent(); if (X.Num()) C->AddInstances(X, false, true, false); };
		Fill(Trunks, TrunkX); Fill(Logs, LogX); Fill(Cyls, CylX);
		Self->TrunkSolids = Trunks;
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
				DrawnOfKey.Add(V.Key, Self->Drawn.Num());
				Self->Drawn.Add(C); Self->DrawnReach.Add(K.Reach); Self->DrawnIsTree.Add(bTree); Self->DrawnShadow.Add(K.bShadow && !bTree);
				C->RegisterComponent();
				C->AddInstances(V.Value, false, true, false);
				++Sets; Instances += V.Value.Num();
				if (bTree && K.bShadow)   // its shadow: its kind's hidden stand-in set (one per kind, whatever the colour), filled round the camera (AOakForest::UpdateShadows)
				{
					const int32 Si = Self->ShadowAll.Add(V.Value);
					ShadowOfKey.Add(V.Key, Si);
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
			// the stumps of felled trees (filled as trees are chopped)
			if (UStaticMesh* SM = Meshes[K_Stump])
			{
				UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(Self, TEXT("FelledStumps"));
				C->SetStaticMesh(SM);
				C->SetupAttachment(Root);
				C->SetMobility(EComponentMobility::Movable);
				C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				C->SetCanEverAffectNavigation(false);
				C->SetCullDistances(0, int32(Kinds[K_Stump].Reach));
				C->RegisterComponent();
				Self->FelledStumps = C;
				Self->StumpBounds = SM->GetBoundingBox();
			}
			Self->ApplyReach();
			if (Self->ShadeSets.Num()) Self->SetActorTickEnabled(true);
		}
		// every pack tree choppable (AOakTreeNode). Lazily (2026-11-18): no node exists until a player is near its tree (AOakForest::UpdateNodes,
		// every machine its own, as before); jeeb.Forest.LazyNodes 0 or -ForestEagerNodes builds them all here, the old way
		for (int32 i = 0; i < Self->Fellable.Num(); ++i)
		{
			AOakForest::FFellable& F = Self->Fellable[i];
			if (const int32* D = DrawnOfKey.Find(F.Key)) F.Drawn = *D;
			if (const int32* S = ShadowOfKey.Find(F.Key)) F.Shadow = *S;
			F.Kind = FellKind[i];
			const FVector L = F.Tree.GetLocation();
			Self->NodeCells.FindOrAdd(FIntPoint(FMath::FloorToInt(L.X / 2000.f), FMath::FloorToInt(L.Y / 2000.f))).Add(i);
		}
		Self->bLazyNodes = CVarLazyNodes.GetValueOnGameThread() != 0 && !FParse::Param(FCommandLine::Get(), TEXT("ForestEagerNodes"));
		if (!Self->bLazyNodes) for (int32 i = 0; i < Self->Fellable.Num(); ++i) Self->SpawnNode(i);
		UE_LOG(LogTemp, Log, TEXT("JEEBFOREST choppable trees: %d (%s), %d node actors now"), Self->Fellable.Num(), Self->bLazyNodes ? TEXT("nodes spawned lazily near players") : TEXT("all spawned"), Self->Nodes.Num());
		if (Self->bLazyNodes) Self->SetActorTickEnabled(true);
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
#if !UE_BUILD_SHIPPING
		// (test pictures, 2026-11-20: -ShotSpruces logs grown spruces of the forest, to aim cameras at)
		if (FParse::Param(FCommandLine::Get(), TEXT("ShotSpruces")))
		{
			int32 Logged = 0;
			for (const FPut& P : Puts)
				if (IsSpruce(P.Kind) && !P.bTown && P.Scale.X > 0.8f && Logged < 60) { ++Logged; UE_LOG(LogTemp, Log, TEXT("JEEBSPRUCE kind %d at %.0f %.0f %.0f scale %.2f"), P.Kind, P.At.X, P.At.Y, P.At.Z, P.Scale.X); }
		}
#endif
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
		CVarSpruceFar.AsVariable()->OnChangedDelegate().AddStatic(&ApplySpruce);
		JeebTreeLook::OnChanged().AddLambda([]() { ApplyTints(); ApplySpruce(); });
		CVarReach->SetOnChangedCallback(FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*) { if (GForest.IsValid()) GForest->ApplyReach(); }));
		CVarShadows->SetOnChangedCallback(FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*) { if (GForest.IsValid()) GForest->ApplyReach(); }));
		for (IConsoleVariable* V : { CVarFarShade.AsVariable(), CVarFarShadeReach.AsVariable(), CVarFarShadeSize.AsVariable() })
			V->OnChangedDelegate().AddLambda([](IConsoleVariable*) { if (GForest.IsValid()) GForest->UpdateShadows(true); });
		for (IConsoleVariable* V : { CVarShadowAll.AsVariable(), CVarSway.AsVariable(), CVarSpruceFace.AsVariable(), CVarSpruceLod.AsVariable() })
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
	B.Steele();   // (Steele Creek's forest, planned by SteeleCreekTown.cpp)
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
		// (jeeb.Forest.ShadowAll: the drawn trees cast their own shadows, for virtual shadow maps, and the stand-ins stay empty)
		const bool bShadow = (DrawnIsTree[i] ? CVarShadowAll.GetValueOnGameThread() != 0 : DrawnShadow[i]) && CVarShadows.GetValueOnGameThread() != 0;
		if (Drawn[i]->CastShadow != bShadow) Drawn[i]->SetCastShadow(bShadow);
		// (2026-11-20, owner: "what's up with this really thin tree?") a spruce's last, simplest model is a camera-facing card (the pack's
		// MA_Imposter turns it by world position offset); with that switched off past the sway reach the card stood still, edge-on from most
		// sides: a thin spike that came and went as you walked. Spruces keep it at every distance (jeeb.Forest.SpruceFace 0: the old way)
		const bool bSpruce = Drawn[i]->GetStaticMesh() && Drawn[i]->GetStaticMesh()->GetName().StartsWith(TEXT("spruce_"));
		const bool bFace = bSpruce && CVarSpruceFace.GetValueOnGameThread() != 0;
		if (DrawnIsTree[i]) Drawn[i]->SetWorldPositionOffsetDisableDistance(bFace ? 0 : int32(FMath::Max(0.f, CVarSway.GetValueOnGameThread()) * 100.f));
	}
	// (2026-11-16, for speed) the spruces drop to their simpler models sooner (jeeb.Forest.SpruceLod: their switch-over sizes on screen
	// times this; a run-time change to the loaded meshes only, never saved)
	{
		static TMap<TWeakObjectPtr<UStaticMesh>, TArray<float>> Orig;
		const float LodK = FMath::Clamp(CVarSpruceLod.GetValueOnGameThread(), 0.25f, 8.f);
		TSet<UStaticMesh*> Done;
		for (UInstancedStaticMeshComponent* C : Drawn)
		{
			UStaticMesh* M = C ? C->GetStaticMesh() : nullptr;
			if (!M || M->HasValidNaniteData() || !M->GetName().StartsWith(TEXT("spruce_")) || !M->GetRenderData()) continue;
			if (!Done.Contains(M))
			{
				Done.Add(M);
				FStaticMeshRenderData* RD = M->GetRenderData();
				TArray<float>& O = Orig.FindOrAdd(M);
				if (O.Num() == 0)
				{
					FString S;
					for (int32 l = 0; l < RD->LODResources.Num() && l < MAX_STATIC_MESH_LODS; ++l) { O.Add(RD->ScreenSize[l].Default); S += FString::Printf(TEXT(" %.3f/%d"), O.Last(), RD->LODResources[l].GetNumTriangles()); for (const FStaticMeshSection& Sec : RD->LODResources[l].Sections) { const TArray<FStaticMaterial>& SM = M->GetStaticMaterials(); S += FString::Printf(TEXT(",%s"), SM.IsValidIndex(Sec.MaterialIndex) && SM[Sec.MaterialIndex].MaterialInterface ? *SM[Sec.MaterialIndex].MaterialInterface->GetName() : TEXT("?")); } }
					UE_LOG(LogTemp, Log, TEXT("JEEBFOREST %s LODs (screen size/triangles):%s"), *M->GetName(), *S);
					FTransform T0; if (C->GetInstanceCount() > 0 && C->GetInstanceTransform(C->GetInstanceCount() / 2, T0, true)) UE_LOG(LogTemp, Log, TEXT("JEEBFOREST %s one at %s yaw %.0f scale %.2f"), *M->GetName(), *T0.GetLocation().ToString(), T0.Rotator().Yaw, T0.GetScale3D().Z);   // (a known tree for test pictures)
				}
				for (int32 l = 1; l < O.Num(); ++l) RD->ScreenSize[l].Default = FMath::Min(O[l] * LodK, l > 0 ? RD->ScreenSize[l - 1].Default * 0.99f : 1.f);
			}
			C->MarkRenderStateDirty();
		}
	}
	if (ShadeSets.Num() && HasActorBegunPlay()) UpdateShadows(true);
}
void AOakForest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (ShadeSets.Num()) UpdateShadows(false);
	UpdateNodes();
}

AOakForest* AOakForest::Instance(UWorld* World)
{
	return GForest.IsValid() && GForest->GetWorld() == World ? GForest.Get() : nullptr;
}

// ------------------------------------------------------------------ the lazy nodes
AOakTreeNode* AOakForest::SpawnNode(int32 Index)
{
	if (!Fellable.IsValidIndex(Index)) return nullptr;
	if (const TWeakObjectPtr<AOakTreeNode>* Old = Nodes.Find(Index)) if (Old->IsValid()) return Old->Get();
	const FFellable& F = Fellable[Index];
	FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AOakTreeNode* N = GetWorld()->SpawnActor<AOakTreeNode>(F.Tree.GetLocation(), FRotator::ZeroRotator, SP);
	if (!N) return nullptr;
	N->Forest = this; N->Index = Index;
	const float H = Kinds[F.Kind].TrunkH * F.Tree.GetScale3D().Z + 300.f;
	N->Setup(IsSpruce(F.Kind) ? ETreeKind::Pine : ETreeKind::Oak, F.Radius, H);
	if (!HasAuthority() && F.bFelled) N->ApplyCharges(0);   // (a player's machine: a node made beside an already felled tree starts felled, so its stump can't be "chopped")
	Nodes.Add(Index, N);
	++NodesSpawned;
	return N;
}

// Every tick: the trees within jeeb.Forest.NodeReach of any player this machine knows (the server: all of them; a player's machine: its own)
// get their node; the nodes past that plus 10 m go again, unless the tree is felled or being worked (the server's felled nodes hold the
// regrow timer). A chop needs a node on both sides: the player's machine finds it to ask, the server finds it by place to decide; the
// server's reach is the same as the player's (35 m against a swing of 4 m or so), so its node is there when the request comes.
void AOakForest::UpdateNodes()
{
	if (!bLazyNodes) return;
	UWorld* W = GetWorld();
	if (!W) return;
	const float R = FMath::Max(500.f, CVarNodeReach.GetValueOnGameThread() * 100.f), Out = R + 1000.f;
	TArray<FVector2D> Pawns;
	for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
		if (const APlayerController* PC = It->Get()) if (const APawn* P = PC->GetPawn()) Pawns.Add(FVector2D(P->GetActorLocation()));
	for (const FVector2D& C : Pawns)
	{
		const int32 C0x = FMath::FloorToInt((C.X - R) / 2000.f), C1x = FMath::FloorToInt((C.X + R) / 2000.f);
		const int32 C0y = FMath::FloorToInt((C.Y - R) / 2000.f), C1y = FMath::FloorToInt((C.Y + R) / 2000.f);
		for (int32 cx = C0x; cx <= C1x; ++cx)
			for (int32 cy = C0y; cy <= C1y; ++cy)
				if (const TArray<int32>* L = NodeCells.Find(FIntPoint(cx, cy)))
					for (int32 i : *L)
						if (FVector2D::DistSquared(FVector2D(Fellable[i].Tree.GetLocation()), C) < R * R) SpawnNode(i);
	}
	for (TMap<int32, TWeakObjectPtr<AOakTreeNode>>::TIterator It(Nodes); It; ++It)
	{
		AOakTreeNode* N = It.Value().Get();
		if (!N) { It.RemoveCurrent(); continue; }
		if (!N->CanRemove()) continue;
		const FVector2D At(Fellable[It.Key()].Tree.GetLocation());
		bool bNear = false;
		for (const FVector2D& C : Pawns) if (FVector2D::DistSquared(At, C) < Out * Out) { bNear = true; break; }
		if (bNear) continue;
		N->Destroy();
		It.RemoveCurrent();
		++NodesRemoved;
	}
	const double Now = FPlatformTime::Seconds();
	if (Nodes.Num() != NodesLogged && Now - NodesLogAt > 2.0)
	{
		NodesLogged = Nodes.Num(); NodesLogAt = Now;
		UE_LOG(LogTemp, Log, TEXT("JEEBFOREST nodes: %d live of %d trees (%d spawned, %d removed so far), %d felled, %d players"), Nodes.Num(), Fellable.Num(), NodesSpawned, NodesRemoved, FelledSet.Num(), Pawns.Num());
	}
}

// A player's machine: what the server says is felled
void AOakForest::SyncNodeCharges(const TArray<FNodeCharge>& List)
{
	TSet<int32> Want;
	for (const FNodeCharge& N : List)
	{
		if (N.Charges > 0) continue;
		const FVector L = N.Loc;
		const FIntPoint K(FMath::FloorToInt(L.X / 2000.f), FMath::FloorToInt(L.Y / 2000.f));
		for (int32 dx = -1; dx <= 1; ++dx)
			for (int32 dy = -1; dy <= 1; ++dy)
				if (const TArray<int32>* Cell = NodeCells.Find(FIntPoint(K.X + dx, K.Y + dy)))
					for (int32 i : *Cell)
					{
						const FVector T = Fellable[i].Tree.GetLocation();
						if (FVector::DistSquared2D(T, L) < 9.f && FMath::Abs(T.Z - L.Z) < 3.f) Want.Add(i);
					}
	}
	const int32 Before = FelledSet.Num();
	for (int32 i : Want) SetFelled(i, true);
	for (int32 i : TArray<int32>(FelledSet.Array())) if (!Want.Contains(i)) SetFelled(i, false);
	if (Before != FelledSet.Num() || Want.Num()) UE_LOG(LogTemp, Log, TEXT("JEEBFOREST synced from the server's list: %d felled (was %d), %d node actors live"), FelledSet.Num(), Before, Nodes.Num());
}


// This player's camera (local visuals only)
bool AOakForest::CameraNow(FVector& Out) const
{
	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->PlayerCameraManager) return false;
	Out = PC->PlayerCameraManager->GetCameraLocation();
	return true;
}

// The trees' stand-in shadow casters (jeeb.Forest.FarShade), one mesh per kind, made again when jeeb.Forest.FarShadeSize changes, all in
// one instance of M_TreeShade (see-through: jeeb.Forest.ShadeDensity, in sun flecks: jeeb.Forest.Dapple)
void AOakForest::BuildShadeMeshes()
{
	ShadeSizeNow = CVarFarShadeSize.GetValueOnGameThread();
	const float Size = FMath::Clamp(ShadeSizeNow, 0.2f, 1.5f);
	if (!ShadeMid)
	{
		if (UMaterialInterface* Shade = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_TreeShade.M_TreeShade"), nullptr, LOAD_Quiet | LOAD_NoWarn)) ShadeMid = UMaterialInstanceDynamic::Create(Shade, this);
		else UE_LOG(LogTemp, Warning, TEXT("JEEBFOREST no /Game/M_TreeShade (Tools/BuildMaterials.ps1): the trees' shade is solid"));
	}
	for (int32 i = 0; i < ShadeSets.Num(); ++i)
	{
		if (!ShadeSets[i]) continue;
		const FKindDef& K = Kinds[ShadeKind[i]];
		UStaticMesh* M = MakeShadeMesh(this, ShadeBounds[i], K.Crown, K.TrunkR, K.TrunkH, IsSpruce(ShadeKind[i]), Size);
		ShadeSets[i]->SetStaticMesh(M);
		if (ShadeMid) ShadeSets[i]->SetMaterial(0, ShadeMid);
		UE_LOG(LogTemp, Log, TEXT("JEEBFOREST stand-in shadow %d (kind %d): crown %.0f cm, trunk %.0f to %.0f cm, mesh %.0f x %.0f x %.0f cm, size %.2f, %d triangles"), i, ShadeKind[i], K.Crown, K.TrunkR, K.TrunkH,
			ShadeBounds[i].GetSize().X, ShadeBounds[i].GetSize().Y, ShadeBounds[i].GetSize().Z, Size, M && M->GetRenderData() && M->GetRenderData()->LODResources.Num() ? M->GetRenderData()->LODResources[0].GetNumTriangles() : -1);
	}
	DensityNow = DappleNow = -1.f;
}

// Refills the stand-ins with the trees within jeeb.Forest.FarShadeReach of this player's camera (a local look only: each machine its
// own), every 8 m of camera movement; a set is only touched when what it holds changes
void AOakForest::UpdateShadows(bool bForce)
{
	// the stand-ins' look (live)
	const float Dens = FMath::Clamp(CVarShadeDensity.GetValueOnGameThread(), 0.f, 1.f), Dapple = FMath::Max(0.f, CVarDapple.GetValueOnGameThread());
	if (ShadeMid && (Dens != DensityNow || Dapple != DappleNow))
	{
		DensityNow = Dens; DappleNow = Dapple;
		ShadeMid->SetScalarParameterValue(TEXT("Density"), Dens);
		ShadeMid->SetScalarParameterValue(TEXT("Dapple"), Dapple);
	}
	FVector Cam3;
	if (!CameraNow(Cam3)) return;
	const FVector2D Cam(Cam3);
	constexpr float Step = 800.f;
	if (!bForce && FVector2D::DistSquared(Cam, ShadeAt) < Step * Step) return;
	ShadeAt = Cam;
	const bool bOn = CVarShadows.GetValueOnGameThread() != 0 && CVarShadowAll.GetValueOnGameThread() == 0;   // (ShadowAll: the drawn trees cast)
	const float RF = (bOn && CVarFarShade.GetValueOnGameThread() != 0) ? FMath::Max(0.f, CVarFarShadeReach.GetValueOnGameThread()) * 100.f : 0.f;
	if (ShadeSets.Num() && !FMath::IsNearlyEqual(ShadeSizeNow, CVarFarShadeSize.GetValueOnGameThread())) BuildShadeMeshes();
	auto Cells = [this](const FVector2D& C, float Scan, TFunctionRef<void(const FIntPoint&, float)> Do)
	{
		const int32 C0x = FMath::FloorToInt((C.X - Scan) / 2000.f), C1x = FMath::FloorToInt((C.X + Scan) / 2000.f);
		const int32 C0y = FMath::FloorToInt((C.Y - Scan) / 2000.f), C1y = FMath::FloorToInt((C.Y + Scan) / 2000.f);
		for (int32 cx = C0x; cx <= C1x; ++cx)
			for (int32 cy = C0y; cy <= C1y; ++cy)
				if (const TArray<FIntPoint>* L = ShadowCells.Find(FIntPoint(cx, cy)))
					for (const FIntPoint& E : *L) Do(E, float(FVector2D::DistSquared(FVector2D(ShadowAll[E.X][E.Y].GetLocation()), C)));
	};
	auto HashOf = [](const FIntPoint& E) { return (uint32(E.X) * 2654435761u) ^ (uint32(E.Y) * 2246822519u + 0x9e3779b9u); };   // (summed: order does not matter)
	TArray<TArray<FTransform>> Far;
	Far.SetNum(ShadeSets.Num());
	uint32 FarH = 0u;
	if (RF > 0.f)
		Cells(Cam, RF, [&](const FIntPoint& E, float D2)
		{
			if (D2 < RF * RF && ShadeOfTwin.IsValidIndex(E.X)) { Far[ShadeOfTwin[E.X]].Add(ShadowAll[E.X][E.Y]); FarH += HashOf(E); }
		});
	FarH ^= uint32(Far.Num()) + (uint32(RF) << 8);
	for (const TArray<FTransform>& F : Far) FarH = FarH * 31u + uint32(F.Num());
	if (FarH != ShadeHash || bForce)
	{
		ShadeHash = FarH;
		int32 N = 0;
		for (int32 s = 0; s < ShadeSets.Num(); ++s)
		{
			if (!ShadeSets[s]) continue;
			ShadeSets[s]->ClearInstances();
			if (Far[s].Num()) ShadeSets[s]->AddInstances(Far[s], false, true, false);
			N += Far[s].Num();
		}
		if (CVarShadowLog.GetValueOnGameThread()) UE_LOG(LogTemp, Log, TEXT("JEEBFOREST refill frame %llu cam %.0f %.0f: %d stand-ins"), (unsigned long long)GFrameCounter, Cam.X, Cam.Y, N);
		if (bForce) UE_LOG(LogTemp, Log, TEXT("JEEBFOREST shadows: %d stand-ins within %.0f m, density %.2f, flecks %.0f cm, material %s"), N, RF / 100.f, Dens, Dapple, ShadeMid ? TEXT("M_TreeShade") : TEXT("none"));
	}
#if !UE_BUILD_SHIPPING
	// (jeeb.Forest.ShadowLog 3, 2026-11-18: trees within 150 m with no other tree within 12 m, for distance tests of one tree's shadow)
	if (bForce && CVarShadowLog.GetValueOnGameThread() >= 3)
		Cells(Cam, 15000.f, [&](const FIntPoint& E, float D2)
		{
			if (D2 > 15000.f * 15000.f || !ShadeOfTwin.IsValidIndex(E.X)) return;
			const FTransform& T = ShadowAll[E.X][E.Y];
			bool bLone = true;
			Cells(FVector2D(T.GetLocation()), 1200.f, [&](const FIntPoint& O, float O2) { if (O != E && O2 < 1200.f * 1200.f) bLone = false; });
			if (!bLone) return;
			const int32 Sh = ShadeOfTwin[E.X], Kd = ShadeKind[Sh];
			UE_LOG(LogTemp, Log, TEXT("JEEBFOREST lone tree kind %d at %.0f %.0f %.0f yaw %.0f scale %.2f: top %.0f cm, crown %.0f cm, %.0f m from the camera"), Kd, T.GetLocation().X, T.GetLocation().Y, T.GetLocation().Z,
				T.Rotator().Yaw, T.GetScale3D().Z, ShadeBounds[Sh].Max.Z * T.GetScale3D().Z, Kinds[Kd].Crown * T.GetScale3D().X, FMath::Sqrt(D2) / 100.f);
		});
#endif
}

// ------------------------------------------------------------------ chopping (AOakTreeNode)
AOakTreeNode::AOakTreeNode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.DoNotCreateDefaultSubobject(TEXT("TrunkCollision")))
{
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("JeebFastRegrow"))) RespawnSeconds = 45.f;   // (tests: a felled tree grows back in 45 s, not 5 minutes)
#endif
}

void AOakTreeNode::OnChargesChanged(int32 Remaining, int32 Max)
{
	bDepleted = Remaining <= 0;
	if (AOakForest* F = Forest.Get()) F->SetFelled(Index, Remaining <= 0);
}

void AOakForest::SetFelled(int32 Index, bool bFelled)
{
	if (!Fellable.IsValidIndex(Index) || Fellable[Index].bFelled == bFelled) return;
	FFellable& F = Fellable[Index];
	F.bFelled = bFelled;
	if (bFelled) FelledSet.Add(Index); else FelledSet.Remove(Index);
	const FVector At = F.Tree.GetLocation();
	UE_LOG(LogTemp, Log, TEXT("JEEBFOREST tree %d %s at %.0f %.0f %.0f"), Index, bFelled ? TEXT("felled") : TEXT("grown back"), At.X, At.Y, At.Z);
	// the trunk solid: a stump's height (to 60 cm above the foot), or the whole trunk again
	if (TrunkSolids && F.Trunk >= 0)
	{
		FTransform T = F.TrunkSolid;
		if (bFelled) { T.SetLocation(At + FVector(0.f, 0.f, 10.f)); T.SetScale3D(FVector(T.GetScale3D().X, T.GetScale3D().Y, 1.f)); }
		TrunkSolids->UpdateInstanceTransform(F.Trunk, T, true, true, true);
	}
	// the drawn tree and its shadow (clients)
	FTransform Gone = F.Tree; Gone.SetScale3D(FVector(0.001f));
	if (Drawn.IsValidIndex(F.Drawn) && Drawn[F.Drawn]) Drawn[F.Drawn]->UpdateInstanceTransform(F.KeyIndex, bFelled ? Gone : F.Tree, true, true, true);
	if (ShadowAll.IsValidIndex(F.Shadow) && ShadowAll[F.Shadow].IsValidIndex(F.KeyIndex))
	{
		ShadowAll[F.Shadow][F.KeyIndex] = bFelled ? Gone : F.Tree;
		ShadeHash = 0u;
		UpdateShadows(true);
	}
	// a stump where it stood, about as wide as the trunk
	if (FelledStumps && StumpBounds.IsValid)
	{
		const FVector Sz = StumpBounds.GetSize();
		const float S = FMath::Clamp(F.Radius * 2.6f / FMath::Max(1.f, FMath::Max(Sz.X, Sz.Y)), 0.35f, 2.5f);
		const float Yaw = float((Index * 2654435761u) % 360u);
		const FTransform T(FRotator(0.f, Yaw, 0.f), At - FVector(0.f, 0.f, 6.f), FVector(bFelled ? S : 0.001f));
		if (F.Stump < 0) { if (bFelled) F.Stump = FelledStumps->AddInstance(T, true); }
		else FelledStumps->UpdateInstanceTransform(F.Stump, T, true, true, true);
	}
}

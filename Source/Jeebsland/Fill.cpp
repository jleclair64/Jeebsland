#include "Fill.h"
#include "FillData.h"
#include "OakHill.h"
#include "TownLab.h"
#include "TownLabDecor.h"
#include "TownLabSwaps.h"
#include "Terrain.h"
#include "WarFortLadders.h"
#include "WorldMap.h"
#include "DayNight.h"
#include "ResourceNode.h"
#include "ChoppableTree.h"
#include "OakForest.h"
#include "MiningRock.h"
#include "Pond.h"
#include "Cottage.h"
#include "Npc.h"
#include "AutumnDecor.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "GameFramework/Pawn.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "ProceduralMeshComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Misc/Crc.h"
#include "LampLook.h"
#include "UObject/UObjectIterator.h"

// ====================================================================================================== switches
static TAutoConsoleVariable<int32> CVarFillShow(TEXT("jeeb.Fill.Show"), 1, TEXT("0 hides the world fill's props (their solids stay); 1 shows them"));
static TAutoConsoleVariable<FString> CVarFillZone(TEXT("jeeb.Fill.Zone"), TEXT(""), TEXT("Build only this fill zone (a FillZones.csv name, or OakHill for its hand-placed sets); empty: all. Read when the world builds (or -FillZone=)"));
static TAutoConsoleVariable<float> CVarFillCull(TEXT("jeeb.Fill.CullScale"), 1.f, TEXT("The fill's tiny and small props' draw distance, times this (0.5 on LOW, matching the grass)"));

namespace
{
	bool TestSwitch(const TCHAR* Name)
	{
#if UE_BUILD_SHIPPING
		return false;
#else
		return FParse::Param(FCommandLine::Get(), Name);
#endif
	}
}

// ====================================================================================================== the data files
namespace
{
	FString Norm(const FString& S)
	{
		FString O;
		for (TCHAR C : S) if (FChar::IsAlnum(C)) O.AppendChar(FChar::ToLower(C));
		return O;
	}
	struct FCsv
	{
		TArray<FString> Head;   // normalized
		TArray<TArray<FString>> Rows;
		TArray<int32> Lines;
		bool bCmHeader = false;
		// The column of the first name that matches (a unit after the name, "FootX_m" or "FootX (cm)", is allowed)
		int32 Col(std::initializer_list<const TCHAR*> Names) const
		{
			for (const TCHAR* N : Names)
			{
				const FString W = Norm(N);
				for (int32 i = 0; i < Head.Num(); ++i)
					if (Head[i] == W || Head[i] == W + TEXT("m") || Head[i] == W + TEXT("cm") || Head[i] == W + TEXT("deg")) return i;
			}
			return -1;
		}
		FString S(int32 R, int32 C) const { return C >= 0 && C < Rows[R].Num() ? Rows[R][C] : FString(); }
		float F(int32 R, int32 C, float Def) const { const FString V = S(R, C); return V.IsEmpty() ? Def : FCString::Atof(*V); }
		int32 I(int32 R, int32 C, int32 Def) const { const FString V = S(R, C); return V.IsEmpty() ? Def : FCString::Atoi(*V); }
	};
	bool ReadCsv(const FString& Path, FCsv& Out)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *Path)) return false;
		for (int32 i = 0; i < Lines.Num(); ++i)
		{
			const FString L = Lines[i].TrimStartAndEnd();
			if (L.IsEmpty() || L.StartsWith(TEXT("#"))) continue;
			TArray<FString> Cells;
			L.ParseIntoArray(Cells, TEXT(","), false);
			for (FString& C : Cells) C.TrimStartAndEndInline();
			if (Out.Head.Num() == 0)
			{
				for (const FString& C : Cells) { Out.Head.Add(Norm(C)); if (C.Contains(TEXT("cm"))) Out.bCmHeader = true; }
				continue;
			}
			Out.Rows.Add(Cells);
			Out.Lines.Add(i + 1);
		}
		return Out.Head.Num() > 0;
	}
	FString KindName(FString K)
	{
		K.TrimStartAndEndInline();
		if (K.StartsWith(TEXT("SM_"))) K.RightChopInline(3);
		return K;
	}
}

void JeebFill::LoadData(FData& D)
{
	D.Folder = FPaths::ProjectContentDir() / TEXT("Data");
#if !UE_BUILD_SHIPPING
	FString Alt;
	if (FParse::Value(FCommandLine::Get(), TEXT("FillData="), Alt)) D.Folder = Alt;
#endif
	auto Path = [&](const TCHAR* Name) { return D.Folder / Name; };

	// ---- kinds
	{
		FCsv C;
		if (!ReadCsv(Path(TEXT("FillKinds.csv")), C)) D.Notes.Add(TEXT("no FillKinds.csv (kinds' sizes from their meshes: test only)"));
		else
		{
			const int32 cKind = C.Col({ TEXT("Kind"), TEXT("Name") }), cMesh = C.Col({ TEXT("Mesh"), TEXT("Path"), TEXT("Asset") }), cShape = C.Col({ TEXT("Shape"), TEXT("Footprint") });
			const int32 cFx = C.Col({ TEXT("FootX"), TEXT("SizeX"), TEXT("Width"), TEXT("LenX") }), cFy = C.Col({ TEXT("FootY"), TEXT("SizeY"), TEXT("Depth"), TEXT("LenY") });
			const int32 cCx = C.Col({ TEXT("FootCX"), TEXT("CX"), TEXT("OffX") }), cCy = C.Col({ TEXT("FootCY"), TEXT("CY"), TEXT("OffY") });
			const int32 cH = C.Col({ TEXT("Height"), TEXT("SizeZ"), TEXT("H") }), cCol = C.Col({ TEXT("Collision"), TEXT("Col"), TEXT("Solid") });
			const int32 cSh = C.Col({ TEXT("Shadow") }), cCull = C.Col({ TEXT("Cull"), TEXT("CullClass"), TEXT("SizeClass") }), cSl = C.Col({ TEXT("Slope"), TEXT("MaxSlope") });
			const int32 cFl = C.Col({ TEXT("Flags") }), cLight = C.Col({ TEXT("Light") });
			// metres or centimetres: the header says so, else the sizes do (a footprint is under 20 m; in cm even a jug is 23)
			float MaxV = 0.f;
			for (int32 r = 0; r < C.Rows.Num(); ++r) MaxV = FMath::Max3(MaxV, C.F(r, cFx, 0.f), FMath::Max(C.F(r, cFy, 0.f), C.F(r, cH, 0.f)));
			D.bKindsCm = C.bCmHeader || MaxV > 20.f;
			const float U = D.bKindsCm ? 1.f : 100.f;
			for (int32 r = 0; r < C.Rows.Num(); ++r)
			{
				FKind K;
				K.Name = KindName(C.S(r, cKind));
				if (K.Name.IsEmpty()) continue;
				K.Mesh = C.S(r, cMesh);
				const FString Sh = C.S(r, cShape).ToLower();
				K.Shape = (Sh.StartsWith(TEXT("c")) || Sh.StartsWith(TEXT("r"))) ? EShape::Circle : EShape::Box;
				K.FootX = FMath::Max(1.f, C.F(r, cFx, 0.5f) * U); K.FootY = K.Shape == EShape::Circle ? K.FootX : FMath::Max(1.f, C.F(r, cFy, C.F(r, cFx, 0.5f)) * U);
				K.FootCX = C.F(r, cCx, 0.f) * U; K.FootCY = C.F(r, cCy, 0.f) * U;
				K.Height = C.F(r, cH, 0.5f) * U;
				const FString Co = C.S(r, cCol).ToLower();
				K.Col = (Co.IsEmpty() || Co == TEXT("none") || Co == TEXT("no") || Co == TEXT("0") || Co == TEXT("false")) ? ECol::None
					: (Co.StartsWith(TEXT("cap")) || Co == TEXT("thin") || Co == TEXT("pole")) ? ECol::Capsule
					: (Co == TEXT("mesh") || Co.StartsWith(TEXT("walk")) || Co == TEXT("stairs")) ? ECol::Mesh : ECol::Box;
				const FString Sd = C.S(r, cSh).ToLower();
				K.Shadow = Sd.StartsWith(TEXT("y")) ? EShadow::Yes : Sd.StartsWith(TEXT("ne")) ? EShadow::Near : EShadow::No;
				const FString Cu = C.S(r, cCull).ToLower();
				K.Cull = Cu.StartsWith(TEXT("t")) ? ECull::Tiny : Cu.StartsWith(TEXT("s")) ? ECull::Small : Cu.StartsWith(TEXT("m")) ? ECull::Medium : Cu.StartsWith(TEXT("la")) ? ECull::Large : Cu.StartsWith(TEXT("lan")) || Cu.StartsWith(TEXT("lm")) ? ECull::Landmark : ECull::Medium;
				if (Cu.StartsWith(TEXT("land"))) K.Cull = ECull::Landmark;
				{
					// (no Slope given: 20 degrees, or 30 for the soft and natural kinds; Oak Hill's ground runs 12-20 degrees in many places, the oak's lawn 13)
					const bool bNatural = K.Name.StartsWith(TEXT("Bush")) || K.Name.StartsWith(TEXT("Shrub")) || K.Name.StartsWith(TEXT("Plant")) || K.Name.StartsWith(TEXT("Flower")) || K.Name.StartsWith(TEXT("Rock")) || K.Name.StartsWith(TEXT("Boulder")) || K.Name.StartsWith(TEXT("Log")) || K.Name.StartsWith(TEXT("Stump"));
					K.Slope = C.F(r, cSl, bNatural ? 30.f : 20.f);
				}
				TArray<FString> Fl; C.S(r, cFl).ParseIntoArray(Fl, TEXT("|"));
				for (FString F : Fl)
				{
					F.TrimStartAndEndInline();
					const FString L = F.ToLower();
					if (L == TEXT("light")) K.bLight = true;
					else if (L == TEXT("hang") || L == TEXT("hung")) K.bHang = true;
					else if (L == TEXT("goods")) K.bGoods = true;
					else if (L.StartsWith(TEXT("flame="))) K.Flame = KindName(F.Mid(6));
				}
				if (cLight >= 0 && (C.S(r, cLight) == TEXT("1") || C.S(r, cLight).ToLower() == TEXT("yes") || C.S(r, cLight).ToLower() == TEXT("true"))) K.bLight = true;
				K.bFromCsv = true;
				D.Kinds.Add(K.Name, K);
			}
		}
	}
	// ---- sets
	{
		FCsv C;
		if (!ReadCsv(Path(TEXT("FillSets.csv")), C)) D.Notes.Add(TEXT("no FillSets.csv"));
		else
		{
			const int32 cSet = C.Col({ TEXT("Set") }), cKind = C.Col({ TEXT("Kind") }), cX = C.Col({ TEXT("X") }), cY = C.Col({ TEXT("Y") }), cZ = C.Col({ TEXT("Z") });
			const int32 cYaw = C.Col({ TEXT("Yaw") }), cS = C.Col({ TEXT("Scale") }), cFl = C.Col({ TEXT("Flags") });
			for (int32 r = 0; r < C.Rows.Num(); ++r)
			{
				const FString SetName = C.S(r, cSet);
				if (SetName.IsEmpty()) continue;
				FSet& S = D.Sets.FindOrAdd(SetName);
				S.Name = SetName;
				FPiece P;
				P.Kind = KindName(C.S(r, cKind));
				P.Pos = FVector(C.F(r, cX, 0.f), C.F(r, cY, 0.f), C.F(r, cZ, 0.f)) * 100.f;
				P.Yaw = C.F(r, cYaw, 0.f); P.Scale = C.F(r, cS, 1.f);
				if (P.Scale <= 0.f) P.Scale = 1.f;
				TArray<FString> Fl; C.S(r, cFl).ParseIntoArray(Fl, TEXT("|"));
				for (FString F : Fl)
				{
					F.TrimStartAndEndInline();
					const FString L = F.ToLower();
					if (L == TEXT("drop")) P.bDrop = true;
					else if (L == TEXT("stack")) P.bStack = true;
					else if (L == TEXT("hang")) P.bHang = true;
					else if (L.StartsWith(TEXT("turn="))) P.Turn = FCString::Atof(*F.Mid(5));
					else if (L.StartsWith(TEXT("alt="))) { TArray<FString> A; F.Mid(4).ParseIntoArray(A, TEXT("/")); for (const FString& K : A) P.Alts.Add(KindName(K)); }
				}
				S.Pieces.Add(P);
				S.Reach = FMath::Max(S.Reach, float(FVector2D(P.Pos.X, P.Pos.Y).Size()) + 150.f);
			}
			// (the wall-backed sets are named in the comment above their rows: "# BarrelRow (wall-backed): ...")
			TArray<FString> Lines;
			if (FFileHelper::LoadFileToStringArray(Lines, *Path(TEXT("FillSets.csv"))))
				for (const FString& L : Lines)
				{
					if (!L.StartsWith(TEXT("# ")) || !L.Contains(TEXT("(wall-backed"))) continue;
					FString Name = L.Mid(2);
					int32 Sp = INDEX_NONE;
					if (Name.FindChar(TEXT(' '), Sp)) Name.LeftInline(Sp);
					if (FSet* S = D.Sets.Find(Name)) S->bWall = true;
				}
		}
	}
	// ---- Oak Hill's hand-placed sets
	{
		FCsv C;
		if (!ReadCsv(Path(TEXT("FillOakHill.csv")), C)) D.Notes.Add(TEXT("no FillOakHill.csv"));
		else
		{
			const int32 cSet = C.Col({ TEXT("Set") }), cE = C.Col({ TEXT("E") }), cN = C.Col({ TEXT("N") }), cYaw = C.Col({ TEXT("Yaw") }), cSeed = C.Col({ TEXT("Seed") });
			const int32 cVar = C.Col({ TEXT("Var") }), cPlot = C.Col({ TEXT("Plot") }), cNote = C.Col({ TEXT("Note") }), cSite = C.Col({ TEXT("Site") });
			for (int32 r = 0; r < C.Rows.Num(); ++r)
			{
				FOakRow O;
				O.Set = C.S(r, cSet);
				if (O.Set.IsEmpty()) continue;
				O.E = C.F(r, cE, 0.f); O.N = C.F(r, cN, 0.f); O.Yaw = C.F(r, cYaw, 0.f);
				O.Seed = C.I(r, cSeed, r + 1); O.bVar = C.I(r, cVar, 0) != 0; O.Plot = C.I(r, cPlot, -1); O.Note = C.S(r, cNote); O.Line = C.Lines[r];
				O.bSquare = C.S(r, cSite).StartsWith(TEXT("sq"), ESearchCase::IgnoreCase);
				O.bSteele = C.S(r, cSite).StartsWith(TEXT("st"), ESearchCase::IgnoreCase);
				O.bRel = C.S(r, cSite).StartsWith(TEXT("pl"), ESearchCase::IgnoreCase);
				D.Oak.Add(O);
			}
		}
	}
	// ---- zones
	{
		FCsv C;
		if (!ReadCsv(Path(TEXT("FillZones.csv")), C)) D.Notes.Add(TEXT("no FillZones.csv"));
		else
		{
			const int32 cZ = C.Col({ TEXT("Zone") }), cOn = C.Col({ TEXT("On"), TEXT("Enabled") }), cD = C.Col({ TEXT("Density") }), cMix = C.Col({ TEXT("Mix"), TEXT("Sets") });
			const int32 cSeed = C.Col({ TEXT("Seed") }), cFace = C.Col({ TEXT("Facing"), TEXT("Face") }), cTrees = C.Col({ TEXT("Trees") });
			for (int32 r = 0; r < C.Rows.Num(); ++r)
			{
				FZone Z;
				Z.Name = C.S(r, cZ);
				if (Z.Name.IsEmpty()) continue;
				const FString On = C.S(r, cOn).ToLower();
				Z.bOn = cOn < 0 || On == TEXT("1") || On == TEXT("yes") || On == TEXT("true") || On == TEXT("on");
				Z.Density = C.F(r, cD, 0.f);
				TArray<FString> Mix; C.S(r, cMix).ParseIntoArray(Mix, TEXT(" "));
				for (const FString& M : Mix)
				{
					FString A, B;
					if (M.Split(TEXT(":"), &A, &B)) Z.Mix.Add(TPair<FString, float>(A, FMath::Max(0.f, FCString::Atof(*B))));
					else if (!M.IsEmpty()) Z.Mix.Add(TPair<FString, float>(M, 1.f));
				}
				Z.Seed = C.I(r, cSeed, 0);
				const FString F = C.S(r, cFace).ToLower();
				Z.Facing = F.StartsWith(TEXT("r")) && F != TEXT("random") ? EFacing::Road : F.StartsWith(TEXT("w")) ? EFacing::Wall : EFacing::Random;
				Z.Trees = C.F(r, cTrees, 0.f);
				D.Zones.Add(Z.Name, Z);
			}
		}
	}
	// ---- tree spots
	{
		FCsv C;
		if (!ReadCsv(Path(TEXT("FillTrees.csv")), C)) D.Notes.Add(TEXT("no FillTrees.csv"));
		else
		{
			const int32 cSite = C.Col({ TEXT("Site") }), cE = C.Col({ TEXT("E") }), cN = C.Col({ TEXT("N") }), cCl = C.Col({ TEXT("Class") }), cR = C.Col({ TEXT("Radius") }), cNote = C.Col({ TEXT("Note") });
			for (int32 r = 0; r < C.Rows.Num(); ++r)
			{
				FTreeRow T;
				T.Site = C.S(r, cSite); if (T.Site.IsEmpty()) T.Site = TEXT("OakHill");
				T.E = C.F(r, cE, 0.f); T.N = C.F(r, cN, 0.f); T.Class = C.S(r, cCl); T.Radius = C.F(r, cR, 1.5f) * 100.f; T.Note = C.S(r, cNote);
				D.Trees.Add(T);
			}
		}
	}
	// ---- plots (Placement v2)
	{
		FCsv C;
		if (!ReadCsv(Path(TEXT("FillPlots.csv")), C)) D.Notes.Add(TEXT("no FillPlots.csv (no plots, no fence runs)"));
		else
		{
			const int32 cP = C.Col({ TEXT("Plot"), TEXT("Name") }), cSite = C.Col({ TEXT("Site") }), cUse = C.Col({ TEXT("Use") }), cB = C.Col({ TEXT("Building") });
			const int32 cO = C.Col({ TEXT("Outline") }), cBo = C.Col({ TEXT("Bound") }), cD = C.Col({ TEXT("Depth") }), cS = C.Col({ TEXT("Side") });
			const int32 cG = C.Col({ TEXT("Gate") }), cSeed = C.Col({ TEXT("Seed") }), cNote = C.Col({ TEXT("Note") });
			for (int32 r = 0; r < C.Rows.Num(); ++r)
			{
				FPlotRow P;
				P.Name = C.S(r, cP); P.Use = C.S(r, cUse);
				if (P.Name.IsEmpty() || P.Use.IsEmpty()) continue;
				P.Site = C.S(r, cSite); if (P.Site.IsEmpty()) P.Site = TEXT("OakHill");
				P.Building = C.I(r, cB, -1);
				TArray<FString> Pts; C.S(r, cO).ParseIntoArray(Pts, TEXT(";"));
				for (const FString& Pt : Pts)
				{
					TArray<FString> V; Pt.TrimStartAndEnd().ParseIntoArray(V, TEXT(" "));
					if (V.Num() >= 2) P.Outline.Add(FVector2D(FCString::Atof(*V[0]), FCString::Atof(*V[1])));
				}
				P.Bound = C.S(r, cBo).ToLower(); if (P.Bound.IsEmpty()) P.Bound = TEXT("none");
				P.Depth = C.F(r, cD, 8.f); P.Side = C.F(r, cS, 3.f);
				P.Gate = C.S(r, cG).ToLower(); if (P.Gate.IsEmpty()) P.Gate = TEXT("auto");
				P.Seed = C.I(r, cSeed, r + 1); P.Note = C.S(r, cNote); P.Line = C.Lines[r];
				D.Plots.Add(P);
			}
		}
	}
	// ---- what each use places
	{
		FCsv C;
		if (!ReadCsv(Path(TEXT("FillUses.csv")), C)) D.Notes.Add(TEXT("no FillUses.csv (plots get their runs only)"));
		else
		{
			const int32 cU = C.Col({ TEXT("Use") }), cSet = C.Col({ TEXT("Set") }), cA = C.Col({ TEXT("Anchor") }), cMin = C.Col({ TEXT("Min") }), cMax = C.Col({ TEXT("Max") });
			for (int32 r = 0; r < C.Rows.Num(); ++r)
			{
				FUseRow U;
				const FString Use = C.S(r, cU);
				U.Set = C.S(r, cSet);
				if (Use.IsEmpty() || U.Set.IsEmpty()) continue;
				const FString A = C.S(r, cA).ToLower();
				U.Anchor = A.StartsWith(TEXT("fr")) ? EAnchor::Front : A.StartsWith(TEXT("f")) ? EAnchor::Fence : A.StartsWith(TEXT("c")) ? EAnchor::Corner : A.StartsWith(TEXT("g")) ? EAnchor::Gate
					: A.StartsWith(TEXT("r")) ? EAnchor::Row : A.StartsWith(TEXT("t")) ? EAnchor::Trees : A.StartsWith(TEXT("ou")) ? EAnchor::Outside
				: A.StartsWith(TEXT("o")) ? EAnchor::Open : EAnchor::Wall;
				U.Min = C.I(r, cMin, 0); U.Max = FMath::Max(U.Min, C.I(r, cMax, 1));
				D.Uses.FindOrAdd(Use).Add(U);
			}
		}
	}
}

// ====================================================================================================== shapes and the grid
namespace
{
	using namespace JeebFill;

	constexpr float Cell = 25.f;              // the occupancy grid
	constexpr int32 ChunkN = 128;             // cells per chunk side: 32 m
	constexpr float ChunkCm = Cell * ChunkN;
	enum : uint16
	{
		F_BUILD = 1,     // a building, wall, fence or other big solid: no prop, no walking
		F_WATER = 2,     // river (plus 1 m), pond, moat, sea
		F_ROAD = 4,      // on a road (props keep 0.4 m off, posts 0.2 m)
		F_KEEP = 8,      // kept clear for players: door aprons and lanes, counters, NPC walks, ladders, spawn, gates, the pier
		F_GAME = 16,     // an existing small solid (hay, pumpkins, rocks, trunks, benches)
		F_SOFT = 32,     // existing visual decor (TownLab decor, lamp posts): no prop, walkable
		F_TREE = 64,     // a reserved tree spot
		F_PROP = 128,    // a fill piece's footprint
		F_PSOLID = 256,  // a fill piece the knight cannot walk through
	};
	constexpr uint16 NoProp = F_BUILD | F_WATER | F_KEEP | F_GAME | F_SOFT | F_TREE;
	constexpr uint16 NoWalk = F_BUILD | F_WATER | F_GAME | F_PSOLID;

	struct FShape
	{
		uint8 Type = 0;                          // 0 box, 1 circle, 2 capsule (a lane)
		FVector2D C = FVector2D::ZeroVector;     // the middle (a capsule's first end)
		FVector2D P1 = FVector2D::ZeroVector;    // a capsule's other end
		FVector2D H = FVector2D::ZeroVector;     // a box's half size along its own X and Y
		float Cs = 1.f, Sn = 0.f, R = 0.f;       // a box's turn; a circle's or capsule's radius
		static FShape Box(const FVector2D& Mid, const FVector2D& Half, float YawDeg) { FShape S; S.Type = 0; S.C = Mid; S.H = Half; S.Cs = FMath::Cos(FMath::DegreesToRadians(YawDeg)); S.Sn = FMath::Sin(FMath::DegreesToRadians(YawDeg)); return S; }
		static FShape Circle(const FVector2D& Mid, float Rad) { FShape S; S.Type = 1; S.C = Mid; S.R = Rad; return S; }
		static FShape Lane(const FVector2D& A, const FVector2D& B, float Rad) { FShape S; S.Type = 2; S.C = A; S.P1 = B; S.R = Rad; return S; }
		FVector2D Local(const FVector2D& P) const { const FVector2D D = P - C; return FVector2D(D.X * Cs + D.Y * Sn, -D.X * Sn + D.Y * Cs); }
		FVector2D World(const FVector2D& L) const { return C + FVector2D(L.X * Cs - L.Y * Sn, L.X * Sn + L.Y * Cs); }
		FBox2D Bounds(float Pad = 0.f) const
		{
			FVector2D E;
			if (Type == 0) E = FVector2D(FMath::Abs(Cs) * H.X + FMath::Abs(Sn) * H.Y, FMath::Abs(Sn) * H.X + FMath::Abs(Cs) * H.Y);
			else E = FVector2D(R, R);
			FBox2D B(C - E, C + E);
			if (Type == 2) B += FBox2D(P1 - E, P1 + E);
			return B.ExpandBy(Pad);
		}
		bool Contains(const FVector2D& P, float Pad) const
		{
			if (Type == 1) return FVector2D::DistSquared(P, C) <= FMath::Square(R + Pad);
			if (Type == 2)
			{
				const FVector2D AB = P1 - C;
				const float T = FMath::Clamp(FVector2D::DotProduct(P - C, AB) / FMath::Max(AB.SizeSquared(), 1.f), 0.f, 1.f);
				return FVector2D::DistSquared(P, C + AB * T) <= FMath::Square(R + Pad);
			}
			const FVector2D L = Local(P);
			return FMath::Abs(L.X) <= H.X + Pad && FMath::Abs(L.Y) <= H.Y + Pad;
		}
		float Area() const { return Type == 0 ? 4.f * H.X * H.Y : Type == 1 ? PI * R * R : 2.f * R * float(FVector2D::Distance(C, P1)) + PI * R * R; }
		float MaxHalf() const { return Type == 0 ? FMath::Max(H.X, H.Y) : R; }
	};
	// Two footprints (boxes or circles) closer than Pad
	bool Touch(const FShape& A, const FShape& B, float Pad)
	{
		if (A.Type == 1 && B.Type == 1) return FVector2D::DistSquared(A.C, B.C) < FMath::Square(A.R + B.R + Pad);
		if (A.Type == 1 || B.Type == 1)
		{
			const FShape& Bx = A.Type == 0 ? A : B; const FShape& Ci = A.Type == 1 ? A : B;
			const FVector2D L = Bx.Local(Ci.C);
			const FVector2D Q(FMath::Clamp(L.X, -Bx.H.X, Bx.H.X), FMath::Clamp(L.Y, -Bx.H.Y, Bx.H.Y));
			return FVector2D::DistSquared(L, Q) < FMath::Square(Ci.R + Pad);
		}
		const FVector2D Ax[4] = { FVector2D(A.Cs, A.Sn), FVector2D(-A.Sn, A.Cs), FVector2D(B.Cs, B.Sn), FVector2D(-B.Sn, B.Cs) };
		const FVector2D D = B.C - A.C;
		for (const FVector2D& V : Ax)
		{
			const float Pa = FMath::Abs(FVector2D::DotProduct(Ax[0], V)) * A.H.X + FMath::Abs(FVector2D::DotProduct(Ax[1], V)) * A.H.Y;
			const float Pb = FMath::Abs(FVector2D::DotProduct(Ax[2], V)) * B.H.X + FMath::Abs(FVector2D::DotProduct(Ax[3], V)) * B.H.Y;
			if (FMath::Abs(FVector2D::DotProduct(D, V)) >= Pa + Pb + Pad) return false;
		}
		return true;
	}
	int32 FloorDiv(int32 A, int32 B) { return A >= 0 ? A / B : -((-A + B - 1) / B); }
	uint32 HashU(uint32 N) { N ^= N >> 16; N *= 0x7feb352dU; N ^= N >> 15; N *= 0x846ca68bU; N ^= N >> 16; return N; }
	void HashIn(uint32& H, float V)
	{
		const int32 Q = FMath::RoundToInt(V * 10.f);   // millimetres
		for (int32 b = 0; b < 4; ++b) { H ^= uint32((Q >> (8 * b)) & 0xFF); H *= 16777619u; }
	}

	// ---------------------------------------------------------------------------------------------- covered ground (grass, leaves)
	// (fillfix 2026-11-16: each solid piece's own collision footprint, not whole 1 m cells, so a cart no longer leaves a bare square;
	// its rim is dithered over Edge cm, so the grass thins into it instead of stopping at a hard line)
	struct FCover { FShape S; float Edge; };
	TArray<FCover> GCover;
	TMap<FIntPoint, TArray<int32>> GCoverBuckets;   // (4 m squares)
	FBox2D GCoveredBox(ForceInit);
	int32 GCoverCells = 0;                          // (m^2 covered, for the log)
	void ResetCover() { GCover.Reset(); GCoverBuckets.Reset(); GCoveredBox = FBox2D(ForceInit); GCoverCells = 0; }
	void AddCover(const FShape& S)
	{
		const float Edge = FMath::Min(40.f, 0.4f * (S.Type == 0 ? FMath::Min(S.H.X, S.H.Y) : S.R));
		const int32 Ci = GCover.Add({ S, Edge });
		const FBox2D B = S.Bounds();
		GCoveredBox += B;
		for (int32 Hx = FMath::FloorToInt(B.Min.X / 400.f); Hx <= FMath::FloorToInt(B.Max.X / 400.f); ++Hx)
			for (int32 Hy = FMath::FloorToInt(B.Min.Y / 400.f); Hy <= FMath::FloorToInt(B.Max.Y / 400.f); ++Hy) GCoverBuckets.FindOrAdd(FIntPoint(Hx, Hy)).Add(Ci);
		GCoverCells += FMath::RoundToInt(S.Area() / 10000.f);
	}
}

bool JeebFill::AnyCovered() { return GCover.Num() > 0; }
bool JeebFill::Covered(float X, float Y)
{
	if (GCover.Num() == 0 || X < GCoveredBox.Min.X || X > GCoveredBox.Max.X || Y < GCoveredBox.Min.Y || Y > GCoveredBox.Max.Y) return false;
	const TArray<int32>* L = GCoverBuckets.Find(FIntPoint(FMath::FloorToInt(X / 400.f), FMath::FloorToInt(Y / 400.f)));
	if (!L) return false;
	const FVector2D P(X, Y);
	for (int32 Ci : *L)
	{
		const FCover& C = GCover[Ci];
		// how far inside the footprint (cm)
		float In;
		if (C.S.Type == 1) In = C.S.R - float(FVector2D::Distance(P, C.S.C));
		else { const FVector2D Lc = C.S.Local(P); In = FMath::Min(C.S.H.X - FMath::Abs(Lc.X), C.S.H.Y - FMath::Abs(Lc.Y)); }
		if (In <= 0.f) continue;
		if (In >= C.Edge) return true;
		// the soft rim: fewer and fewer tufts toward the inside (a fixed dither per spot, the same every time it is asked)
		const float T = In / FMath::Max(C.Edge, 1.f), Want = T * T * (3.f - 2.f * T);
		const uint32 H = HashU(uint32(FMath::FloorToInt(X)) * 73856093u ^ uint32(FMath::FloorToInt(Y)) * 19349663u ^ 0x5bd1e995u);
		if (float(H & 0xffff) / 65536.f < Want) return true;
	}
	return false;
}

namespace
{
	using namespace JeebFill;

	struct FNamedShape { FShape S; uint16 Bit; FString Name; };

	struct FGrid
	{
		TMap<FIntPoint, TArray<uint16>> Chunks;
		TMap<FIntPoint, TArray<int32>> Buckets;   // the blocker shapes touching each chunk (stamped when it is made)
		TArray<FNamedShape> Shapes;
		TFunction<void(const FIntPoint&, TArray<uint16>&)> MakeStatics;
		double MakeSeconds = 0.0;

		static FIntPoint KeyOf(int32 Gx, int32 Gy) { return FIntPoint(FloorDiv(Gx, ChunkN), FloorDiv(Gy, ChunkN)); }
		static int32 G(float V) { return FMath::FloorToInt(V / Cell); }
		static FVector2D Centre(int32 Gx, int32 Gy) { return FVector2D((Gx + 0.5f) * Cell, (Gy + 0.5f) * Cell); }
		TArray<uint16>& Chunk(const FIntPoint& K)
		{
			if (TArray<uint16>* Got = Chunks.Find(K)) return *Got;
			const double T0 = FPlatformTime::Seconds();
			TArray<uint16>& Bits = Chunks.Add(K);
			Bits.SetNumZeroed(ChunkN * ChunkN);
			if (MakeStatics) MakeStatics(K, Bits);
			if (const TArray<int32>* B = Buckets.Find(K))
				for (int32 Si : *B) StampInto(K, Bits, Shapes[Si].S, Shapes[Si].Bit);
			MakeSeconds += FPlatformTime::Seconds() - T0;
			return Bits;
		}
		uint16& At(int32 Gx, int32 Gy)
		{
			const FIntPoint K = KeyOf(Gx, Gy);
			return Chunk(K)[(Gx - K.X * ChunkN) * ChunkN + (Gy - K.Y * ChunkN)];
		}
		void StampInto(const FIntPoint& K, TArray<uint16>& Bits, const FShape& S, uint16 Bit)
		{
			const FBox2D B = S.Bounds();
			const int32 X0 = FMath::Max(G(B.Min.X), K.X * ChunkN), X1 = FMath::Min(G(B.Max.X), K.X * ChunkN + ChunkN - 1);
			const int32 Y0 = FMath::Max(G(B.Min.Y), K.Y * ChunkN), Y1 = FMath::Min(G(B.Max.Y), K.Y * ChunkN + ChunkN - 1);
			for (int32 Gx = X0; Gx <= X1; ++Gx)
				for (int32 Gy = Y0; Gy <= Y1; ++Gy)
					if (S.Contains(Centre(Gx, Gy), 0.f)) Bits[(Gx - K.X * ChunkN) * ChunkN + (Gy - K.Y * ChunkN)] |= Bit;
		}
		// A blocker: into the chunks made already, and remembered for the ones made later
		void AddShape(const FShape& S, uint16 Bit, const FString& Name)
		{
			const int32 Si = Shapes.Add({ S, Bit, Name });
			const FBox2D B = S.Bounds();
			const FIntPoint K0 = KeyOf(G(B.Min.X), G(B.Min.Y)), K1 = KeyOf(G(B.Max.X), G(B.Max.Y));
			for (int32 Kx = K0.X; Kx <= K1.X; ++Kx)
				for (int32 Ky = K0.Y; Ky <= K1.Y; ++Ky)
				{
					const FIntPoint K(Kx, Ky);
					Buckets.FindOrAdd(K).Add(Si);
					if (TArray<uint16>* Got = Chunks.Find(K)) StampInto(K, *Got, S, Bit);
				}
		}
		// A placed piece: straight into the grid (making its chunks)
		void Mark(const FShape& S, uint16 Bit)
		{
			const FBox2D B = S.Bounds();
			for (int32 Gx = G(B.Min.X); Gx <= G(B.Max.X); ++Gx)
				for (int32 Gy = G(B.Min.Y); Gy <= G(B.Max.Y); ++Gy)
					if (S.Contains(Centre(Gx, Gy), 0.f)) At(Gx, Gy) |= Bit;
		}
		// Any of Mask within Pad of the shape
		bool Hits(const FShape& S, float Pad, uint16 Mask)
		{
			const FBox2D B = S.Bounds(Pad);
			for (int32 Gx = G(B.Min.X); Gx <= G(B.Max.X); ++Gx)
				for (int32 Gy = G(B.Min.Y); Gy <= G(B.Max.Y); ++Gy)
					if ((At(Gx, Gy) & Mask) && S.Contains(Centre(Gx, Gy), Pad)) return true;
			return false;
		}
	};

	// ---------------------------------------------------------------------------------------------- zones
	enum EZone : uint8
	{
		Z_None, Z_OakBoxingRing, Z_OakRuins, Z_OakGarden, Z_OakGraveyard, Z_OakForts, Z_OakWatchtower, Z_OakBridge, Z_OakPlaza, Z_OakStreet,
		Z_OakYard, Z_OakFringe, Z_Blackrock, Z_MarketYard, Z_Cottage, Z_HutYards, Z_PigPen, Z_Pond, Z_Castle, Z_Graveyard,
		Z_Roadside, Z_RiverBank, Z_Grove, Z_WoodsEdge, Z_WoodsFloor, Z_Mountain, Z_PvP, Z_Open, Z_Count
	};
	const TCHAR* ZoneNames[Z_Count] = {
		TEXT("None"), TEXT("OakBoxingRing"), TEXT("OakRuins"), TEXT("OakGarden"), TEXT("OakGraveyard"), TEXT("OakForts"), TEXT("OakWatchtower"), TEXT("OakBridge"), TEXT("OakPlaza"), TEXT("OakStreet"),
		TEXT("OakYard"), TEXT("OakFringe"), TEXT("Blackrock"), TEXT("MarketYard"), TEXT("Cottage"), TEXT("HutYards"), TEXT("PigPen"), TEXT("Pond"), TEXT("Castle"), TEXT("Graveyard"),
		TEXT("Roadside"), TEXT("RiverBank"), TEXT("Grove"), TEXT("WoodsEdge"), TEXT("WoodsFloor"), TEXT("Mountain"), TEXT("PvP"), TEXT("Open") };
	const TCHAR ZoneChar[Z_Count + 1] = TEXT(".abcdefghijklmnopqrstuvwxyzA");
	bool IsOak(int32 Z) { return Z >= Z_OakBoxingRing && Z <= Z_OakFringe; }

	bool OakOn() { return JeebOakHill::Enabled() && JeebTownLab::RealTown(); }
	float OakRoadCm(float X, float Y) { return OakOn() ? JeebTownLab::RoadClearance(JeebTownLab::RealSite(), X, Y) : 1.0e6f; }

	int32 ZoneAt(float X, float Y)
	{
		using namespace JeebOakHill;
		if (OakOn() && InBox(X, Y))
		{
			const float E = EastM(Y), N = NorthM(X);
			for (const FPlot& P : Plots())
			{
				if (P.Kind != EPlot::Empty && P.Kind != EPlot::EmptyRound) continue;
				if (PlotSD(P, E, N) >= 2.f) continue;
				const FString Nm(P.Name);
				if (Nm == TEXT("Boxing Ring")) return Z_OakBoxingRing;
				if (Nm == TEXT("Castle Ruins")) return Z_OakRuins;
				if (Nm == TEXT("Garden")) return Z_OakGarden;
				if (Nm == TEXT("Graveyard")) return Z_OakGraveyard;
				if (Nm.Contains(TEXT("Fort"))) return Z_OakForts;
				if (Nm == TEXT("Watchtower")) return Z_OakWatchtower;
				if (Nm == TEXT("Bridge")) return Z_OakBridge;
			}
			if (FVector2D(E, N).Size() < 24.f) return Z_OakPlaza;
			if (OakRoadCm(X, Y) < 400.f) return Z_OakStreet;
			if (BuildingDistance(X, Y) < 1500.f) return Z_OakYard;
			if (GrassArea(X, Y)) return Z_OakFringe;
		}
		const FVector2D P(X, Y);
		if (FMath::Abs(X) < 12000.f && FMath::Abs(Y) < 12000.f)
		{
			using namespace JeebTerrain;
			if (RectSD(P, VillageCenter(), FVector2D(VillageHalf(), VillageHalf())) < -100.f) return Z_Blackrock;
			if (RectSD(P, MarketCenter(), MarketHalf() + FVector2D(600.f, 600.f)) < 0.f) return Z_MarketYard;
			if (FVector2D::Distance(P, FVector2D(CottagePos().X, CottagePos().Y)) < 1400.f) return Z_Cottage;
			for (const FVector2D& S : HutSites()) if (FVector2D::Distance(P, S) < 1000.f) return Z_HutYards;
			{ const float Sd = RectSD(P, PenCenter(), PenHalf()); if (Sd >= 0.f && Sd < 600.f) return Z_PigPen; }
			{ const float Dp = FVector2D::Distance(P, PondCenter()); if (Dp >= 700.f && Dp < 1400.f) return Z_Pond; }
			{ const float Dc = FVector2D::Distance(P, CastlePos()); if (Dc < CastleMoatIn - 120.f || (Dc > CastleMoatOut + 120.f && Dc < CastleMoatOut + 270.f)) return Z_Castle; }
			if (FVector2D::Distance(P, FVector2D(GraveyardPos().X, GraveyardPos().Y)) < 1200.f) return Z_Graveyard;
		}
		if (JeebWorld::InWater(X, Y)) return Z_None;
		if (FMath::Abs(X) < 16000.f && FMath::Abs(Y) < 16000.f) { const float Pc = JeebTerrain::PathClearance(X, Y); if (Pc >= 0.f && Pc < 600.f) return Z_Roadside; }
		{ float Half; const float Rd = JeebWorld::RiverDistance(X, Y, Half) - Half; if (Rd >= 100.f && Rd < 1000.f) return Z_RiverBank; }
		for (const FVector2D& G : JeebTerrain::RedwoodGroves()) if (FVector2D::Distance(P, G) < 2500.f) return Z_Grove;
		if (P.Size() < 9000.f)
		{
			const float W = JeebTerrain::WoodsDensity(X, Y);
			if (W > 0.55f) return Z_WoodsFloor;
			if (W > 0.45f) return Z_WoodsEdge;
		}
		if (JeebWorld::MountainMask(P) > 0.5f) return Z_Mountain;
		return Z_Open;
	}
	// Where each zone can be (cm): its cells are only looked for in here
	FBox2D ZoneBounds(int32 Z)
	{
		using namespace JeebTerrain;
		const FBox2D Oak(FVector2D(JeebOakHill::BoxX0, JeebOakHill::BoxY0), FVector2D(JeebOakHill::BoxX1, JeebOakHill::BoxY1));
		auto Around = [](const FVector2D& C, const FVector2D& H) { return FBox2D(C - H, C + H); };
		switch (Z)
		{
		case Z_Blackrock: return Around(VillageCenter(), FVector2D(VillageHalf(), VillageHalf()));
		case Z_MarketYard: return Around(MarketCenter(), MarketHalf() + FVector2D(600.f, 600.f));
		case Z_Cottage: return Around(FVector2D(CottagePos().X, CottagePos().Y), FVector2D(1400.f, 1400.f));
		case Z_HutYards: { FBox2D B(ForceInit); for (const FVector2D& S : HutSites()) B += Around(S, FVector2D(1000.f, 1000.f)); return B; }
		case Z_PigPen: return Around(PenCenter(), PenHalf() + FVector2D(600.f, 600.f));
		case Z_Pond: return Around(PondCenter(), FVector2D(1400.f, 1400.f));
		case Z_Castle: return Around(CastlePos(), FVector2D(CastleMoatOut + 270.f, CastleMoatOut + 270.f));
		case Z_Graveyard: return Around(FVector2D(GraveyardPos().X, GraveyardPos().Y), FVector2D(1200.f, 1200.f));
		case Z_Roadside: return FBox2D(FVector2D(-16000.f, -16000.f), FVector2D(16000.f, 16000.f));
		default: break;
		}
		if (IsOak(Z)) return Oak;
		return FBox2D(FVector2D(JeebWorld::MinX, JeebWorld::MinY), FVector2D(JeebWorld::MaxX, JeebWorld::MaxY));
	}

	// The model each Oak Hill building is drawn with, for its TownLab decor (TownLabSwaps.h, shared with TownLabTown.cpp)
	const TCHAR* TownLabName(const TCHAR* Model) { return JeebTownLab::RebuiltName(Model); }

	UStaticMesh* LoadMeshPath(const FString& InPath)
	{
		if (InPath.IsEmpty()) return nullptr;
		FString P = InPath;
		if (P.Contains(TEXT("."))) P = P.Left(P.Find(TEXT(".")));
		const FString Name = FPaths::GetBaseFilename(P);
		return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%s.%s"), *P, *Name), nullptr, LOAD_Quiet | LOAD_NoWarn);
	}
	bool InstancingOk(UStaticMesh* Mesh)
	{
		for (const FStaticMaterial& M : Mesh->GetStaticMaterials())
			if (M.MaterialInterface && M.MaterialInterface->GetMaterial() && !M.MaterialInterface->GetMaterial()->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes)) return false;
		return true;
	}
	float CullCm(ECull C)
	{
		switch (C) { case ECull::Tiny: return 3000.f; case ECull::Small: return 5000.f; case ECull::Medium: return 9000.f; case ECull::Large: return 18000.f; default: return 35000.f; }
	}
}

// ====================================================================================================== the builder
struct FFillBuilder
{
	AWorldFill* Owner = nullptr;
	UWorld* World = nullptr;
	bool bServer = false;
	FData D;
	FGrid Grid;
	FString OnlyZone;

	struct FPlaced
	{
		const FKind* Kind = nullptr;
		FTransform Xf;
		FShape Foot;
		bool bGround = true, bSolid = false, bWalkBlock = false;
		bool bSnug = false;   // (pulled onto a building's real wall face: checked against the wall's own collision, not the 50 cm grid)
		int32 Zone = 0;
		FString Set;
		int32 Group = -1;   // (a hand-placed row: its pieces may join end to end, so they are never checked against each other)
		FString Use;        // (the plot use that placed it, Placement v2; empty: a hand row or a rule zone)
	};
	TArray<FPlaced> Placed;
	TMap<FIntPoint, TArray<int32>> PieceHash;   // ground footprints by 4 m square
	TMap<FString, TUniquePtr<FKind>> Guessed;   // kinds not in FillKinds.csv, sized from their meshes (test only; null: no mesh either)
	TSet<FString> MissingLogged;
	struct FTarget { FString Name; FVector2D At; int32 Area; float Reach = 75.f; bool bReached = false; };   // (Reach: reached when the walkable ground comes this close, cm)
	TArray<FTarget> Targets;
	struct FTreeSpot { FVector2D At; float R; FString Class; };
	TArray<FTreeSpot> TreeSpots;

	struct FStats { int32 Sets = 0, Pieces = 0, Solid = 0; TSet<FString> Kinds; TMap<FString, int32> Skipped; uint32 Hash = 2166136261u; double Ms = 0.0; };
	TMap<FString, FStats> Stats;

	// ------------------------------------------------------------------ statics
	TMap<FIntPoint, TArray<int32>> OakPlotsNear;   // building plots whose walls reach each chunk
	void MakeStatics(const FIntPoint& K, TArray<uint16>& Bits)
	{
		using namespace JeebOakHill;
		const float X0 = K.X * ChunkCm, Y0 = K.Y * ChunkCm;
		// (the slow questions on a coarse lattice, read between its points: the square's paths every 1 m, the rivers every 2 m)
		const bool bSquare = X0 < 16500.f && X0 + ChunkCm > -16500.f && Y0 < 16500.f && Y0 + ChunkCm > -16500.f;
		float PathL[33][33], RiverL[17][17];
		const bool bPaths = bSquare || JeebTerrain::ForestRoadNear(FBox2D(FVector2D(X0, Y0), FVector2D(X0 + ChunkCm, Y0 + ChunkCm)));   // (and the forest's roads out of Oak Hill, OakForest.cpp)
		for (int32 i = 0; i <= 32; ++i) for (int32 j = 0; j <= 32; ++j) PathL[i][j] = bPaths ? JeebTerrain::PathClearance(X0 + i * 100.f, Y0 + j * 100.f) : 1.0e6f;
		for (int32 i = 0; i <= 16; ++i) for (int32 j = 0; j <= 16; ++j) { float Half; RiverL[i][j] = JeebWorld::RiverDistance(X0 + i * 200.f, Y0 + j * 200.f, Half) - Half; }
		auto Bi33 = [](const float (&L)[33][33], float U, float V) { const int32 I = FMath::Min(int32(U), 31), J = FMath::Min(int32(V), 31); const float Fu = U - I, Fv = V - J; return FMath::Lerp(FMath::Lerp(L[I][J], L[I][J + 1], Fv), FMath::Lerp(L[I + 1][J], L[I + 1][J + 1], Fv), Fu); };
		auto Bi17 = [](const float (&L)[17][17], float U, float V) { const int32 I = FMath::Min(int32(U), 15), J = FMath::Min(int32(V), 15); const float Fu = U - I, Fv = V - J; return FMath::Lerp(FMath::Lerp(L[I][J], L[I][J + 1], Fv), FMath::Lerp(L[I + 1][J], L[I + 1][J + 1], Fv), Fu); };
		const bool bOak = OakOn() && X0 < BoxX1 && X0 + ChunkCm > BoxX0 && Y0 < BoxY1 && Y0 + ChunkCm > BoxY0;
		TArray<int32> Near;
		if (bOak)
		{
			const TArray<FPlot>& Ps = Plots();
			const FBox2D CB(FVector2D(X0, Y0), FVector2D(X0 + ChunkCm, Y0 + ChunkCm));
			for (int32 i = 0; i < Ps.Num(); ++i)
			{
				if (Ps[i].Kind != EPlot::Building) continue;
				const FVector2D C = FromPx(Ps[i].Px, Ps[i].Py);
				const float R = Ps[i].Reach * 100.f + 300.f;
				if (CB.ExpandBy(R).IsInside(C)) Near.Add(i);
			}
		}
		const TArray<FWallBox>& Ws = WallBoxes();
		const FVector2D Pond = JeebTerrain::PondCenter(), Castle = JeebTerrain::CastlePos();
		const bool bPond = FVector2D::Distance(Pond, FVector2D(X0 + ChunkCm * 0.5f, Y0 + ChunkCm * 0.5f)) < JeebTerrain::PondWaterR + 2400.f;
		const bool bMoat = FVector2D::Distance(Castle, FVector2D(X0 + ChunkCm * 0.5f, Y0 + ChunkCm * 0.5f)) < JeebTerrain::CastleMoatOut + 2400.f;
		for (int32 i = 0; i < ChunkN; ++i)
		{
			const float X = X0 + (i + 0.5f) * Cell;
			const float Coast = JeebWorld::CoastY(X);
			for (int32 j = 0; j < ChunkN; ++j)
			{
				const float Y = Y0 + (j + 0.5f) * Cell;
				uint16 B = 0;
				const float U = (i + 0.5f) * Cell / 100.f, V = (j + 0.5f) * Cell / 100.f;
				if (FMath::Min(Bi33(PathL, U, V), OakRoadCm(X, Y)) < 0.f) B |= F_ROAD;
				if (Bi17(RiverL, U * 0.5f, V * 0.5f) < 100.f || Y - Coast > 200.f) B |= F_WATER;
				if (bPond && FVector2D::Distance(FVector2D(X, Y), Pond) < JeebTerrain::PondWaterR + 100.f) B |= F_WATER;
				if (bMoat)
				{
					const FVector2D Dv = FVector2D(X, Y) - Castle;
					const float Dc = Dv.Size();
					const bool bBridge = Dv.X < 0.f && FMath::Abs(Dv.Y) < JeebTerrain::CastleGateHalf + 20.f;
					if (Dc > JeebTerrain::CastleMoatIn - 120.f && Dc < JeebTerrain::CastleMoatOut + 120.f && !bBridge) B |= F_WATER;
				}
				for (int32 Pi : Near)
				{
					const FWallBox& W = Ws[Pi];
					bool bIn = false;
					if (!W.bWalls) bIn = PlotSD(Plots()[Pi], EastM(Y), NorthM(X)) < 0.f;
					else
					{
						const float DX = X - W.Mid.X, DY = Y - W.Mid.Y;
						bIn = FMath::Abs(DX * W.Cs + DY * W.Sn) <= W.Half.X && FMath::Abs(-DX * W.Sn + DY * W.Cs) <= W.Half.Y;
						if (!bIn && W.Foot)
						{
							const float FX = X - W.Centre.X, FY = Y - W.Centre.Y;
							bIn = InFootprint(*W.Foot, (FX * W.Cs + FY * W.Sn) / W.Scale, -(-FX * W.Sn + FY * W.Cs) / W.Scale);
						}
					}
					if (bIn) { B |= F_BUILD; break; }
				}
				Bits[i * ChunkN + j] |= B;
			}
		}
	}

	void Keep(const FShape& S, const FString& Name, uint16 Bit = F_KEEP) { Grid.AddShape(S, Bit, Name); }
	void Target(const FString& Name, const FVector2D& At, int32 Area, float Reach = 75.f) { Targets.Add({ Name, At, Area, Reach }); }

	// Everything solid that blocks a player near the ground, from every actor built so far (the same on every machine: the server
	// decides where players walk, so all of it is built there too). Visual-only actors and replicated ones are left to the lists below.
	int32 ScanCount = 0, SampleCount = 0;
	void ScanSolids()
	{
		auto Ground = [](float X, float Y) { return JeebTerrain::HeightAt(X, Y); };
		// (near the ground: the knight would walk into it. A thin wide slab, under 60 cm thick and over 1.5 m^2, is a floor, deck or bridge he walks on)
		auto NearGround = [&](const FBox& B) { const float G = Ground(B.GetCenter().X, B.GetCenter().Y); if (B.GetSize().Z < 60.f && B.GetSize().X * B.GetSize().Y > 15000.f) return false; return B.Min.Z < G + 180.f && B.Max.Z > G + 15.f; };
		auto AddBoxOf = [&](const FTransform& Xf, const FBox& Local, const FString& Name)
		{
			const FVector Sc = Xf.GetScale3D();
			const FVector Mid = Xf.TransformPosition(Local.GetCenter());
			const FVector Half = Local.GetExtent() * Sc.GetAbs();
			const FShape S = FShape::Box(FVector2D(Mid.X, Mid.Y), FVector2D(Half.X, Half.Y), Xf.Rotator().Yaw);
			Grid.AddShape(S, S.Area() > 40000.f ? F_BUILD : F_GAME, Name);
			++ScanCount;
		};
		// a big solid mesh (a building's collision shape): its own body sampled every 50 cm, 40-180 cm above the ground, so its notches
		// and porches are right, not its whole bounding box
		auto Sample = [&](const FBodyInstance* Body, const FTransform& Xf, const FBox& Local, const FString& Name)
		{
			const FVector Sc = Xf.GetScale3D().GetAbs();
			const FVector Lo = Local.Min * Sc, Hi = Local.Max * Sc;
			const FQuat Q = FRotator(0.f, Xf.Rotator().Yaw, 0.f).Quaternion();
			const FCollisionShape Probe = FCollisionShape::MakeBox(FVector(25.f, 25.f, 70.f));
			for (float Lx = Lo.X + 25.f; Lx < Hi.X; Lx += 50.f)
				for (float Ly = Lo.Y + 25.f; Ly < Hi.Y; Ly += 50.f)
				{
					const FVector W = Xf.GetLocation() + Q.RotateVector(FVector(Lx, Ly, 0.f));
					const float G = Ground(W.X, W.Y);
					++SampleCount;
					if (Body->OverlapTest(FVector(W.X, W.Y, G + 110.f), Q, Probe, nullptr, true))
						Grid.AddShape(FShape::Box(FVector2D(W.X, W.Y), FVector2D(25.f, 25.f), Xf.Rotator().Yaw), F_BUILD, Name);
				}
		};
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* A = *It;
			if (A == Owner || A->GetIsReplicated() || A->IsA<APawn>() || A->IsA<ANpc>()) continue;
			TArray<UPrimitiveComponent*> Comps;
			A->GetComponents(Comps);
			for (UPrimitiveComponent* C : Comps)
			{
				if (!C || !C->IsRegistered() || C->IsA<UProceduralMeshComponent>()) continue;
				if (!C->IsCollisionEnabled() || C->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block) continue;
				const FString Name = TEXT("scan ") + A->GetClass()->GetName();
				if (const UInstancedStaticMeshComponent* I = Cast<UInstancedStaticMeshComponent>(C))
				{
					if (!I->GetStaticMesh()) continue;
					const FBox Local = I->GetStaticMesh()->GetBoundingBox();

					for (int32 k = 0; k < I->GetInstanceCount(); ++k)
					{
						FTransform Xf; I->GetInstanceTransform(k, Xf, true);
						const FBox WB = Local.TransformBy(Xf);
						if (!NearGround(WB) || WB.GetSize().X > 8000.f || WB.GetSize().Y > 8000.f) continue;
						const FVector Sz = Local.GetSize() * Xf.GetScale3D().GetAbs();
						FBodyInstance* Body = Sz.X * Sz.Y > 300000.f ? I->GetBodyInstance(NAME_None, true, k) : nullptr;
						if (Body) Sample(Body, Xf, Local, Name);
						else AddBoxOf(Xf, Local, Name);
					}
					continue;
				}
				if (const UStaticMeshComponent* M = Cast<UStaticMeshComponent>(C))
				{
					if (!M->GetStaticMesh()) continue;
					const FBox Local = M->GetStaticMesh()->GetBoundingBox();
					const FTransform Xf = M->GetComponentTransform();
					const FBox WB = Local.TransformBy(Xf);
					if (!NearGround(WB) || WB.GetSize().X > 8000.f || WB.GetSize().Y > 8000.f) continue;
					const FVector Sz = Local.GetSize() * Xf.GetScale3D().GetAbs();
					if (Sz.X * Sz.Y > 300000.f) Sample(M->GetBodyInstance(), Xf, Local, Name);
					else AddBoxOf(Xf, Local, Name);
					continue;
				}
				const FTransform Xf = C->GetComponentTransform();
				if (const UBoxComponent* Bx = Cast<UBoxComponent>(C))
				{
					const FVector E = Bx->GetScaledBoxExtent();
					const FBox WB = C->Bounds.GetBox();
					if (!NearGround(WB) || WB.GetSize().X > 8000.f || WB.GetSize().Y > 8000.f) continue;
					const FRotator R = Xf.Rotator();
					if (FMath::Abs(R.Pitch) < 5.f && FMath::Abs(R.Roll) < 5.f) AddBoxOf(FTransform(FRotator(0.f, R.Yaw, 0.f), Xf.GetLocation()), FBox(-E, E), Name);
					else AddBoxOf(FTransform::Identity, WB, Name);
					continue;
				}
				const FBox WB = C->Bounds.GetBox();
				if (!NearGround(WB) || WB.GetSize().X > 8000.f || WB.GetSize().Y > 8000.f) continue;
				if (const UCapsuleComponent* Cap = Cast<UCapsuleComponent>(C)) { Grid.AddShape(FShape::Circle(FVector2D(WB.GetCenter()), Cap->GetScaledCapsuleRadius()), F_GAME, Name); ++ScanCount; continue; }
				if (const USphereComponent* Sp = Cast<USphereComponent>(C)) { Grid.AddShape(FShape::Circle(FVector2D(WB.GetCenter()), Sp->GetScaledSphereRadius()), F_GAME, Name); ++ScanCount; continue; }
				AddBoxOf(FTransform::Identity, WB, Name);
			}
		}
	}

	// Oak Hill: doors and the lanes from them to the road, counters, yards and plots to reach, the oak's roots, the spawn
	void OakKeeps()
	{
		using namespace JeebOakHill;
		if (!OakOn()) return;
		const TArray<FPlot>& Ps = Plots();
		const TArray<FWallBox>& Ws = WallBoxes();
		auto LaneToRoad = [&](const FVector2D& From, float Rad, const FString& Name)
		{
			FVector2D NearEN;
			const float Dm = RoadClearanceM(EastM(From.Y), NorthM(From.X), &NearEN);
			if (Dm > 40.f) return;
			Keep(FShape::Lane(From, FVector2D(PosX + NearEN.Y * 100.f, PosY + NearEN.X * 100.f), Rad), Name + TEXT(" lane"));
		};
		for (int32 i = 0; i < Ps.Num(); ++i)
		{
			const FPlot& P = Ps[i];
			if (P.Kind == EPlot::Empty || P.Kind == EPlot::EmptyRound)
			{
				const FVector2D C = FromPx(P.Px, P.Py);
				Target(FString::Printf(TEXT("plot %s"), P.Name), C, 0, FMath::Max(75.f, 0.5f * FMath::Min(P.W, P.H) * UnitCm));   // (reached anywhere on it: the bridge's plot is mostly river)
				continue;
			}
			if (P.Kind != EPlot::Building || !P.Model || IsStall(P.Model) || FCString::Strcmp(P.Model, TEXT("OakHillWell")) == 0) continue;
			const FModel* M = FindModel(P.Model);
			if (!M) continue;
			const float Yaw = BuildingYaw(P, M);
			const FVector2D C = FromPx(P.Px, P.Py);
			// the door: between the front wall's two lanterns where the rebuilt model has them, else the middle of the front wall
			float Lx = (M->WX0 + M->WX1) * 0.5f * M->Scale;
			if (const TCHAR* Tl = TownLabName(P.Model))
				if (const JeebTownLabDecor::FBuildingDecor* Dc = JeebTownLabDecor::For(Tl))
				{
					float Sum = 0.f; int32 N = 0;
					for (int32 k = 0; k < Dc->Num; ++k) if (Dc->Pieces[k].bLight && Dc->Pieces[k].Y > 0.f) { Sum += Dc->Pieces[k].X; ++N; }
					if (N >= 2) Lx = Sum / N * M->Scale;
				}
			const float Ly = -M->WY0 * M->Scale;
			const FShape Fr = FShape::Box(C, FVector2D(1.f, 1.f), Yaw);
			const FVector2D Door = Fr.World(FVector2D(Lx, Ly));
			const FVector2D Out = Fr.World(FVector2D(0.f, 1.f)) - C;
			Keep(FShape::Box(Door + Out * 160.f, FVector2D(120.f, 170.f), Yaw), FString::Printf(TEXT("door %d %s"), i, P.Name));
			LaneToRoad(Door + Out * 300.f, 60.f, FString::Printf(TEXT("door %d %s"), i, P.Name));
			Target(FString::Printf(TEXT("door %d %s"), i, P.Name), Door + Out * 230.f, 0);
			// yards: 2.5 m out from the back and both sides of the walls
			if (Ws[i].bWalls)
			{
				const FShape Wb = FShape::Box(Ws[i].Mid, Ws[i].Half, Yaw);
				const FVector2D Sides[3] = { FVector2D(Ws[i].Half.X + 250.f, 0.f), FVector2D(-Ws[i].Half.X - 250.f, 0.f), FVector2D(0.f, -Ws[i].Half.Y - 250.f) };
				const TCHAR* SideName[3] = { TEXT("side+"), TEXT("side-"), TEXT("back") };
				for (int32 s = 0; s < 3; ++s) Target(FString::Printf(TEXT("yard %d %s %s"), i, P.Name, SideName[s]), Wb.World(Sides[s]), 0);
			}
		}
		for (const FCounter& Ct : Counters())
		{
			Keep(FShape::Circle(Ct.At, 250.f), FString::Printf(TEXT("counter %s"), Ct.Name));
			LaneToRoad(Ct.At, 75.f, FString::Printf(TEXT("counter %s"), Ct.Name));
			Target(FString::Printf(TEXT("counter %s"), Ct.Name), Ct.At, 0);
		}
		// (the plaza oak is gone, 2026-11-16: its spot is a low flower bed, a FillOakHill row, so no roots keep)
		Keep(FShape::Circle(FVector2D(SpawnX, SpawnY), 500.f), TEXT("Oak Hill spawn"));
		// the rebuilt buildings' own decor (TownLabDecor.h: stones, weeds, props at their feet): visual, walkable, not to be covered
		int32 Pi = -1;
		for (const FPlot& P : Ps)
		{
			++Pi;
			if (P.Kind != EPlot::Building || !P.Model) continue;
			const TCHAR* Tl = TownLabName(P.Model);
			const JeebTownLabDecor::FBuildingDecor* Dc = Tl ? JeebTownLabDecor::For(Tl) : nullptr;
			const FModel* M = FindModel(P.Model);
			if (!Dc || !M) continue;
			const FVector2D C = FromPx(P.Px, P.Py);
			const float Sc = M->Scale, ScZ = M->ScaleZ > 0.f ? M->ScaleZ : Sc;
			const FTransform Xf(FRotator(0.f, BuildingYaw(P, M), 0.f), FVector(C.X, C.Y, 0.f), FVector(Sc, Sc, ScZ));
			for (int32 k = 0; k < Dc->Num; ++k)
			{
				const JeebTownLabDecor::FDecor& Pc = Dc->Pieces[k];
				if (Pc.bScaleWithBuilding || Pc.Z > 40.f) continue;   // (ivy, moss, lanterns: on the walls)
				const FTransform W = JeebTownLabDecor::Place(Xf, Pc);
				FString Mesh(Pc.Mesh);
				const FString KName = KindName(FPaths::GetBaseFilename(Mesh.Left(Mesh.Find(TEXT(".")) >= 0 ? Mesh.Find(TEXT(".")) : Mesh.Len())));
				float R = 35.f * FMath::Max(Pc.SX, Pc.SY);
				if (const FKind* K = D.Kinds.Find(KName)) R = 0.5f * FMath::Max(K->FootX, K->FootY) * FMath::Max(Pc.SX, Pc.SY);
				Keep(FShape::Circle(FVector2D(W.GetLocation()), R), FString::Printf(TEXT("decor %s"), *KName), F_SOFT);
			}
		}
	}

	// The original square: Blackrock's gates, ladders, benches, NPCs and their walks, the market's counters, huts' doors, the pen,
	// the pier, the castle's gate and ladders, the forts' ladders, the scarecrows, the start
	void SquareKeeps()
	{
		using namespace JeebTerrain;
		const FVector2D VC = VillageCenter();
		const float H = VillageHalf();
		auto PathLane = [&](const FVector2D& From, float Rad, const FString& Name)
		{
			const FVector2D Np = NearestPathPoint(From.X, From.Y);
			if (FVector2D::Distance(Np, From) < 2500.f) Keep(FShape::Lane(From, Np, Rad), Name + TEXT(" lane"));
		};
		// gates (east and north walls) and the way through them to the fountain and out to the road
		const FVector2D Gates[2] = { VC + FVector2D(H, 0.f), VC + FVector2D(0.f, H) };
		const FVector2D GateOut[2] = { FVector2D(1.f, 0.f), FVector2D(0.f, 1.f) };
		for (int32 g = 0; g < 2; ++g)
		{
			Keep(FShape::Lane(VC + GateOut[g] * 350.f, Gates[g] + GateOut[g] * 600.f, 200.f), FString::Printf(TEXT("Blackrock gate %d"), g));
			PathLane(Gates[g] + GateOut[g] * 600.f, 120.f, FString::Printf(TEXT("Blackrock gate %d"), g));
			Target(FString::Printf(TEXT("Blackrock gate %d inside"), g), Gates[g] - GateOut[g] * 300.f, 1);
			Target(FString::Printf(TEXT("Blackrock gate %d outside"), g), Gates[g] + GateOut[g] * 400.f, 1);
		}
		for (const FLadder& L : Ladders()) { Keep(FShape::Lane(L.Pos, L.Pos + L.Out * 220.f, 80.f), TEXT("Blackrock ladder")); Target(TEXT("Blackrock ladder"), L.Pos + L.Out * 120.f, 1); }
		for (const FSeat& S : Seats())
		{
			const FVector2D F(FMath::Cos(FMath::DegreesToRadians(S.Yaw)), FMath::Sin(FMath::DegreesToRadians(S.Yaw)));
			Keep(FShape::Lane(S.Pos, S.Pos + F * 160.f, 90.f), TEXT("Blackrock bench front"));
			Target(TEXT("Blackrock bench"), S.Pos + F * 110.f, 1);
		}
		// villagers' walks (Village.cpp: rings of 650 round the plaza with errands to the hut doors; the others' beats) and the gate knight's
		{
			TArray<FVector2D> Ring;
			for (int32 k = 0; k < 24; ++k) { const float A = 2.f * PI * k / 24.f; Ring.Add(VC + FVector2D(FMath::Cos(A), FMath::Sin(A)) * 650.f); }
			for (int32 k = 0; k < 24; ++k) Keep(FShape::Lane(Ring[k], Ring[(k + 1) % 24], 80.f), TEXT("villagers' walk"));
			const FVector2D HutFronts[5] = { FVector2D(-900.f, 880.f), FVector2D(900.f, 880.f), FVector2D(-900.f, -880.f), FVector2D(900.f, -880.f), FVector2D(-780.f, 0.f) };
			for (const FVector2D& Hf : HutFronts) Keep(FShape::Lane(VC + Hf.GetSafeNormal() * 650.f, VC + Hf, 80.f), TEXT("villagers' errand"));
			auto RingAround = [&](const FVector2D& C, float R, const TCHAR* Name) { for (int32 k = 0; k < 18; ++k) { const float A0 = 2.f * PI * k / 18.f, A1 = 2.f * PI * (k + 1) / 18.f; Keep(FShape::Lane(C + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * R, C + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * R, 80.f), Name); } };
			RingAround(HutSites()[0], 620.f, TEXT("Garrick's walk"));
			RingAround(HutSites()[6], 620.f, TEXT("Wren's walk"));
			RingAround(FVector2D(GraveyardPos().X, GraveyardPos().Y), 520.f, TEXT("Fenwick's walk"));
			const FVector2D Pen = PenCenter(), Cas = CastlePos();
			Keep(FShape::Lane(Pen + FVector2D(-500.f, -900.f), Pen + FVector2D(0.f, -960.f), 80.f), TEXT("Hettie's walk"));
			Keep(FShape::Lane(Pen + FVector2D(0.f, -960.f), Pen + FVector2D(500.f, -900.f), 80.f), TEXT("Hettie's walk"));
			Keep(FShape::Lane(Cas + FVector2D(-1100.f, -300.f), Cas + FVector2D(-950.f, 0.f), 80.f), TEXT("Peregrine's walk"));
			Keep(FShape::Lane(Cas + FVector2D(-950.f, 0.f), Cas + FVector2D(-1100.f, 300.f), 80.f), TEXT("Peregrine's walk"));
			Keep(FShape::Lane(FVector2D(-780.f, -110.f), FVector2D(-780.f, -480.f), 90.f), TEXT("gate knight's walk"));
		}
		// the market's three traders: their stalls (solid already), the spot in front where you trade, and the way to it
		const FVector2D Npcs[3] = { ShopNpcPos(), SmithNpcPos(), BankNpcPos() };
		const TCHAR* NpcNames[3] = { TEXT("market store"), TEXT("market smith"), TEXT("market bank") };
		for (int32 k = 0; k < 3; ++k)
		{
			const FVector2D Front = Npcs[k] + FVector2D(0.f, 330.f);
			Keep(FShape::Circle(Front, 250.f), NpcNames[k]);
			PathLane(Front, 75.f, NpcNames[k]);
			Target(NpcNames[k], Front, 1);
		}
		// huts and the cottage: a door apron and the way to the road (ACottage: door on its +X at 380 cm, a lantern post beside it)
		for (TActorIterator<ACottage> It(World); It; ++It)
		{
			const FTransform Xf = It->GetActorTransform();
			const FVector2D Door(It->DoorPos());
			const FVector Fw = Xf.GetRotation().GetForwardVector();
			const FVector2D Out(Fw.X, Fw.Y);
			Keep(FShape::Box(Door + Out * 170.f, FVector2D(170.f, 120.f), Xf.Rotator().Yaw), TEXT("hut door"));
			PathLane(Door + Out * 300.f, 60.f, TEXT("hut door"));
			Keep(FShape::Circle(FVector2D(Xf.TransformPosition(FVector(395.f, 125.f, 0.f))), 30.f), TEXT("hut lantern post"), F_SOFT);
			Target(FString::Printf(TEXT("hut door at %.0f %.0f"), Door.X, Door.Y), Door + Out * 220.f, 1);
		}
		// the pig pen: the pigs' ground inside the fence stays theirs
		Keep(FShape::Box(PenCenter(), PenHalf(), 0.f), TEXT("pig pen"));
		// the pier (north bank of the pond, 1.5 m wide) and the way onto it
		{
			const FVector2D C = PondCenter();
			Keep(FShape::Lane(C + FVector2D(0.f, 320.f), C + FVector2D(0.f, 1150.f), 130.f), TEXT("pier"));
			Target(TEXT("pier"), C + FVector2D(0.f, 860.f), 1);
		}
		// the castle: through the gate and over the bridge, its ladders, the spire's door
		{
			const FVector2D C = CastlePos();
			Keep(FShape::Lane(C + FVector2D(-330.f, 0.f), C + FVector2D(-(CastleMoatOut + 500.f), 0.f), 200.f), TEXT("castle gate and bridge"));
			PathLane(C + FVector2D(-(CastleMoatOut + 500.f), 0.f), 120.f, TEXT("castle gate"));
			for (const FLadder& L : CastleLadders()) { Keep(FShape::Lane(L.Pos, L.Pos + L.Out * 220.f, 80.f), TEXT("castle ladder")); Target(TEXT("castle ladder"), L.Pos + L.Out * 120.f, 1); }
			Target(TEXT("castle courtyard"), C + FVector2D(-450.f, 0.f), 1);
			Target(TEXT("castle bridge"), C + FVector2D(-(CastleMoatOut + 300.f), 0.f), 1);
		}
		for (const FLadder& L : FortLadders()) if (L.Bottom < 0.f) { Keep(FShape::Lane(L.Pos, L.Pos + L.Out * 220.f, 80.f), TEXT("fort ladder")); Target(TEXT("fort ladder"), L.Pos + L.Out * 120.f, 1); }
		for (const FLadder& L : JeebWarFort::Ladders()) { Keep(FShape::Lane(L.Pos, L.Pos + L.Out * 220.f, 80.f), TEXT("Oak Hill fort ladder")); Target(TEXT("Oak Hill fort ladder"), L.Pos + L.Out * 120.f, 1); }   // (2026-11-20)
		// the fightable scarecrows (AutumnDecor.cpp; replicated, so a client may not have them yet: their fixed spots)
		Keep(FShape::Circle(FVector2D(1250.f, 1500.f), 150.f), TEXT("scarecrow"), F_GAME);
		Keep(FShape::Circle(FVector2D(-1350.f, -1000.f), 150.f), TEXT("scarecrow"), F_GAME);
		Keep(FShape::Circle(FVector2D(0.f, 0.f), 500.f), TEXT("start"));
		Target(TEXT("graveyard"), FVector2D(GraveyardPos().X, GraveyardPos().Y) + FVector2D(0.f, -700.f), 1);
	}

	// Gameplay nodes (trees, rocks, fishing spots): room to swing or cast; AutumnDecor's visual pieces near the ground
	void NodeAndDecorKeeps()
	{
		for (TActorIterator<AResourceNode> It(World); It; ++It)
		{
			if (It->IsA<AOakTreeNode>()) continue;   // (the forest's trees: the fill was laid out round them already)
			const FVector2D P(It->GetActorLocation());
			float R = 150.f;
			if (const AChoppableTree* T = Cast<AChoppableTree>(*It)) R = T->SurfaceRadius(0.f) + 150.f;
			else if (It->IsA<AFishingSpot>()) R = 200.f;
			Keep(FShape::Circle(P, R), It->GetClass()->GetName());
		}
		for (TActorIterator<AAutumnDecor> It(World); It; ++It)
		{
			TArray<UInstancedStaticMeshComponent*> Comps; It->GetComponents(Comps);
			for (UInstancedStaticMeshComponent* I : Comps)
			{
				const FString N = I->GetName();
				if (N.Contains(TEXT("Leaf")) || N.Contains(TEXT("Halo")) || N.Contains(TEXT("Flame")) || N.Contains(TEXT("Core")) || !I->GetStaticMesh()) continue;
				const FBox Local = I->GetStaticMesh()->GetBoundingBox();
				for (int32 k = 0; k < I->GetInstanceCount(); ++k)
				{
					FTransform Xf; I->GetInstanceTransform(k, Xf, true);
					const FBox W = Local.TransformBy(Xf);
					const float G = JeebTerrain::HeightAt(W.GetCenter().X, W.GetCenter().Y);
					if (W.Min.Z > G + 150.f || W.GetSize().X > 2000.f) continue;
					Keep(FShape::Circle(FVector2D(W.GetCenter()), 0.5f * FMath::Max(W.GetSize().X, W.GetSize().Y)), TEXT("autumn ") + N, F_SOFT);
				}
			}
		}
	}

	// Reserved tree spots from FillTrees.csv (the trees job plants them later)
	void TreeKeeps()
	{
		for (const FTreeRow& T : D.Trees)
		{
			if (!T.Site.Equals(TEXT("OakHill"), ESearchCase::IgnoreCase) || !OakOn()) continue;
			if (JeebOakHill::InWarFootprint(T.E, T.N, 2.f)) continue;   // (the same footprint rule as AOakForest: no tree spot inside a fort)
			const FVector2D At(JeebOakHill::PosX + T.N * 100.f, JeebOakHill::PosY + T.E * 100.f);
			Keep(FShape::Circle(At, T.Radius), TEXT("tree ") + T.Class, F_TREE);
			TreeSpots.Add({ At, T.Radius, T.Class });
		}
	}

	// ------------------------------------------------------------------ kinds
	const FKind* KindOf(const FString& Name)
	{
		if (const FKind* K = D.Kinds.Find(Name)) return K;
		if (const TUniquePtr<FKind>* G = Guessed.Find(Name)) return G->Get();
		// (not in FillKinds.csv: sized from its mesh's bounds. Test only: a machine without the mesh would place differently)
		FKind K;
		UStaticMesh* M = LoadMeshPath(TEXT("/Game/TownDecor/SM_") + Name);
		if (!M) M = LoadMeshPath(TEXT("/Game/Filler/SM_") + Name);
		if (!M)
		{
			Guessed.Add(Name, nullptr);
			if (!MissingLogged.Contains(Name)) { MissingLogged.Add(Name); UE_LOG(LogTemp, Warning, TEXT("JEEBFILL MISSING %s (not in FillKinds.csv and no mesh)"), *Name); }
			return nullptr;
		}
		const FBox B = M->GetBoundingBox();
		K.Name = Name; K.Shape = EShape::Box;
		K.FootX = FMath::Max(10.f, B.GetSize().X); K.FootY = FMath::Max(10.f, B.GetSize().Y); K.FootCX = B.GetCenter().X; K.FootCY = B.GetCenter().Y;
		K.Height = B.Max.Z;
		const float Big = FMath::Max3(K.FootX, K.FootY, K.Height);
		K.Cull = Big < 50.f ? ECull::Tiny : Big < 100.f ? ECull::Small : Big < 250.f ? ECull::Medium : ECull::Large;
		const bool bSoft = Name.StartsWith(TEXT("Bush")) || Name.StartsWith(TEXT("Shrub")) || Name.StartsWith(TEXT("Plant")) || Name.StartsWith(TEXT("Banner")) && !Name.Contains(TEXT("Pole")) && !Name.Contains(TEXT("Post"));
		K.Col = (bSoft || K.Height < 40.f || K.FootX * K.FootY < 1500.f) ? ECol::None : ECol::Box;
		K.Shadow = Big > 150.f ? EShadow::Yes : Big > 60.f ? EShadow::Near : EShadow::No;
		K.Slope = (Name.StartsWith(TEXT("Rock")) || Name.StartsWith(TEXT("Boulder")) || Name.StartsWith(TEXT("Log")) || bSoft || Name.StartsWith(TEXT("Stump")) || Name.StartsWith(TEXT("Flower"))) ? 30.f : 20.f;
		K.bLight = Name.Contains(TEXT("LampPost")) || Name.Contains(TEXT("Lantern")) || Name.Contains(TEXT("CandleCage")) || Name == TEXT("WellVillage");
		K.bFromCsv = false;
		UE_LOG(LogTemp, Warning, TEXT("JEEBFILL NODATA %s: not in FillKinds.csv, sized from its mesh %.0f x %.0f x %.0f cm (test only)"), *Name, K.FootX, K.FootY, K.Height);
		return Guessed.Add(Name, MakeUnique<FKind>(K)).Get();
	}

	// ------------------------------------------------------------------ placing
	enum class EWhy : uint8 { Ok, Road, Water, Build, Keep, Game, Tree, Prop, Slope, Missing, Plot, Goods };
	static const TCHAR* WhyName(EWhy W)
	{
		switch (W) { case EWhy::Road: return TEXT("road"); case EWhy::Water: return TEXT("water"); case EWhy::Build: return TEXT("building"); case EWhy::Keep: return TEXT("keepclear");
		case EWhy::Game: return TEXT("solid"); case EWhy::Tree: return TEXT("treespot"); case EWhy::Prop: return TEXT("prop"); case EWhy::Slope: return TEXT("slope"); case EWhy::Missing: return TEXT("missing"); case EWhy::Plot: return TEXT("outsideplot"); case EWhy::Goods: return TEXT("goods"); default: return TEXT("ok"); }
	}
	struct FTry { const FKind* Kind; FTransform Xf; FShape Foot; bool bGround, bSolid, bWalkBlock, bDrop; bool bSnug = false; };

	// A solid piece's collision footprint in plan (the box BuildComponents gives it): its footprint box, or for round and capsule kinds
	// a square 85% of its width, here the circle inside that square
	static FShape SolidFoot(const FKind& K, const FShape& Foot, float Scale, float Yaw)
	{
		if (K.Shape == EShape::Circle || K.Col == ECol::Capsule)
		{
			const float Side = (K.Shape == EShape::Box ? FMath::Min(K.FootX, K.FootY) : K.FootX) * 0.85f * Scale;
			return FShape::Circle(Foot.C, Side * 0.5f);
		}
		return FShape::Box(Foot.C, FVector2D(K.FootX, K.FootY) * 0.5f * Scale, Yaw);
	}
	FShape FootOf(const FKind& K, const FVector2D& At, float Yaw, float Scale) const
	{
		const FShape Fr = FShape::Box(At, FVector2D(1.f, 1.f), Yaw);
		const FVector2D Mid = Fr.World(FVector2D(K.FootCX, K.FootCY) * Scale);
		if (K.Shape == EShape::Circle) return FShape::Circle(Mid, 0.5f * K.FootX * Scale);
		return FShape::Box(Mid, FVector2D(K.FootX, K.FootY) * 0.5f * Scale, Yaw);
	}
	// Does a footprint reach into a building's walls (OakHill.h's wall boxes, 2 cm of slack)? (the wall-backed sets pulled onto
	// the wall: checked against the walls themselves, not the grid's 25 cm building cells)
	bool BodyHits(const FShape& F, float /*Z*/) const
	{
		if (!OakOn() || F.Type == 2) return false;
		const FBox2D B = F.Bounds();
		for (const JeebOakHill::FWallBox& Wb : JeebOakHill::WallBoxes())
		{
			if (!Wb.bWalls) continue;
			FShape Bx = FShape::Box(Wb.Mid, Wb.Half - FVector2D(2.f, 2.f), 0.f); Bx.Cs = Wb.Cs; Bx.Sn = Wb.Sn;
			if (!Bx.Bounds().Intersect(B)) continue;
			if (Touch(F, Bx, 0.f)) return true;
		}
		return false;
	}
	// A wall-backed set (FSet::bWall) is pulled back until its back is a few cm off the real wall face behind it: the building's own
	// collision, traced at three spots along the set and two heights (a hung set: across its hung pieces), the nearest face wins. Up to
	// 1.5 m back, or out if it already reaches into the wall; nothing behind (a fence, open ground): it stays. (Owner 2026-11-16: "move
	// the barrels closer to the building's wall"; the banners flat on the wall.) Every machine has the same buildings when the fill runs.
	bool bSnugOk = true;   // (false while a plot dresses its fences and rows: never pulled through a fence onto the house behind it)
	int32 SnugCount = 0, SnugMiss = 0, SnugTrace = 0, SnugBox = 0, SnugCells = 0;   // (lines that found the wall by the scene, the wall box, the grid)
	bool Snug(const FSet& S, FVector2D& At, float Yaw)
	{
		if (!S.bWall || !bSnugOk) return false;
		const float Rad = FMath::DegreesToRadians(Yaw);
		const FVector2D Fwd(-FMath::Sin(Rad), FMath::Cos(Rad)), Side(FMath::Cos(Rad), FMath::Sin(Rad));
		float Back = -1.0e9f, Wid = 0.f;
		bool bHung = false;
		TArray<float> Hs;
		for (const FPiece& P : S.Pieces)
		{
			const FKind* K = KindOf(P.Kind);
			if (!K || P.bStack) continue;
			const bool bH = P.bHang || K->bHang;
			const float Py = FMath::DegreesToRadians(P.Yaw);
			const float ExtY = 0.5f * P.Scale * (FMath::Abs(FMath::Sin(Py)) * K->FootX + FMath::Abs(FMath::Cos(Py)) * K->FootY);
			Back = FMath::Max(Back, -P.Pos.Y + ExtY);
			Wid = FMath::Max(Wid, FMath::Abs(P.Pos.X));
			if (bH) { bHung = true; Hs.Add(P.Pos.Z - 40.f); Hs.Add(P.Pos.Z - 120.f); }
		}
		if (Back < -1.0e8f) return false;
		if (!bHung) { Hs.Add(30.f); Hs.Add(75.f); }
		const float G = JeebTerrain::HeightAt(At.X, At.Y);
		const float Lat = FMath::Min(Wid * 0.8f, 120.f);
		const float Far = bHung ? 250.f : 150.f;   // (how far back a wall is looked for: a hung banner may have been set out past a porch or steps)
		const float Reach = Back + Far;
		float Gap = 1.0e9f;
		int32 Hits = 0, Misses = 0;   // (traces that met a wall body, and bodies near that the trace missed)
		const float Lats[3] = { -Lat, 0.f, Lat };
		for (const float L : Lats)
		{
			if (Lat < 1.f && L != 0.f) continue;
			for (const float H : Hs)
			{
				const FVector2D P0 = At + Side * L + Fwd * 40.f, P1 = At + Side * L - Fwd * Reach;
				const FVector A3(P0.X, P0.Y, G + H), B3(P1.X, P1.Y, G + H);
				// where the wall is behind this line, cm from P0: the scene's own collision when it answers (a big static mesh: a building),
				// else the building's wall box from OakHill.h (its walls' real extents; 8 cm allowed for the plinth), unless the grid's
				// building cells (porches, footprints) start well before it. (Body traces and overlaps on the instanced buildings return
				// nothing at fill time.)
				float WallD = -1.f;
				{
					FHitResult Hit;
					FCollisionQueryParams Qp(SCENE_QUERY_STAT(JeebFillSnug), true, Owner);
					if (World && World->LineTraceSingleByObjectType(Hit, A3, B3, FCollisionObjectQueryParams(ECC_WorldStatic), Qp) && !Hit.bStartPenetrating
						&& Hit.GetComponent() && Hit.GetComponent()->Bounds.BoxExtent.Size2D() > 300.f && FMath::Abs(Hit.ImpactNormal.Z) < 0.5f)   // (a wall, not rising ground)
					{ WallD = Hit.Distance; ++SnugTrace; }
				}
				if (WallD < 0.f && OakOn())
				{
					const FVector2D Dir = -Fwd;
					const float Len = 40.f + Reach;
					float DBox = -1.f;
					for (const JeebOakHill::FWallBox& Wb : JeebOakHill::WallBoxes())
					{
						if (!Wb.bWalls) continue;
						FShape Bx = FShape::Box(Wb.Mid, Wb.Half, 0.f); Bx.Cs = Wb.Cs; Bx.Sn = Wb.Sn;
						const FVector2D O = Bx.Local(P0), V = Bx.Local(P0 + Dir) - O;
						float T0 = 0.f, T1 = Len;
						bool bOk = true;
						for (int32 a = 0; a < 2 && bOk; ++a)
						{
							const float o = a ? O.Y : O.X, v = a ? V.Y : V.X, h = a ? Bx.H.Y : Bx.H.X;
							if (FMath::Abs(v) < 1.0e-6f) { if (FMath::Abs(o) > h) bOk = false; continue; }
							float Ta = (-h - o) / v, Tb = (h - o) / v;
							if (Ta > Tb) Swap(Ta, Tb);
							T0 = FMath::Max(T0, Ta); T1 = FMath::Min(T1, Tb);
							if (T0 > T1) bOk = false;
						}
						if (bOk && T0 > 0.f && (DBox < 0.f || T0 < DBox)) DBox = T0;
					}
					float DGrid = -1.f;
					for (float T = 0.f; T <= Len; T += 5.f)
					{
						const FVector2D Q = P0 + Dir * T;
						if (Grid.At(FGrid::G(Q.X), FGrid::G(Q.Y)) & F_BUILD) { DGrid = T; break; }
					}
					// (a hung set ignores the grid: its building cells are the footprint at the ground, steps and porch floors, and a
					// banner 2 m up stopped at the town hall's steps, a metre out from the wall: the owner's floating banners, 2026-11-18)
					if (DBox > 0.f && (bHung || DGrid < 0.f || DGrid > DBox - 35.f)) { WallD = DBox - 8.f; ++SnugBox; }
					else if (DGrid > 0.f) { WallD = DGrid - 5.f; ++SnugCells; }
				}
				if (WallD >= 0.f) { ++Hits; Gap = FMath::Min(Gap, WallD - 40.f - Back); }
				else ++Misses;
			}
		}
		if (Gap > Far || Gap < -30.f)
		{
			if (SnugMiss++ < 8) UE_LOG(LogTemp, Log, TEXT("JEEBFILL SNUG %s at %.0f %.0f: stays (gap %.0f cm, traces that met a wall %d, missed %d)"), *S.Name, At.X, At.Y, Gap > 1.0e8f ? -1.f : Gap, Hits, Misses);
			return false;
		}
		const float Want = bHung ? 1.f : 3.f;   // (the back this far off the face)
		At -= Fwd * (Gap - Want);
		++SnugCount;
		return true;
	}
	// bHand (FillOakHill rows, laid out and checked by hand): 5 cm from buildings, not 30 (doorstep sets stand 6 cm off the door wall),
	// and a row's own pieces are not tested against each other (fence, wall and hedge runs join end to end)
	EWhy Fits(const FTry& T, const TArray<FTry>& Pending, bool bHand)
	{
		const bool bPost = T.Kind->Height > 150.f && FMath::Max(T.Kind->FootX, T.Kind->FootY) < 60.f;
		if (Grid.Hits(T.Foot, bPost ? 20.f : 40.f, F_ROAD)) return EWhy::Road;
		uint16 Mask = bHand ? uint16(NoProp & ~F_BUILD) : NoProp;
		if (T.bSnug)
		{
			// (a wall-backed set pulled onto the wall: the wall's own collision decides, and the low decor keeps get 5 cm, not 30)
			Mask &= ~uint16(F_BUILD | F_SOFT | F_GAME);
			if (BodyHits(T.Foot, T.Xf.GetLocation().Z)) return EWhy::Build;
			if (Grid.Hits(T.Foot, 5.f, F_SOFT | F_GAME)) return EWhy::Game;   // (a building's own steps, posts and downpipes are small solids: 5 cm from them too)
		}
		else if (bHand && Grid.Hits(T.Foot, 5.f, F_BUILD)) return EWhy::Build;
		if (Grid.Hits(T.Foot, 30.f, Mask))
		{
			// (which kind of blocker, for the logs)
			if (Grid.Hits(T.Foot, 30.f, F_WATER)) return EWhy::Water;
			if (!bHand && Grid.Hits(T.Foot, 30.f, F_BUILD)) return EWhy::Build;
			if (Grid.Hits(T.Foot, 30.f, F_KEEP)) return EWhy::Keep;
			if (Grid.Hits(T.Foot, 30.f, F_TREE)) return EWhy::Tree;
			return EWhy::Game;
		}
		const FBox2D B = T.Foot.Bounds(10.f);
		for (int32 Hx = FMath::FloorToInt(B.Min.X / 400.f); Hx <= FMath::FloorToInt(B.Max.X / 400.f); ++Hx)
			for (int32 Hy = FMath::FloorToInt(B.Min.Y / 400.f); Hy <= FMath::FloorToInt(B.Max.Y / 400.f); ++Hy)
				if (const TArray<int32>* L = PieceHash.Find(FIntPoint(Hx, Hy)))
					for (int32 Pi : *L) if (Touch(T.Foot, Placed[Pi].Foot, 10.f)) return EWhy::Prop;
		if (!bHand) for (const FTry& O : Pending) if (O.bGround && Touch(T.Foot, O.Foot, 10.f)) return EWhy::Prop;
		return EWhy::Ok;
	}
	// The ground under a footprint: its lowest corner (so no corner floats), and the slope across it
	float GroundUnder(const FShape& F, float& OutSlopeDeg) const
	{
		FVector2D Pts[5] = { F.C, F.C, F.C, F.C, F.C };
		if (F.Type == 0) { Pts[1] = F.World(FVector2D(F.H.X, F.H.Y)); Pts[2] = F.World(FVector2D(-F.H.X, F.H.Y)); Pts[3] = F.World(FVector2D(F.H.X, -F.H.Y)); Pts[4] = F.World(FVector2D(-F.H.X, -F.H.Y)); }
		else { Pts[1] = F.C + FVector2D(F.R, 0.f); Pts[2] = F.C - FVector2D(F.R, 0.f); Pts[3] = F.C + FVector2D(0.f, F.R); Pts[4] = F.C - FVector2D(0.f, F.R); }
		float Lo = 1.0e9f, Hi = -1.0e9f;
		float Zs[5]; int32 n = 0;
		for (const FVector2D& P : Pts) { const float Z = JeebTerrain::DrawnHeight(P.X, P.Y); Zs[n++] = Z; Lo = FMath::Min(Lo, Z); Hi = FMath::Max(Hi, Z); }
		// (the steepest rise between any two of the five spots: the ground's own slope, whichever way the piece is turned)
		float Grad = 0.f;
		for (int32 a = 0; a < 5; ++a) for (int32 b = a + 1; b < 5; ++b) { const float Dd = FVector2D::Distance(Pts[a], Pts[b]); if (Dd > 10.f) Grad = FMath::Max(Grad, FMath::Abs(Zs[a] - Zs[b]) / Dd); }
		OutSlopeDeg = FMath::RadiansToDegrees(FMath::Atan(Grad));
		return Lo;
	}

	// One set (or a single kind) at At, turned Yaw. bVar: its drop/alt/turn flags apply (seeded). bAll: every piece that cannot be
	// dropped must fit, or the whole set is left out (rules); otherwise each piece that fits is placed (hand-placed rows).
	int32 PlaceSet(const FString& SetName, const FVector2D& At, float Yaw, uint32 Seed, bool bVar, bool bAll, int32 Zone, FStats& St, FString* OutWhy = nullptr)
	{
		const FSet* S = D.Sets.Find(SetName);
		FSet Single;
		if (!S)
		{
			if (!D.Kinds.Contains(KindName(SetName)) && !KindOf(KindName(SetName))) { St.Skipped.FindOrAdd(TEXT("noset"))++; if (OutWhy) *OutWhy = TEXT("no such set or kind"); return 0; }
			Single.Name = SetName; FPiece P; P.Kind = KindName(SetName); Single.Pieces.Add(P);
			S = &Single;
		}
		FRandomStream V(int32(Seed & 0x7fffffff));
		FVector2D At2 = At;
		const bool bSnug = Snug(*S, At2, Yaw);
		const FShape Fr = FShape::Box(At2, FVector2D(1.f, 1.f), Yaw);
		TArray<FTry> Tries;
		for (const FPiece& P : S->Pieces)
		{
			const float DropRoll = V.FRand(), AltRoll = V.FRand(), TurnRoll = V.FRandRange(-1.f, 1.f);   // (always drawn: the same draws whatever is skipped)
			if (bVar && P.bDrop && DropRoll < 0.25f) continue;
			FString KName = P.Kind;
			if (bVar && P.Alts.Num() > 0) { const int32 k = FMath::Min(int32(AltRoll * (P.Alts.Num() + 1)), P.Alts.Num()); if (k > 0) KName = P.Alts[k - 1]; }
			const FKind* K = KindOf(KName);
			if (!K) { St.Skipped.FindOrAdd(TEXT("missing"))++; if (!P.bDrop && bAll) { if (OutWhy) *OutWhy = TEXT("missing ") + KName; return 0; } continue; }
			// (Placement v2: wares for sale only at the market, a shop front or the square's market yard)
			if (K->bGoods && !GoodsOk(Zone)) { St.Skipped.FindOrAdd(TEXT("goods"))++; if (!P.bDrop && bAll) { if (OutWhy) *OutWhy = TEXT("goods ") + KName; return 0; } if (OutWhy) *OutWhy += FString::Printf(TEXT("%s%s: goods"), OutWhy->IsEmpty() ? TEXT("") : TEXT(", "), *KName); continue; }
			const float PYaw = Yaw + P.Yaw + (bVar ? TurnRoll * P.Turn : 0.f);
			const FVector2D Pw = Fr.World(FVector2D(P.Pos.X, P.Pos.Y));
			FTry T;
			T.Kind = K;
			T.bGround = !P.bStack && !P.bHang && !K->bHang;
			T.bSolid = K->Col != ECol::None && !P.bHang && !K->bHang;
			T.bWalkBlock = T.bSolid && T.bGround && K->Height * P.Scale > 45.f;
			T.bDrop = P.bDrop;
			T.bSnug = bSnug;
			T.Foot = FootOf(*K, Pw, PYaw, P.Scale);
			float Slope = 0.f;
			const float Gz = GroundUnder(T.Foot, Slope);
			const float Sink = K->Height * P.Scale < 60.f ? 2.f : 0.f;
			T.Xf = FTransform(FRotator(0.f, PYaw, 0.f), FVector(Pw.X, Pw.Y, Gz - Sink + P.Pos.Z), FVector(P.Scale));
			// (2026-11-21, owner: "fix these underground flower pots": a low, long piece on a slope (raised beds, tulip beds, flower
			// boxes) stood at its lowest corner, so its uphill end was buried. A low ground piece (under 80 cm) a metre or more long
			// now leans with the ground instead: the ground's plane across its footprint, from the drawn ground at its four edge middles)
			if (T.bGround && !P.bStack && T.Foot.Type == 0 && K->Height * P.Scale <= 80.f && FMath::Max(T.Foot.H.X, T.Foot.H.Y) >= 50.f && Slope > 1.f
#if !UE_BUILD_SHIPPING
				&& !FParse::Param(FCommandLine::Get(), TEXT("FillNoLean"))   // (the old way, for comparing; never on a server)
#endif
				)
			{
				const FShape& F = T.Foot;
				auto Gd = [&](float Lx, float Ly) { const FVector2D Q = F.World(FVector2D(Lx, Ly)); return JeebTerrain::DrawnHeight(Q.X, Q.Y); };
				const float Gx = (Gd(F.H.X, 0.f) - Gd(-F.H.X, 0.f)) / (2.f * F.H.X), Gy = (Gd(0.f, F.H.Y) - Gd(0.f, -F.H.Y)) / (2.f * F.H.Y);
				const float Hc = 0.25f * (Gd(F.H.X, 0.f) + Gd(-F.H.X, 0.f) + Gd(0.f, F.H.Y) + Gd(0.f, -F.H.Y));
				// (the plane's lowest point under the frame against the ground's: it never hangs over a dip by more than the sink)
				float Under = 0.f;
				for (float a = -1.f; a <= 1.01f; a += 0.5f) for (float b = -1.f; b <= 1.01f; b += 1.f)
				{
					Under = FMath::Max(Under, Hc + Gx * a * F.H.X + Gy * b * F.H.Y - Gd(a * F.H.X, b * F.H.Y));
					Under = FMath::Max(Under, Hc + Gx * b * F.H.X + Gy * a * F.H.Y - Gd(b * F.H.X, a * F.H.Y));
				}
				const FVector2D Lp = F.Local(Pw);
				FRotator R(FMath::RadiansToDegrees(FMath::Atan(Gx)), PYaw, FMath::RadiansToDegrees(FMath::Atan(Gy)));
				// (the sign of pitch and roll: whichever lifts the piece's own +X and +Y the way the ground rises)
				if (FMath::Sign(FRotationMatrix(R).GetScaledAxis(EAxis::X).Z) != FMath::Sign(Gx) && Gx != 0.f) R.Pitch = -R.Pitch;
				if (FMath::Sign(FRotationMatrix(R).GetScaledAxis(EAxis::Y).Z) != FMath::Sign(Gy) && Gy != 0.f) R.Roll = -R.Roll;
				const float Zp = Hc + Gx * Lp.X + Gy * Lp.Y - FMath::Min(Under, 6.f);
				T.Xf = FTransform(R, FVector(Pw.X, Pw.Y, Zp - Sink + P.Pos.Z), FVector(P.Scale));
			}
			// (a lantern never stands on the ground (owner 2026-11-16): on a barrel, crate or post, or hung; and a stacked piece only
			// where the piece it stands on was placed: its crate dropped or refused, it is left out too, never left floating)
			if (T.bGround && K->bLight && K->Name.StartsWith(TEXT("Lantern")))
			{
				St.Skipped.FindOrAdd(TEXT("lantern on ground"))++;
				continue;
			}
			if (P.bStack)
			{
				// (2026-11-17, owner: "this lantern is just floating here": a stacked piece rests on the top of the piece of its own set
				// under it, 1 cm into it, not at its own ground height plus the row's Z, which on a slope or a sunk base left a gap)
				const FVector Pz = T.Xf.GetLocation();
				float BestTop = 0.f, BestGap = 35.f;
				bool bOn = false;
				for (const FTry& B : Tries)
				{
					if (!B.bGround || !B.Foot.Contains(FVector2D(Pz.X, Pz.Y), 5.f)) continue;
					const float Top = B.Xf.GetLocation().Z + B.Kind->Height * B.Xf.GetScale3D().Z;
					if (FMath::Abs(Top - Pz.Z) < BestGap) { BestGap = FMath::Abs(Top - Pz.Z); BestTop = Top; bOn = true; }
				}
				if (bOn) T.Xf.SetLocation(FVector(Pz.X, Pz.Y, BestTop - 1.f));
				if (!bOn)
				{
					St.Skipped.FindOrAdd(TEXT("nobase"))++;
					if (OutWhy) *OutWhy += FString::Printf(TEXT("%s%s: nobase"), OutWhy->IsEmpty() ? TEXT("") : TEXT(", "), *KName);
					continue;
				}
			}
			if (T.bGround)
			{
				EWhy W = Slope > K->Slope ? EWhy::Slope : Fits(T, Tries, !bAll);
				if (W == EWhy::Ok && ClipPoly && !FootInPoly(T.Foot, *ClipPoly, 15.f)) W = EWhy::Plot;
				if (W != EWhy::Ok)
				{
					St.Skipped.FindOrAdd(WhyName(W))++;
					if (bAll && !P.bDrop) { if (OutWhy) *OutWhy = FString::Printf(TEXT("%s: %s"), *KName, WhyName(W)); return 0; }
					if (OutWhy) *OutWhy += FString::Printf(TEXT("%s%s: %s"), OutWhy->IsEmpty() ? TEXT("") : TEXT(", "), *KName, W == EWhy::Slope ? *FString::Printf(TEXT("slope %.0f>%.0f"), Slope, K->Slope) : WhyName(W));
					continue;
				}
			}
			Tries.Add(T);
		}
		if (Tries.Num() == 0) return 0;
		const int32 Group = bAll ? -1 : NextGroup++;
		for (const FTry& T : Tries) Commit(T, Zone, S->Name, St, Group);
		++St.Sets;
		return Tries.Num();
	}
	int32 NextGroup = 0;
	void Commit(const FTry& T, int32 Zone, const FString& SetName, FStats& St, int32 Group)
	{
		const int32 Pi = Placed.Add({ T.Kind, T.Xf, T.Foot, T.bGround, T.bSolid, T.bWalkBlock, T.bSnug, Zone, SetName, Group, CurUse });
		if (T.bGround)
		{
			Grid.Mark(T.Foot, F_PROP | (T.bWalkBlock ? F_PSOLID : 0));
			const FBox2D B = T.Foot.Bounds();
			for (int32 Hx = FMath::FloorToInt(B.Min.X / 400.f); Hx <= FMath::FloorToInt(B.Max.X / 400.f); ++Hx)
				for (int32 Hy = FMath::FloorToInt(B.Min.Y / 400.f); Hy <= FMath::FloorToInt(B.Max.Y / 400.f); ++Hy) PieceHash.FindOrAdd(FIntPoint(Hx, Hy)).Add(Pi);
			// the grass and fallen leaves give way under a solid piece over 1 m^2, inside its collision footprint only (no larger: the
			// same box, or the round kinds' capsule, that BuildComponents makes), with a soft rim
			if (T.bSolid && T.Kind->Col != ECol::Mesh)
			{
				const FVector S3 = T.Xf.GetScale3D();
				const FShape Col = FMath::IsNearlyEqual(S3.X, S3.Y, 0.001f) ? SolidFoot(*T.Kind, T.Foot, S3.X, T.Xf.Rotator().Yaw) : T.Foot;   // (a stretched run panel: its own box)
				if (Col.Area() > 10000.f) AddCover(Col);
			}
		}
		const FVector L = T.Xf.GetLocation();
		uint32& H = St.Hash;
		const uint32 Kc = FCrc::StrCrc32(*T.Kind->Name.ToLower());
		H ^= Kc; H *= 16777619u;
		HashIn(H, L.X); HashIn(H, L.Y); HashIn(H, L.Z); HashIn(H, T.Xf.Rotator().Yaw); HashIn(H, T.Xf.GetScale3D().X); HashIn(H, T.bSolid ? 1.f : 0.f);
		if (!FMath::IsNearlyZero(T.Xf.Rotator().Pitch, 0.01f)) HashIn(H, T.Xf.Rotator().Pitch);
		++St.Pieces; if (T.bSolid) ++St.Solid;
		St.Kinds.Add(T.Kind->Name);
	}

	// How far a set's ground pieces reach behind its origin (its -Y), cm: a wall-facing set is moved out by this so its back clears the wall
	TMap<FString, float> BackCache;
	float SetBack(const FString& SetName)
	{
		if (const float* Got = BackCache.Find(SetName)) return *Got;
		float Back = 0.f;
		auto Reach = [&](const FString& KName, const FPiece& P)
		{
			if (const FKind* K = KindOf(KName)) Back = FMath::Max(Back, -P.Pos.Y + 0.5f * FMath::Max(K->FootX, K->FootY) * P.Scale);
		};
		if (const FSet* S = D.Sets.Find(SetName))
		{
			for (const FPiece& P : S->Pieces)
			{
				if (P.bStack || P.bHang) continue;
				Reach(P.Kind, P);
				for (const FString& A : P.Alts) Reach(A, P);
			}
		}
		else Reach(KindName(SetName), FPiece());
		BackCache.Add(SetName, Back);
		return Back;
	}

	// The way a set's front (+Y) turns: toward the nearest road, or away from the nearest wall (onto which it is moved, its back 35 cm off it)
	bool Facing(EFacing F, FVector2D& At, float& Yaw, float Back = 0.f)   // (Back: how far the set reaches behind its origin, cm)
	{
		auto YawFor = [](const FVector2D& Dir) { return FMath::RadiansToDegrees(FMath::Atan2(-Dir.X, Dir.Y)); };   // (R(yaw) * (0,1) = Dir)
		if (F == EFacing::Road)
		{
			FVector2D To;
			if (OakOn() && JeebOakHill::InBox(At.X, At.Y))
			{
				FVector2D NearEN; JeebOakHill::RoadClearanceM(JeebOakHill::EastM(At.Y), JeebOakHill::NorthM(At.X), &NearEN);
				To = FVector2D(JeebOakHill::PosX + NearEN.Y * 100.f, JeebOakHill::PosY + NearEN.X * 100.f);
			}
			else To = JeebTerrain::NearestPathPoint(At.X, At.Y);
			if (FVector2D::Distance(To, At) < 1.f) return true;
			Yaw = YawFor((To - At).GetSafeNormal());
			return true;
		}
		if (F == EFacing::Wall)
		{
			// the nearest wall: Oak Hill's buildings' wall boxes, the huts, the village wall from inside
			float Best = 800.f; FVector2D BestPt, BestOut;
			auto TryBox = [&](const FShape& Bx)
			{
				const FVector2D L = Bx.Local(At);
				FVector2D Q(FMath::Clamp(L.X, -Bx.H.X, Bx.H.X), FMath::Clamp(L.Y, -Bx.H.Y, Bx.H.Y));
				FVector2D N;
				if (FMath::Abs(L.X) <= Bx.H.X && FMath::Abs(L.Y) <= Bx.H.Y) return;   // (inside)
				if (FMath::Abs(L.X) - Bx.H.X > FMath::Abs(L.Y) - Bx.H.Y) { N = FVector2D(FMath::Sign(L.X), 0.f); Q.X = Bx.H.X * N.X; }
				else { N = FVector2D(0.f, FMath::Sign(L.Y)); Q.Y = Bx.H.Y * N.Y; }
				const float Dd = FVector2D::Distance(L, Q);
				if (Dd < Best) { Best = Dd; BestPt = Bx.World(Q); BestOut = Bx.World(Q + N) - BestPt; }
			};
			if (OakOn())
				for (const JeebOakHill::FWallBox& W : JeebOakHill::WallBoxes()) if (W.bWalls) { FShape Bx = FShape::Box(W.Mid, W.Half, 0.f); Bx.Cs = W.Cs; Bx.Sn = W.Sn; TryBox(Bx); }
			for (TActorIterator<ACottage> It(World); It; ++It) TryBox(FShape::Box(FVector2D(It->GetActorLocation()), FVector2D(360.f, 260.f) * It->GetActorScale3D().X, It->GetActorRotation().Yaw));
			{
				const FVector2D VC = JeebTerrain::VillageCenter();
				const float Hh = JeebTerrain::VillageHalf() - 320.f;   // (inside the walkway's posts)
				const FVector2D L = At - VC;
				if (FMath::Abs(L.X) < Hh && FMath::Abs(L.Y) < Hh)
				{
					const float Dx = Hh - FMath::Abs(L.X), Dy = Hh - FMath::Abs(L.Y);
					if (FMath::Min(Dx, Dy) < Best)
					{
						Best = FMath::Min(Dx, Dy);
						if (Dx < Dy) { BestPt = VC + FVector2D(FMath::Sign(L.X) * Hh, L.Y); BestOut = FVector2D(-FMath::Sign(L.X), 0.f); }
						else { BestPt = VC + FVector2D(L.X, FMath::Sign(L.Y) * Hh); BestOut = FVector2D(0.f, -FMath::Sign(L.Y)); }
					}
				}
			}
			if (Best >= 800.f) return false;
			At = BestPt + BestOut * (35.f + Back);   // (its back 35 cm off the wall: clear of the 30 cm rule margin)
			Yaw = YawFor(BestOut);
			return true;
		}
		return true;
	}

	bool ZoneWanted(const FString& Name) const { return OnlyZone.IsEmpty() || OnlyZone.Equals(Name, ESearchCase::IgnoreCase); }

	// The hand-placed rows: Oak Hill's, then the square's (Site Square, stats "SquareRows"), before any rule zone
	void PlaceOakHill()
	{
		for (int32 Pass = 0; Pass < 3; ++Pass)
		{
			const bool bSq = Pass == 1, bSteele = Pass == 2;   // (Pass 2: Steele Creek's rows, Site "Steele", 2026-11-18)
			const TCHAR* Key = bSteele ? TEXT("SteeleRows") : bSq ? TEXT("SquareRows") : TEXT("OakHill");
			if ((Pass == 0 && !OakOn()) || (bSteele && !JeebSteele::Enabled()) || !ZoneWanted(Key)) continue;
			const double T0 = FPlatformTime::Seconds();
			int32 Failed = 0, Rows = 0;
			for (const FOakRow& R : D.Oak)
			{
				if ((R.bSteele ? 2 : R.bSquare ? 1 : 0) != Pass) continue;
				FStats& St = Stats.FindOrAdd(Key);
				++Rows;
				FVector2D At = bSteele ? JeebSteele::ToWorld(R.E, R.N) : bSq ? FVector2D(R.N * 100.f, R.E * 100.f) : FVector2D(JeebOakHill::PosX + R.N * 100.f, JeebOakHill::PosY + R.E * 100.f);
				float RowYaw = R.Yaw;
				if (R.bRel && Pass == 0)
				{
					// (Site "Plot", 2026-11-18: the row belongs to a building or an open plot and moves with it: E, N metres in its own
					// frame, a building's model X, Y (its door on +Y), an open plot's centre unturned; Yaw relative to the building)
					const TArray<JeebOakHill::FPlot>& Ps = JeebOakHill::Plots();
					if (!Ps.IsValidIndex(R.Plot)) { ++Failed; UE_LOG(LogTemp, Warning, TEXT("JEEBFILL OAKROW line %d %s: no plot %d"), R.Line, *R.Set, R.Plot); continue; }
					const JeebOakHill::FPlot& Pl = Ps[R.Plot];
					const float By = Pl.Kind == JeebOakHill::EPlot::Building ? JeebOakHill::BuildingYaw(Pl, JeebOakHill::FindModel(Pl.Model)) : 0.f;
					const float Cs = FMath::Cos(FMath::DegreesToRadians(By)), Sn = FMath::Sin(FMath::DegreesToRadians(By));
					At = JeebOakHill::FromPx(Pl.Px, Pl.Py) + FVector2D(R.E * Cs - R.N * Sn, R.E * Sn + R.N * Cs) * 100.f;
					RowYaw = FRotator::NormalizeAxis(R.Yaw + By);
				}
				if (Pass != 1 && (R.Set.StartsWith(TEXT("Lamp")) || R.Set.StartsWith(TEXT("Junction"))))
				{
					// (a lamp or a signpost stands at the path's edge, 0.7 m off it: the nearest road cell within 8 m)
					FVector2D Best(0.f, 0.f); float BestD = 1.0e9f;
					const int32 Cx = FGrid::G(At.X), Cy = FGrid::G(At.Y);
					for (int32 dx = -32; dx <= 32; ++dx)
						for (int32 dy = -32; dy <= 32; ++dy)
							if (Grid.At(Cx + dx, Cy + dy) & F_ROAD)
							{
								const FVector2D Q((Cx + dx + 0.5f) * Cell, (Cy + dy + 0.5f) * Cell);
								const float Dd = float(FVector2D::DistSquared(Q, At));
								if (Dd < BestD) { BestD = Dd; Best = Q; }
							}
					if (BestD < 1.0e9f && BestD > 1.f)
					{
						const FVector2D Away = (At - Best).GetSafeNormal();
						At = Best + Away * (Cell * 0.5f + 70.f);
						if (R.Set.StartsWith(TEXT("Lamp"))) RowYaw = YawOf(-Away);
					}
				}
				FString Why;
				const int32 Before = St.Pieces;
				const int32 N = PlaceSet(R.Set, At, RowYaw, HashU(uint32(R.Seed) * 2654435761u + 17u), R.bVar, false, ZoneAt(At.X, At.Y), St, &Why);
				if (N == 0 || !Why.IsEmpty())
				{
					++Failed;
					UE_LOG(LogTemp, Log, TEXT("JEEBFILL OAKROW line %d %s at %s E %.1f N %.1f: %d of its pieces placed%s%s"), R.Line, *R.Set, bSteele ? TEXT("Steele Creek") : bSq ? TEXT("square") : TEXT("Oak Hill"), R.E, R.N, St.Pieces - Before, Why.IsEmpty() ? TEXT("") : TEXT("; skipped "), *Why);
				}
			}
			if (FStats* St = Stats.Find(Key)) St->Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
			UE_LOG(LogTemp, Log, TEXT("JEEBFILL %s rows %d, %d with pieces left out (JEEBFILL OAKROW lines)"), Key, Rows, Failed);
		}
	}

	void PlaceZones()
	{
		for (int32 Z = 1; Z < Z_Count; ++Z)
		{
			const FZone* Zr = D.Zones.Find(ZoneNames[Z]);
			if (!Zr || !Zr->bOn || (Zr->Density <= 0.f && Zr->Trees <= 0.f) || !ZoneWanted(ZoneNames[Z])) continue;
			if (IsOak(Z) && !OakOn()) continue;
			const double T0 = FPlatformTime::Seconds();
			FStats& St = Stats.FindOrAdd(ZoneNames[Z]);
			float WSum = 0.f;
			for (const TPair<FString, float>& M : Zr->Mix) WSum += M.Value;
			const FBox2D B = ZoneBounds(Z);
			const uint32 ZSeed = FCrc::StrCrc32(*FString(ZoneNames[Z]).ToLower()) ^ HashU(uint32(Zr->Seed) + 0x9e3779b9u);
			const int32 Cx0 = FMath::FloorToInt(B.Min.X / ChunkCm), Cx1 = FMath::FloorToInt(B.Max.X / ChunkCm);
			const int32 Cy0 = FMath::FloorToInt(B.Min.Y / ChunkCm), Cy1 = FMath::FloorToInt(B.Max.Y / ChunkCm);
			for (int32 Cy = Cy0; Cy <= Cy1; ++Cy)
				for (int32 Cx = Cx0; Cx <= Cx1; ++Cx)
				{
					const FVector2D O(Cx * ChunkCm, Cy * ChunkCm);
					// this 32 m square's open ground in the zone, on a 2 m lattice (fillfix 2026-11-16: spots are drawn from these cells only,
					// not from the whole square, so a small zone such as the pig pen's ring gets its share instead of one set)
					constexpr int32 Lat = 16;
					constexpr float LatCm = ChunkCm / Lat;
					int32 InZone = 0;
					TArray<FVector2D> OpenCells;
					for (int32 a = 0; a < Lat; ++a)
						for (int32 b = 0; b < Lat; ++b)
						{
							const FVector2D P = O + FVector2D((a + 0.5f) * LatCm, (b + 0.5f) * LatCm);
							if (ZoneAt(P.X, P.Y) != Z) continue;
							++InZone;
							if (!(Grid.At(FGrid::G(P.X), FGrid::G(P.Y)) & (NoProp | F_ROAD))) OpenCells.Add(P);
						}
					if (InZone == 0 || OpenCells.Num() == 0) continue;
					const float Area = OpenCells.Num() * (LatCm * LatCm / 10000.f);   // m^2
					FRandomStream R(int32(HashU(uint32(Cx) * 73856093u ^ uint32(Cy) * 19349663u ^ ZSeed) & 0x7fffffff));
					const float WantF = Zr->Density * Area / 100.f, TreesF = Zr->Trees * Area / 100.f;
					const float FracRoll = R.FRand(), TreeFrac = R.FRand();
					const int32 Want = FMath::FloorToInt(WantF) + (FracRoll < FMath::Frac(WantF) ? 1 : 0);
					const int32 WantTrees = FMath::FloorToInt(TreesF) + (TreeFrac < FMath::Frac(TreesF) ? 1 : 0);
					// every number this square uses, drawn before anything is tried (so a skip never shifts the rest)
					const int32 NTry = FMath::Clamp((Want + WantTrees) * 6, 4, 96);
					struct FRoll { float Pick, U, V, Set, Yaw; uint32 Var; };
					TArray<FRoll> Rolls;
					for (int32 t = 0; t < NTry; ++t) Rolls.Add({ R.FRand(), R.FRand(), R.FRand(), R.FRand(), R.FRand(), uint32(R.GetUnsignedInt()) });
					int32 GotTrees = 0, GotSets = 0;
					for (const FRoll& Ro : Rolls)
					{
						if (GotTrees >= WantTrees && GotSets >= Want) break;
						// one of the zone's open cells, anywhere inside it
						FVector2D At = OpenCells[FMath::Min(int32(Ro.Pick * OpenCells.Num()), OpenCells.Num() - 1)] + FVector2D(Ro.U - 0.5f, Ro.V - 0.5f) * LatCm;
						if (ZoneAt(At.X, At.Y) != Z) { St.Skipped.FindOrAdd(TEXT("zone"))++; continue; }
						if (GotTrees < WantTrees)
						{
							// a reserved tree spot: 2 m clear of everything
							const FShape Spot = FShape::Circle(At, 200.f);
							if (Grid.Hits(Spot, 30.f, NoProp | F_ROAD | F_PROP)) { St.Skipped.FindOrAdd(TEXT("treespot"))++; continue; }
							Keep(Spot, FString::Printf(TEXT("tree %s"), ZoneNames[Z]), F_TREE);
							TreeSpots.Add({ At, 200.f, Z == Z_OakFringe || Z == Z_WoodsEdge || Z == Z_Mountain ? TEXT("conifer") : IsOak(Z) ? TEXT("shade") : TEXT("shade") });
							++GotTrees;
							continue;
						}
						if (WSum <= 0.f) break;
						FString SetName;
						{
							float Acc = 0.f; const float Pick = Ro.Set * WSum;
							for (const TPair<FString, float>& M : Zr->Mix) { Acc += M.Value; if (Pick < Acc) { SetName = M.Key; break; } }
							if (SetName.IsEmpty()) SetName = Zr->Mix.Last().Key;
						}
						float Yaw = Ro.Yaw * 360.f;
						if (!Facing(Zr->Facing, At, Yaw, SetBack(SetName))) { St.Skipped.FindOrAdd(TEXT("nowall"))++; continue; }
						if (PlaceSet(SetName, At, Yaw, Ro.Var, true, true, Z, St) > 0) ++GotSets;
						else St.Skipped.FindOrAdd(TEXT("set"))++;
					}
				}
			St.Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
		}
	}

	// ------------------------------------------------------------------ Placement v2: plots, boundary runs, uses (2026-11-16)
	// Every plot belongs to a building or a place with a use (FillPlots.csv). Its edges get continuous fence or wall runs, post to post,
	// panels fitted to the edge's length, one gate toward the road; then its use (FillUses.csv) places its sets against the building's
	// walls, along the inside of its runs, in rows across it or in its middle. Goods only at the market and shop fronts.
	const TArray<FVector2D>* ClipPoly = nullptr;   // (PlaceSet: every ground piece inside this plot)
	FString CurUse;                                // (Commit: the use placing now, for the misplaced check)
	bool GoodsOk(int32 Zone) const
	{
		if (!CurUse.IsEmpty()) return CurUse == TEXT("Market") || CurUse == TEXT("ShopFront");
		return Zone == Z_OakPlaza || Zone == Z_MarketYard;
	}
	static bool InPoly(const TArray<FVector2D>& P, const FVector2D& Q)
	{
		bool In = false;
		for (int32 i = 0, j = P.Num() - 1; i < P.Num(); j = i++)
			if (((P[i].Y > Q.Y) != (P[j].Y > Q.Y)) && (Q.X < (P[j].X - P[i].X) * (Q.Y - P[i].Y) / (P[j].Y - P[i].Y) + P[i].X)) In = !In;
		return In;
	}
	static float SegDist(const FVector2D& A, const FVector2D& B, const FVector2D& Q)
	{
		const FVector2D AB = B - A;
		const float T = FMath::Clamp(FVector2D::DotProduct(Q - A, AB) / FMath::Max(AB.SizeSquared(), 1.f), 0.f, 1.f);
		return float(FVector2D::Distance(Q, A + AB * T));
	}
	static float PolyEdgeDist(const TArray<FVector2D>& P, const FVector2D& Q)
	{
		float Best = 1.0e9f;
		for (int32 i = 0; i < P.Num(); ++i) Best = FMath::Min(Best, SegDist(P[i], P[(i + 1) % P.Num()], Q));
		return Best;
	}
	static bool FootInPoly(const FShape& F, const TArray<FVector2D>& P, float Inset)
	{
		TArray<FVector2D> Pts;
		if (F.Type == 0) { Pts = { F.World(FVector2D(F.H.X, F.H.Y)), F.World(FVector2D(-F.H.X, F.H.Y)), F.World(FVector2D(F.H.X, -F.H.Y)), F.World(FVector2D(-F.H.X, -F.H.Y)) }; }
		else { Pts = { F.C + FVector2D(F.R, 0.f), F.C - FVector2D(F.R, 0.f), F.C + FVector2D(0.f, F.R), F.C - FVector2D(0.f, F.R) }; }
		for (const FVector2D& Q : Pts) if (!InPoly(P, Q) || PolyEdgeDist(P, Q) < Inset) return false;
		return true;
	}
	static float YawOf(const FVector2D& Dir) { return FMath::RadiansToDegrees(FMath::Atan2(-Dir.X, Dir.Y)); }   // (R(yaw) * (0,1) = Dir)

	struct FFamily { const TCHAR* Name; const TCHAR* Panel; const TCHAR* Post; const TCHAR* GatePost; float GateW; };
	static const FFamily* FamilyOf(const FString& B)
	{
		static const FFamily Fs[] = {
			{ TEXT("picket"), TEXT("FencePicket"), TEXT("FencePicketEnd"), TEXT("FencePicketEnd"), 160.f },
			{ TEXT("stick"), TEXT("FenceStick"), TEXT("FenceStickPost"), TEXT("FenceStickPost"), 180.f },
			{ TEXT("stone"), TEXT("WallStone"), TEXT("WallStoneEnd"), TEXT("WallPillarEnd"), 180.f },
			{ TEXT("pillar"), TEXT("WallPillar"), TEXT("WallPillarEnd"), TEXT("WallPillarEnd"), 180.f },
		};
		for (const FFamily& F : Fs) if (B.Equals(F.Name, ESearchCase::IgnoreCase)) return &F;
		return nullptr;
	}
	struct FAnchor { FVector2D P, N; uint8 Type; float Key; };   // (Type: EAnchor; Key: the seeded order)
	struct FPlotB
	{
		const FPlotRow* Row = nullptr;
		FString Name, Use;
		TArray<FVector2D> Poly;            // game cm
		TArray<uint8> Edge;                // per edge Poly[i] -> Poly[i+1]: 0 a run, 1 the building's own wall, 2 open
		bool bBuilding = false;
		FShape Own;                        // the building's walls (a box)
		FVector2D Fwd = FVector2D(1.f, 0.f), Rt = FVector2D(0.f, 1.f);   // the plot's frame: Fwd toward the building's front (or along its first edge)
		int32 GateEdge = -1; FVector2D Gate = FVector2D::ZeroVector, GateOut = FVector2D::ZeroVector; float GateW = 160.f;
		int32 Group = -1, Zone = 0;
		uint32 Seed = 0;
		int32 Panels = 0, Posts = 0, Cuts = 0; float MaxGap = 0.f, RunLen = 0.f;
		TArray<int32> RunPieces;           // Placed indices of its run pieces, in order along each edge
	};
	TArray<FPlotB> PlotsB;
	FStats* PlotStats = nullptr;

	// Grid bits within R of P, leaving out the plot's own building (and its foundation decor within SoftPad of it)
	uint16 BitsNear(const FVector2D& P, float R, const FPlotB& Pl, float SoftPad = 150.f)
	{
		uint16 Or = 0;
		for (int32 Gx = FGrid::G(P.X - R); Gx <= FGrid::G(P.X + R); ++Gx)
			for (int32 Gy = FGrid::G(P.Y - R); Gy <= FGrid::G(P.Y + R); ++Gy)
			{
				const FVector2D C = FGrid::Centre(Gx, Gy);
				if (FVector2D::DistSquared(C, P) > R * R) continue;
				uint16 B = Grid.At(Gx, Gy);
				if (Pl.bBuilding)
				{
					if (Pl.Own.Contains(C, 30.f)) B &= ~uint16(F_BUILD | F_SOFT | F_GAME);
					else if (Pl.Own.Contains(C, SoftPad)) B &= ~uint16(F_SOFT);
				}
				Or |= B;
			}
		return Or;
	}
	FVector2D OakXY(const FVector2D& EN, bool bSquare) const { return bSquare ? FVector2D(EN.Y * 100.f, EN.X * 100.f) : FVector2D(JeebOakHill::PosX + EN.Y * 100.f, JeebOakHill::PosY + EN.X * 100.f); }
	float RoadDistCm(const FVector2D& P) const
	{
		if (OakOn() && JeebOakHill::InBox(P.X, P.Y)) return JeebOakHill::RoadClearanceM(JeebOakHill::EastM(P.Y), JeebOakHill::NorthM(P.X)) * 100.f;
		return JeebTerrain::PathClearance(P.X, P.Y);
	}

	// Is this outline clear? Its runs (every 20 cm) of roads, water, buildings, kept-clear ground, solids, tree spots and other props; its
	// ground (a 50 cm lattice) of roads, water, other buildings and kept-clear ground; and 1 m from every earlier plot. The first spot
	// that is not clear is returned (world cm)
	bool PieceNear(const FVector2D& Q, float R) const
	{
		const FShape C = FShape::Circle(Q, R);
		for (int32 Hx = FMath::FloorToInt((Q.X - R) / 400.f); Hx <= FMath::FloorToInt((Q.X + R) / 400.f); ++Hx)
			for (int32 Hy = FMath::FloorToInt((Q.Y - R) / 400.f); Hy <= FMath::FloorToInt((Q.Y + R) / 400.f); ++Hy)
				if (const TArray<int32>* L = PieceHash.Find(FIntPoint(Hx, Hy)))
					for (int32 Pi : *L) if (Touch(C, Placed[Pi].Foot, 0.f)) return true;
		return false;
	}
	bool OutlineClear(const FPlotB& Pl, FVector2D& Bad, FString& Why)
	{
		const uint16 LineMask = F_ROAD | F_WATER | F_KEEP | F_GAME | F_TREE | F_BUILD | F_PROP | F_SOFT;
		const int32 N = Pl.Poly.Num();
		for (int32 e = 0; e < N; ++e)
		{
			if (Pl.Edge[e] != 0) continue;
			const FVector2D A = Pl.Poly[e], B = Pl.Poly[(e + 1) % N];
			const float Len = float(FVector2D::Distance(A, B));
			for (float t = 0.f; t <= Len; t += 20.f)
			{
				const FVector2D Q = A + (B - A) * (t / FMath::Max(Len, 1.f));
				if ((BitsNear(Q, 40.f, Pl) & LineMask) || (BitsNear(Q, 60.f, Pl) & F_ROAD) || PieceNear(Q, 45.f)) { Bad = Q; Why = FString::Printf(TEXT("edge %d"), e); return false; }
			}
		}
		FBox2D Bx(ForceInit); for (const FVector2D& Q : Pl.Poly) Bx += Q;
		for (float x = Bx.Min.X + 25.f; x < Bx.Max.X; x += 50.f)
			for (float y = Bx.Min.Y + 25.f; y < Bx.Max.Y; y += 50.f)
			{
				const FVector2D Q(x, y);
				if (!InPoly(Pl.Poly, Q) || (Pl.bBuilding && Pl.Own.Contains(Q, 40.f))) continue;
				if (Grid.At(FGrid::G(x), FGrid::G(y)) & (F_ROAD | F_WATER | F_BUILD | F_KEEP)) { Bad = Q; Why = TEXT("ground"); return false; }
			}
		for (const FPlotB& O : PlotsB)
		{
			if (O.Poly.Num() < 3 || &O == &Pl) continue;
			for (int32 e = 0; e < N; ++e)
			{
				const FVector2D A = Pl.Poly[e], B = Pl.Poly[(e + 1) % N];
				const float Len = float(FVector2D::Distance(A, B));
				for (float t = 0.f; t <= Len; t += 25.f)
				{
					const FVector2D Q = A + (B - A) * (t / FMath::Max(Len, 1.f));
					if (InPoly(O.Poly, Q) || PolyEdgeDist(O.Poly, Q) < 100.f) { Bad = Q; Why = TEXT("another plot ") + O.Name; return false; }
				}
			}
			for (const FVector2D& Q : O.Poly) if (InPoly(Pl.Poly, Q)) { Bad = Q; Why = TEXT("another plot ") + O.Name; return false; }
		}
		return true;
	}

	// A building's yard: round its back and sides (Side and Depth the most it may reach), shrunk until clear. A side with no room beside
	// the house becomes a yard behind it only (its run starts at the back wall's corner)
	bool MakeYard(FPlotB& Pl, const FFamily* Fam, FString& Why)
	{
		using namespace JeebOakHill;
		const TArray<FPlot>& Ps = Plots();
		const int32 Bi = Pl.Row->Building;
		if (!OakOn() || Bi < 0 || Bi >= Ps.Num() || Ps[Bi].Kind != EPlot::Building || !Ps[Bi].Model) { Why = TEXT("no such building"); return false; }
		const FModel* M = FindModel(Ps[Bi].Model);
		const FWallBox& W = WallBoxes()[Bi];
		if (!M || !W.bWalls) { Why = TEXT("building has no walls"); return false; }
		FShape Box = FShape::Box(W.Mid, W.Half, 0.f); Box.Cs = W.Cs; Box.Sn = W.Sn;
		Pl.Own = Box; Pl.bBuilding = true;
		const float Yaw = FMath::DegreesToRadians(BuildingYaw(Ps[Bi], M));
		const FVector2D F(-FMath::Sin(Yaw), FMath::Cos(Yaw));   // (the door's way out, as OakKeeps)
		const FVector2D Ax(Box.Cs, Box.Sn), Ay(-Box.Sn, Box.Cs);
		const bool bYFront = FMath::Abs(FVector2D::DotProduct(F, Ay)) >= FMath::Abs(FVector2D::DotProduct(F, Ax));
		const float Hy = bYFront ? W.Half.Y : W.Half.X, Hx = bYFront ? W.Half.X : W.Half.Y;
		Pl.Fwd = F; Pl.Rt = FVector2D(F.Y, -F.X);
		const FVector2D Mid = W.Mid;
		auto ToW = [&](float x, float y) { return Mid + Pl.Rt * x + Pl.Fwd * y; };
		auto Local = [&](const FVector2D& Q) { const FVector2D Dd = Q - Mid; return FVector2D(FVector2D::DotProduct(Dd, Pl.Rt), FVector2D::DotProduct(Dd, Pl.Fwd)); };
		const FKind* PostK = Fam ? KindOf(Fam->Post) : nullptr;
		const float Pw = PostK ? PostK->FootX : 30.f, Pd = PostK ? PostK->FootY : 30.f;
		const float MinSide = 130.f, MinDepth = 250.f;
		float SL = Pl.Row->Side * 100.f, SR = SL, Dp = Pl.Row->Depth * 100.f;
		bool bBackL = false, bBackR = false;
		const float X0 = Hx + 12.f + Pw * 0.5f;   // (a return's end post, 12 cm off the building's front corner)
		const float Yb = -Hy - 12.f - Pw * 0.5f;  // (a behind-only side's first post, 12 cm off the back wall)
		const float Xb = Hx - Pd * 0.5f;         // (... in line with the building's side)
		for (int32 Iter = 0; Iter < 80; ++Iter)
		{
			SL = FMath::Max(SL, MinSide); SR = FMath::Max(SR, MinSide);
			TArray<FVector2D> L;
			// (a side's fence meets the side wall part way back, so the front of the house stays the street's)
			const float Yr = Hy - FMath::Min(Hy * 0.9f, 350.f);
			// (the end post moves out from the wall until it is clear of the building's real outline: a chimney or a buttress)
			auto ClearX = [&](float Sgn)
			{
				float X = X0;
				while (X < X0 + 80.f && Grid.Hits(FShape::Circle(ToW(Sgn * X, Yr), FMath::Max(5.f, Pw * 0.5f - 13.f)), 0.f, F_BUILD)) X += 10.f;
				return X;
			};
			const float XL = ClearX(-1.f), XR = ClearX(1.f);
			if (bBackL) { L.Add(FVector2D(-Xb, Yb)); L.Add(FVector2D(-Xb, -Hy - Dp)); }
			else { L.Add(FVector2D(-XL, Yr)); L.Add(FVector2D(FMath::Min(-Hx - SL, -XL - 40.f), Yr)); L.Add(FVector2D(FMath::Min(-Hx - SL, -XL - 40.f), -Hy - Dp)); }
			if (bBackR) { L.Add(FVector2D(Xb, -Hy - Dp)); L.Add(FVector2D(Xb, Yb)); }
			else { L.Add(FVector2D(FMath::Max(Hx + SR, XR + 40.f), -Hy - Dp)); L.Add(FVector2D(FMath::Max(Hx + SR, XR + 40.f), Yr)); L.Add(FVector2D(XR, Yr)); }
			Pl.Poly.Reset(); for (const FVector2D& Q : L) Pl.Poly.Add(ToW(Q.X, Q.Y));
			Pl.Edge.Init(0, Pl.Poly.Num()); Pl.Edge.Last() = 1;   // (the last edge closes along the building)
			FVector2D BadW;
			if (OutlineClear(Pl, BadW, Why)) { Why.Reset(); UE_LOG(LogTemp, Log, TEXT("JEEBFILL PLOT %s %s: yard %.1f m deep, %s and %s beside"), *Pl.Name, *Pl.Use, Dp / 100.f, bBackL ? TEXT("none") : *FString::Printf(TEXT("%.1f m"), SL / 100.f), bBackR ? TEXT("none") : *FString::Printf(TEXT("%.1f m"), SR / 100.f)); return true; }
			const FVector2D Bq = Local(BadW);
			const float x = Bq.X, y = Bq.Y;
			// what to give up for that spot: depth if it is behind the house, else that side's width, then that side altogether
			if (y < -Hy - 20.f && (-Hy - y) - 80.f >= MinDepth && (-Hy - y) - 80.f < Dp) { Dp = (-Hy - y) - 80.f; continue; }
			if (x < -Hx + 30.f && !bBackL) { if (SL > MinSide) SL = FMath::Max(MinSide, FMath::Min(SL - 25.f, (-x - Hx) - 80.f)); else bBackL = true; continue; }
			if (x > Hx - 30.f && !bBackR) { if (SR > MinSide) SR = FMath::Max(MinSide, FMath::Min(SR - 25.f, (x - Hx) - 80.f)); else bBackR = true; continue; }
			if (Dp > MinDepth) { Dp = FMath::Max(MinDepth, Dp - 50.f); continue; }
			return false;
		}
		Why = TEXT("did not settle");
		return false;
	}

	// A plot drawn as a rectangle in FillPlots.csv: its sides move in, 50 cm at a time, until clear (no smaller than MinM a side)
	bool FitRect(FPlotB& Pl, FString& Why, float MinCm = 400.f)
	{
		const FVector2D O = Pl.Poly[0];
		Pl.Fwd = (Pl.Poly[1] - Pl.Poly[0]).GetSafeNormal(); Pl.Rt = FVector2D(Pl.Fwd.Y, -Pl.Fwd.X);
		FBox2D Lb(ForceInit);
		for (const FVector2D& Q : Pl.Poly) Lb += FVector2D(FVector2D::DotProduct(Q - O, Pl.Rt), FVector2D::DotProduct(Q - O, Pl.Fwd));
		const uint8 Kind = Pl.Edge.Num() > 0 ? Pl.Edge[0] : 0;
		for (int32 Iter = 0; Iter < 200; ++Iter)
		{
			auto ToW = [&](float x, float y) { return O + Pl.Rt * x + Pl.Fwd * y; };
			Pl.Poly = { ToW(Lb.Min.X, Lb.Min.Y), ToW(Lb.Min.X, Lb.Max.Y), ToW(Lb.Max.X, Lb.Max.Y), ToW(Lb.Max.X, Lb.Min.Y) };
			Pl.Edge.Init(Kind, 4);
			FVector2D BadW;
			if (OutlineClear(Pl, BadW, Why)) { Why.Reset(); UE_LOG(LogTemp, Log, TEXT("JEEBFILL PLOT %s %s: %.1f x %.1f m"), *Pl.Name, *Pl.Use, Lb.GetSize().X / 100.f, Lb.GetSize().Y / 100.f); return true; }
			const FVector2D B(FVector2D::DotProduct(BadW - O, Pl.Rt), FVector2D::DotProduct(BadW - O, Pl.Fwd));
			// one side moves in past the bad spot (at least 50 cm): the one that keeps the most ground and leaves both sides MinCm or more
			FBox2D Opt[4] = { Lb, Lb, Lb, Lb };
			Opt[0].Min.X = FMath::Max(Lb.Min.X + 50.f, B.X + 80.f); Opt[1].Max.X = FMath::Min(Lb.Max.X - 50.f, B.X - 80.f);
			Opt[2].Min.Y = FMath::Max(Lb.Min.Y + 50.f, B.Y + 80.f); Opt[3].Max.Y = FMath::Min(Lb.Max.Y - 50.f, B.Y - 80.f);
			int32 Best = -1; float BestA = 0.f;
			for (int32 o = 0; o < 4; ++o)
			{
				const FVector2D Sz = Opt[o].Max - Opt[o].Min;
				if (Sz.X < MinCm || Sz.Y < MinCm) continue;
				const float Ar = Sz.X * Sz.Y * (Sz.X > 3.f * Sz.Y || Sz.Y > 3.f * Sz.X ? 0.5f : 1.f);   // (a long thin strip is worth less)
				if (Ar > BestA) { BestA = Ar; Best = o; }
			}
			if (Best < 0) { Why += TEXT(" (too small)"); return false; }
			Lb = Opt[Best];
		}
		Why = TEXT("did not settle");
		return false;
	}

	// One straight span of panels between two posts' middles (the posts are placed by the caller)
	void RunSpan(FPlotB& Pl, const FKind& Panel, const FVector2D& Pa, float Wa, const FVector2D& Pb, float Wb, TArray<FTry>& Out)
	{
		const FVector2D Dir = (Pb - Pa).GetSafeNormal();
		const FVector2D S0 = Pa + Dir * (Wa * 0.5f - 1.f), S1 = Pb - Dir * (Wb * 0.5f - 1.f);   // (1 cm into each post: no seam)
		const float S = float(FVector2D::DotProduct(S1 - S0, Dir));
		if (S < 12.f) return;
		const float L = Panel.FootX;
		int32 n = FMath::Max(1, FMath::RoundToInt(S / L));
		float Sx = S / (n * L);
		while (Sx > 1.15f) { ++n; Sx = S / (n * L); }
		while (Sx < 0.85f && n > 1) { --n; Sx = S / (n * L); }
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
		const float Len = S / n;
		for (int32 i = 0; i < n; ++i)
		{
			const FVector2D A = S0 + Dir * (Len * i), B = S0 + Dir * (Len * (i + 1)), C = (A + B) * 0.5f;
			const float Za = JeebTerrain::HeightAt(A.X, A.Y), Zb = JeebTerrain::HeightAt(B.X, B.Y);
			const float Pitch = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(Zb - Za, Len)), -20.f, 20.f);
			FTry T;
			T.Kind = &Panel;
			T.bGround = true; T.bSolid = Panel.Col != ECol::None; T.bWalkBlock = T.bSolid && Panel.Height > 45.f; T.bDrop = false;
			T.Foot = FShape::Box(C, FVector2D(Len * 0.5f, Panel.FootY * 0.5f), Yaw);
			T.Xf = FTransform(FRotator(Pitch, Yaw, 0.f), FVector(C.X, C.Y, (Za + Zb) * 0.5f - 2.f), FVector(Sx, 1.f, 1.f));
			Out.Add(T);
			Pl.MaxGap = FMath::Max(Pl.MaxGap, 0.f);
		}
	}
	FTry PostAt(const FKind& Post, const FVector2D& At, float Yaw)
	{
		FTry T;
		T.Kind = &Post; T.bGround = true; T.bSolid = Post.Col != ECol::None; T.bWalkBlock = T.bSolid && Post.Height > 45.f; T.bDrop = false;
		T.Foot = FootOf(Post, At, Yaw, 1.f);
		T.Xf = FTransform(FRotator(0.f, Yaw, 0.f), FVector(At.X, At.Y, JeebTerrain::DrawnHeight(At.X, At.Y) - 2.f), FVector(1.f));
		return T;
	}
	// A run piece is clear of everything fixed (its own building excepted) and of other plots' pieces
	bool RunFits(const FTry& T, const FPlotB& Pl, FString& Why)
	{
		const FBox2D B = T.Foot.Bounds(20.f);
		for (int32 Gx = FGrid::G(B.Min.X); Gx <= FGrid::G(B.Max.X); ++Gx)
			for (int32 Gy = FGrid::G(B.Min.Y); Gy <= FGrid::G(B.Max.Y); ++Gy)
			{
				const FVector2D C = FGrid::Centre(Gx, Gy);
				uint16 Bits = Grid.At(Gx, Gy);
				if (Pl.bBuilding && Pl.Own.Contains(C, 30.f)) Bits &= ~uint16(F_BUILD | F_SOFT | F_GAME);
				if ((Bits & F_ROAD) && T.Foot.Contains(C, 20.f)) { Why = TEXT("road"); return false; }
				if ((Bits & (F_BUILD | F_WATER | F_KEEP | F_GAME | F_TREE)) && T.Foot.Contains(C, 3.f))
				{
					Why = (Bits & F_BUILD) ? TEXT("building") : (Bits & F_WATER) ? TEXT("water") : (Bits & F_KEEP) ? TEXT("keepclear") : (Bits & F_TREE) ? TEXT("treespot") : TEXT("solid");
					return false;
				}
			}
		const FBox2D Pb = T.Foot.Bounds(10.f);
		for (int32 Hx = FMath::FloorToInt(Pb.Min.X / 400.f); Hx <= FMath::FloorToInt(Pb.Max.X / 400.f); ++Hx)
			for (int32 Hy = FMath::FloorToInt(Pb.Min.Y / 400.f); Hy <= FMath::FloorToInt(Pb.Max.Y / 400.f); ++Hy)
				if (const TArray<int32>* L = PieceHash.Find(FIntPoint(Hx, Hy)))
					for (int32 Pi : *L) if (Placed[Pi].Group != Pl.Group && Touch(T.Foot, Placed[Pi].Foot, 2.f)) { Why = TEXT("prop ") + Placed[Pi].Kind->Name; return false; }
		return true;
	}
	// The gate: on a run edge, where the way out (1.5 m beyond it) is nearest a road and open
	void PickGate(FPlotB& Pl, const FFamily& Fam)
	{
		if (Pl.Row->Gate == TEXT("none")) return;
		const float Gw = Fam.GateW;
		const float Pw = D.Kinds.Contains(Fam.GatePost) ? D.Kinds[Fam.GatePost].FootX : 30.f;
		const FKind* CornerK = KindOf(Fam.Post); const FKind* PanelK = KindOf(Fam.Panel);
		const float CornerW = CornerK ? FMath::Min(CornerK->FootX, CornerK->FootY) : 30.f, CornerWx = CornerK ? FMath::Max(CornerK->FootX, CornerK->FootY) : 30.f, PanelLen = PanelK ? PanelK->FootX : 200.f;
		float Best = 1.0e9f;
		for (int32 Pass = 0; Pass < 2 && Pl.GateEdge < 0; ++Pass)
		{
			const bool bCornerOk = Pass == 1;   // (a gate against a corner post only when no run has room for a full panel beside it)
			for (int32 e = 0; e < Pl.Poly.Num(); ++e)
			{
				if (Pl.Edge[e] != 0) continue;
				const FVector2D A = Pl.Poly[e], B = Pl.Poly[(e + 1) % Pl.Poly.Num()];
				const float Len = float(FVector2D::Distance(A, B));
				// (the gate's posts either meet the corner post, or leave room for a panel at least 0.85 of its length: never a stub)
				const float Wc = CornerW, PanelL = PanelLen;
				const float Margin = Gw * 0.5f + Pw + Wc * 0.5f - 2.f, Margin1 = Gw * 0.5f + Pw + CornerWx * 0.5f + 0.85f * PanelL;
				if (Len < 2.f * Margin) continue;
				const FVector2D Dir = (B - A) / Len;
				FVector2D Nrm(Dir.Y, -Dir.X);
				if (InPoly(Pl.Poly, (A + B) * 0.5f + Nrm * 40.f)) Nrm = -Nrm;   // (outward)
				TArray<float> Ts;
				for (float t = Margin; t <= Len - Margin + 0.5f; t += 25.f)
				{
					const float tb = Len - t;   // (the same rule at the far corner)
					if ((t > Margin + 1.f && t < Margin1) || (tb > Margin + 1.f && tb < Margin1)) continue;
					if (!bCornerOk && (t < Margin1 || tb < Margin1)) continue;
					Ts.Add(t);
				}
				if (bCornerOk && Len - Margin > Margin + 1.f) Ts.Add(Len - Margin);
				for (const float t : Ts)
				{
					const FVector2D G = A + Dir * t, Out = G + Nrm * 150.f, In = G - Nrm * 150.f;
					if (BitsNear(Out, 60.f, Pl) & (F_BUILD | F_WATER | F_PSOLID | F_GAME | F_TREE)) continue;
					if (BitsNear(In, 50.f, Pl, 0.f) & (F_BUILD | F_WATER | F_GAME)) continue;
					if (Pl.bBuilding && Pl.Own.Contains(In, 60.f)) continue;
					bool bOther = false;
					for (const FPlotB& O : PlotsB) if (&O != &Pl && O.Poly.Num() >= 3 && (InPoly(O.Poly, Out) || PolyEdgeDist(O.Poly, Out) < 80.f)) { bOther = true; break; }
					if (bOther) continue;
					const float Score = FMath::Max(0.f, RoadDistCm(Out)) + 0.01f * t;
					if (Score < Best) { Best = Score; Pl.GateEdge = e; Pl.Gate = G; Pl.GateOut = Nrm; Pl.GateW = Gw; }
				}
			}
		}
	}
	// The plot's runs: a post at every corner, at both sides of the gate and at a free end, panels fitted between; placed whole
	void PlaceRuns(FPlotB& Pl, const FFamily& Fam)
	{
		const FKind* Panel = KindOf(Fam.Panel); const FKind* Post = KindOf(Fam.Post); const FKind* GPost = KindOf(Fam.GatePost);
		if (!Panel || !Post || !GPost) { UE_LOG(LogTemp, Warning, TEXT("JEEBFILL PLOT %s: family %s has missing kinds"), *Pl.Name, Fam.Name); return; }
		const int32 N = Pl.Poly.Num();
		TArray<FTry> Tries;
		TArray<int32> EdgeOf;
		FTry GateLeaf; bool bLeaf = false;
		auto IsRun = [&](int32 e) { return Pl.Edge[(e + N) % N] == 0; };
		// corner posts, square to the edge that leaves them (so the other edge's panels meet a flat face)
		TArray<float> PostYaw; PostYaw.Init(0.f, N);
		for (int32 v = 0; v < N; ++v)
		{
			const bool bIn = IsRun(v - 1), bOutE = IsRun(v);
			if (!bIn && !bOutE) continue;
			const FVector2D P = Pl.Poly[v];
			const FVector2D Dn = (Pl.Poly[(v + 1) % N] - P).GetSafeNormal(), Dp = (P - Pl.Poly[(v - 1 + N) % N]).GetSafeNormal();
			const FVector2D Along = bOutE ? Dn : Dp;
			PostYaw[v] = FMath::RadiansToDegrees(FMath::Atan2(Along.Y, Along.X));
			Tries.Add(PostAt(*Post, P, PostYaw[v])); EdgeOf.Add(v);
			++Pl.Posts;
		}
		// a post's width along a direction (its footprint box turned by its yaw)
		auto Ext = [](const FKind& K, float PYaw, const FVector2D& Dir)
		{
			const float Rad = FMath::DegreesToRadians(PYaw);
			const FVector2D Ax(FMath::Cos(Rad), FMath::Sin(Rad));
			const float C = FMath::Abs(FVector2D::DotProduct(Dir, Ax)), Sn = FMath::Abs(Dir.X * Ax.Y - Dir.Y * Ax.X);
			return C * K.FootX + Sn * K.FootY;
		};
		for (int32 e = 0; e < N; ++e)
		{
			if (!IsRun(e)) continue;
			const FVector2D A = Pl.Poly[e], B = Pl.Poly[(e + 1) % N];
			const int32 Before = Tries.Num();
			if (e == Pl.GateEdge)
			{
				const FVector2D Dir = (B - A).GetSafeNormal();
				const FVector2D G1 = Pl.Gate - Dir * (Pl.GateW * 0.5f + GPost->FootX * 0.5f), G2 = Pl.Gate + Dir * (Pl.GateW * 0.5f + GPost->FootX * 0.5f);
				const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
				Tries.Add(PostAt(*GPost, G1, Yaw)); Tries.Add(PostAt(*GPost, G2, Yaw)); Pl.Posts += 2;
				// (a fence's gate posts stand 30% taller than its other posts, so the opening reads as a gate)
				if (GPost == Post && Panel->Name.StartsWith(TEXT("Fence")))
					for (int32 k = Tries.Num() - 2; k < Tries.Num(); ++k) Tries[k].Xf.SetScale3D(FVector(1.f, 1.f, 1.3f));
				if (Panel->Height < 1.5f * 100.f && Panel->Name.StartsWith(TEXT("Fence")))
				{
					// the gate itself: a panel cut to the opening, hung on the first post and swung open into the plot
					const FVector2D In = -Pl.GateOut;
					const float W = Pl.GateW - 8.f, Ang = FMath::DegreesToRadians(140.f);   // (swung well back against the fence inside: 100 read as a stub of fence)
					const FVector2D Dl = Dir * FMath::Cos(Ang) + In * FMath::Sin(Ang);
					const FVector2D H = G1 + Dir * (GPost->FootX * 0.5f + 4.f), C = H + Dl * (W * 0.5f);
					FTry T;
					T.Kind = Panel; T.bGround = true; T.bSolid = Panel->Col != ECol::None; T.bWalkBlock = T.bSolid; T.bDrop = false;
					const float LYaw = FMath::RadiansToDegrees(FMath::Atan2(Dl.Y, Dl.X));
					T.Foot = FShape::Box(C, FVector2D(W * 0.5f, Panel->FootY * 0.5f), LYaw);
					T.Xf = FTransform(FRotator(0.f, LYaw, 0.f), FVector(C.X, C.Y, JeebTerrain::DrawnHeight(C.X, C.Y) - 2.f), FVector(W / Panel->FootX, 1.f, 1.f));
					GateLeaf = T; bLeaf = true;
				}
				RunSpan(Pl, *Panel, A, Ext(*Post, PostYaw[e], Dir), G1, GPost->FootX, Tries);
				RunSpan(Pl, *Panel, G2, GPost->FootX, B, Ext(*Post, PostYaw[(e + 1) % N], Dir), Tries);
			}
			else { const FVector2D Dir = (B - A).GetSafeNormal(); RunSpan(Pl, *Panel, A, Ext(*Post, PostYaw[e], Dir), B, Ext(*Post, PostYaw[(e + 1) % N], Dir), Tries); }
			for (int32 k = Before; k < Tries.Num(); ++k) EdgeOf.Add(e);
			Pl.RunLen += float(FVector2D::Distance(A, B));
		}
		// placed whole: each piece that cannot stand is logged where and why (the run is cut there, never silently)
		FStats& St = *PlotStats;
		for (int32 k = 0; k < Tries.Num(); ++k)
		{
			FString Why;
			if (!RunFits(Tries[k], Pl, Why))
			{
				++Pl.Cuts; St.Skipped.FindOrAdd(TEXT("runcut"))++;
				const FVector2D C = Tries[k].Foot.C;
				UE_LOG(LogTemp, Warning, TEXT("JEEBFILL RUN %s edge %d cut by %s at E %.1f N %.1f (%s)"), *Pl.Name, EdgeOf[k], *Why, JeebOakHill::EastM(C.Y), JeebOakHill::NorthM(C.X), *Tries[k].Kind->Name);
				continue;
			}
			if (Tries[k].Kind == Panel) ++Pl.Panels;
			Pl.RunPieces.Add(Placed.Num());
			Commit(Tries[k], Pl.Zone, Pl.Name + TEXT(" run"), St, Pl.Group);
		}
		FString LeafWhy;
		if (bLeaf && RunFits(GateLeaf, Pl, LeafWhy)) Commit(GateLeaf, Pl.Zone, Pl.Name + TEXT(" gate"), St, Pl.Group);
	}
	// Anchors for the use's sets: against the building's walls, along the inside of its runs, near its corners and gate, rows, middle
	bool InOtherPlot(const FPlotB& Pl, const FVector2D& Q) const
	{
		for (const FPlotB& O : PlotsB) if (&O != &Pl && O.Poly.Num() >= 3 && (InPoly(O.Poly, Q) || PolyEdgeDist(O.Poly, Q) < 60.f)) return true;
		return false;
	}
	void Anchors(const FPlotB& Pl, TArray<FAnchor>& Out)
	{
		const int32 N = Pl.Poly.Num();
		FRandomStream R(int32(Pl.Seed & 0x7fffffff));
		if (Pl.bBuilding)
		{
			const FShape& B = Pl.Own;
			const FVector2D Ns[4] = { FVector2D(1.f, 0.f), FVector2D(-1.f, 0.f), FVector2D(0.f, 1.f), FVector2D(0.f, -1.f) };
			for (const FVector2D& Nl : Ns)
			{
				const FVector2D Nw = B.World(Nl) - B.C;
				const bool bFront = FVector2D::DotProduct(Nw, Pl.Fwd) > 0.7f;   // (the street side: front sets only, beside the door)
				const float Half = Nl.X != 0.f ? B.H.Y : B.H.X, Off = Nl.X != 0.f ? B.H.X : B.H.Y;
				for (float s = -Half + 70.f; s <= Half - 70.f; s += 50.f)
				{
					const FVector2D L = Nl.X != 0.f ? FVector2D(Nl.X * Off, s) : FVector2D(s, Nl.Y * Off);
					const FVector2D P = B.World(L);
					if (bFront) { if (!InOtherPlot(Pl, P + Nw * 120.f)) Out.Add({ P, Nw, uint8(EAnchor::Front), R.FRand() }); continue; }
					if (!InPoly(Pl.Poly, P + Nw * 120.f)) continue;
					Out.Add({ P, Nw, uint8(EAnchor::Wall), R.FRand() });
				}
			}
		}
		for (int32 e = 0; e < N; ++e)
		{
			if (Pl.Edge[e] != 0) continue;
			const FVector2D A = Pl.Poly[e], B = Pl.Poly[(e + 1) % N];
			const float Len = float(FVector2D::Distance(A, B));
			if (Len < 1.f) continue;
			const FVector2D Dir = (B - A) / Len;
			FVector2D In(-Dir.Y, Dir.X);
			if (!InPoly(Pl.Poly, (A + B) * 0.5f + In * 40.f)) In = -In;
			for (float t = 80.f; t <= Len - 80.f; t += 50.f)
			{
				const FVector2D P = A + Dir * t;
				const float ToGate = Pl.GateEdge >= 0 ? float(FVector2D::Distance(P, Pl.Gate)) : 1.0e6f;
				if (ToGate < Pl.GateW * 0.5f + 90.f) continue;
				float ToCorner = 1.0e6f;
				for (int32 v = 0; v < N; ++v) ToCorner = FMath::Min(ToCorner, float(FVector2D::Distance(P, Pl.Poly[v])));
				const uint8 Type = (e == Pl.GateEdge && ToGate < Pl.GateW * 0.5f + 260.f) ? uint8(EAnchor::Gate) : ToCorner < 230.f ? uint8(EAnchor::Corner) : uint8(EAnchor::Fence);
				Out.Add({ P, In, Type, R.FRand() });
				if (Type == uint8(EAnchor::Corner)) Out.Add({ P, In, uint8(EAnchor::Fence), R.FRand() });   // (a corner spot also serves as fence)
				// (the outside of the run: a verge of bushes along it, never in another plot)
				if (Type != uint8(EAnchor::Gate) && !InOtherPlot(Pl, P - In * 150.f)) Out.Add({ P, -In, uint8(EAnchor::Outside), R.FRand() });
			}
		}
		Out.Sort([](const FAnchor& A, const FAnchor& B) { return A.Key < B.Key; });
	}
	// The plot's own lattice: rows across it (beds, bales) or tree spots, in its frame, farthest from the gate first
	void Lattice(const FPlotB& Pl, float Along, float Across, float FromEdge, float FromBuilding, TArray<FVector2D>& Out)
	{
		FBox2D Bx(ForceInit);
		for (const FVector2D& Q : Pl.Poly) Bx += FVector2D(FVector2D::DotProduct(Q, Pl.Rt), FVector2D::DotProduct(Q, Pl.Fwd));
		// (centred: the same margin at both ends)
		const float Sy = FMath::Max(0.f, Bx.GetSize().Y - 2.f * FromEdge), Sx = FMath::Max(0.f, Bx.GetSize().X - 2.f * FromEdge);
		const float Y0 = Bx.GetCenter().Y - FMath::FloorToFloat(Sy / Across) * Across * 0.5f, X0 = Bx.GetCenter().X - FMath::FloorToFloat(Sx / Along) * Along * 0.5f;
		for (float y = Y0; y <= Bx.Max.Y - FromEdge + 1.f; y += Across)
			for (float x = X0; x <= Bx.Max.X - FromEdge + 1.f; x += Along)
			{
				const FVector2D P = Pl.Rt * x + Pl.Fwd * y;
				if (!InPoly(Pl.Poly, P) || PolyEdgeDist(Pl.Poly, P) < FromEdge) continue;
				if (Pl.bBuilding && Pl.Own.Contains(P, FromBuilding)) continue;
				Out.Add(P);
			}
		const FVector2D G = Pl.GateEdge >= 0 ? Pl.Gate : Pl.Poly[0];
		Out.Sort([&](const FVector2D& A, const FVector2D& B) { return FVector2D::DistSquared(A, G) > FVector2D::DistSquared(B, G); });
	}
	void DressPlot(FPlotB& Pl)
	{
		const TArray<FUseRow>* Uses = D.Uses.Find(Pl.Use);
		if (!Uses) return;
		FStats& St = *PlotStats;
		TArray<FAnchor> As;
		Anchors(Pl, As);
		TArray<uint8> Used; Used.SetNumZeroed(As.Num());
		CurUse = Pl.Use; ClipPoly = &Pl.Poly;
		const FFamily* Fam = FamilyOf(Pl.Row->Bound);
		const FKind* PanelK = Fam ? KindOf(Fam->Panel) : nullptr;
		const float PanelHalf = PanelK ? PanelK->FootY * 0.5f : 15.f;
		FRandomStream R(int32(HashU(Pl.Seed ^ 0x51ed27u) & 0x7fffffff));
		int32 Sets = 0;
		for (int32 u = 0; u < Uses->Num(); ++u)
		{
			const FUseRow& U = (*Uses)[u];
			const int32 Want = U.Min + FMath::Min(U.Max - U.Min, int32(R.FRand() * (U.Max - U.Min + 1)));
			const uint32 VarSeed = HashU(Pl.Seed * 7919u + uint32(u) * 104729u);
			int32 Got = 0;
			TMap<FString, int32> Whys;
			if (U.Anchor == EAnchor::Row || U.Anchor == EAnchor::Trees || U.Anchor == EAnchor::Open)
			{
				TArray<FVector2D> Pts;
				if (U.Anchor == EAnchor::Row) Lattice(Pl, 260.f, 190.f, 110.f, 150.f, Pts);
				else if (U.Anchor == EAnchor::Trees) Lattice(Pl, 420.f, 420.f, 200.f, 240.f, Pts);
				else
				{
					FVector2D Sum(0.f, 0.f); int32 n = 0;
					TArray<FVector2D> L; Lattice(Pl, 100.f, 100.f, 100.f, 150.f, L);
					for (const FVector2D& Q : L) { Sum += Q; ++n; }
					if (n > 0) Pts.Add(Sum / n);
				}
				const float RowYaw = YawOf(Pl.Fwd);   // (a bed's long side across the plot)
				for (int32 k = 0; k < Pts.Num() && Got < Want; ++k)
				{
					if (U.Anchor == EAnchor::Trees)
					{
						const FShape Spot = FShape::Circle(Pts[k], 130.f);
						if (Grid.Hits(Spot, 0.f, NoProp | F_ROAD | F_PROP) || PieceNear(Pts[k], 130.f)) { Whys.FindOrAdd(TEXT("treespot"))++; continue; }
						Keep(Spot, TEXT("tree orchard ") + Pl.Name, F_TREE);
						TreeSpots.Add({ Pts[k], 150.f, TEXT("orchard") });
						++Got;
						continue;
					}
					const float Yaw = U.Anchor == EAnchor::Open && Pl.GateEdge >= 0 ? YawOf((Pl.Gate - Pts[k]).GetSafeNormal()) : RowYaw;
					// (an open set, a farmstead or a hayfield, is laid out like a hand row: each piece that fits is placed)
					FString W;
					bSnugOk = false;
					const int32 Got1 = PlaceSet(U.Set, Pts[k], Yaw, VarSeed + uint32(k), true, U.Anchor != EAnchor::Open, Pl.Zone, St, &W);
					bSnugOk = true;
					if (Got1 > 0) { ++Got; ++Sets; }
					else Whys.FindOrAdd(W)++;
				}
				if (Pts.Num() == 0) Whys.FindOrAdd(TEXT("no lattice spot"))++;
			}
			if (U.Anchor != EAnchor::Trees && U.Anchor != EAnchor::Open)
			{
				const float Back = SetBack(U.Set);
				// against the plot's fences or the house: the asked kind of spot first; a gate, corner or row set that found too few
				// then takes the fence spots, those nearest the gate first for a gate set
				auto Try = [&](const TArray<int32>& Order)
				{
					for (int32 k : Order)
					{
						if (Got >= Want) break;
						const FAnchor& A = As[k];
						if (Used[k]) continue;
						const float Off = A.Type == uint8(EAnchor::Wall) || A.Type == uint8(EAnchor::Front) ? 50.f + Back : PanelHalf + 14.f + Back;
					ClipPoly = A.Type == uint8(EAnchor::Front) || A.Type == uint8(EAnchor::Outside) ? nullptr : &Pl.Poly;
						FString W;
						bSnugOk = A.Type == uint8(EAnchor::Wall) || A.Type == uint8(EAnchor::Front);
						const int32 Got1 = PlaceSet(U.Set, A.P + A.N * Off, YawOf(A.N), VarSeed + uint32(k), true, true, Pl.Zone, St, &W);
						bSnugOk = true;
						if (Got1 > 0)
						{
							++Got; ++Sets;
							for (int32 j = 0; j < As.Num(); ++j) if (FVector2D::DistSquared(As[j].P, A.P) < 150.f * 150.f) Used[j] = 1;
						}
						else Whys.FindOrAdd(W)++;
					}
				};
				TArray<int32> Order;
				if (U.Anchor != EAnchor::Row) { for (int32 k = 0; k < As.Num(); ++k) if (As[k].Type == uint8(U.Anchor)) Order.Add(k); Try(Order); }
				if (Got < U.Min && (U.Anchor == EAnchor::Gate || U.Anchor == EAnchor::Corner || U.Anchor == EAnchor::Row))
				{
					Order.Reset();
					for (int32 k = 0; k < As.Num(); ++k) if (As[k].Type == uint8(EAnchor::Fence)) Order.Add(k);
					if (U.Anchor == EAnchor::Gate && Pl.GateEdge >= 0)
						Order.Sort([&](int32 a, int32 b) { return FVector2D::DistSquared(As[a].P, Pl.Gate) < FVector2D::DistSquared(As[b].P, Pl.Gate); });
					Try(Order);
				}
			}
			if (Got < U.Min)
			{
				FString Ws; for (const TPair<FString, int32>& P : Whys) Ws += FString::Printf(TEXT(" %s x%d"), *P.Key, P.Value);
				UE_LOG(LogTemp, Log, TEXT("JEEBFILL PLOT %s %s: %s placed %d of at least %d (%d anchors;%s)"), *Pl.Name, *Pl.Use, *U.Set, Got, U.Min, As.Num(), *Ws);
			}
		}
		CurUse.Reset(); ClipPoly = nullptr;
	}

	void PlacePlots()
	{
		if (D.Plots.Num() == 0 || !ZoneWanted(TEXT("Plots"))) return;
		const double T0 = FPlatformTime::Seconds();
		PlotStats = &Stats.FindOrAdd(TEXT("Plots"));
		// 1. outlines (yards shrunk to fit), in file order: earlier rows win the ground
		for (const FPlotRow& Row : D.Plots)
		{
			const bool bSq = Row.Site.StartsWith(TEXT("sq"), ESearchCase::IgnoreCase);
			if (!bSq && !OakOn()) continue;
			FPlotB Pl;
			Pl.Row = &Row; Pl.Name = Row.Name; Pl.Use = Row.Use; Pl.Seed = HashU(uint32(Row.Seed) * 2654435761u + 911u);
			Pl.Group = NextGroup++;
			const FFamily* Fam = FamilyOf(Row.Bound);
			FString Why;
			bool bOk = true;
			if (Row.Outline.Num() >= 3)
			{
				for (const FVector2D& EN : Row.Outline) Pl.Poly.Add(OakXY(EN, bSq));
				Pl.Edge.Init(uint8(Fam ? 0 : 2), Pl.Poly.Num());
				Pl.Fwd = (Pl.Poly[1] - Pl.Poly[0]).GetSafeNormal(); Pl.Rt = FVector2D(Pl.Fwd.Y, -Pl.Fwd.X);
				if (Pl.Poly.Num() == 4) bOk = FitRect(Pl, Why, Row.Use == TEXT("Paddock") || Row.Use == TEXT("Farm") ? 600.f : 400.f);
			}
			else bOk = MakeYard(Pl, Fam, Why);
			if (!bOk) { UE_LOG(LogTemp, Warning, TEXT("JEEBFILL PLOT %s (line %d) left out: %s"), *Row.Name, Row.Line, *Why); continue; }
			if (!Fam) for (uint8& E : Pl.Edge) if (E == 0) E = 2;
			FVector2D C(0.f, 0.f); for (const FVector2D& Q : Pl.Poly) C += Q; C /= Pl.Poly.Num();
			Pl.Zone = ZoneAt(C.X, C.Y);
			PlotsB.Add(MoveTemp(Pl));
		}
		// 2. gates and runs for every plot, before any clutter
		for (FPlotB& Pl : PlotsB)
		{
			const FFamily* Fam = FamilyOf(Pl.Row->Bound);
			if (!Fam) continue;
			PickGate(Pl, *Fam);
			if (Pl.GateEdge < 0 && Pl.Row->Gate != TEXT("none")) UE_LOG(LogTemp, Warning, TEXT("JEEBFILL PLOT %s: no room for a gate"), *Pl.Name);
			PlaceRuns(Pl, *Fam);
		}
		// 3. each gate's way in and out kept clear, and reached
		for (FPlotB& Pl : PlotsB)
		{
			if (Pl.GateEdge < 0) continue;
			Keep(FShape::Lane(Pl.Gate + Pl.GateOut * 130.f, Pl.Gate - Pl.GateOut * 260.f, 70.f), TEXT("gate ") + Pl.Name);
			Target(TEXT("gate ") + Pl.Name + TEXT(" inside"), Pl.Gate - Pl.GateOut * 150.f, 0);
			// the town's walk targets that now lie inside the plot (a house's back or side, from before the yards were fenced) are
			// reached through its gate: the gate's own target stands for them, and the yard's middle stays open by its recipe
			for (int32 t = Targets.Num() - 1; t >= 0; --t)
			{
				const FTarget& Tg = Targets[t];
				if (Tg.Area != 0 || Tg.Name.StartsWith(TEXT("gate ")) || !InPoly(Pl.Poly, Tg.At)) continue;
				UE_LOG(LogTemp, Log, TEXT("JEEBFILL PLOT %s: target %s is inside the plot, reached through its gate"), *Pl.Name, *Tg.Name);
				Targets.RemoveAt(t);
			}
		}
		// 4. what each use places
		for (FPlotB& Pl : PlotsB) DressPlot(Pl);
		PlotStats->Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
		PlotStats = nullptr;
	}

	// Checks: every run joined post to post, every fenced plot closed but for its gate, and no wares outside the market
	void RunChecks()
	{
		int32 Open = 0, Gaps = 0;
		for (const FPlotB& Pl : PlotsB)
		{
			// joins: along each run, every panel end within 2 cm of the next piece's footprint
			int32 BadJoin = 0; float Worst = 0.f;
			for (int32 Pi : Pl.RunPieces)
			{
				const FPlaced& P = Placed[Pi];
				if (P.Xf.GetScale3D().X == P.Xf.GetScale3D().Y && P.Kind->FootX < 100.f) continue;   // (posts)
				const FVector2D Ax(P.Foot.Cs, P.Foot.Sn);
				for (int32 s = -1; s <= 1; s += 2)
				{
					const FVector2D End = P.Foot.C + Ax * (s * P.Foot.H.X);
					float Best = 1.0e9f;
					for (int32 Qi : Pl.RunPieces)
					{
						if (Qi == Pi) continue;
						const FShape& Q = Placed[Qi].Foot;
						const FVector2D L = Q.Local(End);
						const float Dx = FMath::Max(0.f, FMath::Abs(L.X) - Q.H.X), Dy = FMath::Max(0.f, FMath::Abs(L.Y) - Q.H.Y);
						Best = FMath::Min(Best, FMath::Sqrt(Dx * Dx + Dy * Dy));
					}
					if (Best > 2.f) { ++BadJoin; Worst = FMath::Max(Worst, FMath::Min(Best, 999.f)); }
				}
			}
			const bool bFenced = FamilyOf(Pl.Row->Bound) != nullptr;
			const bool bClosed = !bFenced || (Pl.Cuts == 0 && BadJoin == 0);
			if (bFenced && !bClosed) ++Open;
			Gaps += BadJoin;
			UE_LOG(LogTemp, Log, TEXT("JEEBFILL RUN %s %s %s panels %d posts %d length %.1f m joins over 2 cm %d (worst %.0f cm) cuts %d gate %s closed %d"),
				*Pl.Name, *Pl.Use, *Pl.Row->Bound, Pl.Panels, Pl.Posts, Pl.RunLen / 100.f, BadJoin, Worst, Pl.Cuts, Pl.GateEdge >= 0 ? TEXT("yes") : TEXT("no"), bClosed ? 1 : 0);
		}
		int32 Mis = 0;
		for (const FPlaced& P : Placed)
		{
			if (!P.Kind->bGoods) continue;
			const bool bOk = !P.Use.IsEmpty() ? (P.Use == TEXT("Market") || P.Use == TEXT("ShopFront")) : (P.Zone == Z_OakPlaza || P.Zone == Z_MarketYard);
			if (!bOk && Mis++ < 10) UE_LOG(LogTemp, Warning, TEXT("JEEBFILL MISPLACED %s (%s) in %s at E %.1f N %.1f"), *P.Kind->Name, *P.Set, P.Use.IsEmpty() ? ZoneNames[P.Zone] : *P.Use, JeebOakHill::EastM(P.Foot.C.Y), JeebOakHill::NorthM(P.Foot.C.X));
		}
		UE_LOG(LogTemp, Log, TEXT("JEEBFILL ENCLOSURE %d plots, %d fenced plots not closed, %d run joins over 2 cm; MISPLACED %d"), PlotsB.Num(), Open, Gaps, Mis);
	}

	// ------------------------------------------------------------------ checks
	struct FArea { FString Name; FBox2D Box; FVector2D Start; };
	TArray<FArea> Areas() const
	{
		TArray<FArea> A;
		if (OakOn()) A.Add({ TEXT("OakHill"), FBox2D(FVector2D(JeebOakHill::BoxX0, JeebOakHill::BoxY0), FVector2D(JeebOakHill::BoxX1, JeebOakHill::BoxY1)), FVector2D(JeebOakHill::SpawnX, JeebOakHill::SpawnY) });
		A.Add({ TEXT("Square"), FBox2D(FVector2D(-7600.f, -5600.f), FVector2D(9800.f, 5800.f)), FVector2D(0.f, 0.f) });
		return A;
	}
	// Every target reachable from the spawn by the knight's capsule (42 cm: a 1 m lane), on a 50 cm walk grid
	int32 FloodFill(int32 AreaIdx, const FArea& Ar, TArray<uint8>* OutReach = nullptr, int32* OutW = nullptr, int32* OutH = nullptr)
	{
		const int32 Gx0 = FGrid::G(Ar.Box.Min.X), Gy0 = FGrid::G(Ar.Box.Min.Y);
		const int32 Nx = FGrid::G(Ar.Box.Max.X) - Gx0, Ny = FGrid::G(Ar.Box.Max.Y) - Gy0;
		TArray<uint8> Block; Block.SetNumZeroed(Nx * Ny);
		for (int32 i = 0; i < Nx; ++i) for (int32 j = 0; j < Ny; ++j) Block[i * Ny + j] = (Grid.At(Gx0 + i, Gy0 + j) & NoWalk) ? 1 : 0;
		const int32 Wx = Nx / 2, Wy = Ny / 2;
		auto Free = [&](int32 a, int32 b)
		{
			// the capsule at the corner between four 25 cm cells: the 12 cells within 42 cm of it
			const int32 ci = a * 2 + 1, cj = b * 2 + 1;
			for (int32 di = -2; di <= 1; ++di)
				for (int32 dj = -2; dj <= 1; ++dj)
				{
					if ((di == -2 || di == 1) && (dj == -2 || dj == 1)) continue;
					const int32 i = ci + di, j = cj + dj;
					if (i < 0 || j < 0 || i >= Nx || j >= Ny || Block[i * Ny + j]) return false;
				}
			return true;
		};
		TArray<uint8> Seen; Seen.SetNumZeroed(Wx * Wy);
		auto WCell = [&](const FVector2D& P) { return FIntPoint(FMath::FloorToInt((P.X - Gx0 * Cell) / 50.f), FMath::FloorToInt((P.Y - Gy0 * Cell) / 50.f)); };
		// start: the free walk cell nearest the spawn
		FIntPoint S = WCell(Ar.Start);
		bool bStart = false;
		for (int32 r = 0; r < 12 && !bStart; ++r)
			for (int32 a = S.X - r; a <= S.X + r && !bStart; ++a)
				for (int32 b = S.Y - r; b <= S.Y + r && !bStart; ++b)
					if (a >= 0 && b >= 0 && a < Wx && b < Wy && Free(a, b)) { S = FIntPoint(a, b); bStart = true; }
		int32 Reached = 0;
		if (bStart)
		{
			TArray<FIntPoint> Q; Q.Reserve(Wx * Wy / 4);
			Q.Add(S); Seen[S.X * Wy + S.Y] = 1;
			for (int32 h = 0; h < Q.Num(); ++h)
			{
				const FIntPoint P = Q[h];
				const FIntPoint Nb[4] = { FIntPoint(P.X + 1, P.Y), FIntPoint(P.X - 1, P.Y), FIntPoint(P.X, P.Y + 1), FIntPoint(P.X, P.Y - 1) };
				for (const FIntPoint& N : Nb)
				{
					if (N.X < 0 || N.Y < 0 || N.X >= Wx || N.Y >= Wy || Seen[N.X * Wy + N.Y]) continue;
					if (!Free(N.X, N.Y)) { Seen[N.X * Wy + N.Y] = 2; continue; }
					Seen[N.X * Wy + N.Y] = 1;
					Q.Add(N);
				}
			}
		}
		int32 Bad = 0, Count = 0;
		for (FTarget& T : Targets)
		{
			if (T.Area != AreaIdx || !Ar.Box.IsInside(T.At)) continue;
			++Count;
			const FIntPoint C = WCell(T.At);
			const int32 Rw = FMath::Max(1, FMath::CeilToInt(T.Reach / 50.f));
			for (int32 a = C.X - Rw; a <= C.X + Rw && !T.bReached; ++a)
				for (int32 b = C.Y - Rw; b <= C.Y + Rw && !T.bReached; ++b)
					if (a >= 0 && b >= 0 && a < Wx && b < Wy && Seen[a * Wy + b] == 1) T.bReached = true;
			if (!T.bReached)
			{
				++Bad;
				UE_LOG(LogTemp, Warning, TEXT("JEEBFILL UNREACHABLE %s at %.0f %.0f (E %.1f N %.1f)"), *T.Name, T.At.X, T.At.Y, JeebOakHill::EastM(T.At.Y), JeebOakHill::NorthM(T.At.X));
			}
		}
		for (int32 i = 0; i < Seen.Num(); ++i) Reached += Seen[i] == 1 ? 1 : 0;
		UE_LOG(LogTemp, Log, TEXT("JEEBFILL reach %s: %d of %d targets reachable from the spawn (%s), %d unreachable; %.0f m^2 walkable"), *Ar.Name, Count - Bad, Count, bStart ? TEXT("start found") : TEXT("NO FREE START"), Bad, Reached * 0.25f);
		if (OutReach) { *OutReach = MoveTemp(Seen); *OutW = Wx; *OutH = Wy; }
		return Bad;
	}

	// "No empty spaces": in Oak Hill's town (not its fringe), every fillable metre within 3 m of a building, prop or tree spot
	void GapCheck()
	{
		if (!OakOn()) return;
		const float X0 = JeebOakHill::BoxX0, Y0 = JeebOakHill::BoxY0;
		const int32 Nx = FMath::RoundToInt((JeebOakHill::BoxX1 - X0) / 100.f), Ny = FMath::RoundToInt((JeebOakHill::BoxY1 - Y0) / 100.f);
		TArray<uint8> Fillable, Filled; Fillable.SetNumZeroed(Nx * Ny); Filled.SetNumZeroed(Nx * Ny);
		for (int32 i = 0; i < Nx; ++i)
			for (int32 j = 0; j < Ny; ++j)
			{
				const float X = X0 + (i + 0.5f) * 100.f, Y = Y0 + (j + 0.5f) * 100.f;
				const int32 Z = ZoneAt(X, Y);
				const int32 Gx = FGrid::G(X0 + i * 100.f), Gy = FGrid::G(Y0 + j * 100.f);
				uint16 Any = 0;
				for (int32 a = 0; a < 4; ++a) for (int32 b = 0; b < 4; ++b) Any |= Grid.At(Gx + a, Gy + b);
				Filled[i * Ny + j] = (Any & (F_BUILD | F_PROP | F_TREE | F_GAME | F_SOFT)) ? 1 : 0;
				const uint16 Mid = Grid.At(Gx + 2, Gy + 2);
				Fillable[i * Ny + j] = (IsOak(Z) && Z != Z_OakFringe && !(Mid & (F_ROAD | F_WATER | F_KEEP | F_BUILD))) ? 1 : 0;
			}
		// distance to the nearest filled metre (chamfer 1 / 1.41)
		TArray<float> Dist; Dist.Init(1.0e6f, Nx * Ny);
		for (int32 k = 0; k < Nx * Ny; ++k) if (Filled[k]) Dist[k] = 0.f;
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			const int32 S = Pass == 0 ? 1 : -1;
			for (int32 ii = 0; ii < Nx; ++ii)
				for (int32 jj = 0; jj < Ny; ++jj)
				{
					const int32 i = Pass == 0 ? ii : Nx - 1 - ii, j = Pass == 0 ? jj : Ny - 1 - jj;
					float& Dd = Dist[i * Ny + j];
					auto Nb = [&](int32 a, int32 b, float W) { if (a >= 0 && b >= 0 && a < Nx && b < Ny) Dd = FMath::Min(Dd, Dist[a * Ny + b] + W); };
					Nb(i - S, j, 1.f); Nb(i, j - S, 1.f); Nb(i - S, j - S, 1.414f); Nb(i - S, j + S, 1.414f);
				}
		}
		int32 Open = 0, Far = 0; float Worst = 0.f; FVector2D WorstAt(0.f, 0.f);
		TArray<int32> Sq; Sq.SetNumZeroed(Nx * Ny);
		int32 Best = 0; FIntPoint BestAt(0, 0);
		for (int32 i = 0; i < Nx; ++i)
			for (int32 j = 0; j < Ny; ++j)
			{
				const int32 k = i * Ny + j;
				if (!Fillable[k]) continue;
				++Open;
				if (Dist[k] > 3.f) { ++Far; if (Dist[k] > Worst) { Worst = Dist[k]; WorstAt = FVector2D(X0 + (i + 0.5f) * 100.f, Y0 + (j + 0.5f) * 100.f); } }
				if (!Filled[k])
				{
					Sq[k] = 1 + ((i > 0 && j > 0) ? FMath::Min3(Sq[(i - 1) * Ny + j], Sq[i * Ny + j - 1], Sq[(i - 1) * Ny + j - 1]) : 0);
					if (Sq[k] > Best) { Best = Sq[k]; BestAt = FIntPoint(i, j); }
				}
			}
		const FVector2D Mid(X0 + (BestAt.X + 1 - Best * 0.5f) * 100.f, Y0 + (BestAt.Y + 1 - Best * 0.5f) * 100.f);
		UE_LOG(LogTemp, Log, TEXT("JEEBFILL GAP %d m at E %.0f N %.0f (largest empty square in town; %d of %d open metres are over 3 m from anything, the worst %.1f m at E %.0f N %.0f)"),
			Best, JeebOakHill::EastM(Mid.Y), JeebOakHill::NorthM(Mid.X), Far, Open, Worst, JeebOakHill::EastM(WorstAt.Y), JeebOakHill::NorthM(WorstAt.X));
	}

	void OverlapCheck()
	{
		int32 Pairs = 0, OnBlock = 0;
		TSet<uint64> Done;
		for (const TPair<FIntPoint, TArray<int32>>& B : PieceHash)
			for (int32 a = 0; a < B.Value.Num(); ++a)
				for (int32 b = a + 1; b < B.Value.Num(); ++b)
				{
					const int32 i = FMath::Min(B.Value[a], B.Value[b]), j = FMath::Max(B.Value[a], B.Value[b]);
					const uint64 Key = (uint64(i) << 32) | uint64(j);
					if (Done.Contains(Key)) continue;
					Done.Add(Key);
					if ((Placed[i].Group < 0 || Placed[i].Group != Placed[j].Group) && Touch(Placed[i].Foot, Placed[j].Foot, -1.f))
					{
						if (Pairs++ < 10) UE_LOG(LogTemp, Warning, TEXT("JEEBFILL OVERLAP %s (%s) and %s (%s) at %.0f %.0f"), *Placed[i].Kind->Name, *Placed[i].Set, *Placed[j].Kind->Name, *Placed[j].Set, Placed[i].Foot.C.X, Placed[i].Foot.C.Y);
					}
				}
		for (const FPlaced& P : Placed)
		{
			if (!P.bGround) continue;
			FShape In = P.Foot;
			const float Sh = P.Set.EndsWith(TEXT(" run")) ? 13.f : 5.f;   // (a run's end post stands 12 cm off its house's wall: half a cell)
			if (In.Type == 0) In.H -= FVector2D(Sh, Sh); else In.R -= Sh;
			const bool bOnBlock = P.bSnug ? (Grid.Hits(In, 0.f, F_WATER) || BodyHits(P.Foot, P.Xf.GetLocation().Z)) : Grid.Hits(In, 0.f, F_BUILD | F_WATER);
			if (bOnBlock && OnBlock++ < 5) UE_LOG(LogTemp, Warning, TEXT("JEEBFILL OVERLAP %s (%s) on a building or water at %.0f %.0f"), *P.Kind->Name, *P.Set, P.Foot.C.X, P.Foot.C.Y);
		}
		UE_LOG(LogTemp, Log, TEXT("JEEBFILL OVERLAP %d (pieces on pieces %d, on buildings or water %d)"), Pairs + OnBlock, Pairs, OnBlock);
		// every lantern stands on something (a barrel, crate, table or post) or hangs: none on the ground or in the air
		int32 Lanterns = 0, Loose = 0;
		for (const FPlaced& P : Placed)
		{
			if (!P.Kind->Name.StartsWith(TEXT("Lantern")) || P.Kind->bHang) continue;
			++Lanterns;
			const FVector L = P.Xf.GetLocation();
			// (2026-11-17: each lantern is logged with what it stands on and the gap between its foot and that piece's top, cm)
			const FPlaced* On = nullptr;
			float Gap = 35.f;
			for (const FPlaced& B : Placed)
				if (&B != &P && B.bGround && B.Foot.Contains(FVector2D(L.X, L.Y), 5.f))
				{
					const float G = L.Z - (B.Xf.GetLocation().Z + B.Kind->Height * B.Xf.GetScale3D().Z);
					if (FMath::Abs(G) < FMath::Abs(Gap)) { Gap = G; On = &B; }
				}
			const bool bOn = On && FMath::Abs(Gap) < 3.f;
			if (!bOn) ++Loose;
			UE_LOG(LogTemp, Log, TEXT("JEEBFILL LANTERN %s (%s) at %.0f %.0f %.0f on %s gap %.1f cm%s"), *P.Kind->Name, *P.Set, L.X, L.Y, L.Z, On ? *On->Kind->Name : TEXT("nothing"), On ? Gap : 0.f, bOn ? TEXT("") : TEXT(" FLOATING"));
		}
		UE_LOG(LogTemp, Log, TEXT("JEEBFILL LANTERNS %d, on nothing %d; wall-backed sets pulled onto the wall %d (lines: scene %d, wall box %d, grid %d)"), Lanterns, Loose, SnugCount, SnugTrace, SnugBox, SnugCells);
	}

	// ------------------------------------------------------------------ the dump (Saved/Fill, for Tools/FillMap.ps1 and the layout work)
	void Dump(const TCHAR* Stage, const TArray<FArea>& As, const TArray<TArray<uint8>>* Reach = nullptr, const TArray<FIntPoint>* ReachSize = nullptr)
	{
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("Fill");
		IFileManager::Get().MakeDirectory(*Dir, true);
		for (int32 a = 0; a < As.Num(); ++a)
		{
			const FArea& Ar = As[a];
			const int32 Gx0 = FGrid::G(Ar.Box.Min.X), Gy0 = FGrid::G(Ar.Box.Min.Y);
			const int32 Nx = FGrid::G(Ar.Box.Max.X) - Gx0, Ny = FGrid::G(Ar.Box.Max.Y) - Gy0;
			FString Out;
			Out.Reserve((Ny + 2) * (Nx + 20));
			Out += FString::Printf(TEXT("# JEEBFILL grid %s (%s). One character per 25 cm cell. Rows run north to south (the first row is the north edge), columns west to east.\n"), *Ar.Name, Stage);
			Out += FString::Printf(TEXT("# cell 25 rows %d cols %d x0 %.1f y0 %.1f (game cm of the first row's and first column's cell centres: row r is X = x0 - 25 r, column c is Y = y0 + 25 c)\n"), Nx, Ny, (Gx0 + Nx - 0.5f) * Cell, (Gy0 + 0.5f) * Cell);
			Out += FString::Printf(TEXT("# plaza E0 %.3f N0 %.3f (metres east and north of Oak Hill's plaza of the first column and row: E = E0 + 0.25 c, N = N0 - 0.25 r)\n"), JeebOakHill::EastM((Gy0 + 0.5f) * Cell), JeebOakHill::NorthM((Gx0 + Nx - 0.5f) * Cell));
			Out += TEXT("# legend: # building/wall, ~ water, = road, k kept clear (doors, lanes, counters, walks), g existing solid, s existing decor, t tree spot, P fill solid, p fill piece (walk-through), u walkable but not reached (after the flood fill), . open\n");
			const TArray<uint8>* R = Reach ? &(*Reach)[a] : nullptr;
			for (int32 i = Nx - 1; i >= 0; --i)
			{
				for (int32 j = 0; j < Ny; ++j)
				{
					const uint16 B = Grid.At(Gx0 + i, Gy0 + j);
					TCHAR Ch = '.';
					if (B & F_BUILD) Ch = '#'; else if (B & F_WATER) Ch = '~'; else if (B & F_PSOLID) Ch = 'P'; else if (B & F_PROP) Ch = 'p'; else if (B & F_GAME) Ch = 'g';
					else if (B & F_SOFT) Ch = 's'; else if (B & F_TREE) Ch = 't'; else if (B & F_KEEP) Ch = 'k'; else if (B & F_ROAD) Ch = '=';
					if (R && (Ch == '.' || Ch == '=' || Ch == 'k' || Ch == 'p') && R->Num() > 0)
					{
						const int32 a2 = i / 2, b2 = j / 2, H = (*ReachSize)[a].Y;
						if (a2 < (*ReachSize)[a].X && b2 < H && (*R)[a2 * H + b2] != 1) Ch = 'u';
					}
					Out.AppendChar(Ch);
				}
				Out.AppendChar('\n');
			}
			FFileHelper::SaveStringToFile(Out, *(Dir / FString::Printf(TEXT("%s_%s.txt"), Stage, *Ar.Name)));
			// zones, one character per metre
			if (FCString::Strcmp(Stage, TEXT("blockers")) == 0)
			{
				FString Zs;
				const int32 Mx = FMath::RoundToInt(Ar.Box.GetSize().X / 100.f), My = FMath::RoundToInt(Ar.Box.GetSize().Y / 100.f);
				Zs += FString::Printf(TEXT("# JEEBFILL zones %s. One character per metre, rows north to south. rows %d cols %d x0 %.1f y0 %.1f (cell centres, cm); plaza E0 %.2f N0 %.2f\n# legend:"),
					*Ar.Name, Mx, My, Ar.Box.Max.X - 50.f, Ar.Box.Min.Y + 50.f, JeebOakHill::EastM(Ar.Box.Min.Y + 50.f), JeebOakHill::NorthM(Ar.Box.Max.X - 50.f));
				for (int32 z = 0; z < Z_Count; ++z) Zs += FString::Printf(TEXT(" %c=%s"), ZoneChar[z], ZoneNames[z]);
				Zs += TEXT("\n");
				for (int32 i = Mx - 1; i >= 0; --i)
				{
					for (int32 j = 0; j < My; ++j) Zs.AppendChar(ZoneChar[ZoneAt(Ar.Box.Min.X + (i + 0.5f) * 100.f, Ar.Box.Min.Y + (j + 0.5f) * 100.f)]);
					Zs.AppendChar('\n');
				}
				FFileHelper::SaveStringToFile(Zs, *(Dir / FString::Printf(TEXT("zones_%s.txt"), *Ar.Name)));
				// the ground's height at every 25 cm cell centre (JeebTerrain::HeightAt, the same the slope test reads), whole cm, for offline layout tools
				FString Hs;
				Hs.Reserve(Nx * (Ny * 6 + 2) + 512);
				Hs += FString::Printf(TEXT("# JEEBFILL heights %s. Ground height (cm) at each 25 cm cell centre, the same cells as blockers_%s.txt: rows north to south, columns west to east, comma separated.\n"), *Ar.Name, *Ar.Name);
				Hs += FString::Printf(TEXT("# rows %d cols %d plaza E0 %.3f N0 %.3f (E = E0 + 0.25 c, N = N0 - 0.25 r)\n"), Nx, Ny, JeebOakHill::EastM((Gy0 + 0.5f) * Cell), JeebOakHill::NorthM((Gx0 + Nx - 0.5f) * Cell));
				for (int32 i = Nx - 1; i >= 0; --i)
				{
					for (int32 j = 0; j < Ny; ++j)
					{
						if (j) Hs.AppendChar(',');
						Hs.AppendInt(FMath::RoundToInt(JeebTerrain::HeightAt((Gx0 + i + 0.5f) * Cell, (Gy0 + j + 0.5f) * Cell)));
					}
					Hs.AppendChar('\n');
				}
				FFileHelper::SaveStringToFile(Hs, *(Dir / FString::Printf(TEXT("heights_%s.txt"), *Ar.Name)));
			}
		}
		// the shapes: named blockers, targets, plots, tree spots and (after placing) every piece
		FString Csv = TEXT("# JEEBFILL shapes. X,Y game cm; E,N metres from Oak Hill's plaza; Yaw degrees; HalfX,HalfY (box) or Radius cm; X2,Y2 a lane's other end\nType,Class,Name,X,Y,E,N,Yaw,HalfX,HalfY,Radius,X2,Y2,Extra\n");
		auto Row = [&](const TCHAR* Type, const FString& Class, const FString& Name, const FShape& S, const FString& Extra)
		{
			const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(S.Sn, S.Cs));
			Csv += FString::Printf(TEXT("%s,%s,%s,%.0f,%.0f,%.2f,%.2f,%.1f,%.0f,%.0f,%.0f,%.0f,%.0f,%s\n"), Type, *Class, *Name.Replace(TEXT(","), TEXT(" ")), S.C.X, S.C.Y, JeebOakHill::EastM(S.C.Y), JeebOakHill::NorthM(S.C.X),
				Yaw, S.H.X, S.H.Y, S.R, S.P1.X, S.P1.Y, *Extra);
		};
		for (const FNamedShape& N : Grid.Shapes)
		{
			if (N.Name.StartsWith(TEXT("scan "))) continue;   // (the scanned solids are in the grid files)
			const TCHAR* Cl = N.Bit == F_KEEP ? TEXT("keep") : N.Bit == F_TREE ? TEXT("tree") : N.Bit == F_SOFT ? TEXT("decor") : N.Bit == F_GAME ? TEXT("solid") : TEXT("building");
			Row(N.S.Type == 2 ? TEXT("lane") : N.S.Type == 1 ? TEXT("circle") : TEXT("box"), Cl, N.Name, N.S, TEXT(""));
		}
		for (const FTarget& T : Targets) Row(TEXT("target"), !Reach ? TEXT("target") : T.bReached ? TEXT("reached") : TEXT("unreached"), T.Name, FShape::Circle(T.At, 50.f), FString::FromInt(T.Area));
		if (OakOn())
		{
			int32 i = 0;
			for (const JeebOakHill::FPlot& P : JeebOakHill::Plots())
			{
				// (PlotSD's frame: its W along the direction (Cs, -Sn) in east-north, which is game (-Sn, Cs))
				FShape S = FShape::Box(JeebOakHill::FromPx(P.Px, P.Py), FVector2D(P.W, P.H) * 0.5f * JeebOakHill::UnitCm, 0.f);
				S.Cs = -P.Sn; S.Sn = P.Cs;
				Row(TEXT("plot"), P.Model ? P.Model : TEXT("empty"), FString::Printf(TEXT("%d %s"), i++, P.Name), S, TEXT(""));
			}
		}
		for (const FPlotB& Pl : PlotsB)
			for (int32 v = 0; v < Pl.Poly.Num(); ++v)
				Row(TEXT("plotpt"), Pl.Use, Pl.Name, FShape::Circle(Pl.Poly[v], 0.f), FString::Printf(TEXT("%d edge %d gate %d"), v, Pl.Edge[v], Pl.GateEdge == v ? 1 : 0));
		for (const FPlaced& P : Placed) Row(TEXT("piece"), P.Kind->Name, P.Set, P.Foot, FString::Printf(TEXT("%s z %.0f solid %d"), ZoneNames[P.Zone], P.Xf.GetLocation().Z, P.bSolid ? 1 : 0));
		FFileHelper::SaveStringToFile(Csv, *(Dir / FString::Printf(TEXT("%s_shapes.csv"), Stage)));
		UE_LOG(LogTemp, Log, TEXT("JEEBFILL dump %s written to %s"), Stage, *FPaths::ConvertRelativePathToFull(Dir));
	}

	// ------------------------------------------------------------------ drawing and solids
	int32 BodyCount = 0, DrawPlain = 0, Missing = 0, LightCount = 0;
	void BuildComponents()
	{
		USceneComponent* Root = Owner->GetRootComponent();
		// solids: one hidden set of boxes for every machine (never rendered, never in a shadow pass), plus each walkable-mesh kind's own
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		UInstancedStaticMeshComponent* Boxes = nullptr;
		TMap<const FKind*, UInstancedStaticMeshComponent*> MeshSolids;
		auto MakeSolidSet = [&](UStaticMesh* M)
		{
			UInstancedStaticMeshComponent* I = NewObject<UInstancedStaticMeshComponent>(Owner);
			I->SetStaticMesh(M);
			I->SetupAttachment(Root);
			I->SetMobility(EComponentMobility::Static);
			I->SetVisibility(false);
			I->SetHiddenInGame(true);
			I->SetCastShadow(false);
			I->SetCollisionProfileName(TEXT("BlockAll"));
			I->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
			I->SetCanEverAffectNavigation(false);
			I->RegisterComponent();
			Owner->SolidSets.Add(I);
			return I;
		};
		for (const FPlaced& P : Placed)
		{
			if (!P.bSolid) continue;
			const FKind& K = *P.Kind;
			if (K.Col == ECol::Mesh)
			{
				UInstancedStaticMeshComponent*& I = MeshSolids.FindOrAdd(P.Kind);
				if (!I) if (UStaticMesh* M = MeshFor(K)) I = MakeSolidSet(M);
				if (I) { I->AddInstance(P.Xf, true); ++BodyCount; }
				continue;
			}
			if (!Cube) continue;
			if (!Boxes) Boxes = MakeSolidSet(Cube);
			const float S = P.Xf.GetScale3D().Y, Sx = P.Xf.GetScale3D().X;   // (a run panel is stretched along its X only, Placement v2)
			const FVector2D Mid = P.Foot.C;
			const float Yaw = P.Xf.Rotator().Yaw;
			FVector Size = K.Shape == EShape::Circle || K.Col == ECol::Capsule ? FVector(K.FootX * 0.85f, K.FootX * 0.85f, K.Height) * S : FVector(K.FootX * Sx, K.FootY * S, K.Height * S);
			if (K.Col == ECol::Capsule && K.Shape == EShape::Box) Size.X = Size.Y = FMath::Min(K.FootX, K.FootY) * 0.85f * S;
			Boxes->AddInstance(FTransform(FRotator(0.f, Yaw, 0.f), FVector(Mid.X, Mid.Y, P.Xf.GetLocation().Z + Size.Z * 0.5f), Size / 100.f), true);
			++BodyCount;
		}
		if (bServer) return;
		// drawn sets: one per kind (and its flame), culled by size, shadows by class
		TMap<UStaticMesh*, UInstancedStaticMeshComponent*> Sets;
		const float CullScale = FMath::Clamp(CVarFillCull.GetValueOnGameThread(), 0.1f, 4.f);
		int32 OakLights = 0, OtherLights = 0;
		auto Draw = [&](UStaticMesh* M, const FKind& K, const FTransform& Xf, bool bFlame)
		{
			if (!InstancingOk(M))
			{
				UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
				C->SetStaticMesh(M); C->SetupAttachment(Root); C->SetMobility(EComponentMobility::Static); C->SetWorldTransform(Xf);
				C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->SetCastShadow(!bFlame && K.Shadow != EShadow::No); C->bCastFarShadow = K.Shadow == EShadow::Yes;
				C->SetCachedMaxDrawDistance(CullCm(K.Cull)); C->LDMaxDrawDistance = CullCm(K.Cull);
				JeebLamp::ApplyToMesh(C, Owner);
				C->RegisterComponent();
				++DrawPlain;
				return;
			}
			UInstancedStaticMeshComponent*& I = Sets.FindOrAdd(M);
			if (!I)
			{
				I = NewObject<UInstancedStaticMeshComponent>(Owner);
				I->SetStaticMesh(M);
				I->SetupAttachment(Root);
				I->SetMobility(EComponentMobility::Static);
				I->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				I->SetCanEverAffectNavigation(false);
				I->SetCastShadow(!bFlame && K.Shadow != EShadow::No);
				I->bCastFarShadow = K.Shadow == EShadow::Yes;
				I->bAffectDistanceFieldLighting = false;
				const float Cull = CullCm(K.Cull) * ((K.Cull == ECull::Tiny || K.Cull == ECull::Small) ? CullScale : 1.f);
				I->SetCullDistances(int32(Cull * 0.85f), int32(Cull));
				I->SetVisibility(CVarFillShow.GetValueOnGameThread() != 0);
				JeebLamp::ApplyToMesh(I, Owner);
				I->RegisterComponent();
				Owner->DrawSets.Add(I);
				Owner->DrawCull.Add(uint8(K.Cull));
			}
			I->AddInstance(Xf, true);
		};
		for (const FPlaced& P : Placed)
		{
			UStaticMesh* M = MeshFor(*P.Kind);
			if (!M) { ++Missing; continue; }
			Draw(M, *P.Kind, P.Xf, false);
			const FString FlameName = !P.Kind->Flame.IsEmpty() ? P.Kind->Flame : P.Kind->Name + TEXT("Flame");
			if (P.Kind->Name.StartsWith(TEXT("Torch")) || P.Kind->Name == TEXT("Sconce") || !P.Kind->Flame.IsEmpty())
			{
				UStaticMesh* F = LoadMeshPath(TEXT("/Game/TownDecor/SM_") + FlameName);
				if (!F) F = LoadMeshPath(TEXT("/Game/Filler/SM_") + FlameName);
				if (F) Draw(F, *P.Kind, P.Xf, true);
			}
			// a real light only in lamp posts and lit wells (at most 19 more in Oak Hill, 40 lights there in all; 12 elsewhere)
			if (P.Kind->bLight && P.Kind->Height > 150.f)
			{
				int32& Count = IsOak(P.Zone) ? OakLights : OtherLights;
				if (Count >= (IsOak(P.Zone) ? 19 : 12)) continue;
				++Count;
				UPointLightComponent* L = NewObject<UPointLightComponent>(Owner);
				L->SetupAttachment(Root);
				// (the village well, 2026-11-21 owner: "the whole well lights up": its light hung under the roof's middle, shadowless, and lit
				// the roof, posts and stones all round. Now it sits in the lantern on the post (SM_WellVillage's Glow faces, about -125 0 180
				// in its own frame), dimmer and shorter, so only the lantern and the post beside it glow)
				const bool bWell = P.Kind->Name == TEXT("WellVillage");
				// (2026-11-21, owner, the gallows lamp post: "why does this whole post light up instead of just the lantern?": every lamp
				// post's light sat on the post's own axis 35 cm under its top, inside the post and crossbar, so the shadowless light lit the
				// wood from within. Now it sits in the middle of the lantern's glass: each mesh's Glow slot, measured with -GlowLog)
				FVector Glow;
				const FString& Kn = P.Kind->Name;
				const bool bGlow = bWell ? (Glow = FVector(-125.f, -11.f, 187.f), true)
					: Kn == TEXT("LampPostGallows") ? (Glow = FVector(-72.f, 0.f, 186.f), true)
					: Kn == TEXT("LampPostPillar") ? (Glow = FVector(-36.f, 0.f, 239.f), true)
					: Kn == TEXT("LampPostStone") ? (Glow = FVector(-28.f, 0.f, 177.f), true) : false;
				L->SetWorldLocation(bGlow ? P.Xf.TransformPosition(Glow) : P.Xf.GetLocation() + FVector(0.f, 0.f, P.Kind->Height * P.Xf.GetScale3D().Z - 35.f));
				L->SetIntensityUnits(ELightUnits::Lumens);
				L->SetIntensity(bWell ? 40.f : 100.f);
				L->SetAttenuationRadius(bWell ? 200.f : 380.f);
				L->SetSourceRadius(4.f);
				if (bWell) UE_LOG(LogTemp, Log, TEXT("JEEBFILL well %s at %s yaw %.0f, its lantern light at %s"), *P.Kind->Name, *P.Xf.GetLocation().ToCompactString(), P.Xf.Rotator().Yaw, *L->GetComponentLocation().ToCompactString());
				JeebLamp::TrackLight(L);
				L->SetCastShadows(false);
				L->MaxDrawDistance = 4000.f;
				L->SetVisibility(CVarFillShow.GetValueOnGameThread() != 0);
				L->RegisterComponent();
				Owner->Lights.Add(L);
				++LightCount;
			}
		}
#if !UE_BUILD_SHIPPING
		// -FillGallery (test pictures only, fillfix 2026-11-16): every kind in FillKinds.csv once, fronts toward +X, floating 20 m over the
		// open grass far from everything (sky behind), in 15 m strips; each strip's -ShotEyes eye is logged. Shot with r.ForceLOD 0 and
		// then the last LOD, it shows any kind that draws black, unlit, untextured or broken by its generated LODs.
		if (TestSwitch(TEXT("FillGallery")))
		{
			TArray<FString> Names; D.Kinds.GetKeys(Names); Names.Sort();
			const FVector2D Org(-60000.f, -20000.f);
			constexpr float StripLen = 1500.f, StripStep = 4500.f, Gap = 60.f;
			// (one height for the whole gallery: 20 m over the highest ground under it)
			float Z0 = -1.0e9f;
			for (float Sx = Org.X - 1000.f; Sx <= Org.X + 1500.f; Sx += 500.f)
				for (float Sy = Org.Y - 500.f; Sy <= Org.Y + 20.f * StripStep; Sy += 500.f) Z0 = FMath::Max(Z0, JeebTerrain::HeightAt(Sx, Sy));
			Z0 += 2000.f;
			int32 Strip = 0; float Y = 0.f; FString InStrip;
			auto EndStrip = [&]()
			{
				const float Ex = Org.X + 1000.f, Ey = Org.Y + Strip * StripStep + StripLen * 0.5f;
				UE_LOG(LogTemp, Log, TEXT("JEEBFILL GALLERY strip %d eye %.0f_%.0f_%.0f_180_-8 kinds%s"), Strip, Ex, Ey, Z0 + 180.f - JeebTerrain::HeightAt(Ex, Ey), *InStrip);
				++Strip; Y = 0.f; InStrip.Reset();
			};
			for (const FString& N : Names)
			{
				const FKind& K = D.Kinds[N];
				UStaticMesh* M = MeshFor(K);
				if (!M) continue;
				const FBox B = M->GetBoundingBox();
				const float W = B.GetSize().X;
				if (Y > 0.f && Y + W > StripLen) EndStrip();
				const float Y0 = Org.Y + Strip * StripStep + Y;
				// (yaw -90: the kind's front, local +Y, faces +X; its local X runs along world -Y)
				Draw(M, K, FTransform(FRotator(0.f, -90.f, 0.f), FVector(Org.X - B.GetCenter().Y, Y0 + W * 0.5f + B.GetCenter().X, Z0 - B.Min.Z)), false);
				InStrip += TEXT(" ") + N;
				Y += W + Gap;
			}
			if (Y > 0.f) EndStrip();
		}
#endif
	}
	TMap<const FKind*, UStaticMesh*> MeshCache;
	UStaticMesh* MeshFor(const FKind& K)
	{
		if (UStaticMesh** Got = MeshCache.Find(&K)) return *Got;
		UStaticMesh* M = nullptr;
		if (!K.Mesh.IsEmpty())
		{
			if (K.Mesh.StartsWith(TEXT("/Game/Filler/"))) M = LoadMeshPath(K.Mesh.Replace(TEXT("/Game/Filler/"), TEXT("/Game/TownDecor/")));
			if (!M) M = LoadMeshPath(K.Mesh);
		}
		if (!M) M = LoadMeshPath(TEXT("/Game/TownDecor/SM_") + K.Name);
		if (!M) M = LoadMeshPath(TEXT("/Game/Filler/SM_") + K.Name);   // (the editor and test builds: /Game/Filler is never cooked)
		if (!M && !MissingLogged.Contains(K.Name)) { MissingLogged.Add(K.Name); UE_LOG(LogTemp, Warning, TEXT("JEEBFILL MISSING %s (no mesh: its solid stays, nothing drawn)"), *K.Name); }
		MeshCache.Add(&K, M);
		return M;
	}

	// ------------------------------------------------------------------ the whole build
	void Run()
	{
		const double T0 = FPlatformTime::Seconds();
		LoadData(D);
		for (const FString& N : D.Notes) UE_LOG(LogTemp, Log, TEXT("JEEBFILL data: %s"), *N);
		UE_LOG(LogTemp, Log, TEXT("JEEBFILL data from %s: %d kinds (%s), %d sets, %d Oak Hill rows, %d zones, %d tree spots"), *D.Folder, D.Kinds.Num(), D.bKindsCm ? TEXT("sizes in cm") : TEXT("sizes in m"), D.Sets.Num(), D.Oak.Num(), D.Zones.Num(), D.Trees.Num());
		OnlyZone = CVarFillZone.GetValueOnGameThread();
#if !UE_BUILD_SHIPPING
		FParse::Value(FCommandLine::Get(), TEXT("FillZone="), OnlyZone);
#endif
		Grid.MakeStatics = [this](const FIntPoint& K, TArray<uint16>& Bits) { MakeStatics(K, Bits); };
		const double TS = FPlatformTime::Seconds();
		ScanSolids();
		OakKeeps();
		SquareKeeps();
		NodeAndDecorKeeps();
		TreeKeeps();
		const double TS1 = FPlatformTime::Seconds();
		UE_LOG(LogTemp, Log, TEXT("JEEBFILL blockers: %d shapes (%d scanned solids, %d samples of big solids), %d targets, %.0f ms"), Grid.Shapes.Num(), ScanCount, SampleCount, Targets.Num(), (TS1 - TS) * 1000.0);
		const bool bDump = TestSwitch(TEXT("FillDump"));
		const TArray<FArea> As = Areas();
		if (bDump) Dump(TEXT("blockers"), As);
		const double TPl = FPlatformTime::Seconds();   // (placing starts after the dump)

		PlaceOakHill();   // (the hand rows first: shop fronts, doorsteps, the plaza; the plots fit round them)
		PlacePlots();
		PlaceZones();
		const double TP = FPlatformTime::Seconds();

		// the logs: one line per zone, then the checks
		FStats All;
		uint32 Total = 2166136261u;
		TArray<FString> Names; Stats.GetKeys(Names);
		Names.Sort();
		for (const FString& N : Names)
		{
			const FStats& St = Stats[N];
			FString Sk;
			for (const TPair<FString, int32>& S : St.Skipped) Sk += FString::Printf(TEXT("%s%s:%d"), Sk.IsEmpty() ? TEXT("") : TEXT(" "), *S.Key, S.Value);
			UE_LOG(LogTemp, Log, TEXT("JEEBFILL zone %s sets %d pieces %d solid %d kinds %d skipped %s ms %.0f hash %08x"), *N, St.Sets, St.Pieces, St.Solid, St.Kinds.Num(), Sk.IsEmpty() ? TEXT("-") : *Sk, St.Ms, St.Hash);
			All.Sets += St.Sets; All.Pieces += St.Pieces; All.Solid += St.Solid; All.Kinds.Append(St.Kinds);
			Total ^= St.Hash; Total *= 16777619u;
		}
		OverlapCheck();
		RunChecks();
		TArray<TArray<uint8>> Reach; TArray<FIntPoint> ReachSize;
		for (int32 a = 0; a < As.Num(); ++a)
		{
			TArray<uint8> R; int32 W = 0, H = 0;
			FloodFill(a, As[a], &R, &W, &H);
			Reach.Add(MoveTemp(R)); ReachSize.Add(FIntPoint(W, H));
		}
		GapCheck();
		for (const FTreeSpot& T : TreeSpots) UE_LOG(LogTemp, Log, TEXT("JEEBFILL TREESPOT %s %.0f %.0f E %.1f N %.1f r %.1f"), *T.Class, T.At.X, T.At.Y, JeebOakHill::EastM(T.At.Y), JeebOakHill::NorthM(T.At.X), T.R / 100.f);
		const double TC = FPlatformTime::Seconds();
		BuildComponents();
		Owner->Hash = Total;
		if (bDump) Dump(TEXT("filled"), As, &Reach, &ReachSize);
		const double T1 = FPlatformTime::Seconds();
		UE_LOG(LogTemp, Log, TEXT("JEEBFILL total sets %d pieces %d solid %d kinds %d hash %08x (%s)"), All.Sets, All.Pieces, All.Solid, All.Kinds.Num(), Total, bServer ? TEXT("server") : TEXT("client or standalone"));
		UE_LOG(LogTemp, Log, TEXT("JEEBFILL sets %d drawn (+%d plain: materials not flagged for instancing), %d solid sets with %d bodies, %d lights, %d missing meshes, %d covered m^2 under %d pieces"),
			Owner->DrawSets.Num(), DrawPlain, Owner->SolidSets.Num(), BodyCount, LightCount, Missing, GCoverCells, GCover.Num());
		UE_LOG(LogTemp, Log, TEXT("JEEBFILL ms %.0f (blockers %.0f, placing %.0f, checks %.0f, components %.0f; grid chunks made %.0f) grid %d chunks %d KB"),
			(T1 - T0) * 1000.0, (TS1 - TS) * 1000.0, (TP - TPl) * 1000.0, (TC - TP) * 1000.0, (T1 - TC) * 1000.0, Grid.MakeSeconds * 1000.0, Grid.Chunks.Num(), Grid.Chunks.Num() * ChunkN * ChunkN * 2 / 1024);
	}
};

// ====================================================================================================== the actor
void JeebFillApplyCull()
{
	const float S = FMath::Clamp(CVarFillCull.GetValueOnGameThread(), 0.1f, 4.f);
	for (TObjectIterator<AWorldFill> It; It; ++It)
		for (int32 i = 0; i < It->DrawSets.Num(); ++i)
			if (UInstancedStaticMeshComponent* I = It->DrawSets[i])
			{
				const ECull C = ECull(It->DrawCull[i]);
				const float Cull = CullCm(C) * ((C == ECull::Tiny || C == ECull::Small) ? S : 1.f);
				I->SetCullDistances(int32(Cull * 0.85f), int32(Cull));
			}
}
void JeebFillApplyShow()
{
	const bool bShow = CVarFillShow.GetValueOnGameThread() != 0;
	for (TObjectIterator<AWorldFill> It; It; ++It)
	{
		for (UInstancedStaticMeshComponent* I : It->DrawSets) if (I) I->SetVisibility(bShow);
		for (UPointLightComponent* L : It->Lights) if (L) L->SetVisibility(bShow);
	}
}

AWorldFill::AWorldFill()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	SetRootComponent(Root);
}

void AWorldFill::BeginPlay()
{
	Super::BeginPlay();
	if (!GetActorLocation().IsNearlyZero()) SetActorLocation(FVector::ZeroVector);
	static bool bSinks = false;
	if (!bSinks)
	{
		bSinks = true;
		CVarFillCull->SetOnChangedCallback(FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*) { JeebFillApplyCull(); }));
		CVarFillShow->SetOnChangedCallback(FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*) { JeebFillApplyShow(); }));
	}
	ResetCover();
	if (TestSwitch(TEXT("NoFill"))) { UE_LOG(LogTemp, Log, TEXT("JEEBFILL off (-NoFill)")); return; }
	if (JeebTownLab::Active()) { UE_LOG(LogTemp, Log, TEXT("JEEBFILL off in the TownLab map")); return; }
	Build();
	if (!IsNetMode(NM_DedicatedServer))
	{
		JeebAutumnDropCovered(GetWorld());
		if (Lights.Num() > 0) GetWorldTimerManager().SetTimer(LightTimer, this, &AWorldFill::UpdateLights, 1.f, true, 0.1f);
	}
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("FillQuit")))
	{
		FTimerHandle Q;
		GetWorldTimerManager().SetTimer(Q, []() { UE_LOG(LogTemp, Log, TEXT("JEEBFILL quitting (-FillQuit)")); FPlatformMisc::RequestExit(false); }, 3.f, false);
	}
#endif
}

void AWorldFill::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(LightTimer);
	ResetCover();
	Super::EndPlay(Reason);
}

void AWorldFill::Build()
{
	FFillBuilder B;
	B.Owner = this;
	B.World = GetWorld();
	B.bServer = IsNetMode(NM_DedicatedServer);
	B.Run();
}

// The lamp posts' lights follow the sun like the town's lanterns (a 1 s timer, not a tick)
void AWorldFill::UpdateLights()
{
	const float Dk = ADayNight::Dusk;
	if (FMath::Abs(Dk - LastDusk) < 0.01f) return;
	LastDusk = Dk;
	for (UPointLightComponent* L : Lights) if (L) L->SetIntensity((L->AttenuationRadius < 300.f ? 40.f : 100.f) * FMath::Lerp(0.15f, 1.f, Dk));   // (the well's lantern: 40, short reach)
}

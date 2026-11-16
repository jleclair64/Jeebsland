#include "Fill.h"
#include "FillData.h"
#include "OakHill.h"
#include "TownLab.h"
#include "TownLabDecor.h"
#include "TownLabSwaps.h"
#include "Terrain.h"
#include "WorldMap.h"
#include "DayNight.h"
#include "ResourceNode.h"
#include "ChoppableTree.h"
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
		int32 Zone = 0;
		FString Set;
		int32 Group = -1;   // (a hand-placed row: its pieces may join end to end, so they are never checked against each other)
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
		for (int32 i = 0; i <= 32; ++i) for (int32 j = 0; j <= 32; ++j) PathL[i][j] = bSquare ? JeebTerrain::PathClearance(X0 + i * 100.f, Y0 + j * 100.f) : 1.0e6f;
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
		Keep(FShape::Circle(FromPx(255.f, 410.f), 500.f), TEXT("plaza oak roots"));
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
	enum class EWhy : uint8 { Ok, Road, Water, Build, Keep, Game, Tree, Prop, Slope, Missing };
	static const TCHAR* WhyName(EWhy W)
	{
		switch (W) { case EWhy::Road: return TEXT("road"); case EWhy::Water: return TEXT("water"); case EWhy::Build: return TEXT("building"); case EWhy::Keep: return TEXT("keepclear");
		case EWhy::Game: return TEXT("solid"); case EWhy::Tree: return TEXT("treespot"); case EWhy::Prop: return TEXT("prop"); case EWhy::Slope: return TEXT("slope"); case EWhy::Missing: return TEXT("missing"); default: return TEXT("ok"); }
	}
	struct FTry { const FKind* Kind; FTransform Xf; FShape Foot; bool bGround, bSolid, bWalkBlock, bDrop; };

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
	// bHand (FillOakHill rows, laid out and checked by hand): 5 cm from buildings, not 30 (doorstep sets stand 6 cm off the door wall),
	// and a row's own pieces are not tested against each other (fence, wall and hedge runs join end to end)
	EWhy Fits(const FTry& T, const TArray<FTry>& Pending, bool bHand)
	{
		const bool bPost = T.Kind->Height > 150.f && FMath::Max(T.Kind->FootX, T.Kind->FootY) < 60.f;
		if (Grid.Hits(T.Foot, bPost ? 20.f : 40.f, F_ROAD)) return EWhy::Road;
		const uint16 Mask = bHand ? uint16(NoProp & ~F_BUILD) : NoProp;
		if (bHand && Grid.Hits(T.Foot, 5.f, F_BUILD)) return EWhy::Build;
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
		for (const FVector2D& P : Pts) { const float Z = JeebTerrain::HeightAt(P.X, P.Y); Zs[n++] = Z; Lo = FMath::Min(Lo, Z); Hi = FMath::Max(Hi, Z); }
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
		const FShape Fr = FShape::Box(At, FVector2D(1.f, 1.f), Yaw);
		TArray<FTry> Tries;
		for (const FPiece& P : S->Pieces)
		{
			const float DropRoll = V.FRand(), AltRoll = V.FRand(), TurnRoll = V.FRandRange(-1.f, 1.f);   // (always drawn: the same draws whatever is skipped)
			if (bVar && P.bDrop && DropRoll < 0.25f) continue;
			FString KName = P.Kind;
			if (bVar && P.Alts.Num() > 0) { const int32 k = FMath::Min(int32(AltRoll * (P.Alts.Num() + 1)), P.Alts.Num()); if (k > 0) KName = P.Alts[k - 1]; }
			const FKind* K = KindOf(KName);
			if (!K) { St.Skipped.FindOrAdd(TEXT("missing"))++; if (!P.bDrop && bAll) { if (OutWhy) *OutWhy = TEXT("missing ") + KName; return 0; } continue; }
			const float PYaw = Yaw + P.Yaw + (bVar ? TurnRoll * P.Turn : 0.f);
			const FVector2D Pw = Fr.World(FVector2D(P.Pos.X, P.Pos.Y));
			FTry T;
			T.Kind = K;
			T.bGround = !P.bStack && !P.bHang && !K->bHang;
			T.bSolid = K->Col != ECol::None && !P.bHang && !K->bHang;
			T.bWalkBlock = T.bSolid && T.bGround && K->Height * P.Scale > 45.f;
			T.bDrop = P.bDrop;
			T.Foot = FootOf(*K, Pw, PYaw, P.Scale);
			float Slope = 0.f;
			const float Gz = GroundUnder(T.Foot, Slope);
			const float Sink = K->Height * P.Scale < 60.f ? 2.f : 0.f;
			T.Xf = FTransform(FRotator(0.f, PYaw, 0.f), FVector(Pw.X, Pw.Y, Gz - Sink + P.Pos.Z), FVector(P.Scale));
			if (T.bGround)
			{
				EWhy W = Slope > K->Slope ? EWhy::Slope : Fits(T, Tries, !bAll);
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
		const int32 Pi = Placed.Add({ T.Kind, T.Xf, T.Foot, T.bGround, T.bSolid, T.bWalkBlock, Zone, SetName, Group });
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
				const FShape Col = SolidFoot(*T.Kind, T.Foot, T.Xf.GetScale3D().X, T.Xf.Rotator().Yaw);
				if (Col.Area() > 10000.f) AddCover(Col);
			}
		}
		const FVector L = T.Xf.GetLocation();
		uint32& H = St.Hash;
		const uint32 Kc = FCrc::StrCrc32(*T.Kind->Name.ToLower());
		H ^= Kc; H *= 16777619u;
		HashIn(H, L.X); HashIn(H, L.Y); HashIn(H, L.Z); HashIn(H, T.Xf.Rotator().Yaw); HashIn(H, T.Xf.GetScale3D().X); HashIn(H, T.bSolid ? 1.f : 0.f);
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
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			const bool bSq = Pass == 1;
			const TCHAR* Key = bSq ? TEXT("SquareRows") : TEXT("OakHill");
			if ((!bSq && !OakOn()) || !ZoneWanted(Key)) continue;
			const double T0 = FPlatformTime::Seconds();
			int32 Failed = 0, Rows = 0;
			for (const FOakRow& R : D.Oak)
			{
				if (R.bSquare != bSq) continue;
				FStats& St = Stats.FindOrAdd(Key);
				++Rows;
				const FVector2D At = bSq ? FVector2D(R.N * 100.f, R.E * 100.f) : FVector2D(JeebOakHill::PosX + R.N * 100.f, JeebOakHill::PosY + R.E * 100.f);
				FString Why;
				const int32 Before = St.Pieces;
				const int32 N = PlaceSet(R.Set, At, R.Yaw, HashU(uint32(R.Seed) * 2654435761u + 17u), R.bVar, false, ZoneAt(At.X, At.Y), St, &Why);
				if (N == 0 || !Why.IsEmpty())
				{
					++Failed;
					UE_LOG(LogTemp, Log, TEXT("JEEBFILL OAKROW line %d %s at %s E %.1f N %.1f: %d of its pieces placed%s%s"), R.Line, *R.Set, bSq ? TEXT("square") : TEXT("Oak Hill"), R.E, R.N, St.Pieces - Before, Why.IsEmpty() ? TEXT("") : TEXT("; skipped "), *Why);
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
			if (In.Type == 0) In.H -= FVector2D(5.f, 5.f); else In.R -= 5.f;
			if (Grid.Hits(In, 0.f, F_BUILD | F_WATER)) ++OnBlock;
		}
		UE_LOG(LogTemp, Log, TEXT("JEEBFILL OVERLAP %d (pieces on pieces %d, on buildings or water %d)"), Pairs + OnBlock, Pairs, OnBlock);
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
			const float S = P.Xf.GetScale3D().X;
			const FVector2D Mid = P.Foot.C;
			const float Yaw = P.Xf.Rotator().Yaw;
			FVector Size = K.Shape == EShape::Circle || K.Col == ECol::Capsule ? FVector(K.FootX * 0.85f, K.FootX * 0.85f, K.Height) * S : FVector(K.FootX, K.FootY, K.Height) * S;
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
				L->SetWorldLocation(P.Xf.GetLocation() + FVector(0.f, 0.f, P.Kind->Height * P.Xf.GetScale3D().Z - 35.f));
				L->SetIntensityUnits(ELightUnits::Lumens);
				L->SetIntensity(100.f);
				L->SetAttenuationRadius(380.f);
				L->SetLightColor(FLinearColor(1.f, 0.62f, 0.3f));
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

		PlaceOakHill();
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
		JeebMeadowDropCovered(GetWorld());
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
	for (UPointLightComponent* L : Lights) if (L) L->SetIntensity(100.f * FMath::Lerp(0.15f, 1.f, Dk));
}

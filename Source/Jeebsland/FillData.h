#pragma once

#include "CoreMinimal.h"

// The world fill's data (Docs/FillPlan.md; AWorldFill, Fill.cpp): what each filler kind is, the vignettes, Oak Hill's hand-placed sets,
// the zones' rules and the reserved tree spots. Everything comes from Content/Data/Fill*.csv (live with every build: no code change to
// re-tune), read the same on every machine, so the server and every client place the very same pieces.
//
// All five files: '#' lines are comments, the first other line is the header (read by column NAME, any order, extra columns ignored),
// plain commas. Metres and degrees unless a header says cm.
//  FillKinds.csv  (fillassets) Kind, Mesh, Shape (box|circle), FootX, FootY (the footprint along the kind's own X and Y at foot height;
//                 a circle's FootX is its diameter), FootCX, FootCY (the footprint's middle from the pivot), Height, Collision
//                 (none|box|capsule|mesh), Shadow (yes|near|no), Cull (tiny|small|medium|large|landmark), Slope (max degrees; empty:
//                 20, or 30 for bushes, plants, flowers, rocks, logs and stumps), Flags
//                 (light|hang|flame=<Kind>). Every kind's front is its local +Y.
//  FillSets.csv   (filllayout) Set, Kind, X, Y, Z, Yaw, Scale, Flags (drop|alt=A/B|turn=N|stack|hang): one row per piece, set-local metres.
//  FillOakHill.csv(filllayout) Set, E, N, Yaw, Seed, Var, Plot, Note, optional Site: one placed set (or a single Kind) per row, metres
//                 from Oak Hill's plaza; Site "Square" (fillfix 2026-11-16): E, N are metres from the world's origin instead (X = N, Y = E),
//                 for hand-placed sets on the original square (Blackrock's fountain yard).
//  FillZones.csv  (filllayout) Zone, On, Density (sets per 100 m^2), Mix ("Set:Weight Set:Weight"), optional Seed, Facing
//                 (random|road|wall), Trees (tree spots per 100 m^2).
//  FillTrees.csv  (filllayout) Site, E, N, Class (shade|street|conifer|orchard), Radius (m kept clear), Note.
namespace JeebFill
{
	enum class EShape : uint8 { Box, Circle };
	enum class ECol : uint8 { None, Box, Capsule, Mesh };
	enum class EShadow : uint8 { Yes, Near, No };
	enum class ECull : uint8 { Tiny, Small, Medium, Large, Landmark };
	enum class EFacing : uint8 { Random, Road, Wall };

	struct FKind
	{
		FString Name, Mesh, Flame;
		EShape Shape = EShape::Box;
		float FootX = 50.f, FootY = 50.f, FootCX = 0.f, FootCY = 0.f, Height = 50.f;   // cm (read in metres)
		ECol Col = ECol::None;
		EShadow Shadow = EShadow::Near;
		ECull Cull = ECull::Small;
		float Slope = 20.f;
		bool bLight = false, bHang = false;
		bool bFromCsv = false;   // false: guessed from the mesh's bounds (test only: machines may differ)
	};
	struct FPiece
	{
		FString Kind;
		FVector Pos = FVector::ZeroVector;   // cm in the set's frame
		float Yaw = 0.f, Scale = 1.f, Turn = 0.f;
		bool bDrop = false, bStack = false, bHang = false;
		TArray<FString> Alts;
	};
	struct FSet { FString Name; TArray<FPiece> Pieces; float Reach = 0.f; };
	struct FOakRow { FString Set; float E = 0.f, N = 0.f, Yaw = 0.f; int32 Seed = 0; bool bVar = false; int32 Plot = -1; FString Note; int32 Line = 0; bool bSquare = false; };
	struct FZone { FString Name; bool bOn = false; float Density = 0.f; TArray<TPair<FString, float>> Mix; int32 Seed = 0; EFacing Facing = EFacing::Random; float Trees = 0.f; };
	struct FTreeRow { FString Site, Class, Note; float E = 0.f, N = 0.f, Radius = 150.f; };

	struct FData
	{
		TMap<FString, FKind> Kinds;      // (FString keys: case-insensitive)
		TMap<FString, FSet> Sets;
		TArray<FOakRow> Oak;
		TMap<FString, FZone> Zones;
		TArray<FTreeRow> Trees;
		FString Folder;                  // where the files were read
		TArray<FString> Notes;           // what was missing or odd (logged)
		bool bKindsCm = false;           // (FillKinds.csv's sizes turned out to be centimetres)
	};

	// Reads the five files from Content/Data (or -FillData=<folder>, test builds only). Missing files are only notes: the fill places
	// what it can (no sets: nothing).
	void LoadData(FData& Out);
}

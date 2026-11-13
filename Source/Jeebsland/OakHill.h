#pragma once

#include "CoreMinimal.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "WorldMap.h"

// Oak Hill (2026-11-11): the town's own land, roads and building plots, traced from Docs/OakHillPlan.md (the owner's approved
// plan, drawn over Docs/OakHillSketch.webp). Header only, so the ground, the roads, the grass and the trees all read the same
// numbers on every machine. The buildings themselves are placed by AOakHillTown (OakHillTown.cpp).
//
// Coordinates: the plan is in SKETCH UNITS (pixels of the 540 x 800 sketch, north up). One unit is Unit metres (17.5 cm since 2026-11-12, 25 before: the owner asked for the town at about half size, so it covers half the area; the models keep their real size, and the plots were hand-settled for it with an offline map, so the spacing pass below has little left to do). In metres from the
// plaza's centre: east e = (px - 255) * Unit, north n = (410 - py) * Unit. Game cm: X = PosX + n * 100, Y = PosY + e * 100.
// Rotations are degrees clockwise as seen on the north-up map, which is Unreal's yaw.
namespace JeebOakHill
{
	// Off only for comparison pictures: -NoOakHill with -JeebShot (never in a shipped game). Every part of the town reads this.
	// Frame-rate tests only (-JeebShot, never Shipping): -NoOakGrass leaves out the town's meadow grass, -NoOakMeshes its
	// building models (their walls stay); neither changes anything shared
	inline bool TestOff(const TCHAR* Switch)
	{
#if UE_BUILD_SHIPPING
		return false;
#else
		return FParse::Param(FCommandLine::Get(), Switch) && FParse::Param(FCommandLine::Get(), TEXT("JeebShot"));
#endif
	}
	inline bool Enabled()
	{
#if UE_BUILD_SHIPPING
		return true;
#else
		static const bool bOn = !(FParse::Param(FCommandLine::Get(), TEXT("NoOakHill")) && FParse::Param(FCommandLine::Get(), TEXT("JeebShot")));
		return bOn;
#endif
	}

	// Metres per sketch unit (the layout's scale), and how much of the plan's hill heights above the 4.5 m base is kept (the
	// hills got narrower with the layout, so their rise is cut too, or the roads over them would be twice as steep)
	constexpr float Unit = 0.175f, UnitCm = Unit * 100.f, HillRise = 0.75f;
	constexpr float PosX = -3550.f, PosY = 18290.f;   // the plaza's centre (JeebWorld::Towns() "Oak Hill", FromSketch(543, 875))
	inline FVector2D FromPx(float Px, float Py) { return FVector2D(PosX + (410.f - Py) * UnitCm, PosY + (Px - 255.f) * UnitCm); }
	// Where new players, everyone after a death, and (while [Jeebsland.Testing] StartAtOakHill is on) every login appear (owner, 2026-11-12):
	// on the plaza 8 m west of the big oak (between its lawn and the ring road), facing it (the well is on its southeast side)
	constexpr float SpawnX = PosX, SpawnY = PosY - 800.f, SpawnYaw = 90.f;
	inline float EastM(float Y) { return (Y - PosY) / 100.f; }
	inline float NorthM(float X) { return (X - PosX) / 100.f; }

	// Everything the town changes lies in this box (cm). Outside it nothing here costs more than this test.
	constexpr float BoxX0 = -16000.f, BoxX1 = 8800.f, BoxY0 = 10400.f, BoxY1 = 27200.f;
	inline bool InBox(float X, float Y, float Pad = 0.f) { return X > BoxX0 - Pad && X < BoxX1 + Pad && Y > BoxY0 - Pad && Y < BoxY1 + Pad; }
	// The part of the box outside the original square round the start: AOakHillTown draws its ground there on a 2 m grid
	// (the square's own ground mesh, GrassField, already covers the rest), and the world's 8 m mesh leaves a hole for it.
	constexpr float PatchX0 = -16000.f, PatchX1 = 8800.f, PatchY0 = 16000.f, PatchY1 = 27200.f;
	inline bool InPatchCell(float X0, float Y0, float Step) { return X0 >= PatchX0 && X0 + Step <= PatchX1 && Y0 >= PatchY0 && Y0 + Step <= PatchY1; }

	// ------------------------------------------------------------------ the ground (metres above the river's surface)
	// The base: about 4.5 m, easing down to 2 m on the bank of the main river (west) and of its east branch (south)
	inline float BaseM(float E, float N)
	{
		const float W = JeebWorld::Smooth(-52.f, -28.f, E), S = JeebWorld::Smooth(-92.f, -70.f, N);
		return 2.f + 2.5f * FMath::Min(W, S);
	}
	// Soft hills on the base, each an ellipse (sketch units) that is level on top out to Rin and eases down to the base by Rout
	// (both in multiples of the ellipse's size). The plan's contour line for each sits about at the ellipse itself (r = 1).
	struct FBump { float Px, Py, Rx, Ry, Rot, Top, Rin, Rout; };
	inline const TArray<FBump>& Bumps()
	{
		static const TArray<FBump> B = {
			{ 255.f, 405.f, 110.f, 100.f, 0.f,  6.4f, 0.5f, 2.2f },   // the plaza, just over 6 m
			{ 240.f, 95.f,  95.f,  55.f,  20.f, 9.0f, 0.3f, 2.6f },   // the castle knoll, about 9 m
			{ 495.f, 140.f, 65.f,  85.f,  15.f, 6.8f, 0.4f, 1.9f },   // the church's own rise
		};
		return B;
	}
	// A point in a shape's own frame (metres): undo the clockwise rotation
	inline FVector2D Local(float E, float N, float CE, float CN, float RotDeg)
	{
		const float R = FMath::DegreesToRadians(RotDeg), C = FMath::Cos(R), S = FMath::Sin(R);
		const float DE = E - CE, DN = N - CN;
		return FVector2D(DE * C - DN * S, DE * S + DN * C);
	}
	inline float SmoothMax(float A, float B, float K)
	{
		const float H = FMath::Max(K - FMath::Abs(A - B), 0.f) / K;
		return FMath::Max(A, B) + H * H * K * 0.25f;
	}
	// The hills before the building plots are levelled
	inline float HillsM(float E, float N)
	{
		float Up = 0.f;
		for (const FBump& B : Bumps())
		{
			const FVector2D L = Local(E, N, (B.Px - 255.f) * Unit, (410.f - B.Py) * Unit, B.Rot);
			const float R = FMath::Sqrt(FMath::Square(L.X / (B.Rx * Unit)) + FMath::Square(L.Y / (B.Ry * Unit)));
			const float Rise = HillRise * (B.Top - 4.5f) * (1.f - JeebWorld::Smooth(B.Rin, B.Rout, R));
			Up = SmoothMax(Up, Rise, 0.4f);
		}
		return BaseM(E, N) + Up;
	}

	// ------------------------------------------------------------------ the building models
	// Each Meshy model in /Game/Buildings: drawn Scale times its size (Tools/building_clean.py sets that size, by its X), its
	// footprint in metres as drawn (its own X and Y), whether its long side is its Y, and which way its door faces in its own
	// frame (yaw: 90 = its +Y; every model so far has it there, checked from four sides in Blender 2026-11-11).
	// Walls: the foundation's rectangle at foot height (cm, the model's own frame before Scale; the BUILD line's "walls x / y" from
	// Tools/building_clean.py, the 1st..99th percentile of the points 15-45 cm up). The solid box follows it, so the knight walks
	// right up to the stone (owner, 2026-11-12: the old box, 86% of the whole model's bounds, held him half a metre off).
	struct FModel { const TCHAR* Name; float Scale; float LenX, LenY; bool bLongY; float DoorYaw; float WX0 = 0.f, WX1 = 0.f, WY0 = 0.f, WY1 = 0.f; };
	inline const TArray<FModel>& Models()
	{
		static const TArray<FModel> M = {
			{ TEXT("OakHillHouse"),       1.2f, 14.4f, 10.7f, false, 90.f, -597.2f, 563.1f, -293.5f, 399.2f },   // (the 12 m model drawn at 14.4 m so the door clears the knight)
			{ TEXT("OakHillCornerHouse"), 1.2f, 10.8f, 11.8f, false, 90.f, -410.7f, 335.6f, -459.7f, 432.6f },
			{ TEXT("OakHillSmith"),       1.f,  15.0f, 11.4f, false, 90.f, -709.2f, 700.1f, -538.4f, 523.1f },
			{ TEXT("OakHillBank"),        1.f,  13.4f, 13.9f, false, 90.f, -620.3f, 622.4f, -675.4f, 638.2f },
			{ TEXT("OakHillStore"),       1.f,  12.5f, 13.4f, false, 90.f, -610.5f, 568.5f, -582.7f, 543.3f },
			{ TEXT("OakHillPotionShop"),  1.f,  12.0f, 9.6f,  false, 90.f, -464.9f, 528.0f, -410.9f, 403.3f },
			{ TEXT("OakHillTownHall"),    1.f,  21.0f, 12.1f, false, 90.f, -989.1f, 985.8f, -580.0f, 562.0f },
			{ TEXT("OakHillChurch"),      1.f,  13.3f, 15.1f, true,  90.f, -591.5f, 573.5f, -709.2f, 723.0f },   // (its door is on a short end)
			{ TEXT("OakHillWell"),        1.f,  3.4f,  3.4f,  false, 90.f, -151.8f, 121.5f, -117.9f, 117.6f },
			// The general store (owner, 2026-11-12: candidate B, an open-fronted timber trading post; its front is -Y in Blender, which is +Y here: Unreal mirrors Blender's Y; added as a stall beside the shops). Its Y
			// here is the stall's depth plus StoreYard metres front and back, so the spacing pass keeps room behind it for the
			// barrels, crates and cart (AOakHillTown::BuildStoreClutter).
			{ TEXT("GeneralStore"),       1.f,  11.0f, 7.7f + 2.f * 3.f, true,  90.f,  -533.0f, 526.0f, -374.8f, 376.8f },
			// Two more stalls (owner, 2026-11-12), the potion shop with 1.5 m of yard front and back for its clutter, the bank 0.5 m: the bank (Bank_A repainted to
			// the town; open front +Y in Blender, -Y here) and the potion shop (PotionShop_A, stretched deeper and lower; open front -Y
			// in Blender, +Y here; its back is open too, so its clutter makes the back wall)
			{ TEXT("BankStall"),          1.f,  11.0f, 9.45f + 2.f * 0.5f, false, -90.f, -508.4f, 524.3f, -459.6f, 265.1f },
			{ TEXT("PotionStall"),        1.f,  11.0f, 7.45f + 2.f * 1.5f, false, 90.f,  -525.7f, 530.3f, -283.3f, 293.4f },
		};
		return M;
	}
	constexpr float StoreYard = 3.f, StoreDepth = 7.7f;
	inline const FModel* FindModel(const TCHAR* Name)
	{
		for (const FModel& M : Models()) if (FCString::Strcmp(M.Name, Name) == 0) return &M;
		return nullptr;
	}

	// ------------------------------------------------------------------ plots
	// Kind: a building (Model names /Game/Buildings/SM_<Model>), or a space kept empty for something not made yet.
	enum class EPlot : uint8 { Building, Empty, EmptyRound, Lawn };
	struct FPlot
	{
		const TCHAR* Name; const TCHAR* Model; float Px, Py, W, H, Rot; EPlot Kind; bool bLevel;
		float Ce = 0.f, Cn = 0.f, Cs = 1.f, Sn = 0.f, Reach = 0.f;   // (worked out once: its centre in metres, its turn, half its diagonal)
	};
	// Distance (metres) from a point to the nearest road's edge
	inline float RoadClearanceM(float E, float N, FVector2D* OutNearest = nullptr);
	inline float PlotSD(const FPlot& P, float E, float N);
	inline const TArray<FPlot>& Plots()
	{
		static const TArray<FPlot> P = []()
		{
			TArray<FPlot> A;
			// (W x H: the plan's footprint, in sketch units, east x north before turning; a building takes its model's real
			// footprint, its long side along the plan's long side)
			auto B = [&](const TCHAR* Name, const TCHAR* Model, float Px, float Py, float W, float H, float Rot)
			{
				if (const FModel* M = FindModel(Model))
				{
					// (the model's long side, its X or its Y, lies along the plot's long side: AOakHillTown turns it so)
					const float Long = (M->bLongY ? M->LenY : M->LenX) / Unit, Short = (M->bLongY ? M->LenX : M->LenY) / Unit;
					if (W >= H) { W = Long; H = Short; } else { W = Short; H = Long; }
				}
				A.Add({ Name, Model, Px, Py, W, H, Rot, EPlot::Building, true });
			};
			// The four shops round the plaza (16 x 11 m)
			B(TEXT("Smith"), TEXT("OakHillSmith"), 183.f, 288.f, 64.f, 44.f, -35.f);
			B(TEXT("Bank"), TEXT("OakHillBank"), 319.f, 292.f, 64.f, 44.f, 0.f);
			B(TEXT("Store"), TEXT("OakHillStore"), 318.f, 521.f, 64.f, 44.f, -25.f);
			B(TEXT("Potion Shop"), TEXT("OakHillPotionShop"), 163.f, 517.f, 64.f, 44.f, 25.f);
			// The Town Hall east of the plaza, its door onto the east road (the plan's 15 x 42 m; the model is about 16 x 28)
			// The market stall (owner, 2026-11-12: the new general store B, added beside the shops, not instead of one), west of the
			// plaza by the west road; three more stalls like it are planned (smithy, bank and potion shop stalls)
			B(TEXT("Market Stall"), TEXT("GeneralStore"), 94.f, 352.f, 50.f, 88.f, 0.f);
			B(TEXT("Potion Stall"), TEXT("PotionStall"), 83.f, 463.f, 64.f, 44.f, 0.f);   // (opposite the market stall, south of the west road)
			B(TEXT("Bank Stall"), TEXT("BankStall"), 379.f, 604.f, 64.f, 44.f, 45.f);   // (by the store, on the southeast road; only 0.5 m of yard, so its clutter stands beside it)
			B(TEXT("Town Hall"), TEXT("OakHillTownHall"), 433.f, 434.f, 64.f, 112.f, 0.f);
			B(TEXT("Church"), TEXT("OakHillChurch"), 528.f, 106.f, 72.f, 120.f, 15.f);
			// The well on the plaza lawn, beside the big oak
			B(TEXT("Well"), TEXT("OakHillWell"), 282.f, 435.f, 16.f, 16.f, 20.f);
			// 16 houses (14.4 x 9 m): the first design, and the corner house where a house stands at a corner of the roads
			struct FH { float Px, Py, Rot; bool bCorner; };
			const FH Houses[] = {
				{ 97.f, 93.f, -15.f, false }, { 416.f, 117.f, 10.f, false }, { 575.f, 462.f, 0.f, false }, { 101.f, 227.f, 5.f, true },
				{ 503.f, 195.f, 15.f, false }, { 434.f, 282.f, 25.f, true }, { 577.f, 366.f, 25.f, false },   // (the plan's house at 315,280 is left out: the real bank and house overlapped there, with roads on both sides, 2026-11-12)
				{ 13.f, 357.f, 0.f, true },
				{ 598.f, 261.f, -45.f, true }, { 59.f, 546.f, 5.f, false }, { 58.f, 761.f, 0.f, false }, { 157.f, 690.f, 0.f, false },
				{ 306.f, 692.f, 5.f, true }, { 506.f, 680.f, 20.f, false }, { 335.f, 785.f, 10.f, true }, { 515.f, 570.f, 30.f, false },
			};
			for (const FH& H : Houses) B(TEXT("House"), H.bCorner ? TEXT("OakHillCornerHouse") : TEXT("OakHillHouse"), H.Px, H.Py, 58.f, 36.f, H.Rot);
			// Kept empty for what isn't made yet: grass grows there, but no trees
			auto E = [&](const TCHAR* Name, float Px, float Py, float W, float H, float Rot, EPlot K, bool bLevel) { A.Add({ Name, nullptr, Px, Py, W, H, Rot, K, bLevel }); };
			E(TEXT("Boxing Ring"), 255.f, 610.f, 64.f, 72.f, 0.f, EPlot::Empty, true);
			E(TEXT("Castle Ruins"), 240.f, 95.f, 128.f, 64.f, 20.f, EPlot::Empty, false);
			E(TEXT("Garden"), 500.f, 440.f, 36.f, 152.f, 0.f, EPlot::Empty, false);
			E(TEXT("Graveyard"), 470.f, 735.f, 120.f, 120.f, 0.f, EPlot::EmptyRound, false);
			E(TEXT("North Fort"), 5.f, 246.f, 44.f, 104.f, 10.f, EPlot::Empty, false);
			E(TEXT("South Fort"), -24.f, 574.f, 44.f, 104.f, 0.f, EPlot::Empty, false);
			E(TEXT("Watchtower"), 140.f, 195.f, 40.f, 40.f, 0.f, EPlot::EmptyRound, false);
			E(TEXT("Bridge"), -102.f, 410.f, 143.f, 34.f, 0.f, EPlot::Empty, false);
			E(TEXT("Oak Lawn"), 255.f, 410.f, 84.f, 84.f, 0.f, EPlot::Lawn, false);
			// The real models are a little bigger than some plan boxes: a building that would stand on a road steps back from it
			// until its whole footprint is at least 0.5 m clear (1 m before the town was shrunk), and buildings closer than BuildingGap metres wall to wall (two
			// houses north of the plaza read as one lump, 2026-11-12) push each other apart, half each (the well stays put).
			// The two rules take turns until both hold (the JEEBOAK log gives each building's final sketch spot).
			auto Fill = [](FPlot& Pl)
			{
				Pl.Ce = (Pl.Px - 255.f) * Unit; Pl.Cn = (410.f - Pl.Py) * Unit;
				Pl.Cs = FMath::Cos(FMath::DegreesToRadians(Pl.Rot)); Pl.Sn = FMath::Sin(FMath::DegreesToRadians(Pl.Rot));
				Pl.Reach = 0.5f * Unit * FMath::Sqrt(Pl.W * Pl.W + Pl.H * Pl.H);
			};
			auto Outline = [](const FPlot& Pl, int32 k)
			{
				const float U = (k % 6) / 5.f * 2.f - 1.f;
				const int32 Side = k / 6;
				const FVector2D L = Side == 0 ? FVector2D(U, 1.f) : Side == 1 ? FVector2D(U, -1.f) : Side == 2 ? FVector2D(1.f, U) : FVector2D(-1.f, U);
				const FVector2D Lm(L.X * Pl.W * 0.5f * Unit, L.Y * Pl.H * 0.5f * Unit);
				return FVector2D(Pl.Ce, Pl.Cn) + FVector2D(Lm.X * Pl.Cs + Lm.Y * Pl.Sn, -Lm.X * Pl.Sn + Lm.Y * Pl.Cs);
			};
			constexpr float BuildingGap = 3.f;
			for (int32 Round = 0; Round < 40; ++Round)
			{
				bool bMoved = false;
				for (FPlot& Pl : A) Fill(Pl);
				for (int32 i = 0; i < A.Num(); ++i)
					for (int32 j = i + 1; j < A.Num(); ++j)
					{
						FPlot& Pa = A[i]; FPlot& Pb = A[j];
						if (Pa.Kind != EPlot::Building || Pb.Kind != EPlot::Building) continue;
						if (FVector2D::Distance(FVector2D(Pa.Ce, Pa.Cn), FVector2D(Pb.Ce, Pb.Cn)) > Pa.Reach + Pb.Reach + BuildingGap) continue;
						float Gap = 1.0e9f;
						for (int32 k = 0; k < 24; ++k) { const FVector2D Q = Outline(Pa, k); Gap = FMath::Min(Gap, PlotSD(Pb, Q.X, Q.Y)); }
						for (int32 k = 0; k < 24; ++k) { const FVector2D Q = Outline(Pb, k); Gap = FMath::Min(Gap, PlotSD(Pa, Q.X, Q.Y)); }
						if (Gap >= BuildingGap) continue;
						// (only the one further from the plaza moves, straight out from it, so the town keeps its plan's arrangement and
						// grows outward ring by ring instead of shops trading sides; the well never moves)
						const bool bWellA = FCString::Strcmp(Pa.Model, TEXT("OakHillWell")) == 0, bWellB = FCString::Strcmp(Pb.Model, TEXT("OakHillWell")) == 0;
						const bool bMoveB = bWellA || (!bWellB && FVector2D(Pb.Ce, Pb.Cn).SizeSquared() >= FVector2D(Pa.Ce, Pa.Cn).SizeSquared());
						FPlot& Mv = bMoveB ? Pb : Pa;
						const FVector2D Out = FVector2D(Mv.Ce, Mv.Cn).GetSafeNormal();
						const float Push = (BuildingGap - Gap) + 0.3f;
						// (metres east / north to sketch units: east is +Px, north is -Py)
						Mv.Px += Out.X * Push / Unit; Mv.Py -= Out.Y * Push / Unit;
						Fill(Pa); Fill(Pb);
						bMoved = true;
					}
				for (FPlot& Pl : A)
				{
				if (Pl.Kind != EPlot::Building) continue;
				for (int32 Iter = 0; Iter < 12; ++Iter)
				{
					const FVector2D C((Pl.Px - 255.f) * Unit, (410.f - Pl.Py) * Unit);
					const float R = FMath::DegreesToRadians(Pl.Rot), Cs = FMath::Cos(R), Sn = FMath::Sin(R);
					float Worst = 1.0e9f; FVector2D WorstAt = C, Near = C;
					for (int32 k = 0; k < 24; ++k)
					{
						// round the outline: corners and the middle of each side (local east, north), then turned clockwise
						const float U = (k % 6) / 5.f * 2.f - 1.f;
						const int32 Side = k / 6;
						const FVector2D L = Side == 0 ? FVector2D(U, 1.f) : Side == 1 ? FVector2D(U, -1.f) : Side == 2 ? FVector2D(1.f, U) : FVector2D(-1.f, U);
						const FVector2D Lm(L.X * Pl.W * 0.5f * Unit, L.Y * Pl.H * 0.5f * Unit);
						const FVector2D Q = C + FVector2D(Lm.X * Cs + Lm.Y * Sn, -Lm.X * Sn + Lm.Y * Cs);
						FVector2D Nr;
						const float D = RoadClearanceM(Q.X, Q.Y, &Nr);
						if (D < Worst) { Worst = D; WorstAt = Q; Near = Nr; }
					}
					if (Worst >= 0.5f) break;
					const FVector2D Away = (WorstAt - Near).GetSafeNormal();
					const float Move = (0.5f - Worst) + 0.2f;
					Pl.Px += Away.X * Move / Unit;
					Pl.Py -= Away.Y * Move / Unit;
					bMoved = true;
				}
				}
				if (!bMoved) break;
			}
			for (FPlot& Pl : A)
			{
				Pl.Ce = (Pl.Px - 255.f) * Unit; Pl.Cn = (410.f - Pl.Py) * Unit;
				Pl.Cs = FMath::Cos(FMath::DegreesToRadians(Pl.Rot)); Pl.Sn = FMath::Sin(FMath::DegreesToRadians(Pl.Rot));
				Pl.Reach = 0.5f * Unit * FMath::Sqrt(Pl.W * Pl.W + Pl.H * Pl.H);
			}
			return A;
		}();
		return P;
	}
	inline FVector2D PlotCentreM(const FPlot& P) { return FVector2D(P.Ce, P.Cn); }
	// Signed distance (metres) from a point to a plot's outline, negative inside
	inline float PlotSD(const FPlot& P, float E, float N)
	{
		const float DE = E - P.Ce, DN = N - P.Cn;
		if (P.Kind == EPlot::EmptyRound || P.Kind == EPlot::Lawn) return FMath::Sqrt(DE * DE + DN * DN) - P.W * 0.5f * Unit;
		const FVector2D L(DE * P.Cs - DN * P.Sn, DE * P.Sn + DN * P.Cs);
		const FVector2D Q(FMath::Abs(L.X) - P.W * 0.5f * Unit, FMath::Abs(L.Y) - P.H * 0.5f * Unit);
		return FVector2D(FMath::Max(Q.X, 0.f), FMath::Max(Q.Y, 0.f)).Size() + FMath::Min(FMath::Max(Q.X, Q.Y), 0.f);
	}

	// Each building's plot is levelled to the hills' height at its middle: flat 1.5 m round the walls, easing back over at least
	// 6 m, and over PadSpread times the step where the hill is higher or lower than the pad, so no blend is steeper than about
	// 8 degrees (a fixed 6 m made 36 degree banks on the knoll's flank, 2026-11-12). Pads are mixed by weights that grow without
	// end on a flat (w / (1 - w)), so where two blends overlap the result is smooth and every flat stays exactly level.
	// Neighbouring plots' levels are pulled together until the step between them is at most 0.3 m plus PadStep per metre of
	// gap (two houses 2 m apart were 2.2 m apart in height, a 42 degree bank).
	constexpr float PadFlat = 1.5f, PadBlend = 6.f, PadSpread = 6.f, PadMaxBlend = 20.f, PadStep = 0.15f;
	// The pads on top of Ground (metres above the river: the hills, already faded into the world near the rivers)
	inline float TownM(float E, float N, float Ground)
	{
		static const TArray<float> Levels = []()
		{
			const TArray<FPlot>& Ps = Plots();
			TArray<float> L;
			for (const FPlot& P : Ps) { const FVector2D C = PlotCentreM(P); L.Add(HillsM(C.X, C.Y)); }
			for (int32 Pass = 0; Pass < 30; ++Pass)
				for (int32 i = 0; i < Ps.Num(); ++i)
					for (int32 j = i + 1; j < Ps.Num(); ++j)
					{
						if (!Ps[i].bLevel || !Ps[j].bLevel) continue;
						const float Gap = FMath::Max(FVector2D::Distance(PlotCentreM(Ps[i]), PlotCentreM(Ps[j])) - Ps[i].Reach - Ps[j].Reach, 0.f);
						if (Gap > 2.f * (PadFlat + PadMaxBlend)) continue;
						const float Allowed = 0.15f + PadStep * Gap, Diff = L[i] - L[j];
						if (FMath::Abs(Diff) <= Allowed) continue;
						const float Move = 0.5f * (FMath::Abs(Diff) - Allowed) * FMath::Sign(Diff);
						L[i] -= Move; L[j] += Move;
					}
			return L;
		}();
		const float Hills = Ground;
		float Sum = Hills, Weight = 1.f;
		const TArray<FPlot>& Ps = Plots();
		for (int32 i = 0; i < Ps.Num(); ++i)
		{
			if (!Ps[i].bLevel) continue;
			const float Blend = FMath::Clamp(PadSpread * FMath::Abs(Hills - Levels[i]), PadBlend, PadMaxBlend);
			if (FMath::Square(E - Ps[i].Ce) + FMath::Square(N - Ps[i].Cn) > FMath::Square(Ps[i].Reach + PadFlat + Blend)) continue;   // (quick: far from it)
			const float D = PlotSD(Ps[i], E, N);
			if (D >= PadFlat + Blend) continue;
			const float W = 1.f - JeebWorld::Smooth(PadFlat, PadFlat + Blend, D);
			const float Odds = W / FMath::Max(1.f - W, 1e-5f);
			Sum += Odds * Levels[i]; Weight += Odds;
		}
		return Sum / Weight;
	}

	// How much of the town's own shape a point takes (0 .. 1): all of it in the town, fading out over 15 m round it, and none
	// in or right beside the rivers (so their channels stay as they are) or across the main river. The hills fade into the
	// world's banks over Fade cm (10 m, so roads run down to the water at a walk: 3 m made 18 degree road ends); the pads over
	// the old 3 m, so a riverside floor stays level.
	inline float Mask(float E, float N, float RiverD, float RiverHalf, float Fade)
	{
		return JeebWorld::Smooth(-62.f, -56.f, E) * JeebWorld::Smooth(87.f, 72.f, E) * JeebWorld::Smooth(-115.f, -100.f, N) * JeebWorld::Smooth(120.f, 105.f, N)
			* JeebWorld::Smooth(RiverHalf + 150.f, RiverHalf + 150.f + Fade, RiverD);
	}
	// The ground (cm), given the world's height there and the distance to the nearest river (from JeebWorld::SeaAndRivers)
	inline float Apply(float X, float Y, float H, float RiverD, float RiverHalf)
	{
		if (!InBox(X, Y) || !Enabled()) return H;
		const float E = EastM(Y), N = NorthM(X);
		const float MHills = Mask(E, N, RiverD, RiverHalf, 1000.f);
		if (MHills <= 0.f) return H;
		const float Ground = FMath::Lerp(H, JeebWorld::RiverLevel + HillsM(E, N) * 100.f, MHills);
		const float MPads = Mask(E, N, RiverD, RiverHalf, 300.f);
		if (MPads <= 0.f) return Ground;
		return FMath::Lerp(Ground, JeebWorld::RiverLevel + TownM(E, N, (Ground - JeebWorld::RiverLevel) / 100.f) * 100.f, MPads);
	}

	// ------------------------------------------------------------------ roads (3.5 m wide village lanes since the town was shrunk; 5 m before), the plan's paths
	constexpr float RoadWidth = 350.f;
	inline TArray<FVector2D> ParsePath(const TCHAR* S)
	{
		// M x y, L x y and C x1 y1 x2 y2 x y, in sketch units; returns dense game points about every 60 cm
		TArray<FVector2D> Out;
		TArray<float> Num;
		TCHAR Cmd = 0;
		FVector2D Cur(0.f, 0.f);
		auto Emit = [&]()
		{
			if (Cmd == 'M' && Num.Num() >= 2) { Cur = FVector2D(Num[0], Num[1]); Out.Add(FromPx(Cur.X, Cur.Y)); }
			else if (Cmd == 'L' && Num.Num() >= 2)
			{
				const FVector2D To(Num[0], Num[1]);
				const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(Cur, To) * UnitCm / 60.f));
				for (int32 s = 1; s <= Steps; ++s) { const FVector2D Q = FMath::Lerp(Cur, To, float(s) / Steps); Out.Add(FromPx(Q.X, Q.Y)); }
				Cur = To;
			}
			else if (Cmd == 'C' && Num.Num() >= 6)
			{
				const FVector2D P0 = Cur, P1(Num[0], Num[1]), P2(Num[2], Num[3]), P3(Num[4], Num[5]);
				const float Len = 0.5f * (FVector2D::Distance(P0, P3) + FVector2D::Distance(P0, P1) + FVector2D::Distance(P1, P2) + FVector2D::Distance(P2, P3));
				const int32 Steps = FMath::Max(2, FMath::CeilToInt(Len * UnitCm / 60.f));
				for (int32 s = 1; s <= Steps; ++s)
				{
					const float T = float(s) / Steps, U = 1.f - T;
					const FVector2D Q = U * U * U * P0 + 3.f * U * U * T * P1 + 3.f * U * T * T * P2 + T * T * T * P3;
					Out.Add(FromPx(Q.X, Q.Y));
				}
				Cur = P3;
			}
			Num.Reset();
		};
		for (const TCHAR* C = S; *C; )
		{
			if (FChar::IsAlpha(*C)) { Emit(); Cmd = *C; ++C; continue; }
			if (FChar::IsDigit(*C) || *C == '-' || *C == '.')
			{
				const TCHAR* Start = C;
				++C;
				while (*C && (FChar::IsDigit(*C) || *C == '.')) ++C;
				Num.Add(FCString::Atof(*FString::ConstructFromPtrSize(Start, int32(C - Start))));
				// (a C with more than one curve's numbers carries on as another C from where the last one ended)
				if (Cmd == 'C' && Num.Num() == 6) Emit();
				else if ((Cmd == 'L' || Cmd == 'M') && Num.Num() == 2) { const TCHAR Was = Cmd; Emit(); if (Was == 'M') Cmd = 'L'; }
				continue;
			}
			++C;
		}
		Emit();
		return Out;
	}
	struct FRoad { TArray<FVector2D> Pts; bool bClosed = false; };
	inline const TArray<FRoad>& Roads()
	{
		static const TArray<FRoad> R = []()
		{
			TArray<FRoad> A;
			if (!Enabled()) return A;
			// The ring road round the plaza (centreline radius RingR units: 8 m, its inside edge 6 m from the oak)
			{
				FRoad Ring; Ring.bClosed = true;
				constexpr float RingR = 64.f;
				const int32 Pts = FMath::CeilToInt(2.f * PI * RingR * UnitCm / 60.f);
				for (int32 i = 0; i < Pts; ++i) { const float A0 = 2.f * PI * i / Pts; Ring.Pts.Add(FromPx(255.f + RingR * FMath::Cos(A0), 410.f + RingR * FMath::Sin(A0))); }
				A.Add(Ring);
			}
			const TCHAR* Paths[] = {
				TEXT("M-31 410 L191 410"),   // west, toward the bridge (not built yet: the road stops at the bank; drawn on to the river since the town was halved)
				TEXT("M319 410 L380 410"),   // east, to the Town Hall's door
				TEXT("M252 352 C 250 300 262 240 295 190 C 330 135 365 80 395 0"),   // north, and its branch past the castle ruins
				TEXT("M-81 81 L20 180 C 70 140 150 135 220 150 C 255 158 280 172 300 185 C 360 190 450 220 545 295 L590 322"),   // the north arc
				TEXT("M385 206 C 385 260 375 300 372 340 L372 494 C 372 560 440 610 545 640 L590 650"),   // behind the bank, then southeast
				TEXT("M255 478 L255 574"),   // south, to the boxing ring
				TEXT("M250 505 C 215 560 150 620 20 700 L-81 824"),   // southwest, to the river fork (drawn on to the bank)
				TEXT("M257 545 C 270 600 330 630 425 698"),   // southeast, to the graveyard
			};
			for (const TCHAR* S : Paths)
			{
				FRoad Rd; Rd.Pts = ParsePath(S);
				// a road that the plan draws to the river's edge stops on the bank, not in the water
				auto Wet = [](const FVector2D& Q) { float Half; return JeebWorld::RiverDistance(Q.X, Q.Y, Half) < Half + 300.f; };
				while (Rd.Pts.Num() > 2 && Wet(Rd.Pts[0])) Rd.Pts.RemoveAt(0);
				while (Rd.Pts.Num() > 2 && Wet(Rd.Pts.Last())) Rd.Pts.Pop();
				if (Rd.Pts.Num() >= 2) A.Add(Rd);
			}
			return A;
		}();
		return R;
	}

	inline float RoadClearanceM(float E, float N, FVector2D* OutNearest)
	{
		const FVector2D P(PosX + N * 100.f, PosY + E * 100.f);
		float Best = 1.0e12f; FVector2D At = P;
		for (const FRoad& R : Roads())
			for (const FVector2D& Q : R.Pts) { const float D = FVector2D::DistSquared(P, Q); if (D < Best) { Best = D; At = Q; } }
		if (OutNearest) *OutNearest = FVector2D(EastM(At.Y), NorthM(At.X));
		return (FMath::Sqrt(Best) - RoadWidth * 0.5f) / 100.f;
	}

	// ------------------------------------------------------------------ what grass and other code ask
	// Inside a building's walls (no grass, no leaves), with a margin in metres
	inline bool OnBuilding(float X, float Y, float MarginM = 0.f)
	{
		if (!InBox(X, Y) || !Enabled()) return false;
		const float E = EastM(Y), N = NorthM(X);
		for (const FPlot& P : Plots()) if (P.Kind == EPlot::Building && PlotSD(P, E, N) < MarginM) return true;
		return false;
	}
	// Distance (cm) to the nearest building's walls, for the shorter grass beside buildings (1e6 far from town)
	inline float BuildingDistance(float X, float Y)
	{
		if (!InBox(X, Y, 1000.f) || !Enabled()) return 1.0e6f;
		const float E = EastM(Y), N = NorthM(X);
		float D = 1.0e6f;
		for (const FPlot& P : Plots()) if (P.Kind == EPlot::Building) D = FMath::Min(D, PlotSD(P, E, N) * 100.f);
		return D;
	}
	// Where the town's grass grows (the meadow's cells and tufts): the town and its fringe, east of the main river
	inline bool GrassArea(float X, float Y, float PadCm = 0.f)
	{
		static const bool bNoGrass = TestOff(TEXT("NoOakGrass"));
		if (!Enabled() || bNoGrass) return false;
		const float E = EastM(Y), N = NorthM(X), P = PadCm / 100.f;
		return E > -60.f - P && E < 86.f + P && N > -118.f - P && N < 120.f + P;
	}
}

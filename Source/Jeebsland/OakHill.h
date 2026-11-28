#pragma once

#include "CoreMinimal.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "WorldMap.h"
#include "OakHillFootprints.h"

// Oak Hill (2026-11-11): the town's own land, roads and building plots, traced from Docs/OakHillPlan.md (the owner's approved
// plan, drawn over Docs/OakHillSketch.webp). Header only, so the ground, the roads, the grass and the trees all read the same
// numbers on every machine. The buildings themselves are placed by AOakHillTown (OakHillTown.cpp).
//
// Coordinates: the plan is in SKETCH UNITS (pixels of the 540 x 800 sketch, north up). One unit is Unit metres (14 cm since 2026-11-13, 17.5 on 2026-11-12, 25 before: the owner asked twice for the town closer together; the models are drawn at their own Scale (Models()), and the plots were hand-settled for it with an offline map, so the spacing pass below has little left to do). In metres from the
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

	// Metres per sketch unit (the layout's scale), and how much of the plan's hill heights above the 4.5 m base is kept: 0.75 when
	// the town was halved (2026-11-12), twice that since the owner asked for it "twice as hilly" (2026-11-13). Roads stay under
	// about 12 degrees because the west side's base slopes down to the river over a longer way (BaseM) and the plots were settled for it
	constexpr float Unit = 0.14f, UnitCm = Unit * 100.f, HillRise = 1.5f;
	constexpr float PosX = -3550.f, PosY = 18290.f;   // the plaza's centre (JeebWorld::Towns() "Oak Hill", FromSketch(543, 875))
	inline FVector2D FromPx(float Px, float Py) { return FVector2D(PosX + (410.f - Py) * UnitCm, PosY + (Px - 255.f) * UnitCm); }
	// The ring road round the plaza (centreline radius, sketch units: 14 m since 2026-11-13, so the seven shops fit round it facing
	// the oak), the roads' width (3.5 m village lanes), and the oak's lawn (radius, sketch units: 8.4 m)
	constexpr float RingR = 100.f, RoadWidth = 350.f, LawnR = 60.f;
	// Where new players, everyone after a death, and (while [Jeebsland.Testing] StartAtOakHill is on) every login appear (owner, 2026-11-12):
	// on the plaza 10 m west of the big oak (between its lawn and the ring road), facing it (the well is on its southeast side)
	constexpr float SpawnX = PosX, SpawnY = PosY - 1000.f, SpawnYaw = 90.f;
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
	// The base: about 4.5 m, easing down to 2 m on the bank of the main river (west) and of its east branch (south). The west slope
	// starts 14 m from the plaza (28 m before 2026-11-13): with the hills twice as high, a short slope put 18 degrees on the west
	// road's last few metres to the water
	inline float BaseM(float E, float N)
	{
		const float W = JeebWorld::Smooth(-54.f, -14.f, E), S = JeebWorld::Smooth(-92.f, -70.f, N);
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
	// footprint in metres as drawn (its own X and Y, so already times Scale), whether its long side is its Y, and which way its
	// door faces in its own frame (yaw: 90 = its +Y; every model so far has it there, checked from four sides in Blender 2026-11-11).
	// Walls: the foundation's rectangle at foot height (cm, the model's own frame before Scale; the BUILD line's "walls x / y" from
	// Tools/building_clean.py, the 1st..99th percentile of the points 15-45 cm up). The solid box follows it, so the knight walks
	// right up to the stone (owner, 2026-11-12: the old box, 86% of the whole model's bounds, held him half a metre off).
	// ScaleZ: the height's scale when it differs from Scale (0: the same).
	// Scales (owner, 2026-11-13: "All these houses look too big ... reduce their sizes by about 20%" and "The shops are way too big,
	// they make the knight look tiny"): every building at least 20% smaller, each shop sized by its door or counter next to the
	// 1.9 m knight (front views with a knight-sized post, measured in Blender): the smith's door 3.5 m as it came, now 2.3; the
	// bank's arched door 2.8, now 2.1; the store's counter 1.2, now 0.9; the potion shop's door 2.3, now 1.9; the market stall's
	// counter 1.4, now 1.0. The house's door is 2.4 m (its model at 1.2 was 2.9); the corner house's door is the smallest, 1.7 m
	// (owner to judge: at the houses' 0.96 it would be 1.6 m, shorter than the knight)
	struct FModel { const TCHAR* Name; float Scale; float LenX, LenY; bool bLongY; float DoorYaw; float WX0 = 0.f, WX1 = 0.f, WY0 = 0.f, WY1 = 0.f; float ScaleZ = 0.f; };
	inline const TArray<FModel>& Models()
	{
		static const TArray<FModel> M = {
			{ TEXT("OakHillHouse"),       0.96f, 11.52f, 8.56f,  false, 90.f, -597.2f, 563.1f, -293.5f, 399.2f },   // (the 12 m model; it was drawn at 1.2x, 14.4 m, until 2026-11-13)
			{ TEXT("OakHillCornerHouse"), 1.0f,  9.0f,   9.83f,  false, 90.f, -410.7f, 335.6f, -459.7f, 432.6f },   // (the 9 m model; 1.2x before)
			{ TEXT("OakHillSmith"),       0.65f, 9.75f,  7.41f,  false, 90.f, -709.2f, 700.1f, -538.4f, 523.1f },
			{ TEXT("OakHillBank"),        0.75f, 10.05f, 10.43f, false, 90.f, -620.3f, 622.4f, -675.4f, 638.2f },
			{ TEXT("OakHillStore"),       0.75f, 9.38f,  10.05f, false, 90.f, -610.5f, 568.5f, -582.7f, 543.3f },
			{ TEXT("OakHillPotionShop"),  0.82f, 9.84f,  7.87f,  false, 90.f, -464.9f, 528.0f, -410.9f, 403.3f },
			{ TEXT("OakHillTownHall"),    0.85f, 17.85f, 10.29f, false, 90.f, -989.1f, 985.8f, -580.0f, 562.0f },
			{ TEXT("OakHillChurch"),      0.85f, 11.31f, 12.84f, true,  90.f, -591.5f, 573.5f, -709.2f, 723.0f },   // (its door is on a short end)
			{ TEXT("OakHillWell"),        0.85f, 2.89f,  2.89f,  false, 90.f, -151.8f, 121.5f, -117.9f, 117.6f },
			// Silver Point's wizard village (2026-11-26, SilverPoint.h; building_clean.py sizes and walls; door to -Y in Blender, +Y here)
			{ TEXT("SilverHome"),        1.0f, 7.2f,  9.82f, false, 90.f, -210.6f, 212.0f, -361.5f, 339.1f },
			{ TEXT("SilverPetShop"),     1.0f, 11.0f, 8.88f, false, 90.f, -424.6f, 485.9f, -227.5f, 372.0f },
			{ TEXT("SilverCrystalShop"), 1.0f, 9.0f,  6.73f, false, 90.f, -281.5f, 337.2f, -239.0f, 238.2f },
			{ TEXT("SilverBroomShop"),   1.0f, 13.0f, 5.56f, false, 90.f, -581.3f, 600.8f, -209.1f, 210.5f },
			{ TEXT("SilverWandShop"),    1.0f, 5.6f,  6.24f, false, 90.f, -188.1f, 191.5f, -158.4f, 162.8f },
			{ TEXT("SilverStore"),       1.0f, 13.0f, 9.46f, false, 90.f, -605.8f, 604.7f, -433.1f, 407.8f },
			{ TEXT("SilverBank"),        1.0f, 9.0f,  7.32f, false, 90.f, -251.1f, 315.5f, -312.7f, 315.9f },
			{ TEXT("SilverTownHall"),    1.0f, 17.0f, 13.6f, false, 90.f, -727.0f, 694.3f, -452.9f, 452.6f },
			{ TEXT("SilverSmith"),       1.0f, 11.0f, 11.8f, false, 90.f, -508.3f, 544.9f, -311.7f, 486.3f },
			{ TEXT("SilverPotionShop"),  1.0f, 9.0f,  8.64f, false, 90.f, -443.7f, 267.8f, -383.6f, 416.3f },
			{ TEXT("SilverTavern"),      1.0f, 15.0f, 9.38f, false, 90.f, -513.3f, 549.5f, -188.1f, 331.4f },
			{ TEXT("SilverAcademy"),     1.0f, 24.0f, 15.4f, false, 90.f, -1148.9f, 1073.5f, -582.8f, 659.0f },
			// Steele Creek's troll town (2026-11-27, Docs/TrollTownPlan.md; building_clean.py: door to -Y in Blender, +Y here)
			{ TEXT("TrollHall"),         1.0f, 10.0f, 15.52f, false, 90.f, -464.2f, 465.1f, -770.9f, 711.6f },
			// the rest of the troll town (2026-11-28, agent trolltown; building_clean.py, door to -Y in Blender)
			{ TEXT("TrollBoneCarver"), 1.0f, 12.00f, 15.15f, false, 90.f, -552.2f, 585.9f, -718.1f, 749.2f },
			{ TEXT("TrollTanner"), 1.0f, 13.00f, 20.40f, false, 90.f, -615.4f, 615.0f, -1015.5f, 961.4f },
			{ TEXT("TrollGrogHall"), 1.0f, 15.00f, 17.64f, false, 90.f, -730.9f, 735.6f, -721.4f, 872.0f },
			{ TEXT("TrollTradingPost"), 1.0f, 12.00f, 18.95f, false, 90.f, -560.1f, 562.1f, -941.9f, 912.4f },
			{ TEXT("TrollBrewHut"), 1.0f, 9.00f, 8.56f, false, 90.f, -397.5f, 397.4f, -329.6f, 411.5f },
			{ TEXT("TrollWarForge"), 1.0f, 14.00f, 13.37f, false, 90.f, -651.8f, 664.3f, -580.8f, 582.6f },
			{ TEXT("TrollFightPit"), 1.0f, 24.00f, 26.63f, false, 90.f, -1163.5f, 1161.4f, -1267.0f, 1285.2f },
			{ TEXT("TrollWatchtower"), 1.0f, 7.00f, 7.19f, false, 90.f, -291.1f, 338.3f, -341.0f, 270.0f },
			{ TEXT("TrollGate"), 1.0f, 16.00f, 10.38f, false, 90.f, -770.5f, 762.3f, -433.6f, 151.5f },
			{ TEXT("TrollPalisade"), 1.0f, 10.00f, 1.57f, false, 90.f, -487.4f, 489.1f, -52.4f, 63.0f },
			{ TEXT("TrollCairn"), 1.0f, 7.00f, 7.16f, false, 90.f, -331.1f, 327.6f, -353.0f, 315.5f },
			{ TEXT("TrollHut"), 1.0f, 9.00f, 13.66f, false, 90.f, -427.7f, 428.1f, -672.5f, 671.4f },
			{ TEXT("TrollCaves"), 1.0f, 16.00f, 16.36f, false, 90.f, -756.4f, 755.7f, -802.8f, 766.2f },
			{ TEXT("TrollVault"), 1.0f, 10.00f, 10.53f, false, 90.f, -496.7f, 496.2f, -509.4f, 509.3f },   // (the troll town's bank)
			// The open-air stalls (owner, 2026-11-12), all three with their open front on their -Y in Blender, which is +Y here
			// (Unreal mirrors Blender's Y). The general store ("Market Stall", candidate B, a timber trading post), the bank (Bank_A
			// repainted to the town) and the potion shop (PotionShop_A, stretched deeper and lower by building_clean.py, its back open).
			// Their old yards for barrels and crates went with the clutter (owner, 2026-11-13: "cut them out and remove them").
			// The potion stall keeps its full height (ScaleZ 1): at 0.8 its 0.75 m counter would be knee high
			{ TEXT("GeneralStore"),       0.7f,  7.7f,   5.39f,  false, 90.f,  -533.0f, 526.0f, -374.8f, 376.8f },
			{ TEXT("BankStall"),          0.7f,  7.7f,   6.62f,  false, 90.f,  -508.4f, 524.3f, -459.6f, 265.1f },
			{ TEXT("PotionStall"),        0.8f,  8.8f,   5.96f,  false, 90.f,  -525.7f, 530.3f, -283.3f, 293.4f, 1.0f },
			// The war buildings (2026-11-18, Meshy, Docs/TownBuildings.md): one fort model used twice, the round watchtower and the
			// castle ruins, drawn at their own size (Scale 1: building_clean.py sized them). Their door (the fort's gate, the tower's door,
			// the ruins' great arch) is on their +Y like the others. They stand on their plots without a level pad (FPlot bLevel false):
			// ATownLabTown sets each on the lowest ground under its walls, its stone footing running into the slope
			// (the fort at 1.4x since the 2026-11-18 review: at 1.0 it read as a fenced pen beside the houses; now 20.3 x 8.2 m, its
			// palisade about 4.6 m and its blockhouses 7.3 m. LenX/LenY are the drawn size, so the plan's spacing pass sees the real one)
			{ TEXT("OakHillFort"),        1.4f,  20.3f,  11.2f,  false, 90.f,  -709.6f, 709.8f, -281.5f, 287.8f },   // (LenY: the walls 8.2 m + a 1.5 m apron each side, so the plot, the tree clearing and the terrain flattening cover the gate's approach; the opened gate, 2026-11-18)
			{ TEXT("OakHillWatchtower"),  1.0f,  5.3f,   5.31f,  false, 90.f,  -235.8f, 236.0f, -234.3f, 234.2f },
			{ TEXT("OakHillRuins"),       1.0f,  19.0f,  10.24f, false, 90.f,  -892.1f, 913.5f, -493.7f, 490.7f },
		};
		return M;
	}
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
		// bAlongW: the model's long side (as FModel says) lies along W, the plot's own east-west before turning (the plan drew the
		// plot wider than tall); it used to be read from W >= H, which turned the nearly square models (bank, store, corner house)
		// 90 degrees off their own box. bFacePlaza: the door faces the oak, not the nearest road (the shops round the plaza)
		bool bAlongW = true, bFacePlaza = false;
		// DoorDir: the way the building's door (or a stall's open front, the forts' gates) faces, a compass bearing in degrees (0 north,
		// 90 east; Unreal's yaw). Set by hand for the 2026-11-18 layout; every other building gets it filled in from BuildingYaw at the
		// end of Plots(), so it is always right (the terrain flattens an apron there; DoorAt gives the spot). -1 only on open plots.
		// Id: a stable name for each building that NPC data and notes can tie to (houses "House_W1"..., "Bank", "Church"; Docs/TownLab/OakHillLayout_2026-11-18.md)
		float DoorDir = -1.f;
		const TCHAR* Id = nullptr;
	};
	// Distance (metres) from a point to the nearest road's edge
	inline float RoadClearanceM(float E, float N, FVector2D* OutNearest = nullptr);
	inline float PlotSD(const FPlot& P, float E, float N);
	inline float BuildingYaw(const FPlot& P, const FModel* Mdl);
	inline const TArray<FPlot>& Plots()
	{
		static const TArray<FPlot> P = []()
		{
			TArray<FPlot> A;
			// (W x H: the plan's footprint, in sketch units, east x north before turning; a building takes its model's real
			// footprint, its long side along the plan's long side)
			auto B = [&](const TCHAR* Name, const TCHAR* Model, float Px, float Py, float W, float H, float Rot)
			{
				FPlot Pt{ Name, Model, Px, Py, W, H, Rot, EPlot::Building, true };
				Pt.bAlongW = W >= H;
				if (const FModel* M = FindModel(Model))
				{
					// (the model's long side, its X or its Y, lies along the plot's long side: AOakHillTown turns it so)
					const float Long = (M->bLongY ? M->LenY : M->LenX) / Unit, Short = (M->bLongY ? M->LenX : M->LenY) / Unit;
					if (W >= H) { Pt.W = Long; Pt.H = Short; } else { Pt.W = Short; Pt.H = Long; }
				}
				A.Add(Pt);
			};
			// The shops round the plaza (owner, 2026-11-13: "The stores should sit around the square in the middle of the town, not
			// off to the side"): each stands 0.8 m (plus Back) outside the ring road at its angle (degrees counter-clockwise from east,
			// as on the map), its long side along the ring and its door or open front toward the oak, its corners at least 0.5 m off
			// the roads leaving the ring
			auto Ring = [&](const TCHAR* Name, const TCHAR* Id, const TCHAR* Model, float Deg, float Back = 0.f)
			{
				const FModel* M = FindModel(Model);
				if (!M) return;
				const float Out = (RingR * Unit + RoadWidth / 200.f + 0.8f + Back + M->LenY * 0.5f) / Unit, R = FMath::DegreesToRadians(Deg);
				FPlot Pt{ Name, Model, 255.f + Out * FMath::Cos(R), 410.f - Out * FMath::Sin(R), M->LenX / Unit, M->LenY / Unit, FRotator::NormalizeAxis(-Deg - 90.f), EPlot::Building, true };
				Pt.bAlongW = true; Pt.bFacePlaza = true; Pt.Id = Id;
				A.Add(Pt);
			};
			// A building placed by hand (the 2026-11-18 layout, Docs/TownLab/OakHillLayout_2026-11-18.md, laid out and checked with
			// Saved/Scratch_layout/new.py in the townlab copy): its middle E, N metres from the plaza and the bearing its door faces;
			// its long side runs along the street (the church's along its nave, its door on the short end)
			auto D = [&](const TCHAR* Name, const TCHAR* Id, const TCHAR* Model, float E, float N, float Door)
			{
				const FModel* M = FindModel(Model);
				if (!M) return;
				FPlot Pt{ Name, Model, 255.f + E / Unit, 410.f - N / Unit, (M->bLongY ? M->LenY : M->LenX) / Unit, (M->bLongY ? M->LenX : M->LenY) / Unit,
					FRotator::NormalizeAxis(M->bLongY ? Door - 90.f : Door), EPlot::Building, true };
				Pt.bAlongW = true; Pt.DoorDir = FRotator::ClampAxis(Door); Pt.Id = Id;
				A.Add(Pt);
			};
			// THE LAYOUT (owner, 2026-11-18: "Some of the layout of Oak Hill just doesn't make sense ... why is the entrance to the
			// church tucked away behind a house"): a market town grown along its streets. West Street comes in over the bridge past
			// the smithy and the bank, crosses the market place round the plaza and runs on as East Street to the church's door, so
			// from the bridge (and from the spawn, which faces east) the church tower closes the view. The town hall fronts the market
			// place from the north with open ground before its door; the shops and stalls face the plaza; houses line the lanes with
			// their doors on the street and yards behind. Plot order is kept (FillPlots.csv, FillOakHill.csv and the logs use the indices).
			Ring(TEXT("Bank Stall"), TEXT("BankStall"), TEXT("BankStall"), 24.f);
			D(TEXT("Bank"), TEXT("Bank"), TEXT("OakHillBank"), -21.72f, 7.67f, 180.f);                // (on West Street's north side at the market's mouth)
			D(TEXT("Smith"), TEXT("Smithy"), TEXT("OakHillSmith"), -45.22f, -6.16f, 0.f);              // (at the town's edge by the bridge, its yard toward the river)
			Ring(TEXT("Potion Stall"), TEXT("PotionStall"), TEXT("PotionStall"), 246.f);
			Ring(TEXT("Potion Shop"), TEXT("PotionShop"), TEXT("OakHillPotionShop"), 204.4f);
			Ring(TEXT("Store"), TEXT("Store"), TEXT("OakHillStore"), 295.f);
			Ring(TEXT("Market Stall"), TEXT("MarketStall"), TEXT("GeneralStore"), 336.f);
			// The Town Hall on the market place's north side, its door to the oak, 2.8 m more open ground before it than a shop has
			Ring(TEXT("Town Hall"), TEXT("TownHall"), TEXT("OakHillTownHall"), 90.f, 2.8f);
			// The church at the end of East Street, its door (under the tower) facing west down the street to the plaza and the bridge
			D(TEXT("Church"), TEXT("Church"), TEXT("OakHillChurch"), 32.5f, 0.f, 270.f);
			// The well on the plaza lawn, beside the big oak
			B(TEXT("Well"), TEXT("OakHillWell"), 290.5f, 443.f, 16.f, 16.f, 20.f);   // (6.8 m from the oak: clear of its roots, 5 m out, 2026-11-13)
			A.Last().Id = TEXT("Well");
			// 16 houses (11.5 x 8.6 m): the first design, and the corner house (9 x 9.8) at the corners of the lanes, each 0.7 m off its
			// street with its door to it (plots 10..25; the corner houses keep plots 13, 15, 17, 18, 22 and 24)
			D(TEXT("House"), TEXT("House_SE3"), TEXT("OakHillHouse"), 17.33f, -41.27f, 10.8f);          // 10: Southeast Road, south side
			D(TEXT("House"), TEXT("House_NL1"), TEXT("OakHillHouse"), 24.42f, 47.73f, 281.8f);          // 11: North Lane, east side
			D(TEXT("House"), TEXT("House_FL1"), TEXT("OakHillHouse"), -31.04f, 33.89f, 264.3f);         // 12: Fort Lane, by the watchtower
			D(TEXT("House"), TEXT("House_CLS3"), TEXT("OakHillCornerHouse"), 59.83f, 12.08f, 21.2f);    // 13: Church Lane, south side
			D(TEXT("House"), TEXT("House_CLN2"), TEXT("OakHillHouse"), 39.28f, 30.46f, 186.3f);         // 14: Church Lane, north side
			D(TEXT("House"), TEXT("House_CLN1"), TEXT("OakHillCornerHouse"), 22.84f, 31.93f, 178.2f);   // 15: the corner of North Lane and Church Lane
			D(TEXT("House"), TEXT("House_CLS1"), TEXT("OakHillHouse"), 31.17f, 17.66f, 3.4f);           // 16: Church Lane, south side, behind the church
			D(TEXT("House"), TEXT("House_FL0"), TEXT("OakHillCornerHouse"), -29.57f, 20.33f, 268.2f);   // 17: Fort Lane, behind the bank
			D(TEXT("House"), TEXT("House_CLN3"), TEXT("OakHillCornerHouse"), 54.77f, 28.78f, 190.9f);   // 18: Church Lane, north side
			D(TEXT("House"), TEXT("House_CLS2"), TEXT("OakHillHouse"), 46.24f, 15.95f, 8.8f);           // 19: Church Lane, south side, above the graveyard
			D(TEXT("House"), TEXT("House_SE1"), TEXT("OakHillHouse"), 25.39f, -28.91f, 187.4f);         // 20: Southeast Road, north side
			D(TEXT("House"), TEXT("House_SE2"), TEXT("OakHillHouse"), 40.84f, -30.13f, 183.1f);         // 21: Southeast Road, north side
			D(TEXT("House"), TEXT("House_WS1"), TEXT("OakHillCornerHouse"), -32.72f, -7.37f, 0.f);      // 22: West Street, south side, beside the smithy
			D(TEXT("House"), TEXT("House_SE4"), TEXT("OakHillHouse"), 34.07f, -43.24f, 3.5f);           // 23: Southeast Road, south side
			D(TEXT("House"), TEXT("House_SW1"), TEXT("OakHillCornerHouse"), -22.65f, -27.77f, 158.2f);  // 24: Southwest Road, north side
			D(TEXT("House"), TEXT("House_SW2"), TEXT("OakHillHouse"), -23.39f, -44.54f, 330.f);         // 25: Southwest Road, south side
			// Kept empty for what isn't made yet: grass grows there, but no trees. The bridge and the forts keep their spots on the
			// river bank (the river doesn't move with the town)
			auto E = [&](const TCHAR* Name, float Px, float Py, float W, float H, float Rot, EPlot K, bool bLevel) { A.Add({ Name, nullptr, Px, Py, W, H, Rot, K, bLevel }); };
			E(TEXT("Boxing Ring"), 255.f, 663.6f, 64.f, 72.f, 0.f, EPlot::Empty, true);   // (the training ground, between the southwest and southeast roads where they part)
			// The war buildings (2026-11-18), in the same order as the empty plots they were. The forts and the watchtower aren't
			// levelled (bLevel false: they stand on the river bank and the ground stays as it was); the ruins are, as a house is (left
			// as it was, the castle knoll's top rose through their nave to the window sills)
			auto W = [&](const TCHAR* Name, const TCHAR* Model, float Px, float Py, float Wd, float Ht, float Rot) { B(Name, Model, Px, Py, Wd, Ht, Rot); A.Last().bLevel = false; };
			B(TEXT("Castle Ruins"), TEXT("OakHillRuins"), 240.f, 95.f, 128.f, 64.f, 20.f);
			E(TEXT("Garden"), 562.1f, 531.4f, 36.f, 152.f, 90.f, EPlot::Empty, false);   // (the allotments, south of the church behind the graveyard, along the southeast road)
			E(TEXT("Graveyard"), 594.3f, 420.7f, 120.f, 120.f, 0.f, EPlot::EmptyRound, false);   // (the churchyard, round the church's east end, as a village churchyard is)
			W(TEXT("North Fort"), TEXT("OakHillFort"), -57.f, 205.f, 44.f, 104.f, 10.f);
			W(TEXT("South Fort"), TEXT("OakHillFort"), -60.f, 615.f, 44.f, 104.f, 0.f);   // (the plan's -93 put its west half on the bank's slope, 3 m from the water: 4.6 m further east)
			W(TEXT("Watchtower"), TEXT("OakHillWatchtower"), -31.f, 103.f, 40.f, 40.f, 0.f);   // (northwest beyond the north fort, on the bank: the plan's 140,195 was pushed 13 m into the houses by the spacing pass and shoved a house 9 m)
			E(TEXT("Bridge"), -191.f, 410.f, 179.f, 42.f, 0.f, EPlot::Empty, false);
			E(TEXT("Oak Lawn"), 255.f, 410.f, 2.f * LawnR, 2.f * LawnR, 0.f, EPlot::Lawn, false);
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
						const float Gap = RoadClearanceM(Q.X, Q.Y, &Nr);
						if (Gap < Worst) { Worst = Gap; WorstAt = Q; Near = Nr; }
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
			// every building's door bearing, the hand-set ones as they are (the shops round the plaza, the ruins, the forts and the
			// watchtower get theirs from BuildingYaw)
			for (FPlot& Pl : A)
				if (Pl.Kind == EPlot::Building && Pl.DoorDir < 0.f)
				{
					const FModel* Mdl = FindModel(Pl.Model);
					Pl.DoorDir = FRotator::ClampAxis(BuildingYaw(Pl, Mdl) + (Mdl ? Mdl->DoorYaw : 90.f));
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
	// True when (E, N) metres lies inside a fort's or the watchtower's plot (its real drawn footprint, sized from the FModel row) or
	// within MarginM of it: the one test every "keep trees off the forts" rule uses (2026-11-18: FillTrees.csv's spots were marked on
	// the forts' old empty plots and stood inside the walls once the 1.4x forts came)
	inline bool InWarFootprint(float E, float N, float MarginM)
	{
		for (const FPlot& P : Plots())
			if (P.Kind == EPlot::Building && P.Model && (FCString::Strcmp(P.Model, TEXT("OakHillFort")) == 0 || FCString::Strcmp(P.Model, TEXT("OakHillWatchtower")) == 0) && PlotSD(P, E, N) < MarginM) return true;
		return false;
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
	// How much a point is a building's ground (1 on its plot and the flat 1.5 m round it, fading out over 4 m more)
	inline float BuildingKeep(float E, float N)
	{
		float K = 0.f;
		for (const FPlot& P : Plots())
		{
			if (P.Kind != EPlot::Building) continue;
			if (FMath::Square(E - P.Ce) + FMath::Square(N - P.Cn) > FMath::Square(P.Reach + PadFlat + 4.f)) continue;
			K = FMath::Max(K, 1.f - JeebWorld::Smooth(PadFlat, PadFlat + 4.f, PlotSD(P, E, N)));
		}
		return K;
	}
	// The ground (cm), given the world's height there and the distance to the nearest river (from JeebWorld::SeaAndRivers)
	// bCap: the rivers' bank cap (WorldMap.h UnderBankCap) trims the world and the hills before the level plots go on, and the
	// finished ground once more everywhere but on a building's plot: a walk up from the river never meets a steep lip (the pads'
	// 3 m fade by the water made a 52 degree step under the North Fort when only the hills were capped), and the houses near
	// the river (13-15 m from its edge) keep their own levels (capping their ground cut up to 59 cm from under them, 2026-11-15).
	inline bool Covers(float X, float Y) { return InBox(X, Y) && Enabled(); }
	inline float Apply(float X, float Y, float H, float RiverD, float RiverHalf, bool bCap = true)
	{
		if (!Covers(X, Y)) return H;
		if (bCap) H = JeebWorld::UnderBankCap(X, Y, H, RiverD);
		const float E = EastM(Y), N = NorthM(X);
		const float MHills = Mask(E, N, RiverD, RiverHalf, 1000.f);
		if (MHills <= 0.f) return H;
		float Ground = FMath::Lerp(H, JeebWorld::RiverLevel + HillsM(E, N) * 100.f, MHills);
		if (bCap) Ground = JeebWorld::UnderBankCap(X, Y, Ground, RiverD);
		const float MPads = Mask(E, N, RiverD, RiverHalf, 300.f);
		if (MPads <= 0.f) return Ground;
		const float Padded = FMath::Lerp(Ground, JeebWorld::RiverLevel + TownM(E, N, (Ground - JeebWorld::RiverLevel) / 100.f) * 100.f, MPads);
		if (!bCap) return Padded;
		const float Capped = JeebWorld::UnderBankCap(X, Y, Padded, RiverD);
		return Capped >= Padded ? Padded : FMath::Lerp(Capped, Padded, BuildingKeep(E, N));
	}

	// ------------------------------------------------------------------ roads (3.5 m wide village lanes since the town was shrunk; 5 m before: RoadWidth), the plan's paths
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
			// The ring road round the plaza (centreline radius RingR units: 14 m, its inside edge 12.25 m from the oak)
			{
				FRoad Ring; Ring.bClosed = true;
				const int32 Pts = FMath::CeilToInt(2.f * PI * RingR * UnitCm / 60.f);
				for (int32 i = 0; i < Pts; ++i) { const float A0 = 2.f * PI * i / Pts; Ring.Pts.Add(FromPx(255.f + RingR * FMath::Cos(A0), 410.f + RingR * FMath::Sin(A0))); }
				A.Add(Ring);
			}
			// The streets of the 2026-11-18 layout (Docs/TownLab/OakHillLayout_2026-11-18.md); the river-bound ones are drawn on past
			// the bank and cut where the water starts, since the river stays where it is while the town shrinks
			const TCHAR* Paths[] = {
				TEXT("M-200 410 L155 410"),   // West Street, from the bridge to the market place (the road stops at the bank: the bridge is a plot of its own)
				TEXT("M355.0 410.0 L412.1 410.0"),   // East Street, from the market place to the church door
				TEXT("M323.2 336.9 C 327.6 326.4 343.5 297.8 349.3 274.3 C 355.1 250.8 353.6 225.5 357.9 195.7 C 362.1 166.0 368.8 128.3 375.0 95.7 C 381.2 63.1 391.7 16.0 395.0 0.0"),   // North Lane, past the town hall's east side to the north (the forest road starts at its end)
				TEXT("M352.1 238.6 C 366.9 237.9 409.3 233.8 440.7 234.3 C 472.1 234.8 505.0 237.1 540.7 241.4 C 576.4 245.7 624.0 252.9 655.0 260.0 C 686.0 267.1 704.4 276.5 726.4 284.3 C 748.5 292.0 777.0 302.7 787.1 306.4"),   // Church Lane, east from North Lane behind the church to the east farm
				TEXT("M-9.3 410.0 C -9.0 393.3 -7.5 343.3 -7.9 310.0 C -8.2 276.7 -9.5 240.2 -11.4 210.0 C -13.3 179.8 -18.0 142.1 -19.3 128.6"),   // Fort Lane, north off West Street by the bridge to the north fort and the watchtower
				TEXT("M255 510 L255 592.1"),   // South Road, to the fork by the training ground
				TEXT("M255.0 592.1 C 248.5 596.3 233.2 608.7 215.7 617.1 C 198.2 625.6 172.0 633.9 150.0 642.9 C 128.0 651.8 105.2 658.8 83.6 670.7 C 61.9 682.6 46.1 689.9 20.0 714.3 C -6.1 738.7 -42.0 781.2 -72.9 817.1 C -103.7 853.1 -149.6 911.2 -165.0 930.0"),   // Southwest Road, to the river fork (drawn on to the bank)
				TEXT("M255.0 592.1 C 261.5 596.9 277.6 611.1 294.3 620.7 C 311.0 630.4 328.2 642.3 355.0 650.0 C 381.8 657.7 416.9 662.9 455.0 667.1 C 493.1 671.4 544.3 672.6 583.6 675.7 C 622.9 678.8 659.8 681.8 690.7 685.7 C 721.7 689.6 756.2 697.0 769.3 699.3"),   // Southeast Road, past the houses and the allotments to the east paddock (Birch Grove's road starts at its end)
				TEXT("M202.0 325.2 C 197.3 319.1 179.7 303.0 173.6 288.6 C 167.4 274.1 165.5 255.2 165.0 238.6 C 164.5 221.9 163.8 202.1 170.7 188.6 C 177.6 175.0 200.5 162.4 206.4 157.1"),   // Castle Path, from the market place between the bank and the town hall up to the ruins' great arch
				TEXT("M69.3 678.6 C 62.1 672.6 40.2 653.5 26.4 642.9 C 12.6 632.3 -6.9 619.6 -13.6 615.0"),   // South Fort Path, off the southwest road to the south fort's gate
			};
			for (const TCHAR* S : Paths)
			{
				FRoad Rd; Rd.Pts = ParsePath(S);
				// a road that the plan draws to the river's edge stops on the bank, not in the water
				auto Wet = [](const FVector2D& Q) { return JeebWorld::NearRiver(Q.X, Q.Y, 300.f); };
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

	// A building's turn (Unreal yaw, degrees): the model's long side (its X, or its Y for bLongY) along the plot's long side (bAlongW),
	// then whichever of the two ways puts its door toward the nearest road, or toward the oak for the shops round the plaza
	// (bFacePlaza). AOakHillTown::BuildBuildings places the model and its walls by it.
	inline float BuildingYaw(const FPlot& P, const FModel* Mdl)
	{
		const float DoorYaw = Mdl ? Mdl->DoorYaw : 90.f;
		const FVector2D C = FromPx(P.Px, P.Py);
		const float Yaw0 = (P.bAlongW ? 90.f : 0.f) + P.Rot - (Mdl && Mdl->bLongY ? 90.f : 0.f);
		float ToRoad = Yaw0 + DoorYaw, Best = 1.0e12f;
		if (P.DoorDir >= 0.f) ToRoad = P.DoorDir;   // (the 2026-11-18 layout sets each door by hand)
		else if (P.bFacePlaza) ToRoad = FMath::RadiansToDegrees(FMath::Atan2(PosY - C.Y, PosX - C.X));
		else
			for (const FRoad& R : Roads())
				for (const FVector2D& Q : R.Pts) { const float D = FVector2D::DistSquared(Q, C); if (D < Best) { Best = D; ToRoad = FMath::RadiansToDegrees(FMath::Atan2(Q.Y - C.Y, Q.X - C.X)); } }
		auto Diff = [](float A, float B) { return FMath::Abs(FMath::FindDeltaAngleDegrees(A, B)); };
		return Diff(Yaw0 + DoorYaw, ToRoad) <= Diff(Yaw0 + 180.f + DoorYaw, ToRoad) ? Yaw0 : Yaw0 + 180.f;
	}
	// A building's plot by its Id ("Church", "House_SE1", ...; Docs/TownLab/OakHillLayout_2026-11-18.md), or -1
	inline int32 PlotIndexById(const TCHAR* Id)
	{
		const TArray<FPlot>& Ps = Plots();
		for (int32 i = 0; i < Ps.Num(); ++i) if (Ps[i].Id && FCString::Stricmp(Ps[i].Id, Id) == 0) return i;
		return INDEX_NONE;
	}
	// The spot OutCm in front of a building's door wall, in the middle of its front (game cm; the door wall is the foundation
	// walls' front, as the shop counters use), and the yaw from there back to the door
	inline FVector2D DoorAt(const FPlot& P, float OutCm, float* OutYawToDoor = nullptr)
	{
		const FModel* Mdl = FindModel(P.Model);
		const FVector2D C = FromPx(P.Px, P.Py);
		if (!Mdl) return C;
		const float Yaw = BuildingYaw(P, Mdl), Cs = FMath::Cos(FMath::DegreesToRadians(Yaw)), Sn = FMath::Sin(FMath::DegreesToRadians(Yaw));
		const float LX = (Mdl->WX0 + Mdl->WX1) * 0.5f * Mdl->Scale, LY = -Mdl->WY0 * Mdl->Scale + OutCm;
		if (OutYawToDoor) *OutYawToDoor = FRotator::NormalizeAxis(Yaw + Mdl->DoorYaw + 180.f);
		return C + FVector2D(LX * Cs - LY * Sn, LX * Sn + LY * Cs);
	}
	inline bool IsStall(const TCHAR* Model)
	{
		return Model && (FCString::Strcmp(Model, TEXT("GeneralStore")) == 0 || FCString::Strcmp(Model, TEXT("PotionStall")) == 0 || FCString::Strcmp(Model, TEXT("BankStall")) == 0);
	}
	// ------------------------------------------------------------------ shop counters (owner, 2026-11-13: "rig the stores so to have
	// popups with things to buy/trade. We'll add NPCs to the shops later")
	// Each shop round the plaza has a counter spot just outside its door or open front: E there opens the shop's window, like
	// talking to the market's traders. The server checks the same spots (KnightCharacterNet.cpp NearTrader), so they are worked out
	// the same way on every machine, from the plots alone. Smith: the smithy; bank and bank stall: the bank; the store, market
	// stall, potion shop and potion stall: the store (it sells potions too)
	enum class EShop : uint8 { Store, Smith, Bank };
	struct FCounter { FVector2D At; EShop Shop; const TCHAR* Name; float Facing; };   // (At: game cm; Facing: yaw from the counter to the shop)
	constexpr float CounterOut = 120.f;   // (cm in front of the foundation's front wall, or of a stall's open front)
	inline bool ShopOf(const TCHAR* Model, EShop& Out)
	{
		if (!Model) return false;
		auto Is = [Model](const TCHAR* N) { return FCString::Strcmp(Model, N) == 0; };
		if (Is(TEXT("OakHillSmith")) || Is(TEXT("TrollWarForge"))) { Out = EShop::Smith; return true; }
		if (Is(TEXT("OakHillBank")) || Is(TEXT("BankStall")) || Is(TEXT("TrollVault"))) { Out = EShop::Bank; return true; }
		if (Is(TEXT("OakHillStore")) || Is(TEXT("GeneralStore")) || Is(TEXT("OakHillPotionShop")) || Is(TEXT("PotionStall"))
			|| Is(TEXT("TrollTradingPost")) || Is(TEXT("TrollBoneCarver")) || Is(TEXT("TrollTanner")) || Is(TEXT("TrollBrewHut"))) { Out = EShop::Store; return true; }   // (the troll town's shops, 2026-11-28: their keepers sell the town's goods)
		return false;
	}
	inline const TArray<FCounter>& Counters()
	{
		static const TArray<FCounter> C = []()
		{
			TArray<FCounter> A;
			if (!Enabled()) return A;
			for (const FPlot& P : Plots())
			{
				EShop Shop;
				if (P.Kind != EPlot::Building || !ShopOf(P.Model, Shop)) continue;
				const FModel* Mdl = FindModel(P.Model);
				if (!Mdl) continue;
				const float Yaw = BuildingYaw(P, Mdl), Cs = FMath::Cos(FMath::DegreesToRadians(Yaw)), Sn = FMath::Sin(FMath::DegreesToRadians(Yaw));
				// (the door is on the model's +Y here, Blender's -Y: its front wall at -WY0; centred on the walls along X)
				const float LX = (Mdl->WX0 + Mdl->WX1) * 0.5f * Mdl->Scale, LY = -Mdl->WY0 * Mdl->Scale + CounterOut;
				const FVector2D At = FromPx(P.Px, P.Py) + FVector2D(LX * Cs - LY * Sn, LX * Sn + LY * Cs);
				A.Add({ At, Shop, P.Name, float(FRotator::NormalizeAxis(Yaw + Mdl->DoorYaw + 180.f)) });
			}
			return A;
		}();
		return C;
	}
	// The nearest counter of that shop within RadiusCm of (X, Y), or null
	inline const FCounter* NearCounter(EShop Shop, float X, float Y, float RadiusCm)
	{
		if (!InBox(X, Y, RadiusCm)) return nullptr;
		const FCounter* Best = nullptr; float BestD = RadiusCm * RadiusCm;
		for (const FCounter& C : Counters())
		{
			const float D = FVector2D::DistSquared(C.At, FVector2D(X, Y));
			if (C.Shop == Shop && D <= BestD) { BestD = D; Best = &C; }
		}
		return Best;
	}
	// The nearest counter of any shop within RadiusCm, or null
	inline const FCounter* NearAnyCounter(float X, float Y, float RadiusCm)
	{
		if (!InBox(X, Y, RadiusCm)) return nullptr;
		const FCounter* Best = nullptr; float BestD = RadiusCm * RadiusCm;
		for (const FCounter& C : Counters())
		{
			const float D = FVector2D::DistSquared(C.At, FVector2D(X, Y));
			if (D <= BestD) { BestD = D; Best = &C; }
		}
		return Best;
	}

	// Each building plot's foundation walls in game cm (the same box as its solid walls in AOakHillTown::BuildBuildings), by plot
	// index. bWalls is false where the plot's own box is used instead: no measured walls, or a market stall (its open front, with
	// the counter's platform, stays clear of grass)
	// Foot: the model's ground footprint (OakHillFootprints.h), used when there is one; Centre and Scale place it.
	struct FWallBox { bool bWalls = false; FVector2D Mid = FVector2D::ZeroVector, Half = FVector2D::ZeroVector, Centre = FVector2D::ZeroVector; float Cs = 1.f, Sn = 0.f, Scale = 1.f; const FFootprint* Foot = nullptr; };
	inline bool InFootprint(const FFootprint& F, float BX, float BY)   // (BX, BY: the model's own frame, Blender's, cm)
	{
		const int32 I = FMath::FloorToInt((BX - F.X0) / FootCell), J = FMath::FloorToInt((BY - F.Y0) / FootCell);
		if (I < 0 || I >= F.NX || J < 0 || J >= F.NY) return false;
		const char Ch = F.Hex[I * ((F.NY + 3) / 4) + J / 4];
		const int32 Nib = Ch >= 'a' ? Ch - 'a' + 10 : Ch - '0';
		return ((Nib >> (3 - J % 4)) & 1) != 0;
	}
	inline const TArray<FWallBox>& WallBoxes()
	{
		static const TArray<FWallBox> W = []()
		{
			TArray<FWallBox> A;
			for (const FPlot& P : Plots())
			{
				FWallBox& B = A.AddDefaulted_GetRef();
				if (P.Kind != EPlot::Building || !P.Model || IsStall(P.Model)) continue;
				const FModel* Mdl = FindModel(P.Model);
				if (!Mdl || !(Mdl->WX1 > Mdl->WX0)) continue;
				const float Yaw = BuildingYaw(P, Mdl);
				// (Unreal mirrors Blender's Y on import: a model's +Y in Blender is its -Y here)
				const FVector2D Lo(Mdl->WX0 * Mdl->Scale, -Mdl->WY1 * Mdl->Scale), Hi(Mdl->WX1 * Mdl->Scale, -Mdl->WY0 * Mdl->Scale);
				const FVector2D MidL = (Lo + Hi) * 0.5f;
				B.Cs = FMath::Cos(FMath::DegreesToRadians(Yaw)); B.Sn = FMath::Sin(FMath::DegreesToRadians(Yaw));
				B.Mid = FromPx(P.Px, P.Py) + FVector2D(MidL.X * B.Cs - MidL.Y * B.Sn, MidL.X * B.Sn + MidL.Y * B.Cs);
				B.Half = (Hi - Lo) * 0.5f;
				B.bWalls = true;
				B.Centre = FromPx(P.Px, P.Py); B.Scale = Mdl->Scale;
				int32 NumFeet = 0; const FFootprint* Feet = Footprints(NumFeet);
				for (int32 f = 0; f < NumFeet; ++f) if (FCString::Strcmp(Feet[f].Model, P.Model) == 0) B.Foot = &Feet[f];
			}
			return A;
		}();
		return W;
	}

	// ------------------------------------------------------------------ what grass and other code ask
	// On a building's real footprint (no grass, no leaves): its walls, steps and porch floors from OakHillFootprints.h, else its
	// foundation walls' box; with a margin in metres, the walls' box. Porch corners and the ground under eaves keep their grass
	// (owner's ground pass, 2026-11-13: the whole model's bounds left bare corners with no building on them)
	// The walk-in houses' rooms (HouseInterior.cpp adds each as it is built): their floor is a room, never grass or leaves
	struct FOpenRoom { const void* Owner; FVector2D At; float Cs, Sn; FVector2D Lo, Hi; };
	inline TArray<FOpenRoom>& OpenRooms() { static TArray<FOpenRoom> R; return R; }
	inline bool InOpenRoom(float X, float Y)
	{
		for (const FOpenRoom& R : OpenRooms())
		{
			const float DX = X - R.At.X, DY = Y - R.At.Y, LX = DX * R.Cs + DY * R.Sn, LY = -DX * R.Sn + DY * R.Cs;
			if (LX > R.Lo.X && LX < R.Hi.X && LY > R.Lo.Y && LY < R.Hi.Y) return true;
		}
		return false;
	}
	inline bool OnBuilding(float X, float Y, float MarginM = 0.f)
	{
		if (!InBox(X, Y) || !Enabled()) return false;
		if (InOpenRoom(X, Y)) return true;
		const float E = EastM(Y), N = NorthM(X);
		const TArray<FPlot>& Ps = Plots();
		const TArray<FWallBox>& Ws = WallBoxes();
		for (int32 i = 0; i < Ps.Num(); ++i)
		{
			const FPlot& P = Ps[i];
			if (P.Kind != EPlot::Building) continue;
			if (!Ws[i].bWalls) { if (PlotSD(P, E, N) < MarginM) return true; continue; }
			const FWallBox& B = Ws[i];
			if (B.Foot && MarginM <= 0.f)
			{
				// (into the model's frame: turn back by its yaw, undo its scale, and Unreal's Y is Blender's -Y)
				const float FX = X - B.Centre.X, FY = Y - B.Centre.Y;
				if (InFootprint(*B.Foot, (FX * B.Cs + FY * B.Sn) / B.Scale, -(-FX * B.Sn + FY * B.Cs) / B.Scale)) return true;
				continue;
			}
			const float DX = X - B.Mid.X, DY = Y - B.Mid.Y;
			const FVector2D Q(FMath::Abs(DX * B.Cs + DY * B.Sn) - B.Half.X, FMath::Abs(-DX * B.Sn + DY * B.Cs) - B.Half.Y);
			const float SD = FVector2D(FMath::Max(Q.X, 0.f), FMath::Max(Q.Y, 0.f)).Size() + FMath::Min(FMath::Max(Q.X, Q.Y), 0.f);
			if (SD < MarginM * 100.f) return true;
		}
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

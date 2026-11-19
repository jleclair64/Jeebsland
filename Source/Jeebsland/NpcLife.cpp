#include "NpcLife.h"
#include "Npc.h"
#include "Terrain.h"
#include "OakHill.h"
#include "SteeleCreek.h"
#include "TownLab.h"
#include "JeebWater.h"
#include "Cottage.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Algo/Reverse.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "ProceduralMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "UnrealClient.h"
#include "TimerManager.h"

// (test pictures only: moves YOUR clock for the townsfolk, so they no longer match anyone else's screen)
static TAutoConsoleVariable<float> CVarLifeSkip(TEXT("jeeb.Life.Skip"), 0.f,
	TEXT("Seconds added to the townsfolk's clock on this machine only (test pictures: see someone's round later on). 0 = the real clock."));

namespace JeebLife
{
	double Now(const UWorld* World)
	{
		if (!World) return 0.0;
		const AGameStateBase* GS = World->GetGameState();
		const double T = GS ? GS->GetServerWorldTimeSeconds() : double(World->GetTimeSeconds());
		return T + double(CVarLifeSkip.GetValueOnGameThread());
	}

	bool IsNight(double Time)
	{
		// (DayNight.h: the sun loops every 20 minutes from noon at phase 0; it sets at about 0.29 and rises at 0.71 with the default
		// low point. A little inside that, so they are still out at sunset and back out just after sunrise)
		const double Phase = FMath::Fmod(Time / 1200.0, 1.0);
		const double P = Phase < 0.0 ? Phase + 1.0 : Phase;
		return P > 0.31 && P < 0.69;
	}

	float DrawnGround(float X, float Y)
	{
		return JeebTerrain::DrawnHeight(X, Y);
	}

	FPose Eval(const FRound& R, double Time, bool bCheckPartner)
	{
		FPose P;
		if (R.Segs.Num() == 0 || R.Period <= 0.f) return P;
		const double L = Time + double(R.Offset);
		const double Cycle = FMath::FloorToDouble(L / double(R.Period));
		const float Local = float(L - Cycle * double(R.Period));
		if (R.bNightHome && IsNight(Cycle * double(R.Period) - double(R.Offset)))
		{
			P.At = R.Segs[0].A; P.Yaw = R.Segs[0].YawA;
			return P;
		}
		int32 Lo = 0, Hi = R.Segs.Num() - 1;
		while (Lo < Hi) { const int32 Mid = (Lo + Hi + 1) / 2; if (R.Segs[Mid].T0 <= Local) Lo = Mid; else Hi = Mid - 1; }
		const FSeg& S = R.Segs[Lo];
		const float F = S.Dur > 0.f ? FMath::Clamp((Local - S.T0) / S.Dur, 0.f, 1.f) : 1.f;
		switch (S.Kind)
		{
		case ESeg::Walk:
			P.At = FMath::Lerp(S.A, S.B, F); P.Yaw = S.YawA; P.bWalk = true; P.bStay = false;
			break;
		case ESeg::Turn:
			P.At = S.A; P.Yaw = S.YawA + FMath::FindDeltaAngleDegrees(S.YawA, S.YawB) * FMath::SmoothStep(0.f, 1.f, F); P.bTurn = true; P.bStay = false;
			break;
		default:
			P.At = S.A; P.Yaw = S.YawA;
			if (S.bChat && bCheckPartner)
				if (const TSharedPtr<FRound> O = R.Partner.Pin())
				{
					const FPose Q = Eval(*O, Time, false);
					if (Q.bStay && FVector2D::Distance(Q.At, P.At) < 320.f) { P.bChat = true; P.ChatAt = Q.At; }
				}
			break;
		}
		return P;
	}

	// Writes a round: stays, turns on the spot (about 140 degrees a second) before every walk, walks in straight legs
	struct FBuilder
	{
		FRound& R;
		FVector2D Pos;
		float Yaw = 0.f, T = 0.f;
		FBuilder(FRound& In, const FVector2D& Start, float StartYaw) : R(In), Pos(Start), Yaw(StartYaw) { R.Segs.Reset(); }
		void Add(ESeg K, const FVector2D& A, const FVector2D& B, float YA, float YB, float Dur, bool bChat = false)
		{
			FSeg S; S.Kind = K; S.A = A; S.B = B; S.YawA = YA; S.YawB = YB; S.T0 = T; S.Dur = Dur; S.bChat = bChat;
			R.Segs.Add(S);
			T += Dur;
		}
		void Turn(float To)
		{
			const float D = FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, To));
			if (D > 8.f) Add(ESeg::Turn, Pos, Pos, Yaw, To, D / 140.f + 0.15f);
			Yaw = To;
		}
		void Stay(float Secs, float Face, bool bChat = false)
		{
			Turn(Face);
			if (Secs > 0.05f) Add(ESeg::Stay, Pos, Pos, Yaw, Yaw, Secs, bChat);
		}
		void WalkTo(const FVector2D& B)
		{
			const float D = float(FVector2D::Distance(Pos, B));
			if (D < 5.f) return;
			const float Dir = FVector(B.X - Pos.X, B.Y - Pos.Y, 0.f).Rotation().Yaw;
			Turn(Dir);
			Add(ESeg::Walk, Pos, B, Dir, Dir, D / FMath::Max(R.Speed, 10.f));
			Pos = B;
		}
		void Path(const TArray<FVector2D>& Pts) { for (const FVector2D& P : Pts) WalkTo(P); }
		// Back at the start: stand there, facing the first way, until the round's Period is up
		void Finish(float Period)
		{
			const FVector2D Start = R.Segs.Num() > 0 ? R.Segs[0].A : Pos;
			WalkTo(Start);
			const float Face = R.Segs.Num() > 0 ? R.Segs[0].YawA : Yaw;
			Turn(Face);
			if (Period > T + 0.05f) Add(ESeg::Stay, Pos, Pos, Yaw, Yaw, Period - T);
			R.Period = FMath::Max(T, 1.f);
		}
	};

	TSharedPtr<FRound> MakeBeat(const TArray<FVector2D>& Pts, float Pause, float Speed, uint32 Seed, bool bNightHome)
	{
		TSharedPtr<FRound> R = MakeShared<FRound>();
		R->Speed = Speed;
		R->bNightHome = bNightHome;
		if (Pts.Num() < 2) return R;
		FRandomStream Rs(int32(Seed & 0x7fffffff));
		const float Yaw0 = FVector(Pts[1].X - Pts[0].X, Pts[1].Y - Pts[0].Y, 0.f).Rotation().Yaw;
		FBuilder B(*R, Pts[0], Yaw0);
		B.Stay(Pause * Rs.FRandRange(0.6f, 1.4f), Yaw0);
		for (int32 i = 1; i < Pts.Num(); ++i)
		{
			B.WalkTo(Pts[i]);
			B.Stay(Pause * Rs.FRandRange(0.6f, 1.4f), B.Yaw);
		}
		B.Finish(0.f);
		R->Anchor = Pts[0];
		for (const FVector2D& P : Pts) R->Roam = FMath::Max(R->Roam, float(FVector2D::Distance(P, Pts[0])));
		R->Offset = float(Seed % 997u) * 0.37f;
		return R;
	}
}

// ======================================================================================================== walking routes
namespace
{
	using namespace JeebLife;

	// Where people can walk, on a 90 cm grid, worked out the same way on every machine: not on or within 45 cm of a building's
	// walls (the towns' plans), not in water, and nothing solid in a person-sized capsule from 27 cm up (props, walls, fences,
	// trees; the ground's own meshes, players and the townsfolk themselves don't count). Streets cost less than open ground, so
	// routes keep to them.
	struct FWalkMap
	{
		UWorld* W = nullptr;
		static constexpr float Cell = 90.f;
		TMap<FIntPoint, float> Costs;   // < 0 blocked
		int32 Tests = 0;

		static FIntPoint CellOf(const FVector2D& P) { return FIntPoint(FMath::FloorToInt(P.X / Cell), FMath::FloorToInt(P.Y / Cell)); }
		static FVector2D Centre(const FIntPoint& C) { return FVector2D((C.X + 0.5f) * Cell, (C.Y + 0.5f) * Cell); }

		bool SolidFree(const FVector2D& P, float Radius = 30.f) const
		{
			const float G = JeebLife::DrawnGround(P.X, P.Y);
			TArray<FOverlapResult> Hits;
			FCollisionQueryParams Q(SCENE_QUERY_STAT(TownLifeRoute), false);
			W->OverlapMultiByChannel(Hits, FVector(P.X, P.Y, G + 27.f + 70.f), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(Radius, 70.f), Q);
			for (const FOverlapResult& H : Hits)
			{
				const UPrimitiveComponent* C = H.GetComponent();
				const AActor* A = H.GetActor();
				if (!C || !C->IsCollisionEnabled()) continue;
				if (C->IsA<UProceduralMeshComponent>()) continue;   // (the ground)
				if (A && (A->IsA<APawn>() || A->IsA<ANpc>() || A->GetIsReplicated())) continue;   // (players, people, loot, foes: not the same on every machine)
				return false;
			}
			// (and nothing to stand on between knee and head: a counter or table built into a town's own mesh, which the test above
			// skips with the ground; a vertical sweep never reaches the ground at this spot)
			FHitResult Top;
			FCollisionObjectQueryParams Obj; Obj.AddObjectTypesToQuery(ECC_WorldStatic); Obj.AddObjectTypesToQuery(ECC_WorldDynamic);
			if (W->SweepSingleByObjectType(Top, FVector(P.X, P.Y, G + 150.f), FVector(P.X, P.Y, G + 35.f), FQuat::Identity, Obj, FCollisionShape::MakeSphere(Radius * 0.7f), Q))
			{
				const AActor* A = Top.GetActor();
				if (!(A && (A->IsA<APawn>() || A->IsA<ANpc>() || A->GetIsReplicated()))) return false;
			}
			return true;
		}
		static bool OnBuildings(const FVector2D& P)
		{
			return JeebOakHill::OnBuilding(P.X, P.Y, 0.45f) || JeebSteele::OnBuilding(P.X, P.Y, 0.45f);
		}
		static bool Wet(const FVector2D& P)
		{
			float S;
			return JeebWater::SurfaceAt(P.X, P.Y, S) && S > JeebLife::DrawnGround(P.X, P.Y) + 15.f;
		}
		static bool OnStreet(const FVector2D& P)
		{
			if (JeebOakHill::Enabled() && JeebOakHill::InBox(P.X, P.Y) && JeebOakHill::RoadClearanceM(JeebOakHill::EastM(P.Y), JeebOakHill::NorthM(P.X)) < -0.3f) return true;
			for (const JeebSteele::FStreet& St : JeebSteele::Streets())
				for (const FVector2D& Q : St.Pts) if (FVector2D::DistSquared(Q, P) < FMath::Square(St.Width * 0.5f - 30.f)) return true;
			return JeebTerrain::PathClearance(P.X, P.Y) < -30.f;
		}
		float CostOf(const FIntPoint& C)
		{
			if (const float* F = Costs.Find(C)) return *F;
			const FVector2D P = Centre(C);
			float Cost = -1.f;
			++Tests;
			if (!OnBuildings(P) && !Wet(P) && SolidFree(P)) Cost = OnStreet(P) ? 1.f : 2.2f;
			Costs.Add(C, Cost);
			return Cost;
		}
		// Every 40 cm along a straight line is walkable (the ends' own cells always count as walkable); its cost per metre, summed
		bool LineFree(const FVector2D& A, const FVector2D& B, float* OutCost = nullptr)
		{
			const float L = float(FVector2D::Distance(A, B));
			const int32 N = FMath::Max(1, FMath::CeilToInt(L / 40.f));
			const FIntPoint CA = CellOf(A), CB = CellOf(B);
			float Sum = 0.f;
			for (int32 i = 0; i <= N; ++i)
			{
				const FVector2D P = FMath::Lerp(A, B, float(i) / N);
				const FIntPoint C = CellOf(P);
				const float K = (C == CA || C == CB) ? 1.f : CostOf(C);
				if (K < 0.f) return false;
				Sum += K * (L / 100.f) / (N + 1);
			}
			if (OutCost) *OutCost = Sum;
			return true;
		}

		// A walk from A to B: straight when that's clear and short (or no dearer than the way round), else A* over the grid and pulled
		// tight. Out holds the points after A, ending at B. False (a straight line) only if nothing was found.
		bool Route(const FVector2D& A, const FVector2D& B, TArray<FVector2D>& Out)
		{
			Out.Reset();
			if (FVector2D::Distance(A, B) < 10.f) { Out.Add(B); return true; }
			float Straight = 0.f;
			const bool bStraight = LineFree(A, B, &Straight);
			if (bStraight && FVector2D::Distance(A, B) < 900.f) { Out.Add(B); return true; }

			const FIntPoint CA = CellOf(A), CB = CellOf(B);
			const int32 Pad = 28;   // (25 m round the two ends)
			const FIntPoint Lo(FMath::Min(CA.X, CB.X) - Pad, FMath::Min(CA.Y, CB.Y) - Pad), Hi(FMath::Max(CA.X, CB.X) + Pad, FMath::Max(CA.Y, CB.Y) + Pad);
			const int32 GW = Hi.X - Lo.X + 1, GH = Hi.Y - Lo.Y + 1;
			if (int64(GW) * GH > 300 * 300) { Out.Add(B); return bStraight; }
			const int32 NC = GW * GH;
			TArray<float> G; G.Init(1.0e30f, NC);
			TArray<int32> From; From.Init(-1, NC);
			TArray<uint8> Closed; Closed.Init(0, NC);
			auto Idx = [&](const FIntPoint& C) { return (C.X - Lo.X) * GH + (C.Y - Lo.Y); };
			auto At = [&](int32 I) { return FIntPoint(Lo.X + I / GH, Lo.Y + I % GH); };
			auto H = [&](const FIntPoint& C) { return FMath::Sqrt(float(FMath::Square(C.X - CB.X) + FMath::Square(C.Y - CB.Y))) * 0.999f; };
			auto Pass = [&](const FIntPoint& C) { return (C == CA || C == CB) ? 1.f : CostOf(C); };
			struct FOpen { float F; int32 I; };
			TArray<FOpen> Open;
			auto Less = [](const FOpen& X, const FOpen& Y) { return X.F < Y.F; };
			const int32 S = Idx(CA), E = Idx(CB);
			G[S] = 0.f;
			Open.HeapPush({ H(CA), S }, Less);
			bool bFound = false;
			static const int32 DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
			while (Open.Num() > 0)
			{
				FOpen Top; Open.HeapPop(Top, Less);
				if (Closed[Top.I]) continue;
				Closed[Top.I] = 1;
				if (Top.I == E) { bFound = true; break; }
				const FIntPoint C = At(Top.I);
				for (int32 d = 0; d < 8; ++d)
				{
					const FIntPoint N(C.X + DX[d], C.Y + DY[d]);
					if (N.X < Lo.X || N.X > Hi.X || N.Y < Lo.Y || N.Y > Hi.Y) continue;
					const int32 NI = Idx(N);
					if (Closed[NI]) continue;
					const float K = Pass(N);
					if (K < 0.f) continue;
					if (d >= 4 && (Pass(FIntPoint(C.X + DX[d], C.Y)) < 0.f || Pass(FIntPoint(C.X, C.Y + DY[d])) < 0.f)) continue;   // (no cutting a corner)
					const float Ng = G[Top.I] + K * (d >= 4 ? 1.41421f : 1.f);
					if (Ng < G[NI]) { G[NI] = Ng; From[NI] = Top.I; Open.HeapPush({ Ng + H(N), NI }, Less); }
				}
			}
			if (!bFound) { Out.Add(B); return bStraight; }
			// the straight way, if it is clear and costs no more than the way round (crossing a lawn the streets go round)
			if (bStraight && Straight <= G[E] * Cell / 100.f * 1.05f) { Out.Add(B); return true; }
			TArray<FVector2D> Pts;
			TArray<float> Cum;   // the path's cost so far at each point (per metre)
			for (int32 I = E; I != -1; I = From[I]) { Pts.Add(Centre(At(I))); Cum.Add(G[I] * Cell / 100.f); }
			Algo::Reverse(Pts); Algo::Reverse(Cum);
			Pts[0] = A; Pts.Last() = B;
			// pulled tight: from each point, the furthest later point reachable in a clear straight line that costs no more than the path
			int32 i = 0;
			while (i < Pts.Num() - 1)
			{
				int32 j = Pts.Num() - 1;
				for (; j > i + 1; --j)
				{
					float Cost = 0.f;
					if (LineFree(Pts[i], Pts[j], &Cost) && Cost <= (Cum[j] - Cum[i]) * 1.03f + 0.3f) break;
				}
				Out.Add(Pts[j]);
				i = j;
			}
			return true;
		}
	};

	// Content/Data/Townsfolk.csv (the same CSV rules as the quest files)
	struct FSheet
	{
		TMap<FString, int32> Cols;
		TArray<TArray<FString>> Rows;
		bool Load(const TCHAR* Name)
		{
			FString Text;
			const FString Path = FPaths::ProjectContentDir() / TEXT("Data") / Name;
			if (!FFileHelper::LoadFileToString(Text, *Path)) { UE_LOG(LogTemp, Warning, TEXT("JEEBLIFE cannot read %s"), *Path); return false; }
			TArray<TArray<FString>> All;
			TArray<FString> Row;
			FString Cell;
			bool bQuoted = false, bStarted = false;
			auto EndCell = [&]() { Row.Add(Cell.TrimStartAndEnd()); Cell.Reset(); bStarted = false; };
			auto EndRow = [&]() { EndCell(); if (Row.Num() > 1 || !Row[0].IsEmpty()) All.Add(Row); Row.Reset(); };
			for (int32 i = 0; i < Text.Len(); ++i)
			{
				const TCHAR C = Text[i];
				if (bQuoted)
				{
					if (C == '"') { if (i + 1 < Text.Len() && Text[i + 1] == '"') { Cell.AppendChar('"'); ++i; } else bQuoted = false; }
					else Cell.AppendChar(C);
					continue;
				}
				if (C == '"' && !bStarted) { bQuoted = true; bStarted = true; }
				else if (C == ',') EndCell();
				else if (C == '\n') EndRow();
				else if (C == '\r' || C == 0xFEFF) continue;
				else { Cell.AppendChar(C); if (C != ' ') bStarted = true; }
			}
			if (!Cell.IsEmpty() || Row.Num() > 0) EndRow();
			All.RemoveAll([](const TArray<FString>& R) { return R.Num() > 0 && R[0].StartsWith(TEXT("#")); });
			if (All.Num() == 0) return false;
			for (int32 c = 0; c < All[0].Num(); ++c) Cols.Add(All[0][c].ToLower(), c);
			All.RemoveAt(0);
			Rows = MoveTemp(All);
			return true;
		}
		FString S(int32 R, const TCHAR* Col) const
		{
			const int32* C = Cols.Find(FString(Col).ToLower());
			return (C && Rows[R].IsValidIndex(*C)) ? Rows[R][*C] : FString();
		}
	};

	FString Squash(const FString& S) { return S.Replace(TEXT(" "), TEXT("")).ToLower(); }
	FVector2D Dir2(float Yaw) { const float R = FMath::DegreesToRadians(Yaw); return FVector2D(FMath::Cos(R), FMath::Sin(R)); }
	float YawOf(const FVector2D& V) { return FVector(V.X, V.Y, 0.f).Rotation().Yaw; }

	// ---------------------------------------------------------------------------------------------- the towns' places
	// A spot Out cm in front of an Oak Hill building's door (or a stall's open front), centred on its walls, and the yaw out of the door
	// (the same sums as JeebOakHill::Counters: drawn from the town's plan, so it follows the buildings wherever the plan puts them)
	FVector2D OakFront(const JeebOakHill::FPlot& P, float Out, float& OutYaw)
	{
		const JeebOakHill::FModel* M = JeebOakHill::FindModel(P.Model);
		if (!M) { OutYaw = 0.f; return JeebOakHill::FromPx(P.Px, P.Py); }
		const float Yaw = JeebOakHill::BuildingYaw(P, M), Cs = FMath::Cos(FMath::DegreesToRadians(Yaw)), Sn = FMath::Sin(FMath::DegreesToRadians(Yaw));
		const float LX = (M->WX0 + M->WX1) * 0.5f * M->Scale, LY = -M->WY0 * M->Scale + Out;
		OutYaw = FRotator::NormalizeAxis(Yaw + M->DoorYaw);
		return JeebOakHill::FromPx(P.Px, P.Py) + FVector2D(LX * Cs - LY * Sn, LX * Sn + LY * Cs);
	}
	// An empty plot (the garden, the graveyard, the boxing ring, the bridge's bank): the edge nearest the plaza, Out cm outside it,
	// facing in
	FVector2D OakEdge(const JeebOakHill::FPlot& P, float Out, float& OutYaw)
	{
		const FVector2D C = JeebOakHill::FromPx(P.Px, P.Py), Plaza(JeebOakHill::PosX, JeebOakHill::PosY);
		const FVector2D D = (Plaza - C).GetSafeNormal();
		FVector2D At = C;
		for (int32 k = 0; k < 400; ++k)
		{
			At = C + D * (25.f * k);
			if (JeebOakHill::PlotSD(P, JeebOakHill::EastM(At.Y), JeebOakHill::NorthM(At.X)) * 100.f >= Out) break;
		}
		OutYaw = YawOf(-D);
		return At;
	}
	const JeebOakHill::FPlot* OakPlot(const FString& Name)
	{
		const FString Want = Squash(Name);
		for (const JeebOakHill::FPlot& P : JeebOakHill::Plots()) if (Squash(P.Name) == Want) return &P;
		return nullptr;
	}
	// The Nth house (1 = nearest the plaza's oak), so a home stays a home when the plan moves the houses about
	const JeebOakHill::FPlot* OakHouse(int32 N)
	{
		TArray<const JeebOakHill::FPlot*> H;
		for (const JeebOakHill::FPlot& P : JeebOakHill::Plots())
			if (P.Kind == JeebOakHill::EPlot::Building && P.Model && (FCString::Strcmp(P.Model, TEXT("OakHillHouse")) == 0 || FCString::Strcmp(P.Model, TEXT("OakHillCornerHouse")) == 0)) H.Add(&P);
		const FVector2D Plaza(JeebOakHill::PosX, JeebOakHill::PosY);
		H.StableSort([&](const JeebOakHill::FPlot& A, const JeebOakHill::FPlot& B)
			{ return FVector2D::DistSquared(JeebOakHill::FromPx(A.Px, A.Py), Plaza) < FVector2D::DistSquared(JeebOakHill::FromPx(B.Px, B.Py), Plaza); });
		return H.IsValidIndex(N - 1) ? H[N - 1] : nullptr;
	}

	enum class ETown : uint8 { None, OakHill, Steele, Blackrock };
	ETown TownOf(const FString& S)
	{
		const FString T = Squash(S);
		if (T == TEXT("oakhill")) return ETown::OakHill;
		if (T == TEXT("steelecreek") || T == TEXT("steele")) return ETown::Steele;
		if (T == TEXT("blackrock")) return ETown::Blackrock;
		return ETown::None;
	}
	bool TownOn(ETown T)
	{
		if (T == ETown::OakHill) return JeebTownLab::RealTown();
		if (T == ETown::Steele) return JeebSteele::Enabled();
		return T == ETown::Blackrock;
	}

	// A building's front for a keeper (Shop: what it sells) or an errand: where, facing out of it
	struct FPlace { bool bOk = false; FVector2D At = FVector2D::ZeroVector; float Yaw = 0.f; bool bStall = false; uint8 Shop = 255; };
	// Out: cm in front of the front wall (negative: inside an open stall)
	FPlace Front(ETown Town, const FString& Name, float Out)
	{
		FPlace R;
		if (Town == ETown::OakHill)
		{
			const JeebOakHill::FPlot* P = OakPlot(Name);
			if (!P) return R;
			if (P->Kind != JeebOakHill::EPlot::Building) { R.At = OakEdge(*P, FMath::Max(Out, 100.f), R.Yaw); R.Yaw += 180.f; R.bOk = true; return R; }
			R.At = OakFront(*P, Out, R.Yaw);
			R.bStall = JeebOakHill::IsStall(P->Model);
			JeebOakHill::EShop Sh; if (JeebOakHill::ShopOf(P->Model, Sh)) R.Shop = uint8(Sh);
			R.bOk = true;
		}
		else if (Town == ETown::Steele)
		{
			const JeebSteele::FPlot* P = JeebSteele::FindPlot(*Name);
			if (!P)
				for (const JeebSteele::FPlot& Q : JeebSteele::Plots()) if (Squash(Q.Def.Name) == Squash(Name)) P = &Q;
			if (!P) return R;
			R.At = JeebSteele::DoorSpot(*P, Out, &R.Yaw);
			R.bStall = P->bStall;
			JeebOakHill::EShop Sh; if (JeebOakHill::ShopOf(P->Def.Model, Sh)) R.Shop = uint8(Sh);
			R.bOk = true;
		}
		return R;
	}

	// An errand's spot (where to stand, facing what they came for), by name, in a town
	FPlace Errand(ETown Town, const FString& Name, uint32 Seed, UWorld* W)
	{
		const FString N = Squash(Name);
		FPlace R;
		if (Town == ETown::OakHill)
		{
			if (N == TEXT("oak"))
			{
				// round the oak's lawn, somewhere by the person's seed, facing the tree
				const float A = float(Seed % 360u);
				const FVector2D C(JeebOakHill::PosX, JeebOakHill::PosY);
				R.At = C + Dir2(A) * (JeebOakHill::LawnR * JeebOakHill::UnitCm + 120.f);
				R.Yaw = A + 180.f; R.bOk = true;
				return R;
			}
			static const TCHAR* Alias[][2] = { { TEXT("market"), TEXT("Market Stall") }, { TEXT("potions"), TEXT("Potion Stall") }, { TEXT("potionshop"), TEXT("Potion Shop") },
				{ TEXT("bankstall"), TEXT("Bank Stall") }, { TEXT("townhall"), TEXT("Town Hall") }, { TEXT("ring"), TEXT("Boxing Ring") }, { TEXT("ruins"), TEXT("Castle Ruins") },
				{ TEXT("fort"), TEXT("North Fort") }, { TEXT("smithy"), TEXT("Smith") } };
			FString PlotName = Name;
			for (const auto& A : Alias) if (N == A[0]) PlotName = A[1];
			const JeebOakHill::FPlot* P = OakPlot(PlotName);
			if (!P) return R;
			// a shop: beside its counter (the counter's own spot is the customers'); the well close up; other buildings a little off the door
			JeebOakHill::EShop Sh;
			const bool bShop = P->Kind == JeebOakHill::EPlot::Building && JeebOakHill::ShopOf(P->Model, Sh);
			const bool bWell = P->Model && FCString::Strcmp(P->Model, TEXT("OakHillWell")) == 0;
			R = Front(Town, PlotName, bShop ? 230.f : bWell ? 110.f : 260.f);
			if (!R.bOk) return R;
			if (P->Kind == JeebOakHill::EPlot::Building && bShop) R.At += Dir2(R.Yaw + 90.f) * ((Seed & 1) ? 190.f : -190.f);
			R.Yaw += 180.f;
			return R;
		}
		if (Town == ETown::Steele)
		{
			if (N == TEXT("landing") || N == TEXT("quarry"))
			{
				// the river lane's end at the landing, the mine track's end below the quarry (SteeleCreek.h StreetDefs 3 and 4)
				const TArray<JeebSteele::FStreet>& St = JeebSteele::Streets();
				const int32 K = N == TEXT("landing") ? 3 : 4;
				if (!St.IsValidIndex(K) || St[K].Pts.Num() < 8) return R;
				const TArray<FVector2D>& Pts = St[K].Pts;
				R.At = Pts[Pts.Num() - 6]; R.Yaw = YawOf(Pts.Last() - Pts[Pts.Num() - 6]); R.bOk = true;
				return R;
			}
			const bool bSmithy = N == TEXT("smithy") || N == TEXT("forge");
			const bool bWell = N == TEXT("well");
			R = Front(Town, bSmithy ? FString(TEXT("Smithy")) : Name, bSmithy ? 700.f : bWell ? 110.f : 260.f);
			if (!R.bOk) return R;
			if (bSmithy || R.Shop != 255) R.At += Dir2(R.Yaw + 90.f) * ((Seed & 1) ? 200.f : -200.f);
			R.Yaw += 180.f;
			return R;
		}
		if (Town == ETown::Blackrock)
		{
			const FVector2D VC = JeebTerrain::VillageCenter();
			if (N == TEXT("fountain"))
			{
				// in front of a bench round the fountain, facing it
				const TArray<JeebTerrain::FSeat>& Seats = JeebTerrain::Seats();
				const JeebTerrain::FSeat& S = Seats[Seed % uint32(Seats.Num())];
				R.At = S.Pos + Dir2(S.Yaw) * 110.f; R.Yaw = S.Yaw; R.bOk = true;
				return R;
			}
			if (N == TEXT("market")) { R.At = JeebTerrain::MarketCenter() + FVector2D(0.f, (Seed & 1) ? 300.f : -300.f) + FVector2D(450.f, 0.f); R.Yaw = 180.f; R.bOk = true; return R; }
			if (N == TEXT("shop") || N == TEXT("smith") || N == TEXT("bank"))
			{
				const FVector2D K = N == TEXT("shop") ? JeebTerrain::ShopNpcPos() : N == TEXT("smith") ? JeebTerrain::SmithNpcPos() : JeebTerrain::BankNpcPos();
				R.At = K + FVector2D(0.f, 330.f) + FVector2D((Seed & 1) ? 160.f : -160.f, 0.f); R.Yaw = -90.f; R.bOk = true;
				return R;
			}
			if (N == TEXT("gate")) { R.At = VC + FVector2D(-JeebTerrain::VillageHalf() + 350.f, 0.f); R.Yaw = 180.f; R.bOk = true; return R; }
		}
		return R;
	}

	// A home's doorstep: where, facing out
	bool Home(ETown Town, const FString& Spec, UWorld* W, FVector2D& OutAt, float& OutYaw)
	{
		FString Kind, Num;
		if (!Spec.TrimStartAndEnd().Split(TEXT(" "), &Kind, &Num)) { Kind = Spec; Num = TEXT("1"); }
		const int32 Idx = FCString::Atoi(*Num);
		if (Town == ETown::OakHill)
		{
			const JeebOakHill::FPlot* P = OakHouse(Idx);
			if (!P) return false;
			OutAt = OakFront(*P, 150.f, OutYaw);
			return true;
		}
		if (Town == ETown::Steele)
		{
			const JeebSteele::FPlot* P = JeebSteele::FindPlot(*FString::Printf(TEXT("House%d"), Idx));
			if (!P) return false;
			OutAt = JeebSteele::DoorSpot(*P, 150.f, &OutYaw);
			return true;
		}
		if (Town == ETown::Blackrock)
		{
			const TArray<JeebTerrain::FHutDef>& Huts = JeebTerrain::VillageHuts();
			if (!Huts.IsValidIndex(Idx - 1)) return false;
			// the hut there (its door is on its +X, ACottage), 1.3 m out from the doorway
			const ACottage* Best = nullptr; float BestD = 600.f * 600.f;
			for (TActorIterator<ACottage> It(W); It; ++It)
			{
				const float D = FVector2D::DistSquared(FVector2D(It->GetActorLocation()), Huts[Idx - 1].Pos);
				if (D < BestD) { BestD = D; Best = *It; }
			}
			if (!Best) return false;
			OutYaw = Best->GetActorRotation().Yaw;
			OutAt = FVector2D(Best->DoorPos()) + Dir2(OutYaw) * 130.f;
			return true;
		}
		return false;
	}

	ANpc* FindByName(UWorld* W, const FString& Name)
	{
		for (TActorIterator<ANpc> It(W); It; ++It)
		{
			const FString N = It->GetNpcName();
			if (N.Equals(Name, ESearchCase::IgnoreCase) || N.StartsWith(Name + TEXT(","), ESearchCase::IgnoreCase)) return *It;
		}
		return nullptr;
	}

	uint32 Seed(const FString& S) { return GetTypeHash(S) * 2654435761u; }
	float Hash01(uint32 S, int32 K) { const uint32 H = (S >> ((K * 5) % 29)) ^ (S * uint32(2 * K + 7)) ^ (uint32(K) * 2246822519u); return float((H ^ (H >> 13)) % 1000u) / 1000.f; }
}

// ======================================================================================================== ATownLife
ATownLife::ATownLife()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	bReplicates = false;
}

void ATownLife::BeginPlay()
{
	Super::BeginPlay();
	// (built half a second in, on every machine: the physics scene only answers traces and overlaps for the towns' and props' solids
	// a frame or more after they are made, and the routes and the keepers' spots behind the counters need them)
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, this, &ATownLife::Build, 0.5f, false);
}

void ATownLife::Build()
{
	UWorld* W = GetWorld();
	if (!W) return;
	const double T0 = FPlatformTime::Seconds();
	FWalkMap Map; Map.W = W;
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	uint32 Hash = 0;
	auto HashPt = [&](const FVector2D& P) { Hash = HashCombine(Hash, GetTypeHash(FIntPoint(FMath::RoundToInt(P.X), FMath::RoundToInt(P.Y)))); };
	TSet<ANpc*> Done;
	FString Report;

	// A keeper's round: their spot behind the counter (or in the doorway), facing the customers; now and then a step along the
	// counter and back (if there is room), never far
	auto KeeperRound = [&](ANpc* N, const FVector2D& K, float Yaw, bool bStall)
	{
		TSharedPtr<FRound> R = MakeShared<FRound>();
		R->Speed = 70.f; R->Anchor = K; R->Label = N->GetNpcName();
		const uint32 S = Seed(R->Label);
		const float Step = bStall ? 110.f : 70.f;
		FVector2D Side = Dir2(Yaw + 90.f) * Step * ((S & 1) ? 1.f : -1.f);
		if (!Map.SolidFree(K + Side, 26.f)) Side = -Side;
		const bool bStep = Map.SolidFree(K + Side, 26.f);
		FBuilder B(*R, K, Yaw);
		B.Stay(35.f + 25.f * Hash01(S, 1), Yaw);
		if (bStep) { B.WalkTo(K + Side); B.Stay(14.f + 10.f * Hash01(S, 2), Yaw); B.WalkTo(K); }
		B.Finish(0.f);
		R->Roam = bStep ? Step : 0.f;
		R->Offset = 97.f * Hash01(S, 3);
		N->SetRound(R);
		Done.Add(N);
		HashPt(K);
		Report += FString::Printf(TEXT("\n  keeper %s at %.0f,%.0f facing %.0f%s"), *R->Label, K.X, K.Y, Yaw, bStep ? TEXT("") : TEXT(" (no room for a step)"));
	};
	// A keeper's spot: inside an open stall, behind its counter (70 cm in from the front), or on a building's doorstep (45 cm out),
	// stepping out until a person fits there
	auto KeeperSpot = [&](ETown Town, const FString& Place, FPlace& Out) -> bool
	{
		const FPlace Probe = Front(Town, Place, 0.f);
		if (!Probe.bOk) return false;
		static const float StallOuts[] = { -70.f, -45.f, -100.f, -130.f, -20.f, 40.f };
		static const float DoorOuts[] = { 45.f, 70.f, 95.f, 120.f };
		const bool bSmithy = Town == ETown::Steele && Squash(Place) == TEXT("smithy");
		TArray<float> Outs;
		if (!bSmithy && Probe.bStall)
		{
			// (a stall: walk in from the customers' side; once past the counter (a solid top), the first spot a person fits)
			// (vertical line traces, three abreast: a town's own mesh answers traces, not overlaps)
			auto Top = [&](const FVector2D& At, const FVector2D& Across) -> float
			{
				float Best = -1.f;
				for (int32 k = -1; k <= 1; ++k)
				{
					const FVector2D Q2 = At + Across * (22.f * k);
					const float G = JeebLife::DrawnGround(Q2.X, Q2.Y);
					FHitResult H;
					FCollisionQueryParams Q(SCENE_QUERY_STAT(TownLifeCounter), true);
					if (Map.W->LineTraceSingleByObjectType(H, FVector(Q2.X, Q2.Y, G + 150.f), FVector(Q2.X, Q2.Y, G + 35.f), FCollisionObjectQueryParams(ECC_WorldStatic), Q))
						Best = FMath::Max(Best, float(H.ImpactPoint.Z) - G);
				}
				return Best;
			};
			FString Prof;
			bool bPast = false;
			for (float O = 90.f; O >= -320.f; O -= 15.f)
			{
				const FPlace P = Front(Town, Place, O);
				if (!P.bOk) break;
				const float H = Top(P.At, Dir2(P.Yaw + 90.f));
				Prof += FString::Printf(TEXT(" %.0f:%.0f"), O, H);
				if (H > 0.f) bPast = true;
				else if (bPast && Top(Front(Town, Place, O - 20.f).At, Dir2(P.Yaw + 90.f)) < 0.f)
				{
					Out = Front(Town, Place, O - 20.f);
					UE_LOG(LogTemp, Log, TEXT("JEEBLIFE counter %s: keeper %.0f cm in (tops%s)"), *Place, -(O - 20.f), *Prof);
					return true;
				}
			}
			// (none clear behind it: the bank stall's counter is one deep solid box, the potion stall has shelves right behind its low
			// table; 70 cm in reads right for both: behind the bank's counter, at the front of the potion table)
			UE_LOG(LogTemp, Log, TEXT("JEEBLIFE counter %s: none clear behind it, keeper 70 cm in (tops%s)"), *Place, *Prof);
			Out = Front(Town, Place, -70.f);
			return Out.bOk;
		}
		if (bSmithy) Outs = { 380.f, 340.f, 420.f };   // (Steele Creek's smith at his anvil in the forge yard, as SteeleCreekTown.cpp places it)
		else if (Probe.bStall) Outs = TArray<float>(StallOuts, UE_ARRAY_COUNT(StallOuts));
		else Outs = TArray<float>(DoorOuts, UE_ARRAY_COUNT(DoorOuts));
		for (float O : Outs)
		{
			const FPlace P = Front(Town, Place, O);
			if (P.bOk && Map.SolidFree(P.At, 26.f)) { Out = P; return true; }
		}
		Out = Front(Town, Place, Outs[0]);
		return Out.bOk;
	};

	// ------------------------------------------------------------------ Townsfolk.csv: keepers and the people with homes
	struct FFolk { ANpc* N; ETown Town; FVector2D Door; float DoorYaw; TArray<FPlace> Errands; TSharedPtr<FRound> R; float ArriveT = 0.f; int32 Pair = -1; int32 Side = 0; };
	TArray<FFolk> Folk;
	FSheet Sheet;
	if (Sheet.Load(TEXT("Townsfolk.csv")))
		for (int32 r = 0; r < Sheet.Rows.Num(); ++r)
		{
			const FString Kind = Sheet.S(r, TEXT("Kind")).ToLower(), Name = Sheet.S(r, TEXT("Name"));
			const ETown Town = TownOf(Sheet.S(r, TEXT("Town")));
			if (Name.IsEmpty() || Town == ETown::None || !TownOn(Town)) continue;
			const FString Place = Sheet.S(r, TEXT("Place")), Model = Sheet.S(r, TEXT("Model")), FolkS = Sheet.S(r, TEXT("Folk")).ToLower(), Hello = Sheet.S(r, TEXT("Greeting"));
			FLinearColor Tint(-1.f, 0.f, 0.f);
			{
				TArray<FString> C; Sheet.S(r, TEXT("Tint")).ParseIntoArrayWS(C);
				if (C.Num() >= 3) Tint = FLinearColor(FCString::Atof(*C[0]), FCString::Atof(*C[1]), FCString::Atof(*C[2]));
			}
			const uint8 FolkK = FolkS == TEXT("troll") ? 1 : FolkS == TEXT("wizard") ? 2 : 0;
			ANpc* N = FindByName(W, Name);
			if (Kind == TEXT("keeper"))
			{
				FPlace K;
				if (!KeeperSpot(Town, Place, K)) { UE_LOG(LogTemp, Warning, TEXT("JEEBLIFE Townsfolk.csv: no place %s for %s"), *Place, *Name); continue; }
				if (!N)
				{
					const ENpcRole KRole = K.Shop == uint8(JeebOakHill::EShop::Smith) ? ENpcRole::Blacksmith : K.Shop == uint8(JeebOakHill::EShop::Bank) ? ENpcRole::Banker : ENpcRole::Shopkeeper;
					N = W->SpawnActor<ANpc>(FVector(K.At.X, K.At.Y, DrawnGround(K.At.X, K.At.Y) + 92.f), FRotator(0.f, K.Yaw, 0.f), SP);
					if (!N) continue;
					N->SetFolk(FolkK, Name, Hello);
					if (!Model.IsEmpty()) N->SetLookHint(Model, Tint);
					N->Setup(KRole);
				}
				else if (!Model.IsEmpty()) { N->SetLookHint(Model, Tint); N->RebuildBody(); }
				KeeperRound(N, K.At, K.Yaw, K.bStall);
				continue;
			}
			if (Kind != TEXT("folk")) continue;
			FFolk F; F.Town = Town;
			if (!Home(Town, Place, W, F.Door, F.DoorYaw)) { UE_LOG(LogTemp, Warning, TEXT("JEEBLIFE Townsfolk.csv: no home %s for %s"), *Place, *Name); continue; }
			if (!N)
			{
				N = W->SpawnActor<ANpc>(FVector(F.Door.X, F.Door.Y, DrawnGround(F.Door.X, F.Door.Y) + 92.f), FRotator(0.f, F.DoorYaw, 0.f), SP);
				if (!N) continue;
				N->SetFolk(FolkK, Name, Hello);
				N->SetLookHint(Model.IsEmpty() ? FString(TEXT("Townsman")) : Model, Tint);
				N->Setup(ENpcRole::Townsfolk);
			}
			else if (!Model.IsEmpty()) { N->SetLookHint(Model, Tint); N->RebuildBody(); }
			F.N = N;
			TArray<FString> Es; Sheet.S(r, TEXT("Errands")).ParseIntoArrayWS(Es);
			for (const FString& E : Es)
			{
				const FPlace P = Errand(Town, E, Seed(Name), W);
				if (P.bOk) F.Errands.Add(P);
				else UE_LOG(LogTemp, Warning, TEXT("JEEBLIFE Townsfolk.csv: %s has no errand %s here"), *Name, *E);
			}
			Folk.Add(F);
		}

	// Pairs: the first two (then the next two) of a town with the same first errand meet there, standing 1.4 m apart
	for (int32 i = 0; i < Folk.Num(); ++i)
	{
		if (Folk[i].Pair >= 0 || Folk[i].Errands.Num() == 0) continue;
		for (int32 j = i + 1; j < Folk.Num(); ++j)
			if (Folk[j].Pair < 0 && Folk[j].Town == Folk[i].Town && Folk[j].Errands.Num() > 0 && FVector2D::Distance(Folk[j].Errands[0].At, Folk[i].Errands[0].At) < 50.f)
			{
				Folk[i].Pair = j; Folk[j].Pair = i; Folk[i].Side = -1; Folk[j].Side = 1;
				break;
			}
	}

	// Their rounds: the doorstep, a potter round the yard, the errands, home. Every round in a town lasts as long as the longest, so
	// partners arrive at their meeting together
	TMap<uint8, float> TownPeriod;
	for (FFolk& F : Folk)
	{
		TSharedPtr<FRound> R = MakeShared<FRound>();
		R->Speed = 95.f; R->bNightHome = true; R->Label = F.N->GetNpcName(); R->Anchor = F.Door;
		const uint32 S = Seed(R->Label);
		FBuilder B(*R, F.Door, F.DoorYaw);
		TArray<FVector2D> Way;
		B.Stay(6.f + 8.f * Hash01(S, 1), F.DoorYaw);
		// the yard: up to two spots beside the door along the house's front, a little out
		const FVector2D Fwd = Dir2(F.DoorYaw), Right = Dir2(F.DoorYaw + 90.f);
		int32 Yards = 0;
		for (int32 k = 0; k < 4 && Yards < 2; ++k)
		{
			const float Sd = ((k + (S & 1)) % 2 == 0) ? 1.f : -1.f;
			const FVector2D Y = F.Door + Right * Sd * (k < 2 ? 280.f : 180.f) + Fwd * (k < 2 ? 70.f : 220.f);
			if (!Map.LineFree(B.Pos, Y)) continue;
			B.WalkTo(Y);
			B.Stay(8.f + 7.f * Hash01(S, 2 + k), F.DoorYaw + Sd * (50.f + 30.f * Hash01(S, 6 + k)));
			++Yards;
		}
		if (Yards > 0) { Map.Route(B.Pos, F.Door, Way); B.Path(Way); B.Stay(3.f, F.DoorYaw); }
		for (int32 e = 0; e < F.Errands.Num() && e < 2; ++e)
		{
			FPlace E = F.Errands[e];
			const bool bMeet = e == 0 && F.Pair >= 0;
			if (bMeet) E.At += Dir2(E.Yaw + 90.f) * 70.f * F.Side;
			Map.Route(B.Pos, E.At, Way);
			B.Path(Way);
			if (bMeet) F.ArriveT = B.T;
			B.Stay(bMeet ? 38.f : 16.f + 10.f * Hash01(S, 8 + e), E.Yaw, bMeet);
		}
		Map.Route(B.Pos, F.Door, Way);
		B.Path(Way);
		F.R = R;
		float& P = TownPeriod.FindOrAdd(uint8(F.Town));
		P = FMath::Max(P, B.T);
		R->Roam = 0.f;
		for (const FSeg& Sg : R->Segs) R->Roam = FMath::Max(R->Roam, float(FVector2D::Distance(Sg.A, F.Door)));
	}
	// (the final turn and stay at the door, by hand: the same sums as FBuilder::Finish, to the town's period)
	for (FFolk& F : Folk)
	{
		FRound& R = *F.R;
		const float Period = FMath::Max(300.f, FMath::CeilToFloat((TownPeriod.FindRef(uint8(F.Town)) + 25.f) / 10.f) * 10.f);
		const float End = R.Segs.Num() ? R.Segs.Last().T0 + R.Segs.Last().Dur : 0.f;
		const float LastYaw = R.Segs.Num() ? (R.Segs.Last().Kind == ESeg::Turn ? R.Segs.Last().YawB : R.Segs.Last().YawA) : F.DoorYaw;
		float T = End;
		const float D = FMath::Abs(FMath::FindDeltaAngleDegrees(LastYaw, F.DoorYaw));
		if (D > 8.f) { FSeg S; S.Kind = ESeg::Turn; S.A = S.B = F.Door; S.YawA = LastYaw; S.YawB = F.DoorYaw; S.T0 = T; S.Dur = D / 140.f + 0.15f; R.Segs.Add(S); T += S.Dur; }
		if (Period > T + 0.05f) { FSeg S; S.Kind = ESeg::Stay; S.A = S.B = F.Door; S.YawA = S.YawB = F.DoorYaw; S.T0 = T; S.Dur = Period - T; R.Segs.Add(S); T = Period; }
		R.Period = FMath::Max(T, Period);
		R.Offset = R.Period * Hash01(Seed(R.Label), 4);
	}
	// partners' clocks: both arrive at their meeting at the same moment
	for (FFolk& F : Folk)
		if (F.Pair >= 0 && F.Side > 0)
		{
			FFolk& A = Folk[F.Pair];
			F.R->Offset = FMath::Fmod(A.R->Offset + F.ArriveT - A.ArriveT + 4.f * F.R->Period, F.R->Period);
			F.R->Partner = A.R; A.R->Partner = F.R;
		}
	for (FFolk& F : Folk)
	{
		F.N->SetRound(F.R);
		Done.Add(F.N);
		for (const FSeg& S : F.R->Segs) HashPt(S.A);
		Report += FString::Printf(TEXT("\n  folk %s home %.0f,%.0f round %.0f s, %d steps, %d errands%s, reach %.0f m"), *F.R->Label, F.Door.X, F.Door.Y, F.R->Period, F.R->Segs.Num(),
			F.Errands.Num(), F.Pair >= 0 ? *FString::Printf(TEXT(", meets %s"), *Folk[F.Pair].R->Label) : TEXT(""), F.R->Roam / 100.f);
	}

	// ------------------------------------------------------------------ Oak Hill's quest givers: a few steps round their post
	{
		TArray<ANpc*> Givers;
		for (TActorIterator<ANpc> It(W); It; ++It) if (It->IsQuestGiver() && !Done.Contains(*It) && JeebOakHill::InBox(It->PlacedAt.X, It->PlacedAt.Y)) Givers.Add(*It);
		Givers.Sort([](const ANpc& A, const ANpc& B) { return A.GetPersonKey().LexicalLess(B.GetPersonKey()); });
		const FVector2D Oak(JeebOakHill::PosX, JeebOakHill::PosY);
		// neighbours within 9 m with a clear line between them meet halfway
		TArray<int32> Mate; Mate.Init(-1, Givers.Num());
		for (int32 i = 0; i < Givers.Num(); ++i)
			for (int32 j = i + 1; j < Givers.Num() && Mate[i] < 0; ++j)
			{
				const FVector2D A(Givers[i]->PlacedAt), Bp(Givers[j]->PlacedAt);
				if (Mate[j] < 0 && FVector2D::Distance(A, Bp) < 900.f && Map.LineFree(A, Bp)) { Mate[i] = j; Mate[j] = i; }
			}
		TArray<TSharedPtr<FRound>> Rs; TArray<float> Meet;
		for (int32 i = 0; i < Givers.Num(); ++i)
		{
			ANpc* N = Givers[i];
			const FVector2D A(N->PlacedAt);
			const float Yaw0 = N->PlacedYaw;
			TSharedPtr<FRound> R = MakeShared<FRound>();
			R->Speed = 80.f; R->Anchor = A; R->Label = N->GetNpcName(); R->bNightHome = false;
			const uint32 S = Seed(R->Label);
			FBuilder B(*R, A, Yaw0);
			B.Stay(20.f + 15.f * Hash01(S, 1), Yaw0);
			float MeetT = -1.f;
			for (int32 k = 0; k < 2; ++k)
			{
				// a spot 2-3 m off, the first clear one round the circle from the person's own angle
				for (int32 a = 0; a < 8; ++a)
				{
					const FVector2D Q = A + Dir2(float(S % 360u) + 140.f * k + 45.f * a) * (220.f + 80.f * Hash01(S, 2 + k));
					if (!Map.LineFree(A, Q)) continue;
					B.WalkTo(Q);
					const float Look = YawOf(Oak - Q) + (Hash01(S, 4 + k) - 0.5f) * 120.f;
					B.Stay(10.f + 8.f * Hash01(S, 6 + k), Look);
					B.WalkTo(A);
					B.Stay(12.f + 10.f * Hash01(S, 8 + k), Yaw0);
					break;
				}
				if (k == 0 && Mate[i] >= 0)
				{
					const FVector2D O(Givers[Mate[i]]->PlacedAt);
					const FVector2D Mid = (A + O) * 0.5f + (A - O).GetSafeNormal() * 65.f;
					B.WalkTo(Mid);
					MeetT = B.T;
					B.Stay(32.f, YawOf(O - A), true);
					B.WalkTo(A);
					B.Stay(8.f, Yaw0);
				}
			}
			R->Roam = 0.f;
			for (const FSeg& Sg : R->Segs) R->Roam = FMath::Max3(R->Roam, float(FVector2D::Distance(Sg.A, A)), float(FVector2D::Distance(Sg.B, A)));
			Rs.Add(R); Meet.Add(MeetT);
			B.Finish(0.f);
		}
		// partners share one period (the longer) and arrive together
		for (int32 i = 0; i < Givers.Num(); ++i)
		{
			FRound& R = *Rs[i];
			R.Offset = 113.f * Hash01(Seed(R.Label), 3);
			const int32 j = Mate[i];
			if (j < 0) continue;
			const float P = FMath::Max(Rs[i]->Period, Rs[j]->Period);
			for (const TSharedPtr<FRound>& X : { Rs[i], Rs[j] })
			{
				FSeg& Last = X->Segs.Last();
				if (Last.Kind == ESeg::Stay) Last.Dur += P - X->Period;
				else { FSeg S; S.Kind = ESeg::Stay; S.A = S.B = Last.Kind == ESeg::Walk ? Last.B : Last.A; S.YawA = S.YawB = X->Segs[0].YawA; S.T0 = X->Period; S.Dur = P - X->Period; X->Segs.Add(S); }
				X->Period = P;
			}
			if (j < i) { R.Offset = FMath::Fmod(Rs[j]->Offset + Meet[i] - Meet[j] + 4.f * P, P); R.Partner = Rs[j]; Rs[j]->Partner = Rs[i]; }
		}
		for (int32 i = 0; i < Givers.Num(); ++i)
		{
			Givers[i]->SetRound(Rs[i]);
			Done.Add(Givers[i]);
			for (const FSeg& S : Rs[i]->Segs) HashPt(S.A);
			Report += FString::Printf(TEXT("\n  quest giver %s round %.0f s, reach %.1f m%s"), *Rs[i]->Label, Rs[i]->Period, Rs[i]->Roam / 100.f, Mate[i] >= 0 ? *FString::Printf(TEXT(", meets %s"), *Givers[Mate[i]]->GetNpcName()) : TEXT(""));
		}
	}

	// ------------------------------------------------------------------ everyone else with a beat: on the clock, round obstacles
	for (TActorIterator<ANpc> It(W); It; ++It)
	{
		ANpc* N = *It;
		if (Done.Contains(N) || N->GetPatrol().Num() < 2) continue;
		const TArray<FVector2D>& Pts = N->GetPatrol();
		const bool bKnight = N->GetRole() == ENpcRole::GateKnight;
		TSharedPtr<FRound> R = MakeShared<FRound>();
		R->Speed = bKnight ? 120.f : 95.f; R->bNightHome = !bKnight; R->Label = N->GetNpcName(); R->Anchor = Pts[0];
		const uint32 S = Seed(R->Label);
		FBuilder B(*R, Pts[0], YawOf(Pts[1] - Pts[0]));
		TArray<FVector2D> Way;
		for (int32 i = 0; i <= Pts.Num(); ++i)
		{
			const FVector2D& To = Pts[i % Pts.Num()];
			if (i > 0) { Map.Route(B.Pos, To, Way); B.Path(Way); }
			if (i < Pts.Num()) B.Stay((bKnight ? 4.f : 2.f + 5.f * Hash01(S, i % 6)), i + 1 < Pts.Num() ? YawOf(Pts[i + 1] - To) : YawOf(Pts[0] - To));
		}
		B.Finish(0.f);
		R->Offset = R->Period * Hash01(S, 5);
		for (const FSeg& Sg : R->Segs) { R->Roam = FMath::Max(R->Roam, float(FVector2D::Distance(Sg.A, Pts[0]))); HashPt(Sg.A); }
		N->SetRound(R);
		Done.Add(N);
		Report += FString::Printf(TEXT("\n  beat %s round %.0f s, %d steps"), *R->Label, R->Period, R->Segs.Num());
	}

	UE_LOG(LogTemp, Log, TEXT("JEEBLIFE built %d rounds in %.0f ms (%d walk tests), hash %08x:%s"), Done.Num(), (FPlatformTime::Seconds() - T0) * 1000.0, Map.Tests, Hash, *Report);
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("LifeLog")) || FCString::Strifind(FCommandLine::Get(), TEXT("LifeShots="))) SetActorTickEnabled(true);
#endif
}

#if !UE_BUILD_SHIPPING
// (test: -LifeLog) every 5 s of the clock, where the clock has the townsfolk and quest givers, the same moment on every machine, so a
// server's and a client's logs can be laid side by side. (test: -LifeShots=<name>[@front|@feet]+...) from 12 s, every 4 s a camera
// looks at that person (their feet from low beside them, or from the customers' side), the knight put 10 m off so they tick in full;
// Saved/Shots/life_<n>.png, then quits. JEEBLIFESHOT logs their feet against the ground drawn, the exact height and a trace.
void ATownLife::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* W = GetWorld();
	const double Clock = JeebLife::Now(W);
	static const bool bLog = FParse::Param(FCommandLine::Get(), TEXT("LifeLog"));
	const int64 Slot = int64(FMath::FloorToDouble(Clock / 5.0));
	if (bLog && Slot != LastLogSlot)
	{
		LastLogSlot = Slot;
		TArray<FString> Parts;
		for (TActorIterator<ANpc> It(W); It; ++It)
		{
			if (It->GetRole() != ENpcRole::Townsfolk && It->GetRole() != ENpcRole::QuestGiver) continue;
			const TSharedPtr<JeebLife::FRound>& R = It->GetRound();
			if (!R.IsValid()) continue;
			const JeebLife::FPose P = JeebLife::Eval(*R, double(Slot) * 5.0);
			Parts.Add(FString::Printf(TEXT("%s %.0f,%.0f %s"), *It->GetNpcName().Left(12), P.At.X, P.At.Y, P.bWalk ? TEXT("walk") : P.bChat ? TEXT("chat") : P.bTurn ? TEXT("turn") : TEXT("stay")));
		}
		Parts.Sort();
		UE_LOG(LogTemp, Log, TEXT("JEEBLIFE clock %.0f%s | %s"), double(Slot) * 5.0, JeebLife::IsNight(double(Slot) * 5.0) ? TEXT(" night") : TEXT(""), *FString::Join(Parts, TEXT(" | ")));
	}

	static FString List;
	static const bool bShots = FParse::Value(FCommandLine::Get(), TEXT("LifeShots="), List, false);
	if (!bShots) return;
	static TArray<FString> Names;
	if (Names.Num() == 0) List.ParseIntoArray(Names, TEXT("+"));
	APlayerController* PC = W->GetFirstPlayerController();   // (test pictures only)
	APawn* Me = PC ? PC->GetPawn() : nullptr;
	if (!Me) return;
	const float T = W->GetTimeSeconds() - 12.f;
	if (T < 0.f) return;
	const int32 Idx = FMath::FloorToInt(T / 4.f);
	const float In = T - Idx * 4.f;
	static ACameraActor* Cam = nullptr;
	static int32 Shot = -1;
	if (Idx >= Names.Num()) { static bool bQuit = false; if (!bQuit && In > 1.f) { bQuit = true; FPlatformMisc::RequestExit(false); } return; }
	FString Who = Names[Idx], Mode = TEXT("feet");
	Who.Split(TEXT("@"), &Who, &Mode);
	ANpc* N = nullptr;
	for (TActorIterator<ANpc> It(W); It; ++It) if (It->GetNpcName().StartsWith(Who)) { N = *It; break; }
	if (!N) { if (Shot < Idx) { Shot = Idx; UE_LOG(LogTemp, Warning, TEXT("JEEBLIFESHOT %d no %s"), Idx, *Who); } return; }
	if (!Cam) { Cam = W->SpawnActor<ACameraActor>(); if (Cam) { Cam->GetCameraComponent()->bConstrainAspectRatio = false; Cam->GetCameraComponent()->SetFieldOfView(60.f); PC->SetViewTargetWithBlend(Cam); } }
	if (!Cam) return;
	const FVector L = N->GetActorLocation();
	const float Feet = L.Z - 92.f * N->GetActorScale3D().Z;
	const bool bFront = Mode == TEXT("front");
	const FVector Dir = FRotator(0.f, N->GetActorRotation().Yaw + (bFront ? 0.f : 55.f), 0.f).Vector();
	const FVector Eye = bFront ? FVector(L.X, L.Y, Feet) + Dir * 420.f + FVector(0.f, 0.f, 175.f) : FVector(L.X, L.Y, Feet) + Dir * 260.f + FVector(0.f, 0.f, 45.f);
	const FVector Aim = FVector(L.X, L.Y, Feet + (bFront ? 110.f : 40.f));
	Cam->SetActorLocationAndRotation(Eye, (Aim - Eye).Rotation());
	if (In < 0.2f) { const FVector K = FVector(L.X, L.Y, L.Z) + Dir * 1000.f; Me->SetActorLocation(FVector(K.X, K.Y, JeebLife::DrawnGround(K.X, K.Y) + 120.f), false, nullptr, ETeleportType::TeleportPhysics); }
	if (In > 2.6f && Shot < Idx)
	{
		Shot = Idx;
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(LifeShot), false, N);
		const bool bHit = W->LineTraceSingleByObjectType(Hit, FVector(L.X, L.Y, Feet + 120.f), FVector(L.X, L.Y, Feet - 200.f), FCollisionObjectQueryParams(ECC_WorldStatic), Q);
		UE_LOG(LogTemp, Log, TEXT("JEEBLIFESHOT %d %s at %.0f,%.0f: feet %.1f, drawn ground %.1f, exact height %.1f, trace %s %.1f, road %.0f cm"), Idx, *N->GetNpcName(), L.X, L.Y, Feet,
			JeebLife::DrawnGround(L.X, L.Y), JeebTerrain::HeightAt(L.X, L.Y), bHit && Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("none"), bHit ? float(Hit.ImpactPoint.Z) : 0.f, JeebTerrain::PathClearance(L.X, L.Y));
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / FString::Printf(TEXT("life_%d.png"), Idx), false, false);
	}
}
#else
void ATownLife::Tick(float DeltaSeconds) { Super::Tick(DeltaSeconds); }
#endif

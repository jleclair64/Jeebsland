#include "KnightCharacter.h"
#include "KnightMovement.h"
#include "KnightAnimator.h"
#include "JeebWater.h"
#include "JeebTuning.h"
#include "Terrain.h"
#include "OakHill.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

// Test runs for the rivers' banks and swimming (2026-11-15), test builds only, a standalone game only. Neither needs -JeebShot: they are
// meant for a run that starts like the owner's Play (-JeebOffline: straight into the world at Oak Hill, the normal spawn).
//   -BankScan    once the world is built: the steepest ground along every river (both banks, out to 20 m past the water), how deep and
//                wide the swimmable middle is, and whether the bank cap cut into any building's ground (log JEEBBANK)
//   -WaterWalk   walks the knight by its own keys (AddMovementInput, as a player's keys do): from the normal spawn down Oak Hill's west
//                road into the river, across, up the far bank and back; then (moved there) across four more river spots and into the
//                pond. Logs JEEBWALK lines (depth at his feet, mode, floor slope) and the JEEBSWIM lines of the swim itself; takes
//                <WaterShots>_bank.png (a fixed eye on the Oak Hill bank) and <WaterShots>_oak.png / _pond.png (swimming), then quits.
//   -WaterShots=name   the pictures' name (Saved/Shots), default "water"

#if !UE_BUILD_SHIPPING
namespace
{
	float SlopeDeg(float X, float Y)
	{
		constexpr float E = 50.f;   // (a 1 m stencil: the finest ground, Oak Hill's, is a 1 m grid)
		const float Gx = (JeebTerrain::HeightAt(X + E, Y) - JeebTerrain::HeightAt(X - E, Y)) / (2.f * E);
		const float Gy = (JeebTerrain::HeightAt(X, Y + E) - JeebTerrain::HeightAt(X, Y - E)) / (2.f * E);
		return FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(Gx * Gx + Gy * Gy)));
	}

	void BankScan()
	{
		using namespace JeebWorld;
		const double T0 = FPlatformTime::Seconds();
		const TCHAR* Names[] = { TEXT("main"), TEXT("east"), TEXT("south") };
		const float Walkable = 44.765f;
		UE_LOG(LogTemp, Log, TEXT("JEEBBANK shape %s; water at %.0f; swim starts at %.0f cm deep"), OldBanks() ? TEXT("OLD (-OldRiverBanks)") : TEXT("new"), RiverLevel, JeebTune::V().SwimEnterDepth);
		for (int32 r = 0; r < Rivers().Num(); ++r)
		{
			const FRiver& Rv = Rivers()[r];
			const float Half = Rv.HalfWidth, Reach = Half + 2000.f;
			struct FWorst { float Deg; FVector2D At; float Past; float Over; };
			TArray<FWorst> Stations;
			int32 N = 0, Over25 = 0, Over30 = 0, Over40 = 0, OverWalk = 0, Coast = 0;
			float MaxDry = 0.f, MaxWet = 0.f; FWorst DryAt{}, WetAt{};
			float ShallowMid = 1.0e9f; FVector2D ShallowAt(0.f, 0.f);
			float NarrowSwim = 1.0e9f, WideSwim = 0.f;
			for (int32 i = 0; i + 1 < Rv.Pts.Num(); ++i)
			{
				const FVector2D A = Rv.Pts[i], B = Rv.Pts[i + 1];
				const float Len = FVector2D::Distance(A, B);
				const FVector2D Dir = (B - A).GetSafeNormal(), Side(-Dir.Y, Dir.X);
				for (float s = 0.f; s < Len; s += 200.f)
				{
					const FVector2D C = A + Dir * s;
					if (C.Y - CoastY(C.X) > -4000.f) { ++Coast; continue; }   // (the river's mouth: the coast's beach shapes it there)
					const float Mid = RiverLevel - JeebTerrain::HeightAt(C.X, C.Y);
					if (Mid < ShallowMid) { ShallowMid = Mid; ShallowAt = C; }
					float Wd = 0.f;
					for (const float Sg : { -1.f, 1.f })
						for (float o = 0.f; o < Half + 600.f; o += 25.f)
						{
							const FVector2D P = C + Side * Sg * o;
							if (RiverLevel - JeebTerrain::HeightAt(P.X, P.Y) < JeebTune::V().SwimEnterDepth) { Wd += o; break; }
						}
					NarrowSwim = FMath::Min(NarrowSwim, Wd); WideSwim = FMath::Max(WideSwim, Wd);
					FWorst St{ 0.f, C, 0.f, 0.f };
					for (const float Sg : { -1.f, 1.f })
						for (float o = 0.f; o <= Reach; o += 50.f)
						{
							const FVector2D P = C + Side * Sg * o;
							float Hn;
							if (RiverDistance(P.X, P.Y, Hn) < o - 150.f) break;   // (nearer another stretch or river: counted there)
							const float H = JeebTerrain::HeightAt(P.X, P.Y);
							const float D = SlopeDeg(P.X, P.Y);
							++N;
							Over25 += D > 25.f; Over30 += D > 30.f; Over40 += D > 40.f; OverWalk += D > Walkable;
							const FWorst W{ D, P, o - (Half + 100.f), H - RiverLevel };
							if (H >= RiverLevel) { if (D > MaxDry) { MaxDry = D; DryAt = W; } }
							else if (D > MaxWet) { MaxWet = D; WetAt = W; }
							if (D > St.Deg) St = W;
						}
					Stations.Add(St);
				}
			}
			UE_LOG(LogTemp, Log, TEXT("JEEBBANK river %s (half width %.0f): %d stations every 2 m (%d at the mouth skipped), %d ground samples within %.0f m of the middle"),
				Names[r], Half, Stations.Num(), Coast, N, Reach / 100.f);
			UE_LOG(LogTemp, Log, TEXT("JEEBBANK river %s: steepest DRY bank %.1f deg at (%.0f, %.0f), %.1f m past the water's edge, %.2f m over the water"),
				Names[r], MaxDry, DryAt.At.X, DryAt.At.Y, DryAt.Past / 100.f, DryAt.Over / 100.f);
			UE_LOG(LogTemp, Log, TEXT("JEEBBANK river %s: steepest UNDER WATER %.1f deg at (%.0f, %.0f), %.1f m from the water's edge, %.2f m deep"),
				Names[r], MaxWet, WetAt.At.X, WetAt.At.Y, WetAt.Past / 100.f, -WetAt.Over / 100.f);
			UE_LOG(LogTemp, Log, TEXT("JEEBBANK river %s: share of samples over 25 deg %.2f%%, over 30 %.2f%%, over 40 %.2f%%, over the walkable 44.8 %.3f%% (%d)"),
				Names[r], 100.f * Over25 / FMath::Max(N, 1), 100.f * Over30 / FMath::Max(N, 1), 100.f * Over40 / FMath::Max(N, 1), 100.f * OverWalk / FMath::Max(N, 1), OverWalk);
			UE_LOG(LogTemp, Log, TEXT("JEEBBANK river %s: shallowest middle %.0f cm deep at (%.0f, %.0f); swimmable (deeper than %.0f) width %.1f .. %.1f m"),
				Names[r], ShallowMid, ShallowAt.X, ShallowAt.Y, JeebTune::V().SwimEnterDepth, NarrowSwim / 100.f, WideSwim / 100.f);
			Stations.Sort([](const FWorst& a, const FWorst& b) { return a.Deg > b.Deg; });
			TArray<FVector2D> Shown;
			for (const FWorst& W : Stations)
			{
				if (Shown.Num() >= 6) break;
				bool bNear = false;
				for (const FVector2D& S : Shown) bNear |= FVector2D::Distance(S, W.At) < 5000.f;
				if (bNear) continue;
				Shown.Add(W.At);
				UE_LOG(LogTemp, Log, TEXT("JEEBBANK river %s:   worst spot %.1f deg at (%.0f, %.0f), %.1f m past the water's edge, %.2f m over the water%s"),
					Names[r], W.Deg, W.At.X, W.At.Y, W.Past / 100.f, W.Over / 100.f, JeebOakHill::InBox(W.At.X, W.At.Y) ? TEXT(" (Oak Hill)") : TEXT(""));
			}
		}
		// Did the cap cut into ground something stands on? (it may only shape open banks)
		auto Cut = [](float X, float Y) { return JeebTerrain::HeightAt(X, Y, false) - JeebTerrain::HeightAt(X, Y, true); };
		float WorstCut = 0.f; FString WorstWhat = TEXT("-");
		int32 Checked = 0;
		for (const JeebOakHill::FPlot& P : JeebOakHill::Plots())
		{
			const float Cx = JeebOakHill::PosX + P.Cn * 100.f, Cy = JeebOakHill::PosY + P.Ce * 100.f;
			float Mx = 0.f, MinRiver = 1.0e9f;
			const float R = P.Reach * 100.f + 200.f;
			for (float dx = -R; dx <= R; dx += 100.f)
				for (float dy = -R; dy <= R; dy += 100.f)
				{
					const float X = Cx + dx, Y = Cy + dy;
					if (JeebOakHill::PlotSD(P, JeebOakHill::EastM(Y), JeebOakHill::NorthM(X)) > 1.5f) continue;   // (its plot and 1.5 m round it)
					Mx = FMath::Max(Mx, Cut(X, Y));
					float Hn = 0.f; const float Dn = RiverDistance(X, Y, Hn); MinRiver = FMath::Min(MinRiver, Dn - Hn);
				}
			++Checked;
			if (Mx > 0.5f || MinRiver < 1500.f)
				UE_LOG(LogTemp, Log, TEXT("JEEBBANK plot %s (%s): %.1f m from the nearest river's edge line, cap cut %.1f cm"), P.Name, P.Kind == JeebOakHill::EPlot::Building ? TEXT("building") : TEXT("open"), MinRiver / 100.f, Mx);
			if (P.Kind == JeebOakHill::EPlot::Building && Mx > WorstCut) { WorstCut = Mx; WorstWhat = P.Name; }
		}
		TArray<FVector2D> Spots;
		for (const JeebTerrain::FHutDef& Hd : JeebTerrain::VillageHuts()) Spots.Add(Hd.Pos);
		for (const FVector2D& S : JeebTerrain::HutSites()) Spots.Add(S);
		Spots.Add(FVector2D(JeebTerrain::CottagePos())); Spots.Add(JeebTerrain::ShopPos()); Spots.Add(JeebTerrain::SmithyPos()); Spots.Add(JeebTerrain::BankPos()); Spots.Add(JeebTerrain::CastlePos());
		for (const FVector2D& S : Spots)
			for (float dx = -500.f; dx <= 500.f; dx += 100.f)
				for (float dy = -500.f; dy <= 500.f; dy += 100.f)
				{
					const float C = Cut(S.X + dx, S.Y + dy);
					if (C > WorstCut) { WorstCut = C; WorstWhat = FString::Printf(TEXT("Blackrock building at (%.0f, %.0f)"), S.X, S.Y); }
				}
		UE_LOG(LogTemp, Log, TEXT("JEEBBANK buildings: %d Oak Hill plots and %d Blackrock spots checked; the most the bank cap lowered any building's ground: %.1f cm (%s)"), Checked, Spots.Num(), WorstCut, *WorstWhat);
		const FVector2D Pc = JeebTerrain::PondCenter();
		UE_LOG(LogTemp, Log, TEXT("JEEBBANK pond: water %.0f, ground in the middle %.0f (%.0f cm deep); moat water %.0f, ground at the ring's middle %.0f"), JeebTerrain::PondWaterZ(), JeebTerrain::HeightAt(Pc.X, Pc.Y),
			JeebTerrain::PondWaterZ() - JeebTerrain::HeightAt(Pc.X, Pc.Y), JeebWater::MoatZ(), JeebTerrain::HeightAt(JeebTerrain::CastlePos().X - 1625.f, JeebTerrain::CastlePos().Y));
		UE_LOG(LogTemp, Log, TEXT("JEEBBANK scan took %.1f s"), FPlatformTime::Seconds() - T0);
	}

	// One walk: from Start (moved there, or where he stands) along Dir for Ahead cm, then back for Back cm
	struct FLeg { FString Name; FVector2D Start; FVector2D Dir; float Ahead = 0.f, Back = 0.f; bool bMove = true; float Hold = 0.f; float Cross = 0.f; bool bJump = false; };
	struct FWalk
	{
		TArray<FLeg> Legs;
		int32 Leg = -1, Phase = 0;
		float T = 0.f, Clock = 0.f, LogT = 0.f, StuckT = 0.f, SwimT = 0.f, HoldT = 0.f;
		FVector2D LastXY = FVector2D::ZeroVector;
		bool bPlanned = false, bBankShot = false, bWaist = false, bSwam = false, bShot = false, bJumped = false, bOut = false;
		float MaxDepth = 0.f, SwimAtDepth = -1.f;
		TWeakObjectPtr<ACameraActor> Eye;
		FString ShotName = TEXT("water");
	};
	FWalk& W() { static FWalk S; return S; }

	// Where a river's line is nearest a point: the point, and the river's direction there
	void NearestOnRiver(int32 r, const FVector2D& P, FVector2D& OutC, FVector2D& OutDir)
	{
		const TArray<FVector2D>& Pts = JeebWorld::Rivers()[r].Pts;
		float Best = 1.0e18f;
		for (int32 i = 0; i + 1 < Pts.Num(); ++i)
		{
			const FVector2D A = Pts[i], AB = Pts[i + 1] - A;
			const float T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / FMath::Max(AB.SizeSquared(), 1.f), 0.f, 1.f);
			const float D = FVector2D::DistSquared(P, A + AB * T);
			if (D < Best) { Best = D; OutC = A + AB * T; OutDir = AB.GetSafeNormal(); }
		}
	}
	FVector2D AlongRiver(int32 r, float Frac, FVector2D& OutDir)
	{
		const TArray<FVector2D>& Pts = JeebWorld::Rivers()[r].Pts;
		float Total = 0.f;
		for (int32 i = 0; i + 1 < Pts.Num(); ++i) Total += FVector2D::Distance(Pts[i], Pts[i + 1]);
		float Want = Total * Frac;
		for (int32 i = 0; i + 1 < Pts.Num(); ++i)
		{
			const float L = FVector2D::Distance(Pts[i], Pts[i + 1]);
			if (Want <= L) { OutDir = (Pts[i + 1] - Pts[i]).GetSafeNormal(); return Pts[i] + OutDir * Want; }
			Want -= L;
		}
		OutDir = (Pts.Last() - Pts[Pts.Num() - 2]).GetSafeNormal();
		return Pts.Last();
	}
}
#endif

void AKnightCharacter::TickWaterWalk(float Dt)
{
#if !UE_BUILD_SHIPPING
	static const bool bScan = FParse::Param(FCommandLine::Get(), TEXT("BankScan"));
	static const bool bWalk = FParse::Param(FCommandLine::Get(), TEXT("WaterWalk"));
	if ((!bScan && !bWalk) || !IsLocallyControlled() || (GetNetMode() != NM_Standalone && GetNetMode() != NM_Client)) return;   // (on a client joined to a server: the Oak Hill walk only, by its keys; the server runs the moves)
	const bool bOnline = GetNetMode() == NM_Client;
	FWalk& S = W();
	S.Clock += Dt;
	UKnightMovement* KM = KnightMove();
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!KM || !PC) return;
	if (S.Clock < 5.f) return;   // (the world settles)

	if (!S.bPlanned)
	{
		S.bPlanned = true;
		FParse::Value(FCommandLine::Get(), TEXT("WaterShots="), S.ShotName);
		UE_LOG(LogTemp, Log, TEXT("JEEBWALK start: knight at (%.0f, %.0f, %.0f) mode %d, net mode %d, role %d, swim enter %.0f exit %.0f, gravity x%.2f"), GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z,
			int32(KM->MovementMode), int32(GetNetMode()), int32(GetLocalRole()), JeebTune::V().SwimEnterDepth, JeebTune::V().SwimExitDepth, KM->GravityScale);
		if (bScan) BankScan();
		if (!bWalk) { PC->ConsoleCommand(TEXT("quit")); return; }
		// Leg 1, from the normal spawn: due west down Oak Hill's west road to where it meets the river, across, up and back
		const FVector2D Here(GetActorLocation());
		float BestD = 1.0e9f, BestY = Here.Y, Half = 700.f;
		for (float Y = Here.Y; Y > Here.Y - 12000.f; Y -= 50.f) { float Hn; const float D = JeebWorld::RiverDistance(Here.X, Y, Hn); if (D < BestD) { BestD = D; BestY = Y; Half = Hn; } }
		const float Top = JeebWorld::BankTop(Half);
		{
			FLeg L; L.Name = TEXT("oak"); L.Start = Here; L.Dir = FVector2D(0.f, -1.f); L.bMove = false; L.Cross = Here.Y - BestY;
			L.Ahead = L.Cross + Top + 800.f; L.Back = 2.f * Top + 1600.f;
			S.Legs.Add(L);
		}
		// Four more spots along the rivers (moved there: a test shortcut), each crossed from 10 m beyond the bank's top
		const struct { int32 R; float F; float Side; bool bJump; } Spots[] = { { 0, 0.55f, 1.f, false }, { 1, 0.5f, -1.f, true }, { 2, 0.3f, 1.f, false }, { 2, 0.75f, -1.f, false } };
		for (const auto& Sp : Spots) if (!bOnline)
		{
			FVector2D Dir; const FVector2D C = AlongRiver(Sp.R, Sp.F, Dir);
			const FVector2D Side(-Dir.Y * Sp.Side, Dir.X * Sp.Side);
			const float T2 = JeebWorld::BankTop(JeebWorld::Rivers()[Sp.R].HalfWidth);
			FLeg L; L.Name = FString::Printf(TEXT("river%d@%.2f"), Sp.R, Sp.F); L.Start = C + Side * (T2 + 1000.f); L.Dir = -Side; L.Cross = T2 + 1000.f;
			L.Ahead = L.Cross + T2 + 600.f; L.Back = 2.f * T2 + 1200.f; L.bJump = Sp.bJump;
			S.Legs.Add(L);
		}
		// The pond: from its south side (the pier runs out from the east) to the middle, tread water there, and back out
		if (!bOnline)
		{
			const FVector2D Pc = JeebTerrain::PondCenter();
			FLeg L; L.Name = TEXT("pond"); L.Start = Pc + FVector2D(-1400.f, 0.f); L.Dir = FVector2D(1.f, 0.f); L.Cross = 1400.f;
			L.Ahead = 1400.f; L.Back = 1700.f; L.Hold = 4.f;
			S.Legs.Add(L);
		}
		// The bank picture: a fixed eye beside the water on Oak Hill's side, looking along the river at the slope up to the town
		if (!bOnline)
		{
			const FVector2D C(Here.X, BestY);
			FVector2D RC, RD; NearestOnRiver(0, C, RC, RD);
			const FVector2D Edge = RC + FVector2D(0.f, 1.f) * (Half + 100.f);
			const FVector EyeAt(Edge.X + RD.X * 2200.f, Edge.Y + RD.Y * 2200.f - 300.f, JeebWorld::RiverLevel + 380.f);
			const FVector Look(RC.X + 0.f, RC.Y + Top * 0.8f, JeebWorld::RiverLevel + 40.f);
			if (ACameraActor* E = GetWorld()->SpawnActor<ACameraActor>(EyeAt, (Look - EyeAt).Rotation()))
			{
				E->GetCameraComponent()->SetFieldOfView(60.f);
				E->GetCameraComponent()->bConstrainAspectRatio = false;
				PC->SetViewTarget(E);
				S.Eye = E;
			}
			UE_LOG(LogTemp, Log, TEXT("JEEBWALK oak crossing: river middle at Y %.0f (%.0f m west of the spawn), half width %.0f, bank top %.1f m from the middle; eye at %s"), BestY, (Here.Y - BestY) / 100.f, Half, Top / 100.f, *EyeAt.ToCompactString());
			// the ground across it, every metre (the profile the knight walks)
			FString Prof;
			for (float o = -Top - 600.f; o <= Top + 600.f; o += 100.f) Prof += FString::Printf(TEXT(" %.0f"), JeebTerrain::HeightAt(RC.X, RC.Y + o) - JeebWorld::RiverLevel);
			UE_LOG(LogTemp, Log, TEXT("JEEBWALK oak profile (cm over the water, west to east, every 1 m from %.0f m west of the middle):%s"), (Top + 600.f) / 100.f, *Prof);
		}
		return;
	}
	if (S.Eye.IsValid())
	{
		if (!S.bBankShot && S.Clock > 7.f) { S.bBankShot = true; FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / (S.ShotName + TEXT("_bank.png")), false, false); }
		if (S.Clock > 8.5f) { PC->SetViewTarget(this); S.Eye->Destroy(); }
		return;
	}
	if (!S.bWaist && KM->IsMovingOnGround() && Anim.IsValid())
	{
		S.bWaist = true;
		if (GetFollowCamera()) GetFollowCamera()->SetFieldOfView(42.f);   // (the pictures: closer on him)
		if (PC->GetHUD()) PC->GetHUD()->bShowHUD = false;
		const float Feet = float(GetActorLocation().Z) - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		UE_LOG(LogTemp, Log, TEXT("JEEBWALK body over his feet: hips %.0f, chest %.0f, neck %.0f, head %.0f cm (capsule half height %.0f)"), float(Anim->BoneAt(JeebAnim::Hips).Z) - Feet,
			float(Anim->BoneAt(JeebAnim::Spine2).Z) - Feet, float(Anim->BoneAt(JeebAnim::Neck).Z) - Feet, float(Anim->BoneAt(JeebAnim::Head).Z) - Feet, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}

	// The legs
	auto Report = [&](const TCHAR* How)
	{
		const FLeg& L = S.Legs[S.Leg];
		UE_LOG(LogTemp, Log, TEXT("JEEBWALK leg %s %s: swam %s (it began %.0f cm deep), deepest at his feet walking %.0f, back out on dry ground %s, %.1f s"), *L.Name, How,
			S.bSwam ? TEXT("YES") : TEXT("NO"), S.SwimAtDepth, S.MaxDepth, S.bOut ? TEXT("yes") : TEXT("NO"), S.T);
	};
	auto NextLeg = [&]()
	{
		++S.Leg; S.Phase = 0; S.T = 0.f; S.StuckT = 0.f; S.SwimT = 0.f; S.HoldT = 0.f; S.bSwam = false; S.bShot = false; S.bJumped = false; S.bOut = false; S.MaxDepth = 0.f; S.SwimAtDepth = -1.f;
		if (!S.Legs.IsValidIndex(S.Leg)) { UE_LOG(LogTemp, Log, TEXT("JEEBWALK done")); PC->ConsoleCommand(TEXT("quit")); return; }
		const FLeg& L = S.Legs[S.Leg];
		if (L.bMove)
		{
			if (KM->InWater()) KM->SetMovementMode(MOVE_Falling);
			KM->Velocity = FVector::ZeroVector;
			SetActorLocation(FVector(L.Start.X, L.Start.Y, JeebTerrain::HeightAt(L.Start.X, L.Start.Y) + 110.f), false, nullptr, ETeleportType::TeleportPhysics);
			KM->SetMovementMode(MOVE_Falling);
		}
		S.LastXY = FVector2D(GetActorLocation());
		UE_LOG(LogTemp, Log, TEXT("JEEBWALK leg %s from (%.0f, %.0f) heading (%.2f, %.2f): river middle %.0f cm ahead"), *L.Name, L.Start.X, L.Start.Y, L.Dir.X, L.Dir.Y, L.Cross);
	};
	if (S.Leg < 0) { NextLeg(); return; }
	if (!S.Legs.IsValidIndex(S.Leg)) return;
	const FLeg& L = S.Legs[S.Leg];
	S.T += Dt;
	if (L.bMove && S.T < 1.5f) return;   // (lands first)
	const FVector P = GetActorLocation();
	const float Prog = FVector2D::DotProduct(FVector2D(P) - L.Start, L.Dir);
	const float Sp = FVector2D::Distance(FVector2D(P), S.LastXY) / FMath::Max(Dt, 1e-3f);
	S.LastXY = FVector2D(P);
	const bool bInWater = KM->InWater();
	if (bInWater && !S.bSwam) { S.bSwam = true; S.SwimAtDepth = KM->WaterDepth; }
	if (!bInWater && KM->IsMovingOnGround()) S.MaxDepth = FMath::Max(S.MaxDepth, KM->WaterDepth);
	S.SwimT = bInWater ? S.SwimT + Dt : 0.f;
	// the camera: from his side and a little ahead, so pictures show him side on
	{
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(L.Dir.Y, L.Dir.X)) + (S.Phase == 2 ? -65.f : 115.f);
		PC->SetControlRotation(FRotator(-14.f, Yaw, 0.f));
	}
	if (!S.bShot && bInWater && S.SwimT > 1.4f && (L.Name == TEXT("oak") || L.Name == TEXT("pond")) && S.Phase == (L.Name == TEXT("pond") ? 1 : 0))
	{
		S.bShot = true;
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / FString::Printf(TEXT("%s_%s.png"), *S.ShotName, *L.Name), false, false);
		UE_LOG(LogTemp, Log, TEXT("JEEBWALK picture %s_%s at %s"), *S.ShotName, *L.Name, *P.ToCompactString());
	}
	// the keys
	FVector2D Want = S.Phase == 0 ? L.Dir : (S.Phase == 2 ? -L.Dir : FVector2D::ZeroVector);
	if (S.Phase == 1) { S.HoldT += Dt; if (S.HoldT > L.Hold) { S.Phase = 2; S.StuckT = 0.f; } }
	if (!Want.IsZero()) AddMovementInput(FVector(Want.X, Want.Y, 0.f), 1.f);
	if (L.bJump && !S.bJumped && S.Phase == 0 && KM->IsMovingOnGround() && KM->WaterDepth > 50.f) { S.bJumped = true; Jump(); UE_LOG(LogTemp, Log, TEXT("JEEBWALK leg %s: Space pressed %.0f cm deep"), *L.Name, KM->WaterDepth); }
	if (S.bJumped && S.T > 0.f && !KM->IsMovingOnGround()) StopJumping();
	// the log
	if ((S.LogT += Dt) > 0.25f)
	{
		S.LogT = 0.f;
		const FHitResult& F = KM->CurrentFloor.HitResult;
		const float FloorDeg = KM->CurrentFloor.bBlockingHit ? FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(float(F.ImpactNormal.Z), -1.f, 1.f))) : -1.f;
		float Surf = 0.f; const bool bW = JeebWater::SurfaceAt(P.X, P.Y, Surf);
		UE_LOG(LogTemp, Log, TEXT("JEEBWALK %s p%d t=%.2f at (%.0f, %.0f, %.0f) from middle %+.0f ground %.0f water %s depth@feet %.0f mode %d.%d speed %.0f floor %.1f deg %s"),
			*L.Name, S.Phase, S.T, P.X, P.Y, P.Z, Prog - L.Cross, JeebTerrain::HeightAt(P.X, P.Y), bW ? *FString::Printf(TEXT("%.0f"), Surf) : TEXT("none"), KM->WaterDepth,
			int32(KM->MovementMode), int32(KM->CustomMovementMode), Sp, FloorDeg, F.GetComponent() ? *F.GetComponent()->GetName() : TEXT("-"));
	}
	// done, stuck or out of time
	S.StuckT = (Sp < 40.f && !Want.IsZero()) ? S.StuckT + Dt : 0.f;
	const bool bStuck = S.StuckT > 4.f, bLate = S.T > (L.Name == TEXT("oak") ? 70.f : 45.f) * (S.Phase == 0 ? 1.f : 2.f);
	if (S.Phase == 0 && (Prog >= L.Ahead || bStuck || bLate))
	{
		if (bStuck || bLate) UE_LOG(LogTemp, Log, TEXT("JEEBWALK leg %s: %s going in/across at %+.0f from the middle"), *L.Name, bStuck ? TEXT("STUCK") : TEXT("OUT OF TIME"), Prog - L.Cross);
		S.Phase = L.Hold > 0.f ? 1 : 2; S.StuckT = 0.f;
		return;
	}
	if (S.Phase == 2 && (Prog <= L.Ahead - L.Back || bStuck || bLate))
	{
		S.bOut = !KM->InWater() && KM->IsMovingOnGround() && KM->WaterDepth <= 0.f;
		Report(bStuck ? TEXT("STUCK coming back") : bLate ? TEXT("OUT OF TIME coming back") : TEXT("finished"));
		if (bStuck) UE_LOG(LogTemp, Log, TEXT("JEEBWALK leg %s: STUCK coming back at %+.0f from the middle"), *L.Name, Prog - L.Cross);
		NextLeg();
	}
#endif
}

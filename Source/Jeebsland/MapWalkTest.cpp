// Map rework walking test (2026-11-26, agent checks; test builds only, standalone). Drives the local knight along a list of waypoints and
// logs where he is every second, so falling through the ground, sticking and invisible walls show up in the log (JEEBMAPWALK).
//   -MapWalk="x_y[_flags];x,y[,flags];..."   centimetres, X north, Y east. The first point is a teleport. Flags: J jump while walking this
//        leg, C climb (hold W; on a rock face the keys follow the rock up), S sprint, T stand him there (no walking), F the climbing bar
//        stays full on this leg (a high-level climber: is the whole face climbable?). A leg ends within 1.5 m of its point or after 40 s.
//   -MapProbe="X:Y0:Y1:step[;...]"           logs the ground's height (JEEBMAPPROBE) along a line of Y at that X, once at start, and the
//        steepest stretch of that line (JEEBMAPSLOPE, degrees over one step).
#include "CoreMinimal.h"
#if !UE_BUILD_SHIPPING
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Containers/Ticker.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "KnightCharacter.h"
#include "KnightMovement.h"
#include "Terrain.h"
#include "WorldMap.h"
#include "MapRework.h"
#include "Caves.h"
#include "DeepCaves.h"

namespace
{
	struct FLeg { FVector2D P; bool bJump = false, bClimb = false, bSprint = false, bTele = false, bFullBar = false; FString Tag; float Z = -1.0e9f; };   // (Z: a teleport's own height, inside the Deeps)
	TArray<FLeg> Legs;
	int32 LegIdx = -1;
	double LegStart = 0.0, LastLog = 0.0, LastMoveT = 0.0;
	FVector LastMoveP = FVector::ZeroVector;
	bool bProbed = false, bInit = false, bDone = false;
	double WorldReady = -1.0;

	float TraceDown(UWorld* W, float X, float Y, float FromZ, float Len, AActor* Ignore)
	{
		FHitResult H;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(MapWalk), false, Ignore);
		if (W->LineTraceSingleByChannel(H, FVector(X, Y, FromZ), FVector(X, Y, FromZ - Len), ECC_Visibility, Q)) return H.ImpactPoint.Z;
		return -1.0e9f;
	}

	bool Tick(float Dt)
	{
		if (bDone) return true;
		static FString Walk, Probe;
		if (!bInit)
		{
			bInit = true;
			FParse::Value(FCommandLine::Get(), TEXT("MapWalk="), Walk);
			FParse::Value(FCommandLine::Get(), TEXT("MapProbe="), Probe);
			TArray<FString> Segs; Walk.ParseIntoArray(Segs, TEXT(";"));
			for (const FString& S : Segs)
			{
				TArray<FString> P; S.ParseIntoArray(P, TEXT("_"));
				if (P.Num() < 2) continue;
				FLeg L; L.P = FVector2D(FCString::Atof(*P[0]), FCString::Atof(*P[1]));
				if (P.Num() > 2) { L.bJump = P[2].Contains(TEXT("J")); L.bClimb = P[2].Contains(TEXT("C")); L.bSprint = P[2].Contains(TEXT("S")); L.bTele = P[2].Contains(TEXT("T")); L.bFullBar = P[2].Contains(TEXT("F")); }
				Legs.Add(L);
			}
		}
		if (Walk.IsEmpty() && Probe.IsEmpty() && !FParse::Param(FCommandLine::Get(), TEXT("MapHeather")) && !FCString::Strifind(FCommandLine::Get(), TEXT("CaveWalk")) && !FCString::Strifind(FCommandLine::Get(), TEXT("DeepWalk"))) { bDone = true; return true; }
		UWorld* W = nullptr;
		for (const FWorldContext& C : GEngine->GetWorldContexts()) if (C.World() && (C.WorldType == EWorldType::Game || C.WorldType == EWorldType::PIE) && C.World()->GetNetMode() == NM_Standalone) W = C.World();
		if (!W) return true;
		static bool bHeathed = false;
		if (!bHeathed && FParse::Param(FCommandLine::Get(), TEXT("MapHeather")))
		{
			// (the middle of the biggest heather drifts near the moor's heart: for placing a camera in the heather)
			bHeathed = true;
			TArray<FVector> Best;
			for (float X = 0.f; X <= 20000.f; X += 250.f) for (float Y = 74000.f; Y <= 94000.f; Y += 250.f)
				if (JeebRework::Heather(X, Y) > 0.98f && JeebRework::Heather(X + 1500.f, Y) > 0.9f && JeebRework::Heather(X - 1500.f, Y) > 0.9f && JeebRework::Heather(X, Y + 1500.f) > 0.9f && JeebRework::Heather(X, Y - 1500.f) > 0.9f) Best.Add(FVector(X, Y, 0.f));
			for (int32 i = 0; i < Best.Num(); i += FMath::Max(1, Best.Num() / 8)) UE_LOG(LogTemp, Log, TEXT("JEEBMAPHEATHER deep in a drift at %.0f %.0f (of %d spots)"), Best[i].X, Best[i].Y, Best.Num());
		}
		if (!bProbed && !Probe.IsEmpty())
		{
			bProbed = true;
			TArray<FString> Segs; Probe.ParseIntoArray(Segs, TEXT(";"));
			for (const FString& S : Segs)
			{
				TArray<FString> P; S.ParseIntoArray(P, TEXT(":"));
				if (P.Num() < 4) continue;
				const float X = FCString::Atof(*P[0]), Y0 = FCString::Atof(*P[1]), Y1 = FCString::Atof(*P[2]), St = FMath::Max(FCString::Atof(*P[3]), 10.f);
				FString Line;
				for (float Y = Y0; Y <= Y1; Y += St) Line += FString::Printf(TEXT(" %.0f:%.1f"), Y, JeebTerrain::HeightAt(X, Y) / 100.f);
				UE_LOG(LogTemp, Log, TEXT("JEEBMAPPROBE X %.0f Y:height(m)%s (coast Y %.0f)"), X, *Line, JeebWorld::CoastY(X));
				float Worst = 0.f, WorstY = Y0;
				for (float Y = Y0; Y + St <= Y1; Y += St) { const float Sl = FMath::Abs(JeebTerrain::HeightAt(X, Y + St) - JeebTerrain::HeightAt(X, Y)) / St; if (Sl > Worst) { Worst = Sl; WorstY = Y; } }
				UE_LOG(LogTemp, Log, TEXT("JEEBMAPSLOPE X %.0f Y %.0f..%.0f: steepest %.1f degrees over %.0f cm at Y %.0f"), X, Y0, Y1, FMath::RadiansToDegrees(FMath::Atan(Worst)), St, WorstY);
			}
		}
		// (-CaveWalk, 2026-11-26: into every cave, Caves.h, from 12 m outside its mouth (out at sea for a sea cave) along its passage to the
		// chamber's far end and back out; -CaveWalk=2 one cave. Each leg's line names the cave and the stretch: JEEBMAPWALK leg ... [cave 2 in 30])
		static bool bCaveLegs = false;
		if (!bCaveLegs && FCString::Strifind(FCommandLine::Get(), TEXT("CaveWalk")) && JeebCaves::All().Num() > 0)
		{
			bCaveLegs = true;
			int32 Only = -1;
			FParse::Value(FCommandLine::Get(), TEXT("CaveWalk="), Only);
			for (int32 c = 0; c < JeebCaves::All().Num(); ++c)
			{
				if (Only >= 0 && c != Only) continue;
				const JeebCaves::FCave& Cv = JeebCaves::All()[c];
				FLeg T; T.P = Cv.Mouth - Cv.D * (Cv.bSea ? 1500.f : 1200.f); T.bTele = true; T.Tag = FString::Printf(TEXT("cave %d outside"), c); Legs.Add(T);
				TArray<FLeg> In;
				for (int32 s = 0; s < Cv.St.Num() && Cv.St[s].A <= 5300.f; s += 3) { FLeg L; L.P = Cv.St[s].P; L.Tag = FString::Printf(TEXT("cave %d in %d"), c, FMath::RoundToInt(Cv.St[s].A / 100.f)); In.Add(L); }
				Legs.Append(In);
				for (int32 k = In.Num() - 2; k >= 0; k -= 2) { FLeg L = In[k]; L.Tag = FString::Printf(TEXT("cave %d out %s"), c, *In[k].Tag.RightChop(In[k].Tag.Find(TEXT(" in ")) + 4)); Legs.Add(L); }
				FLeg O; O.P = Cv.Mouth - Cv.D * 900.f; O.Tag = FString::Printf(TEXT("cave %d back outside"), c); Legs.Add(O);
			}
			if (Legs.Num() > 0) Legs[0].bTele = false;   // (the first point is the start's teleport anyway)
			UE_LOG(LogTemp, Log, TEXT("JEEBMAPWALK caves: %d legs"), Legs.Num());
		}
		// (-DeepWalk=A_B+C_D..., 2026-11-28: from just outside mouth A of the Deeps (DeepCaves.h) along the maze's floor to mouth B and out; each
		// route starts with a teleport. The mouths' numbers are the order of the JEEBDEEP mouth lines.)
		static bool bDeepLegs = false;
		if (!bDeepLegs && JeebDeep::Ready())
		{
			bDeepLegs = true;
			FString Dw;
			if (FParse::Value(FCommandLine::Get(), TEXT("DeepWalk="), Dw))
			{
				TArray<FString> Rs; Dw.ParseIntoArray(Rs, TEXT("+"));
				for (const FString& R : Rs)
				{
					FString A, B; if (!R.Split(TEXT("_"), &A, &B)) continue;
					TArray<FVector> Pts;
					if (!JeebDeep::Route(FCString::Atoi(*A), FCString::Atoi(*B), Pts)) { UE_LOG(LogTemp, Warning, TEXT("JEEBMAPWALK deep %s: no route"), *R); continue; }
					float Len = 0.f; for (int32 i = 1; i < Pts.Num(); ++i) Len += FVector::Dist(Pts[i], Pts[i - 1]);
					UE_LOG(LogTemp, Log, TEXT("JEEBMAPWALK deep %s: %d points, %.0f m"), *R, Pts.Num(), Len / 100.f);
					FLeg T; T.P = FVector2D(Pts[0].X, Pts[0].Y); T.Z = Pts[0].Z; T.bTele = true; T.Tag = FString::Printf(TEXT("deep %s start"), *R); Legs.Add(T);
					for (int32 i = 1; i < Pts.Num(); ++i)
					{
						if (i + 1 < Pts.Num() && FVector::Dist2D(Pts[i], Legs.Last().bTele ? Pts[0] : FVector(Legs.Last().P.X, Legs.Last().P.Y, 0.f)) < 200.f) continue;
						FLeg L; L.P = FVector2D(Pts[i].X, Pts[i].Y); L.Tag = FString::Printf(TEXT("deep %s %d/%d z %.0f"), *R, i, Pts.Num() - 1, Pts[i].Z); Legs.Add(L);
					}
				}
				UE_LOG(LogTemp, Log, TEXT("JEEBMAPWALK deep: %d legs"), Legs.Num());
			}
		}
		if (Legs.Num() == 0) { if (bProbed) bDone = true; return true; }
		APlayerController* PC = W->GetFirstPlayerController();
		AKnightCharacter* Me = PC ? Cast<AKnightCharacter>(PC->GetPawn()) : nullptr;
		if (!Me) { static double Wn = 0.0; if (FPlatformTime::Seconds() - Wn > 10.0) { Wn = FPlatformTime::Seconds(); UE_LOG(LogTemp, Log, TEXT("JEEBMAPWALK waiting: controller %s, pawn %s"), PC ? TEXT("yes") : TEXT("none"), (PC && PC->GetPawn()) ? *PC->GetPawn()->GetClass()->GetName() : TEXT("none")); } return true; }
		if (WorldReady < 0.0) WorldReady = W->GetTimeSeconds();
		if (W->GetTimeSeconds() - WorldReady < 12.0) return true;   // (the world and the grass settle first)
		UCharacterMovementComponent* M = Me->GetCharacterMovement();
		UKnightMovement* KM = Me->KnightMove();
		const double Now = W->GetTimeSeconds();
		const float HH = Me->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		if (LegIdx < 0)
		{
			LegIdx = 0;
			const FLeg& L = Legs[0];
			const float Z = L.Z > -1.0e8f ? L.Z : FMath::Max(TraceDown(W, L.P.X, L.P.Y, 30000.f, 40000.f, Me), JeebTerrain::HeightAt(L.P.X, L.P.Y));
			Me->TeleportTo(FVector(L.P.X, L.P.Y, Z + HH + 10.f), Me->GetActorRotation());
			UE_LOG(LogTemp, Log, TEXT("JEEBMAPWALK start at %.0f %.0f z %.0f (%d legs)"), L.P.X, L.P.Y, Z, Legs.Num());
			LegIdx = 1; LegStart = LastLog = LastMoveT = Now; LastMoveP = Me->GetActorLocation();
			if (Legs.Num() < 2) { bDone = true; return true; }
			return true;
		}
		const FLeg& L = Legs[LegIdx];
		const FVector P = Me->GetActorLocation();
		FVector2D To = L.P - FVector2D(P.X, P.Y);
		const float Dist = To.Size();
		if (Dist < 150.f || Now - LegStart > 40.0)
		{
			UE_LOG(LogTemp, Log, TEXT("JEEBMAPWALK leg %d %s: at %.0f %.0f z %.0f, %.0f cm from its point after %.1f s [%s]"), LegIdx, Dist < 150.f ? TEXT("DONE") : TEXT("TIMEOUT"), P.X, P.Y, P.Z, Dist, Now - LegStart, *L.Tag);
				++LegIdx;
				while (LegIdx < Legs.Num() && Legs[LegIdx].bTele)   // (a T leg: stand him there)
				{
					const FLeg& T = Legs[LegIdx];
					const float Z = T.Z > -1.0e8f ? T.Z : FMath::Max(TraceDown(W, T.P.X, T.P.Y, 30000.f, 40000.f, Me), JeebTerrain::HeightAt(T.P.X, T.P.Y));
					Me->TeleportTo(FVector(T.P.X, T.P.Y, Z + HH + 10.f), Me->GetActorRotation());
					if (KM) { KM->SwimStamina = 1.f; KM->Breath = 1.f; KM->ClimbStamina = 1.f; KM->bClimbExhausted = false; KM->bNoRegrab = false; }   // (every leg starts with full bars)
					UE_LOG(LogTemp, Log, TEXT("JEEBMAPWALK teleport to %.0f %.0f z %.0f"), T.P.X, T.P.Y, Z);
					++LegIdx;
				}
				if (LegIdx >= Legs.Num()) { bDone = true; UE_LOG(LogTemp, Log, TEXT("JEEBMAPWALK finished")); if (FCString::Strifind(FCommandLine::Get(), TEXT("DeepWalk="))) PC->ConsoleCommand(TEXT("quit")); return true; }
				LegStart = LastMoveT = Now; LastMoveP = Me->GetActorLocation(); return true;
		}
		To /= Dist;
		if (L.bFullBar && KM) { KM->ClimbStamina = 1.f; KM->bClimbExhausted = false; }
		const FRotator Face(0.f, FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X)), 0.f);
		if (PC) PC->SetControlRotation(Face);
		if (KM && KM->OnWall() && L.bClimb) Me->AddMovementInput(UKnightMovement::WallUp(KM->WallNormal), 1.f);
		else Me->AddMovementInput(FVector(To.X, To.Y, 0.f), 1.f);
		if (L.bJump && FMath::Fmod(Now - LegStart, 2.5) < 0.1) Me->Jump(); else if (L.bJump) Me->StopJumping();
		if (Now - LastLog >= 1.0)
		{
			LastLog = Now;
			const float TerrH = JeebTerrain::HeightAt(P.X, P.Y), Tr = TraceDown(W, P.X, P.Y, P.Z + 200.f, 6000.f, Me);
			const float Feet = P.Z - HH;
			UE_LOG(LogTemp, Log, TEXT("JEEBMAPWALK leg %d t %.0f pos %.0f %.0f z %.0f | terrain %.0f trace %.0f feet-over-trace %.0f | mode %d/%d speed %.0f dist %.0f%s%s"), LegIdx, Now - LegStart, P.X, P.Y, P.Z, TerrH, Tr, Tr > -1.0e8f ? Feet - Tr : 0.f,
				int32(M->MovementMode), int32(M->CustomMovementMode), M->Velocity.Size(), Dist,
				Feet < FMath::Min(TerrH, Tr > -1.0e8f ? Tr : TerrH) - 120.f && !(KM && KM->InWater()) ? TEXT(" UNDER-GROUND") : TEXT(""),
				FVector::Dist2D(P, LastMoveP) < 30.f && Now - LastMoveT > 3.0 ? TEXT(" STUCK") : TEXT(""));
			if (FVector::Dist2D(P, LastMoveP) >= 30.f) { LastMoveP = P; LastMoveT = Now; }
		}
		return true;
	}
	struct FReg { FReg() { FCoreDelegates::OnPostEngineInit.AddLambda([]() { FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick), 0.f); }); } };
	static FReg GReg;
}
#endif

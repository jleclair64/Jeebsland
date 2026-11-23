// The online test of the PvP zones, fall damage and the mounted-target arrow bonus (test builds only, never Shipping):
//   bash Tools/pvp_net.sh   (a world server and two players: -JeebPvpTest)
// The server places the two players, tries every case and logs JEEBPVPTEST PASS or FAIL for each; the players' machines log what reaches
// them (JEEBPVPCLIENT: zone, title, health) and take pictures of a warning sign and of the title and icon (Saved/Shots/pvp_*.png).
#include "PvpSigns.h"
#include "PvpZones.h"
#include "KnightCharacter.h"
#include "KnightHUD.h"
#include "Projectile.h"
#include "SkillComponent.h"
#include "Terrain.h"
#include "WorldMap.h"
#include "JeebWater.h"
#include "OpenWorld.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Components/CapsuleComponent.h"

#if !UE_BUILD_SHIPPING
static float XO() { return JeebPvp::Zones()[0].Min.X - 900.f; }    // (the test lines: outside the zone, 9 m before its edge,
static float XI() { return JeebPvp::Zones()[0].Min.X + 2500.f; }   // and inside it, 25 m past)
struct FPvpTestDriver
{
	static void Log(const FString& S) { UE_LOG(LogTemp, Log, TEXT("JEEBPVPTEST %s"), *S); }

	static void Place(AKnightCharacter* K, const FVector2D& P, const FVector2D& LookAt, float Up = 0.f)
	{
		if (!K) return;
		const FRotator R(0.f, FMath::RadiansToDegrees(FMath::Atan2(LookAt.Y - P.Y, LookAt.X - P.X)), 0.f);
		K->TeleportTo(FVector(P.X, P.Y, JeebTerrain::DrawnHeight(P.X, P.Y) + 100.f + Up), R, false, true);
		if (APlayerController* PC = Cast<APlayerController>(K->GetController())) PC->ClientSetRotation(FRotator(-4.f, R.Yaw, 0.f));
	}

	// flat, dry ground for two players side by side at both X lines, on a warning sign's line (the signs stand every 60 m from Y -360 m)
	static float FindY()
	{
		for (float Tol : { 60.f, 120.f, 250.f }) for (int32 k = 0; k < 25; ++k)
		{
			const float Y = JeebPvp::Zones()[0].Min.Y + JeebPvp::Zones()[0].SignEvery * (k + 4);   // (a warning sign stands on every one of these lines)
			bool bOk = true;
			for (float X : { XO(), XI() }) for (float Dy = -200.f; Dy <= 900.f; Dy += 300.f) for (float Dx = -300.f; Dx <= 300.f; Dx += 300.f)
			{
				const float G = JeebTerrain::DrawnHeight(X + Dx, Y + Dy);
				const float Gc = JeebTerrain::DrawnHeight(X, Y);
				float S;
				if (FMath::Abs(G - Gc) > Tol || (JeebWater::SurfaceAt(X + Dx, Y + Dy, S) && S > G - 40.f)) bOk = false;
			}
			if (bOk) return Y;
		}
		return 6000.f;
	}

	// deep water close to Oak Hill's plaza: the spot and its depth
	static bool FindWater(FVector2D& Out, float& Depth)
	{
		const FVector2D Plaza(-3550.f, 18290.f);
		for (float R = 1000.f; R < 30000.f; R += 500.f)
			for (int32 a = 0; a < 24; ++a)
			{
				const FVector2D P = Plaza + FVector2D(FMath::Cos(a * 0.2618f), FMath::Sin(a * 0.2618f)) * R;
				float S;
				if (!JeebWater::SurfaceAt(P.X, P.Y, S)) continue;
				const float D = S - JeebTerrain::HeightAt(P.X, P.Y);
				if (D > 160.f) { Out = P; Depth = D; return true; }
			}
		return false;
	}

	static void Fire(AKnightCharacter* From, AKnightCharacter* At, int32 Dmg)
	{
		const FVector A = From->GetActorLocation() + FVector(0.f, 0.f, 40.f), B = At->GetActorLocation() + FVector(0.f, 0.f, 20.f);
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector Dir = (B - A).GetSafeNormal();
		if (AProjectile* P = From->GetWorld()->SpawnActor<AProjectile>(AProjectile::StaticClass(), A + Dir * 60.f, Dir.Rotation(), SP))
			P->Launch(From, Dir, 3500.f, 0.f, Dmg, false, ESkillType::Archery, false, 0, 0, 0.f, 0.f);
	}

	static void Server(APvpSigns* C, float Dt)
	{
		static float Clock = 0.f, Next = 0.f, StageT = 0.f, Seen = -1.f;
		static int32 Stage = 0, Passes = 0, Fails = 0;
		static TWeakObjectPtr<AKnightCharacter> PA, PB;
		static float Y0 = 0.f, HpB = 0.f, D1 = 0.f;
		static FVector2D Wet; static float WetDepth = 0.f;
		Clock += Dt; StageT += Dt;
		if (Clock < Next) return;
		UWorld* W = C->GetWorld();
		auto Go = [&](float Wait) { ++Stage; StageT = 0.f; Next = Clock + Wait; };
		auto Check = [&](bool bOk, const FString& What) { Log(FString::Printf(TEXT("%s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What)); (bOk ? Passes : Fails)++; };
		AKnightCharacter* A = PA.Get(); AKnightCharacter* B = PB.Get();
		switch (Stage)
		{
		case 0:
		{
			TArray<AKnightCharacter*> Ks;
			for (TActorIterator<AKnightCharacter> It(W); It; ++It) if (It->GetController()) Ks.Add(*It);
			if (Ks.Num() < 2 && Clock < 600.f) { Next = Clock + 1.f; return; }
			if (Ks.Num() < 2) { Log(TEXT("FAIL: fewer than two players joined")); Stage = 900; return; }
			if (Seen < 0.f) Seen = Clock;
			if (Clock - Seen < 20.f) { Next = Clock + 1.f; return; }   // (20 s for both players' machines to finish loading)
			PA = Ks[0]; PB = Ks[1];
			A = PA.Get(); B = PB.Get();
			Y0 = FindY();
			Log(FString::Printf(TEXT("start: players %s and %s; test line at Y %.0f; zones %d"), *A->GetName(), *B->GetName(), Y0, JeebPvp::Zones().Num()));
			for (float X = 44000.f; X <= 104000.f; X += 4000.f)   // (where snow lies: the share of dry land along each X line with snow on the ground)
			{
				int32 Land = 0, Snowy = 0; float Hi = -1.0e9f;
				for (float Y = JeebWorld::MinY + 2000.f; Y < JeebWorld::MaxY - 2000.f; Y += 2000.f)
				{
					float S; const float G = JeebTerrain::HeightAt(X, Y);
					if (JeebWater::SurfaceAt(X, Y, S) && S > G - 40.f) continue;
					++Land; Hi = FMath::Max(Hi, G); if (JeebOpen::SnowOnGround(X, Y) > 0.3f) ++Snowy;
				}
				Log(FString::Printf(TEXT("snow scan X %.0f m: %d of %d land samples snowy, highest ground %.0f m"), X / 100.f, Snowy, Land, Hi / 100.f));
			}
			Check(IsInPvpZone(FVector(XI(), Y0, 0.f)) && !IsInPvpZone(FVector(XO(), Y0, 0.f)) && !IsInPvpZone(FVector(-3550.f, 18290.f, 0.f)), TEXT("IsInPvpZone: in 25 m past the edge, out 9 m before it and at Oak Hill"));
			// outside, 9 m from a warning sign: the pictures of the sign are taken here
			Place(A, FVector2D(XO(), Y0), FVector2D(XI(), Y0)); Place(B, FVector2D(XO(), Y0 + 500.f), FVector2D(XI(), Y0 + 500.f));
			Go(9.f); return;
		}
		case 1:   // both outside: a blow, an arrow do nothing
			HpB = B->Health;
			Check(!IsInPvpZone(A->GetActorLocation()) && !IsInPvpZone(B->GetActorLocation()), FString::Printf(TEXT("both players stand outside (X %.0f, %.0f)"), A->GetActorLocation().X, B->GetActorLocation().X));
			B->Hurt(10, A);
			Check(B->Health == HpB, FString::Printf(TEXT("outside: a blow from the other player does nothing (health %.0f of %.0f)"), B->Health, HpB));
			Place(A, FVector2D(XO(), Y0), FVector2D(XO(), Y0 + 500.f)); Place(B, FVector2D(XO(), Y0 + 600.f), FVector2D(XO(), Y0));
			Go(7.f); return;
		case 2:
			Fire(A, B, 10);
			Go(2.f); return;
		case 3:
			Check(B->Health == HpB, FString::Printf(TEXT("outside: a real arrow does nothing (health %.0f of %.0f)"), B->Health, HpB));
			Place(A, FVector2D(XI(), Y0), FVector2D(XI(), Y0 + 500.f));   // (one inside, one outside: still nothing)
			Go(7.f); return;
		case 4:
			B->Hurt(10, A);
			Check(B->Health == HpB && IsInPvpZone(A->GetActorLocation()), FString::Printf(TEXT("shooter inside, target outside: nothing (health %.0f of %.0f)"), B->Health, HpB));
			Place(A, FVector2D(XO(), Y0), FVector2D(XI(), Y0 + 500.f)); Place(B, FVector2D(XI(), Y0 + 500.f), FVector2D(XO(), Y0));
			Go(7.f); return;
		case 5:
			B->Hurt(10, A);
			Check(B->Health == HpB && IsInPvpZone(B->GetActorLocation()), FString::Printf(TEXT("shooter outside, target inside: nothing (health %.0f of %.0f)"), B->Health, HpB));
			Place(A, FVector2D(XI(), Y0), FVector2D(XI(), Y0 + 600.f)); Place(B, FVector2D(XI(), Y0 + 600.f), FVector2D(XI(), Y0));   // (both inside)
			Go(10.f); return;   // (the players' pictures of the title and the icon)
		case 6:
			Check(IsInPvpZone(A->GetActorLocation()) && IsInPvpZone(B->GetActorLocation()), TEXT("both players stand inside"));
			B->Hurt(10, A);
			Check(B->Health < HpB, FString::Printf(TEXT("inside: a blow from the other player lands (health %.0f of %.0f)"), B->Health, HpB));
			B->Health = HpB; B->InvulnUntil = 0.f;
			Go(0.5f); return;
		case 7:
			Fire(A, B, 10);
			Go(2.f); return;
		case 8:
			D1 = HpB - B->Health;
			Check(D1 > 0.f, FString::Printf(TEXT("inside: a real arrow lands, %.0f damage"), D1));
			B->Health = HpB;
			B->bRushing = true;   // (a rushing troll: IsFastMounted reads it)
			Check(B->IsFastMounted(), TEXT("a rushing target is IsFastMounted"));
			Go(0.3f); return;
		case 9:
			Fire(A, B, 10);
			Go(2.f); return;
		case 10:
		{
			const float D2 = HpB - B->Health;
			Check(D1 > 0.f && FMath::Abs(D2 - 2.f * D1) <= 1.5f, FString::Printf(TEXT("a rushing target takes double from an arrow: %.0f against %.0f"), D2, D1));
			B->Health = HpB;
			B->Hurt(10, A);   // (a blow, not an arrow: not doubled)
			const float D3 = HpB - B->Health;
			Check(FMath::Abs(D3 - D1) <= 1.5f, FString::Printf(TEXT("a rushing target takes a plain blow at its normal damage: %.0f against %.0f"), D3, D1));
			B->bRushing = false; B->Health = HpB; B->InvulnUntil = 0.f;
			Place(A, FVector2D(XO(), Y0), FVector2D(XI(), Y0));   // (fall damage: the flat spot outside the zone)
			Go(8.f); return;
		}
		case 11:
			A->Health = float(A->MaxHealth);
			Place(A, FVector2D(XO(), Y0), FVector2D(XI(), Y0), 300.f);   // 3 m
			Go(9.f); return;
		case 12:
			Check(A->Health >= float(A->MaxHealth), FString::Printf(TEXT("a 3 m drop costs nothing (health %.0f)"), A->Health));
			A->Health = float(A->MaxHealth);
			Place(A, FVector2D(XO(), Y0), FVector2D(XI(), Y0), 800.f);   // 8 m: (800 - 450) / 100 * 8 = 28
			Go(10.f); return;
		case 13:
		{
			const float Lost = float(A->MaxHealth) - A->Health;
			Check(Lost >= 24.f && Lost <= 32.f, FString::Printf(TEXT("an 8 m drop costs about 28: %.0f lost"), Lost));
			A->Health = float(A->MaxHealth); A->InvulnUntil = 0.f;
			if (!FindWater(Wet, WetDepth)) { Log(TEXT("FAIL: no deep water found near Oak Hill")); Stage = 900; return; }
			Log(FString::Printf(TEXT("deep water at %.0f %.0f, %.0f cm deep"), Wet.X, Wet.Y, WetDepth));
			float S = 0.f; JeebWater::SurfaceAt(Wet.X, Wet.Y, S);
			A->TeleportTo(FVector(Wet.X, Wet.Y, S + 900.f), FRotator::ZeroRotator, false, true);   // (9 m over the water's surface)
			Go(10.f); return;
		}
		case 14:
			Check(A->Health >= float(A->MaxHealth), FString::Printf(TEXT("landing in deep water from 9 m costs nothing (health %.0f)"), A->Health));
			Go(0.5f); return;
		default:
			Log(FString::Printf(TEXT("DONE: %d passed, %d failed"), Passes, Fails));
			Next = 1.0e9f;
			return;
		}
	}

	static void Client(APvpSigns* C, float Dt)
	{
		static float Clock = 0.f, QuitAt = -1.f, SignAt = -1.f, TitleAt = -1.f;
		static bool bSign = false, bTitle = false;
		static float LastHp = -1.f; static int32 LastZone = -2;
		Clock += Dt;
		APlayerController* PC = C->GetWorld()->GetFirstPlayerController();   // (test pictures and logs only: this machine's own view)
		AKnightCharacter* K = PC ? Cast<AKnightCharacter>(PC->GetPawn()) : nullptr;
		if (QuitAt < 0.f) { float S = 240.f; FParse::Value(FCommandLine::Get(), TEXT("PvpTestClientTime="), S); QuitAt = S; }
		if (Clock > QuitAt) { if (PC) PC->ConsoleCommand(TEXT("quit")); QuitAt = 1.0e9f; return; }
		if (!K) return;
		const FVector P = K->GetActorLocation();
		const int32 Zone = JeebPvp::ZoneAt(float(P.X), float(P.Y));
		AKnightHUD* H = Cast<AKnightHUD>(PC->GetHUD());
		if (Zone != LastZone)
		{
			LastZone = Zone;
			UE_LOG(LogTemp, Log, TEXT("JEEBPVPCLIENT %s now %s at %.0f %.0f"), *K->GetName(), Zone == INDEX_NONE ? TEXT("outside every zone") : TEXT("inside a PvP zone"), P.X, P.Y);
			if (Zone != INDEX_NONE && !bTitle) { bTitle = true; TitleAt = Clock + 2.0f; }
		}
		static float NextPos = 0.f; if (Clock >= NextPos) { NextPos = Clock + 4.f; UE_LOG(LogTemp, Log, TEXT("JEEBPVPCLIENT at %.0f %.0f %.0f"), P.X, P.Y, P.Z); }
		if (LastHp != K->GetHealth()) { UE_LOG(LogTemp, Log, TEXT("JEEBPVPCLIENT %s health %.0f"), *K->GetName(), K->GetHealth()); LastHp = K->GetHealth(); }
		if (Zone == INDEX_NONE && !bSign && P.X > JeebPvp::Zones()[0].Min.X - 1500.f) { bSign = true; SignAt = Clock + 3.0f; }
		auto Shot = [&](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / FString::Printf(TEXT("pvp_%s_%s.png"), Name, *K->GetName()), false, false); UE_LOG(LogTemp, Log, TEXT("JEEBPVPCLIENT picture pvp_%s"), Name); };
		if (SignAt > 0.f && Clock >= SignAt) { SignAt = -1.f; Shot(TEXT("sign")); }
		if (TitleAt > 0.f && Clock >= TitleAt)
		{
			TitleAt = -1.f;
			UE_LOG(LogTemp, Log, TEXT("JEEBPVPCLIENT banner age %.1f s, title '%s'"), H ? C->GetWorld()->GetTimeSeconds() - H->PvpBannerT : -1.f, H ? *H->PvpTitle : TEXT(""));
			Shot(TEXT("title"));
		}
	}
};

void APvpSigns::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetNetMode() == NM_DedicatedServer) FPvpTestDriver::Server(this, DeltaSeconds);
	else if (GetNetMode() == NM_Client) FPvpTestDriver::Client(this, DeltaSeconds);
}
#endif

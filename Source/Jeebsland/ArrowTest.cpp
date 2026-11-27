// The online test of the distinct arrows (2026-11-26; test builds only, never Shipping):   bash Tools/arrows_net.sh
// A world server and two players: the server stands them in the PvP zone, shoots every arrow from one at the other (a fixed 20 damage, so the jobs
// compare), shoots a deer with a wooden arrow and a broadhead, fires one through the real bow path (UnlimitedArrows), and tries the smithy's checks
// (level, materials, a made bundle). It logs JEEBARROWTEST PASS or FAIL for each; each player's machine logs the arrows it sees (JEEBARROWCLIENT).
#include "KnightCharacter.h"
#include "Projectile.h"
#include "SkillComponent.h"
#include "HealthComponent.h"
#include "Inventory.h"
#include "JeebProps.h"
#include "PvpZones.h"
#include "Terrain.h"
#include "JeebWater.h"
#include "Deer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ConfigCacheIni.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Engine.h"
#include "Containers/Ticker.h"
#include "Misc/CoreDelegates.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

#if !UE_BUILD_SHIPPING
struct FArrowTestDriver
{
	static void Log(const FString& S) { UE_LOG(LogTemp, Log, TEXT("JEEBARROWTEST %s"), *S); }

	static void Place(AKnightCharacter* K, const FVector2D& P, const FVector2D& LookAt)
	{
		const FRotator R(0.f, FMath::RadiansToDegrees(FMath::Atan2(LookAt.Y - P.Y, LookAt.X - P.X)), 0.f);
		K->TeleportTo(FVector(P.X, P.Y, JeebTerrain::DrawnHeight(P.X, P.Y) + 100.f), R, false, true);
	}
	static float FindY(float X)
	{
		const FBox2D Z = FBox2D(FVector2D(JeebPvp::Zones()[0].Min), FVector2D(JeebPvp::Zones()[0].Max));
		for (float Tol : { 50.f, 100.f, 250.f })
			for (float Y = Z.Min.Y + 3000.f; Y < Z.Max.Y - 3000.f; Y += 1500.f)
			{
				bool bOk = true;
				const float G0 = JeebTerrain::DrawnHeight(X, Y);
				for (float Dy = -300.f; Dy <= 1500.f; Dy += 300.f)
				{
					float S; const float G = JeebTerrain::DrawnHeight(X, Y + Dy);
					if (FMath::Abs(G - G0) > Tol || (JeebWater::SurfaceAt(X, Y + Dy, S) && S > G - 40.f)) bOk = false;
				}
				if (bOk) return Y;
			}
		return Z.Min.Y + 5000.f;
	}
	static void Shoot(AKnightCharacter* From, const FVector& A, const FVector& B, int32 ArrowId, int32 Dmg)
	{
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector Dir = (B - A).GetSafeNormal();
		Log(FString::Printf(TEXT("shoot from %s to %s"), *A.ToCompactString(), *B.ToCompactString()));
		if (AProjectile* P = From->GetWorld()->SpawnActor<AProjectile>(AProjectile::StaticClass(), A, Dir.Rotation(), SP))
			P->Launch(From, Dir, 3500.f, 0.f, Dmg, false, ESkillType::Archery, false, 0, ArrowId, 0.f, 0.f);
	}

	static void Server(UWorld* W, float Dt)
	{
		using namespace JeebItems;
		static float Clock = 0.f, Next = 0.f, Seen = -1.f, X0 = 0.f, Y0 = 0.f;
		static int32 Stage = 0, Passes = 0, Fails = 0;
		static TWeakObjectPtr<AKnightCharacter> PA, PB;
		static TWeakObjectPtr<ADeer> Deer;
		static float Hp0 = 0.f, Took[8] = {}, Moved[8] = {}, DeerTook[2] = {};
		static FVector BAt;
		static const int32 Order[6] = { A_Wooden, A_Broadhead, A_Bodkin, A_Heavy, A_Fire, A_Eagle };
		Clock += Dt;
		if (Clock < Next) return;
		auto Go = [&](float Wait) { ++Stage; Next = Clock + Wait; };
		auto Check = [&](bool bOk, const FString& What) { Log(FString::Printf(TEXT("%s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What)); (bOk ? Passes : Fails)++; };
		AKnightCharacter* A = PA.Get(); AKnightCharacter* B = PB.Get();
		if (Stage > 0 && Stage < 900 && (!A || !B)) { Log(TEXT("FAIL: a player left")); Stage = 900; return; }
		const FVector2D PAt(X0, Y0), PBt(X0, Y0 + 1200.f);
		auto Reset = [&]() { B->Health = float(B->MaxHealth); B->InvulnUntil = 0.f; B->StunLeft = 0.f; Place(B, PBt, PAt); };
		auto Chest = [](AKnightCharacter* K) { return K->GetActorLocation() + FVector(0.f, 0.f, 25.f); };
		if (Stage == 0)
		{
			TArray<AKnightCharacter*> Ks;
			for (TActorIterator<AKnightCharacter> It(W); It; ++It) if (It->GetController()) Ks.Add(*It);
			if (Ks.Num() < 2) { if (Clock > 600.f) { Log(TEXT("FAIL: fewer than two players joined")); Stage = 900; } Next = Clock + 1.f; return; }
			if (Seen < 0.f) Seen = Clock;
			if (Clock - Seen < 20.f) { Next = Clock + 1.f; return; }
			PA = Ks[0]; PB = Ks[1]; A = Ks[0]; B = Ks[1];
			X0 = JeebPvp::Zones()[0].Min.X + 2500.f; Y0 = FindY(X0);
			Place(A, FVector2D(X0, Y0), FVector2D(X0, Y0 + 1200.f)); Place(B, FVector2D(X0, Y0 + 1200.f), FVector2D(X0, Y0)); B->Health = float(B->MaxHealth);
			Log(FString::Printf(TEXT("placed: A at %s, B at %s"), *A->GetActorLocation().ToCompactString(), *B->GetActorLocation().ToCompactString()));
			Log(FString::Printf(TEXT("start: shooter %s, target %s at %.0f %.0f; target armour takes %.2f of a body hit"), *A->GetName(), *B->GetName(), X0, Y0, ZoneTake(B->GetSkills(), 0)));
			for (int32 k = 0; k < 6; ++k) Log(FString::Printf(TEXT("row %d %s: model %s, steps %.1f, animal x%.1f, pierce %.2f, knock %.0f, burn %.0f, speed x%.2f, Smithing %d"), Order[k], *ItemName(ArrowId(Order[k])),
				*GetNameSafe(JeebProps::ArrowMesh(ArrowId(Order[k]))), ArrowRow(Order[k]).Steps, ArrowRow(Order[k]).AnimalMul, ArrowRow(Order[k]).Pierce, ArrowRow(Order[k]).Knock, ArrowRow(Order[k]).Burn, ArrowRow(Order[k]).SpeedMul, ArrowRow(Order[k]).Level));
			Check(ReplaceRetired(ArrowId(A_RetiredEmerald)) == ArrowId(A_Eagle) && ItemName(ArrowId(A_Wooden)) == TEXT("Wooden Arrows") && ItemName(IdFireArrow) == TEXT("Fire Arrows"),
				TEXT("old ids keep their arrows: 900 wooden ... 906 fire, a save's retired emerald arrows become eagle arrows"));
			Go(6.f); return;
		}
		if (Stage >= 1 && Stage <= 12)   // each arrow at the other player: odd stages shoot, even ones read
		{
			const int32 k = (Stage - 1) / 2;
			if (Stage % 2 == 1) { Reset(); BAt = B->GetActorLocation(); Hp0 = B->Health; Shoot(A, Chest(A) + FVector(0.f, 60.f, 0.f), Chest(B), ArrowId(Order[k]), 20); Go(Order[k] == A_Fire ? 3.5f : 1.5f); return; }
			Took[k] = Hp0 - B->Health; Moved[k] = FVector::Dist2D(B->GetActorLocation(), BAt);
			Log(FString::Printf(TEXT("%s at the player: %.0f damage, moved %.0f cm"), *ItemName(ArrowId(Order[k])), Took[k], Moved[k]));
			if (Stage == 12)
			{
				Check(Took[0] > 0.f, FString::Printf(TEXT("a wooden arrow lands: %.0f"), Took[0]));
				Check(Took[2] > Took[0], FString::Printf(TEXT("the bodkin goes through armour: %.0f against the wooden arrow's %.0f (same 20 fired)"), Took[2], Took[0]));
				Check(Moved[3] > 40.f && Moved[3] > Moved[0] + 30.f, FString::Printf(TEXT("the heavy arrow knocks him back: %.0f cm against %.0f"), Moved[3], Moved[0]));
				Check(Took[4] > Took[0] + 3.f, FString::Printf(TEXT("the fire arrow burns on: %.0f in all against %.0f"), Took[4], Took[0]));
				Check(FMath::Abs(Took[1] - Took[0]) <= 1.f, FString::Printf(TEXT("the broadhead's animal bonus does not apply to a player: %.0f against %.0f"), Took[1], Took[0]));
			}
			Go(1.f); return;
		}
		switch (Stage)
		{
		case 13:   // a deer: wooden against broadhead
		{
			ADeer* Best = nullptr; float BestD = 1.0e12f;
			for (TActorIterator<ADeer> It(W); It; ++It) { UHealthComponent* H = It->FindComponentByClass<UHealthComponent>(); if (H && H->IsAlive() && H->Max >= 25) { const float D = FVector::DistSquared(It->GetActorLocation(), A->GetActorLocation()); if (D < BestD) { BestD = D; Best = *It; } } }
			Deer = Best;
			if (!Best) { Log(TEXT("FAIL: no deer found")); ++Fails; Stage = 18; return; }
			Log(FString::Printf(TEXT("deer %s at %s"), *Best->GetName(), *Best->GetActorLocation().ToCompactString()));
			Go(0.1f); return;
		}
		case 14: case 16:   // (14 a wooden arrow, 16 a broadhead; 15 and 17 read them)
		case 15: case 17:
		{
			ADeer* D = Deer.Get(); UHealthComponent* H = D ? D->FindComponentByClass<UHealthComponent>() : nullptr;
			if (!H) { Log(TEXT("FAIL: the deer is gone")); ++Fails; Stage = 18; return; }
			if (Stage == 14 || Stage == 16)
			{
				H->Reset(); Hp0 = float(H->Cur);
				const FVector T = D->GetActorLocation() + FVector(0.f, 0.f, 95.f);
				Shoot(A, T + FVector(400.f, 0.f, 250.f), T, ArrowId(Stage == 14 ? A_Wooden : A_Broadhead), 10);
				Go(1.2f); return;
			}
			const int32 i = Stage == 15 ? 0 : 1;
			DeerTook[i] = Hp0 - float(H->Cur);
			Log(FString::Printf(TEXT("deer took %.0f from the %s"), DeerTook[i], i == 0 ? TEXT("wooden arrow") : TEXT("broadhead")));
			if (i == 1) Check(DeerTook[0] > 0.f && DeerTook[1] >= DeerTook[0] * 1.3f, FString::Printf(TEXT("the broadhead hurts animals more: %.0f against %.0f (same 10 fired)"), DeerTook[1], DeerTook[0]));
			Go(0.3f); return;
		}
		case 18:   // the real bow path: one bodkin out of the pack
		{
			bool bUnlimited = false; GConfig->GetBool(TEXT("Jeebsland.Testing"), TEXT("UnlimitedArrows"), bUnlimited, GGameIni);
			USkillComponent* S = A->GetSkills();
			S->AddInvItem(ArrowId(A_Bodkin), 10);
			const int32 Before = S->CountItem(ArrowId(A_Bodkin));
			A->FireArrow((Chest(B) - Chest(A)).GetSafeNormal(), 1.f, ArrowId(A_Bodkin), Chest(A) + FVector(0.f, 60.f, 0.f));
			const int32 After = S->CountItem(ArrowId(A_Bodkin));
			Check(bUnlimited ? After == Before : After == Before - 1, FString::Printf(TEXT("the bow fires a bodkin from the pack: %d -> %d (UnlimitedArrows %s)"), Before, After, bUnlimited ? TEXT("on") : TEXT("off")));
			Go(1.f); return;
		}
		case 19:   // the smithy's checks
		{
			USkillComponent* S = A->GetSkills();
			{ TArray<FInvSlot> Out; TArray<uint8> Where; int32 Ing = 0; S->TakeEverything(Out, Where, Ing); S->AddIngots(Ing); }   // (an empty pack, so the materials fit)
			auto Strip = [&](EItemType M) { const int32 N = S->GetItemCount(M); if (N > 0) S->RemoveItems(MaterialId(M), N); };
			for (EItemType M : { EItemType::IronOre, EItemType::PineLogs, EItemType::Logs, EItemType::EagleFeather, EItemType::SilverOre, EItemType::RedwoodLogs }) Strip(M);
			const int32 B0 = S->CountItem(ArrowId(A_Bodkin));
			S->SetAllLevels(1);
			S->AddInvItem(MaterialId(EItemType::IronOre), 2); S->AddInvItem(MaterialId(EItemType::PineLogs), 1);
			Check(!A->SmithArrowsNow(A_Bodkin) && S->CountItem(ArrowId(A_Bodkin)) == B0 && S->GetItemCount(EItemType::IronOre) == 2, TEXT("Smithing 1 can't make bodkins (needs 20); nothing used"));
			S->SetAllLevels(20);
			Strip(EItemType::PineLogs);
			Check(!A->SmithArrowsNow(A_Bodkin) && S->GetItemCount(EItemType::IronOre) == 2, TEXT("Smithing 20 without the pine: refused, nothing used"));
			{ const bool bAdded = S->AddInvItem(MaterialId(EItemType::PineLogs), 1); Log(FString::Printf(TEXT("pine added %d, now %d"), bAdded ? 1 : 0, S->GetItemCount(EItemType::PineLogs))); }
			Check(A->SmithArrowsNow(A_Bodkin) && S->CountItem(ArrowId(A_Bodkin)) == B0 + ArrowBundle && S->GetItemCount(EItemType::IronOre) == 0 && S->GetItemCount(EItemType::PineLogs) == 0,
				FString::Printf(TEXT("Smithing 20 with 2 iron ore and a pine log: 10 bodkins made, the materials used (%d)"), S->CountItem(ArrowId(A_Bodkin))));
			S->AddInvItem(MaterialId(EItemType::EagleFeather), 2); S->AddInvItem(MaterialId(EItemType::SilverOre), 1); S->AddInvItem(MaterialId(EItemType::RedwoodLogs), 1);
			Check(!A->SmithArrowsNow(A_Eagle), TEXT("Smithing 20 can't make eagle arrows (needs 45)"));
			S->SetAllLevels(45);
			Check(A->SmithArrowsNow(A_Eagle) && S->GetItemCount(EItemType::EagleFeather) == 0, TEXT("Smithing 45 with 2 Eagle Feathers, silver ore and redwood: eagle arrows made"));
			Check(!A->SmithArrowsNow(A_RetiredEmerald), TEXT("the retired row is never made"));
			S->AddInvItem(MaterialId(EItemType::Logs), 2); S->SetAllLevels(1);
			Check(A->SmithArrowsNow(A_Wooden), TEXT("Smithing 1 makes wooden arrows from 2 logs"));
			Go(0.5f); return;
		}
		default:
			if (Stage < 900) Log(FString::Printf(TEXT("DONE: %d passed, %d failed"), Passes, Fails));
			Next = 1.0e9f;
			return;
		}
	}

	static void Client(UWorld* W, float Dt)
	{
		static float Clock = 0.f, QuitAt = -1.f;
		static TSet<TWeakObjectPtr<AProjectile>> Seen;
		Clock += Dt;
		APlayerController* PC = W->GetFirstPlayerController();   // (test logs only)
		if (QuitAt < 0.f) { float S = 200.f; FParse::Value(FCommandLine::Get(), TEXT("ArrowTestClientTime="), S); QuitAt = S; }
		if (Clock > QuitAt) { if (PC) PC->ConsoleCommand(TEXT("quit")); QuitAt = 1.0e9f; return; }
		for (TActorIterator<AProjectile> It(W); It; ++It)
		{
			if (Seen.Contains(*It) || It->GetArrowId() == 0) continue;
			Seen.Add(*It);
			UE_LOG(LogTemp, Log, TEXT("JEEBARROWCLIENT sees %s (model %s)"), *JeebItems::ItemName(It->GetArrowId()), *GetNameSafe(JeebProps::ArrowMesh(It->GetArrowId())));
		}
		AKnightCharacter* K = PC ? Cast<AKnightCharacter>(PC->GetPawn()) : nullptr;
		static float LastHp = -1.f;
		if (K && LastHp != K->GetHealth()) { LastHp = K->GetHealth(); UE_LOG(LogTemp, Log, TEXT("JEEBARROWCLIENT %s health %.0f"), *K->GetName(), LastHp); }
	}
};

// (test pictures, -ArrowLineup=X_Y: every arrow in a vertical stack 1.2 m up at X,Y pointing +Y, a post 3 m on in +X with one of each stuck in its near face,
// and an arrow of each kind in turn flying across +Y 8 m on in +X every 0.6 s, for the smear film; players' machines and single player only)
static void ArrowLineup(UWorld* W, float Dt)
{
	static FString V; static bool bOn = FParse::Value(FCommandLine::Get(), TEXT("ArrowLineup="), V);
	if (!bOn) return;
	static bool bBuilt = false; static float Clock = 0.f, NextFly = 6.f; static int32 FlyK = 0; static FVector O;
	static const int32 Order[6] = { JeebItems::A_Wooden, JeebItems::A_Broadhead, JeebItems::A_Bodkin, JeebItems::A_Heavy, JeebItems::A_Fire, JeebItems::A_Eagle };
	Clock += Dt;
	if (!bBuilt && Clock > 3.f)
	{
		bBuilt = true;
		TArray<FString> P; V.ParseIntoArray(P, TEXT("_"));
		const float X = P.Num() > 0 ? FCString::Atof(*P[0]) : 0.f, Y = P.Num() > 1 ? FCString::Atof(*P[1]) : 0.f;
		O = FVector(X, Y, JeebTerrain::DrawnHeight(X, Y));
		auto Piece = [&](UStaticMesh* M, const FVector& At, const FRotator& R, const FVector& S) -> UStaticMeshComponent*
		{
			AStaticMeshActor* A = W->SpawnActor<AStaticMeshActor>(At, R);
			if (!A) return nullptr;
			A->SetMobility(EComponentMobility::Movable);
			A->GetStaticMeshComponent()->SetStaticMesh(M);
			A->SetActorScale3D(S);
			return A->GetStaticMeshComponent();
		};
		for (int32 i = 0; i < 6; ++i)
			if (UStaticMeshComponent* C = Piece(JeebProps::ArrowMesh(JeebItems::ArrowId(Order[i])), O + FVector(0.f, 35.f, 150.f - i * 13.f), FRotator(0.f, 90.f, 0.f), FVector(1.f))) JeebProps::TintArrow(C, JeebItems::ArrowId(Order[i]));
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (UStaticMeshComponent* Post = Piece(Cube, O + FVector(320.f, 0.f, 80.f), FRotator::ZeroRotator, FVector(0.4f, 0.6f, 1.6f)))
			if (UMaterialInterface* Mc = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"))) { UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(Mc, Post); D->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.32f, 0.2f, 0.1f)); Post->SetMaterial(0, D); }
		for (int32 i = 0; i < 6; ++i)
			if (UStaticMeshComponent* C = Piece(JeebProps::ArrowMesh(JeebItems::ArrowId(Order[i])), O + FVector(300.f + 7.f, -20.f + (i % 3) * 20.f, 120.f - (i / 3) * 45.f), FRotator(-8.f + i * 3.f, -6.f + i * 2.5f, 0.f), FVector(1.f))) JeebProps::TintArrow(C, JeebItems::ArrowId(Order[i]));
		UE_LOG(LogTemp, Log, TEXT("JEEBARROWLINEUP at %s"), *O.ToCompactString());
	}
	if (bBuilt && Clock >= NextFly)
	{
		NextFly = Clock + 0.6f;
		APlayerController* PC = W->GetFirstPlayerController();   // (test pictures only)
		AKnightCharacter* K = PC ? Cast<AKnightCharacter>(PC->GetPawn()) : nullptr;
		if (!K) return;
		FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector A = O + FVector(800.f, -900.f, 320.f);   // (over the tall grass)
		FVector From = A, Dir(0.f, 1.f, 0.f);
		static const bool bCam = FParse::Param(FCommandLine::Get(), TEXT("ArrowFilmCam"));
		if (bCam)   // (-ArrowFilmCam, smear film 2026-11-26: each arrow crosses the view 4 m in front of the camera, left to right, wherever the camera has got to)
		{
			AActor* VT = PC->GetViewTarget();
			if (VT)
			{
				const FRotator Y = VT->GetActorRotation();   // (the camera's own look, pitch too, so the arrow crosses wherever it points)
				float D = 300.f; FParse::Value(FCommandLine::Get(), TEXT("ArrowFilmDist="), D);
				Dir = FRotationMatrix(Y).GetScaledAxis(EAxis::Y);
				From = VT->GetActorLocation() + Y.Vector() * D - Dir * D * 1.05f;
			}
		}
		UE_LOG(LogTemp, Log, TEXT("JEEBARROWFLY #%d kind %d at clock %.2f from %s"), FlyK, Order[FlyK % 6], Clock, *From.ToCompactString());
		if (AProjectile* Pj = W->SpawnActor<AProjectile>(AProjectile::StaticClass(), From, Dir.Rotation(), SP))
			Pj->Launch(K, Dir, 3000.f, 0.f, 1, false, ESkillType::Archery, false, 0, JeebItems::ArrowId(Order[FlyK++ % 6]), 0.f, 0.f);
	}
}

// (a core ticker, on only with -JeebArrowTest or -ArrowLineup=: the game world's test each frame)
static bool ArrowTestTick(float Dt)
{
	static int32 On = -1;   // (the command line is read once the engine is up: at load it is still empty)
	if (On < 0) { if (!GEngine) return true; On = (FParse::Param(FCommandLine::Get(), TEXT("JeebArrowTest")) || FCString::Strifind(FCommandLine::Get(), TEXT("ArrowLineup="))) ? 1 : 0; }
	if (On == 0) return false;
	static bool bSaid = false; if (!bSaid) { bSaid = true; UE_LOG(LogTemp, Log, TEXT("JEEBARROWTEST ticker on")); }
	if (!GEngine) return true;
	for (const FWorldContext& C : GEngine->GetWorldContexts())
	{
		UWorld* W = C.World();
		if (!W || !W->IsGameWorld() || !W->GetGameState()) continue;
		const ENetMode M = W->GetNetMode();
		if (M == NM_DedicatedServer) FArrowTestDriver::Server(W, Dt);
		else if (M == NM_Client) FArrowTestDriver::Client(W, Dt);
		else ArrowLineup(W, Dt);
	}
	return true;
}
static struct FArrowTestHook
{
	FArrowTestHook()
	{
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&ArrowTestTick));
	}
} GArrowTestHook;
#endif

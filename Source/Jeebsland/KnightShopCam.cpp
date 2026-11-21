// The shop camera (2026-11-18, owner: "when we go to buy something, zoom in like they do in Hogwarts or Arc Raiders when you're buying
// something"). While a shop, the bank or the smithy is open with its keeper (TradeNpc), your view eases over about half a second to a
// close view of the keeper at the counter, a little off-centre so the trading window sits beside them, and eases back when you close it.
// Your own knight is hidden from your view meanwhile (he stands between the camera and the counter). Looks only, on your own machine:
// nothing is sent, other players see nothing change, and the trade itself is the server's as before (KnightCharacterNet.cpp).
// Live: jeeb.ShopCam.On, .Dist, .Height, .Side, .Time, .Fov.
#include "KnightCharacter.h"
#include "Npc.h"
#include "GameFramework/HUD.h"
#include "Camera/CameraTypes.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"

static TAutoConsoleVariable<int32> CVarShopCamOn(TEXT("jeeb.ShopCam.On"), 1, TEXT("The shop camera: 1 your view eases in to the keeper while you trade, 0 off."));
static TAutoConsoleVariable<float> CVarShopCamDist(TEXT("jeeb.ShopCam.Dist"), 260.f, TEXT("Shop camera: how far from the keeper's face (cm)."));
static TAutoConsoleVariable<float> CVarShopCamHeight(TEXT("jeeb.ShopCam.Height"), 8.f, TEXT("Shop camera: how far above the keeper's eyes (cm; negative looks up at them)."));
static TAutoConsoleVariable<float> CVarShopCamSide(TEXT("jeeb.ShopCam.Side"), -60.f, TEXT("Shop camera: how far right of the picture's middle the keeper stands (cm at their distance; negative: left)."));
static TAutoConsoleVariable<float> CVarShopCamTime(TEXT("jeeb.ShopCam.Time"), 0.5f, TEXT("Shop camera: seconds to ease in, and out again."));
static TAutoConsoleVariable<float> CVarShopCamFov(TEXT("jeeb.ShopCam.Fov"), 55.f, TEXT("Shop camera: the field of view while framing the keeper (degrees)."));

void AKnightCharacter::ShopCamView(float DeltaTime, FMinimalViewInfo& View)
{
	if (!IsLocallyControlled()) return;
	APlayerController* PC = Cast<APlayerController>(GetController());
	ANpc* N = TradeNpc.Get();
	const bool bWant = CVarShopCamOn.GetValueOnGameThread() != 0 && N && (bShopOpen || bSmithOpen || bBankOpen);
	if (bWant) ShopCamNpc = N;
	const float Step = DeltaTime / FMath::Max(CVarShopCamTime.GetValueOnGameThread(), 0.05f);
	ShopCamT = FMath::Clamp(ShopCamT + (bWant ? Step : -Step), 0.f, 1.f);
	ANpc* K = ShopCamNpc.Get();

	// Your knight (and what he carries) hidden from your own view once the camera is well on its way in, back as it leaves
	const bool bHide = K && ShopCamT > 0.35f;
	if (bHide != bShopCamHid && PC)
	{
		bShopCamHid = bHide;
		if (bHide)
		{
			ShopCamHidden.Reset();
			TArray<AActor*> Mine; GetAttachedActors(Mine, true, true);
			Mine.Add(this);
			for (AActor* A : Mine) if (A && !PC->HiddenActors.Contains(A)) { PC->HiddenActors.Add(A); ShopCamHidden.Add(A); }
		}
		else
		{
			for (const TWeakObjectPtr<AActor>& A : ShopCamHidden) if (A.IsValid()) PC->HiddenActors.Remove(A.Get());
			ShopCamHidden.Reset();
		}
	}
	if (ShopCamT <= 0.f || !K) { if (ShopCamT <= 0.f) ShopCamNpc = nullptr; return; }

	// The view: in front of the keeper on your side of them, at their eye height, looking at their face and chest, turned a little
	// so they stand right of the middle
	const FVector KLoc = K->GetActorLocation();
	FVector2D Out = FVector2D(GetActorLocation() - KLoc);
	if (Out.SizeSquared() < 100.f) Out = FVector2D(K->GetActorForwardVector());
	Out.Normalize();
	const float Eye = K->HeadTopZ() - 14.f;
	const FVector Focus(KLoc.X, KLoc.Y, Eye - 22.f);
	FVector Cam = Focus + FVector(Out.X, Out.Y, 0.f) * CVarShopCamDist.GetValueOnGameThread() + FVector(0.f, 0.f, 22.f + CVarShopCamHeight.GetValueOnGameThread());
	// (never inside a wall, a stall's post or the counter: pulled in toward the keeper to whatever is in the way)
	{
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(ShopCam), false, this);
		Q.AddIgnoredActor(K);
		TArray<AActor*> Mine; GetAttachedActors(Mine, true, true);   // (his sword, shield and the rest: hidden, never in the way)
		Q.AddIgnoredActors(Mine);
		const FVector From = Focus + FVector(0.f, 0.f, 22.f);
		if (GetWorld()->SweepSingleByChannel(Hit, From, Cam, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(12.f), Q))
			Cam = From + (Cam - From).GetSafeNormal() * FMath::Max(float(Hit.Distance), 120.f);   // (never closer than 1.2 m to their face)
	}
	const FVector Look = (Focus - Cam).GetSafeNormal();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Look).GetSafeNormal();
	const FRotator Rot = (Focus - Right * CVarShopCamSide.GetValueOnGameThread() - Cam).Rotation();

	const float A = FMath::SmoothStep(0.f, 1.f, ShopCamT);
	View.Location = FMath::Lerp(View.Location, Cam, A);
	View.Rotation = FQuat::Slerp(View.Rotation.Quaternion(), Rot.Quaternion(), A).Rotator();
	View.FOV = FMath::Lerp(View.FOV, CVarShopCamFov.GetValueOnGameThread(), A);

}

#if !UE_BUILD_SHIPPING
namespace
{
	struct FShopCamTest
	{
		FShopCamTest()
		{
			FWorldDelegates::OnWorldTickStart.AddLambda([](UWorld* W, ELevelTick, float)
			{
				static FString List;
				static const bool bOn = FParse::Value(FCommandLine::Get(), TEXT("ShopCamTest="), List, false);
				if (!bOn || !W || !W->IsGameWorld()) return;
				APlayerController* PC = W->GetFirstPlayerController();   // (test pictures only)
				AKnightCharacter* Me = PC ? Cast<AKnightCharacter>(PC->GetPawn()) : nullptr;
				if (!Me) return;
				static TArray<FString> Names;
				if (Names.Num() == 0) List.ParseIntoArray(Names, TEXT("+"));
				const float T = W->GetTimeSeconds() - 10.f;
				if (T < 0.f) return;
				// (-ShopCamHold=<s>, 2026-11-20: the menu stays open that long, a picture every second, to film the keeper's idle and gestures)
				static float Hold = 0.f;
				static const bool bHold = FParse::Value(FCommandLine::Get(), TEXT("ShopCamHold="), Hold) && Hold > 0.f;
				if (bHold)
				{
					const float Per = Hold + 2.f;
					const int32 Idx = FMath::FloorToInt(T / Per);
					const float In = T - Idx * Per;
					static int32 Done = -1;
					if (Idx >= Names.Num())
					{
						static bool bQuit = false; if (!bQuit) { bQuit = true; UE_LOG(LogTemp, Log, TEXT("JEEBSHOPCAM done")); FPlatformMisc::RequestExit(false); }
						return;
					}
					const int32 Key = Idx * 1000 + (In < 0.3f ? 0 : In < 0.6f ? 1 : In >= 0.6f + Hold ? 999 : 2 + FMath::FloorToInt(In - 0.6f));
					if (Key == Done) return;
					Done = Key;
					const int32 Step = Key % 1000;
					if (Step == 0)
					{
						ANpc* K = nullptr;
						for (TActorIterator<ANpc> It(W); It; ++It) if (It->GetNpcName().StartsWith(Names[Idx])) { K = *It; break; }
						if (!K) { UE_LOG(LogTemp, Warning, TEXT("JEEBSHOPCAM no keeper %s"), *Names[Idx]); return; }
						const FVector F = K->GetActorForwardVector();
						Me->SetActorLocation(K->GetActorLocation() + FVector(F.X, F.Y, 0.f) * 170.f + FVector(0.f, 0.f, 30.f), false, nullptr, ETeleportType::TeleportPhysics);
						Me->SetActorRotation(FRotator(0.f, K->GetActorRotation().Yaw + 180.f, 0.f));
						PC->SetControlRotation(FRotator(-12.f, K->GetActorRotation().Yaw + 180.f, 0.f));
						UE_LOG(LogTemp, Log, TEXT("JEEBSHOPCAM hold %s"), *K->GetNpcName());
					}
					else if (Step == 1) { Me->ShopCamTestOpen(true); if (AHUD* Hud = PC->GetHUD()) Hud->bShowHUD = false; }   // (pictures of the keeper without the menu over him)
					else if (Step == 999) { Me->ShopCamTestOpen(false); if (AHUD* Hud = PC->GetHUD()) Hud->bShowHUD = true; }
					else FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / FString::Printf(TEXT("keeper_%d_%02d.png"), Idx, Step - 2), false, false);   // (without the menu: the keeper behind it)
					return;
				}
				const int32 Idx = FMath::FloorToInt(T / 6.f);
				const float In = T - Idx * 6.f;
				static int32 Stage = -1, Done = -1;
				if (Idx >= Names.Num())
				{
					if (In > 0.5f) { static bool bQuit = false; if (!bQuit) { bQuit = true; UE_LOG(LogTemp, Log, TEXT("JEEBSHOPCAM done")); FPlatformMisc::RequestExit(false); } }
					return;
				}
				const int32 Key = Idx * 10 + (In < 0.1f ? 0 : In < 0.35f ? 1 : In < 1.5f ? 2 : In < 3.f ? 3 : In < 3.25f ? 4 : In < 4.5f ? 5 : 6);
				if (Key == Done) return;
				Done = Key;
				const int32 Step = Key % 10;
				const FString Shot = FPaths::ProjectSavedDir() / TEXT("Shots") / FString::Printf(TEXT("shopcam_%d_%c.png"), Idx, TCHAR('a' + Step));
				if (Step == 0)
				{
					ANpc* K = nullptr;
					for (TActorIterator<ANpc> It(W); It; ++It) if (It->GetNpcName().StartsWith(Names[Idx])) { K = *It; break; }
					if (!K) { UE_LOG(LogTemp, Warning, TEXT("JEEBSHOPCAM no keeper %s"), *Names[Idx]); return; }
					const FVector F = K->GetActorForwardVector();
					const FVector At = K->GetActorLocation() + FVector(F.X, F.Y, 0.f) * 170.f;
					Me->SetActorLocation(At + FVector(0.f, 0.f, 30.f), false, nullptr, ETeleportType::TeleportPhysics);
					Me->SetActorRotation(FRotator(0.f, K->GetActorRotation().Yaw + 180.f, 0.f));
					PC->SetControlRotation(FRotator(-12.f, K->GetActorRotation().Yaw + 180.f, 0.f));
					UE_LOG(LogTemp, Log, TEXT("JEEBSHOPCAM %s at %.0f,%.0f,%.0f facing %.0f; knight put at %.0f,%.0f"), *K->GetNpcName(), K->GetActorLocation().X, K->GetActorLocation().Y, K->GetActorLocation().Z, K->GetActorRotation().Yaw, At.X, At.Y);
					Stage = Idx;
					return;
				}
				if (Step == 1) { Me->ShopCamTestOpen(true); FScreenshotRequest::RequestScreenshot(Shot, true, false); }    // (as it opens: the knight's own view)
				if (Step == 2) FScreenshotRequest::RequestScreenshot(Shot, true, false);   // (on the way in)
				if (Step == 3) FScreenshotRequest::RequestScreenshot(Shot, true, false);   // (settled)
				if (Step == 4) { Me->ShopCamTestOpen(false); }
				if (Step == 5) FScreenshotRequest::RequestScreenshot(Shot, true, false);   // (on the way out)
				if (Step == 6) FScreenshotRequest::RequestScreenshot(Shot, true, false);   // (back)
			});
		}
	} GShopCamTest;
}

// (test: opens the shop of the keeper standing nearest in front of the knight, as talking to them to the end does; or closes it)
void AKnightCharacter::ShopCamTestOpen(bool bOpen)
{
	if (!bOpen) { CloseShop(); CloseBank(); CloseSmith(); return; }
	ANpc* Best = nullptr; float BestD = 600.f;
	for (TActorIterator<ANpc> It(GetWorld()); It; ++It)
	{
		if (!It->OffersTrade()) continue;
		const float D = FVector::Dist2D(It->GetActorLocation(), GetActorLocation());
		if (D < BestD) { BestD = D; Best = *It; }
	}
	if (!Best) return;
	TradeNpc = Best;
	Best->SetTalking(true);
	if (Best->GetRole() == ENpcRole::Blacksmith) OpenSmith();
	else if (Best->GetRole() == ENpcRole::Banker) OpenBank();
	else OpenShop();
}
#else
void AKnightCharacter::ShopCamTestOpen(bool) {}
#endif

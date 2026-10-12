#include "KnightPlayerController.h"
#include "KnightGameMode.h"
#include "JeebGameInstance.h"
#include "JeebSave.h"
#include "Terrain.h"
#include "Camera/CameraActor.h"
#include "Engine/World.h"
#include "GameFramework/GameSession.h"
#include "UObject/Package.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

AKnightPlayerController::AKnightPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AKnightPlayerController::BeginPlay()
{
	Super::BeginPlay();
	ConnectTime = FPlatformTime::Seconds();
}

void AKnightPlayerController::SendLogin()
{
	if (bSentLogin) return;
	bSentLogin = true;
	UJeebGameInstance* GI = GetGameInstance<UJeebGameInstance>();
	// Playing as a client in the editor (no starting screen): a test account per editor window, no password
	if (GetWorld()->WorldType == EWorldType::PIE || !GI || GI->LoginName.IsEmpty())
	{
		const int32 Id = FMath::Max(1, GetWorld()->GetOutermost()->GetPIEInstanceID());
		ServerLogin(FString::Printf(TEXT("Tester %d"), Id), FString(), false, true);
		return;
	}
	ServerLogin(GI->LoginName, GI->LoginPassword, GI->bCreateAccount, false);
}

void AKnightPlayerController::ServerLogin_Implementation(const FString& Name, const FString& Password, bool bCreate, bool bDev)
{
	if (AKnightGameMode* GM = GetWorld()->GetAuthGameMode<AKnightGameMode>()) GM->TryLogin(this, Name, Password, bCreate, bDev);
}

void AKnightPlayerController::ClientLoginResult_Implementation(bool bOk, const FString& Message)
{
	bLoginDone = true;
	UJeebGameInstance* GI = GetGameInstance<UJeebGameInstance>();
	if (bOk)
	{
		if (GI) { GI->bJoining = false; GI->bCreateAccount = false; GI->MenuMessage.Reset(); }
		return;
	}
	if (GI) GI->ReturnToMenu(Message);   // refused: back to the starting screen with the reason
}

void AKnightPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Server: a connection that never logs in is shown the door after 30 seconds
	if (HasAuthority() && !IsLocalController() && GetNetMode() != NM_Standalone && !bLoggedIn && FPlatformTime::Seconds() - ConnectTime > 30.0)
	{
		ConnectTime = FPlatformTime::Seconds() + 1.0e6;   // (only once)
		if (AGameModeBase* GM = GetWorld()->GetAuthGameMode()) if (GM->GameSession) GM->GameSession->KickPlayer(this, FText::FromString(TEXT("You didn't log in in time.")));
	}
}

void AKnightPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	// (done here rather than in BeginPlay: on a client this controller only becomes "ours" a moment after it appears)
	if (GetNetMode() == NM_Client && !bSentLogin) SendLogin();
	if (!MenuCam && !bMenuSetup && UJeebGameInstance::InMenu(this))
	{
		// The starting screen: the world behind it, seen from a camera slowly circling Blackrock; the mouse is free
		bMenuSetup = true;
		MenuCam = GetWorld()->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (MenuCam) SetViewTarget(MenuCam);
		bShowMouseCursor = true;
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		SetInputMode(Mode);
	}
	// Testing: -JeebAutoLogin=Name:Password:World[:new] skips the starting screen once (TEST ACCOUNTS ONLY: the command line
	// is written into this machine's log)
	static bool bAutoTried = false;
	FString Auto;
	if (MenuCam && !bAutoTried && FParse::Value(FCommandLine::Get(), TEXT("JeebAutoLogin="), Auto, false))
	{
		bAutoTried = true;
		TArray<FString> P;
		Auto.ParseIntoArray(P, TEXT(":"), false);
		UJeebGameInstance* GI = GetGameInstance<UJeebGameInstance>();
		if (GI && P.Num() >= 3)
		{
			GI->LoginName = P[0];
			GI->LoginPassword = P[1];
			GI->bCreateAccount = P.Num() >= 4 && P[3] == TEXT("new");
			GI->JoinWorld(FMath::Max(0, FCString::Atoi(*P[2]) - 1));
		}
	}
	if (!MenuCam) return;
	// A slow circle round the walled town, high enough to see over it, looking in at its middle
	MenuClock += DeltaTime;
	const FVector2D C = JeebTerrain::VillageCenter();
	const float R = JeebTerrain::VillageHalf() + 2600.f;
	const float A = MenuClock * 0.025f + 0.6f;
	const FVector2D At = C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R;
	const float Ground = JeebTerrain::HeightAt(At.X, At.Y);
	const FVector Eye(At.X, At.Y, Ground + 1500.f);
	const FVector Look(C.X, C.Y, JeebTerrain::HeightAt(C.X, C.Y) + 200.f);
	MenuCam->SetActorLocationAndRotation(Eye, (Look - Eye).Rotation());
}

void AKnightPlayerController::PawnLeavingGame()
{
	if (HasAuthority())
		if (AKnightGameMode* GM = GetWorld()->GetAuthGameMode<AKnightGameMode>()) GM->SaveCharacter(this);
	Super::PawnLeavingGame();
}

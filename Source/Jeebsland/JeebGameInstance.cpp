#include "JeebGameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ConfigCacheIni.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "GameFramework/PlayerController.h"

void UJeebGameInstance::Init()
{
	Super::Init();
	if (Worlds.Num() == 0)
	{
		FJeebWorld W; W.Name = TEXT("World 1"); W.Port = 7777;
		Worlds.Add(W);
	}
	Status.SetNum(Worlds.Num());

	// Remembered from last time; -JeebServer=<address> on the command line wins (the test scripts use it)
	if (GConfig)
	{
		GConfig->GetString(TEXT("Jeebsland"), TEXT("ServerAddress"), ServerAddress, GGameUserSettingsIni);
		GConfig->GetString(TEXT("Jeebsland"), TEXT("LastName"), LoginName, GGameUserSettingsIni);
	}
	FString Addr;
	if (FParse::Value(FCommandLine::Get(), TEXT("JeebServer="), Addr) && !Addr.IsEmpty()) ServerAddress = Addr;
	if (ServerAddress.IsEmpty()) ServerAddress = TEXT("127.0.0.1");

	if (GEngine)
	{
		GEngine->OnNetworkFailure().AddUObject(this, &UJeebGameInstance::OnNetworkFailure);
		GEngine->OnTravelFailure().AddUObject(this, &UJeebGameInstance::OnTravelFailure);
	}
}

void UJeebGameInstance::Shutdown()
{
	if (GEngine)
	{
		GEngine->OnNetworkFailure().RemoveAll(this);
		GEngine->OnTravelFailure().RemoveAll(this);
	}
	LoginPassword.Reset();
	Super::Shutdown();
}

bool UJeebGameInstance::InMenu(const UObject* WorldContext)
{
	const UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!W || W->GetNetMode() != NM_Standalone || W->WorldType != EWorldType::Game) return false;
	const TCHAR* Cmd = FCommandLine::Get();
	// (the editor's "Standalone Game" play mode starts with -PIEVIACONSOLE: like every editor Play button, it plays straight away)
	return !FParse::Param(Cmd, TEXT("JeebOffline")) && !FParse::Param(Cmd, TEXT("JeebShot")) && !FParse::Param(Cmd, TEXT("PIEVIACONSOLE"));
}

void UJeebGameInstance::RememberChoices()
{
	if (!GConfig) return;
	GConfig->SetString(TEXT("Jeebsland"), TEXT("ServerAddress"), *ServerAddress, GGameUserSettingsIni);
	GConfig->SetString(TEXT("Jeebsland"), TEXT("LastName"), *LoginName, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void UJeebGameInstance::PollWorlds()
{
	Status.SetNum(Worlds.Num());
	const double Now = FPlatformTime::Seconds();
	for (int32 i = 0; i < Worlds.Num(); ++i)
	{
		FJeebWorldStatus& S = Status[i];
		if (S.bAsked || Now - S.LastAsk < 3.0) continue;
		S.bAsked = true;
		S.LastAsk = Now;
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
		Req->SetURL(FString::Printf(TEXT("http://%s:%d/status"), *ServerAddress, Worlds[i].Port + StatusPortOffset));
		Req->SetVerb(TEXT("GET"));
		Req->SetTimeout(2.5f);
		TWeakObjectPtr<UJeebGameInstance> Self(this);
		Req->OnProcessRequestComplete().BindLambda([Self, i](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
		{
			if (!Self.IsValid() || !Self->Status.IsValidIndex(i)) return;
			FJeebWorldStatus& St = Self->Status[i];
			St.bAsked = false;
			// the world answers "<players> <max>"
			TArray<FString> Parts;
			if (bOk && Resp.IsValid() && Resp->GetResponseCode() == 200 && Resp->GetContentAsString().ParseIntoArrayWS(Parts) >= 2)
			{
				St.bUp = true;
				St.Players = FCString::Atoi(*Parts[0]);
				St.Max = FCString::Atoi(*Parts[1]);
				St.LastReply = FPlatformTime::Seconds();
			}
			else St.bUp = false;
		});
		Req->ProcessRequest();
	}
}

void UJeebGameInstance::JoinWorld(int32 Index)
{
	if (!Worlds.IsValidIndex(Index)) return;
	ChosenWorld = Index;
	bJoining = true;
	MenuMessage.Reset();
	RememberChoices();
	APlayerController* PC = GetFirstLocalPlayerController();
	if (PC) PC->ClientTravel(FString::Printf(TEXT("%s:%d"), *ServerAddress, Worlds[Index].Port), TRAVEL_Absolute);
}

void UJeebGameInstance::ReturnToMenu(const FString& Why)
{
	bJoining = false;
	MenuMessage = Why;
	if (UWorld* W = GetWorld()) UGameplayStatics::OpenLevel(W, FName(TEXT("/Game/Field")), true);
}

void UJeebGameInstance::OnNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error)
{
	// (the engine then takes us back to the starting map by itself)
	if (!MenuMessage.IsEmpty()) return;   // we already know why (a refused login)
	if (bJoining) MenuMessage = FString::Printf(TEXT("Couldn't reach %s at %s. Is it running?"), *WorldName(ChosenWorld), *ServerAddress);
	else MenuMessage = TEXT("The connection to the world was lost.");
	bJoining = false;
}

void UJeebGameInstance::OnTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error)
{
	if (!MenuMessage.IsEmpty()) return;
	MenuMessage = FString::Printf(TEXT("Couldn't reach %s at %s. Is it running?"), *WorldName(ChosenWorld), *ServerAddress);
	bJoining = false;
}

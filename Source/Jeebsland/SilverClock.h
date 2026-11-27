#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SilverClock.generated.h"

class USceneComponent;

// The working clock on Silver Point's town hall tower (2026-11-27, owner: "make that clock be set to US central time on all servers").
// The Meshy model has a clock painted on both faces of its tower (front and back; model frame (29.6, +-180.8, 1283) cm, the white
// about 118 cm in radius); this puts a real dial over each (gilt rim, cream face, dark markers and hands) proud of
// the painted one (a drum 8.5 cm deep, as the model's painted hands stand out from its dial). The hands show US Central time (America/Chicago, daylight saving by the US rule) worked out from UTC, never the PC's
// own time zone, so every machine shows the same time. Looks only: each client places its own (AOpenWorld::BuildHouses), none on a
// dedicated server, no replication; the hands turn once a second.
namespace JeebClock
{
	// Central time from a UTC time; bDst true in CDT (UTC-5), false in CST (UTC-6)
	FDateTime CentralFromUtc(const FDateTime& Utc, bool* bDst = nullptr);
}

UCLASS()
class ASilverClock : public AActor
{
	GENERATED_BODY()
public:
	ASilverClock();
	// Places one on the town hall (its model's origin and yaw), on clients only
	static void PlaceOn(UWorld* World, const FVector& At, float Yaw);
	virtual void Tick(float Dt) override;
protected:
	virtual void BeginPlay() override;
private:
	void SetHands();
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> HourPivots;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> MinutePivots;
};

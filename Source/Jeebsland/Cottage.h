#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Cottage.generated.h"

class UStaticMeshComponent;
class UBoxComponent;

// An old-time cottage you can walk into: plaster walls, dark timber framing, a thatched roof,
// a chimney, a doorway, windows, and a furnished room with a warm light.
// The actor's local +X side is the front (the door). Its origin is at ground level.
UCLASS()
class ACottage : public AActor
{
	GENERATED_BODY()

public:
	ACottage();

	// Lock level 0 = never locked. A locked hut has a stash of ingots inside (Loot) that comes back some minutes after taking it.
	void Configure(int32 InLockLevel, int32 InLoot);
	virtual void Tick(float DeltaSeconds) override;

	FVector DoorPos() const;      // world position of the middle of the doorway (floor level)
	FVector LootPos() const;
	bool IsLocked() const { return bLocked; }
	bool IsDoorOpen() const { return bOpen; }
	int32 GetLockLevel() const { return LockLevel; }
	void Unlock();                // picked: unlocks and swings open
	void ToggleDoor();            // E: open or close. Closing a hut that has a lock locks it again.
	void SetDoorState(bool bInOpen, bool bInLocked);   // online: the server's door, copied onto this machine's cottage
	bool HasLoot() const { return bLootThere && LootAmount > 0; }
	int32 TakeLoot();             // returns the ingots, empties the stash

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() USceneComponent* DoorPivot = nullptr;
	UPROPERTY() UBoxComponent* DoorSolid = nullptr;
	UPROPERTY() UStaticMeshComponent* LockMesh = nullptr;
	UPROPERTY() TArray<UStaticMeshComponent*> LootMeshes;
	bool bLocked = false, bOpen = false, bLootThere = false;
	int32 LockLevel = 0, LootAmount = 0;
	float DoorAngle = 0.f, LootRespawn = 0.f;
	void ShowLootAndLock();
	TArray<TPair<UStaticMeshComponent*, FLinearColor>> Parts;
};

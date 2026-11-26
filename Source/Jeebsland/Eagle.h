#pragma once
// Eagles (2026-11-26, Docs/AnimalsPlan2.md): a few birds that circle high over the wild herds, now and then perch on a tree top for a while,
// and drop onto a fresh hunting kill nearby to peck at it for a few seconds (the window to shoot one; it can be shot in flight too). A downed
// eagle falls, drops Eagle Feathers and gives Hunting XP. The server decides everything; players' machines glide the bird to where the
// server says it is. Two still bodies (no rig): wings spread in the air, the perched bird on a branch or the ground.
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HitBoxes.h"
#include "Eagle.generated.h"

class UStaticMeshComponent;
class UHealthComponent;

enum class EEagleState : uint8 { Circle, Land, Perch, Peck, Leave, Fall, Down };

UCLASS()
class AEagle : public AActor
{
	GENERATED_BODY()
public:
	AEagle();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// (server) every eagle in the world, over the wild herds' homes; and the test lab's birds (-DeerLab, -EagleLab)
	static void SpawnAll(UWorld* World, const TArray<FVector2D>& Homes);
	static void SpawnLab(UWorld* World, const FVector2D& At, const FVector2D& Fw);
	// (server) a hunted animal fell here: the nearest free eagle may come down to it
	static void OnKill(UWorld* World, const FVector& Body);

	EEagleState StateNow() const { return EEagleState(RepState); }
	UPROPERTY() UHealthComponent* Hp = nullptr;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() USceneComponent* Root = nullptr;
	UPROPERTY() UStaticMeshComponent* FlyBody = nullptr;
	UPROPERTY() UStaticMeshComponent* SitBody = nullptr;
	UPROPERTY() USceneComponent* JBody = nullptr;   // (arrows stick to the nearest joint)

	UPROPERTY(Replicated) FVector_NetQuantize RepLoc;
	UPROPERTY(Replicated) FVector_NetQuantize RepVel;
	UPROPERTY(Replicated) uint16 RepYaw = 0;
	UPROPERTY(ReplicatedUsing = OnRep_State) uint8 RepState = 0;
	UFUNCTION() void OnRep_State();

	FVector2D Home = FVector2D::ZeroVector;
	float Radius = 5000.f, Height = 4500.f, CircleA = 0.f, CircleDir = 1.f;
	FVector Goal = FVector::ZeroVector, KillAt = FVector::ZeroVector, Vel = FVector::ZeroVector;
	float StateT = 0.f, StateFor = 0.f, PerchClock = 60.f, SleepClock = 0.f, RespawnT = 0.f, Bank = 0.f, LastYaw = 0.f;
	bool bSleeping = false, bLab = false;
	FRandomStream Rand;
	double RepAt = 0.0;

	void SetState(EEagleState S, const TCHAR* Why);
	void TickServer(float Dt);
	void TickLook(float Dt);
	bool Fly(const FVector& To, float Want, float Dt);   // steers toward To; true when there
	bool FindPerch(FVector& Out);
	void OnHurt(AActor* From);
	void Die(AActor* From);
	void GatherHurt(TArray<JeebHit::FCapsule>& Out) const;
	bool Flying() const { const EEagleState S = StateNow(); return S == EEagleState::Circle || S == EEagleState::Land || S == EEagleState::Leave || S == EEagleState::Fall; }
};

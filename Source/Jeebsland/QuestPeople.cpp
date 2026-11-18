#include "QuestPeople.h"
#include "QuestData.h"
#include "Npc.h"
#include "Terrain.h"
#include "OakHill.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "WorldCollision.h"

AQuestPeople::AQuestPeople()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

ANpc* AQuestPeople::Find(const UWorld* World, FName Key)
{
	if (!World || Key.IsNone()) return nullptr;
	for (TActorIterator<ANpc> It(World); It; ++It)
		if (It->IsQuestGiver() && It->GetPersonKey() == Key) return *It;
	return nullptr;
}

void AQuestPeople::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();
	if (!World || !JeebOakHill::Enabled()) return;
	const double T0 = FPlatformTime::Seconds();
	FCollisionQueryParams Q(SCENE_QUERY_STAT(QuestPeople), false);
	auto GroundAt = [&](const FVector2D& P) -> float
	{
		const float H = JeebTerrain::HeightAt(P.X, P.Y);
		FHitResult Hit;
		// (the town's paving: the first solid thing under a point 2 m up, below any roof)
		if (World->LineTraceSingleByChannel(Hit, FVector(P.X, P.Y, H + 200.f), FVector(P.X, P.Y, H - 300.f), ECC_Pawn, Q) && Hit.GetActor() && !Hit.GetActor()->IsA<APawn>()) return Hit.ImpactPoint.Z;
		return H;
	};
	const FCollisionShape Body = FCollisionShape::MakeCapsule(38.f, 92.f);
	auto Free = [&](const FVector2D& P)
	{
		const float G = GroundAt(P);
		if (G > JeebTerrain::HeightAt(P.X, P.Y) + 60.f) return false;   // (standing on top of a prop: a cart, a crate)
		return !World->OverlapBlockingTestByChannel(FVector(P.X, P.Y, G + 92.f + 15.f), FQuat::Identity, ECC_Pawn, Body, Q);
	};
	FString Report;
	for (const JeebQuests::FPersonDef& P : JeebQuests::People())
	{
		// Their spot, or the nearest free one on rings 50 cm apart (12 directions each, starting north), up to 3 m off
		FVector2D At = P.Pos;
		bool bFree = Free(At);
		for (int32 Ring = 1; Ring <= 6 && !bFree; ++Ring)
			for (int32 k = 0; k < 12 && !bFree; ++k)
			{
				const float A = FMath::DegreesToRadians(30.f * k);
				const FVector2D C = P.Pos + FVector2D(FMath::Cos(A), FMath::Sin(A)) * (50.f * Ring);
				if (Free(C)) { At = C; bFree = true; }
			}
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ANpc* N = World->SpawnActor<ANpc>(FVector(At.X, At.Y, GroundAt(At) + 92.f), FRotator(0.f, P.Yaw, 0.f), SP);
		if (!N) continue;
		FNpcLook L;
		L.Name = P.Name; L.Tunic = P.Tunic; L.Hair = P.Hair; L.Hat = P.Hat; L.bApron = P.bApron; L.Scale = P.Scale;
		N->SetupPerson(P.Key, L);
		Report += FString::Printf(TEXT(" %s %.0f,%.0f(moved %.0f%s)"), *P.Key.ToString(), At.X, At.Y, FVector2D::Distance(At, P.Pos), bFree ? TEXT("") : TEXT(", NO FREE SPOT"));
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBQUEST people (%.1f ms):%s"), (FPlatformTime::Seconds() - T0) * 1000.0, *Report);
#if !UE_BUILD_SHIPPING
	// (test runs, -JeebQuestTest: where the shops' counters are, to choose spots for the people)
	if (FParse::Param(FCommandLine::Get(), TEXT("JeebQuestTest")))
		for (const JeebOakHill::FCounter& C : JeebOakHill::Counters())
			UE_LOG(LogTemp, Log, TEXT("JEEBQUEST counter %s shop %d at north %.1f east %.1f m from the plaza, facing %.0f"), C.Name, int32(C.Shop),
				(C.At.X - JeebOakHill::PosX) / 100.f, (C.At.Y - JeebOakHill::PosY) / 100.f, C.Facing);
#endif
}

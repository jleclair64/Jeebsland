#include "SilverClock.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Math/RotationMatrix.h"

FDateTime JeebClock::CentralFromUtc(const FDateTime& Utc, bool* bDst)
{
	// US rule (since 2007): daylight time from the second Sunday of March at 2:00 CST (08:00 UTC) to the first Sunday of November at
	// 2:00 CDT (07:00 UTC)
	const int32 Y = Utc.GetYear();
	auto FirstSunday = [Y](int32 Month)
	{
		const int32 Dow = int32(FDateTime(Y, Month, 1).GetDayOfWeek());   // (Monday 0 .. Sunday 6)
		return 1 + (6 - Dow + 7) % 7;
	};
	const FDateTime Start(Y, 3, FirstSunday(3) + 7, 8, 0, 0), End(Y, 11, FirstSunday(11), 7, 0, 0);
	const bool D = Utc >= Start && Utc < End;
	if (bDst) *bDst = D;
	return Utc - FTimespan::FromHours(D ? 5.0 : 6.0);
}

namespace
{
	// The painted clock faces in the town hall model's own frame (Unreal: Blender's Y mirrored; found by ray casts on
	// Art/Buildings/SilverTownHall_300k.glb at the game size): the dial's plane, its middle, and the radii of the new rim and face
	const float FaceX = 29.6f, FaceY = 180.8f, FaceZ = 1283.f, RimR = 117.f, DialR = 111.f;
	// A face-frame turn that points a hand or marker (made along +Z) at an angle clockwise from 12 as seen from in front. The viewer looks
	// along the face's -X, so their right is the face's -Y (Unreal is left-handed)
	FRotator Towards(float Deg)
	{
		const float A = FMath::DegreesToRadians(Deg);
		return FRotationMatrix::MakeFromXZ(FVector(1.f, 0.f, 0.f), FVector(0.f, -FMath::Sin(A), FMath::Cos(A))).Rotator();
	}
}

ASilverClock::ASilverClock()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 1.f;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ASilverClock::PlaceOn(UWorld* World, const FVector& At, float Yaw)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer) return;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	World->SpawnActor<ASilverClock>(ASilverClock::StaticClass(), At, FRotator(0.f, Yaw, 0.f), P);
}

void ASilverClock::BeginPlay()
{
	Super::BeginPlay();
	if (IsNetMode(NM_DedicatedServer)) { SetActorTickEnabled(false); return; }
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	const FLinearColor Gilt(0.42f, 0.30f, 0.10f), Cream(0.80f, 0.72f, 0.55f), Ink(0.025f, 0.022f, 0.025f);
	int32 N = 0;
	auto Part = [&](USceneComponent* Parent, UStaticMesh* Mesh, const FVector& Loc, const FRotator& Rot, const FVector& Scale, const FLinearColor& Col)
	{
		if (!Mesh) return;
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("ClockPart%d"), N++));
		C->SetStaticMesh(Mesh);
		C->SetupAttachment(Parent);
		C->SetRelativeLocationAndRotation(Loc, Rot);
		C->SetRelativeScale3D(Scale);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);   // (flat on the wall)
		C->RegisterComponent();
		if (Base) if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), Col);
	};
	auto Pivot = [&](USceneComponent* Parent, float Out) -> USceneComponent*
	{
		USceneComponent* S = NewObject<USceneComponent>(this, *FString::Printf(TEXT("ClockPivot%d"), N++));
		S->SetupAttachment(Parent);
		S->SetRelativeLocation(FVector(Out, 0.f, 0.f));
		S->RegisterComponent();
		return S;
	};
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		// Each face's frame: X out of the wall, Z up, origin on the painted dial's plane at its middle
		USceneComponent* Face = NewObject<USceneComponent>(this, *FString::Printf(TEXT("ClockFace%d"), N++));
		Face->SetupAttachment(RootComponent);
		Face->SetRelativeLocationAndRotation(FVector(FaceX, Side * FaceY, FaceZ), FRotator(0.f, Side * 90.f, 0.f));
		Face->RegisterComponent();
		// (the engine cylinder: 100 cm across, 100 tall along its Z; pitched onto X)
		const FRotator Flat(90.f, 0.f, 0.f);
		Part(Face, Cyl, FVector(4.4f, 0, 0), Flat, FVector(RimR / 50.f, RimR / 50.f, 0.082f), Gilt);    // gilt drum, 0.3 to 8.5 cm out (over the model's raised painted hands)
		Part(Face, Cyl, FVector(8.6f, 0, 0), Flat, FVector(DialR / 50.f, DialR / 50.f, 0.01f), Cream);   // cream face, to 9.1 cm
		for (int32 h = 0; h < 12; ++h)
		{
			const bool bMajor = h % 3 == 0;
			const float L = bMajor ? 18.f : 11.f, R = DialR - 8.f - L * 0.5f;
			const float A = FMath::DegreesToRadians(h * 30.f);
			Part(Face, Cube, FVector(9.4f, -FMath::Sin(A) * R, FMath::Cos(A) * R), Towards(h * 30.f), FVector(0.008f, bMajor ? 0.07f : 0.045f, L / 100.f), Ink);
		}
		USceneComponent* HP = Pivot(Face, 9.9f);
		Part(HP, Cube, FVector(0, 0, 22.f), FRotator::ZeroRotator, FVector(0.012f, 0.075f, 0.62f), Ink);   // hour hand, 53 cm past the middle
		USceneComponent* MP = Pivot(Face, 10.7f);
		Part(MP, Cube, FVector(0, 0, 36.f), FRotator::ZeroRotator, FVector(0.012f, 0.045f, 0.92f), Ink);   // minute hand, 82 cm
		Part(Face, Cyl, FVector(11.5f, 0, 0), Flat, FVector(0.14f, 0.14f, 0.025f), Gilt);                   // the boss over both
		HourPivots.Add(HP); MinutePivots.Add(MP);
	}
	bool bDst = false;
	const FDateTime Utc = FDateTime::UtcNow(), C = JeebClock::CentralFromUtc(Utc, &bDst);
	UE_LOG(LogTemp, Log, TEXT("JEEBCLOCK town hall clock: Central %s %s (UTC %s), %d faces"), *C.ToString(TEXT("%H:%M:%S")), bDst ? TEXT("CDT") : TEXT("CST"),
		*Utc.ToString(TEXT("%Y-%m-%d %H:%M:%S")), HourPivots.Num());
	SetHands();
}

void ASilverClock::Tick(float Dt)
{
	Super::Tick(Dt);
	SetHands();
}

void ASilverClock::SetHands()
{
	const FDateTime C = JeebClock::CentralFromUtc(FDateTime::UtcNow());
	const float Min = C.GetMinute() + C.GetSecond() / 60.f;
	const float HourDeg = (C.GetHour() % 12) * 30.f + Min * 0.5f, MinDeg = Min * 6.f;
	// Clockwise seen from in front: the same roll as the markers' (12 up, 3 to the viewer's right)
	for (USceneComponent* P : HourPivots) if (P) P->SetRelativeRotation(Towards(HourDeg));
	for (USceneComponent* P : MinutePivots) if (P) P->SetRelativeRotation(Towards(MinDeg));
}

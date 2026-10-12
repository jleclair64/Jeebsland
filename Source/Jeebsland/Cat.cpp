#include "Cat.h"
#include "Terrain.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"

ACat::ACat()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	BodyPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyPivot"));
	BodyPivot->SetupAttachment(Root);
	HeadPivot = CreateDefaultSubobject<USceneComponent>(TEXT("HeadPivot"));
	HeadPivot->SetupAttachment(BodyPivot);
	HeadPivot->SetRelativeLocation(FVector(17.f, 0.f, 20.f));
	TailPivot = CreateDefaultSubobject<USceneComponent>(TEXT("TailPivot"));
	TailPivot->SetupAttachment(BodyPivot);
	TailPivot->SetRelativeLocation(FVector(-17.f, 0.f, 15.f));
	auto Leg = [&](const TCHAR* Name, float X, float Y)
	{
		USceneComponent* L = CreateDefaultSubobject<USceneComponent>(Name);
		L->SetupAttachment(BodyPivot);
		L->SetRelativeLocation(FVector(X, Y, 11.f));
		return L;
	};
	LegFL = Leg(TEXT("LegFL"), 12.f, -5.f);  LegFR = Leg(TEXT("LegFR"), 12.f, 5.f);
	LegBL = Leg(TEXT("LegBL"), -12.f, -5.f); LegBR = Leg(TEXT("LegBR"), -12.f, 5.f);

	ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));

	int32 N = 0;
	auto Part = [&](USceneComponent* Parent, UStaticMesh* M, const FVector& Loc, const FRotator& Rot, const FVector& Scale, int32 ColourRole)
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("P%d"), N++));
		C->SetupAttachment(Parent);
		C->SetStaticMesh(M);
		C->SetRelativeLocation(Loc);
		C->SetRelativeRotation(Rot);
		C->SetRelativeScale3D(Scale);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		Parts.Add(TPair<UStaticMeshComponent*, int32>(C, ColourRole));
	};
	// Sizes are in centimetres / 100 (a cat about 50 cm long and 25 cm tall)
	Part(BodyPivot, Sphere.Object, FVector(0, 0, 17), FRotator::ZeroRotator, FVector(0.46f, 0.20f, 0.19f), 0);                 // body
	Part(BodyPivot, Sphere.Object, FVector(2, 0, 12), FRotator::ZeroRotator, FVector(0.36f, 0.15f, 0.10f), 1);                 // pale belly
	Part(HeadPivot, Sphere.Object, FVector(0, 0, 0), FRotator::ZeroRotator, FVector(0.19f, 0.17f, 0.16f), 0);                  // head
	Part(HeadPivot, Cone.Object, FVector(-1, -6.5f, 9), FRotator(0.f, 0.f, -12.f), FVector(0.06f, 0.05f, 0.09f), 3);          // ears
	Part(HeadPivot, Cone.Object, FVector(-1, 6.5f, 9), FRotator(0.f, 0.f, 12.f), FVector(0.06f, 0.05f, 0.09f), 3);
	Part(HeadPivot, Sphere.Object, FVector(7.5f, -3.5f, 2.f), FRotator::ZeroRotator, FVector(0.035f, 0.035f, 0.045f), 2);     // eyes
	Part(HeadPivot, Sphere.Object, FVector(7.5f, 3.5f, 2.f), FRotator::ZeroRotator, FVector(0.035f, 0.035f, 0.045f), 2);
	Part(HeadPivot, Sphere.Object, FVector(9.f, 0.f, -1.f), FRotator::ZeroRotator, FVector(0.03f, 0.04f, 0.025f), 3);          // nose
	Part(TailPivot, Cyl.Object, FVector(-9, 0, 8), FRotator(-35.f, 0.f, 0.f), FVector(0.045f, 0.045f, 0.30f), 0);              // tail, up and back
	Part(LegFL, Cyl.Object, FVector(0, 0, -5), FRotator::ZeroRotator, FVector(0.055f, 0.055f, 0.17f), 0);
	Part(LegFR, Cyl.Object, FVector(0, 0, -5), FRotator::ZeroRotator, FVector(0.055f, 0.055f, 0.17f), 0);
	Part(LegBL, Cyl.Object, FVector(0, 0, -5), FRotator::ZeroRotator, FVector(0.06f, 0.06f, 0.17f), 0);
	Part(LegBR, Cyl.Object, FVector(0, 0, -5), FRotator::ZeroRotator, FVector(0.06f, 0.06f, 0.17f), 0);
}

void ACat::Setup(const FVector2D& InHome, float InRoamRadius, const FLinearColor& InFur, const FLinearColor& InBelly, int32 Seed)
{
	Home = InHome;
	RoamRadius = InRoamRadius;
	Fur = InFur;
	Belly = InBelly;
	Rand.Initialize(Seed);
	Goal = Home;
	StateT = Rand.FRandRange(1.f, 5.f);
}

void ACat::BeginPlay()
{
	Super::BeginPlay();
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	for (const TPair<UStaticMeshComponent*, int32>& P : Parts)
	{
		FLinearColor C = Fur;
		if (P.Value == 1) C = Belly;
		else if (P.Value == 2) C = FLinearColor(0.35f, 0.9f, 0.3f) * 1.4f;   // green eyes, glowing a little
		else if (P.Value == 3) C = FLinearColor(0.85f, 0.55f, 0.55f);
		if (UMaterialInstanceDynamic* M = P.Key->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), C);
	}
	const FVector L = GetActorLocation();
	SetActorLocation(FVector(L.X, L.Y, JeebTerrain::HeightAt(L.X, L.Y)));
}

bool ACat::Open(const FVector2D& P) const
{
	return !JeebTerrain::PropBlocked(P.X, P.Y, 60.f) && !JeebTerrain::NoGroundCover(P.X, P.Y);   // not through walls, fences or huts
}

void ACat::PickGoal()
{
	for (int32 Try = 0; Try < 12; ++Try)
	{
		const float A = Rand.FRandRange(0.f, 2.f * PI), R = Rand.FRandRange(150.f, RoamRadius);
		const FVector2D P = Home + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R;
		if (Open(P)) { Goal = P; return; }
	}
	Goal = Home;
}

void ACat::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = FMath::Min(DeltaSeconds, 0.05f);
	const float T = GetWorld()->GetTimeSeconds();
	const FVector Me = GetActorLocation();
	const FVector2D Me2(Me.X, Me.Y);
	APawn* P = UGameplayStatics::GetPlayerPawn(this, 0);

	// startled by the knight coming close: trot away from him
	if (P && State != EState::Flee)
	{
		const FVector2D To = FVector2D(P->GetActorLocation().X, P->GetActorLocation().Y) - Me2;
		if (To.Size() < 260.f)
		{
			State = EState::Flee;
			StateT = Rand.FRandRange(1.5f, 2.5f);
			Goal = Me2 - To.GetSafeNormal() * 450.f;
			if (!Open(Goal)) PickGoal();
		}
	}

	StateT -= Dt;
	float Speed = 0.f;
	if (State == EState::Sit)
	{
		if (StateT <= 0.f) { State = EState::Walk; PickGoal(); StateT = 20.f; }
	}
	else
	{
		Speed = (State == EState::Flee) ? 380.f : 110.f;
		const FVector2D D = Goal - Me2;
		const float Len = D.Size();
		if (Len < 12.f || StateT <= 0.f) { State = EState::Sit; StateT = Rand.FRandRange(4.f, 14.f); Speed = 0.f; }
		else
		{
			const FVector2D N = Me2 + D / Len * Speed * Dt;
			if (Open(N))
			{
				SetActorLocation(FVector(N.X, N.Y, JeebTerrain::HeightAt(N.X, N.Y)));
				const float Want = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
				SetActorRotation(FRotator(0.f, FMath::FixedTurn(GetActorRotation().Yaw, Want, 360.f * Dt), 0.f));
			}
			else { State = EState::Sit; StateT = Rand.FRandRange(2.f, 5.f); Speed = 0.f; }   // blocked: sit and think
		}
	}

	// animation: legs swing when walking, the body settles when sitting, the tail sways, the head looks about
	Walking = FMath::FInterpTo(Walking, Speed > 1.f ? 1.f : 0.f, Dt, 8.f);
	SitAlpha = FMath::FInterpTo(SitAlpha, State == EState::Sit ? 1.f : 0.f, Dt, 4.f);
	Phase += Dt * (Speed > 200.f ? 16.f : 9.f) * Walking;
	const float Sw = 32.f * FMath::Sin(Phase) * Walking;
	LegFL->SetRelativeRotation(FRotator(Sw, 0.f, 0.f));
	LegBR->SetRelativeRotation(FRotator(Sw, 0.f, 0.f));
	LegFR->SetRelativeRotation(FRotator(-Sw, 0.f, 0.f));
	LegBL->SetRelativeRotation(FRotator(-Sw, 0.f, 0.f));
	BodyPivot->SetRelativeLocation(FVector(0.f, 0.f, -4.f * SitAlpha + 1.2f * FMath::Abs(FMath::Sin(Phase)) * Walking));
	BodyPivot->SetRelativeRotation(FRotator(-14.f * SitAlpha, 0.f, 0.f));   // sitting: the front rises, the back sinks
	const float Id = float(GetUniqueID() % 100);
	TailPivot->SetRelativeRotation(FRotator(0.f, 22.f * FMath::Sin(T * (State == EState::Sit ? 1.6f : 3.f) + Id), 0.f));
	HeadPivot->SetRelativeRotation(FRotator(8.f * SitAlpha * FMath::Sin(T * 0.7f), 14.f * SitAlpha * FMath::Sin(T * 0.45f + Id), 0.f));
}

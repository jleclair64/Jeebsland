#include "Fireflies.h"
#include "Terrain.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"

AFireflies::AFireflies()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Bugs = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Bugs"));
	Bugs->SetupAttachment(RootComponent);
	Bugs->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bugs->SetCastShadow(false);
	Bugs->bAffectDynamicIndirectLighting = false;
	Bugs->bAffectDistanceFieldLighting = false;
	ConstructorHelpers::FObjectFinder<UStaticMesh> Ball(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Ball.Succeeded()) Bugs->SetStaticMesh(Ball.Object);
	Rand.Initialize(909);
}

void AFireflies::BeginPlay()
{
	Super::BeginPlay();
	if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Flame.M_Flame")))
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Mat, this);
		M->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.75f, 1.0f, 0.25f));   // a yellow-green glow
		M->SetScalarParameterValue(TEXT("Intensity"), 5.f);
		Bugs->SetMaterial(0, M);
	}
	else UE_LOG(LogTemp, Warning, TEXT("M_Flame is missing: run Tools/BuildMaterials.ps1. Fireflies will not glow."));
	for (int32 i = 0; i < Count; ++i)
	{
		FBug B;
		B.Off = FVector(Rand.FRandRange(-1.f, 1.f), Rand.FRandRange(-1.f, 1.f), 0.f) * 380.f;
		B.Off.Z = Rand.FRandRange(40.f, 220.f);
		B.Phase = Rand.FRandRange(0.f, 2.f * PI);
		B.Rate = Rand.FRandRange(0.25f, 0.6f);
		B.Blink = Rand.FRandRange(0.5f, 1.1f);
		B.Radius = Rand.FRandRange(40.f, 140.f);
		BugData.Add(B);
		Bugs->AddInstance(FTransform(FQuat::Identity, FVector(0, 0, -9000.f), FVector(0.001f)));   // hidden until a swarm arrives
	}
}

void AFireflies::StartSwarm()
{
	APawn* P = UGameplayStatics::GetPlayerPawn(this, 0);
	const FVector Pl = P ? P->GetActorLocation() : FVector::ZeroVector;
	for (int32 Try = 0; Try < 16; ++Try)
	{
		const float A = Rand.FRandRange(0.f, 2.f * PI), R = Rand.FRandRange(900.f, 2200.f);
		const FVector2D C(Pl.X + FMath::Cos(A) * R, Pl.Y + FMath::Sin(A) * R);
		if (FMath::Abs(C.X) > 9000.f || FMath::Abs(C.Y) > 9000.f) continue;
		if (JeebTerrain::WoodsDensity(C.X, C.Y) > 0.4f || JeebTerrain::NoGroundCover(C.X, C.Y)) continue;   // open meadow, not inside the woods or huts
		Centre = C;
		break;
	}
	Life = Rand.FRandRange(MinLife, MaxLife);
	Age = 0.f;
	bActive = true;
}

void AFireflies::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bActive)
	{
		Timer -= DeltaSeconds;
		if (Timer <= 0.f) StartSwarm();
		return;
	}
	Age += DeltaSeconds;
	const float Fade = FMath::Clamp(FMath::Min(Age / 6.f, (Life - Age) / 8.f), 0.f, 1.f);   // they drift in and out gently
	const float T = GetWorld()->GetTimeSeconds();
	const float Ground = JeebTerrain::HeightAt(Centre.X, Centre.Y);
	for (int32 i = 0; i < Count; ++i)
	{
		const FBug& B = BugData[i];
		const float A = T * B.Rate + B.Phase;
		const FVector Pos(Centre.X + B.Off.X + FMath::Cos(A) * B.Radius, Centre.Y + B.Off.Y + FMath::Sin(A * 1.3f) * B.Radius,
			Ground + B.Off.Z + 28.f * FMath::Sin(A * 0.9f + B.Phase));
		const float Blink = 0.2f + 0.8f * FMath::Pow(0.5f + 0.5f * FMath::Sin(T * B.Blink * 2.f + B.Phase * 3.f), 3.f);   // slow pulses of light
		const float Size = 0.055f * Fade * (0.35f + 0.65f * Blink);
		Bugs->UpdateInstanceTransform(i, FTransform(FQuat::Identity, Pos, FVector(FMath::Max(Size, 0.001f))), true, i == Count - 1, true);
	}
	if (Age >= Life)
	{
		bActive = false;
		Timer = Rand.FRandRange(MinGap, MaxGap);
		for (int32 i = 0; i < Count; ++i) Bugs->UpdateInstanceTransform(i, FTransform(FQuat::Identity, FVector(0, 0, -9000.f), FVector(0.001f)), true, i == Count - 1, true);
	}
}

#include "GrassField.h"
#include "Terrain.h"
#include "ChoppableTree.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AGrassField::AGrassField()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));

	Terrain = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Terrain"));
	Terrain->SetupAttachment(Root);
	Terrain->bUseComplexAsSimpleCollision = true;

	Grass = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Grass"));
	Grass->SetupAttachment(Root);
	Grass->SetStaticMesh(ConeMesh.Object);
	Grass->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Grass->SetCastShadow(false);                    // thousands of tiny blades do not need shadows
	Grass->bAffectDynamicIndirectLighting = false;
	Grass->bAffectDistanceFieldLighting = false;
}

void AGrassField::Tint(UPrimitiveComponent* Comp, const FLinearColor& Color)
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	for (int32 i = 0; i < Comp->GetNumMaterials(); ++i)
	{
		if (UMaterialInstanceDynamic* M = Comp->CreateDynamicMaterialInstance(i, Base))
		{
			M->SetVectorParameterValue(TEXT("Color"), Color);
		}
	}
}

void AGrassField::BuildTerrain()
{
	const float Half = 16000.f;
	const float Step = 200.f;
	const int32 N = FMath::RoundToInt(2.f * Half / Step); // cells per side

	TArray<FVector> Verts;
	TArray<FVector> Normals;
	TArray<FVector2D> UV;
	TArray<int32> Tris;
	TArray<int32> TrisBack;
	Verts.Reserve((N + 1) * (N + 1));
	Normals.Reserve((N + 1) * (N + 1));
	UV.Reserve((N + 1) * (N + 1));
	Tris.Reserve(N * N * 6);
	TrisBack.Reserve(N * N * 6);

	for (int32 j = 0; j <= N; ++j)
	{
		for (int32 i = 0; i <= N; ++i)
		{
			const float X = -Half + i * Step;
			const float Y = -Half + j * Step;
			const float H = JeebTerrain::HeightAt(X, Y);

			// Smooth shading: the slope comes from the height function itself
			const float DHdX = (JeebTerrain::HeightAt(X + Step, Y) - JeebTerrain::HeightAt(X - Step, Y)) / (2.f * Step);
			const float DHdY = (JeebTerrain::HeightAt(X, Y + Step) - JeebTerrain::HeightAt(X, Y - Step)) / (2.f * Step);

			Verts.Add(FVector(X, Y, H));
			Normals.Add(FVector(-DHdX, -DHdY, 1.f).GetSafeNormal());
			UV.Add(FVector2D(X / 1000.f, Y / 1000.f));
		}
	}

	for (int32 j = 0; j < N; ++j)
	{
		for (int32 i = 0; i < N; ++i)
		{
			const int32 A = j * (N + 1) + i;     // this corner
			const int32 B = A + 1;               // +X
			const int32 C = A + (N + 1);         // +Y
			const int32 D = C + 1;               // +X +Y
			Tris.Add(A); Tris.Add(C); Tris.Add(B);
			Tris.Add(B); Tris.Add(C); Tris.Add(D);
			// The same triangles wound the other way, so the ground shows from above whichever way is 'front'
			TrisBack.Add(A); TrisBack.Add(B); TrisBack.Add(C);
			TrisBack.Add(B); TrisBack.Add(D); TrisBack.Add(C);
		}
	}

	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	Terrain->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UV, Colors, Tangents, true);
	Terrain->CreateMeshSection_LinearColor(1, Verts, TrisBack, Normals, UV, Colors, Tangents, false);
	Terrain->SetCollisionProfileName(TEXT("BlockAll"));
	Terrain->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void AGrassField::BeginPlay()
{
	Super::BeginPlay();

	SetActorLocation(FVector::ZeroVector); // always sit at the world origin

	BuildTerrain();
	Tint(Terrain, FLinearColor(0.085f, 0.13f, 0.035f));   // a mid green under the tufts, so gaps between blades blend in instead of showing as dark lines
	Tint(Grass,   FLinearColor(0.30f, 0.34f, 0.06f));

	FRandomStream Rand(1337); // fixed seed = same meadow every run

	for (int32 i = 0; i < GrassCount; ++i)
	{
		const float Angle = Rand.FRandRange(0.f, 2.f * PI);
		const float Dist = FMath::Sqrt(Rand.FRand()) * FieldRadius;
		const float Height = Rand.FRandRange(25.f, 60.f);
		const float X = FMath::Cos(Angle) * Dist;
		const float Y = FMath::Sin(Angle) * Dist;
		if (FVector2D::Distance(FVector2D(X, Y), JeebTerrain::PondCenter()) < 620.f) continue; // no grass growing in the pond
		if (JeebTerrain::PathClearance(X, Y) < 60.f) continue; // keep the paths bare
		if (FVector2D::Distance(FVector2D(X, Y), JeebTerrain::CastlePos()) < JeebTerrain::CastleFlat) continue; // bare ground round the castle
		if (JeebTerrain::NoGroundCover(X, Y)) continue;   // none inside huts or stalls
		const FVector Pos(X, Y, JeebTerrain::HeightAt(X, Y) + Height * 0.5f - 3.f);
		const FVector Scale(0.15f, 0.15f, Height / 100.f);
		Grass->AddInstance(FTransform(FRotator(0.f, Rand.FRandRange(0.f, 360.f), 0.f), Pos, Scale));
	}
}

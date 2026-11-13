#include "Meadow.h"
#include "Terrain.h"
#include "GroundMask.h"
#include "Canopy.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

namespace
{
	constexpr float CellSize = 750.f;
	constexpr float Reach = 9500.f;          // grass grows this far around the player
	constexpr float FieldR = 9000.f;
	constexpr float NearDensity = 0.0012f;   // full tufts per square cm, close up
	constexpr float FarDensity = 0.0006f;    // thinner, cheaper tufts further out
	constexpr float NearDist = 1100.f;
	inline int32 TierFor(float Dist) { return Dist < 1500.f ? 0 : (Dist < 4000.f ? 1 : 2); }
	inline float TierDensity(int32 T) { return T == 0 ? 1.f : (T == 1 ? 0.55f : 0.3f); }
	inline float TierSize(int32 T) { return T == 0 ? 1.f : (T == 1 ? 1.25f : 1.6f); }

	// Where the meadow grows (2026-11-12): all over the world's land, so the original square, Oak Hill and the open world are one
	// meadow with no edge (before, only within FieldR of the start and in Oak Hill's rectangle, whose straight side showed).
	// Rock, mud, sand and water thin it out tuft by tuft (GroundMask.h). Frame-rate tests only (-JeebShot): -NoOakGrass leaves
	// out the town's part, -NoWorldGrass everything outside the start's circle and the town.
	bool GrowsHere(float X, float Y, float PadCm = 0.f)
	{
		if (X < JeebWorld::MinX - PadCm || X > JeebWorld::MaxX + PadCm || Y < JeebWorld::MinY - PadCm || Y > JeebWorld::MaxY + PadCm) return false;
		const float E = JeebOakHill::EastM(Y), N = JeebOakHill::NorthM(X), P = PadCm / 100.f;
		const bool bTown = JeebOakHill::Enabled() && E > -60.f - P && E < 86.f + P && N > -118.f - P && N < 120.f + P;   // (JeebOakHill::GrassArea's rectangle)
		static const bool bNoTown = JeebOakHill::TestOff(TEXT("NoOakGrass")), bNoWorld = JeebOakHill::TestOff(TEXT("NoWorldGrass"));
		if (bTown) return !bNoTown || FVector2D(X, Y).Size() <= FieldR + PadCm;
		return !bNoWorld || FVector2D(X, Y).Size() <= FieldR + PadCm;
	}
}

AMeadow::AMeadow()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

// A grass tuft as three crossed flat cards (6 triangles). The blades are painted on a see-through texture (M_Grass).
static UStaticMesh* MakeCardMesh(UObject* Outer, UMaterialInterface* Mat, float Width, float Height)
{
	UStaticMesh* SM = NewObject<UStaticMesh>(Outer);
	SM->GetStaticMaterials().Add(FStaticMaterial(Mat, FName("Grass")));

	FMeshDescription MD;
	FStaticMeshAttributes Attr(MD);
	Attr.Register();
	TVertexAttributesRef<FVector3f> Pos = Attr.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Nrm = Attr.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tan = Attr.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> Sign = Attr.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
	TPolygonGroupAttributesRef<FName> GroupSlot = Attr.GetPolygonGroupMaterialSlotNames();
	const FPolygonGroupID Group = MD.CreatePolygonGroup();
	GroupSlot[Group] = FName("Grass");

	auto Tri = [&](const FVector& A, const FVector& B, const FVector& C, const FVector2D& UA, const FVector2D& UB, const FVector2D& UC)
	{
		const FVector Pts[3] = { A, B, C };
		const FVector2D Uv[3] = { UA, UB, UC };
		TArray<FVertexInstanceID> Inst;
		for (int32 k = 0; k < 3; ++k)
		{
			const FVertexID V = MD.CreateVertex();
			Pos[V] = FVector3f(Pts[k]);
			const FVertexInstanceID I = MD.CreateVertexInstance(V);
			Nrm[I] = FVector3f(0.f, 0.f, 1.f);
			Tan[I] = FVector3f(1.f, 0.f, 0.f);
			Sign[I] = 1.f;
			UVs[I] = FVector2f(float(Uv[k].X), float(Uv[k].Y));
			Inst.Add(I);
		}
		MD.CreatePolygon(Group, Inst);
	};

	for (int32 c = 0; c < 3; ++c)
	{
		const float Yaw = c * PI / 3.f;
		const FVector Along(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.f);
		const FVector BL = -Along * (Width * 0.5f), BR = Along * (Width * 0.5f);
		const FVector Up(0.f, 0.f, Height);
		Tri(BL, BR, BR + Up, FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0));
		Tri(BL, BR + Up, BL + Up, FVector2D(0, 1), FVector2D(1, 0), FVector2D(0, 0));
	}

	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = true;
	TArray<const FMeshDescription*> Descs;
	Descs.Add(&MD);
	SM->BuildFromMeshDescriptions(Descs, Params);
	return SM;
}

// A bush as a dome of leafy cards (the leaf picture is painted on them by MakeGrassCards.bat)
static UStaticMesh* MakeBushMesh(UObject* Outer, UMaterialInterface* Mat)
{
	UStaticMesh* SM = NewObject<UStaticMesh>(Outer);
	SM->GetStaticMaterials().Add(FStaticMaterial(Mat, FName("Leaf")));

	FMeshDescription MD;
	FStaticMeshAttributes Attr(MD);
	Attr.Register();
	TVertexAttributesRef<FVector3f> Pos = Attr.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Nrm = Attr.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tan = Attr.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> Sign = Attr.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
	TPolygonGroupAttributesRef<FName> GroupSlot = Attr.GetPolygonGroupMaterialSlotNames();
	const FPolygonGroupID Group = MD.CreatePolygonGroup();
	GroupSlot[Group] = FName("Leaf");

	auto Tri = [&](const FVector& A, const FVector& B, const FVector& C, const FVector2D& UA, const FVector2D& UB, const FVector2D& UC, const FVector& N)
	{
		const FVector Pts[3] = { A, B, C };
		const FVector2D Uv[3] = { UA, UB, UC };
		TArray<FVertexInstanceID> Inst;
		for (int32 k = 0; k < 3; ++k)
		{
			const FVertexID V = MD.CreateVertex();
			Pos[V] = FVector3f(Pts[k]);
			const FVertexInstanceID I = MD.CreateVertexInstance(V);
			Nrm[I] = FVector3f(N);
			Tan[I] = FVector3f(1.f, 0.f, 0.f);
			Sign[I] = 1.f;
			UVs[I] = FVector2f(float(Uv[k].X), float(Uv[k].Y));
			Inst.Add(I);
		}
		MD.CreatePolygon(Group, Inst);
	};

	FRandomStream Rand(77);
	const int32 Cards = 30;
	for (int32 c = 0; c < Cards; ++c)
	{
		const float Yaw = (c + Rand.FRandRange(-0.4f, 0.4f)) / Cards * 2.f * PI * 2.f;      // wraps round twice, so cards fan out in all directions
		const FVector D(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.f);
		const FVector Side(-D.Y, D.X, 0.f);
		const float Lean = Rand.FRandRange(0.35f, 1.15f);                                      // radians from vertical
		const FVector Up = (D * FMath::Sin(Lean) + FVector(0.f, 0.f, FMath::Cos(Lean))).GetSafeNormal();
		const float Out = (c % 3 == 0) ? Rand.FRandRange(0.f, 12.f) : Rand.FRandRange(10.f, 42.f);
		const FVector Base = D * Out + FVector(0.f, 0.f, Rand.FRandRange(0.f, 28.f));
		const float W = Rand.FRandRange(85.f, 115.f), H = Rand.FRandRange(75.f, 105.f);
		const FVector N = (D * 0.55f + FVector(0.f, 0.f, 1.f)).GetSafeNormal();
		const FVector BL = Base - Side * (W * 0.5f), BR = Base + Side * (W * 0.5f);
		Tri(BL, BR, BR + Up * H, FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0), N);
		Tri(BL, BR + Up * H, BL + Up * H, FVector2D(0, 1), FVector2D(1, 0), FVector2D(0, 0), N);
	}

	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = true;
	TArray<const FMeshDescription*> Descs;
	Descs.Add(&MD);
	SM->BuildFromMeshDescriptions(Descs, Params);
	return SM;
}

void AMeadow::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(FVector::ZeroVector);

	UMaterialInterface* GrassMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Grass.M_Grass"));
	if (GrassMat)
	{
		NearMesh = MakeCardMesh(this, GrassMat, 75.f, 48.f);
		FarMesh = NearMesh;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("M_Grass is missing: run MakeGrassCards.bat once. No grass tufts will grow."));
	}
	PlaceBushesAndFlowers();

}

void AMeadow::Prewarm()
{
	// Grow the grass round the start right away (a moment longer to load, but nothing pops in when you first run off)
	if (NearMesh)
	{
		for (int32 j = -6; j <= 6; ++j)
			for (int32 i = -6; i <= 6; ++i)
			{
				const FVector2D Ctr((i + 0.5f) * CellSize, (j + 0.5f) * CellSize);
				const float D = Ctr.Size();
				if (D > 4200.f || D > FieldR) continue;
				StartJob(FIntPoint(i, j), TierFor(D));
				StepJob(1.0e6);
			}
	}
}

void AMeadow::DropCell(const FIntPoint& Id)
{
	if (FCell* C = Cells.Find(Id))
	{
		if (C->Comp) C->Comp->DestroyComponent();
		Cells.Remove(Id);
	}
}

void AMeadow::Tick(float Dt)
{
	Super::Tick(Dt);
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn || !NearMesh) return;
	if (!bPrewarmed) { bPrewarmed = true; Prewarm(); }   // first tick: the woods are planted by now, so grass can thin under them
	const FVector2D Me(Pawn->GetActorLocation().X, Pawn->GetActorLocation().Y);

	TArray<FIntPoint> Far;
	for (const TPair<FIntPoint, FCell>& P : Cells)
	{
		const FVector2D Ctr((P.Key.X + 0.5f) * CellSize, (P.Key.Y + 0.5f) * CellSize);
		if (FVector2D::Distance(Ctr, Me) > Reach + CellSize) Far.Add(P.Key);
	}
	for (const FIntPoint& Id : Far) DropCell(Id);

	// Build toward where you are heading: distances are measured from a point a second and a half ahead of you
	const FVector Vel = Pawn->GetVelocity();
	const FVector2D Ahead = Me + FVector2D(Vel.X, Vel.Y) * 1.5f;

	struct FWant { FIntPoint Id; int32 Tier; float D; };
	TArray<FWant> Want;
	const int32 Cx = FMath::FloorToInt(Me.X / CellSize), Cy = FMath::FloorToInt(Me.Y / CellSize);
	const int32 Span = FMath::CeilToInt(Reach / CellSize) + 1;
	for (int32 j = Cy - Span; j <= Cy + Span; ++j)
		for (int32 i = Cx - Span; i <= Cx + Span; ++i)
		{
			const FVector2D Ctr((i + 0.5f) * CellSize, (j + 0.5f) * CellSize);
			const float D = FVector2D::Distance(Ctr, Me);
			if (D > Reach || !GrowsHere(Ctr.X, Ctr.Y, CellSize * 0.75f)) continue;   // (the whole world's land, 2026-11-12)
			const FCell* Have = Cells.Find(FIntPoint(i, j));
			// Pick the detail from the nearer of now and a moment ahead, and only coarsen a square once it is well behind (no flicker at the boundary)
			const float DA = FMath::Min(D, float(FVector2D::Distance(Ctr, Ahead)));
			int32 T = TierFor(DA);
			if (Have && T > Have->Tier && TierFor(FMath::Max(D - 400.f, 0.f)) <= Have->Tier) T = Have->Tier;
			if (!Have || Have->Tier != T) Want.Add({ FIntPoint(i, j), T, DA + (Have ? 600.f : 0.f) });
		}
	Want.Sort([](const FWant& A, const FWant& B) { return A.D < B.D; });
	if (!bJob && Want.Num() > 0) StartJob(Want[0].Id, Want[0].Tier);
	if (bJob) StepJob(0.006);   // about 6 ms of grass work per frame
	if (!bJob && Want.Num() > 1) { StartJob(Want[1].Id, Want[1].Tier); StepJob(0.002); }   // a quick second square when there is time
}

void AMeadow::StartJob(const FIntPoint& Id, int32 Tier)
{
	bJob = true; JobId = Id; JobTier = Tier; JobDone = 0;
	JobRand.Initialize(int32(uint32(Id.X) * 73856093u ^ uint32(Id.Y) * 19349663u ^ uint32(Tier) * 83492791u));
	JobCount = FMath::RoundToInt(CellSize * CellSize * NearDensity * TierDensity(Tier));
	// Far from every road, no tuft in this square can be near one: skip the (slow) road check for each tuft
	const FVector2D Ctr((Id.X + 0.5f) * CellSize, (Id.Y + 0.5f) * CellSize);
	bJobNearRoad = JeebTerrain::PathClearance(Ctr.X, Ctr.Y) < CellSize * 0.75f + 200.f;
	// Near a building, each tuft asks for the doorway and drip-line wear itself: those patches are a metre or two across, finer
	// than the grid below, which used to blur them away so the grass stood untouched right up to doors and the well
	bJobExactWear = JeebOakHill::InBox(Ctr.X, Ctr.Y, CellSize);
	if (!bJobExactWear && FMath::Abs(Ctr.X) < 9800.f && FMath::Abs(Ctr.Y) < 9800.f)
		for (const JeebGround::FHutSpot& H : JeebGround::HutSpots())
			if (FVector2D::Distance(Ctr, H.Pos) < 900.f + CellSize) { bJobExactWear = true; break; }
	// the ground's mask on a 5 x 5 grid over the square (the roads are left out: each tuft checks those itself, below)
	for (int32 b = 0; b < 5; ++b)
		for (int32 a = 0; a < 5; ++a)
		{
			const float X = (Id.X + a * 0.25f) * CellSize, Y = (Id.Y + b * 0.25f) * CellSize;
			const JeebGround::FMask M = JeebGround::At(X, Y, false);
			JobAmount[b * 5 + a] = JeebGround::GrassAmount(M);
			JobWear[b * 5 + a] = JeebGround::BuildingWear(X, Y, bJobNearRoad ? JeebTerrain::PathClearance(X, Y) : 1.0e6f);
		}
	JobXf.Reset();
	JobXf.Reserve(JobCount);
}

void AMeadow::StepJob(double BudgetSeconds)
{
	const double End = FPlatformTime::Seconds() + BudgetSeconds;
	while (JobDone < JobCount)
	{
		if ((JobDone & 15) == 15 && FPlatformTime::Seconds() > End) return;   // (checked first, so a tuft skipped below never misses it; the next call picks up here)
		++JobDone;
		const float X = (JobId.X + JobRand.FRand()) * CellSize, Y = (JobId.Y + JobRand.FRand()) * CellSize;
		const float Sxy = JobRand.FRandRange(0.85f, 1.3f) * TierSize(JobTier);
		const float Yaw = JobRand.FRandRange(0.f, 360.f), Sz = JobRand.FRandRange(0.75f, 1.3f);
		float Amount, Wear;
		SampleJob(X, Y, Amount, Wear);
		const float Keep = JobRand.FRand();
		if (Keep >= Amount || !GrowsHere(X, Y)) continue;   // thinning out over rock, mud and sand
		const float Pc = bJobNearRoad ? JeebTerrain::PathClearance(X, Y) : 1.0e6f;
		if (bJobExactWear) Wear = JeebGround::BuildingWear(X, Y, Pc);
		if (Keep < Amount * (1.f - 0.5f * JeebWorld::Smooth(0.3f, 0.7f, Wear)))   // and over trampled ground: sparser and short at walls and doors, never cleared (bare ground in a building's shadow reads black)
		{
			if ((Pc >= -12.f || (Pc >= -40.f && JobRand.FRand() < (Pc + 40.f) / 28.f)) && !JeebTerrain::NoGroundCover(X, Y))   // a few tufts creep onto the edge of the dirt
			{
				// the grass grows right into the frayed edge of a path, short there and full height about a metre back
				float Short = FMath::Lerp(0.3f, 1.f, FMath::Clamp((Pc + 12.f) / 110.f, 0.f, 1.f));
				// under a tree the grass is short, so the fallen leaves show through it
				const float Cov = JeebCanopy::Cover(X, Y);
				Short *= 1.f - 0.72f * Cov;                                          // about ankle height under the crown
				const bool bThin = Cov > 0.f && JobRand.FRand() < 0.35f * Cov;     // and a little sparser
				// beside every building (huts, stalls, the castle, the village inside its palisade, the forts) the grass is a little shorter,
				// easing back up to full height over about 7 m so it never reads as tall-then-suddenly-short
				const float Near = JeebTerrain::BuildingNearness(X, Y);
				Short *= 1.f - 0.22f * Near;   // a bit shorter beside buildings (taller than before, so the crossed cards stand up)
				Short *= FMath::Lerp(0.55f, 1.f, Amount) * (1.f - 0.6f * JeebWorld::Smooth(0.1f, 0.6f, Wear));   // short where it thins out, and trampled by doors
				const float Wide = Sxy * FMath::Lerp(0.75f, 1.f, Short) * (1.f + 0.35f * Near);   // and wider there, so the tufts overlap into one mat and the individual cards disappear
				if (!bThin) JobXf.Add(FTransform(FRotator(0.f, Yaw, 0.f), FVector(X, Y, JeebTerrain::HeightAt(X, Y) + 4.f), FVector(Wide, Wide, Sz * (1.f + 0.1f * JobTier) * Short)));
			}
		}
	}
	FinishJob();
}

void AMeadow::SampleJob(float X, float Y, float& OutAmount, float& OutWear) const
{
	const float U = FMath::Clamp((X / CellSize - JobId.X) * 4.f, 0.f, 3.999f), V = FMath::Clamp((Y / CellSize - JobId.Y) * 4.f, 0.f, 3.999f);
	const int32 A = FMath::FloorToInt(U), B = FMath::FloorToInt(V);
	const float Fu = U - A, Fv = V - B;
	auto Bi = [&](const float* G) { return FMath::Lerp(FMath::Lerp(G[B * 5 + A], G[B * 5 + A + 1], Fu), FMath::Lerp(G[(B + 1) * 5 + A], G[(B + 1) * 5 + A + 1], Fu), Fv); };
	OutAmount = Bi(JobAmount);
	OutWear = Bi(JobWear);
}

void AMeadow::FinishJob()
{
	bJob = false;
	DropCell(JobId);
	FCell Cell; Cell.Tier = JobTier;
	if (JobXf.Num() > 0)
	{
		UInstancedStaticMeshComponent* Comp = NewObject<UInstancedStaticMeshComponent>(this);
		Comp->SetStaticMesh(JobTier == 0 ? NearMesh : FarMesh);
		Comp->SetupAttachment(RootComponent);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCastShadow(false);
		Comp->bAffectDynamicIndirectLighting = false;
		Comp->bAffectDistanceFieldLighting = false;
		Comp->SetCullDistances(0, 10000);
		Comp->RegisterComponent();
		Comp->AddInstances(JobXf, false, true);
		Cell.Comp = Comp;
	}
	Cells.Add(JobId, Cell);
	JobXf.Reset();
}

void AMeadow::PlaceBushesAndFlowers()
{
	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	UStaticMesh* Ball = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	auto MakeISM = [&](const FLinearColor& Color, bool bShadow)
	{
		UInstancedStaticMeshComponent* I = NewObject<UInstancedStaticMeshComponent>(this);
		I->SetStaticMesh(Ball);
		I->SetupAttachment(RootComponent);
		I->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		I->SetCastShadow(bShadow);
		I->bAffectDynamicIndirectLighting = false;
		I->bAffectDistanceFieldLighting = false;
		I->RegisterComponent();
		if (UMaterialInstanceDynamic* M = I->CreateDynamicMaterialInstance(0, BaseMat)) M->SetVectorParameterValue(TEXT("Color"), Color);
		return I;
	};
	UInstancedStaticMeshComponent* PetalA = MakeISM(FLinearColor(0.9f, 0.85f, 0.7f), false);
	UInstancedStaticMeshComponent* PetalB = MakeISM(FLinearColor(0.9f, 0.55f, 0.1f), false);

	FRandomStream Rand(2024);
	auto Spot = [&](float& X, float& Y)
	{
		const float A = Rand.FRandRange(0.f, 2.f * PI), D = FMath::Sqrt(Rand.FRand()) * FieldR;
		X = FMath::Cos(A) * D; Y = FMath::Sin(A) * D;
	};

	// bushes: leafy cards in a dome, lit and swaying like the grass
	UMaterialInterface* BushMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Bush.M_Bush"));
	if (!BushMat) UE_LOG(LogTemp, Warning, TEXT("M_Bush is missing: run MakeGrassCards.bat once. No bushes will grow."));
	UInstancedStaticMeshComponent* Bushes = nullptr;
	if (BushMat)
	{
		Bushes = NewObject<UInstancedStaticMeshComponent>(this);
		Bushes->SetStaticMesh(MakeBushMesh(this, BushMat));
		Bushes->SetupAttachment(RootComponent);
		Bushes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Bushes->SetCastShadow(false);
		Bushes->bAffectDynamicIndirectLighting = false;
		Bushes->bAffectDistanceFieldLighting = false;
		Bushes->SetCullDistances(0, 10000);
		Bushes->RegisterComponent();
	}
	const int32 BushTries = 960;
	for (int32 n = 0; n < BushTries && Bushes; ++n)
	{
		float X, Y; Spot(X, Y);
		if (JeebTerrain::PathClearance(X, Y) < 140.f || JeebTerrain::NoGroundCover(X, Y) || JeebTerrain::TreeBlocked(X, Y)) continue;
		const float Size = Rand.FRandRange(0.8f, 1.3f);
		Bushes->AddInstance(FTransform(FRotator(0.f, Rand.FRandRange(0.f, 360.f), 0.f), FVector(X, Y, JeebTerrain::HeightAt(X, Y) - 2.f), FVector(Size, Size, Size * Rand.FRandRange(0.85f, 1.15f))));
	}

	// flowers: 30% of the test plot's amount
	for (int32 n = 0; n < 1000; ++n)
	{
		float X, Y; Spot(X, Y);
		if (JeebTerrain::PathClearance(X, Y) < 60.f || JeebTerrain::NoGroundCover(X, Y)) continue;
		const FVector P(X, Y, JeebTerrain::HeightAt(X, Y) + Rand.FRandRange(14.f, 24.f));
		(Rand.FRand() < 0.5f ? PetalA : PetalB)->AddInstance(FTransform(FRotator::ZeroRotator, P, FVector(0.16f, 0.16f, 0.08f)));
	}
}

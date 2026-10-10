#include "MiningRock.h"
#include "GemMesh.h"
#include "Math/RotationMatrix.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	struct FRockMesh { TArray<FVector> V, N; TArray<int32> I; };

	float Hash3(const FVector& P, float Seed)
	{
		const float S = FMath::Sin(P.X * 12.9898f + P.Y * 78.233f + P.Z * 37.719f + Seed * 11.13f) * 43758.5453f;
		return S - FMath::FloorToFloat(S);
	}

	// A lumpy, faceted stone: a twice-subdivided icosahedron whose points are pushed in and out, flat-shaded.
	// The bottom is flattened so it sits on the ground at z = 0 around (Cx, Cy).
	void AppendRock(FRockMesh& M, float Cx, float Cy, FVector Radii, float Seed, float Rough, int32 Subdiv, float Flat)
	{
		const float T = (1.f + FMath::Sqrt(5.f)) * 0.5f;
		TArray<FVector> Pts = { {-1,T,0},{1,T,0},{-1,-T,0},{1,-T,0},{0,-1,T},{0,1,T},{0,-1,-T},{0,1,-T},{T,0,-1},{T,0,1},{-T,0,-1},{-T,0,1} };
		for (FVector& P : Pts) P.Normalize();
		TArray<int32> F = { 0,11,5, 0,5,1, 0,1,7, 0,7,10, 0,10,11, 1,5,9, 5,11,4, 11,10,2, 10,7,6, 7,1,8,
			3,9,4, 3,4,2, 3,2,6, 3,6,8, 3,8,9, 4,9,5, 2,4,11, 6,2,10, 8,6,7, 9,8,1 };
		for (int32 s = 0; s < Subdiv; ++s)
		{
			TMap<uint64, int32> Mid;
			auto MidPt = [&](int32 A, int32 B)
			{
				const uint64 Key = (uint64)FMath::Min(A, B) << 32 | (uint64)FMath::Max(A, B);
				if (const int32* Found = Mid.Find(Key)) return *Found;
				Pts.Add(((Pts[A] + Pts[B]) * 0.5f).GetSafeNormal());
				Mid.Add(Key, Pts.Num() - 1);
				return Pts.Num() - 1;
			};
			TArray<int32> NF;
			for (int32 i = 0; i < F.Num(); i += 3)
			{
				const int32 A = F[i], B = F[i + 1], C = F[i + 2];
				const int32 AB = MidPt(A, B), BC = MidPt(B, C), CA = MidPt(C, A);
				NF.Append({ A, AB, CA,  B, BC, AB,  C, CA, BC,  AB, BC, CA });
			}
			F = NF;
		}
		// Push every point in or out: coarse bumps plus per-point jitter
		TArray<FVector> World;
		const float Floor = Flat > 0.f ? -Flat * Radii.Z : -100000.f; // Flat = 0: no flattening, centred on the point
		for (const FVector& D : Pts)
		{
			const float Coarse = FMath::Sin(D.X * 3.1f + Seed) * FMath::Sin(D.Y * 2.7f + Seed * 1.7f) * 0.5f + FMath::Sin(D.Z * 4.3f + Seed * 0.6f) * 0.25f;
			const float Jit = Hash3(D, Seed) - 0.5f;
			const float Disp = 1.f + Rough * (Coarse * 0.9f + Jit * 1.2f);
			FVector P = FVector(D.X * Radii.X, D.Y * Radii.Y, D.Z * Radii.Z) * Disp;
			P.Z = FMath::Max(P.Z, Floor);
			P += FVector(Cx, Cy, Flat > 0.f ? -Floor - 2.f : 0.f);
			World.Add(P);
		}
		for (int32 i = 0; i < F.Num(); i += 3)
		{
			const FVector A = World[F[i]], B = World[F[i + 1]], C = World[F[i + 2]];
			FVector Nrm = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
			const FVector Centre = (A + B + C) / 3.f;
			if (FVector::DotProduct(Nrm, Centre - FVector(Cx, Cy, Radii.Z * 0.2f)) < 0.f) Nrm = -Nrm; // always point outward
			const int32 Base = M.V.Num();
			M.V.Add(A); M.V.Add(B); M.V.Add(C);
			M.N.Add(Nrm); M.N.Add(Nrm); M.N.Add(Nrm);
			// both windings, so it shows from outside whichever way the engine treats as front
			M.I.Append({ Base, Base + 1, Base + 2,  Base, Base + 2, Base + 1 });
		}
	}

	void Commit(UProceduralMeshComponent* C, const FRockMesh& M, bool bCollide)
	{
		C->CreateMeshSection_LinearColor(0, M.V, M.I, M.N, TArray<FVector2D>(), TArray<FLinearColor>(), TArray<FProcMeshTangent>(), bCollide);
	}
}

AMiningRock::AMiningRock()
{
	Skill = ESkillType::Mining;
	GatherSeconds = 2.f;
	RespawnSeconds = 300.f; // 5 minutes
	ItemsPerGather = 1;
	MaxCharges = 5;
	StartText = TEXT("You swing your pickaxe...");
	EmptyText = TEXT("This rock is empty. Give it a few minutes.");

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Main = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Main"));
	Main->SetupAttachment(Root);
	Main->bUseComplexAsSimpleCollision = true;

	// Chunky ore lumps sitting on the rock's surface (position, tilt)
	struct FLump { FVector Loc; FRotator Rot; };
	const FLump Lumps[4] = {
		{ FVector(40.f, -14.f, 60.f),  FRotator(10.f, 20.f, 0.f) },
		{ FVector(-28.f, -26.f, 64.f), FRotator(-15.f, 140.f, 10.f) },
		{ FVector(10.f, 26.f, 68.f),   FRotator(5.f, 250.f, -10.f) },
		{ FVector(-44.f, 20.f, 56.f),  FRotator(20.f, 300.f, 0.f) },
	};
	for (int32 i = 0; i < 4; ++i)
	{
		UProceduralMeshComponent* L = CreateDefaultSubobject<UProceduralMeshComponent>(*FString::Printf(TEXT("OreLump%d"), i));
		L->SetupAttachment(Root);
		L->SetRelativeLocationAndRotation(Lumps[i].Loc, Lumps[i].Rot);
		L->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ore.Add(L);
		OreBaseScale.Add(FVector(1.f));
	}

	// Diamond crystals: attached to the root (not the squashed body) so they keep their sharp shape,
	// and tilted to stick out of the rock's surface
	auto AddGem = [&](const TCHAR* Name, FVector Loc, FVector Dir, FVector Scale)
	{
		UProceduralMeshComponent* G = CreateDefaultSubobject<UProceduralMeshComponent>(Name);
		G->SetupAttachment(Root);
		G->SetRelativeLocation(Loc);
		G->SetRelativeRotation(FRotationMatrix::MakeFromZ(Dir.GetSafeNormal()).Rotator());
		G->SetRelativeScale3D(Scale);
		G->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		G->SetVisibility(false);
		return G;
	};
	GemA = AddGem(TEXT("GemA"), FVector(44.f, -10.f, 61.f),  FVector(0.52f, -0.16f, 0.80f), FVector(0.34f, 0.34f, 0.42f));
	GemB = AddGem(TEXT("GemB"), FVector(-30.f, -30.f, 62.f), FVector(-0.31f, -0.43f, 0.85f), FVector(0.30f, 0.30f, 0.38f));
	GemC = AddGem(TEXT("GemC"), FVector(5.f, 15.f, 71.f),    FVector(0.05f, 0.12f, 0.99f),  FVector(0.40f, 0.40f, 0.52f));
	GemScaleA = GemA->GetRelativeScale3D();
	GemScaleB = GemB->GetRelativeScale3D();
	GemScaleC = GemC->GetRelativeScale3D();
}

void AMiningRock::BeginPlay()
{
	Super::BeginPlay();

	// Build the stones. Each rock looks a little different (seeded by where it stands).
	const float Seed = FMath::Frac(GetActorLocation().X * 0.0137f + GetActorLocation().Y * 0.0291f) * 40.f;
	{
		FRockMesh M;
		AppendRock(M, 0.f, 0.f, FVector(64.f, 54.f, 46.f), Seed, 0.22f, 2, 0.72f);
		AppendRock(M, 52.f, 40.f, FVector(26.f, 22.f, 20.f), Seed + 3.f, 0.28f, 1, 0.6f);
		AppendRock(M, -58.f, 30.f, FVector(22.f, 19.f, 17.f), Seed + 7.f, 0.3f, 1, 0.6f);
		AppendRock(M, -8.f, -58.f, FVector(24.f, 20.f, 15.f), Seed + 11.f, 0.3f, 1, 0.6f);
		Commit(Main, M, true);
	}
	for (int32 i = 0; i < Ore.Num(); ++i)
	{
		FRockMesh M;
		AppendRock(M, 0.f, 0.f, FVector(20.f, 17.f, 15.f), Seed + 20.f + i * 5.f, 0.38f, 1, 0.0f);
		Commit(Ore[i], M, false);
	}

	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	auto Tint = [&](UPrimitiveComponent* C, const FLinearColor& Color) -> UMaterialInstanceDynamic*
	{
		UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, Base);
		if (M)
		{
			M->SetVectorParameterValue(TEXT("Color"), Color);
		}
		return M;
	};
	Tint(Main, FLinearColor(0.27f, 0.26f, 0.26f));
	for (UProceduralMeshComponent* L : Ore) OreMats.Add(Tint(L, FLinearColor(0.80f, 0.40f, 0.12f)));

	// Build the crystal shapes (a mesh section must exist before it can have a material)
	for (UProceduralMeshComponent* G : { GemA, GemB, GemC })
	{
		JeebGem::Build(G);
		GemMats.Add(Tint(G, FLinearColor(0.45f, 0.92f, 1.0f)));
	}

	RollOre();
	UpdateVisuals(MaxCharges, MaxCharges);
}

void AMiningRock::RollOre()
{
	const float Roll = FMath::FRand();
	bDiamond = Roll < 0.10f;                    // 10% diamond
	const bool bCoal = !bDiamond && Roll < 0.30f; // 20% coal, the other 70% is copper

	if (bDiamond)
	{
		ItemType = EItemType::Diamond;
		XPReward = 30;
		SuccessText = TEXT("You mine a sparkling diamond!");
	}
	else
	{
		ItemType = bCoal ? EItemType::Coal : EItemType::CopperOre;
		XPReward = bCoal ? 10 : 5;
		SuccessText = bCoal ? TEXT("You mine some coal.") : TEXT("You mine some copper ore.");

		const FLinearColor Color = bCoal ? FLinearColor(0.01f, 0.01f, 0.01f) : FLinearColor(0.80f, 0.40f, 0.12f);
		for (UMaterialInstanceDynamic* M : OreMats) if (M) M->SetVectorParameterValue(TEXT("Color"), Color);
	}
}

void AMiningRock::UpdateVisuals(int32 Remaining, int32 Max)
{
	// The chunks shrink with each mining and vanish when the rock is empty
	const float Fraction = Max > 0 ? float(Remaining) / float(Max) : 0.f;
	const bool bHasOre = Remaining > 0;

	for (int32 i = 0; i < Ore.Num(); ++i)
	{
		Ore[i]->SetRelativeScale3D(OreBaseScale[i] * Fraction);
		Ore[i]->SetVisibility(bHasOre && !bDiamond);
	}

	GemA->SetRelativeScale3D(GemScaleA * Fraction);
	GemB->SetRelativeScale3D(GemScaleB * Fraction);
	GemC->SetRelativeScale3D(GemScaleC * Fraction);
	GemA->SetVisibility(bHasOre && bDiamond);
	GemB->SetVisibility(bHasOre && bDiamond);
	GemC->SetVisibility(bHasOre && bDiamond);
}

void AMiningRock::OnChargesChanged(int32 Remaining, int32 Max)
{
	// A freshly refilled rock rolls new ore
	if (Remaining == Max)
	{
		RollOre();
	}
	UpdateVisuals(Remaining, Max);
}

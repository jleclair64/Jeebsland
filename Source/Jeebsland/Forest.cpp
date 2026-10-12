#include "Forest.h"
#include "Terrain.h"
#include "Canopy.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

namespace
{

	const FLinearColor LeafColors[5] = {
		FLinearColor(0.50f, 0.17f, 0.045f), // burnt orange
		FLinearColor(0.36f, 0.07f, 0.045f), // deep red
		FLinearColor(0.55f, 0.33f, 0.07f),  // amber
		FLinearColor(0.42f, 0.27f, 0.07f),  // gold-brown
		FLinearColor(0.17f, 0.22f, 0.07f),  // last green holdout
	};
}


// A leaf cluster: three crossed 100x100 cards, centred on the point, with the leaf picture painted on them
static UStaticMesh* MakeLeafClusterMesh(UObject* Outer, UMaterialInterface* Mat)
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
		const FVector BL = -Along * 50.f + FVector(0.f, 0.f, -50.f), BR = Along * 50.f + FVector(0.f, 0.f, -50.f);
		const FVector Up(0.f, 0.f, 100.f);
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


// A pine bough: two cards crossed along +X (0..100 = from where it joins the branch out to its tip), one lying flat, one
// standing on edge (narrower), so it reads as a bough from above, below and the side
static UStaticMesh* MakeNeedleCardMesh(UObject* Outer, UMaterialInterface* Mat)
{
	UStaticMesh* SM = NewObject<UStaticMesh>(Outer);
	SM->GetStaticMaterials().Add(FStaticMaterial(Mat, FName("Needles")));
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
	GroupSlot[Group] = FName("Needles");
	auto Quad = [&](const FVector& A, const FVector& B, const FVector& C, const FVector& D)   // A-B along the bough's near edge, D-C far edge
	{
		const FVector Pts[4] = { A, B, C, D };
		const FVector2D Uv[4] = { FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0), FVector2D(0, 0) };
		const int32 Tris[6] = { 0, 1, 2, 0, 2, 3 };
		for (int32 t = 0; t < 2; ++t)
		{
			TArray<FVertexInstanceID> Inst;
			for (int32 k = 0; k < 3; ++k)
			{
				const int32 i = Tris[t * 3 + k];
				const FVertexID V = MD.CreateVertex();
				Pos[V] = FVector3f(Pts[i]);
				const FVertexInstanceID I = MD.CreateVertexInstance(V);
				Nrm[I] = FVector3f(0.f, 0.f, 1.f);   // lit from above like the leaves (soft, even)
				Tan[I] = FVector3f(1.f, 0.f, 0.f);
				Sign[I] = 1.f;
				UVs[I] = FVector2f(float(Uv[i].X), float(Uv[i].Y));
				Inst.Add(I);
			}
			MD.CreatePolygon(Group, Inst);
		}
	};
	Quad(FVector(0, -50, 0), FVector(100, -50, 0), FVector(100, 50, 0), FVector(0, 50, 0));     // flat
	Quad(FVector(0, 0, -30), FVector(100, 0, -30), FVector(100, 0, 30), FVector(0, 0, 30));     // on edge
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = true;
	TArray<const FMeshDescription*> Descs;
	Descs.Add(&MD);
	SM->BuildFromMeshDescriptions(Descs, Params);
	return SM;
}

// ---------------------------------------------------------------- shaped trunks and branches
// Four trunk shapes, built as tapered tubes. Mesh units: height 0..100, and 100 = the trunk's base width.
static FVector2D TrunkCenter(int32 V, float t)
{
	switch (V)
	{
	case 1:  return FVector2D(70.f * FMath::Sin(t * PI * 0.85f), 20.f * FMath::Sin(t * 3.1f));      // bowed, leaning
	case 2:  return FVector2D(-22.f * t, 0.f);                                                      // forked: one stem that splits
	case 3:  return FVector2D(26.f * FMath::Sin(t * 3.f), 18.f * FMath::Cos(t * 2.3f) - 18.f);      // gnarled, wandering
	default: return FVector2D(0.f, 0.f);                                                            // straight with a flared foot
	}
}
static float TrunkRadius(int32 V, float t)
{
	float R = 50.f * (1.f - 0.55f * t) * (1.f - 0.5f * FMath::SmoothStep(0.7f, 1.f, t));   // narrows at the top into the leader branch
	const float FlareAmt = (V == 3) ? 34.f : (V == 0 ? 22.f : (V == 1 ? 16.f : 26.f));
	R += FlareAmt * FMath::Exp(-t * 9.f);
	const float Wob = (V == 3) ? 0.13f * FMath::Sin(t * 11.f + 1.f) : 0.05f * FMath::Sin(t * 7.f + V);
	R *= (1.f + Wob);
	if (V == 2) R *= 1.f - 0.28f * FMath::SmoothStep(0.30f, 0.40f, t);
	return R;
}
static const float ForkT = 0.36f;
static FVector ForkLimbPoint(float u)   // mesh units; the second limb of the forked trunk
{
	const FVector2D C0 = TrunkCenter(2, ForkT);
	return FVector(FMath::Lerp(float(C0.X), 170.f, u) + 15.f * FMath::Sin(u * PI), FMath::Lerp(float(C0.Y), 15.f, u), FMath::Lerp(ForkT * 100.f, 100.f, u));
}

struct FRing { FVector C; float R; };

// UTiles: how many times the bark picture wraps round the tube; VPer100: how many times it repeats per 100 units along it
static UStaticMesh* MakeTubeMesh(UObject* Outer, UMaterialInterface* Mat, const TArray<TArray<FRing>>& Tubes, int32 Sides, const FVector& Right, const FVector& Fwd, bool bCapEnd, float UTiles, float VPer100)
{
	UStaticMesh* SM = NewObject<UStaticMesh>(Outer);
	SM->GetStaticMaterials().Add(FStaticMaterial(Mat, FName("Bark")));

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
	GroupSlot[Group] = FName("Bark");

	struct FCorner { FVector P; FVector N; FVector T; FVector2D UV; };
	// Unreal's front face is the one where (B-A)x(C-A) points out, so wind each triangle to face the way its normals point
	auto Tri = [&](FCorner A, FCorner B, FCorner C)
	{
		if (FVector::CrossProduct(B.P - A.P, C.P - A.P).Dot(A.N + B.N + C.N) < 0.f) Swap(B, C);
		TArray<FVertexInstanceID> Inst;
		for (const FCorner* K : { &A, &B, &C })
		{
			const FVertexID V = MD.CreateVertex();
			Pos[V] = FVector3f(K->P);
			const FVertexInstanceID I = MD.CreateVertexInstance(V);
			Nrm[I] = FVector3f(K->N);
			Tan[I] = FVector3f(K->T);   // runs round the tube, always at right angles to the normal
			Sign[I] = 1.f;
			UVs[I] = FVector2f(K->UV);
			Inst.Add(I);
		}
		MD.CreatePolygon(Group, Inst);
	};

	for (const TArray<FRing>& Rings : Tubes)
	{
		// Distance along the tube's centre line, for the bark picture's V
		TArray<float> Along;
		Along.Add(0.f);
		for (int32 k = 1; k < Rings.Num(); ++k) Along.Add(Along[k - 1] + FVector::Distance(Rings[k].C, Rings[k - 1].C));

		auto Pt = [&](int32 k, int32 j) -> FCorner
		{
			const float A = 2.f * PI * (j % Sides) / Sides;
			FCorner Out;
			Out.N = (FMath::Cos(A) * Right + FMath::Sin(A) * Fwd).GetSafeNormal();
			Out.T = (-FMath::Sin(A) * Right + FMath::Cos(A) * Fwd).GetSafeNormal();
			Out.P = Rings[k].C + Out.N * Rings[k].R;
			Out.UV = FVector2D(UTiles * float(j) / Sides, Along[k] / 100.f * VPer100);   // j (not wrapped) so there is no seam smear
			return Out;
		};
		for (int32 k = 0; k + 1 < Rings.Num(); ++k)
		{
			for (int32 j = 0; j < Sides; ++j)
			{
				const FCorner C00 = Pt(k, j), C10 = Pt(k, j + 1), C01 = Pt(k + 1, j), C11 = Pt(k + 1, j + 1);
				Tri(C00, C10, C11);
				Tri(C00, C11, C01);
			}
		}
		if (bCapEnd && Rings.Num() > 1)
		{
			const int32 L = Rings.Num() - 1;
			const FVector Out = (Rings[L].C - Rings[L - 1].C).GetSafeNormal();
			const FVector T = FVector::CrossProduct(FMath::Abs(Out.Z) < 0.9f ? FVector::UpVector : Right, Out).GetSafeNormal();
			FCorner Mid{ Rings[L].C, Out, T, FVector2D(0.5f, 0.5f) };
			for (int32 j = 0; j < Sides; ++j)
			{
				FCorner Ca = Pt(L, j), Cb = Pt(L, j + 1);
				Ca.N = Cb.N = Out; Ca.T = Cb.T = T;
				Tri(Mid, Ca, Cb);
			}
		}
	}

	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = true;
	TArray<const FMeshDescription*> Descs;
	Descs.Add(&MD);
	SM->BuildFromMeshDescriptions(Descs, Params);
	return SM;
}
static UStaticMesh* MakeTrunkMesh(UObject* Outer, UMaterialInterface* Mat, int32 V)
{
	TArray<TArray<FRing>> Tubes;
	TArray<FRing> Main;
	const float Ts[8] = { 0.f, 0.07f, 0.16f, 0.28f, 0.42f, 0.6f, 0.8f, 1.f };
	for (float t : Ts)
	{
		const FVector2D C = TrunkCenter(V, t);
		Main.Add({ FVector(C.X, C.Y, t * 100.f), TrunkRadius(V, t) });
	}
	Tubes.Add(Main);
	if (V == 2)
	{
		TArray<FRing> Limb;
		const float R0 = TrunkRadius(2, ForkT) * 0.9f;
		for (int32 k = 0; k < 5; ++k)
		{
			const float u = k / 4.f;
			Limb.Add({ ForkLimbPoint(u), R0 * (1.f - 0.65f * u) });
		}
		Tubes.Add(Limb);
	}
	return MakeTubeMesh(Outer, Mat, Tubes, 20, FVector(1, 0, 0), FVector(0, 1, 0), true, 2.f, 5.5f);
}

// One piece of a branch: along X from -50 to +50 (centred, like the stick transforms expect), 100 wide at the base, 75 at the end
static UStaticMesh* MakeBranchMesh(UObject* Outer, UMaterialInterface* Mat)
{
	TArray<TArray<FRing>> Tubes;
	TArray<FRing> B;
	const float Ts[4] = { 0.f, 0.33f, 0.66f, 1.f };
	for (float t : Ts) B.Add({ FVector(t * 100.f - 50.f, 0.f, 0.f), 50.f * (1.f - 0.25f * t) });   // straight, gently tapered: limbs bend by joining several
	Tubes.Add(B);
	return MakeTubeMesh(Outer, Mat, Tubes, 6, FVector(0, 1, 0), FVector(0, 0, 1), true, 1.f, 8.f);
}

AForest::AForest()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> CylMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));

	Trunk    = MakeISM(TEXT("Trunk"),    CylMesh.Object,  true,  0,    0);
		RedTrunk = MakeISM(TEXT("RedTrunk"), CylMesh.Object,  true,  0,    0);
	for (int32 i = 0; i < 4; ++i) TrunkVar.Add(MakeISM(*FString::Printf(TEXT("TrunkV%d"), i), CylMesh.Object, true, 0, 0));
	for (int32 i = 0; i < 4; ++i) BranchVar.Add(MakeISM(*FString::Printf(TEXT("BranchV%d"), i), CubeMesh.Object, true, 3500, 4500, true));
	Branch   = MakeISM(TEXT("Branch"),   CubeMesh.Object, false, 3500, 4500);
	for (int32 i = 0; i < 5; ++i)
	{
		Leaf.Add(MakeISM(*FString::Printf(TEXT("Leaf%d"), i), CubeMesh.Object, true, 7000, 9000, true));   // canopies cast dappled shade on trunks and ground
		Leaf.Last()->ShadowCacheInvalidationBehavior = EShadowCacheInvalidationBehavior::Rigid;   // the gentle sway does not force shadows to redraw
		Leaf.Last()->SetNumCustomDataFloats(1);   // per cluster: how shaded it is inside the crown
	}
	// needle boughs (or cones if the needle material is missing); pines fade out a little further away than leafy crowns,
	// redwoods much further (they stand over the forest)
	Pine.Add(MakeISM(TEXT("PineA"), ConeMesh.Object, true, 9000, 12000, true));
	Pine.Add(MakeISM(TEXT("PineB"), ConeMesh.Object, true, 9000, 12000, true));
	RedCone  = MakeISM(TEXT("RedCone"), ConeMesh.Object, true, 20000, 25000, true);
	for (UInstancedStaticMeshComponent* C : { Pine[0], Pine[1], RedCone })
	{
		C->ShadowCacheInvalidationBehavior = EShadowCacheInvalidationBehavior::Rigid;
		C->SetNumCustomDataFloats(1);   // per bough: how deep in the crown it sits
	}
}

UInstancedStaticMeshComponent* AForest::MakeISM(const TCHAR* Name, UStaticMesh* Mesh, bool bShadow, int32 CullStart, int32 CullEnd, bool bHierarchical)
{
	// Hierarchical for the many small pieces (leaves, branches): culled in clumps, so only what is on screen is drawn
	UInstancedStaticMeshComponent* C = bHierarchical
		? CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(Name)
		: CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
	C->SetupAttachment(RootComponent);
	C->SetStaticMesh(Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCanEverAffectNavigation(false);
	C->SetCastShadow(bShadow);
	C->bAffectDynamicIndirectLighting = false; // keeps thousands of instances out of the expensive lighting passes
	C->bAffectDistanceFieldLighting = false;
	C->bUseAsOccluder = bShadow && !bHierarchical;   // only trunks and needle tiers hide things behind them
	if (CullEnd > 0)
	{
		C->SetCullDistances(CullStart, CullEnd);
	}
	return C;
}

void AForest::Tint(UInstancedStaticMeshComponent* C, const FLinearColor& Color)
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, Base))
	{
		M->SetVectorParameterValue(TEXT("Color"), Color);
	}
}

AChoppableTree* AForest::AddTree(ETreeKind Kind, const FVector& Pos, float S, FRandomStream& R)
{
	UWorld* World = GetWorld();
	if (!World) return nullptr;
	AChoppableTree* Tree = World->SpawnActor<AChoppableTree>(Pos, FRotator::ZeroRotator);
	if (!Tree) return nullptr;

	TArray<FTransform> TrunkT, BranchT, LeafT, ConeT;
	UInstancedStaticMeshComponent* TrunkComp = Trunk;
	UInstancedStaticMeshComponent* BranchComp = Branch;
	UInstancedStaticMeshComponent* LeafComp = nullptr;
	UInstancedStaticMeshComponent* ConeComp = nullptr;
	float Radius = 25.f, Height = 220.f;
	TArray<float> LeafShade;   // how deep in the crown each leaf cluster sits (darkens it)
	float CanopyR = 0.f;

	// A thin square stick between two points
	auto Stick = [](const FVector& A, const FVector& B, float Thick)
	{
		const FVector D = B - A;
		const float Len = FMath::Max(D.Size(), 1.f);
		const FQuat Q = FRotationMatrix::MakeFromX(D / Len).ToQuat();
		return FTransform(Q, (A + B) * 0.5f, FVector(Len / 100.f, Thick / 100.f, Thick / 100.f));
	};

	// Every tree is about 30% taller than before, and heights vary a lot from tree to tree
	const float HV = (Kind == ETreeKind::Redwood) ? 1.3f * R.FRandRange(0.9f, 1.1f) : 2.4f * R.FRandRange(0.75f, 1.3f);   // broad-leaf trees about 20% taller than before

	if (Kind == ETreeKind::Oak)
	{
		// ------- Broad leafy tree: trunk, 100 branches, a leaf cluster on the end of each twig -------
		const int32 LeafColor = R.RandRange(0, 4);
		LeafComp = Leaf[LeafColor];
		Height = 220.f * S * HV;
		Radius = 20.f * S;
				const int32 TrunkVariant = R.RandRange(0, 3);
		const float TrunkYaw = R.FRandRange(0.f, 360.f);
		const FQuat YawQ = FRotator(0.f, TrunkYaw, 0.f).Quaternion();
		const float Ds = 0.5f * S;   // world cm per mesh unit across the trunk
		if (bBark)
		{
			TrunkComp = TrunkVar[TrunkVariant];
			BranchComp = BranchVar[TrunkVariant];
			TrunkT.Add(FTransform(YawQ, Pos, FVector(Ds, Ds, Height / 100.f)));
			Radius = (TrunkVariant == 3 ? 28.f : 24.f) * S;
		}
		else
		{
			TrunkT.Add(FTransform(FQuat::Identity, Pos + FVector(0, 0, Height * 0.5f), FVector(0.5f * S, 0.5f * S, Height / 100.f)));
		}
		// Where the trunk's centre line is at fraction Tt of its height (bLimb: along the second limb of a forked trunk)
		auto AxisPoint = [&](float Tt, bool bLimb) -> FVector
		{
			if (!bBark) return Pos + FVector(0.f, 0.f, Height * Tt);
			FVector M;
			if (bLimb && TrunkVariant == 2) M = ForkLimbPoint(FMath::Clamp((Tt - ForkT) / (1.f - ForkT), 0.f, 1.f));
			else { const FVector2D C = TrunkCenter(TrunkVariant, Tt); M = FVector(C.X, C.Y, Tt * 100.f); }
			return Pos + YawQ.RotateVector(FVector(M.X * Ds, M.Y * Ds, 0.f)) + FVector(0.f, 0.f, M.Z * Height / 100.f);
		};

		// Small diamond leaves: thin square plates turned 45 degrees (only if the painted leaf material is missing)
		auto Diamonds = [&](const FVector& At)
		{
			for (int32 k = 0; k < 2; ++k)
			{
				const float W = R.FRandRange(26.f, 40.f) * S;
				const FQuat Q = FRotator(R.FRandRange(-40.f, 40.f), 45.f + R.FRandRange(-20.f, 20.f), R.FRandRange(-40.f, 40.f)).Quaternion();
				LeafT.Add(FTransform(Q, At + R.GetUnitVector() * (k == 0 ? 0.f : 22.f * S), FVector(W / 100.f, W / 100.f, 0.035f * S)));
			}
		};
		// A small painted cluster of leaves, turned any which way
		auto Leafy = [&](const FVector& At)
		{
			if (!bLeafCards) { Diamonds(At); return; }
			const float W = R.FRandRange(22.f, 32.f) * S;
			LeafT.Add(FTransform(FRotator(R.FRandRange(-90.f, 90.f), R.FRandRange(0.f, 360.f), R.FRandRange(-60.f, 60.f)).Quaternion(), At + R.GetUnitVector() * 8.f * S, FVector(W / 100.f)));
		};

		// ---- Crown shape: how high the limbs start, how steeply they rise, how long they reach, and whether they bend up or droop ----
		enum EShape { Round, Tall, Spreading, Weeping };
		const EShape Shape = EShape(R.RandRange(0, 3));
		// limbs start high on the trunk, so walking under a tree you pass trunks, not a thicket of low branches
		float StartLo = 0.52f, ElLo = 25.f, ElHi = 55.f, LenMul = 1.15f, Bend = 0.10f;   // Bend > 0 curls the limbs up, < 0 makes them droop
		switch (Shape)
		{
		case Tall:      StartLo = 0.45f; ElLo = 48.f; ElHi = 72.f; LenMul = 0.90f; Bend = 0.12f; break;
		case Spreading: StartLo = 0.50f; ElLo = 8.f;  ElHi = 32.f; LenMul = 1.60f; Bend = 0.16f; break;
		case Weeping:   StartLo = 0.58f; ElLo = 28.f; ElHi = 48.f; LenMul = 1.25f; Bend = -0.28f; break;
		default: break;
		}
		if (R.FRand() < 0.8f) StartLo = FMath::Min(StartLo * 1.25f, 0.8f);   // most trees: the lowest limbs another 25% up, clear of your head

		// A bending branch: Segs straight pieces, each turning a little (and curling up or drooping), tapering as it goes.
		// Calls Out(point, fraction along) at every joint, so side shoots and leaves can grow from it.
		auto Limb = [&](const FVector& From, FVector Dir, float Len, float Thick, int32 Segs, float BendBy, TFunctionRef<void(const FVector&, const FVector&, float, float)> Out)
		{
			FVector P = From;
			const float SegLen = Len / Segs;
			for (int32 s = 0; s < Segs; ++s)
			{
				const float K = float(s + 1) / Segs;
				const float ThickHere = Thick * FMath::Pow(1.f - 0.82f * float(s) / Segs, 1.3f);
				const FVector Next = P + Dir * SegLen;
				BranchT.Add(Stick(P - Dir * ThickHere * 0.3f, Next, ThickHere));   // overlaps the joint a touch, so bends never show a gap
				P = Next;
				Out(P, Dir, K, ThickHere);
				Dir = (Dir + FVector(0.f, 0.f, BendBy * (BendBy < 0.f ? K : 1.f)) + R.GetUnitVector() * 0.22f).GetSafeNormal();
			}
		};

		// Leaves along a limb: side shoots from each joint (more on longer limbs), each shoot dressed in leaf cards all along
		// its length (spaced evenly, so long shoots are as full as short ones) and a spray at its tip
		auto Grow = [&](float L1)
		{
			return [&, L1](const FVector& P, const FVector& D, float K, float Th)
			{
				if (K < 0.3f) return;   // bare near the trunk
				const int32 Shoots = (K >= 1.f ? 4 : 3) + (L1 > 400.f * S ? 1 : 0);
				for (int32 j = 0; j < Shoots; ++j)
				{
					const FVector D2 = (D * 0.5f + R.GetUnitVector() * 0.85f + FVector(0.f, 0.f, Bend >= 0.f ? 0.3f : -0.1f)).GetSafeNormal();
					const float L2 = L1 * 0.42f * R.FRandRange(0.75f, 1.2f);
					const int32 Along = FMath::Clamp(FMath::RoundToInt(L2 / (16.f * S)), 4, 12);
					Limb(P, D2, L2, FMath::Max(2.5f * S, Th * 0.42f), 2, Bend * 1.4f, [&, L2, Along](const FVector& Q, const FVector& QD, float K2, float)
					{
						for (int32 n = 0; n < Along / 2; ++n) Leafy(Q - QD * (L2 * 0.5f) * (float(n) / (Along / 2)) + R.GetUnitVector() * 20.f * S);   // all along this half of the shoot
						if (K2 >= 1.f) for (int32 n = 0; n < 6; ++n) Leafy(Q + R.GetUnitVector() * 36.f * S);                                          // a full spray at its tip
					});
				}
				for (int32 n = 0; n < 3; ++n) Leafy(P + R.GetUnitVector() * 26.f * S);
			};
		};

		const int32 NumLimbs = R.RandRange(9, 12);
		for (int32 i = 0; i < NumLimbs; ++i)
		{
			const float T = (i + 0.5f) / NumLimbs;
			const bool bLimbBranch = (TrunkVariant == 2) && (i % 3 == 1);
			const FVector Start = AxisPoint(FMath::Max(StartLo + (1.f - StartLo) * T, bLimbBranch ? ForkT + 0.1f : 0.f), bLimbBranch);
			const float Az = FMath::DegreesToRadians(i * 137.5f + R.FRandRange(-20.f, 20.f));
			const float El = FMath::DegreesToRadians(R.FRandRange(ElLo, ElHi));
			const FVector Dir(FMath::Cos(El) * FMath::Cos(Az), FMath::Cos(El) * FMath::Sin(Az), FMath::Sin(El));
			const float L1 = 300.f * S * LenMul * (1.1f - 0.45f * T) * R.FRandRange(0.85f, 1.15f);   // long limbs: a broad crown
			const float T1 = 18.f * S * (1.f - 0.45f * T);   // stout at the trunk, tapering hard toward the tips
			Limb(Start, Dir, L1, T1, 4, Bend, Grow(L1));
		}
		// The trunk carries on as a leader branch that grows up through the middle and crowns the tree, so the top is
		// leafy and rounded instead of ending in a stump (a forked trunk gets one on each limb)
		for (int32 k = 0; k < (TrunkVariant == 2 ? 2 : 1); ++k)
		{
			const FVector Start = AxisPoint(0.97f, k == 1);
			const FVector Dir = (FVector(R.FRandRange(-0.25f, 0.25f), R.FRandRange(-0.25f, 0.25f), 1.f)).GetSafeNormal();
			const float L1 = FMath::Max(Height * 0.32f, 160.f * S) * FMath::Min(LenMul, 1.2f);
			Limb(Start, Dir, L1, 11.f * S, 3, 0.f, Grow(L1));
		}
		// ---- Shade inside the crown: clusters deep inside and low down are darker, so it reads as one rounded mass ----
		if (LeafT.Num() > 0)
		{
			FVector Mid = FVector::ZeroVector;
			float ZLo = 1e9f, ZHi = -1e9f;
			for (const FTransform& X : LeafT) { Mid += X.GetLocation(); ZLo = FMath::Min(ZLo, float(X.GetLocation().Z)); ZHi = FMath::Max(ZHi, float(X.GetLocation().Z)); }
			Mid /= LeafT.Num();
			float Far = 1.f, Spread = 1.f;
			for (const FTransform& X : LeafT)
			{
				Far = FMath::Max(Far, float(FVector::Dist(X.GetLocation(), Mid)));
				Spread = FMath::Max(Spread, float(FVector::Dist2D(X.GetLocation(), Pos)));
			}
			for (const FTransform& X : LeafT)
			{
				const float Inner = 1.f - FVector::Dist(X.GetLocation(), Mid) / Far;
				const float Low = 1.f - (X.GetLocation().Z - ZLo) / FMath::Max(ZHi - ZLo, 1.f);
				LeafShade.Add(FMath::Clamp(0.42f * Inner + 0.22f * Low, 0.f, 0.55f));
			}
			CanopyR = Spread * 0.9f;
		}
	}
	else if (bNeedleCards && (Kind == ETreeKind::Pine || Kind == ETreeKind::Redwood))
	{
		// ------- Pines and redwoods: a straight trunk with whorls of drooping branches dressed in needle boughs, shorter
		// toward the top (a narrow point on a pine; on a redwood a crown that starts high up a long bare trunk) -------
		const bool bRed = (Kind == ETreeKind::Redwood);
		bool bFortTree = false;
		if (bRed)
			for (const JeebTerrain::FFort& F : JeebTerrain::Forts())
				if (FVector2D::Distance(F.Tree, FVector2D(Pos.X, Pos.Y)) < 1.f) bFortTree = true;
		if (bRed)
		{
			TrunkComp = RedTrunk;
			ConeComp = RedCone;
			Height = bFortTree ? JeebTerrain::FortTreeHeight() : 2100.f * S * HV;
			Radius = 85.f * S;
			TrunkT.Add(FTransform(FQuat::Identity, Pos + FVector(0, 0, Height * 0.5f), FVector(1.6f * S, 1.6f * S, Height / 100.f)));
			TrunkT.Add(FTransform(FQuat::Identity, Pos + FVector(0, 0, 110.f * S), FVector(2.4f * S, 2.4f * S, 220.f * S / 100.f)));   // base flare
		}
		else
		{
			ConeComp = Pine[R.RandRange(0, 1)];
			Height = 250.f * S * HV;
			Radius = 15.f * S;
			if (bBark)
			{
				TrunkComp = TrunkVar[0];
				TrunkT.Add(FTransform(FRotator(0.f, R.FRandRange(0.f, 360.f), 0.f).Quaternion(), Pos, FVector(0.3f * S, 0.3f * S, Height / 100.f)));
			}
			else TrunkT.Add(FTransform(FQuat::Identity, Pos + FVector(0, 0, Height * 0.5f), FVector(0.3f * S, 0.3f * S, Height / 100.f)));
		}
		// (a fort tree keeps no crown: its trunk ends under the top deck, which is open to the sky)
		if (bBark) BranchComp = BranchVar[bRed ? 3 : 2];   // bark-textured branches (reddish for redwoods, grey-brown for pines)
		const int32 Whorls = bFortTree ? 0 : (bRed ? R.RandRange(16, 19) : R.RandRange(11, 14));
		const float Lowest = bRed ? 0.42f : 0.16f;
		const float ReachBase = bRed ? 330.f * S : 160.f * S * FMath::Sqrt(HV);
		const float ReachTop = bRed ? 70.f * S : 28.f * S;
		const float CardLen = bRed ? 120.f * S : 70.f * S;
		TArray<float> Shade;
		auto Bough = [&](const FVector& At, const FVector& Dir, float Len, float Shd)
		{
			const FQuat Q = FRotationMatrix::MakeFromXZ(Dir, FVector(0.f, 0.f, 1.f)).ToQuat() * FRotator(0.f, 0.f, R.FRandRange(-25.f, 25.f)).Quaternion();
			ConeT.Add(FTransform(Q, At, FVector(Len / 100.f, Len * 0.75f / 100.f, Len * 0.75f / 100.f)));
			Shade.Add(Shd);
		};
		for (int32 w = 0; w < Whorls; ++w)
		{
			const float T = float(w) / FMath::Max(Whorls - 1, 1);
			const FVector Start = Pos + FVector(0.f, 0.f, FMath::Lerp(Lowest, 0.97f, T) * Height);
			const float Reach = FMath::Lerp(ReachBase, ReachTop, FMath::Pow(T, 0.85f));
			const int32 NB = T > 0.85f ? 3 : R.RandRange(4, 6);
			for (int32 b = 0; b < NB; ++b)
			{
				const float Az = FMath::DegreesToRadians(w * 47.f + b * 360.f / NB + R.FRandRange(-18.f, 18.f));
				const float El = FMath::DegreesToRadians(FMath::Lerp(-20.f, -6.f, T) + R.FRandRange(-6.f, 6.f));   // branches droop a little, less near the top
				FVector Dir(FMath::Cos(El) * FMath::Cos(Az), FMath::Cos(El) * FMath::Sin(Az), FMath::Sin(El));
				const float L = Reach * R.FRandRange(0.85f, 1.1f);
				const float Th = (bRed ? 16.f : 6.f) * S * (1.f - 0.7f * T) + 1.5f * S;
				// the branch: two pieces, the outer one drooping more
				const FVector Mid = Start + Dir * L * 0.55f;
				const FVector Dir2 = (Dir + FVector(0.f, 0.f, -0.25f)).GetSafeNormal();
				const FVector Tip = Mid + Dir2 * L * 0.45f;
				BranchT.Add(Stick(Start, Mid, Th));
				BranchT.Add(Stick(Mid - Dir2 * Th * 0.3f, Tip, Th * 0.6f));
				// needle boughs all along it, from a little way out to just past the tip (fanned a little)
				const float Cl = FMath::Clamp(L * 0.55f, CardLen * 0.45f, CardLen);
				const int32 N = FMath::Clamp(FMath::RoundToInt(L / (Cl * 0.45f)), 2, 7);
				for (int32 k = 0; k < N; ++k)
				{
					const float Fk = 0.15f + 0.85f * float(k) / N;
					const FVector P = Fk < 0.55f ? FMath::Lerp(Start, Mid, Fk / 0.55f) : FMath::Lerp(Mid, Tip, (Fk - 0.55f) / 0.45f);
					const FVector D = ((Fk < 0.55f ? Dir : Dir2) + FVector(R.FRandRange(-0.35f, 0.35f), R.FRandRange(-0.35f, 0.35f), R.FRandRange(-0.12f, 0.05f))).GetSafeNormal();
					Bough(P, D, Cl * R.FRandRange(0.8f, 1.1f), FMath::Clamp(0.38f * (1.f - Fk) + 0.22f * (1.f - T), 0.f, 0.55f));
				}
			}
		}
		// the leader: a tuft of short boughs pointing up at the very top
		if (Whorls > 0)
			for (int32 k = 0; k < 4; ++k)
				Bough(Pos + FVector(0.f, 0.f, Height * 0.97f), (FVector(R.FRandRange(-0.4f, 0.4f), R.FRandRange(-0.4f, 0.4f), 1.f)).GetSafeNormal(), CardLen * 0.55f, 0.05f);
		LeafShade = Shade;   // (handed to the needle boughs below)
	}
	else if (Kind == ETreeKind::Pine)
	{
		// ------- Pine: slim trunk with seven stacked cone tiers -------
		ConeComp = Pine[R.RandRange(0, 1)];
		Height = 250.f * S * HV;
		Radius = 15.f * S;
				if (bBark)
		{
			TrunkComp = TrunkVar[0];
			TrunkT.Add(FTransform(FRotator(0.f, R.FRandRange(0.f, 360.f), 0.f).Quaternion(), Pos, FVector(0.3f * S, 0.3f * S, Height / 100.f)));
		}
		else
		{
			TrunkT.Add(FTransform(FQuat::Identity, Pos + FVector(0, 0, Height * 0.5f), FVector(0.3f * S, 0.3f * S, Height / 100.f)));
		}
		const int32 Tiers = 7;
		for (int32 i = 0; i < Tiers; ++i)
		{
			const float T = float(i) / (Tiers - 1);
			const float W = FMath::Lerp(240.f, 70.f, T) * S;
			const float H = 120.f * S * FMath::Sqrt(HV);
			ConeT.Add(FTransform(FRotator(0.f, R.FRandRange(0.f, 360.f), 0.f).Quaternion(),
				Pos + FVector(0, 0, FMath::Lerp(60.f * S + H * 0.5f, Height + 40.f * S, T)),
				FVector(W / 100.f, W / 100.f, H / 100.f)));
		}
	}
	else
	{
		// ------- Redwood: enormous trunk with a flared base and a tall narrow crown way up high -------
		TrunkComp = RedTrunk;
		ConeComp = RedCone;
				bool bFortTree = false;
		for (const JeebTerrain::FFort& F : JeebTerrain::Forts())
			if (FVector2D::Distance(F.Tree, FVector2D(Pos.X, Pos.Y)) < 1.f) bFortTree = true;
		Height = bFortTree ? JeebTerrain::FortTreeHeight() : 2100.f * S * HV;
		Radius = 85.f * S;
		TrunkT.Add(FTransform(FQuat::Identity, Pos + FVector(0, 0, Height * 0.5f), FVector(1.6f * S, 1.6f * S, Height / 100.f)));
		TrunkT.Add(FTransform(FQuat::Identity, Pos + FVector(0, 0, 110.f * S), FVector(2.4f * S, 2.4f * S, 220.f * S / 100.f))); // base flare
		// A fort tree has no crown at all: its trunk ends under the top deck, which is open to the sky
		const int32 Tiers = bFortTree ? 0 : 16;
		for (int32 i = 0; i < Tiers; ++i)
		{
			const float T = float(i) / (Tiers - 1);
			const float W = FMath::Lerp(620.f, 140.f, T) * S;
			const float H = 380.f * S;
			const float Zc = FMath::Lerp(Height * 0.35f, Height + 200.f * S, T);
			ConeT.Add(FTransform(FRotator(0.f, R.FRandRange(0.f, 360.f), 0.f).Quaternion(),
				Pos + FVector(0, 0, Zc),
				FVector(W / 100.f, W / 100.f, H / 100.f)));
		}
	}

	// Hand the instances to the shared meshes and remember which ones belong to this tree
	auto Commit = [&](UInstancedStaticMeshComponent* C, TArray<FTransform>& T, bool bTrunk)
	{
		if (!C || T.Num() == 0) return;
		FTreePart P;
		P.Comp = C;
		P.Start = C->GetInstanceCount();
		P.Count = T.Num();
		P.bTrunk = bTrunk;
		C->AddInstances(T, false, false, false);
		Tree->Parts.Add(P);
	};
	Commit(TrunkComp, TrunkT, true);
	Commit(BranchComp, BranchT, false);
	Commit(LeafComp, LeafT, false);
	if (LeafComp && LeafShade.Num() == LeafT.Num() && LeafComp->NumCustomDataFloats > 0)
	{
		const int32 First = LeafComp->GetInstanceCount() - LeafT.Num();
		for (int32 i = 0; i < LeafShade.Num(); ++i) LeafComp->SetCustomDataValue(First + i, 0, LeafShade[i], false);
		LeafComp->MarkRenderStateDirty();
	}
	Commit(ConeComp, ConeT, false);
	if (bNeedleCards && ConeComp && LeafShade.Num() == ConeT.Num() && ConeT.Num() > 0 && ConeComp->NumCustomDataFloats > 0)
	{
		const int32 First = ConeComp->GetInstanceCount() - ConeT.Num();
		for (int32 i = 0; i < LeafShade.Num(); ++i) ConeComp->SetCustomDataValue(First + i, 0, LeafShade[i], false);
		ConeComp->MarkRenderStateDirty();
	}

	Tree->Setup(Kind, FMath::Max(Radius, 18.f), Height);
	if (Kind == ETreeKind::Oak)
	{
		// remember the canopy: thinner grass and a carpet of this tree's leaves underneath
		JeebCanopy::Add(FVector2D(Pos.X, Pos.Y), FMath::Clamp(CanopyR > 0.f ? CanopyR : Height * 0.5f, 160.f, 1100.f), LeafComp ? Leaf.IndexOfByKey(LeafComp) : 0);
	}
	return Tree;
}

void AForest::BeginPlay()
{
	Super::BeginPlay();
	JeebCanopy::Spots().Reset();   // fresh for each play session
	JeebCanopy::Grid().Reset();

	SetActorLocation(FVector::ZeroVector);

	Tint(Trunk,    FLinearColor(0.19f, 0.12f, 0.07f));
	Tint(RedTrunk, FLinearColor(0.29f, 0.12f, 0.06f));
	Tint(Branch,   FLinearColor(0.18f, 0.11f, 0.06f));
	UMaterialInterface* LeafMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_TreeLeaf.M_TreeLeaf"));
	if (LeafMat)
	{
		UStaticMesh* LeafMesh = MakeLeafClusterMesh(this, LeafMat);
		for (int32 i = 0; i < Leaf.Num(); ++i)
		{
			Leaf[i]->SetStaticMesh(LeafMesh);
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(LeafMat, this);
			M->SetVectorParameterValue(TEXT("Tint"), LeafColors[i]);   // the same five autumn colours as before
			Leaf[i]->SetMaterial(0, M);
		}
		bLeafCards = true;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("M_TreeLeaf is missing: run MakeGrassCards.bat once. Trees keep their old blocky leaves."));
		for (int32 i = 0; i < Leaf.Num(); ++i) Tint(Leaf[i], LeafColors[i]);
	}
	UMaterialInterface* NeedleMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_PineNeedle.M_PineNeedle"));
	if (NeedleMat)
	{
		// painted needle boughs, in three greens: two for pines, a bluer one for the redwoods
		UStaticMesh* NeedleMesh = MakeNeedleCardMesh(this, NeedleMat);
		const FLinearColor Greens[3] = { FLinearColor(0.10f, 0.26f, 0.12f), FLinearColor(0.13f, 0.31f, 0.14f), FLinearColor(0.10f, 0.27f, 0.19f) };
		UInstancedStaticMeshComponent* Comps[3] = { Pine[0], Pine[1], RedCone };
		for (int32 i = 0; i < 3; ++i)
		{
			Comps[i]->SetStaticMesh(NeedleMesh);
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(NeedleMat, this);
			M->SetVectorParameterValue(TEXT("Tint"), Greens[i]);
			Comps[i]->SetMaterial(0, M);
		}
		bNeedleCards = true;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("M_PineNeedle is missing: run Tools/BuildMaterials.ps1. Pines keep their old cones."));
		Tint(Pine[0],  FLinearColor(0.04f, 0.15f, 0.07f));
		Tint(Pine[1],  FLinearColor(0.06f, 0.19f, 0.09f));
		Tint(RedCone,  FLinearColor(0.05f, 0.17f, 0.12f));
	}

		UMaterialInterface* BarkMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_BarkTint.M_BarkTint"));
	if (BarkMat)
	{
		const FLinearColor BarkColors[4] = {
			FLinearColor(0.20f, 0.13f, 0.07f),    // brown
			FLinearColor(0.26f, 0.17f, 0.10f),    // warm brown
			FLinearColor(0.21f, 0.155f, 0.11f),  // grey-brown
			FLinearColor(0.26f, 0.14f, 0.07f) };  // reddish
		UStaticMesh* BranchMesh = MakeBranchMesh(this, BarkMat);
		for (int32 v = 0; v < 4; ++v)
		{
			TrunkVar[v]->SetStaticMesh(MakeTrunkMesh(this, BarkMat, v));
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BarkMat, this);
			M->SetVectorParameterValue(TEXT("Tint"), BarkColors[v]);
			TrunkVar[v]->SetMaterial(0, M);
			BranchVar[v]->SetStaticMesh(BranchMesh);
			BranchVar[v]->SetMaterial(0, M);   // the very same bark as the trunk it grows from
		}
		Branch->SetStaticMesh(BranchMesh);
		UMaterialInstanceDynamic* BM = UMaterialInstanceDynamic::Create(BarkMat, this);
		BM->SetVectorParameterValue(TEXT("Tint"), BarkColors[0]);
		Branch->SetMaterial(0, BM);
		bBark = true;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("M_BarkTint is missing: run MakeGrassCards.bat once. Trunks and branches keep their old shapes."));
	}

	FRandomStream R(777); // fixed seed = the same woods every run
	const FVector Cottage = JeebTerrain::CottagePos();
	int32 Planted = 0;

	// ---- Woods: a jittered grid, thick where the density map is high, empty in the valleys ----
	for (float GX = -FieldRadius; GX <= FieldRadius && Planted < MaxTrees; GX += CellSize)
	{
		for (float GY = -FieldRadius; GY <= FieldRadius && Planted < MaxTrees; GY += CellSize)
		{
			const float X = GX + R.FRandRange(0.f, CellSize);
			const float Y = GY + R.FRandRange(0.f, CellSize);
			const float Dist = FMath::Sqrt(X * X + Y * Y);
			if (Dist > FieldRadius || Dist < 1700.f) continue; // keep the start area clear
			if (JeebTerrain::TreeBlocked(X, Y)) continue;     // no trees on roads, in the village or on hut plots
			if (FVector2D::Distance(FVector2D(X, Y), FVector2D(Cottage.X, Cottage.Y)) < 1000.f) continue;
			bool bGrove = false;
			for (const FVector2D& G : JeebTerrain::RedwoodGroves())
				if (FVector2D::Distance(FVector2D(X, Y), G) < 3400.f) { bGrove = true; break; }
			if (bGrove) continue;
			if (FVector2D::Distance(FVector2D(X, Y), FVector2D(JeebTerrain::GraveyardPos().X, JeebTerrain::GraveyardPos().Y)) < 900.f) continue;

			const float P = FMath::Clamp((JeebTerrain::WoodsDensity(X, Y) - 0.40f) / 0.25f, 0.f, 1.f);
			if (R.FRand() > P) continue;

			const bool bPine = JeebTerrain::PineZone(X, Y) > 0.55f ? (R.FRand() < 0.85f) : (R.FRand() < 0.10f);
			const float S = 0.7f + 1.0f * FMath::Pow(R.FRand(), 1.7f); // mostly medium, a few giants
			AddTree(bPine ? ETreeKind::Pine : ETreeKind::Oak, FVector(X, Y, JeebTerrain::HeightAt(X, Y) - 20.f * S), S, R);
			++Planted;
		}
	}

	// ---- Redwood groves: a great tree in the middle of each (it carries a fort, see TreeFort.cpp), with others around it ----
	for (const JeebTerrain::FRedwoodSpot& Spot : JeebTerrain::RedwoodSpots())
	{
		AChoppableTree* Giant = AddTree(ETreeKind::Redwood, FVector(Spot.Pos.X, Spot.Pos.Y, JeebTerrain::HeightAt(Spot.Pos.X, Spot.Pos.Y) - 40.f), Spot.Scale, R);
		for (const JeebTerrain::FFort& F : JeebTerrain::Forts())
		{
			if (Giant && FVector2D::Distance(F.Tree, Spot.Pos) < 1.f) Giant->Lock(TEXT("This giant redwood holds up a tree fort. Best to leave it standing."));
		}
	}
}

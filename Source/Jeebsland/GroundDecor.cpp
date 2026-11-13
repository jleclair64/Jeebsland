#include "GroundDecor.h"
#include "Terrain.h"
#include "GameFramework/Actor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

namespace
{
	constexpr float CullCm = 7000.f;   // small pieces: gone past 70 m, where they are a pixel or two

	// Writes triangles (positions shared, one normal per position) into a static mesh with one material slot
	UStaticMesh* BuildMesh(UObject* Outer, UMaterialInterface* Mat, const TArray<FVector>& Pos, const TArray<FVector>& Nrm, const TArray<FVector2D>& UV, const TArray<int32>& Tri)
	{
		UStaticMesh* SM = NewObject<UStaticMesh>(Outer);
		SM->GetStaticMaterials().Add(FStaticMaterial(Mat, FName("Main")));
		FMeshDescription MD;
		FStaticMeshAttributes Attr(MD);
		Attr.Register();
		TVertexAttributesRef<FVector3f> P = Attr.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> N = Attr.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector3f> T = Attr.GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<float> S = Attr.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesRef<FVector2f> U = Attr.GetVertexInstanceUVs();
		TPolygonGroupAttributesRef<FName> Slot = Attr.GetPolygonGroupMaterialSlotNames();
		const FPolygonGroupID Group = MD.CreatePolygonGroup();
		Slot[Group] = FName("Main");
		TArray<FVertexID> Ids;
		for (const FVector& V : Pos) { const FVertexID Id = MD.CreateVertex(); P[Id] = FVector3f(V); Ids.Add(Id); }
		for (int32 t = 0; t + 2 < Tri.Num(); t += 3)
		{
			TArray<FVertexInstanceID> Inst;
			for (int32 k = 0; k < 3; ++k)
			{
				const int32 i = Tri[t + k];
				const FVertexInstanceID I = MD.CreateVertexInstance(Ids[i]);
				N[I] = FVector3f(Nrm[i]);
				const FVector Tg = FVector::CrossProduct(FVector::UpVector, Nrm[i]).SizeSquared() > 1e-4f ? FVector::CrossProduct(FVector::UpVector, Nrm[i]).GetSafeNormal() : FVector::ForwardVector;
				T[I] = FVector3f(Tg);
				S[I] = 1.f;
				U[I] = FVector2f(UV[i]);
				Inst.Add(I);
			}
			MD.CreatePolygon(Group, Inst);
		}
		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bBuildSimpleCollision = false;
		Params.bFastBuild = true;
		TArray<const FMeshDescription*> Descs;
		Descs.Add(&MD);
		SM->BuildFromMeshDescriptions(Descs, Params);
		return SM;
	}

	// A rounded, slightly lumpy field stone, about 100 cm across and 60 tall before scaling, its middle at the origin. Soft
	// (smooth normals, no facets), so it sits with the painted buildings rather than reading as a low-poly blob.
	UStaticMesh* MakeStoneMesh(UObject* Outer, UMaterialInterface* Mat, int32 Seed)
	{
		const int32 Rings = 9, Segs = 16;
		FRandomStream R(Seed);
		const FVector Off(R.FRandRange(-50.f, 50.f), R.FRandRange(-50.f, 50.f), R.FRandRange(-50.f, 50.f));
		const float Squash = R.FRandRange(0.52f, 0.68f), Long = R.FRandRange(1.0f, 1.3f);
		TArray<FVector> Pos; TArray<FVector2D> UV;
		auto Radius = [&](const FVector& D)
		{
			return 50.f * (1.f + 0.16f * FMath::PerlinNoise3D(D * 1.4f + Off) + 0.05f * FMath::PerlinNoise3D(D * 3.7f - Off));
		};
		auto Put = [&](const FVector& D)
		{
			const FVector V = D * Radius(D);
			Pos.Add(FVector(V.X * Long, V.Y, V.Z * Squash));
			UV.Add(FVector2D(0.f, 0.f));
		};
		Put(FVector::UpVector);
		for (int32 r = 1; r < Rings; ++r)
		{
			const float Th = PI * r / Rings;
			for (int32 s = 0; s < Segs; ++s)
			{
				const float Ph = 2.f * PI * s / Segs;
				Put(FVector(FMath::Sin(Th) * FMath::Cos(Ph), FMath::Sin(Th) * FMath::Sin(Ph), FMath::Cos(Th)));
			}
		}
		Put(-FVector::UpVector);
		const int32 Bottom = Pos.Num() - 1;
		auto RingV = [&](int32 r, int32 s) { return 1 + (r - 1) * Segs + (s % Segs); };
		TArray<int32> Tri;
		for (int32 s = 0; s < Segs; ++s) Tri.Append({ 0, RingV(1, s), RingV(1, s + 1) });
		for (int32 r = 1; r < Rings - 1; ++r)
			for (int32 s = 0; s < Segs; ++s)
			{
				const int32 A = RingV(r, s), B = RingV(r, s + 1), C = RingV(r + 1, s), D = RingV(r + 1, s + 1);
				Tri.Append({ A, C, B, B, C, D });
			}
		for (int32 s = 0; s < Segs; ++s) Tri.Append({ Bottom, RingV(Rings - 1, s + 1), RingV(Rings - 1, s) });
		// Wound as Unreal draws a front face (its cross product points INTO the stone, as the ground meshes found: GrassField),
		// then smooth normals from the faces round each corner
		TArray<FVector> Nrm; Nrm.Init(FVector::ZeroVector, Pos.Num());
		for (int32 t = 0; t + 2 < Tri.Num(); t += 3)
		{
			const FVector A = Pos[Tri[t]], B = Pos[Tri[t + 1]], C = Pos[Tri[t + 2]];
			FVector F = FVector::CrossProduct(B - A, C - A);
			if (FVector::DotProduct(F, (A + B + C) / 3.f) > 0.f) { Swap(Tri[t + 1], Tri[t + 2]); F = -F; }
			for (int32 k = 0; k < 3; ++k) Nrm[Tri[t + k]] -= F;   // (outward is minus the cross product)
		}
		for (FVector& N : Nrm) N = N.GetSafeNormal();
		return BuildMesh(Outer, Mat, Pos, Nrm, UV, Tri);
	}

	// A small leafy shrub: a low dome of leaf cards (the meadow's bush, smaller and rounder), about 100 cm across before scaling
	UStaticMesh* MakeShrubMesh(UObject* Outer, UMaterialInterface* Mat)
	{
		TArray<FVector> Pos, Nrm; TArray<FVector2D> UV; TArray<int32> Tri;
		FRandomStream R(913);
		const int32 Cards = 26;
		for (int32 c = 0; c < Cards; ++c)
		{
			const float Yaw = (c + R.FRandRange(-0.4f, 0.4f)) / Cards * 2.f * PI * 3.f;   // wraps round three times
			const FVector D(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.f), Side(-D.Y, D.X, 0.f);
			const float Lean = R.FRandRange(0.5f, 1.25f);
			const FVector Up = (D * FMath::Sin(Lean) + FVector(0.f, 0.f, FMath::Cos(Lean))).GetSafeNormal();
			const FVector Base = D * R.FRandRange(0.f, 22.f) + FVector(0.f, 0.f, R.FRandRange(0.f, 18.f));
			const float W = R.FRandRange(60.f, 80.f), H = R.FRandRange(50.f, 68.f);
			const FVector N = (D * 0.6f + FVector(0.f, 0.f, 1.f)).GetSafeNormal();
			const int32 First = Pos.Num();
			Pos.Append({ Base - Side * (W * 0.5f), Base + Side * (W * 0.5f), Base + Side * (W * 0.5f) + Up * H, Base - Side * (W * 0.5f) + Up * H });
			UV.Append({ FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0), FVector2D(0, 0) });
			Nrm.Append({ N, N, N, N });
			Tri.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
		}
		return BuildMesh(Outer, Mat, Pos, Nrm, UV, Tri);
	}

	UInstancedStaticMeshComponent* NewSet(AActor* Owner, UStaticMesh* Mesh, bool bShadow)
	{
		UInstancedStaticMeshComponent* I = NewObject<UInstancedStaticMeshComponent>(Owner);
		I->SetStaticMesh(Mesh);
		I->SetupAttachment(Owner->GetRootComponent());
		I->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		I->SetMobility(EComponentMobility::Static);
		I->SetCastShadow(bShadow);
		I->bAffectDynamicIndirectLighting = false;
		I->bAffectDistanceFieldLighting = false;
		I->SetCullDistances(0, CullCm);
		I->RegisterComponent();
		return I;
	}
	void Tint(UInstancedStaticMeshComponent* I, UMaterialInterface* Mat, const TCHAR* Param, const FLinearColor& C)
	{
		if (I && Mat) if (UMaterialInstanceDynamic* M = I->CreateDynamicMaterialInstance(0, Mat)) M->SetVectorParameterValue(Param, C);
	}
}

void JeebDecor::DressFoundations(AActor* Owner, const TArray<FFootprint>& Fps, int32 Seed)
{
	if (!Owner || Owner->IsNetMode(NM_DedicatedServer) || Fps.Num() == 0) return;
	UMaterialInterface* StoneMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Stone.M_Stone"));
	UMaterialInterface* BushMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Bush.M_Bush"));
	UMaterialInterface* Plain = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	UStaticMesh* Ball = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!StoneMat) UE_LOG(LogTemp, Warning, TEXT("JEEBDECOR M_Stone is missing (run Tools/BuildWorldMaterial.ps1): the stones are plain grey"));

	// Three stone shapes, each its own shade of the painted rock (warm, neutral, cool), shadows on (they are what sits them down)
	UInstancedStaticMeshComponent* Stones[3];
	const FLinearColor StoneTints[3] = { FLinearColor(1.06f, 1.0f, 0.92f), FLinearColor(0.95f, 0.95f, 0.95f), FLinearColor(0.9f, 0.94f, 1.0f) };
	for (int32 k = 0; k < 3; ++k)
	{
		Stones[k] = NewSet(Owner, MakeStoneMesh(Owner, StoneMat ? StoneMat : Plain, 7100 + k * 31), true);
		if (StoneMat) Tint(Stones[k], StoneMat, TEXT("Tint"), StoneTints[k]);
		else Tint(Stones[k], Plain, TEXT("Color"), FLinearColor(0.25f, 0.24f, 0.22f));
	}
	UInstancedStaticMeshComponent* Shrubs = BushMat ? NewSet(Owner, MakeShrubMesh(Owner, BushMat), false) : nullptr;
	UInstancedStaticMeshComponent* Petals[3] = { nullptr, nullptr, nullptr };
	const FLinearColor PetalCol[3] = { FLinearColor(0.9f, 0.85f, 0.7f), FLinearColor(0.85f, 0.62f, 0.12f), FLinearColor(0.62f, 0.42f, 0.75f) };   // cream, warm yellow, soft lilac
	if (Ball && Plain) for (int32 k = 0; k < 3; ++k) { Petals[k] = NewSet(Owner, Ball, false); Tint(Petals[k], Plain, TEXT("Color"), PetalCol[k]); }

	FRandomStream R(Seed);
	int32 NStones = 0, NShrubs = 0, NFlowers = 0;
	for (int32 f = 0; f < Fps.Num(); ++f)
	{
		const FFootprint& Fp = Fps[f];
		const FRotator Turn(0.f, Fp.Yaw, 0.f);
		const float LenX = 2.f * Fp.Half.X, LenY = 2.f * Fp.Half.Y;   // (the sides facing +-Y run along X, and the other way)
		const float Around = 2.f * (LenX + LenY);
		const int32 Clusters = FMath::Clamp(FMath::RoundToInt(Around / 380.f), 4, 16);
		for (int32 c = 0; c < Clusters; ++c)
		{
			// a spot along one wall (longer walls get more), a little out from it
			const float Pick = R.FRand() * Around, Along = R.FRandRange(-0.48f, 0.48f), Out = R.FRandRange(22.f, 48.f);
			const float Keep = R.FRand(), ShrubRoll = R.FRand(), FlowerRoll = R.FRand();
			const int32 NumStones = 1 + (R.FRand() < 0.7f ? 1 : 0) + (R.FRand() < 0.45f ? 1 : 0) + (R.FRand() < 0.2f ? 1 : 0);
			FVector2D Local, OutDir, AlongDir;
			if (Pick < LenX) { Local = FVector2D(Along * LenX, Fp.Half.Y + Out); OutDir = FVector2D(0.f, 1.f); AlongDir = FVector2D(1.f, 0.f); }
			else if (Pick < 2.f * LenX) { Local = FVector2D(Along * LenX, -Fp.Half.Y - Out); OutDir = FVector2D(0.f, -1.f); AlongDir = FVector2D(1.f, 0.f); }
			else if (Pick < 2.f * LenX + LenY) { Local = FVector2D(Fp.Half.X + Out, Along * LenY); OutDir = FVector2D(1.f, 0.f); AlongDir = FVector2D(0.f, 1.f); }
			else { Local = FVector2D(-Fp.Half.X - Out, Along * LenY); OutDir = FVector2D(-1.f, 0.f); AlongDir = FVector2D(0.f, 1.f); }
			const FVector OutW = Turn.RotateVector(FVector(OutDir, 0.f)), AlongW = Turn.RotateVector(FVector(AlongDir, 0.f));
			const FVector At3 = FVector(Fp.Centre, 0.f) + Turn.RotateVector(FVector(Local, 0.f));
			const FVector2D At(At3.X, At3.Y);
			if (Keep > 0.9f) continue;   // (a few spots left bare, so it never reads as a planted border)
			// never on the door's side (its wall faces within 50 degrees of the door), on a road, or in the gap to a neighbour
			if (Fp.bDoor && FMath::Abs(FMath::FindDeltaAngleDegrees(float(OutW.Rotation().Yaw), Fp.DoorYaw)) < 50.f) continue;
			if (JeebTerrain::PathClearance(At.X, At.Y) < 60.f) continue;
			bool bTight = false;
			for (int32 o = 0; o < Fps.Num() && !bTight; ++o)
			{
				if (o == f) continue;
				const FVector L = FRotator(0.f, -Fps[o].Yaw, 0.f).RotateVector(FVector(At - Fps[o].Centre, 0.f));
				if (JeebTerrain::RectSD(FVector2D(L.X, L.Y), FVector2D::ZeroVector, Fps[o].Half) < 160.f) bTight = true;
			}
			if (bTight) continue;
			// the stones: one to three, side by side along the wall, sunk a third into the ground
			for (int32 s = 0; s < NumStones; ++s)
			{
				const float Size = (s == 0 ? R.FRandRange(0.4f, 0.75f) : R.FRandRange(0.22f, 0.5f));   // (a big one and smaller ones, about 12-40 cm above the ground: smaller vanished in the grass)
				const FVector P = At3 + AlongW * ((s - 0.5f * (NumStones - 1)) * R.FRandRange(28.f, 42.f)) + OutW * R.FRandRange(-8.f, 14.f);
				const float Z = JeebTerrain::HeightAt(P.X, P.Y) + 8.f * Size;   // (about 60 cm tall at scale 1, its middle at the origin: its lower third under ground)
				const int32 Shape = R.RandRange(0, 2);
				Stones[Shape]->AddInstance(FTransform(FRotator(R.FRandRange(-8.f, 8.f), R.FRandRange(0.f, 360.f), R.FRandRange(-8.f, 8.f)), FVector(P.X, P.Y, Z), FVector(Size)), true);
				++NStones;
			}
			// a shrub beside them more often than not, sometimes with a few flowers
			if (Shrubs && ShrubRoll < 0.7f)
			{
				const float Size = R.FRandRange(0.55f, 0.85f);
				const FVector P = At3 + AlongW * R.FRandRange(-70.f, 70.f) + OutW * R.FRandRange(0.f, 15.f);
				const float Z = JeebTerrain::HeightAt(P.X, P.Y) - 3.f;
				Shrubs->AddInstance(FTransform(FRotator(0.f, R.FRandRange(0.f, 360.f), 0.f), FVector(P.X, P.Y, Z), FVector(Size, Size, Size * R.FRandRange(0.85f, 1.1f))), true);
				++NShrubs;
				if (FlowerRoll < 0.6f && Petals[0])
				{
					const int32 Col = R.RandRange(0, 2), Heads = R.RandRange(3, 5);
					for (int32 h = 0; h < Heads; ++h)
					{
						const float A = R.FRandRange(0.f, 2.f * PI), D = R.FRandRange(8.f, 26.f) * Size / 0.5f;
						const FVector Q(P.X + FMath::Cos(A) * D, P.Y + FMath::Sin(A) * D, Z + R.FRandRange(24.f, 34.f) * Size / 0.5f);
						Petals[Col]->AddInstance(FTransform(FRotator::ZeroRotator, Q, FVector(0.09f, 0.09f, 0.05f)), true);
						++NFlowers;
					}
				}
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBDECOR %d footprints: %d stones, %d shrubs, %d flower heads"), Fps.Num(), NStones, NShrubs, NFlowers);
}

#include "AutumnDecor.h"
#include "Scarecrow.h"
#include "Cottage.h"
#include "ChoppableTree.h"
#include "Terrain.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/BoxComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

namespace
{
	// The same five autumn colours as the tree leaves (see Forest.cpp); the last is the green holdout
	constexpr int32 NumLeafTypes = 5;
	const FLinearColor LeafPalette[NumLeafTypes] = {
		FLinearColor(0.50f, 0.17f, 0.045f), // burnt orange
		FLinearColor(0.36f, 0.07f, 0.045f), // deep red
		FLinearColor(0.55f, 0.33f, 0.07f),  // amber
		FLinearColor(0.42f, 0.27f, 0.07f),  // gold-brown
		FLinearColor(0.17f, 0.22f, 0.07f),  // last green holdout
	};
	constexpr int32 NumFallingLeaves = 15;
	constexpr int32 NumGustLeaves = 42;
	constexpr float GustDuration = 2.4f;
	constexpr float GustSpeed = 840.f;    // 40% slower than before
	constexpr float GustWidth = 800.f;   // about as wide as a hut
	constexpr int32 NumOpenLeaves = 1500;   // loose leaves scattered in the open
	constexpr int32 NumOpenPiles = 40;
	constexpr float LeafScale = 0.30f;      // flying leaves: 30 cm long
}

// One painted leaf on a flat card (100 x 100, lying in XY, long axis along X), the same painted style as the tree leaves
static UStaticMesh* MakeLeafCardMesh(UObject* Outer, UMaterialInterface* Mat)
{
	UStaticMesh* SM = NewObject<UStaticMesh>(Outer);
	SM->GetStaticMaterials().Add(FStaticMaterial(Mat, FName("Leaf"), FName("Leaf")));

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

	auto Corner = [&](float X, float Y)
	{
		const FVertexID V = MD.CreateVertex();
		Pos[V] = FVector3f(X, Y, 0.f);
		const FVertexInstanceID I = MD.CreateVertexInstance(V);
		Nrm[I] = FVector3f(0.f, 0.f, 1.f);
		Tan[I] = FVector3f(1.f, 0.f, 0.f);
		Sign[I] = 1.f;
		UVs[I] = FVector2f((Y + 50.f) / 100.f, 1.f - (X + 50.f) / 100.f);   // texture "up" runs along +X
		return I;
	};
	const FVertexInstanceID A = Corner(-50.f, -50.f), B = Corner(50.f, -50.f), C = Corner(50.f, 50.f), D = Corner(-50.f, 50.f);
	MD.CreatePolygon(Group, { A, B, C });
	MD.CreatePolygon(Group, { A, C, D });

	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = true;
	TArray<const FMeshDescription*> Descs;
	Descs.Add(&MD);
	SM->BuildFromMeshDescriptions(Descs, Params);
	return SM;
}

AAutumnDecor::AAutumnDecor()
{
	PrimaryActorTick.bCanEverTick = true;
	Rand.Initialize(2025);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> CylMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	PumpkinA = MakeISM(TEXT("PumpkinA"), SphereMesh.Object, FLinearColor(0.85f, 0.30f, 0.03f), true);
	PumpkinB = MakeISM(TEXT("PumpkinB"), SphereMesh.Object, FLinearColor(0.68f, 0.17f, 0.02f), true);
	PumpkinC = MakeISM(TEXT("PumpkinC"), SphereMesh.Object, FLinearColor(0.80f, 0.46f, 0.08f), true);
	Stem     = MakeISM(TEXT("Stem"),     CylMesh.Object,    FLinearColor(0.20f, 0.25f, 0.06f), false);
	Glow     = MakeISM(TEXT("Glow"),     CubeMesh.Object,   FLinearColor(3.2f, 2.0f, 0.45f), false);   // bright: lantern glass and carved faces glow
	GlowCone = MakeISM(TEXT("GlowCone"), ConeMesh.Object,   FLinearColor(3.2f, 2.0f, 0.45f), false);
	Wood     = MakeISM(TEXT("Wood"),     CylMesh.Object,    FLinearColor(0.16f, 0.09f, 0.05f), true);
	WoodCone = MakeISM(TEXT("WoodCone"), ConeMesh.Object,   FLinearColor(0.07f, 0.04f, 0.08f), true);
	Cloth    = MakeISM(TEXT("Cloth"),    CubeMesh.Object,   FLinearColor(0.35f, 0.12f, 0.12f), false);
	HayRoll  = MakeISM(TEXT("HayRoll"),  CylMesh.Object,    FLinearColor(0.78f, 0.58f, 0.15f), true);
	HayBox   = MakeISM(TEXT("HayBox"),   CubeMesh.Object,   FLinearColor(0.78f, 0.58f, 0.15f), false);
	StoneBox = MakeISM(TEXT("StoneBox"), CubeMesh.Object,   FLinearColor(0.22f, 0.23f, 0.27f), true);
	StoneCyl = MakeISM(TEXT("StoneCyl"), CylMesh.Object,    FLinearColor(0.22f, 0.23f, 0.27f), true);
	CandleBody = MakeISM(TEXT("CandleBody"), CylMesh.Object, FLinearColor(0.88f, 0.84f, 0.72f), false);
	CandleCup  = MakeISM(TEXT("CandleCup"),  CylMesh.Object, FLinearColor(0.50f, 0.33f, 0.07f), false);
	Flame    = MakeISM(TEXT("Flame"),    ConeMesh.Object,   FLinearColor(5.0f, 2.2f, 0.35f), false);   // bright: glows
	Rope     = MakeISM(TEXT("Rope"),     CylMesh.Object,    FLinearColor(0.20f, 0.14f, 0.07f), false);
	Iron     = MakeISM(TEXT("Iron"),     CylMesh.Object,    FLinearColor(0.07f, 0.07f, 0.08f), false);
	TorchFlame = MakeISM(TEXT("TorchFlame"), ConeMesh.Object, FLinearColor(5.0f, 1.7f, 0.25f), false);
	TorchCore  = MakeISM(TEXT("TorchCore"),  ConeMesh.Object, FLinearColor(7.0f, 4.5f, 1.2f), false);
	for (int32 i = 0; i < NumLeafTypes; ++i)
	{
		GroundLeaf.Add(MakeISM(*FString::Printf(TEXT("GroundLeaf%d"), i), CubeMesh.Object, LeafPalette[i], false, 4500, 6000));
		FallLeaf.Add(MakeISM(*FString::Printf(TEXT("FallLeaf%d"), i), CubeMesh.Object, LeafPalette[i], false));
		GustLeaf.Add(MakeISM(*FString::Printf(TEXT("GustLeaf%d"), i), CubeMesh.Object, LeafPalette[i], false));
	}
}

UInstancedStaticMeshComponent* AAutumnDecor::MakeISM(const TCHAR* Name, UStaticMesh* Mesh, const FLinearColor& Color, bool bShadow, int32 CullStart, int32 CullEnd)
{
	UInstancedStaticMeshComponent* C = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
	C->SetupAttachment(RootComponent);
	C->SetStaticMesh(Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCanEverAffectNavigation(false);
	C->SetCastShadow(bShadow);
	C->bAffectDynamicIndirectLighting = false;
	C->bAffectDistanceFieldLighting = false;
	if (CullEnd > 0) C->SetCullDistances(CullStart, CullEnd);
	Tints.Add(TPair<UInstancedStaticMeshComponent*, FLinearColor>(C, Color));
	return C;
}

// A warm point light that flickers (Tick varies its strength)
void AAutumnDecor::AddFlickerLight(const FVector& P, float Intensity, float Radius, const FLinearColor& Col, float Flicker)
{
	UPointLightComponent* L = NewObject<UPointLightComponent>(this);
	L->SetupAttachment(RootComponent);
	L->SetRelativeLocation(P);
	L->SetIntensity(Intensity);
	L->SetLightColor(Col);
	L->SetAttenuationRadius(Radius);
	L->SetCastShadows(false);   // cheap: no shadows from these
	L->RegisterComponent();
	FlickerLights.Add(L);
	FlickerBase.Add(Intensity);
	FlickerAmt.Add(Flicker);
	FlickerPhase.Add(Rand.FRandRange(0.f, 100.f));
}

static FTransform CylBetween(const FVector& A, const FVector& B, float Thick)
{
	const FVector D = B - A;
	const float Len = FMath::Max(D.Size(), 0.1f);
	return FTransform(FRotationMatrix::MakeFromZ(D / Len).ToQuat(), (A + B) * 0.5f, FVector(Thick / 100.f, Thick / 100.f, Len / 100.f));
}

// The burning end of a torch: an iron cup, a layered flame and its light
void AAutumnDecor::TorchHead(const FVector& Top)
{
	Iron->AddInstance(FTransform(FQuat::Identity, Top + FVector(0.f, 0.f, 4.f), FVector(0.13f, 0.13f, 0.12f)));
	Iron->AddInstance(FTransform(FQuat::Identity, Top + FVector(0.f, 0.f, 10.f), FVector(0.16f, 0.16f, 0.02f)));   // cup rim
	const FVector F = Top + FVector(0.f, 0.f, 27.f);
	TorchFlame->AddInstance(FTransform(FQuat::Identity, F, FVector(0.16f, 0.16f, 0.36f)));
	TorchCore->AddInstance(FTransform(FQuat::Identity, F - FVector(0.f, 0.f, 5.f), FVector(0.09f, 0.09f, 0.22f)));
	TorchFlamePos.Add(F);
	TorchPhase.Add(Rand.FRandRange(0.f, 100.f));
	AddFlickerLight(F + FVector(0.f, 0.f, 15.f), 4500.f, 1100.f, FLinearColor(1.0f, 0.55f, 0.22f), 0.22f);
}

// A torch in an iron bracket on a wall, leaning out from it
void AAutumnDecor::WallTorch(const FVector2D& WallPt, const FVector2D& Out, float OutOff, float Height)
{
	const float Z = JeebTerrain::HeightAt(WallPt.X, WallPt.Y) + Height;
	const FVector O(Out.X, Out.Y, 0.f);
	const FVector Wall(WallPt.X, WallPt.Y, Z);
	const FVector Base = Wall + O * OutOff;
	Iron->AddInstance(CylBetween(Wall + O * (OutOff - 30.f) - FVector(0.f, 0.f, 10.f), Base + O * 6.f, 3.f));      // bracket arm
	Iron->AddInstance(CylBetween(Base - FVector(0.f, 0.f, 6.f), Base + FVector(0.f, 0.f, 6.f), 9.f));              // ring
	const FVector Top = Base + (O * 0.45f + FVector::UpVector).GetSafeNormal() * 55.f;
	Wood->AddInstance(CylBetween(Base - (Top - Base).GetSafeNormal() * 14.f, Top, 7.f));
	TorchHead(Top);
}

// A torch on its own post
void AAutumnDecor::PostTorch(const FVector2D& P)
{
	const float Z = JeebTerrain::HeightAt(P.X, P.Y);
	Wood->AddInstance(FTransform(FQuat::Identity, FVector(P.X, P.Y, Z + 115.f), FVector(0.14f, 0.14f, 2.3f)));
	TorchHead(FVector(P.X, P.Y, Z + 230.f));
}

void AAutumnDecor::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(FVector::ZeroVector);

	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	for (const TPair<UInstancedStaticMeshComponent*, FLinearColor>& T : Tints)
	{
		if (UMaterialInstanceDynamic* M = T.Key->CreateDynamicMaterialInstance(0, Base))
		{
			M->SetVectorParameterValue(TEXT("Color"), T.Value);
		}
	}

	// Loose leaves use a painted leaf card (same look and tints as the tree leaves) instead of plain squares
	bool bLeafCards = false;
	if (UMaterialInterface* LeafMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_FlyLeaf.M_FlyLeaf")))
	{
		UStaticMesh* LeafMesh = MakeLeafCardMesh(this, LeafMat);
		for (int32 t = 0; t < NumLeafTypes; ++t)
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(LeafMat, this);
			M->SetVectorParameterValue(TEXT("Tint"), LeafPalette[t]);
			for (UInstancedStaticMeshComponent* C : { GroundLeaf[t], FallLeaf[t], GustLeaf[t] })
			{
				C->SetStaticMesh(LeafMesh);
				C->SetMaterial(0, M);
			}
		}
		bLeafCards = true;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("M_FlyLeaf is missing: run Tools/BuildMaterials.ps1. Loose leaves stay plain squares."));
	}
	LeafZ = bLeafCards ? 1.f : 0.012f;           // the card is flat already; the old cubes needed squashing
	LeafXY = bLeafCards ? LeafScale : 0.22f;

	// Collect transforms per mesh, then add them all in one go
	TMap<UInstancedStaticMeshComponent*, TArray<FTransform>> Batch;
	auto Put = [&](UInstancedStaticMeshComponent* C, const FTransform& T) { Batch.FindOrAdd(C).Add(T); };
	auto Ground = [](float X, float Y) { return JeebTerrain::HeightAt(X, Y); };
	const FVector Cottage = JeebTerrain::CottagePos();
	const FVector2D CottageXY(Cottage.X, Cottage.Y);

	// Invisible solid boxes so you can stand and jump on pumpkins and hay bales
	auto Solid = [&](const FVector& Center, const FVector& Half, const FQuat& Rot)
	{
		UBoxComponent* B = NewObject<UBoxComponent>(this);
		B->SetupAttachment(RootComponent);
		B->SetBoxExtent(Half);
		B->SetRelativeLocationAndRotation(Center, Rot);
		B->SetCollisionProfileName(TEXT("BlockAll"));
		B->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		B->RegisterComponent();
	};

	// ---------------- A pumpkin; face points along +X after the yaw, so carved ones look at the player ----------------
	auto AddPumpkin = [&](const FVector& Pos, float Scale, float YawDeg, bool bCarved)
	{
		const float D = 60.f * Scale; // body diameter
		if (JeebTerrain::PropBlocked(Pos.X, Pos.Y, D * 0.5f + 10.f)) return; // never on a fence or wall
		const FQuat Q = FRotator(0.f, YawDeg, 0.f).Quaternion();
		auto L = [&](FVector Off, FVector Size) { return FTransform(Q, Pos + Q.RotateVector(Off), Size / 100.f); };

		const int32 Kind = Rand.RandRange(0, 2);
		UInstancedStaticMeshComponent* Body = Kind == 0 ? PumpkinA : (Kind == 1 ? PumpkinB : PumpkinC);
		Put(Body, L(FVector(0, 0, 0.41f * D), FVector(D, D, 0.82f * D)));
		Solid(Pos + FVector(0, 0, 0.41f * D), FVector(0.3f * D, 0.3f * D, 0.35f * D), Q); // flat-topped stand-on shape
		Put(Stem, FTransform(FRotator(Rand.FRandRange(-12.f, 12.f), YawDeg, Rand.FRandRange(-12.f, 12.f)).Quaternion(),
			Pos + FVector(0, 0, 0.86f * D), FVector(0.12f * D, 0.12f * D, 0.2f * D) / 100.f));

		if (bCarved)
		{
			const float X = 0.485f * D;
			const float Zc = 0.41f * D;
			// Triangle eyes and nose (flat cones), a jagged grin of little blocks
			Put(GlowCone, L(FVector(X, -0.17f * D, Zc + 0.13f * D), FVector(0.06f * D, 0.16f * D, 0.15f * D)));
			Put(GlowCone, L(FVector(X,  0.17f * D, Zc + 0.13f * D), FVector(0.06f * D, 0.16f * D, 0.15f * D)));
			Put(GlowCone, L(FVector(X, 0.f, Zc + 0.01f * D),       FVector(0.06f * D, 0.08f * D, 0.09f * D)));
			for (int32 k = -2; k <= 2; ++k)
			{
				const float Hh = (k % 2 == 0) ? 0.11f * D : 0.06f * D;
				Put(Glow, L(FVector(X * 0.99f, k * 0.085f * D, Zc - 0.13f * D), FVector(0.06f * D, 0.075f * D, Hh)));
			}
		}
	};

	auto Facing = [&](const FVector2D& P) { return FRotator((-FVector(P.X, P.Y, 0.f)).Rotation()).Yaw + Rand.FRandRange(-70.f, 70.f); };

	// ---------------- Pumpkin patches ----------------
	auto Patch = [&](const FVector2D& C, int32 Count, float Radius, float SizeMul)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			const float Ang = Rand.FRandRange(0.f, 2.f * PI);
			const float Rad = Radius * FMath::Sqrt(Rand.FRand());
			const float X = C.X + Rad * FMath::Cos(Ang);
			const float Y = C.Y + Rad * FMath::Sin(Ang);
			AddPumpkin(FVector(X, Y, Ground(X, Y) - 2.f), SizeMul * Rand.FRandRange(0.6f, 1.5f), Facing(FVector2D(X, Y)), Rand.FRand() < 0.6f);
		}
	};

	// Beside the cottage door
	const FVector2D ToStart = (-CottageXY).GetSafeNormal();
	const FVector2D Side(-ToStart.Y, ToStart.X);
	Patch(CottageXY + ToStart * 650.f + Side * 380.f, 7, 200.f, 1.0f);
	Patch(CottageXY + ToStart * 650.f - Side * 380.f, 6, 200.f, 1.0f);
	Patch(CottageXY - ToStart * 600.f + Side * 200.f, 5, 220.f, 1.1f);

	// Out in the open meadows (found by looking for gaps in the woods)
	int32 Patches = 0;
	for (int32 Try = 0; Try < 400 && Patches < 10; ++Try)
	{
		const float Ang = Rand.FRandRange(0.f, 2.f * PI);
		const float Rad = Rand.FRandRange(1900.f, 8000.f);
		const FVector2D C(Rad * FMath::Cos(Ang), Rad * FMath::Sin(Ang));
		if (JeebTerrain::WoodsDensity(C.X, C.Y) > 0.40f) continue;
		if (JeebTerrain::TreeBlocked(C.X, C.Y)) continue; // not on roads or around the village
		if (FVector2D::Distance(C, CottageXY) < 1400.f) continue;
		Patch(C, Rand.RandRange(4, 8), 320.f, 1.0f);
		if (Patches < 2) // a couple of giants
		{
			AddPumpkin(FVector(C.X, C.Y, Ground(C.X, C.Y) - 4.f), 3.2f, Facing(C), true);
		}
		++Patches;
	}

	// A few pumpkins at every hut, beside the door
	for (const FVector2D& Site : JeebTerrain::HutSites())
	{
		const FVector2D Near = JeebTerrain::NearestPathPoint(Site.X, Site.Y);
		const FVector2D Fwd = (Near - Site).GetSafeNormal();
		const FVector2D Sd(-Fwd.Y, Fwd.X);
		Patch(Site + Fwd * 520.f + Sd * 300.f, 3, 90.f, 0.9f);
	}
	{
		const FVector2D VC = JeebTerrain::VillageCenter();
		Patch(VC + FVector2D(-1000.f, 520.f), 5, 130.f, 1.0f);
		Patch(VC + FVector2D(1000.f, 0.f), 4, 110.f, 0.9f);
	}

	// ---------------- Hay bales ----------------
	auto Bale = [&](const FVector& Pos, float YawDeg)
	{
		const FQuat Q = FRotator(0.f, YawDeg, 0.f).Quaternion() * FRotator(0.f, 0.f, 90.f).Quaternion(); // lying on its side
		Put(HayRoll, FTransform(Q, Pos + FVector(0, 0, 50.f), FVector(1.1f, 1.1f, 0.9f)));
		Solid(Pos + FVector(0, 0, 50.f), FVector(50.f, 45.f, 48.f), FRotator(0.f, Q.Rotator().Yaw, 0.f).Quaternion());
	};
	auto HayGroup = [&](const FVector2D& Wanted, float YawDeg)
	{
		const FQuat Yaw = FRotator(0.f, YawDeg, 0.f).Quaternion();
		const FVector Offs[4] = { FVector(0, -60, 0), FVector(0, 60, 0), FVector(0, 0, 90), FVector(170, 30, 0) };
		// If any bale would touch a fence or the wall, slide the whole group to the nearest clear spot
		FVector2D C = Wanted;
		bool bFound = false;
		for (int32 Ring = 0; Ring <= 10 && !bFound; ++Ring)
		{
			for (int32 k = 0; k < (Ring == 0 ? 1 : 8) && !bFound; ++k)
			{
				const float A = k * PI * 0.25f;
				const FVector2D Cand = Wanted + FVector2D(FMath::Cos(A), FMath::Sin(A)) * (Ring * 150.f);
				bool bOk = true;
				for (const FVector& O : Offs)
				{
					const FVector W = FVector(Cand.X, Cand.Y, 0.f) + Yaw.RotateVector(O);
					if (JeebTerrain::PropBlocked(W.X, W.Y, 115.f)) { bOk = false; break; }
				}
				if (bOk) { C = Cand; bFound = true; }
			}
		}
		if (!bFound) return;
		const float Z = Ground(C.X, C.Y);
		Bale(FVector(C.X, C.Y, Z) + Yaw.RotateVector(Offs[0]), YawDeg);
		Bale(FVector(C.X, C.Y, Z) + Yaw.RotateVector(Offs[1]), YawDeg);
		Bale(FVector(C.X, C.Y, Z) + Yaw.RotateVector(Offs[2]), YawDeg + 4.f);
		Bale(FVector(C.X, C.Y, Z) + Yaw.RotateVector(Offs[3]), YawDeg + 70.f);
	};
	HayGroup(CottageXY + Side * 650.f - ToStart * 100.f, 20.f);
	HayGroup(CottageXY - Side * 700.f - ToStart * 250.f, -30.f);
	HayGroup(FVector2D(-1500.f, 1800.f), 10.f);

	// ---------------- Scarecrows (pumpkin heads): fightable actors, see Scarecrow.cpp ----------------
	{
		const TArray<TPair<FVector2D, float>> Spots = { TPair<FVector2D, float>(FVector2D(1250.f, 1500.f), -130.f), TPair<FVector2D, float>(FVector2D(-1350.f, -1000.f), 60.f) };
		for (const TPair<FVector2D, float>& Sp : Spots)
		{
			if (JeebTerrain::PropBlocked(Sp.Key.X, Sp.Key.Y, 110.f)) continue;
			FActorSpawnParameters SP;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			GetWorld()->SpawnActor<AScarecrow>(AScarecrow::StaticClass(), FVector(Sp.Key.X, Sp.Key.Y, Ground(Sp.Key.X, Sp.Key.Y)), FRotator(0.f, Sp.Value, 0.f), SP);
		}
	}

	// ---------------- Lamp posts with a warm glow ----------------
	TArray<FVector> LampSpots;
	{
		const FVector2D Dir = CottageXY.GetSafeNormal();
		const FVector2D Perp(-Dir.Y, Dir.X);
		for (int32 i = 0; i < 4; ++i)
		{
			const float S = 450.f + i * 270.f;
			const FVector2D P = Dir * S + Perp * ((i % 2 == 0) ? 170.f : -170.f);
			LampSpots.Add(FVector(P.X, P.Y, 0.f));
		}
		LampSpots.Add(FVector(CottageXY.X + ToStart.X * 520.f + Side.X * 230.f, CottageXY.Y + ToStart.Y * 520.f + Side.Y * 230.f, 0.f));
		LampSpots.Add(FVector(CottageXY.X + ToStart.X * 520.f - Side.X * 230.f, CottageXY.Y + ToStart.Y * 520.f - Side.Y * 230.f, 0.f));
	}
	{
		const FVector2D VC = JeebTerrain::VillageCenter();
		for (int32 sx = -1; sx <= 1; sx += 2)
		{
			for (int32 sy = -1; sy <= 1; sy += 2)
			{
				LampSpots.Add(FVector(VC.X + sx * 270.f, VC.Y + sy * 270.f, 0.f)); // plaza corners
			}
		}
		LampSpots.Add(FVector(VC.X + JeebTerrain::VillageHalf() + 110.f, VC.Y + 260.f, 0.f)); // outside the gate
		LampSpots.Add(FVector(VC.X + JeebTerrain::VillageHalf() + 110.f, VC.Y - 260.f, 0.f));
	}
	for (const FVector& Spot : LampSpots)
	{
		const float Z = Ground(Spot.X, Spot.Y);
		Put(Wood, FTransform(FQuat::Identity, FVector(Spot.X, Spot.Y, Z + 125.f), FVector(0.09f, 0.09f, 2.5f)));
		Put(Glow, FTransform(FQuat::Identity, FVector(Spot.X, Spot.Y, Z + 262.f), FVector(0.26f, 0.26f, 0.34f)));
		Put(WoodCone, FTransform(FQuat::Identity, FVector(Spot.X, Spot.Y, Z + 296.f), FVector(0.4f, 0.4f, 0.25f)));

		AddFlickerLight(FVector(Spot.X, Spot.Y, Z + 250.f), 2400.f, 950.f, FLinearColor(1.0f, 0.68f, 0.32f), 0.07f);
	}

	// ---------------- Torches: on the village palisade and round the castle ----------------
	{
		const FVector2D VC = JeebTerrain::VillageCenter();
		const float VH = JeebTerrain::VillageHalf();
		struct FWall { FVector2D C; FVector2D Along; FVector2D Out; bool bGate; };
		const FWall Walls[4] = {
			{ VC + FVector2D(VH, 0.f),  FVector2D(0, 1), FVector2D(1, 0),  true },    // east, gate toward the start
			{ VC + FVector2D(0.f, VH),  FVector2D(1, 0), FVector2D(0, 1),  true },    // north, gate toward the graveyard
			{ VC + FVector2D(-VH, 0.f), FVector2D(0, 1), FVector2D(-1, 0), false },
			{ VC + FVector2D(0.f, -VH), FVector2D(1, 0), FVector2D(0, -1), false },
		};
		for (const FWall& W : Walls)
		{
			for (float U : { -1400.f, -700.f, 700.f, 1400.f }) WallTorch(W.C + W.Along * U, W.Out, 28.f, 215.f);
			if (W.bGate)
			{
				// both sides of the gate, outside and in
				for (float U : { -235.f, 235.f })
				{
					WallTorch(W.C + W.Along * U, W.Out, 52.f, 235.f);
					WallTorch(W.C + W.Along * U, -W.Out, 52.f, 235.f);
				}
			}
		}

		const FVector2D CP = JeebTerrain::CastlePos();
		const float Face = JeebTerrain::CastleWall + JeebTerrain::CastleWallT;
		const float Gate = JeebTerrain::CastleGateHalf;
		WallTorch(CP + FVector2D(-Face, Gate + 75.f), FVector2D(-1, 0), 4.f, 300.f);    // flanking the gate
		WallTorch(CP + FVector2D(-Face, -(Gate + 75.f)), FVector2D(-1, 0), 4.f, 300.f);
		WallTorch(CP + FVector2D(Face, 0.f), FVector2D(1, 0), 4.f, 300.f);               // the middle of each other wall
		WallTorch(CP + FVector2D(0.f, Face), FVector2D(0, 1), 4.f, 300.f);
		WallTorch(CP + FVector2D(0.f, -Face), FVector2D(0, -1), 4.f, 300.f);
		const float BridgeEnd = JeebTerrain::CastleMoatOut + 70.f;
		PostTorch(CP + FVector2D(-BridgeEnd, Gate + 45.f));                              // posts where the bridge meets the bank
		PostTorch(CP + FVector2D(-BridgeEnd, -(Gate + 45.f)));
	}

	// ---------------- Graveyard ----------------
	{
		const FVector G = JeebTerrain::GraveyardPos();
		const FQuat Face = FRotator(0.f, (-FVector(G.X, G.Y, 0.f)).Rotation().Yaw, 0.f).Quaternion(); // slabs face the start
		for (int32 row = -1; row <= 1; ++row)
		{
			for (int32 col = -2; col <= 2; ++col)
			{
				if (Rand.FRand() < 0.15f) continue;
				const FVector Local(row * 230.f + Rand.FRandRange(-30.f, 30.f), col * 150.f + Rand.FRandRange(-20.f, 20.f), 0.f);
				const FVector W = FVector(G.X, G.Y, 0.f) + Face.RotateVector(Local);
				if (JeebTerrain::PropBlocked(W.X, W.Y, 70.f)) continue;
				const float Z = Ground(W.X, W.Y);
				const FQuat Q = Face * FRotator(Rand.FRandRange(-4.f, 4.f), Rand.FRandRange(-10.f, 10.f), Rand.FRandRange(-7.f, 7.f)).Quaternion();
				const float Hgt = Rand.FRandRange(75.f, 100.f);
				if (Rand.FRand() < 0.2f)
				{
					// a cross
					Put(StoneBox, FTransform(Q, FVector(W.X, W.Y, Z + Hgt * 0.5f), FVector(0.14f, 0.14f, Hgt / 100.f * 1.25f)));
					Put(StoneBox, FTransform(Q, FVector(W.X, W.Y, Z + Hgt * 0.85f) , FVector(0.14f, 0.6f, 0.14f)));
				}
				else
				{
					// rounded headstone: a slab with a half-round top
					Put(StoneBox, FTransform(Q, FVector(W.X, W.Y, Z + Hgt * 0.5f), FVector(0.14f, 0.6f, Hgt / 100.f)));
					Put(StoneCyl, FTransform(Q * FRotator(90.f, 0.f, 0.f).Quaternion(), FVector(W.X, W.Y, Z + Hgt), FVector(0.6f, 0.6f, 0.14f)));
				}
			}
		}
		// a couple of pumpkins keep watch
		AddPumpkin(FVector(G.X + 90.f, G.Y + 420.f, Ground(G.X + 90.f, G.Y + 420.f) - 2.f), 1.2f, Facing(FVector2D(G.X, G.Y)), true);
		AddPumpkin(FVector(G.X - 90.f, G.Y - 420.f, Ground(G.X - 90.f, G.Y - 420.f) - 2.f), 1.0f, Facing(FVector2D(G.X, G.Y)), true);
	}

	// ---------------- Fallen leaves: mostly under the trees, a thin scatter in the open, and leaf piles ----------------
	auto PickLeaf = [&]() { return Rand.FRand() < 0.08f ? NumLeafTypes - 1 : Rand.RandRange(0, NumLeafTypes - 2); };
	auto LooseLeaf = [&](float X, float Y)
	{
		if (JeebTerrain::NoGroundCover(X, Y)) return;                                        // none indoors, in the moat or on the pond
		if (JeebTerrain::PathClearance(X, Y) < 40.f && Rand.FRand() < 0.6f) return;          // paths stay mostly swept
		const float Wd = Rand.FRandRange(22.f, 36.f) * (bLeafCards ? 1.15f : 1.f);
		const FQuat Q = FRotator(Rand.FRandRange(-10.f, 10.f), Rand.FRandRange(0.f, 360.f), Rand.FRandRange(-10.f, 10.f)).Quaternion();
		Put(GroundLeaf[PickLeaf()], FTransform(Q, FVector(X, Y, Ground(X, Y) + 2.f), FVector(Wd / 100.f, Wd / 100.f, LeafZ)));
	};
	// A mound of leaves: cards heaped in a dome, tilted more toward the edge
	auto LeafPile = [&](float CX, float CY, float Radius)
	{
		if (JeebTerrain::NoGroundCover(CX, CY) || JeebTerrain::PathClearance(CX, CY) < Radius + 40.f) return;
		const int32 Cards = FMath::RoundToInt(Radius * 0.6f);
		const int32 Dominant = PickLeaf();   // a pile is mostly one or two colours, like leaves from one tree
		for (int32 k = 0; k < Cards; ++k)
		{
			const float A = Rand.FRandRange(0.f, 2.f * PI);
			const float Fr = FMath::Sqrt(Rand.FRand());                               // 0 at the middle, 1 at the rim
			const float X = CX + Radius * Fr * FMath::Cos(A), Y = CY + Radius * Fr * FMath::Sin(A);
			const float Hgt = Radius * 0.32f * (1.f - Fr * Fr);
			const float Wd = Rand.FRandRange(26.f, 42.f) * (bLeafCards ? 1.15f : 1.f);
			const float Tilt = 12.f + 38.f * Fr;
			const FQuat Q = FRotator(Rand.FRandRange(-Tilt, Tilt), Rand.FRandRange(0.f, 360.f), Rand.FRandRange(-Tilt, Tilt)).Quaternion();
			const int32 Type = Rand.FRand() < 0.7f ? Dominant : PickLeaf();
			Put(GroundLeaf[Type], FTransform(Q, FVector(X, Y, Ground(X, Y) + 2.f + Hgt + Rand.FRandRange(0.f, 3.f)), FVector(Wd / 100.f, Wd / 100.f, LeafZ)));
		}
	};

	// Under every broad-leaf tree: a carpet about as wide as the canopy, thinning toward the edge and drifting a little downwind
	for (TActorIterator<AChoppableTree> It(GetWorld()); It; ++It)
	{
		if (It->GetKind() != ETreeKind::Oak) continue;
		const FVector T = It->GetActorLocation();
		const float R = FMath::Clamp(It->GetTreeHeight() * 0.5f, 160.f, 520.f);       // canopy radius, from the tree's height
		const int32 Count = FMath::RoundToInt(PI * R * R / 3500.f * Rand.FRandRange(0.7f, 1.3f));   // about one leaf per 60 x 60 cm
		for (int32 i = 0; i < Count; ++i)
		{
			const float A = Rand.FRandRange(0.f, 2.f * PI);
			const float Rad = R * 1.1f * FMath::Pow(Rand.FRand(), 0.65f);               // denser near the trunk
			if (Rad < 35.f) continue;                                                   // not inside the trunk
			LooseLeaf(T.X + Rad * FMath::Cos(A) + 0.12f * R, T.Y + Rad * FMath::Sin(A) + 0.05f * R);
		}
		if (Rand.FRand() < 0.30f)
		{
			const float A = Rand.FRandRange(0.f, 2.f * PI), Rad = R * Rand.FRandRange(0.3f, 0.85f);
			LeafPile(T.X + Rad * FMath::Cos(A), T.Y + Rad * FMath::Sin(A), Rand.FRandRange(28.f, 60.f));
		}
	}
	// A thin scatter across the open land, and a few piles out in the meadows
	for (int32 i = 0; i < NumOpenLeaves; ++i)
	{
		const float Ang = Rand.FRandRange(0.f, 2.f * PI);
		const float Rad = 600.f + 8600.f * FMath::Sqrt(Rand.FRand());
		LooseLeaf(Rad * FMath::Cos(Ang), Rad * FMath::Sin(Ang));
	}
	for (int32 i = 0; i < NumOpenPiles; ++i)
	{
		const float Ang = Rand.FRandRange(0.f, 2.f * PI);
		const float Rad = 900.f + 7000.f * FMath::Sqrt(Rand.FRand());
		LeafPile(Rad * FMath::Cos(Ang), Rad * FMath::Sin(Ang), Rand.FRandRange(30.f, 65.f));
	}

	for (TPair<UInstancedStaticMeshComponent*, TArray<FTransform>>& Pair : Batch)
	{
		Pair.Key->AddInstances(Pair.Value, false, false, false);
	}

	// ---------------- Candle string lights in the village: poles around the plaza and in front of each hut, cords swagged between them ----------------
	{
		TMap<UInstancedStaticMeshComponent*, TArray<FTransform>> Strings;
		auto Add = [&](UInstancedStaticMeshComponent* C, const FTransform& T) { Strings.FindOrAdd(C).Add(T); };
		auto Cyl = [&](UInstancedStaticMeshComponent* C, const FVector& A, const FVector& B, float Thick)   // a thin cylinder from A to B
		{
			const FVector D = B - A;
			const float Len = FMath::Max(D.Size(), 0.1f);
			Add(C, FTransform(FRotationMatrix::MakeFromZ(D / Len).ToQuat(), (A + B) * 0.5f, FVector(Thick / 100.f, Thick / 100.f, Len / 100.f)));
		};

		// A pole: wood post with a little cap, returns the point the cord ties on to
		auto Pole = [&](float X, float Y, float Height) -> FVector
		{
			const float Z = Ground(X, Y);
			Add(Wood, FTransform(FQuat::Identity, FVector(X, Y, Z + Height * 0.5f), FVector(0.12f, 0.12f, Height / 100.f)));
			Add(WoodCone, FTransform(FQuat::Identity, FVector(X, Y, Z + Height + 12.f), FVector(0.2f, 0.2f, 0.26f)));
			return FVector(X, Y, Z + Height);
		};

		// One candle hanging from the cord at P: two fine wires to a brass cup, a wax candle with a melted rim, and a flame
		auto Candle = [&](const FVector& P, const FVector& AlongDir)
		{
			const FVector Dh = FVector(AlongDir.X, AlongDir.Y, 0.f).GetSafeNormal();
			const float BaseZ = P.Z - 34.f - 6.f;                                  // bottom of the cup
			const float H = Rand.FRandRange(10.f, 15.f);                           // candle height
			const FVector Rim = FVector(P.X, P.Y, BaseZ + 6.f);
			Cyl(Rope, P, Rim + Dh * 5.f, 0.5f);                                    // wires from the cord down to the cup's rim
			Cyl(Rope, P, Rim - Dh * 5.f, 0.5f);
			Add(CandleCup,  FTransform(FQuat::Identity, FVector(P.X, P.Y, BaseZ + 3.f), FVector(0.08f, 0.08f, 0.06f)));
			Add(CandleCup,  FTransform(FQuat::Identity, FVector(P.X, P.Y, BaseZ + 6.f), FVector(0.105f, 0.105f, 0.015f)));    // flared rim
			Add(CandleBody, FTransform(FQuat::Identity, FVector(P.X, P.Y, BaseZ + 2.f + H * 0.5f), FVector(0.044f, 0.044f, H / 100.f)));
			Add(CandleBody, FTransform(FQuat::Identity, FVector(P.X, P.Y, BaseZ + 2.f + H + 0.5f), FVector(0.056f, 0.056f, 0.012f)));   // melted wax pool on top
			const FVector F(P.X, P.Y, BaseZ + 2.f + H + 4.2f);
			Add(Flame, FTransform(FQuat::Identity, F, FVector(0.027f, 0.027f, 0.065f)));
			FlameBase.Add(F);
			FlamePhase.Add(Rand.FRandRange(0.f, 6.28f));
		};

		// A cord hung from A to B in a gentle sag, with candles along it and (on long runs) one warm light
		auto Swag = [&](const FVector& A, const FVector& B, bool bLight)
		{
			const float Len = FVector::Distance(A, B);
			const float Sag = FMath::Clamp(Len * 0.09f, 18.f, 55.f);
			auto At = [&](float T) { return FMath::Lerp(A, B, T) - FVector(0.f, 0.f, Sag * 4.f * T * (1.f - T)); };
			const int32 Segs = FMath::Max(6, FMath::CeilToInt(Len / 40.f));
			for (int32 s = 0; s < Segs; ++s) Cyl(Rope, At(float(s) / Segs), At(float(s + 1) / Segs), 1.6f);
			const int32 N = FMath::Max(2, FMath::RoundToInt(Len / 85.f));
			for (int32 i = 0; i < N; ++i)
			{
				const float T = (i + 0.5f) / N;
				Candle(At(T), B - A);
			}
			if (bLight && Len > 300.f) AddFlickerLight(At(0.5f) - FVector(0.f, 0.f, 60.f), 1500.f, 560.f, FLinearColor(1.0f, 0.66f, 0.30f), 0.12f);
		};

		const FVector2D VC = JeebTerrain::VillageCenter();
		const float VHalf = JeebTerrain::VillageHalf();

		// Ring of poles round the fountain plaza, joined to one another
		TArray<FVector> RingTops;
		for (int32 k = 0; k < 8; ++k)
		{
			const float A = (k + 0.5f) * PI * 0.25f;
			RingTops.Add(Pole(VC.X + 820.f * FMath::Cos(A), VC.Y + 820.f * FMath::Sin(A), 380.f));
		}
		for (int32 k = 0; k < 8; ++k) Swag(RingTops[k], RingTops[(k + 1) % 8], true);

		// At each hut inside the walls: from the front roof corners to a pole either side of the doorway, then on to the ring
		for (TActorIterator<ACottage> It(GetWorld()); It; ++It)
		{
			const FTransform HT = It->GetActorTransform();
			const FVector HP = HT.GetLocation();
			if (JeebTerrain::RectSD(FVector2D(HP.X, HP.Y), VC, FVector2D(VHalf, VHalf)) > -150.f) continue;   // only the huts inside the village
			for (int32 Sd = -1; Sd <= 1; Sd += 2)
			{
				const FVector Eave = HT.TransformPosition(FVector(385.f, Sd * 300.f, 262.f));
				const FVector PoleXY = HT.TransformPosition(FVector(560.f, Sd * 330.f, 0.f));
				const FVector Top = Pole(PoleXY.X, PoleXY.Y, 330.f);
				Swag(Eave, Top, false);
				int32 Near = 0;
				for (int32 r = 1; r < 8; ++r)
					if (FVector2D::DistSquared(FVector2D(Top), FVector2D(RingTops[r])) < FVector2D::DistSquared(FVector2D(Top), FVector2D(RingTops[Near]))) Near = r;
				Swag(Top, RingTops[Near], true);
			}
		}

		for (TPair<UInstancedStaticMeshComponent*, TArray<FTransform>>& Pair : Strings)
		{
			Pair.Key->AddInstances(Pair.Value, false, false, false);
		}
	}
	// ---------------- Leaves drifting down around the player ----------------
	{
		TArray<TArray<FTransform>> PerType;
		PerType.SetNum(NumLeafTypes);
		for (int32 i = 0; i < NumFallingLeaves; ++i)
		{
			FFallingLeaf Lf;
			Lf.Type = i % 4;   // no green ones in the air
			Lf.Pos = FVector(Rand.FRandRange(-1500.f, 1500.f), Rand.FRandRange(-1500.f, 1500.f), Rand.FRandRange(0.f, 800.f));
			Lf.Fall = Rand.FRandRange(55.f, 110.f);
			Lf.Phase = Rand.FRandRange(0.f, 6.28f);
			Lf.Spin = Rand.FRandRange(0.6f, 1.6f);
			Lf.Index = PerType[Lf.Type].Num();
			PerType[Lf.Type].Add(FTransform(FQuat::Identity, Lf.Pos, FVector(LeafXY, LeafXY, LeafZ)));
			Falling.Add(Lf);
		}
		for (int32 t = 0; t < NumLeafTypes; ++t) FallLeaf[t]->AddInstances(PerType[t], false, false, false);
	}

	// ---------------- Gust leaves (hidden until a gust blows through) ----------------
	{
		TArray<TArray<FTransform>> PerType;
		PerType.SetNum(NumLeafTypes);
		for (int32 i = 0; i < NumGustLeaves; ++i)
		{
			FGustLeaf L;
			L.Type = i % 4;
			L.Along = Rand.FRandRange(-350.f, 350.f);
			L.Across = Rand.FRandRange(-GustWidth * 0.5f, GustWidth * 0.5f);
			L.Height = Rand.FRandRange(40.f, 330.f);
			L.Phase = Rand.FRandRange(0.f, 6.28f);
			L.Spin = Rand.FRandRange(0.8f, 2.0f);
			L.Index = PerType[L.Type].Num();
			PerType[L.Type].Add(FTransform(FQuat::Identity, FVector(0, 0, -5000.f), FVector(0.001f)));
			GustLeaves.Add(L);
		}
		for (int32 t = 0; t < NumLeafTypes; ++t) GustLeaf[t]->AddInstances(PerType[t], false, false, false);
		bGustHidden = true;
	}
}

void AAutumnDecor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Time += DeltaSeconds;

	// Lamp, candle and torch lights flicker
	for (int32 i = 0; i < FlickerLights.Num(); ++i)
	{
		const float Ph = FlickerPhase[i];
		const float W = 0.55f * FMath::Sin(Time * 9.3f + Ph) + 0.30f * FMath::Sin(Time * 23.1f + Ph * 1.7f) + 0.15f * FMath::Sin(Time * 41.f + Ph * 2.3f);
		FlickerLights[i]->SetIntensity(FlickerBase[i] * (1.f + FlickerAmt[i] * W));
	}

	// Candle flames flicker (about 12 times a second is plenty)
	FlameTimer += DeltaSeconds;
	if (FlameTimer >= 0.08f && FlameBase.Num() > 0)
	{
		FlameTimer = 0.f;
		for (int32 i = 0; i < FlameBase.Num(); ++i)
		{
			const float Ph = FlamePhase[i];
			const float Flicker = 1.f + 0.20f * FMath::Sin(Time * 11.f + Ph) + 0.10f * FMath::Sin(Time * 23.f + Ph * 2.f);
			const FVector P = FlameBase[i] + FVector(0.4f * FMath::Sin(Time * 7.f + Ph), 0.4f * FMath::Cos(Time * 6.f + Ph), 3.2f * (Flicker - 1.f));
			Flame->UpdateInstanceTransform(i, FTransform(FQuat::Identity, P, FVector(0.027f, 0.027f, 0.065f * Flicker)), true, i == FlameBase.Num() - 1, true);
		}
		// Torch flames lick and sway
		for (int32 i = 0; i < TorchFlamePos.Num(); ++i)
		{
			const float Ph = TorchPhase[i];
			const float H = 1.f + 0.22f * FMath::Sin(Time * 8.f + Ph) + 0.12f * FMath::Sin(Time * 19.f + Ph * 2.f);
			const float Wd = 1.f - 0.10f * FMath::Sin(Time * 11.f + Ph * 1.3f);
			const FQuat Lean = FRotator(5.f * FMath::Sin(Time * 3.1f + Ph), 0.f, 5.f * FMath::Cos(Time * 2.7f + Ph)).Quaternion();
			const FVector F = TorchFlamePos[i] + FVector(0.f, 0.f, 18.f * (H - 1.f));
			const bool bLast = i == TorchFlamePos.Num() - 1;
			TorchFlame->UpdateInstanceTransform(i, FTransform(Lean, F, FVector(0.16f * Wd, 0.16f * Wd, 0.36f * H)), true, bLast, true);
			TorchCore->UpdateInstanceTransform(i, FTransform(Lean, F - FVector(0.f, 0.f, 5.f), FVector(0.09f * Wd, 0.09f * Wd, 0.22f * H)), true, bLast, true);
		}
	}
	// ---- Gust of wind: every few seconds a flurry of leaves whips past in an open part of the map ----
	{
		APlayerController* GPC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		APawn* GPawn = GPC ? GPC->GetPawn() : nullptr;
		if (GPawn)
		{
			if (!bGusting)
			{
				GustTimer += DeltaSeconds;
				if (GustTimer >= 1.7f) // about three times as many gusts as before
				{
					GustTimer = 0.f;
					const FVector PC = GPawn->GetActorLocation();
					// Look for an open (not wooded) spot fairly close by, so the gust can actually be seen
					for (int32 Try = 0; Try < 10; ++Try)
					{
						const float Ang = Rand.FRandRange(0.f, 2.f * PI);
						const float Dist = Rand.FRandRange(700.f, 2300.f);
						const float X = PC.X + Dist * FMath::Cos(Ang);
						const float Y = PC.Y + Dist * FMath::Sin(Ang);
						if (JeebTerrain::WoodsDensity(X, Y) > 0.5f) continue;
						// Blow roughly across the player's view, from one side to the other
						const FVector2D ToPlayer = FVector2D(PC.X - X, PC.Y - Y).GetSafeNormal();
						const FVector2D Across(-ToPlayer.Y, ToPlayer.X);
						GustDir = (Across * (Rand.FRand() < 0.5f ? 1.f : -1.f) + ToPlayer * Rand.FRandRange(-0.3f, 0.3f)).GetSafeNormal();
						GustOrigin = FVector(X, Y, 0.f) - FVector(GustDir.X, GustDir.Y, 0.f) * (GustSpeed * GustDuration * 0.5f);
						GustT = 0.f;
						bGusting = true;
						break;
					}
				}
			}
			if (bGusting)
			{
				GustT += DeltaSeconds;
				const float Fade = FMath::Clamp(FMath::Min(GustT / 0.3f, (GustDuration - GustT) / 0.4f), 0.f, 1.f);
				SetGustLeaves(Fade);
				bGustHidden = false;
				if (GustT >= GustDuration)
				{
					bGusting = false;
				}
			}
			else if (!bGustHidden)
			{
				SetGustLeaves(0.f);
				bGustHidden = true;
			}
		}
	}

	// Leaves fall, sway and spin around the player
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn) return;
	const FVector C = Pawn->GetActorLocation();

	int32 LastOfType[NumLeafTypes];
	for (int32 t = 0; t < NumLeafTypes; ++t) LastOfType[t] = -1;
	for (int32 i = 0; i < Falling.Num(); ++i) LastOfType[Falling[i].Type] = i;

	for (int32 i = 0; i < Falling.Num(); ++i)
	{
		FFallingLeaf& L = Falling[i];
		L.Pos.Z -= L.Fall * DeltaSeconds;
		L.Pos.X += 45.f * FMath::Sin(Time * 0.9f + L.Phase) * DeltaSeconds;
		L.Pos.Y += 45.f * FMath::Cos(Time * 0.7f + L.Phase) * DeltaSeconds;

		const float DistXY = FVector2D::Distance(FVector2D(L.Pos.X, L.Pos.Y), FVector2D(C.X, C.Y));
		if (L.Pos.Z < JeebTerrain::HeightAt(L.Pos.X, L.Pos.Y) + 4.f || DistXY > 1800.f
			|| (L.Pos.Z < JeebTerrain::HeightAt(L.Pos.X, L.Pos.Y) + 560.f && JeebTerrain::NoGroundCover(L.Pos.X, L.Pos.Y)))   // drifting leaves never fall through a roof
		{
			const float Ang = Rand.FRandRange(0.f, 2.f * PI);
			const float Rad = 1600.f * FMath::Sqrt(Rand.FRand());
			L.Pos = FVector(C.X + Rad * FMath::Cos(Ang), C.Y + Rad * FMath::Sin(Ang), C.Z + Rand.FRandRange(250.f, 900.f));
		}

		const FQuat Q = FRotator(Time * L.Spin * 50.f + L.Phase * 40.f, Time * L.Spin * 70.f, Time * 35.f).Quaternion();
		FallLeaf[L.Type]->UpdateInstanceTransform(L.Index, FTransform(Q, L.Pos, FVector(LeafXY, LeafXY, LeafZ)), true, i == LastOfType[L.Type], true);
	}
}

void AAutumnDecor::SetGustLeaves(float Fade)
{
	const FVector2D Perp(-GustDir.Y, GustDir.X);
	const FVector2D Center = FVector2D(GustOrigin.X, GustOrigin.Y) + GustDir * (GustSpeed * GustT);

	int32 LastOfType[NumLeafTypes];
	for (int32 t = 0; t < NumLeafTypes; ++t) LastOfType[t] = -1;
	for (int32 i = 0; i < GustLeaves.Num(); ++i) LastOfType[GustLeaves[i].Type] = i;

	for (int32 i = 0; i < GustLeaves.Num(); ++i)
	{
		const FGustLeaf& L = GustLeaves[i];
		FTransform T;
		if (Fade <= 0.f)
		{
			T = FTransform(FQuat::Identity, FVector(0, 0, -5000.f), FVector(0.001f));
		}
		else
		{
			const float Swirl = 70.f * FMath::Sin(GustT * 4.f + L.Phase);
			const FVector2D P = Center + GustDir * L.Along + Perp * (L.Across + Swirl);
			const float Z = JeebTerrain::HeightAt(P.X, P.Y) + L.Height + 45.f * FMath::Sin(GustT * 6.f + L.Phase) + 25.f * GustT;
			const FQuat Q = FRotator(GustT * L.Spin * 260.f, GustT * L.Spin * 340.f + L.Phase * 50.f, GustT * 200.f).Quaternion();
			T = FTransform(Q, FVector(P.X, P.Y, Z), FVector(FMath::Max(LeafXY * Fade, 0.001f), FMath::Max(LeafXY * Fade, 0.001f), LeafZ));
		}
		GustLeaf[L.Type]->UpdateInstanceTransform(L.Index, T, true, i == LastOfType[L.Type], true);
	}
}

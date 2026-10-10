#include "AutumnDecor.h"
#include "Scarecrow.h"
#include "Terrain.h"
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
	const FLinearColor LeafPalette[3] = {
		FLinearColor(0.80f, 0.30f, 0.05f), // orange
		FLinearColor(0.55f, 0.08f, 0.05f), // red
		FLinearColor(0.85f, 0.55f, 0.08f), // amber
	};
	constexpr int32 NumFallingLeaves = 15;
	constexpr int32 NumGustLeaves = 42;
	constexpr float GustDuration = 2.4f;
	constexpr float GustSpeed = 840.f;    // 40% slower than before
	constexpr float GustWidth = 800.f;   // about as wide as a hut
	constexpr int32 NumGroundLeaves = 4000;
	constexpr int32 NumCandles = 45;
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
	Glow     = MakeISM(TEXT("Glow"),     CubeMesh.Object,   FLinearColor(1.00f, 0.72f, 0.12f), false);
	GlowCone = MakeISM(TEXT("GlowCone"), ConeMesh.Object,   FLinearColor(1.00f, 0.72f, 0.12f), false);
	Wood     = MakeISM(TEXT("Wood"),     CylMesh.Object,    FLinearColor(0.16f, 0.09f, 0.05f), true);
	WoodCone = MakeISM(TEXT("WoodCone"), ConeMesh.Object,   FLinearColor(0.07f, 0.04f, 0.08f), true);
	Cloth    = MakeISM(TEXT("Cloth"),    CubeMesh.Object,   FLinearColor(0.35f, 0.12f, 0.12f), false);
	HayRoll  = MakeISM(TEXT("HayRoll"),  CylMesh.Object,    FLinearColor(0.78f, 0.58f, 0.15f), true);
	HayBox   = MakeISM(TEXT("HayBox"),   CubeMesh.Object,   FLinearColor(0.78f, 0.58f, 0.15f), false);
	StoneBox = MakeISM(TEXT("StoneBox"), CubeMesh.Object,   FLinearColor(0.22f, 0.23f, 0.27f), true);
	StoneCyl = MakeISM(TEXT("StoneCyl"), CylMesh.Object,    FLinearColor(0.22f, 0.23f, 0.27f), true);
	CandleBody = MakeISM(TEXT("CandleBody"), CylMesh.Object, FLinearColor(0.88f, 0.84f, 0.72f), false);
	Flame    = MakeISM(TEXT("Flame"),    ConeMesh.Object,   FLinearColor(1.00f, 0.72f, 0.10f), false);
	for (int32 i = 0; i < 3; ++i)
	{
		GroundLeaf.Add(MakeISM(*FString::Printf(TEXT("GroundLeaf%d"), i), CubeMesh.Object, LeafPalette[i], false, 5000, 6500));
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

		UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
		Light->SetupAttachment(RootComponent);
		Light->SetRelativeLocation(FVector(Spot.X, Spot.Y, Z + 255.f));
		Light->SetIntensity(160.f);
		Light->SetLightColor(FLinearColor(1.0f, 0.68f, 0.32f));
		Light->SetAttenuationRadius(650.f);
		Light->SetCastShadows(false); // cheap: no shadow for these
		Light->RegisterComponent();
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

	// ---------------- Fallen leaves covering the ground ----------------
	for (int32 i = 0; i < NumGroundLeaves; ++i)
	{
		const float Ang = Rand.FRandRange(0.f, 2.f * PI);
		const float Rad = 600.f + 8600.f * FMath::Sqrt(Rand.FRand());
		const float X = Rad * FMath::Cos(Ang);
		const float Y = Rad * FMath::Sin(Ang);
				const float Wd = Rand.FRandRange(22.f, 38.f);
		if (JeebTerrain::NoGroundCover(X, Y)) continue;   // no leaves indoors, in the moat or on the pond
		const FQuat Q = FRotator(Rand.FRandRange(-12.f, 12.f), Rand.FRandRange(0.f, 360.f), Rand.FRandRange(-12.f, 12.f)).Quaternion();
		Put(GroundLeaf[Rand.RandRange(0, 2)], FTransform(Q, FVector(X, Y, Ground(X, Y) + 2.f), FVector(Wd / 100.f, Wd / 100.f, 0.012f)));
	}

	for (TPair<UInstancedStaticMeshComponent*, TArray<FTransform>>& Pair : Batch)
	{
		Pair.Key->AddInstances(Pair.Value, false, false, false);
	}

	// ---------------- Floating candles over the meadow, like the great hall ----------------
	{
		TArray<FTransform> Bodies, Flames;
		for (int32 i = 0; i < NumCandles; ++i)
		{
			const float Ang = Rand.FRandRange(0.f, 2.f * PI);
			const float Rad = Rand.FRandRange(450.f, 1500.f);
			const float X = 500.f + Rad * FMath::Cos(Ang);
			const float Y = 300.f + Rad * FMath::Sin(Ang);
			const FVector P(X, Y, Ground(X, Y) + Rand.FRandRange(330.f, 700.f));
			CandleBase.Add(P);
			CandleBob.Add(FVector(Rand.FRandRange(8.f, 18.f), Rand.FRandRange(0.6f, 1.1f), Rand.FRandRange(0.f, 6.28f)));
			Bodies.Add(FTransform(FQuat::Identity, P, FVector(0.06f, 0.06f, 0.28f)));
			Flames.Add(FTransform(FQuat::Identity, P + FVector(0, 0, 21.f), FVector(0.07f, 0.07f, 0.14f)));
		}
		CandleBody->AddInstances(Bodies, false, false, false);
		Flame->AddInstances(Flames, false, false, false);
	}

	// ---------------- Leaves drifting down around the player ----------------
	{
		TArray<TArray<FTransform>> PerType;
		PerType.SetNum(3);
		for (int32 i = 0; i < NumFallingLeaves; ++i)
		{
			FFallingLeaf Lf;
			Lf.Type = i % 3;
			Lf.Pos = FVector(Rand.FRandRange(-1500.f, 1500.f), Rand.FRandRange(-1500.f, 1500.f), Rand.FRandRange(0.f, 800.f));
			Lf.Fall = Rand.FRandRange(55.f, 110.f);
			Lf.Phase = Rand.FRandRange(0.f, 6.28f);
			Lf.Spin = Rand.FRandRange(0.6f, 1.6f);
			Lf.Index = PerType[Lf.Type].Num();
			PerType[Lf.Type].Add(FTransform(FQuat::Identity, Lf.Pos, FVector(0.22f, 0.22f, 0.012f)));
			Falling.Add(Lf);
		}
		for (int32 t = 0; t < 3; ++t) FallLeaf[t]->AddInstances(PerType[t], false, false, false);
	}

	// ---------------- Gust leaves (hidden until a gust blows through) ----------------
	{
		TArray<TArray<FTransform>> PerType;
		PerType.SetNum(3);
		for (int32 i = 0; i < NumGustLeaves; ++i)
		{
			FGustLeaf L;
			L.Type = i % 3;
			L.Along = Rand.FRandRange(-350.f, 350.f);
			L.Across = Rand.FRandRange(-GustWidth * 0.5f, GustWidth * 0.5f);
			L.Height = Rand.FRandRange(40.f, 330.f);
			L.Phase = Rand.FRandRange(0.f, 6.28f);
			L.Spin = Rand.FRandRange(0.8f, 2.0f);
			L.Index = PerType[L.Type].Num();
			PerType[L.Type].Add(FTransform(FQuat::Identity, FVector(0, 0, -5000.f), FVector(0.001f)));
			GustLeaves.Add(L);
		}
		for (int32 t = 0; t < 3; ++t) GustLeaf[t]->AddInstances(PerType[t], false, false, false);
		bGustHidden = true;
	}
}

void AAutumnDecor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Time += DeltaSeconds;

	// Candles bob gently
	for (int32 i = 0; i < CandleBase.Num(); ++i)
	{
		const FVector& B = CandleBase[i];
		const FVector& Bob = CandleBob[i];
		const FVector P(B.X + 10.f * FMath::Sin(Time * 0.3f + Bob.Z), B.Y + 10.f * FMath::Cos(Time * 0.27f + Bob.Z),
		                B.Z + Bob.X * FMath::Sin(Time * Bob.Y + Bob.Z));
		const bool bLast = (i == CandleBase.Num() - 1);
		CandleBody->UpdateInstanceTransform(i, FTransform(FQuat::Identity, P, FVector(0.06f, 0.06f, 0.28f)), true, bLast, true);
		const float Flicker = 1.f + 0.12f * FMath::Sin(Time * 9.f + Bob.Z * 3.f);
		Flame->UpdateInstanceTransform(i, FTransform(FQuat::Identity, P + FVector(0, 0, 21.f), FVector(0.07f, 0.07f, 0.14f * Flicker)), true, bLast, true);
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

	int32 LastOfType[3] = { -1, -1, -1 };
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
		FallLeaf[L.Type]->UpdateInstanceTransform(L.Index, FTransform(Q, L.Pos, FVector(0.22f, 0.22f, 0.012f)), true, i == LastOfType[L.Type], true);
	}
}

void AAutumnDecor::SetGustLeaves(float Fade)
{
	const FVector2D Perp(-GustDir.Y, GustDir.X);
	const FVector2D Center = FVector2D(GustOrigin.X, GustOrigin.Y) + GustDir * (GustSpeed * GustT);

	int32 LastOfType[3] = { -1, -1, -1 };
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
			T = FTransform(Q, FVector(P.X, P.Y, Z), FVector(FMath::Max(0.22f * Fade, 0.001f), FMath::Max(0.22f * Fade, 0.001f), 0.012f));
		}
		GustLeaf[L.Type]->UpdateInstanceTransform(L.Index, T, true, i == LastOfType[L.Type], true);
	}
}

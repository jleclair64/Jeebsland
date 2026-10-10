#include "Castle.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"

ACastle::ACastle()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Origin = CreateDefaultSubobject<USceneComponent>(TEXT("Origin"));
	SetRootComponent(Origin);
}

void ACastle::Build()
{
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* CylMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* ConeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));

	auto MakeISM = [&](UStaticMesh* Msh, const FLinearColor& Col, bool bShadow)
	{
		UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this);
		C->SetStaticMesh(Msh);
		C->SetupAttachment(RootComponent);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(bShadow);
		C->bAffectDynamicIndirectLighting = false;
		C->bAffectDistanceFieldLighting = false;
		C->RegisterComponent();
		if (UMaterialInstanceDynamic* Mid = C->CreateDynamicMaterialInstance(0, BaseMat)) Mid->SetVectorParameterValue(TEXT("Color"), Col);
		return C;
	};
	UInstancedStaticMeshComponent* Stone  = MakeISM(CubeMesh, FLinearColor(0.36f, 0.35f, 0.38f), true);
	UInstancedStaticMeshComponent* Dark   = MakeISM(CubeMesh, FLinearColor(0.23f, 0.22f, 0.26f), true);
	UInstancedStaticMeshComponent* Plank  = MakeISM(CubeMesh, FLinearColor(0.36f, 0.22f, 0.10f), true);
	UInstancedStaticMeshComponent* Beam   = MakeISM(CubeMesh, FLinearColor(0.20f, 0.11f, 0.05f), true);
	UInstancedStaticMeshComponent* Iron   = MakeISM(CubeMesh, FLinearColor(0.10f, 0.10f, 0.12f), true);
	UInstancedStaticMeshComponent* Banner = MakeISM(CubeMesh, FLinearColor(0.55f, 0.08f, 0.06f), false);
	UInstancedStaticMeshComponent* Lamp   = MakeISM(CubeMesh, FLinearColor(5.0f, 2.0f, 0.4f), false);   // bright: glows
	UInstancedStaticMeshComponent* Pillar = MakeISM(CylMesh, FLinearColor(0.40f, 0.39f, 0.42f), true);
	UInstancedStaticMeshComponent* Water  = MakeISM(CylMesh, FLinearColor(0.04f, 0.10f, 0.18f), false);
	UInstancedStaticMeshComponent* Slate  = MakeISM(ConeMesh, FLinearColor(0.12f, 0.16f, 0.32f), true);
	UInstancedStaticMeshComponent* Gilt   = MakeISM(ConeMesh, FLinearColor(0.95f, 0.70f, 0.10f), true);

	const FVector2D Cen = JeebTerrain::CastlePos();
	const float G = JeebTerrain::CastleLevel();                 // the castle stands on level ground
	const float W = JeebTerrain::CastleWall;
	const float Wt = JeebTerrain::CastleWallT;
	const float Hw = JeebTerrain::CastleWallH;
	const float Tw = JeebTerrain::CastleTower;
	const float Gh = JeebTerrain::CastleGateHalf;
	const float GateTop = 320.f;

	// Spire tower
	const float KeepOut = 320.f, KeepIn = 270.f, KeepH = 1800.f;
	const float StepRise = 18.f, StepDeg = 9.f, DoorDeg = 180.f;
	const int32 NumSteps = 100;

	auto Pt = [&](float Lx, float Ly, float Lz) { return FVector(Cen.X + Lx, Cen.Y + Ly, G + Lz); };
		auto Solid = [&](const FVector& Ctr, const FVector& Ext, const FQuat& Rot, bool bCamera)
	{
		UBoxComponent* B = NewObject<UBoxComponent>(this);
		B->SetupAttachment(RootComponent);
		B->SetBoxExtent(Ext);
		B->SetRelativeLocationAndRotation(Ctr, Rot);
		B->SetCollisionProfileName(TEXT("BlockAll"));
				B->SetCollisionResponseToChannel(ECC_Camera, bCamera ? ECR_Block : ECR_Ignore);   // walls and floors stop the camera too
		B->RegisterComponent();
	};
	// A box from height Z0 to Z1 above the castle ground, turned by Yaw (its X is the Hx direction); null Mi = invisible
	auto Box = [&](UInstancedStaticMeshComponent* Mi, float Lx, float Ly, float Z0, float Z1, float Hx, float Hy, float Yaw, bool bSolid)
	{
		const FQuat Rot = FRotator(0.f, Yaw, 0.f).Quaternion();
		const float Hz = (Z1 - Z0) * 0.5f;
		const FVector Ctr = Pt(Lx, Ly, (Z0 + Z1) * 0.5f);
		if (Mi) Mi->AddInstance(FTransform(Rot, Ctr, FVector(2.f * Hx / 100.f, 2.f * Hy / 100.f, 2.f * Hz / 100.f)));
				if (bSolid) Solid(Ctr, FVector(Hx, Hy, Hz), Rot, Mi != Plank && Mi != Beam);
	};
	auto Round = [&](UInstancedStaticMeshComponent* Mi, float Lx, float Ly, float Z0, float Z1, float Rad, bool bSolid)
	{
		const FVector Ctr = Pt(Lx, Ly, (Z0 + Z1) * 0.5f);
		Mi->AddInstance(FTransform(FQuat::Identity, Ctr, FVector(2.f * Rad / 100.f, 2.f * Rad / 100.f, (Z1 - Z0) / 100.f)));
				if (bSolid) Solid(Ctr, FVector(Rad * 0.8f, Rad * 0.8f, (Z1 - Z0) * 0.5f), FQuat::Identity, true);
	};
	auto Light = [&](float Lx, float Ly, float Lz, float Intensity, float Reach)
	{
		UPointLightComponent* Pl = NewObject<UPointLightComponent>(this);
		Pl->SetupAttachment(RootComponent);
		Pl->SetRelativeLocation(Pt(Lx, Ly, Lz));
		Pl->SetLightColor(FLinearColor(1.f, 0.62f, 0.25f));
		Pl->SetIntensity(Intensity);
		Pl->SetAttenuationRadius(Reach);
		Pl->SetCastShadows(false);
		Pl->RegisterComponent();
	};
	auto Cs = [](float Deg) { return FMath::Cos(FMath::DegreesToRadians(Deg)); };
	auto Sn = [](float Deg) { return FMath::Sin(FMath::DegreesToRadians(Deg)); };

	// ---- The moat: one flat disc of water; the ground hides it everywhere except in the ditch ----
	Round(Water, 0.f, 0.f, -52.f, -47.f, JeebTerrain::CastleMoatOut + 50.f, false);

	// ---- The wall (the gate is in the -X side) ----
	Box(Stone, W, 0.f, -40.f, Hw, Wt, W + Wt, 0.f, true);
	Box(Stone, 0.f, W, -40.f, Hw, W + Wt, Wt, 0.f, true);
	Box(Stone, 0.f, -W, -40.f, Hw, W + Wt, Wt, 0.f, true);
	{
		const float SideHalf = (W + Wt - Gh) * 0.5f;
		const float SideMid = Gh + SideHalf;
		Box(Stone, -W, SideMid, -40.f, Hw, Wt, SideHalf, 0.f, true);
		Box(Stone, -W, -SideMid, -40.f, Hw, Wt, SideHalf, 0.f, true);
		Box(Stone, -W, 0.f, GateTop, Hw, Wt, Gh, 0.f, true);   // the wall carries on over the gate
	}

	// Parapet on the outer edge of the walkway: a low wall with blocks along the top
	auto Parapet = [&](float Lx, float Ly, float HalfLen, bool bAlongY)
	{
		Box(Stone, Lx, Ly, Hw, Hw + 80.f, bAlongY ? 15.f : HalfLen, bAlongY ? HalfLen : 15.f, 0.f, true);
		for (float S = -HalfLen + 30.f; S <= HalfLen - 30.f; S += 90.f)
		{
			Box(Dark, Lx + (bAlongY ? 0.f : S), Ly + (bAlongY ? S : 0.f), Hw + 80.f, Hw + 135.f, bAlongY ? 18.f : 35.f, bAlongY ? 35.f : 18.f, 0.f, false);
		}
	};
	const float Outer = W + Wt - 15.f;
	const float Reach = W - Tw;
	Parapet(Outer, 0.f, Reach, true);
	Parapet(-Outer, 0.f, Reach, true);
	Parapet(0.f, Outer, Reach, false);
	Parapet(0.f, -Outer, Reach, false);

	// ---- Corner towers: level with the walkway, so you can walk straight round the whole top ----
	for (int32 Sx = -1; Sx <= 1; Sx += 2)
	{
		for (int32 Sy = -1; Sy <= 1; Sy += 2)
		{
			const float Cx = Sx * W, Cy = Sy * W;
			Box(Stone, Cx, Cy, -40.f, Hw, Tw, Tw, 0.f, true);
			Parapet(Sx * (W + Tw - 15.f), Cy, Tw, true);
			Parapet(Cx, Sy * (W + Tw - 15.f), Tw, false);
			Box(Beam, Cx, Cy, Hw, Hw + 380.f, 5.f, 5.f, 0.f, false);                       // flag pole
			Box(Banner, Cx, Cy + 42.f, Hw + 290.f, Hw + 370.f, 2.f, 38.f, 0.f, false);     // and its flag
		}
	}

	// ---- Gate: a raised portcullis, trim, lanterns and banners ----
	for (float Yb = -110.f; Yb <= 110.f; Yb += 44.f) Box(Iron, -W, Yb, 235.f, GateTop, 4.f, 4.f, 0.f, false);
	for (int32 Sd = -1; Sd <= 1; Sd += 2)
	{
		Box(Dark, -W - Wt + 8.f, Sd * (Gh + 10.f), 0.f, GateTop, 14.f, 10.f, 0.f, false);
		Box(Lamp, -W - Wt - 12.f, Sd * (Gh + 60.f), 230.f, 260.f, 7.f, 7.f, 0.f, false);
		Light(-W - Wt - 40.f, Sd * (Gh + 60.f), 250.f, 4000.f, 800.f);
		Box(Banner, -W - Wt - 4.f, Sd * (Gh + 140.f), 300.f, 470.f, 3.f, 32.f, 0.f, false);
	}

	// ---- The bridge over the moat ----
	{
		const float B0 = -(JeebTerrain::CastleMoatOut + 110.f), B1 = -(JeebTerrain::CastleMoatIn - 110.f);
		const float Bm = (B0 + B1) * 0.5f, Bh = (B1 - B0) * 0.5f;
		Box(Plank, Bm, 0.f, -40.f, 6.f, Bh, Gh + 5.f, 0.f, true);
		for (int32 Sd = -1; Sd <= 1; Sd += 2)
		{
			Box(Beam, Bm, Sd * (Gh - 2.f), 6.f, 95.f, Bh, 6.f, 0.f, true);
			for (float Px = B0 + 30.f; Px <= B1 - 20.f; Px += 130.f) Box(Beam, Px, Sd * (Gh - 2.f), 6.f, 130.f, 9.f, 9.f, 0.f, false);
		}
	}

	// ---- Ladders from the courtyard up to the walkway ----
	for (const JeebTerrain::FLadder& L : JeebTerrain::CastleLadders())
	{
		const FQuat Lq = FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan2(L.Out.Y, L.Out.X)), 0.f).Quaternion();
		const FVector2D Side(-L.Out.Y, L.Out.X);
		const float Len = Hw + 30.f;
		for (int32 Sd = -1; Sd <= 1; Sd += 2)
		{
			const FVector2D Rp = L.Pos + Side * (Sd * 26.f);
			Beam->AddInstance(FTransform(Lq, FVector(Rp.X, Rp.Y, G + Len * 0.5f), FVector(0.05f, 0.05f, Len / 100.f)));
		}
		for (float Z = 30.f; Z < Len; Z += 38.f)
		{
			Beam->AddInstance(FTransform(Lq, FVector(L.Pos.X, L.Pos.Y, G + Z), FVector(0.05f, 0.64f, 0.05f)));
		}
	}

	// ======================= The spire tower =======================
	// Wall: a ring of planks, with the door on the gate side (the stair starts at the door and winds up, turning left)
	for (int32 K = 0; K < 24; ++K)
	{
		const float Deg = 15.f * K;
		const bool bDoor = FMath::Abs(FMath::FindDeltaAngleDegrees(Deg, DoorDeg)) < 20.f;
		Box(Stone, Cs(Deg) * 292.f, Sn(Deg) * 292.f, bDoor ? 300.f : -30.f, KeepH, 46.f, 28.f, Deg + 90.f, true);
	}
	Round(Pillar, 0.f, 0.f, -30.f, KeepH, 55.f, false);          // the column the stair winds round
	Box(nullptr, 0.f, 0.f, -30.f, KeepH, 38.f, 38.f, 0.f, true);

	// The stair: 100 treads (18 cm apart, nine degrees round), two and a half turns, then two flat landing treads
	for (int32 I = 1; I <= NumSteps + 2; ++I)
	{
		const float Deg = DoorDeg + StepDeg * I;
		const float Top = (I <= NumSteps) ? StepRise * I : KeepH;
		Box(Plank, Cs(Deg) * 161.f, Sn(Deg) * 161.f, Top - 26.f, Top, 112.f, 24.f, Deg, true);
	}

	// Top floor: open over the last turn of the stair (225 deg round to 6 deg) where you step out, solid everywhere else
	for (int32 K = 0; K < 30; ++K)
	{
		if (K >= 19 || K == 0) continue;
		const float Deg = 12.f * K;
		Box(Stone, Cs(Deg) * 178.f, Sn(Deg) * 178.f, KeepH - 40.f, KeepH, 143.f, 37.f, Deg, true);
	}
	// The terrace round the outside, with its parapet
	for (int32 K = 0; K < 24; ++K)
	{
		const float Deg = 15.f * K;
		Box(Stone, Cs(Deg) * 380.f, Sn(Deg) * 380.f, KeepH - 40.f, KeepH, 60.f, 62.f, Deg, true);
		Box(Stone, Cs(Deg) * 428.f, Sn(Deg) * 428.f, KeepH, KeepH + 100.f, 12.f, 58.f, Deg, true);
		Box(Dark, Cs(Deg) * 428.f, Sn(Deg) * 428.f, KeepH + 100.f, KeepH + 150.f, 14.f, 26.f, Deg, false);
	}
	// Roof: six pillars carry a slate cone, with a gilded spire on top and a beacon underneath
	for (int32 J = 0; J < 6; ++J)
	{
		const float Deg = 30.f + 60.f * J;
		Round(Pillar, Cs(Deg) * 395.f, Sn(Deg) * 395.f, KeepH, KeepH + 380.f, 22.f, true);
	}
	Slate->AddInstance(FTransform(FQuat::Identity, Pt(0.f, 0.f, KeepH + 380.f + 225.f), FVector(9.6f, 9.6f, 4.5f)));
	Gilt->AddInstance(FTransform(FQuat::Identity, Pt(0.f, 0.f, KeepH + 380.f + 450.f + 230.f), FVector(0.9f, 0.9f, 4.6f)));
	Round(Pillar, 0.f, 0.f, KeepH, KeepH + 50.f, 34.f, false);
	Box(Lamp, 0.f, 0.f, KeepH + 50.f, KeepH + 110.f, 22.f, 22.f, 0.f, false);
	Light(0.f, 0.f, KeepH + 170.f, 12000.f, 3500.f);

	// Lights inside the tower, and over its door
	Light(150.f, 0.f, 300.f, 5000.f, 900.f);
	Light(-150.f, 0.f, 860.f, 5000.f, 900.f);
	Light(0.f, 150.f, 1420.f, 5000.f, 900.f);
	Box(Lamp, 252.f, 0.f, 280.f, 310.f, 7.f, 7.f, 0.f, false);
	Box(Lamp, 0.f, 252.f, 1400.f, 1430.f, 7.f, 7.f, 0.f, false);
	for (int32 Sd = -1; Sd <= 1; Sd += 2)
	{
		Box(Lamp, -KeepOut - 10.f, Sd * 130.f, 230.f, 260.f, 7.f, 7.f, 0.f, false);
	}
	Light(-KeepOut - 40.f, 0.f, 250.f, 3500.f, 700.f);
	(void)KeepIn;
}

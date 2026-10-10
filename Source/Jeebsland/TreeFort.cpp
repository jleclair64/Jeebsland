#include "TreeFort.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"

AFort::AFort()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Origin = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Origin);
}

void AFort::Build(const JeebTerrain::FFort& Def)
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));

	auto MakeISM = [&](UStaticMesh* Msh, const FLinearColor& Color, bool bShadow)
	{
		UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this);
		C->SetStaticMesh(Msh);
		C->SetupAttachment(RootComponent);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(bShadow);
		C->bAffectDynamicIndirectLighting = false;
		C->bAffectDistanceFieldLighting = false;
		C->RegisterComponent();
		if (UMaterialInstanceDynamic* Mid = C->CreateDynamicMaterialInstance(0, BaseMat)) Mid->SetVectorParameterValue(TEXT("Color"), Color);
		return C;
	};
	UInstancedStaticMeshComponent* Plank = MakeISM(Cube, FLinearColor(0.36f, 0.22f, 0.10f), true);
	UInstancedStaticMeshComponent* Beam = MakeISM(Cube, FLinearColor(0.20f, 0.11f, 0.05f), true);
	UInstancedStaticMeshComponent* Log = MakeISM(Cyl, FLinearColor(0.28f, 0.16f, 0.07f), true);
	UInstancedStaticMeshComponent* Roof = MakeISM(Cone, FLinearColor(0.42f, 0.11f, 0.06f), true);
	UInstancedStaticMeshComponent* Rope = MakeISM(Cyl, FLinearColor(0.62f, 0.50f, 0.30f), false);
	UInstancedStaticMeshComponent* Lamp = MakeISM(Cube, FLinearColor(5.0f, 2.0f, 0.4f), false);   // bright: glows

	const FVector2D Tree = Def.Tree;
	const float Ground = JeebTerrain::HeightAt(Tree.X, Tree.Y);

	for (int32 Lv = 0; Lv < JeebTerrain::FortLevels; ++Lv)
	{
		const float Top = Ground + Def.Height + JeebTerrain::FortSpacing * Lv;    // the walking surface of this deck
		const float Half = JeebTerrain::FortHalf(Lv);
		const bool bTopDeck = (Lv == JeebTerrain::FortLevels - 1);
		const float Yaw = Def.LadderYaw + 90.f * Lv;                                // local +X is the side the ladder comes up
		const FQuat Q = FRotator(0.f, Yaw, 0.f).Quaternion();
		auto W = [&](float X, float Y, float Z) { return FVector(Tree.X, Tree.Y, 0.f) + Q.RotateVector(FVector(X, Y, 0.f)) + FVector(0.f, 0.f, Z); };
		auto Solid = [&](const FVector& Center, const FVector& HalfExtent)
		{
			UBoxComponent* B = NewObject<UBoxComponent>(this);
			B->SetupAttachment(RootComponent);
			B->SetBoxExtent(HalfExtent);
			B->SetRelativeLocationAndRotation(Center, Q);
			B->SetCollisionProfileName(TEXT("BlockAll"));
			B->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
			B->RegisterComponent();
		};
		auto Stick = [&](UInstancedStaticMeshComponent* C, const FVector& A, const FVector& B, float Thick)
		{
			const FVector D = B - A;
			const float Len = FMath::Max(D.Size(), 1.f);
			C->AddInstance(FTransform(FRotationMatrix::MakeFromX(D / Len).ToQuat(), (A + B) * 0.5f, FVector(Len / 100.f, Thick / 100.f, Thick / 100.f)));
		};

		// ---- Deck, with beams under it and braces down to the trunk ----
		Plank->AddInstance(FTransform(Q, W(0, 0, Top - 12.f), FVector(2.f * Half / 100.f, 2.f * Half / 100.f, 0.24f)));
		Solid(W(0, 0, Top - 12.f), FVector(Half, Half, 12.f));
		Solid(W(0, 0, Top + 200.f), FVector(80.f, 80.f, 200.f));   // the trunk itself is solid on every deck
		Beam->AddInstance(FTransform(Q, W(0, 0, Top - 38.f), FVector(2.f * Half / 100.f, 0.3f, 0.3f)));
		Beam->AddInstance(FTransform(Q, W(0, 0, Top - 38.f), FVector(0.3f, 2.f * Half / 100.f, 0.3f)));
		for (int32 Sx = -1; Sx <= 1; Sx += 2)
		{
			for (int32 Sy = -1; Sy <= 1; Sy += 2)
			{
				Stick(Beam, W(Sx * 90.f, Sy * 90.f, Top - 320.f), W(Sx * (Half - 30.f), Sy * (Half - 30.f), Top - 36.f), 20.f);   // brace
				if (bTopDeck) Log->AddInstance(FTransform(Q, W(Sx * (Half - 12.f), Sy * (Half - 12.f), Top + 105.f), FVector(0.16f, 0.16f, 2.1f))); // roof post
			}
		}

		// ---- Rails: two bars along each side, with a gap where the ladder comes up (local +X side) ----
		const float Gap = 85.f;
		auto Rail = [&](float Cx, float Cy, float Len, bool bAlongX)
		{
			for (float Z : { 45.f, 95.f })
			{
				const FVector S = bAlongX ? FVector(Len / 100.f, 0.07f, 0.07f) : FVector(0.07f, Len / 100.f, 0.07f);
				Beam->AddInstance(FTransform(Q, W(Cx, Cy, Top + Z), S));
			}
			const FVector HE = bAlongX ? FVector(Len * 0.5f, 6.f, 55.f) : FVector(6.f, Len * 0.5f, 55.f);
			Solid(W(Cx, Cy, Top + 55.f), HE);
		};
		Rail(0.f, Half - 8.f, 2.f * Half, true);
		Rail(0.f, -(Half - 8.f), 2.f * Half, true);
		Rail(-(Half - 8.f), 0.f, 2.f * Half, false);
		const float SideLen = Half - Gap;
		Rail(Half - 8.f, Gap + SideLen * 0.5f, SideLen, false);
		Rail(Half - 8.f, -(Gap + SideLen * 0.5f), SideLen, false);

		// ---- Lanterns on every deck; the roof and the warm light on the top one ----
		Lamp->AddInstance(FTransform(Q, W(Half - 40.f, Half - 40.f, Top + 80.f), FVector(0.14f, 0.14f, 0.2f)));
		Lamp->AddInstance(FTransform(Q, W(-(Half - 40.f), -(Half - 40.f), Top + 80.f), FVector(0.14f, 0.14f, 0.2f)));
		if (bTopDeck)
		{
			Roof->AddInstance(FTransform(Q, W(0, 0, Top + 210.f + 80.f), FVector(8.2f, 8.2f, 1.6f)));
		}
		{
			UPointLightComponent* Pl = NewObject<UPointLightComponent>(this);
			Pl->SetupAttachment(RootComponent);
			Pl->SetRelativeLocation(W(Half * 0.5f, 0, Top + 140.f));
			Pl->SetLightColor(FLinearColor(1.f, 0.62f, 0.25f));
			Pl->SetIntensity(bTopDeck ? 5000.f : 3000.f);
			Pl->SetAttenuationRadius(700.f);
			Pl->SetCastShadows(false);
			Pl->RegisterComponent();
		}

		// ---- Rope ladder: two ropes and a rung every 38 cm, from the edge of this deck down to the one below (or the ground) ----
		{
			const FVector P(Tree.X, Tree.Y, 0.f);
			const FVector LadderXY = P + Q.RotateVector(FVector(Half + 12.f, 0.f, 0.f));
			const float Zg = (Lv == 0) ? JeebTerrain::HeightAt(LadderXY.X, LadderXY.Y) : Top - JeebTerrain::FortSpacing;
			const float ZTop = Top + 30.f;
			const float Len = ZTop - Zg;
			for (int32 Sd = -1; Sd <= 1; Sd += 2)
			{
				const FVector RP = LadderXY + Q.RotateVector(FVector(0.f, Sd * 26.f, 0.f));
				Rope->AddInstance(FTransform(Q, FVector(RP.X, RP.Y, Zg + Len * 0.5f), FVector(0.04f, 0.04f, Len / 100.f)));
			}
			for (float Z = 30.f; Z < Len - 10.f; Z += 38.f)
			{
				Beam->AddInstance(FTransform(Q, FVector(LadderXY.X, LadderXY.Y, Zg + Z), FVector(0.05f, 0.64f, 0.05f)));
			}
			Beam->AddInstance(FTransform(Q, FVector(LadderXY.X, LadderXY.Y, ZTop - 6.f), FVector(0.1f, 0.7f, 0.1f)));   // the bar it hangs from
		}
	}
}

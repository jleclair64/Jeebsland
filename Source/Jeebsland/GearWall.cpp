#include "GearWall.h"
#include "Inventory.h"
#include "ItemModels.h"
#include "KnightModel.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Algo/Sort.h"

namespace
{
	// The armour board faces the start (+X). One block per tier, three columns in it (Light, Classic, Heavy); rows from the
	// top: helmet, shoulders, chest, gloves, boots, then the shield of the matching style (kite, round, tower).
	constexpr float ColW = 66.f, TierGap = 45.f, ItemSize = 30.f, DollScale = 0.55f;
	const float RowZ[6] = { 222.f, 188.f, 150.f, 112.f, 78.f, 62.f };   // (only the last, the shield, is still used)
	const int32 RowKind[5] = { JeebItems::K_Helmet, JeebItems::K_Shoulders, JeebItems::K_Chest, JeebItems::K_Gloves, JeebItems::K_Boots };
	const int32 ColStyle[3] = { JeebItems::AS_Light, JeebItems::AS_Classic, JeebItems::AS_Heavy };
	// The weapon board stands on the same line, to the right of the armour (seen from the start).
	constexpr float ArmourMidY = 450.f, WeaponGap = 60.f, WeaponColW = 82.f, WeaponRowH = 28.f, WeaponZ0 = 26.f, WeaponSize = 58.f;
	constexpr int32 ArrowCount = 20;
}

AGearWall::AGearWall()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;   // (built the same on every machine; pegs are named by number)
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AGearWall::BeginPlay()
{
	Super::BeginPlay();
	Build();
}

void AGearWall::AddBoard(const FVector& Centre, const FVector& Extent, const FLinearColor& Col)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	if (!Cube) return;
	UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(this);
	M->SetStaticMesh(Cube);
	M->SetupAttachment(Root);
	M->SetRelativeLocation(Centre);
	M->SetRelativeScale3D(Extent / 50.f);   // (the basic cube is 1 m across)
	M->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	M->SetCollisionResponseToAllChannels(ECR_Block);
	M->RegisterComponent();
	if (Mat) if (UMaterialInstanceDynamic* D = M->CreateDynamicMaterialInstance(0, Mat)) D->SetVectorParameterValue(TEXT("Color"), Col);
}

void AGearWall::AddSign(const FVector& Local, const FRotator& Rot, const FString& Text, float Size)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
	T->SetupAttachment(Root);
	T->SetRelativeLocationAndRotation(Local, Rot);
	T->SetHorizontalAlignment(EHTA_Center);
	T->SetVerticalAlignment(EVRTA_TextCenter);
	T->SetWorldSize(Size);
	T->SetTextRenderColor(FColor(240, 228, 200));
	T->SetText(FText::FromString(Text));
	T->RegisterComponent();
}

// One item on a peg: its own model (ItemModels.h), scaled so its longest side is Size, the longest side laid along the board
void AGearWall::AddItem(int32 Id, int32 Count, const FVector& Local, float Size, bool bLay)
{
	FPeg P;
	P.Id = Id;
	P.Count = Count;
	P.Pos = GetActorTransform().TransformPosition(Local);
	Pegs.Add(P);
	if (GetNetMode() == NM_DedicatedServer) return;

	TArray<JeebItemModel::FPart> Parts;
	JeebItemModel::Build(Id, Parts);
	if (Parts.Num() == 0) return;
	FBox Bx(ForceInit);
	for (const JeebItemModel::FPart& Pt : Parts) for (const FVector& V : Pt.Mesh.V) Bx += V;
	if (!Bx.IsValid) return;
	const FVector Sz = Bx.GetSize();
	const float Grow = Size / FMath::Max((float)Sz.GetMax(), 1.f);
	// Weapons and shields show their broad side: the thinnest side turns toward the viewer (+X). Long things (weapons) also lie along
	// the board (their longest side along Y); a thing modelled lying flat is stood up; the rest stay as modelled.
	int32 Ax[3] = { 0, 1, 2 };   // the item's own axes, thinnest first
	Algo::Sort(Ax, [&Sz](int32 a, int32 b) { return Sz[a] < Sz[b]; });
	FVector Img[3];              // where each of the item's own axes ends up
	Img[Ax[0]] = FVector::ForwardVector;
	if (bLay) { Img[Ax[2]] = FVector::RightVector; Img[Ax[1]] = FVector::UpVector; }
	else if (Ax[0] == 2) { Img[Ax[2]] = FVector::UpVector; Img[Ax[1]] = FVector::RightVector; }   // lying flat (a shield): stood up, its long side up
	else { Img[0] = FVector::ForwardVector; Img[1] = FVector::RightVector; Img[2] = FVector::UpVector; }   // already upright: kept as modelled
	FMatrix Rm(Img[0], Img[1], Img[2], FVector::ZeroVector);
	if (Rm.Determinant() < 0.f) { Img[Ax[0]] = -Img[Ax[0]]; Rm = FMatrix(Img[0], Img[1], Img[2], FVector::ZeroVector); }
	const FRotator Turn = Rm.Rotator();

	USceneComponent* Holder = NewObject<USceneComponent>(this);
	Holder->SetupAttachment(Root);
	Holder->SetRelativeLocationAndRotation(Local, Turn);
	Holder->SetRelativeScale3D(FVector(Grow));
	Holder->RegisterComponent();
	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	UMaterialInterface* MetalMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Metal.M_Metal"));
	const FVector Centre = Bx.GetCenter();
	for (const JeebItemModel::FPart& Pt : Parts)
	{
		UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(this);
		C->SetupAttachment(Holder);
		C->SetRelativeLocation(-Centre);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);   // (a few hundred small pieces: no shadows)
		C->RegisterComponent();
		TArray<FVector> N;
		KM::ComputeNormals(Pt.Mesh, N);
		TArray<int32> Tris;
		for (int32 t = 0; t + 2 < Pt.Mesh.I.Num(); t += 3)
		{
			Tris.Add(Pt.Mesh.I[t]); Tris.Add(Pt.Mesh.I[t + 1]); Tris.Add(Pt.Mesh.I[t + 2]);
			Tris.Add(Pt.Mesh.I[t]); Tris.Add(Pt.Mesh.I[t + 2]); Tris.Add(Pt.Mesh.I[t + 1]);
		}
		C->CreateMeshSection_LinearColor(0, Pt.Mesh.V, Tris, N, TArray<FVector2D>(), TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
		if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, (Pt.bMetal && MetalMat) ? MetalMat : BaseMat))
		{
			M->SetVectorParameterValue(TEXT("Color"), Pt.Color);
			if (Pt.bMetal && MetalMat) { M->SetScalarParameterValue(TEXT("Metallic"), 1.f); M->SetScalarParameterValue(TEXT("Roughness"), 0.28f); }
		}
	}
}

void AGearWall::Build()
{
	using namespace JeebItems;
	Pegs.Reset();
	const FLinearColor Wood(0.16f, 0.10f, 0.06f), Rail(0.30f, 0.20f, 0.12f);
	const int32 NT = NumTiersKnight;
	// Both boards face the start (+X) in one line. Seen from the start, -Y is on the right, so things run left to right with
	// falling Y: the armour board on the left (Tin to Diamond), the weapon board on the right.

	// ---- armour and shields ----
	const float TierW = 3.f * ColW + TierGap;
	const float ArmLeft = ArmourMidY + 0.5f * NT * TierW;
	AddBoard(FVector(-6.f, ArmourMidY, 140.f), FVector(4.f, 0.5f * NT * TierW + 20.f, 135.f), Wood);
	AddSign(FVector(0.f, ArmourMidY, 300.f), FRotator::ZeroRotator, TEXT("ARMOUR  -  E takes a copy"), 18.f);
	for (int32 t = 0; t < NT; ++t)
	{
		const float TL = ArmLeft - t * TierW - 0.5f * TierGap;   // the tier block's left edge
		AddBoard(FVector(-1.f, TL - 1.5f * ColW, 140.f), FVector(1.5f, 1.5f * ColW + 6.f, 128.f), Rail);
		AddSign(FVector(2.f, TL - 1.5f * ColW, 264.f), FRotator::ZeroRotator, TierName(t), 15.f);
		for (int32 c = 0; c < 3; ++c)
		{
			const float Y = TL - (c + 0.5f) * ColW;
			AddSign(FVector(2.f, Y, 246.f), FRotator::ZeroRotator, ArmourStyleRow(ColStyle[c]).Name, 8.f);
			AddMannequin(ColStyle[c], t, FVector(24.f, Y, 160.f));
			AddItem(ShieldId(c, t), 1, FVector(16.f, Y, RowZ[5]), ItemSize * 1.3f, false);
		}
	}

	// ---- weapons and arrows: a column per weapon kind, then arrows by tier and fire arrows; a row per tier from the ground up ----
	const int32 NumCols = (K_Rod - K_Sword + 1) + 2;
	const float Len = NumCols * WeaponColW;
	const float WLeft = ArmourMidY - 0.5f * NT * TierW - WeaponGap;
	AddBoard(FVector(-6.f, WLeft - 0.5f * Len, 140.f), FVector(4.f, 0.5f * Len + 20.f, 135.f), Wood);
	AddBoard(FVector(-1.f, WLeft - 0.5f * Len, 140.f), FVector(1.5f, 0.5f * Len + 6.f, 128.f), Rail);
	AddSign(FVector(0.f, WLeft - 0.5f * Len, 300.f), FRotator::ZeroRotator, TEXT("WEAPONS  -  E takes a copy"), 18.f);
	for (int32 col = 0; col < NumCols; ++col)
	{
		const float Y = WLeft - (col + 0.5f) * WeaponColW;
		const bool bArrows = col == NumCols - 2, bFire = col == NumCols - 1;
		const FString Head = bFire ? FString(TEXT("Fire arrows")) : bArrows ? FString(TEXT("Arrows")) : FString(KindName(K_Sword + col));
		AddSign(FVector(2.f, Y, WeaponZ0 + NT * WeaponRowH + 4.f), FRotator::ZeroRotator, Head, 9.f);
		for (int32 t = 0; t < NT; ++t)
		{
			if (bFire && t > 0) break;
			const int32 Id = bFire ? IdFireArrow : bArrows ? ArrowId(t) : GearId(K_Sword + col, t);
			AddItem(Id, (bFire || bArrows) ? ArrowCount : 1, FVector(16.f, Y, WeaponZ0 + t * WeaponRowH), (bArrows || bFire) ? WeaponSize * 0.7f : WeaponSize, true);
		}
	}
	for (int32 t = 0; t < NT; ++t)   // tier names down the left edge
		AddSign(FVector(2.f, WLeft + 22.f, WeaponZ0 + t * WeaponRowH), FRotator::ZeroRotator, TierName(t), 8.f);
}

int32 AGearWall::PegAt(const FVector& From, const FVector& Dir, const FVector& Near, float MaxDist) const
{
	int32 Best = -1;
	float BestOff = 14.f;   // how far off the view line a peg may be (cm)
	for (int32 i = 0; i < Pegs.Num(); ++i)
	{
		const FVector& P = Pegs[i].Pos;
		if (FVector::Dist(P, Near) > MaxDist) continue;
		const float Along = FVector::DotProduct(P - From, Dir);
		if (Along <= 0.f) continue;
		const float Off = FVector::Dist(P, From + Dir * Along);
		if (Off < BestOff) { BestOff = Off; Best = i; }
	}
	return Best;
}

// A small knight on the board wearing one style's whole suit at one tier: the style's own skinned model (KnightModel.h),
// standing in its rest pose, tinted like a worn suit. Each of the five pieces is a peg where it sits on him.
void AGearWall::AddMannequin(int32 Style, int32 Tier, const FVector& Local)
{
	using namespace KnightDesign;
	const struct { int32 Kind; FVector At; } Spots[5] = {
		{ JeebItems::K_Helmet, PivotPos(P_Head) + FVector(0.f, 0.f, 12.f) },
		{ JeebItems::K_Shoulders, PivotPos(P_ArmL) + FVector(0.f, 0.f, 4.f) },
		{ JeebItems::K_Chest, PivotPos(P_Upper) + FVector(0.f, 0.f, -12.f) },
		{ JeebItems::K_Gloves, PivotPos(P_ElbowR) + FVector(0.f, 0.f, -28.f) },
		{ JeebItems::K_Boots, PivotPos(P_AnkleL) + FVector(0.f, 0.f, -4.f) } };
	for (const auto& S : Spots)
	{
		FPeg P;
		P.Id = JeebItems::ArmourId(S.Kind, Style, Tier);
		P.Pos = GetActorTransform().TransformPosition(Local + S.At * DollScale + FVector(12.f, 0.f, 0.f));
		Pegs.Add(P);
	}
	if (GetNetMode() == NM_DedicatedServer) return;
	USkeletalMesh* M = LoadObject<USkeletalMesh>(nullptr, KnightModel::SetMeshPath(Style));
	if (!M) return;
	USkeletalMeshComponent* C = NewObject<USkeletalMeshComponent>(this);
	C->SetupAttachment(Root);
	C->SetRelativeLocation(Local);
	C->SetRelativeScale3D(FVector(DollScale));
	C->SetSkinnedAssetAndUpdate(M);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->RegisterComponent();
	UMaterialInterface* Mi = LoadObject<UMaterialInterface>(nullptr, KnightModel::SetMaterialPath(Style));
	for (int32 m = 0; m < C->GetNumMaterials(); ++m)
		if (UMaterialInstanceDynamic* D = C->CreateDynamicMaterialInstance(m, Mi ? Mi : C->GetMaterial(m)))
		{
			D->SetVectorParameterValue(TEXT("Tint"), KnightModel::TierTint(Tier));
			if (Style == KnightModel::Set_Classic) D->SetScalarParameterValue(TEXT("DesatGain"), KnightModel::ClassicGreyGain);
		}
}

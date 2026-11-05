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
	// The armour board (2026-11-03, made bigger and lower at the owner's word) faces the start (+X), behind it. One block per
	// tier, Tin to Diamond from left to right, three knights in it (Light, Classic, Heavy) standing on the floor at
	// DollScale of life size, a peg on each piece; each knight's shield (kite, round, tower) hangs on the board above him.
	constexpr float ColW = 115.f, TierGap = 55.f, DollScale = 1.f, DollZ = 96.f * DollScale + 4.f, ShieldZ = 262.f, ShieldSize = 50.f;
	const int32 ColStyle[3] = { JeebItems::AS_Light, JeebItems::AS_Classic, JeebItems::AS_Heavy };
	// The weapon board stands across the floor, WeaponX in front of the armour board, facing it (and the start): a column
	// per weapon kind, then arrows by tier and fire arrows; a row per tier from the floor up.
	constexpr float WeaponX = 1300.f, WeaponColW = 135.f, WeaponRowH = 40.f, WeaponZ0 = 30.f, WeaponSize = 95.f;
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
			if (Pt.bMetal && MetalMat) { M->SetScalarParameterValue(TEXT("Metallic"), 1.f); M->SetScalarParameterValue(TEXT("Roughness"), Pt.Rough); }
		}
	}
}

void AGearWall::Build()
{
	using namespace JeebItems;
	Pegs.Reset();
	const FLinearColor Wood(0.16f, 0.10f, 0.06f), Rail(0.30f, 0.20f, 0.12f);
	const int32 NT = NumTiersKnight;

	// ---- armour: seen from the start, -Y is on the right, so things run left to right with falling Y ----
	const float TierW = 3.f * ColW + TierGap;
	const float ArmLeft = 0.5f * NT * TierW;
	AddBoard(FVector(-6.f, 0.f, 185.f), FVector(4.f, ArmLeft + 20.f, 185.f), Wood);
	AddSign(FVector(0.f, 0.f, 352.f), FRotator::ZeroRotator, TEXT("ARMOUR  -  look at a piece, E takes a copy"), 30.f);
	for (int32 t = 0; t < NT; ++t)
	{
		const float TL = ArmLeft - t * TierW - 0.5f * TierGap;   // the tier block's left edge
		AddBoard(FVector(-1.f, TL - 1.5f * ColW, 168.f), FVector(1.5f, 1.5f * ColW + 8.f, 160.f), Rail);
		AddSign(FVector(2.f, TL - 1.5f * ColW, 312.f), FRotator::ZeroRotator, TierName(t), 34.f);
		for (int32 c = 0; c < 3; ++c)
		{
			const float Y = TL - (c + 0.5f) * ColW;
			AddSign(FVector(2.f, Y, 222.f), FRotator::ZeroRotator, ArmourStyleRow(ColStyle[c]).Name, 20.f);   // (just above his helmet)
			AddMannequin(ColStyle[c], t, FVector(45.f, Y, DollZ));
			AddItem(ShieldId(c, t), 1, FVector(14.f, Y, ShieldZ), ShieldSize, false);
		}
	}

	// ---- weapons and arrows: across the floor, facing back toward the armour (-X); seen from the start, +Y is on the right ----
	const FRotator Back(0.f, 180.f, 0.f);
	const int32 NumCols = (K_Rod - K_Sword + 1) + 3;   // (then capes, arrows by tier, fire arrows)
	const float Len = NumCols * WeaponColW;
	AddBoard(FVector(WeaponX + 6.f, 0.f, 245.f), FVector(4.f, 0.5f * Len + 110.f, 245.f), Wood);
	AddBoard(FVector(WeaponX + 1.f, 0.f, 200.f), FVector(1.5f, 0.5f * Len + 6.f, 190.f), Rail);
	AddSign(FVector(WeaponX, 0.f, 462.f), Back, TEXT("WEAPONS  -  look at one, E takes a copy"), 34.f);   // (its own strip above the column names)
	for (int32 col = 0; col < NumCols; ++col)
	{
		const float Y = -0.5f * Len + (col + 0.5f) * WeaponColW;
		const bool bCapes = col == NumCols - 3, bArrows = col == NumCols - 2, bFire = col == NumCols - 1;
		const FString Head = bFire ? FString(TEXT("Fire arrows")) : bArrows ? FString(TEXT("Arrows")) : bCapes ? FString(TEXT("Capes")) : FString(KindName(K_Sword + col));
		AddSign(FVector(WeaponX - 2.f, Y, WeaponZ0 + NT * WeaponRowH + 16.f), Back, Head, 22.f);
		for (int32 t = 0; t < NT; ++t)
		{
			if (bFire && t > 0) break;
			const int32 Id = bFire ? IdFireArrow : bArrows ? ArrowId(t) : bCapes ? GearId(K_Cape, t) : GearId(K_Sword + col, t);
			AddItem(Id, (bFire || bArrows) ? ArrowCount : 1, FVector(WeaponX - 14.f, Y, WeaponZ0 + t * WeaponRowH), (bArrows || bFire) ? WeaponSize * 0.7f : bCapes ? WeaponSize * 0.4f : WeaponSize, !bCapes);
		}
	}
	for (int32 t = 0; t < NT; ++t)   // tier names down the left edge
		AddSign(FVector(WeaponX - 2.f, -0.5f * Len - 70.f, WeaponZ0 + t * WeaponRowH), Back, TierName(t), 26.f);
}

int32 AGearWall::PegAt(const FVector& From, const FVector& Dir, const FVector& Near, float MaxDist) const
{
	// The peg nearest the middle of the screen by ANGLE (owner, 2026-11-03: aiming right at a piece often gave no prompt; the
	// old test allowed 14 cm off the camera's line, a sliver from 6 m back). Within 5 degrees, or 25 cm, whichever is wider.
	int32 Best = -1;
	float BestScore = 1.f;
	for (int32 i = 0; i < Pegs.Num(); ++i)
	{
		const FVector& P = Pegs[i].Pos;
		if (FVector::Dist(P, Near) > MaxDist) continue;
		const float Along = FVector::DotProduct(P - From, Dir);
		if (Along <= 0.f) continue;
		const float Off = FVector::Dist(P, From + Dir * Along);
		const float Allow = FMath::Max(25.f, Along * 0.0875f);   // (tan 5 degrees)
		const float Score = Off / Allow;
		if (Score < BestScore) { BestScore = Score; Best = i; }
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
	for (const auto& S : Spots) if (JeebItems::StyleHasPiece(S.Kind, Style))   // (no peg for a piece the style does not have)
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

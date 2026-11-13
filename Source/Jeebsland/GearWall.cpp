#include "GearWall.h"
#include "Inventory.h"
#include "ItemModels.h"
#include "JeebProps.h"
#include "KnightAnimator.h"
#include "Components/PoseableMeshComponent.h"
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
	constexpr float ColW = 160.f, TierGap = 55.f, DollScale = 1.f, DollZ = 96.f * DollScale + 4.f, ShieldZ = 135.f, ShieldSize = 50.f, ShieldSide = 60.f;
	// (owner, 2026-11-06: no item centre above about 2.2 m, so each shield hangs beside its knight, not above his helm)
	const int32 ColStyle[3] = { JeebItems::AS_Light, JeebItems::AS_Classic, JeebItems::AS_Heavy };
	// The weapon board stands across the floor, WeaponX in front of the armour board, facing it (and the start): a column
	// per weapon kind, then arrows by tier and fire arrows; a row per tier from the floor up.
	constexpr float WeaponX = 1300.f, WeaponColW = 135.f, WeaponRowH = 38.f, WeaponZ0 = 20.f, WeaponSize = 95.f;
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
	UStaticMesh* Prop = JeebItems::IsGear(Id) ? JeebProps::MeshFor(JeebItems::KindOf(Id)) : nullptr;   // (a model from /Game/Props)
	if (Parts.Num() == 0 && !Prop) return;
	FBox Bx(ForceInit);
	if (Prop) Bx = Prop->GetBoundingBox();
	else for (const JeebItemModel::FPart& Pt : Parts) for (const FVector& V : Pt.Mesh.V) Bx += V;
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
	if (Prop)
		if (UStaticMeshComponent* S = JeebProps::Make(this, Holder, JeebItems::KindOf(Id), JeebItems::VariantOf(Id))) { S->SetRelativeLocation(-Centre); S->SetCastShadow(false); }
	if (!Prop) for (const JeebItemModel::FPart& Pt : Parts)
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
	AddBoard(FVector(-6.f, 0.f, 150.f), FVector(4.f, ArmLeft + 20.f, 150.f), Wood);
	AddSign(FVector(0.f, 0.f, 282.f), FRotator::ZeroRotator, TEXT("ARMOUR  -  look at a piece, E takes a copy"), 30.f);
	for (int32 t = 0; t < NT; ++t)
	{
		const float TL = ArmLeft - t * TierW - 0.5f * TierGap;   // the tier block's left edge
		AddBoard(FVector(-1.f, TL - 1.5f * ColW, 140.f), FVector(1.5f, 1.5f * ColW + 8.f, 135.f), Rail);
		AddSign(FVector(2.f, TL - 1.5f * ColW, 250.f), FRotator::ZeroRotator, TierName(t), 34.f);
		for (int32 c = 0; c < 3; ++c)
		{
			const float Y = TL - (c + 0.5f) * ColW;
			AddSign(FVector(2.f, Y, 222.f), FRotator::ZeroRotator, ArmourStyleRow(ColStyle[c]).Name, 20.f);   // (just above his helmet)
			AddMannequin(ColStyle[c], t, FVector(45.f, Y, DollZ));
			AddItem(ShieldId(c, t), 1, FVector(14.f, Y - ShieldSide, ShieldZ), ShieldSize, false);
		}
	}

	// ---- weapons and arrows: across the floor, facing back toward the armour (-X); seen from the start, +Y is on the right ----
	const FRotator Back(0.f, 180.f, 0.f);
	const int32 CrossCol = K_Rod - K_Sword + 2;   // (a crossbow's prod is far taller than a row: they take two columns, turn about, each row's clear of the next)
	const int32 NumCols = (K_Rod - K_Sword + 1) + 3 + 3;   // (then the longbow and crossbow (two wide), capes, arrows by tier, fire arrows)
	auto WeaponOf = [](int32 col) { return col <= K_Rod - K_Sword ? K_Sword + col : col == K_Rod - K_Sword + 1 ? int32(K_Longbow) : int32(K_Crossbow); };
	const float Len = NumCols * WeaponColW;
	AddBoard(FVector(WeaponX + 6.f, 0.f, 165.f), FVector(4.f, 0.5f * Len + 110.f, 165.f), Wood);
	AddBoard(FVector(WeaponX + 1.f, 0.f, 140.f), FVector(1.5f, 0.5f * Len + 6.f, 135.f), Rail);
	AddSign(FVector(WeaponX, 0.f, 300.f), Back, TEXT("WEAPONS  -  look at one, E takes a copy"), 34.f);   // (its own strip above the column names)
	for (int32 col = 0; col < NumCols; ++col)
	{
		if (col == CrossCol + 1) continue;
		if (col <= K_Rod - K_Sword && IsRetiredKind(WeaponOf(col))) continue;   // (the katana is out of the game: its column stays empty)
		const bool bCrossbow = col == CrossCol;
		const float Y = -0.5f * Len + (col + 0.5f) * WeaponColW;
		const bool bCapes = col == NumCols - 3, bArrows = col == NumCols - 2, bFire = col == NumCols - 1;
		const FString Head = bFire ? FString(TEXT("Fire arrows")) : bArrows ? FString(TEXT("Arrows")) : bCapes ? FString(TEXT("Capes")) : FString(KindName(WeaponOf(col)));
		AddSign(FVector(WeaponX - 2.f, bCrossbow ? Y + 0.5f * WeaponColW : Y, WeaponZ0 + NT * WeaponRowH + 16.f), Back, Head, 22.f);
		for (int32 t = 0; t < NT; ++t)
		{
			if (bFire && t > 0) break;
			const int32 Id = bFire ? IdFireArrow : bArrows ? ArrowId(t) : bCapes ? GearId(K_Cape, t) : GearId(WeaponOf(col), t);
			if (bCrossbow) { AddItem(Id, 1, FVector(WeaponX - 14.f, Y + (t % 2 ? WeaponColW : 0.f), WeaponZ0 + 10.f + t * WeaponRowH), WeaponSize * 0.75f, true); continue; }
			AddItem(Id, (bFire || bArrows) ? ArrowCount : 1, FVector(WeaponX - 14.f, Y, WeaponZ0 + t * WeaponRowH), (bArrows || bFire) ? WeaponSize * 0.7f : bCapes ? WeaponSize * 0.4f : WeaponSize, !bCapes);
		}
	}
	for (int32 t = 0; t < NT; ++t)   // tier names down the left edge
		AddSign(FVector(WeaponX - 2.f, -0.5f * Len - 70.f, WeaponZ0 + t * WeaponRowH), Back, TierName(t), 26.f);

	// ---- everything else (owner, 2026-11-06, for testing): the BACK of the weapon board, facing away from the start (+X). Potions,
	// rings, amulets and the helm brush, then the things you gather. A band per two groups, each group under its own header, each
	// item with its name under it (rows run left to right, seen from +X, with falling Y). Each peg gives a full stack. ----
	{
		constexpr float FaceX = WeaponX + 30.f, BandPitch = 120.f, TopHeaderZ = 255.f, Spacing = 100.f, GroupGap = 70.f, ItemSize = 50.f;   // (two bands: the top item centre is 215 cm)
		AddBoard(FVector(WeaponX + 16.f, 0.f, 165.f), FVector(4.f, 920.f, 165.f), Wood);
		AddSign(FVector(WeaponX + 22.f, 0.f, 300.f), FRotator::ZeroRotator, TEXT("EVERYTHING ELSE  -  look at one, E takes a copy"), 32.f);   // (its own strip above the first band)
		struct FItem { int32 Id; bool bLay; };
		struct FGroup { FString Head; TArray<FItem> Items; };
		auto Many = [](int32 Kind, int32 Num) { TArray<FItem> A; for (int32 i = 0; i < Num; ++i) A.Add({ GearId(Kind, i), true }); return A; };
		auto Mats = [](std::initializer_list<EItemType> L, bool bLay) { TArray<FItem> A; for (EItemType T : L) A.Add({ MaterialId(T), bLay }); return A; };
		TArray<FItem> Potions;
		for (int32 p = 0; p < NumPotions(); ++p) Potions.Add({ PotionId(p), false });
		TArray<FItem> Charms = Many(K_Amulet, NumAmulets());
		Charms.Add({ GearId(K_Crest, 0), true });
		// (copper ore, coal and onyx are retired and raw pork has no source: left off)
		const TArray<TArray<FGroup>> Bands = {
			{ { TEXT("Potions"), Potions }, { TEXT("Rings"), Many(K_Ring, NumRings()) }, { TEXT("Amulets and helm brush"), Charms } },
			{ { TEXT("Logs"), Mats({ EItemType::Logs, EItemType::PineLogs, EItemType::RedwoodLogs }, true) }, { TEXT("Ores"), Mats({ EItemType::TinOre, EItemType::IronOre, EItemType::SilverOre, EItemType::GoldOre }, false) },
			  { TEXT("Gems"), Mats({ EItemType::Diamond, EItemType::Emerald }, false) }, { TEXT("Fish"), Mats({ EItemType::Perch, EItemType::Trout, EItemType::GoldenCarp }, true) },
			  { TEXT("Animal drops"), Mats({ EItemType::Hide, EItemType::Bones }, true) } } };
		for (int32 b = 0; b < Bands.Num(); ++b)
		{
			const float HeaderZ = TopHeaderZ - b * BandPitch;
			float Total = GroupGap * (Bands[b].Num() - 1);
			for (const FGroup& G : Bands[b]) Total += G.Items.Num() * Spacing;
			float Y = 0.5f * Total;   // (the band's left edge as seen from +X)
			for (const FGroup& G : Bands[b])
			{
				const float W = G.Items.Num() * Spacing;
				AddSign(FVector(WeaponX + 22.f, Y - 0.5f * W, HeaderZ), FRotator::ZeroRotator, G.Head, 22.f);
				for (int32 i = 0; i < G.Items.Num(); ++i)
				{
					const int32 Id = G.Items[i].Id;
					const float CY = Y - (i + 0.5f) * Spacing;
					AddItem(Id, FMath::Max(1, StackSize(Id)), FVector(FaceX, CY, HeaderZ - 40.f), ItemSize, G.Items[i].bLay);
					FString Nm = IsMaterial(Id) ? FString(MaterialName(MaterialType(Id))) : ItemName(Id);
					if (Nm.Len() > 11)   // (two short lines, so a name never runs into its neighbour)
					{
						int32 Cut = INDEX_NONE;
						for (int32 c = 0; c < Nm.Len(); ++c) if (Nm[c] == TEXT(' ') && (Cut == INDEX_NONE || FMath::Abs(c - Nm.Len() / 2) < FMath::Abs(Cut - Nm.Len() / 2))) Cut = c;
						if (Cut != INDEX_NONE) Nm = Nm.Left(Cut) + TEXT("\n") + Nm.Mid(Cut + 1);
					}
					AddSign(FVector(WeaponX + 22.f, CY, HeaderZ - 78.f), FRotator::ZeroRotator, Nm, 13.f);
				}
				Y -= W + GroupGap;
			}
		}
	}
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

// A knight on the board wearing a whole suit of one style at one tier, standing in his rest pose, tinted like a worn suit. Each of the five pieces is a peg where it sits on him.
// (The styles are the same model: they differ in the numbers, ArmourStyles.csv.)
void AGearWall::AddMannequin(int32 Style, int32 Tier, const FVector& Local)
{
	using namespace JeebAnim;
	const FClipLib& L = FClipLib::Get();
	const FVector Feet = Local - FVector(0.f, 0.f, DollZ - 1.f);
	const FClip* RestClip = L.Find(TEXT("Rest"));
	if (!RestClip) return;
	FPose Rest;
	Rest.Rot.SetNum(L.Parents.Num());
	L.Sample(*RestClip, 0.f, Rest, false);
	const struct { int32 Kind; int32 Bone; FVector Off; } Spots[5] = {
		{ JeebItems::K_Helmet, Head, FVector(0.f, 0.f, 12.f) },
		{ JeebItems::K_Shoulders, LArm, FVector(0.f, 0.f, 4.f) },
		{ JeebItems::K_Chest, Spine1, FVector::ZeroVector },
		{ JeebItems::K_Gauntlets, RHand, FVector::ZeroVector },
		{ JeebItems::K_Boots, LFoot, FVector::ZeroVector } };
	for (const auto& S : Spots) if (JeebItems::StyleHasPiece(S.Kind, Style))   // (no peg for a piece the style does not have)
	{
		FPeg P;
		P.Id = JeebItems::ArmourId(S.Kind, Style, Tier);
		P.Pos = GetActorTransform().TransformPosition(Feet + L.BindInGame(Rest, S.Bone).GetLocation() + S.Off + FVector(12.f, 0.f, 0.f));
		Pegs.Add(P);
	}
	if (GetNetMode() == NM_DedicatedServer) return;
	FKnightAnimator Statue;
	if (UPoseableMeshComponent* C = Statue.MakeMesh(this, Root))
	{
		C->SetCastShadow(false);
		for (int32 p = 0; p < Piece_Count; ++p) Statue.Wear(p, Tier);
		FInput In;
		In.bShotLog = true;   // (write the pose now, seen or not)
		Statue.Update(0.016f, In, GetActorTransform().TransformPosition(Feet), GetActorRotation().Yaw);
	}
}

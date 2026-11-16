#pragma once

#include "CoreMinimal.h"
#include "Inventory.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "KnightMesh.h"
#include "KnightDesign.h"

// Models brought in by Tools\BuildProps.ps1 (/Game/Props), in place of the code-built ones (2026-11-05; since 2026-11-12 the sword, axe, bow, pickaxe, longbow,
// crossbow, arrows, quiver, potions and amulets come from Meshy's community library: Docs/MeshyCommunityLibrary.md). Each is built around its grip, in the same
// frame the code-built ones are (WeaponDesign): the working end along -Z on the fist, X along the limbs on the bow's hold. M_Prop tints what the mesh's vertex
// colour marks: red the tier's (or the potion's) colour, green a second colour (an arrow's feathers, Tint2).
namespace JeebProps
{
	// Measured on the models (Tools\prop_clean.py prints them as "PROP mark"): the bows' string ends (X along the limbs, Z toward the string), and on the
	// crossbow (X toward the prod, Z up) the prod's ends and where its string sits, drawn back to the nut
	constexpr float BowTipX = 48.f, BowBrace = 16.5f;
	constexpr float LongTipX = 75.f, LongBrace = 20.5f;
	constexpr float CrossTipX = 31.5f, CrossTipY = 37.f, CrossZ = 6.5f, CrossNutX = 9.5f, CrossBoltLen = 40.f;
	// Arrows: the tip at the origin, the shaft back along -X, this long (the nock at -ArrowLen)
	constexpr float ArrowLen = 70.f;
	// The sword's point from the grip, and the axe's head (BladeReach: hit boxes and keeping the point off the floor)
	constexpr float SwordReach = 82.f;

	inline const TCHAR* PathFor(int32 Kind)
	{
		switch (Kind)
		{
		case JeebItems::K_Sword: return TEXT("/Game/Props/SM_Sword.SM_Sword");
		case JeebItems::K_Axe: return TEXT("/Game/Props/SM_Axe.SM_Axe");
		case JeebItems::K_Bow: return TEXT("/Game/Props/SM_Bow.SM_Bow");
		case JeebItems::K_Pickaxe: return TEXT("/Game/Props/SM_Pickaxe.SM_Pickaxe");
		case JeebItems::K_Longbow: return TEXT("/Game/Props/SM_Longbow.SM_Longbow");
		case JeebItems::K_Crossbow: return TEXT("/Game/Props/SM_Crossbow.SM_Crossbow");
		default: return nullptr;
		}
	}
	// Where a model built around its grip, working end along -Z, sits on the right wrist's pivot: exactly as the code-built weapons are placed
	// (KM::RotateEuler's order: X, then Y, then Z)
	inline FTransform FistPlace()
	{
		const FVector E = KM::DownTo(KnightDesign::BladeDir(1));
		const FQuat Q = FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(E.Z)) * FQuat(FVector::YAxisVector, FMath::DegreesToRadians(E.Y)) * FQuat(FVector::XAxisVector, FMath::DegreesToRadians(E.X));
		return FTransform(Q, KnightDesign::GripPos(false) - KnightDesign::PivotPos(KnightDesign::P_WristR));
	}
	inline UStaticMesh* Load(const TCHAR* Path) { return Path ? LoadObject<UStaticMesh>(nullptr, Path) : nullptr; }
	inline UStaticMesh* MeshFor(int32 Kind) { return Load(PathFor(Kind)); }

	// Potions (Potions.csv rows by their effect): healing the red bottle, vigour the purple one, swiftness and radar the green one with its liquid in
	// the potion's colour (the red and purple keep their own colours: their liquid isn't marked)
	inline UStaticMesh* PotionMesh(int32 Type)
	{
		const int32 E = JeebItems::PotionRow(FMath::Clamp<int32>(Type, 0, FMath::Max(0, JeebItems::NumPotions() - 1))).Effect;
		if (E == JeebItems::PE_Heal) return Load(TEXT("/Game/Props/SM_PotionRed.SM_PotionRed"));
		if (E == JeebItems::PE_Vigour) return Load(TEXT("/Game/Props/SM_PotionPurple.SM_PotionPurple"));
		return Load(TEXT("/Game/Props/SM_PotionGreen.SM_PotionGreen"));
	}
	inline UStaticMesh* ArrowMesh() { return Load(TEXT("/Game/Props/SM_Arrow.SM_Arrow")); }
	inline UStaticMesh* ArrowBackMesh() { return Load(TEXT("/Game/Props/SM_ArrowBack.SM_ArrowBack")); }   // (only the back 30 cm: what shows out of the quiver)
	inline UStaticMesh* QuiverMesh() { return Load(TEXT("/Game/Props/SM_Quiver.SM_Quiver")); }
	// Amulets by their row among the amulets in Accessories.csv
	inline UStaticMesh* AmuletMesh(int32 Variant)
	{
		static const TCHAR* const Paths[] = {
			TEXT("/Game/Props/SM_AmuletEmber.SM_AmuletEmber"),           // 0 Hawk's Eye Amulet (amber gem in gold)
			TEXT("/Game/Props/SM_AmuletScarab.SM_AmuletScarab"),         // 1 Emerald Necklace (the green scarab)
			TEXT("/Game/Props/SM_AmuletVoidSeer.SM_AmuletVoidSeer"),     // 2 Chaos Pendant (violet stone)
			TEXT("/Game/Props/SM_AmuletRune.SM_AmuletRune"),             // 3 Guardian Amulet (blue rune medallion)
			TEXT("/Game/Props/SM_AmuletEmberHeart.SM_AmuletEmberHeart"), // 4 Vitality Amulet (red heart stone)
			TEXT("/Game/Props/SM_AmuletCrystal.SM_AmuletCrystal") };     // 5 Astral Prism Pendant
		return Load(Paths[FMath::Clamp<int32>(Variant, 0, int32(UE_ARRAY_COUNT(Paths)) - 1)]);
	}

	inline void Tint(UStaticMeshComponent* C, const FLinearColor& Col, float Metal = 0.f, float Rough = 0.5f)
	{
		if (!C) return;
		UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(C->GetMaterial(0));
		if (!M) M = C->CreateDynamicMaterialInstance(0);
		if (!M) return;
		M->SetVectorParameterValue(TEXT("Tint"), Col);
		M->SetScalarParameterValue(TEXT("TintMetal"), Metal);
		M->SetScalarParameterValue(TEXT("TintRough"), Rough);
	}
	// The second colour, on what the vertex colour's green marks (an arrow's feathers)
	inline void Tint2(UStaticMeshComponent* C, const FLinearColor& Col)
	{
		if (!C) return;
		UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(C->GetMaterial(0));
		if (!M) M = C->CreateDynamicMaterialInstance(0);
		if (M) M->SetVectorParameterValue(TEXT("Tint2"), Col);
	}
	// An arrow's head and feathers in its kind's colours (ArrowTipColor / ArrowFeatherColor: steel, fire, and the tiers)
	inline void TintArrow(UStaticMeshComponent* C, int32 ArrowId)
	{
		const FLinearColor Tip = JeebItems::ArrowTipColor(ArrowId);
		Tint(C, Tip, Tip.GetMax() > 1.f ? 0.f : 0.6f, 0.35f);
		Tint2(C, JeebItems::ArrowFeatherColor(ArrowId));
	}
	inline UStaticMeshComponent* MakeStatic(AActor* Owner, USceneComponent* Attach, UStaticMesh* Mesh)
	{
		if (!Mesh || !Owner) return nullptr;
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
		C->SetupAttachment(Attach);
		C->SetStaticMesh(Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->RegisterComponent();
		return C;
	}
	// A gear item's model, in its tier's colour (null when this kind has no such model)
	inline UStaticMeshComponent* Make(AActor* Owner, USceneComponent* Attach, int32 Kind, int32 Tier)
	{
		if (!Owner || Owner->GetNetMode() == NM_DedicatedServer) return nullptr;   // (nothing to see on a server)
		UStaticMeshComponent* C = MakeStatic(Owner, Attach, MeshFor(Kind));
		if (!C) return nullptr;
		const bool bShiny = JeebItems::TierRow(Tier).Metal > 0.5f;
		Tint(C, JeebItems::Palette(Tier, 0), bShiny ? 0.8f : 0.2f, JeebItems::TierRough(Tier));
		return C;
	}
}

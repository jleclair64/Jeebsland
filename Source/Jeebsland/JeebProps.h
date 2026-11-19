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
		case JeebItems::K_Staff: return TEXT("/Game/Props/SM_Staff.SM_Staff");   // (2026-11-18: Meshy models; orb end along -Z)
		case JeebItems::K_Wand: return TEXT("/Game/Props/SM_Wand.SM_Wand");
		case JeebItems::K_Club: return TEXT("/Game/Props/SM_Club.SM_Club");   // (2026-11-19: Meshy, for the troll; head along -Z)
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

	// Shields (2026-11-16, the owner's picks from Meshy's library: Art/Props/Sources.md): one model per style, 0 kite, 1 round, 2 tower (JeebItems::StyleOf).
	// Each is built face along +X, up +Z, its back at the origin (Tools\prop_clean.py SHIELD); ShieldPlace puts it where the code-built shield of that
	// style was, in KnightDesign's shield frame (the shield part's own frame: the part follows the arm, or lies on his back, and the model with it).
	// The code-built parts stay, hidden, for what arrows stick in (GetShieldParts) and on the server.
	inline UStaticMesh* ShieldMesh(int32 Style)
	{
		static const TCHAR* const Paths[] = { TEXT("/Game/Props/SM_ShieldKite.SM_ShieldKite"), TEXT("/Game/Props/SM_ShieldRound.SM_ShieldRound"), TEXT("/Game/Props/SM_ShieldTower.SM_ShieldTower") };
		return Load(Paths[FMath::Clamp<int32>(Style, 0, 2)]);
	}
	// Where each style's back sits (X), its middle (Y, Z), in KnightDesign's shield frame: the old shields' back plane was X 14, their middles Y -41 and
	// Z -15 (kite), -6 (round), -16 (tower). The tower's handle stands 5 cm off its back, so it goes that much nearer the arm.
	inline FTransform ShieldPlace(int32 Style)
	{
		static const FVector At[] = { FVector(13.5f, -41.f, -15.f), FVector(13.5f, -41.f, -6.f), FVector(9.5f, -41.f, -16.f) };
		return FTransform(At[FMath::Clamp<int32>(Style, 0, 2)]);
	}
	// A gear item's model by its id (a shield's style is in its id), or null
	inline UStaticMesh* MeshForItem(int32 Id)
	{
		const int32 K = JeebItems::KindOf(Id);
		return K == JeebItems::K_Shield ? ShieldMesh(JeebItems::StyleOf(Id)) : MeshFor(K);
	}

	// (every material slot: a model can come in with more than one, and its faces need not be on the first; 2026-11-16 the baked longbow drew plain grey)
	inline void Tint(UStaticMeshComponent* C, const FLinearColor& Col, float Metal = 0.f, float Rough = 0.5f)
	{
		if (!C) return;
		for (int32 i = 0; i < C->GetNumMaterials(); ++i)
		{
			UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(C->GetMaterial(i));
			if (!M) M = C->CreateDynamicMaterialInstance(i);
			if (!M) continue;
			M->SetVectorParameterValue(TEXT("Tint"), Col);
			M->SetScalarParameterValue(TEXT("TintMetal"), Metal);
			M->SetScalarParameterValue(TEXT("TintRough"), Rough);
		}
	}
	// The second colour, on what the vertex colour's green marks (an arrow's feathers)
	inline void Tint2(UStaticMeshComponent* C, const FLinearColor& Col)
	{
		if (!C) return;
		for (int32 i = 0; i < C->GetNumMaterials(); ++i)
		{
			UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(C->GetMaterial(i));
			if (!M) M = C->CreateDynamicMaterialInstance(i);
			if (M) M->SetVectorParameterValue(TEXT("Tint2"), Col);
		}
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
	inline void TintTier(UStaticMeshComponent* C, int32 Tier)
	{
		Tint(C, JeebItems::Palette(Tier, 0), JeebItems::TierRow(Tier).Metal > 0.5f ? 0.8f : 0.2f, JeebItems::TierRough(Tier));
	}
	// The same by item id (a shield: its style's model)
	inline UStaticMeshComponent* MakeItem(AActor* Owner, USceneComponent* Attach, int32 Id)
	{
		if (!Owner || Owner->GetNetMode() == NM_DedicatedServer) return nullptr;
		UStaticMeshComponent* C = MakeStatic(Owner, Attach, MeshForItem(Id));
		if (C) TintTier(C, JeebItems::VariantOf(Id));
		return C;
	}
	// A shield's model on one of its code-built parts (the plate of that style), in the tier's colour; the caller hides the code-built parts of that
	// style (still there for arrows). Null on a server or with no model.
	inline UStaticMeshComponent* AddShieldModel(AActor* Owner, USceneComponent* Plate, int32 Style, int32 Tier)
	{
		if (!Owner || !Plate || Owner->GetNetMode() == NM_DedicatedServer) return nullptr;
		UStaticMeshComponent* C = MakeStatic(Owner, Plate, ShieldMesh(Style));
		if (!C) return nullptr;
		C->SetRelativeTransform(ShieldPlace(Style));
		TintTier(C, Tier);
		return C;
	}
}

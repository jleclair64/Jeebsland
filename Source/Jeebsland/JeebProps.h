#pragma once

#include "CoreMinimal.h"
#include "Inventory.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

// Models made in Meshy and brought in by Tools\BuildProps.ps1 (/Game/Props), in place of the code-built ones (2026-11-05). Each is built around its grip, in the same
// frame the code-built ones are (WeaponDesign): the working end along -Z on the fist, X along the limbs on the bow's hold. M_Prop tints what the mesh's vertex colour marks.
namespace JeebProps
{
	// Measured on the models (Tools\\prop_clean.py makes them): the longbow's tips, and on the crossbow (X toward the prod, Z up) the prod's ends and where its string sits, drawn back to the nut
	constexpr float LongTipX = 76.f, LongBrace = 16.f;
	constexpr float CrossTipX = 31.f, CrossTipY = 37.f, CrossZ = 4.5f, CrossNutX = 12.f, CrossBoltLen = 34.f;
	inline const TCHAR* PathFor(int32 Kind)
	{
		switch (Kind)
		{
		case JeebItems::K_Pickaxe: return TEXT("/Game/Props/SM_Pickaxe.SM_Pickaxe");
		case JeebItems::K_Longbow: return TEXT("/Game/Props/SM_Longbow.SM_Longbow");
		case JeebItems::K_Crossbow: return TEXT("/Game/Props/SM_Crossbow.SM_Crossbow");
		default: return nullptr;
		}
	}
	inline UStaticMesh* MeshFor(int32 Kind)
	{
		const TCHAR* P = PathFor(Kind);
		return P ? LoadObject<UStaticMesh>(nullptr, P) : nullptr;
	}
	inline UStaticMesh* PotionMesh() { return LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Props/SM_Potion.SM_Potion")); }
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
	// A gear item's model, in its tier's colour (null when this kind has no such model)
	inline UStaticMeshComponent* Make(AActor* Owner, USceneComponent* Attach, int32 Kind, int32 Tier)
	{
		UStaticMesh* Mesh = MeshFor(Kind);
		if (!Mesh || !Owner || Owner->GetNetMode() == NM_DedicatedServer) return nullptr;   // (nothing to see on a server)
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
		C->SetupAttachment(Attach);
		C->SetStaticMesh(Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->RegisterComponent();
		const bool bShiny = JeebItems::TierRow(Tier).Metal > 0.5f;
		Tint(C, JeebItems::Palette(Tier, 0), bShiny ? 0.8f : 0.2f, JeebItems::TierRough(Tier));
		return C;
	}
}

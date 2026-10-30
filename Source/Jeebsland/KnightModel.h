#pragma once

#include "CoreMinimal.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "KnightDesign.h"
#include "Inventory.h"

// The knight's armour as a real model (made in Meshy from the owner's concept sheet, 2026-10-29). Tools\BuildKnight.ps1 cuts
// it into pieces, one per joint, and writes them to Content/Knight/KnightParts.bin (staged with the packaged game, see
// DefaultGame.ini); M_Knight paints them with the model's own picture and tints the metal for the armour tier.
// Each piece is in the same "capsule space" as KnightDesign (cm, feet at Z = -96) and hangs off KnightDesign::PivotPos.
// The helmet, body, shoulders, gloves and boots slots use these pieces; the shields, the cape, the Red Helm Brush and the
// jewellery are still built in KnightDesign.h. If the file is missing the knight falls back to the code-built armour.
namespace KnightModel
{
	struct FPart
	{
		int32 Pivot = 0, Slot = 0;
		TArray<FVector> V, N;
		TArray<FVector2D> UV;
		TArray<int32> I;
	};

	inline const TArray<FPart>& Parts()
	{
		static TArray<FPart> Cache;
		static bool bLoaded = false;
		if (bLoaded) return Cache;
		bLoaded = true;
		TArray<uint8> B;
		if (!FFileHelper::LoadFileToArray(B, *(FPaths::ProjectContentDir() / TEXT("Knight/KnightParts.bin")))) return Cache;
		int64 Pos = 0;
		auto Read = [&](void* Dst, int64 Bytes) { if (Bytes < 0 || Pos + Bytes > B.Num()) return false; FMemory::Memcpy(Dst, B.GetData() + Pos, Bytes); Pos += Bytes; return true; };
		char Magic[4];
		int32 Ver = 0, Num = 0;
		if (!Read(Magic, 4) || FMemory::Memcmp(Magic, "JKNT", 4) != 0 || !Read(&Ver, 4) || Ver != 1 || !Read(&Num, 4)) return Cache;
		for (int32 p = 0; p < Num; ++p)
		{
			FPart P;
			int32 NV = 0, NI = 0;
			if (!Read(&P.Pivot, 4) || !Read(&P.Slot, 4) || !Read(&NV, 4) || !Read(&NI, 4) || P.Pivot < 0 || P.Pivot >= KnightDesign::P_Count) { Cache.Empty(); return Cache; }
			P.V.SetNum(NV); P.N.SetNum(NV); P.UV.SetNum(NV);
			for (int32 i = 0; i < NV; ++i)
			{
				float F[8];
				if (!Read(F, sizeof(F))) { Cache.Empty(); return Cache; }
				P.V[i] = FVector(F[0], F[1], F[2]);
				P.N[i] = FVector(F[3], F[4], F[5]);
				P.UV[i] = FVector2D(F[6], F[7]);
			}
			P.I.SetNum(NI);
			if (!Read(P.I.GetData(), int64(NI) * 4)) { Cache.Empty(); return Cache; }
			for (int32 Ix : P.I) if (Ix < 0 || Ix >= NV) { Cache.Empty(); return Cache; }
			Cache.Add(MoveTemp(P));
		}
		return Cache;
	}

	// The slots the model pieces stand in for
	inline bool IsModelSlot(int32 Slot)
	{
		using namespace KnightDesign;
		return Slot == SlotHelmet || Slot == SlotBody || Slot == SlotShoulders || Slot == SlotBoots || Slot == SlotGloves;
	}

	// The model is painted steel: the tint that turns its metal into a tier's plate colour
	inline FLinearColor TierTint(int32 Tier)
	{
		const FLinearColor Steel = JeebItems::Palette(3, 0), C = JeebItems::Palette(Tier, 0);
		return FLinearColor(FMath::Min(C.R / Steel.R, 2.f), FMath::Min(C.G / Steel.G, 2.f), FMath::Min(C.B / Steel.B, 2.f), 1.f);
	}

	// One piece as a mesh component on its joint (its shape is in capsule space, so it is moved back by the joint's position)
	inline UProceduralMeshComponent* MakeComp(UObject* Outer, USceneComponent* Attach, const FPart& P)
	{
		UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(Outer);
		C->SetupAttachment(Attach);
		C->SetRelativeLocation(-KnightDesign::PivotPos(P.Pivot));
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->RegisterComponent();
		TArray<int32> Tris;
		Tris.Reserve(P.I.Num() * 2);
		for (int32 t = 0; t + 2 < P.I.Num(); t += 3)
		{
			// both windings, as everywhere else in the game, so the outside shows whichever way the engine treats as front
			Tris.Add(P.I[t]); Tris.Add(P.I[t + 1]); Tris.Add(P.I[t + 2]);
			Tris.Add(P.I[t]); Tris.Add(P.I[t + 2]); Tris.Add(P.I[t + 1]);
		}
		C->CreateMeshSection_LinearColor(0, P.V, Tris, P.N, P.UV, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
		return C;
	}

	inline void Paint(UPrimitiveComponent* C, int32 Tier)
	{
		UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Knight/M_Knight.M_Knight"));
		if (!C || !Mat) return;
		if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, Mat))
			M->SetVectorParameterValue(TEXT("Tint"), TierTint(Tier));
	}
}

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
		TArray<FProcMeshTangent> T;   // (for the normal map)
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
		if (!Read(Magic, 4) || FMemory::Memcmp(Magic, "JKNT", 4) != 0 || !Read(&Ver, 4) || Ver != 2 || !Read(&Num, 4)) return Cache;
		for (int32 p = 0; p < Num; ++p)
		{
			FPart P;
			int32 NV = 0, NI = 0;
			if (!Read(&P.Pivot, 4) || !Read(&P.Slot, 4) || !Read(&NV, 4) || !Read(&NI, 4) || P.Pivot < 0 || P.Pivot >= KnightDesign::P_Count) { Cache.Empty(); return Cache; }
			P.V.SetNum(NV); P.N.SetNum(NV); P.UV.SetNum(NV); P.T.SetNum(NV);
			for (int32 i = 0; i < NV; ++i)
			{
				float F[12];
				if (!Read(F, sizeof(F))) { Cache.Empty(); return Cache; }
				P.V[i] = FVector(F[0], F[1], F[2]);
				P.N[i] = FVector(F[3], F[4], F[5]);
				P.UV[i] = FVector2D(F[6], F[7]);
				P.T[i] = FProcMeshTangent(FVector(F[8], F[9], F[10]), F[11] < 0.f);
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

	// The model is painted steel: the tint that turns its metal into a tier's plate colour. Its brightness is pulled halfway
	// back towards the steel's, so warm tiers (bronze) stay bright metal instead of going dark brown
	inline FLinearColor TierTint(int32 Tier)
	{
		const FLinearColor Steel = JeebItems::Palette(3, 0), C = JeebItems::Palette(Tier, 0);
		FLinearColor T(C.R / Steel.R, C.G / Steel.G, C.B / Steel.B, 1.f);
		const float Lum = FMath::Max(0.2126f * T.R + 0.7152f * T.G + 0.0722f * T.B, 0.05f);
		T *= 1.f / FMath::Sqrt(Lum);
		return FLinearColor(FMath::Min(T.R, 2.f), FMath::Min(T.G, 2.f), FMath::Min(T.B, 2.f), 1.f);
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
		C->CreateMeshSection_LinearColor(0, P.V, Tris, P.N, P.UV, TArray<FLinearColor>(), P.T, false);
		return C;
	}

	// The body inside the armour: dark padded cloth, so wherever the plates part at a joint (a swinging knee, a turning
	// ankle) you see the man inside instead of the hollow shell. A rounded rod along each limb, a ball in every joint, and
	// a chest, hips and neck; all sit inside the model's plates at rest.
	inline void AddCores(TArray<KnightDesign::FDef>& Out)
	{
		using namespace KnightDesign;
		auto Def = [&](int32 Pivot) -> FDef&
		{
			FDef D; D.Pivot = Pivot; D.Slot = SlotBody; D.Role = EArmorRole::Under;
			D.bFixedColor = true; D.Fixed = FLinearColor(0.035f, 0.035f, 0.04f);
			Out.Add(D);
			return Out.Last();
		};
		// a rod of radius R from A to B with rounded ends (built pointing down, turned to run from A to B)
		auto Rod = [](KM::FMD& M, const FVector& A, const FVector& B, double R)
		{
			const double L = FVector::Dist(A, B);
			KM::Lathe(M, Prof({ 0, R, R * 0.71, R * 0.71, R, 0, R, -L, R * 0.71, -L - R * 0.71, 0, -L - R }), 14, 1.0, 1.0, KM::Xf(A, KM::DownTo(B - A)));
		};
		{ FDef& D = Def(P_Upper); KM::Ellipsoid(D.Mesh, 16.0, 15.5, 19.0, 1.0, 1.0, 20, 14, KM::Xf(FVector(-3, 0, 2))); }      // chest
		{ FDef& D = Def(P_Body); KM::Ellipsoid(D.Mesh, 14.0, 15.0, 11.0, 1.0, 1.0, 20, 12, KM::Xf(FVector(-1, 0, -26))); }    // hips
		{ FDef& D = Def(P_Head); Rod(D.Mesh, FVector(-1, 0, 32), FVector(-1, 0, 16), 7.0); }                                   // neck
		for (int32 sd = -1; sd <= 1; sd += 2)
		{
			const int32 Arm = sd < 0 ? P_ArmL : P_ArmR, Elb = sd < 0 ? P_ElbowL : P_ElbowR, Hip = sd < 0 ? P_HipL : P_HipR, Knee = sd < 0 ? P_KneeL : P_KneeR, Ank = sd < 0 ? P_AnkleL : P_AnkleR;
			{ FDef& D = Def(Arm); Rod(D.Mesh, PivotPos(Arm), PivotPos(Elb), 4.5); }
			{ FDef& D = Def(P_Upper); KM::Blob(D.Mesh, 9.0, 8.0, 9.0, KM::Xf(PivotPos(Arm) + FVector(0, -sd * 2.0, 0))); }   // the shoulder joint, seen when the arm swings high
			{ FDef& D = Def(Arm); KM::Blob(D.Mesh, 8.0, 8.0, 8.0, KM::Xf(PivotPos(Arm))); }
			{ FDef& D = Def(Elb); Rod(D.Mesh, PivotPos(Elb), WristPos(sd), 4.8); }
			{ FDef& D = Def(Hip); Rod(D.Mesh, PivotPos(Hip), PivotPos(Knee), 7.0); }
			{ FDef& D = Def(Knee); Rod(D.Mesh, PivotPos(Knee), PivotPos(Ank), 6.0); }
			{ FDef& D = Def(Ank); KM::Ellipsoid(D.Mesh, 9.0, 5.5, 5.5, 1.0, 1.0, 14, 10, KM::Xf(PivotPos(Ank) + FVector(2.0, 0, -4.0))); }   // the foot inside the sabaton
		}
	}

	inline void Paint(UPrimitiveComponent* C, int32 Tier)
	{
		UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Knight/M_Knight.M_Knight"));
		if (!C || !Mat) return;
		if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, Mat))
			M->SetVectorParameterValue(TEXT("Tint"), TierTint(Tier));
	}
}

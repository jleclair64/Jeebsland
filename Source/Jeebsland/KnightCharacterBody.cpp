#include "KnightCharacter.h"
#include "KnightShieldRest.h"
#include "Messages.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputModifiers.h"
#include "EnhancedInputComponent.h"
#include "SkillComponent.h"
#include "ResourceNode.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Terrain.h"
#include "GrassField.h"
#include "Canopy.h"
#include "Pig.h"
#include "Inventory.h"
#include "Npc.h"
#include "Horse.h"
#include "Cottage.h"
#include "Loot.h"
#include "Pond.h"
#include "HealthComponent.h"
#include "Projectile.h"
#include "Combat.h"
#include "WeaponDesign.h"
#include "ProceduralMeshComponent.h"
#include "KnightModel.h"
#include "Misc/ConfigCacheIni.h"

// ---------------- The knight's body parts ----------------
void AKnightCharacter::BuildKnight()
{
	USceneComponent* Pivots[KnightDesign::P_Count] = {};
	Pivots[KnightDesign::P_Body] = BodyRoot;     Pivots[KnightDesign::P_Upper] = UpperPivot;  Pivots[KnightDesign::P_Head] = HeadPivot;
	Pivots[KnightDesign::P_Plume] = PlumePivot;  Pivots[KnightDesign::P_CapeU] = CapeUpper;   Pivots[KnightDesign::P_CapeL] = CapeLower;
	Pivots[KnightDesign::P_ArmL] = ArmPivotL;    Pivots[KnightDesign::P_ArmR] = ArmPivot;     Pivots[KnightDesign::P_ElbowL] = ElbowL;
	Pivots[KnightDesign::P_ElbowR] = ElbowR;     Pivots[KnightDesign::P_HipL] = HipL;         Pivots[KnightDesign::P_HipR] = HipR;
	Pivots[KnightDesign::P_KneeL] = KneeL;       Pivots[KnightDesign::P_KneeR] = KneeR;
	Pivots[KnightDesign::P_AnkleL] = AnkleL;     Pivots[KnightDesign::P_AnkleR] = AnkleR;

	TArray<KnightDesign::FDef> Defs;
	KnightDesign::Build(Defs);

	// The armour itself comes from the Meshy model when it is there; the code-built pieces for those slots are left out
	const TArray<KnightModel::FPart>& Model = KnightModel::Parts();
	if (Model.Num() > 0)
	{
		TArray<KnightDesign::FDef> Cores;
		KnightModel::AddCores(Cores);
		for (const KnightDesign::FDef& D : Cores)
		{
			if (!Pivots[D.Pivot]) continue;
			FArmorPart P;
			P.Comp = MakeMeshComp(Pivots[D.Pivot], D.Pivot, D.Mesh);
			P.Slot = D.Slot; P.Role = D.Role; P.Fixed = D.Fixed; P.bFixedColor = true;
			ArmorParts.Add(P);
		}
	}
	for (const KnightModel::FPart& MP : Model)
	{
		if (!Pivots[MP.Pivot]) continue;
		FArmorPart P;
		P.Comp = KnightModel::MakeComp(this, Pivots[MP.Pivot], MP);
		P.Slot = MP.Slot;
		P.bModel = true;
		ArmorParts.Add(P);
	}
	for (const KnightDesign::FDef& D : Defs)
	{
		if (D.Mesh.V.Num() == 0 || !Pivots[D.Pivot]) continue;
		if (Model.Num() > 0 && KnightModel::IsModelSlot(D.Slot)) continue;
		UProceduralMeshComponent* C = MakeMeshComp(Pivots[D.Pivot], D.Pivot, D.Mesh);

		FArmorPart P;
		P.Comp = C;
		P.Slot = D.Slot; P.Role = D.Role; P.MinTier = D.MinTier; P.MaxTier = D.MaxTier;
		P.Fixed = D.Fixed; P.bFixedMetal = D.bFixedMetal; P.Shade = D.Shade; P.bBare = D.bBare;
		P.Style = D.Style; P.bFixedColor = D.bFixedColor;
		ArmorParts.Add(P);
	}
}

UProceduralMeshComponent* AKnightCharacter::MakeMeshComp(USceneComponent* Attach, int32 PivotId, const KM::FMD& Md)
{
	UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(this);
	C->SetupAttachment(Attach);
	// The shape is modelled around the knight's centre; move it back so it sits right on its pivot
	C->SetRelativeLocation(-KnightDesign::PivotPos(PivotId));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->RegisterComponent();

	TArray<FVector> Normals;
	KM::ComputeNormals(Md, Normals);
	TArray<int32> Tris;
	Tris.Reserve(Md.I.Num() * 2);
	for (int32 t = 0; t + 2 < Md.I.Num(); t += 3)
	{
		// Both windings, so the outside shows whichever way the engine treats as front
		Tris.Add(Md.I[t]); Tris.Add(Md.I[t + 1]); Tris.Add(Md.I[t + 2]);
		Tris.Add(Md.I[t]); Tris.Add(Md.I[t + 2]); Tris.Add(Md.I[t + 1]);
	}
	TArray<FVector2D> UV;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	C->CreateMeshSection_LinearColor(0, Md.V, Tris, Normals, UV, Colors, Tangents, false);
	return C;
}

// Where cape point i of the top row is pinned: across the back of the shoulders, curving round them a little
FVector AKnightCharacter::CapePin(int32 i) const
{
	const float Y = FMath::Lerp(-16.f, 16.f, float(i) / (CapeNX - 1));
	// across the tops of the shoulders (the upper body is modelled in body space, 41 cm above the chest pivot)
	return UpperPivot->GetComponentTransform().TransformPosition(FVector(-25.5f + 0.012f * Y * Y, Y, 66.f));   // (5 cm higher, 2026-10-30)
}

// A little cloth simulation (Verlet points held together by distance links): pinned at the shoulders, pulled down by
// gravity, slowed by the air, pushed back and lifted by the wind of running, and kept off the knight's back.
void AKnightCharacter::TickCape(float Dt)
{
	if (!CapeCloth || !UpperPivot || !CapeCloth->IsVisible()) { bCapeInit = false; return; }
	const int32 N = CapeNX * CapeNY;
	const float Length = 68.f, WTop = 36.f, WBot = 56.f;   // from the shoulders to the upper thigh
	const float DV = Length / (CapeNY - 1);
	auto Idx = [](int32 i, int32 j) { return j * CapeNX + i; };
	const FTransform Up = UpperPivot->GetComponentTransform();
	const FVector Back = -Up.GetUnitAxis(EAxis::X);

	if (!bCapeInit || CapeP.Num() != N)
	{
		CapeP.SetNum(N); CapePrev.SetNum(N);
		for (int32 j = 0; j < CapeNY; ++j)
			for (int32 i = 0; i < CapeNX; ++i)
			{
				const FVector P = CapePin(i) - FVector(0.f, 0.f, j * DV) + Back * (j * 0.8f);
				CapeP[Idx(i, j)] = P; CapePrev[Idx(i, j)] = P;
			}
		bCapeInit = true;
		bCapeRootSet = false;
		CapeAccum = 0.f;
	}

	// Wind of moving: streams the cape back and lifts it when running, with a ripple running down it
	const FVector Vel = GetVelocity();
	const float Speed = FVector(Vel.X, Vel.Y, 0.f).Size();
	const FVector Fwd = GetActorForwardVector();
	const FVector Side = GetActorRightVector();
	CapeTime += Dt;

	constexpr float H = 1.f / 90.f;   // fixed small steps, so it behaves the same at any frame rate
	CapeAccum = FMath::Min(CapeAccum + Dt, 8.f * H);
	// The cloth is carried up and down with most of the body's own rise and fall (a jump, a landing), so it only has to
	// swing with what is left: no sudden stretching and snapping back when the knight leaps
	{
		const FVector Root = GetActorLocation();
		if (bCapeRootSet)
		{
			const FVector Carry(0.f, 0.f, (Root.Z - CapeRootPrev.Z) * 0.8f);   // up and down only: running still trails it behind
			for (int32 k = CapeNX; k < N; ++k) { CapeP[k] += Carry; CapePrev[k] += Carry; }
		}
		CapeRootPrev = Root; bCapeRootSet = true;
	}
	auto Link = [&](int32 a, int32 b, float Rest, float Stiff)
	{
		const FVector D = CapeP[b] - CapeP[a];
		const float L = D.Size();
		if (L < 1e-3f) return;
		const FVector Fix = D * ((L - Rest) / L) * 0.5f * Stiff;
		const bool bPinA = a < CapeNX, bPinB = b < CapeNX;
		if (bPinA && bPinB) return;
		if (bPinA) CapeP[b] -= Fix * 2.f;
		else if (bPinB) CapeP[a] += Fix * 2.f;
		else { CapeP[a] += Fix; CapeP[b] -= Fix; }
	};
	while (CapeAccum >= H)
	{
		CapeAccum -= H;
		for (int32 j = 1; j < CapeNY; ++j)
			for (int32 i = 0; i < CapeNX; ++i)
			{
				const int32 k = Idx(i, j);
				const float Down = float(j) / (CapeNY - 1);
				// A very heavy cloth (twice the old weight): slow, low ripples, and the wind of running barely lifts it (gravity wins)
				const FVector Ripple = (Side * FMath::Sin(CapeTime * 3.6f - j * 0.7f + i * 0.5f) * 0.6f + Back * FMath::Sin(CapeTime * 2.6f - j * 0.5f) * 0.4f)
				                     * (7.f + 0.17f * Speed) * Down;
				const FVector Force = FVector(0.f, 0.f, -2200.f) - Fwd * (0.4f * Speed) + FVector(0.f, 0.f, 0.06f * Speed) + Ripple;
				const FVector V = (CapeP[k] - CapePrev[k]) * 0.9f;   // more drag: it swings slowly and settles quickly
				CapePrev[k] = CapeP[k];
				CapeP[k] += V + Force * H * H;
			}
		// Keep its shape: links across, down, diagonally, and every other point down (stiffer, so it folds rather than crumples)
		for (int32 It = 0; It < 8; ++It)
		{
			for (int32 i = 0; i < CapeNX; ++i) CapeP[Idx(i, 0)] = CapePin(i);
			for (int32 j = 0; j < CapeNY; ++j)
			{
				const float W = FMath::Lerp(WTop, WBot, float(j) / (CapeNY - 1));
				const float DH = W / (CapeNX - 1);
				for (int32 i = 0; i < CapeNX; ++i)
				{
					if (i + 1 < CapeNX) Link(Idx(i, j), Idx(i + 1, j), DH, 1.f);
					if (j + 1 < CapeNY) Link(Idx(i, j), Idx(i, j + 1), DV, 1.f);
					if (i + 1 < CapeNX && j + 1 < CapeNY)
					{
						const float Diag = FMath::Sqrt(DH * DH + DV * DV);
						Link(Idx(i, j), Idx(i + 1, j + 1), Diag, 0.6f);
						Link(Idx(i + 1, j), Idx(i, j + 1), Diag, 0.6f);
					}
					if (j + 2 < CapeNY) Link(Idx(i, j), Idx(i, j + 2), 2.f * DV, 0.35f);
				}
			}
			// Never through the knight: stay behind his back and the backs of his legs
			for (int32 k = CapeNX; k < N; ++k)
			{
				FVector L = Up.InverseTransformPosition(CapeP[k]);
				float Lim = -27.5f;   // the back of the armour (it stands out to about -23.6 at the shoulder blades), plus the cloth's 3 cm thickness
				if (bStowed && L.Z > -9.f) Lim = -38.f;   // and over the shield when it is on the back
				if (L.X > Lim) { L.X = Lim; CapeP[k] = Up.TransformPosition(L); }
			}
		}
	}

	// Draw it: positions, smooth normals facing away from the back, and two triangles per cell
	TArray<FVector> V; TArray<FVector> Nrm; TArray<FVector2D> UV;
	V.Reserve(N); Nrm.Reserve(N); UV.Reserve(N);
	for (int32 j = 0; j < CapeNY; ++j)
		for (int32 i = 0; i < CapeNX; ++i)
		{
			V.Add(CapeP[Idx(i, j)]);
			const FVector Dx = CapeP[Idx(FMath::Min(i + 1, CapeNX - 1), j)] - CapeP[Idx(FMath::Max(i - 1, 0), j)];
			const FVector Dy = CapeP[Idx(i, FMath::Min(j + 1, CapeNY - 1))] - CapeP[Idx(i, FMath::Max(j - 1, 0))];
			FVector Nm = FVector::CrossProduct(Dx, Dy).GetSafeNormal();
			if (FVector::DotProduct(Nm, Back) < 0.f) Nm = -Nm;
			// ease the shading normal from frame to frame: the folds still catch the light, but it no longer flickers as the
			// little ripples come and go
			{
				const int32 k = Idx(i, j);
				if (CapeNrmPrev.Num() != N) CapeNrmPrev.Init(Nm, N);
				Nm = FMath::Lerp(CapeNrmPrev[k], Nm, FMath::Clamp(Dt * 7.f, 0.f, 1.f)).GetSafeNormal();
				CapeNrmPrev[k] = Nm;
			}
			Nrm.Add(Nm);
			UV.Add(FVector2D(float(i) / (CapeNX - 1), float(j) / (CapeNY - 1)));
		}
	// a thick, heavy cloth: a second layer 1.6 cm inside, joined round the edges, so it never reads as a sheet of paper
	V.Reserve(2 * N); Nrm.Reserve(2 * N); UV.Reserve(2 * N);
	for (int32 k = 0; k < N; ++k) { const FVector P = V[k] - Nrm[k] * 3.0f, Nk = -Nrm[k]; const FVector2D Uk = UV[k]; V.Add(P); Nrm.Add(Nk); UV.Add(Uk); }
	if (!bCapeMeshMade)
	{
		TArray<int32> Tris;
		for (int32 j = 0; j + 1 < CapeNY; ++j)
			for (int32 i = 0; i + 1 < CapeNX; ++i)
			{
				const int32 A = Idx(i, j), B = Idx(i + 1, j), C = Idx(i, j + 1), D = Idx(i + 1, j + 1);
				Tris.Append({ A, B, D, A, D, C });
				Tris.Append({ N + A, N + D, N + B, N + A, N + C, N + D });
			}
		auto Rim = [&](int32 P, int32 Q) { Tris.Append({ P, Q, N + Q, P, N + Q, N + P }); };
		for (int32 i = 0; i + 1 < CapeNX; ++i) { Rim(Idx(i, 0), Idx(i + 1, 0)); Rim(Idx(i + 1, CapeNY - 1), Idx(i, CapeNY - 1)); }
		for (int32 j = 0; j + 1 < CapeNY; ++j) { Rim(Idx(0, j + 1), Idx(0, j)); Rim(Idx(CapeNX - 1, j), Idx(CapeNX - 1, j + 1)); }
		CapeCloth->CreateMeshSection_LinearColor(0, V, Tris, Nrm, UV, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
		if (CapeMid) CapeCloth->SetMaterial(0, CapeMid);
		bCapeMeshMade = true;
	}
	else
	{
		CapeCloth->UpdateMeshSection_LinearColor(0, V, Nrm, UV, TArray<FLinearColor>(), TArray<FProcMeshTangent>());
	}
}

// ---------------- What you wear and hold ----------------
namespace
{
	bool PartIsMetal(int32 Tier, EArmorRole Role)
	{
		return Tier >= 0 && (Role == EArmorRole::Plate || Role == EArmorRole::Trim || Role == EArmorRole::BootBody);
	}
	// The kind of gear that belongs in each knight slot
	int32 KindForSlot(int32 Slot)
	{
		switch (Slot)
		{
		case KnightDesign::SlotHelmet:    return JeebItems::K_Helmet;
		case KnightDesign::SlotBody:      return JeebItems::K_Chest;
		case KnightDesign::SlotShoulders: return JeebItems::K_Shoulders;
		case KnightDesign::SlotBoots:     return JeebItems::K_Boots;
		case KnightDesign::SlotCape:      return JeebItems::K_Cape;
		case KnightDesign::SlotGloves:    return JeebItems::K_Gloves;
		case KnightDesign::SlotBrush:     return JeebItems::K_Crest;
		case KnightDesign::SlotRingR: case KnightDesign::SlotRingL: return JeebItems::K_Ring;
		case KnightDesign::SlotAmulet:    return JeebItems::K_Amulet;
		default:                          return JeebItems::K_Shield;
		}
	}
}

void AKnightCharacter::ApplyPartMaterial(FArmorPart& P, const FLinearColor& Color, bool bMetal)
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	// Metal parts use the shiny M_Metal if it exists (made once by MakeMetal.bat); otherwise plain colour
	UMaterialInterface* Metal = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Metal.M_Metal"));
	UMaterialInterface* Mat = (bMetal && Metal) ? Metal : Base;
	if (UMaterialInstanceDynamic* M = P.Comp->CreateDynamicMaterialInstance(0, Mat))
	{
		M->SetVectorParameterValue(TEXT("Color"), Color);
		if (bMetal && Metal)
		{
			M->SetScalarParameterValue(TEXT("Metallic"), 1.0f);
			M->SetScalarParameterValue(TEXT("Roughness"), 0.2f);   // polished plate
		}
	}
}

void AKnightCharacter::ApplyArmorSlot(int32 Slot)
{
	if (!Skills) return;
	const int32 Kind = KindForSlot(Slot);
	int32 Id = (Slot == KnightDesign::SlotShield) ? (ShieldActive() ? Skills->Worn[Kind] : 0) : Skills->Worn[Kind];
	if (Slot == KnightDesign::SlotBrush)   // the brush is worn in one of the three miscellaneous slots
	{
		Id = 0;
		for (int32 M : { Skills->Worn[JeebItems::K_Ring], Skills->RingB, Skills->Worn[JeebItems::K_Amulet] })
			if (JeebItems::IsGear(M) && JeebItems::KindOf(M) == JeebItems::K_Crest) Id = M;
	}
	if (Slot == KnightDesign::SlotRingR || Slot == KnightDesign::SlotRingL || Slot == KnightDesign::SlotAmulet)   // jewellery in the miscellaneous slots
	{
		Id = 0;
		int32 Rings = 0;
		for (int32 M : { Skills->Worn[JeebItems::K_Ring], Skills->RingB, Skills->Worn[JeebItems::K_Amulet] })
		{
			if (!JeebItems::IsGear(M)) continue;
			const int32 MK = JeebItems::KindOf(M);
			if (MK == JeebItems::K_Amulet && Slot == KnightDesign::SlotAmulet) Id = M;
			if (MK == JeebItems::K_Ring) { ++Rings; if ((Rings == 1 && Slot == KnightDesign::SlotRingR) || (Rings == 2 && Slot == KnightDesign::SlotRingL)) Id = M; }
		}
	}
	const bool bWorn = Id != 0;
	const int32 Var = bWorn ? JeebItems::VariantOf(Id) : 0;   // tier (or cape colour)
	for (FArmorPart& P : ArmorParts)
	{
		if (P.Slot != Slot) continue;

		// Underclothes, belt and boots are always there; everything else shows only while worn
		bool bAlways = false;
		if (Slot == KnightDesign::SlotBody) bAlways = (P.Role == EArmorRole::Under || P.Role == EArmorRole::Leather);
		if (Slot == KnightDesign::SlotBoots) bAlways = (P.Role == EArmorRole::BootBody || P.Role == EArmorRole::Under);
		bool bVisible;
		if (P.bBare) bVisible = !bWorn;
		else if (bAlways) bVisible = true;
		else bVisible = bWorn && (Slot == KnightDesign::SlotCape || (Var >= P.MinTier && Var <= P.MaxTier));
		if (Slot == KnightDesign::SlotShield && bWorn && P.Style != JeebItems::StyleOf(Id)) bVisible = false;   // only the worn shield style
		if (P.bModel) bVisible = true;   // (armour is never taken off; the model has no bare-skin version)
		P.Comp->SetVisibility(bVisible);
		if (!bVisible) continue;
		if (P.bModel) { KnightModel::Paint(P.Comp, Var); continue; }

		FLinearColor Color;
		bool bMetal = false;
		if (P.bBare || P.bFixedColor) Color = P.Fixed;
		else if (Slot == KnightDesign::SlotCape)
		{
			Color = JeebItems::CapeColor(Var);
			Color.R *= P.Shade; Color.G *= P.Shade; Color.B *= P.Shade;
		}
		else
		{
			Color = JeebItems::Palette(Var, static_cast<int32>(P.Role));
			Color.R *= P.Shade; Color.G *= P.Shade; Color.B *= P.Shade;
			bMetal = PartIsMetal(Var, P.Role);
		}
		ApplyPartMaterial(P, Color, bMetal);
	}
	if (Slot == KnightDesign::SlotCape && CapeCloth)
	{
		CapeCloth->SetVisibility(bWorn);
		if (CapeMid && bWorn) CapeMid->SetVectorParameterValue(TEXT("Color"), JeebItems::CapeColor(Var));
	}
}

void AKnightCharacter::RefreshEquipment()
{
	for (int32 Slot = 0; Slot < KnightDesign::NumSlots; ++Slot) ApplyArmorSlot(Slot);
	RebuildHeld();
}

void AKnightCharacter::RebuildHeld()
{
	for (UProceduralMeshComponent* C : HeldComps)
	{
		if (C) C->DestroyComponent();
	}
	HeldComps.Reset();
	EaseComps.Reset();
	HeldElbow = 25.f;
	if (!Skills) return;
	const int32 Id = Skills->SelectedItem();
	if (!JeebItems::IsGear(Id)) return;
	const int32 K = JeebItems::KindOf(Id), T = JeebItems::VariantOf(Id);
	if (!JeebItems::IsWeaponKind(K)) return;
	HeldElbow = WeaponDesign::IdleElbow(K);

	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	UMaterialInterface* Metal = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Metal.M_Metal"));

	auto Make = [&](USceneComponent* Attach, int32 PivotId, const KM::FXf& Place)
	{
		TArray<WeaponDesign::FWPart> Parts;
		WeaponDesign::Build(K, Parts, Place);
		for (const WeaponDesign::FWPart& WP : Parts)
		{
			if (WP.Mesh.V.Num() == 0) continue;
			UProceduralMeshComponent* C = MakeMeshComp(Attach, PivotId, WP.Mesh);
			HeldComps.Add(C);
			FLinearColor Color;
			bool bMetal = false;
			if (WP.bFixed)
			{
				Color = WP.Fixed;
				Color.R *= WP.Glow; Color.G *= WP.Glow; Color.B *= WP.Glow;
			}
			else
			{
				Color = JeebItems::Palette(T, WP.Role);
				bMetal = PartIsMetal(T, static_cast<EArmorRole>(WP.Role));
			}
			UMaterialInterface* Mat = (bMetal && Metal) ? Metal : Base;
			if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, Mat))
			{
				M->SetVectorParameterValue(TEXT("Color"), Color);
				if (bMetal && Metal) { M->SetScalarParameterValue(TEXT("Metallic"), 0.9f); M->SetScalarParameterValue(TEXT("Roughness"), 0.28f); }
			}
		}
	};

	if (bStowed)
	{
		// On the back, diagonally: the grip over the right shoulder and the blade down to the left hip, lying flat against
		// the back (under the shield). Built at the grip, then turned flat (width across the back) and tilted 35 degrees.
		const int32 First = HeldComps.Num();
		Make(UpperPivot, KnightDesign::P_Upper, KM::Xf(FVector::ZeroVector));
		const FQuat Q = FQuat(FVector::XAxisVector, FMath::DegreesToRadians(-35.f)) * FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(90.f));
		for (int32 i = First; i < HeldComps.Num(); ++i)
			HeldComps[i]->SetRelativeLocationAndRotation(FVector(-27.5f, 14.f, 41.f + 16.f), Q);   // (clear of the Meshy model's back)   // chest pivot sits 41 below the body's centre
		return;
	}
	// Blades are held the way a hand really grips a sword: the grip runs across the closed fist (the Meshy model's fists close
	// side to side, knuckles forward), so the blade leaves from the thumb side, forward and in, with the pommel showing on the
	// little-finger side. Other things (tools, bows, spears, staffs) keep the old hold, pointing down and forward.
	const bool bBlade = K == JeebItems::K_Sword || K == JeebItems::K_Broadsword || K == JeebItems::K_Katana;
	const KM::FXf Tilt = bBlade ? KM::Xf(KnightDesign::GripPos(false), KM::DownTo(KnightDesign::BladeDir(1)))
	                            : KM::Xf(KnightDesign::GripPos(false), FVector(0, (K == JeebItems::K_Bow) ? -28.0 : -20.0, 0));
	{
		const int32 First = HeldComps.Num();
		Make(ElbowR, KnightDesign::P_ElbowR, Tilt);
		EaseKind = K;
		if (K == JeebItems::K_Sword || K == JeebItems::K_Broadsword || K == JeebItems::K_Katana || K == JeebItems::K_Staff)   // held at ease
			for (int32 i = First; i < HeldComps.Num(); ++i) EaseComps.Add(HeldComps[i]);
	}
	if (K == JeebItems::K_Katana) Make(ElbowL, KnightDesign::P_ElbowL, KM::Xf(KnightDesign::GripPos(true), KM::DownTo(KnightDesign::BladeDir(-1))));
}

// The shield on the arm, or on the back: turned to face backwards, its inner face just behind the sword on the back,
// centred on the spine with its point down by the hips (the cape is kept outside it, see TickCape)
void AKnightCharacter::PlaceShield(bool bOnBack)
{
	for (FArmorPart& P : ArmorParts)
	{
		if (P.Slot != KnightDesign::SlotShield || !P.Comp) continue;
		if (bOnBack)
		{
			P.Comp->AttachToComponent(UpperPivot, FAttachmentTransformRules::KeepRelativeTransform);
			P.Comp->SetRelativeLocationAndRotation(FVector(-14.f, -41.f, 41.f), FRotator(0.f, 180.f, 0.f));
		}
		else
		{
			P.Comp->AttachToComponent(ArmPivotL, FAttachmentTransformRules::KeepRelativeTransform);   // HoldShield poses it
		}
	}
}

// The resting shield, and the left arm that holds it. The shield stays where it was placed (in the shoulder's frame, so it
// swings with the arm); the elbow bends up and turns in so the forearm lies flat behind it, a hand's depth back.
// (declared in KnightShieldRest.h; the left elbow in Tick uses it too)
const FShieldRest& ShieldRest()
{
	static const FShieldRest R = []()
	{
		FShieldRest Out;
		const FVector PA = KnightDesign::PivotPos(KnightDesign::P_ArmL), PE = KnightDesign::PivotPos(KnightDesign::P_ElbowL);
		const FVector W = KnightDesign::WristPos(-1);
		// where the shield sat (strapped to the forearm bent 15 degrees forward and turned 22 degrees in): keep exactly that
		{
			const FVector Fore = (PE - W).GetSafeNormal();
			const FVector Up = FVector(0.573, 0.0, 0.819);
			const FVector Want = FVector(0.676, -0.70, -0.181).GetSafeNormal();
			const FVector N = (Want - Up * FVector::DotProduct(Want, Up)).GetSafeNormal();
			const FQuat Q = FQuat(N, FMath::DegreesToRadians(-30.f)) * FRotationMatrix::MakeFromXZ(N, Up).ToQuat();
			const FVector Strap(14.0, -41.0, -8.0);
			const FVector Target = -Fore * 10.0 + N * 11.5 + FVector(5.0, 4.0, 0.0);   // (8 cm further down the forearm since the model's fists, 2026-10-29)
			const FTransform InElbow(Q, Target - Q.RotateVector(Strap));
			const FTransform ElbowInArm(FRotator(15.f, 0.f, -22.f).Quaternion(), PE - PA);
			Out.InArm = InElbow * ElbowInArm;
			// then turned 30 degrees about the upright through its strap (anticlockwise seen from above)
			const FVector Pv = Out.InArm.TransformPosition(Strap);
			Out.InArm = Out.InArm * FTransform(-Pv) * FTransform(FQuat(FVector::UpVector, FMath::DegreesToRadians(-30.f))) * FTransform(Pv);
		}
		// the left fist goes to the middle of the shield's inside, a hand's depth behind it, with the forearm lying flat
		// along the back of the shield: try every elbow bend, swing and turn, keep the best
		const FVector Face = Out.InArm.TransformVectorNoScale(FVector(1, 0, 0)).GetSafeNormal();
		const FVector Goal = Out.InArm.TransformPosition(FVector(14.0, -41.0, -10.0)) - Face * 6.0;
		const FVector F = KnightDesign::FistPos(-1);
		float Best = 1e9f;
		for (float P = 0.f; P <= 140.f; P += 4.f)
			for (float Yw = -60.f; Yw <= 90.f; Yw += 4.f)
				for (float Rl = -60.f; Rl <= 60.f; Rl += 4.f)
				{
					const FQuat Q = FRotator(P, Yw, Rl).Quaternion();
					const FVector Dir = Q.RotateVector((W - PE).GetSafeNormal());
					const FVector Fist = (PE - PA) + Q.RotateVector(F - PE);
					const float Cost = (Fist - Goal).SizeSquared() + FMath::Square(FVector::DotProduct(Dir, Face)) * 300.f;
					if (Cost < Best) { Best = Cost; Out.ElbowPitch = P; Out.ElbowYaw = Yw; Out.ElbowRoll = Rl; }
				}
		return Out;
	}();
	return R;
}

void AKnightCharacter::HoldShield()
{
	if (bStowed || !ElbowL || !ArmPivotL) return;
	// Raised: where the guard always put it (in front of the arm, turning with the shoulder); at rest: as worked out above
	const FTransform Guard(-KnightDesign::PivotPos(KnightDesign::P_ArmL));
	FTransform Now;
	Now.Blend(ShieldRest().InArm, Guard, BlockAlpha);
	for (FArmorPart& P : ArmorParts)
	{
		if (P.Slot != KnightDesign::SlotShield || !P.Comp) continue;
		if (P.Comp->GetAttachParent() != ArmPivotL) P.Comp->AttachToComponent(ArmPivotL, FAttachmentTransformRules::KeepRelativeTransform);
		P.Comp->SetRelativeTransform(Now);
	}
}

void AKnightCharacter::HoldWeaponAtEase(float Dt, bool bBusy)
{
	EaseAlpha = FMath::FInterpTo(EaseAlpha, (bBusy || bStowed || EaseComps.Num() == 0) ? 0.f : 1.f, Dt, 9.f);
	if (EaseComps.Num() == 0) return;
	// The blade leaves the hand pointing down and forward (built that way); at ease it turns in so the tip rests in front
	// of the knight, between his feet (worked out for the arm's relaxed bend)
	static const FQuat SwordQ = []()
	{
		// (since 2026-10-30 a blade stays exactly as gripped: turning it in the hand pulled the grip out of the fist)
		return FQuat::Identity;
	}();
	// The wizard staff stands upright, held round the middle of its shaft with the arm a little out, its foot on the ground
	// (worked out for the staff's relaxed elbow bend, see WeaponDesign::IdleElbow)
	static const FVector StaffHead = KM::RotateEuler(FVector(0, 0, -1), FVector(0, -20, 0)).GetSafeNormal();
	static const FQuat StaffQ = []()
	{
		const float Bend = WeaponDesign::IdleElbow(JeebItems::K_Staff) + 4.f;   // the elbow, plus the arms' relaxed lean
		const FVector Up(FMath::Sin(FMath::DegreesToRadians(Bend)), 0.0, FMath::Cos(FMath::DegreesToRadians(Bend)));   // world up, in the forearm's frame
		return FQuat::FindBetweenNormals(StaffHead, Up);
	}();
	const bool bStaff = EaseKind == JeebItems::K_Staff;
	const FQuat Q = FQuat::Slerp(FQuat::Identity, bStaff ? StaffQ : SwordQ, EaseAlpha);
	const FVector Slide = bStaff ? -StaffHead * (40.f * EaseAlpha) : FVector::ZeroVector;   // slid down through the hand to grip the middle
	const FVector G = KnightDesign::GripPos(false), PE = KnightDesign::PivotPos(KnightDesign::P_ElbowR);
	for (UProceduralMeshComponent* C : EaseComps)
		if (C) C->SetRelativeLocationAndRotation(G - PE - Q.RotateVector(G) + Q.RotateVector(Slide), Q);
}


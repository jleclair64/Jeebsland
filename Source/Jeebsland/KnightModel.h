#pragma once

#include "CoreMinimal.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"
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
		if (KnightDesign::IsFootPivot(P.Pivot))   // the feet are drawn smaller (KnightDesign::FootScale)
		{
			TArray<FVector> V = P.V, N = P.N;
			for (int32 i = 0; i < V.Num(); ++i) { N[i] = KnightDesign::FootNormal(P.Pivot, P.V[i], P.N[i]); V[i] = KnightDesign::FootVertex(P.Pivot, P.V[i]); }
			C->CreateMeshSection_LinearColor(0, V, Tris, N, P.UV, TArray<FLinearColor>(), P.T, false);
			return C;
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
			{ FDef& D = Def(Hip); Rod(D.Mesh, PivotPos(Hip), PivotPos(Knee), 8.5); }
			{ FDef& D = Def(Knee); Rod(D.Mesh, PivotPos(Knee), PivotPos(Ank), 7.5); }
			{ FDef& D = Def(Ank); KM::Ellipsoid(D.Mesh, 9.0, 5.5, 5.5, 1.0, 1.0, 14, 10, KM::Xf(PivotPos(Ank) + FVector(2.0, 0, -4.0))); }   // the foot inside the sabaton
		}
	}

	inline void Paint(UPrimitiveComponent* C, int32 Tier, int32 MatIndex = 0)
	{
		UMaterialInterface* Mat = C && C->IsA<UPoseableMeshComponent>() ? LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Knight/M_KnightSkin.M_KnightSkin"))
			: LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Knight/M_Knight.M_Knight"));   // (the skinned knight: one material slot per armour set)
		if (!C || !Mat) return;
		if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(MatIndex, Mat))
			M->SetVectorParameterValue(TEXT("Tint"), TierTint(Tier));
	}

	// ---- The skinned knight (2026-11-01) ----
	// /Game/Knight/SK_Knight (Tools\BuildKnight.ps1) is the same model as ONE mesh on a skeleton whose bones sit on the knight's
	// joints, weighted across each joint so the plates and cloth bend there instead of parting. Every pose is still worked out
	// on the joint pivots as before; UpdateSkin copies them onto the bones each frame, so all moves carry over. Its material
	// slots are the armour sets (Helm, Body, Shoulders, Boots, Gloves), tinted one by one for their tiers. When the asset is
	// missing the knight falls back to the cut pieces above.
	inline const TCHAR* BoneName(int32 Pivot)
	{
		using namespace KnightDesign;
		switch (Pivot)
		{
		case P_Body: return TEXT("Body");     case P_Upper: return TEXT("Upper");   case P_Head: return TEXT("Head");
		case P_ArmL: return TEXT("ArmL");     case P_ArmR: return TEXT("ArmR");     case P_ElbowL: return TEXT("ElbowL");
		case P_ElbowR: return TEXT("ElbowR"); case P_WristR: return TEXT("WristR"); case P_HipL: return TEXT("HipL");
		case P_HipR: return TEXT("HipR");     case P_KneeL: return TEXT("KneeL");   case P_KneeR: return TEXT("KneeR");
		case P_AnkleL: return TEXT("AnkleL"); case P_AnkleR: return TEXT("AnkleR");
		default: return nullptr;
		}
	}
	inline const TCHAR* SlotMaterial(int32 Slot)
	{
		using namespace KnightDesign;
		switch (Slot)
		{
		case SlotHelmet: return TEXT("Helm"); case SlotBody: return TEXT("Body"); case SlotShoulders: return TEXT("Shoulders");
		case SlotBoots: return TEXT("Boots"); case SlotGloves: return TEXT("Gloves");
		default: return nullptr;
		}
	}

	// ---- Armour styles (2026-11-03) ----
	// Every armour style is its own skinned model on the SAME skeleton (Tools\BuildKnight.ps1): Classic (the gold armour),
	// Light (the crusader tunic), Heavy (the original knight, SK_Knight) and the chain mail worn under all of them. The mail is
	// the leader: each of its slots shows only while that slot is empty (it is pulled just inside the plate as well) and its
	// bones are the ones posed each frame; a style's mesh is made the first time a piece of that style is worn and simply
	// follows the mail's bones (SetLeaderPoseComponent: no second bone copy). Each worn slot shows only its own section of its
	// style's mesh, tinted for its tier; a style with nothing shown is hidden.
	enum ESet : int32 { Set_Classic, Set_Light, Set_Heavy, Set_Mail, Set_Count };   // (the first three = JeebItems::EArmourStyle)
	inline const TCHAR* SetMeshPath(int32 Set)
	{
		static const TCHAR* P[Set_Count] = { TEXT("/Game/Knight/SK_Knight_Classic.SK_Knight_Classic"), TEXT("/Game/Knight/SK_Knight_Light.SK_Knight_Light"),
			TEXT("/Game/Knight/SK_Knight.SK_Knight"), TEXT("/Game/Knight/SK_Knight_Mail.SK_Knight_Mail") };
		return P[FMath::Clamp(Set, 0, Set_Count - 1)];
	}
	inline const TCHAR* SetMaterialPath(int32 Set)
	{
		static const TCHAR* P[Set_Count] = { TEXT("/Game/Knight/Classic/MI_KnightSkin_Classic.MI_KnightSkin_Classic"), TEXT("/Game/Knight/Light/MI_KnightSkin_Light.MI_KnightSkin_Light"),
			TEXT("/Game/Knight/Heavy/MI_KnightSkin_Heavy.MI_KnightSkin_Heavy"), TEXT("/Game/Knight/Mail/MI_KnightSkin_Mail.MI_KnightSkin_Mail") };
		return P[FMath::Clamp(Set, 0, Set_Count - 1)];
	}
	constexpr float ClassicGreyGain = 1.25f;   // the Classic model's greyed gold, brightened to about the Heavy model's steel before the tint
	constexpr int32 NumModelSlots = 5;
	inline int32 ModelSlotIndex(int32 Slot)
	{
		using namespace KnightDesign;
		return Slot == SlotHelmet ? 0 : Slot == SlotBody ? 1 : Slot == SlotShoulders ? 2 : Slot == SlotBoots ? 3 : Slot == SlotGloves ? 4 : -1;
	}

	struct FSkin
	{
		UPoseableMeshComponent* Comp = nullptr;   // the leader (the chain mail; SK_Knight when the mail is missing)
		int32 LeaderSet = Set_Mail;
		TArray<FName> Bone;                   // the bones that follow a joint, parents first
		TArray<USceneComponent*> Joint;       // ... the joint pivot each one follows
		TArray<FTransform> Bind;              // ... and its rest pose with the joint's position taken out
		AActor* Owner = nullptr;
		USceneComponent* Attach = nullptr;
		USkinnedMeshComponent* Sets[Set_Count] = {};       // the style meshes made so far (the leader is one of them)
		TArray<UMaterialInstanceDynamic*> Mids[Set_Count];  // per style, its tintable material per material index
		int32 Worn[NumModelSlots] = { -2, -2, -2, -2, -2 }; // the armour id each slot shows (0 = bare: only the mail); -2 = not set yet
	};

	// The skinned knight on Attach (the capsule: the joints' positions are in its space), its bones following Pivots
	// (indexed by KnightDesign::EPivot). Null when no skinned model is there (then the cut pieces are used).
	inline UPoseableMeshComponent* MakeSkin(FSkin& S, AActor* Owner, USceneComponent* Attach, USceneComponent* const* Pivots)
	{
		USkeletalMesh* M = LoadObject<USkeletalMesh>(nullptr, SetMeshPath(Set_Mail));
		S.LeaderSet = Set_Mail;
		if (!M) { M = LoadObject<USkeletalMesh>(nullptr, SetMeshPath(Set_Heavy)); S.LeaderSet = Set_Heavy; }
		if (!M || !Attach) return nullptr;
		S.Owner = Owner;
		S.Attach = Attach;
		UPoseableMeshComponent* C = NewObject<UPoseableMeshComponent>(Owner);
		C->SetupAttachment(Attach);
		C->SetSkinnedAssetAndUpdate(M);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetBoundsScale(1.5f);   // (the arms and legs reach well past the standing pose's bounds in a swing or a kick)
		C->PrimaryComponentTick.AddPrerequisite(Owner, Owner->PrimaryActorTick);   // (the bones follow this frame's pose, not last frame's)
		C->RegisterComponent();
		const FReferenceSkeleton& Ref = M->GetRefSkeleton();
		TArray<FTransform> CS;
		CS.SetNum(Ref.GetNum());
		for (int32 b = 0; b < Ref.GetNum(); ++b)
		{
			const int32 Parent = Ref.GetParentIndex(b);
			CS[b] = Parent >= 0 ? Ref.GetRefBonePose()[b] * CS[Parent] : Ref.GetRefBonePose()[b];
			for (int32 p = 0; p < KnightDesign::P_Count; ++p)
				if (BoneName(p) && Pivots[p] && Ref.GetBoneName(b) == FName(BoneName(p)))
				{
					S.Bone.Add(Ref.GetBoneName(b));
					S.Joint.Add(Pivots[p]);
					S.Bind.Add(CS[b] * FTransform(-KnightDesign::PivotPos(p)));
					static int32 Logged = 0;   // (once: the bones must sit on their joints, or the import axes are wrong)
					if (Logged++ < KnightDesign::P_Count) UE_LOG(LogTemp, Log, TEXT("JEEBSKIN bone %s at %s, joint at %s"), BoneName(p), *CS[b].GetLocation().ToCompactString(), *KnightDesign::PivotPos(p).ToCompactString());
				}
		}
		S.Comp = C;
		S.Sets[S.LeaderSet] = C;
		if (UMaterialInterface* Mi = LoadObject<UMaterialInterface>(nullptr, SetMaterialPath(S.LeaderSet)))
			for (int32 m = 0; m < C->GetNumMaterials(); ++m) C->SetMaterial(m, Mi);
		return C;
	}

	// Shows or hides one material's sections of a style mesh (LOD 0; the knight has no other)
	inline void ShowSection(USkinnedMeshComponent* C, int32 MatIndex, bool bShow)
	{
		const FSkeletalMeshRenderData* RD = C ? C->GetSkeletalMeshRenderData() : nullptr;
		if (!RD || RD->LODRenderData.Num() == 0 || MatIndex == INDEX_NONE) return;
		const TArray<FSkelMeshRenderSection>& Secs = RD->LODRenderData[0].RenderSections;
		for (int32 i = 0; i < Secs.Num(); ++i)
			if (Secs[i].MaterialIndex == MatIndex) C->ShowMaterialSection(MatIndex, i, bShow, 0);
	}

	// The style mesh for Set, made the first time it is needed (it follows the leader's bones). Null if its asset is missing.
	inline USkinnedMeshComponent* SetComp(FSkin& S, int32 Set)
	{
		if (Set < 0 || Set >= Set_Count) return nullptr;
		if (S.Sets[Set]) return S.Sets[Set];
		USkeletalMesh* M = LoadObject<USkeletalMesh>(nullptr, SetMeshPath(Set));
		if (!M || !S.Comp || !S.Owner) return nullptr;
		USkeletalMeshComponent* C = NewObject<USkeletalMeshComponent>(S.Owner);
		C->SetupAttachment(S.Attach);
		C->SetSkinnedAssetAndUpdate(M);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetBoundsScale(1.5f);
		C->SetLeaderPoseComponent(S.Comp);   // (the same skeleton: it draws with the leader's bones)
		C->RegisterComponent();
		if (UMaterialInterface* Mi = LoadObject<UMaterialInterface>(nullptr, SetMaterialPath(Set)))
			for (int32 m = 0; m < C->GetNumMaterials(); ++m) C->SetMaterial(m, Mi);
		for (int32 m = 0; m < C->GetNumMaterials(); ++m) ShowSection(C, m, false);   // (nothing until a slot shows its piece)
		S.Sets[Set] = C;
		return C;
	}

	// The tint of one style's slot for a tier. Classic is painted gold, so its metal is greyed (Desat, set in its material)
	// and brightened, then tinted like the steel-painted Heavy; the mail is never tinted.
	inline void PaintSet(FSkin& S, int32 Set, int32 MatIndex, int32 Tier)
	{
		USkinnedMeshComponent* C = S.Sets[Set];
		if (!C || MatIndex == INDEX_NONE) return;
		TArray<UMaterialInstanceDynamic*>& Mids = S.Mids[Set];
		if (Mids.Num() <= MatIndex) Mids.SetNumZeroed(MatIndex + 1);
		if (!Mids[MatIndex]) Mids[MatIndex] = C->CreateDynamicMaterialInstance(MatIndex, C->GetMaterial(MatIndex));
		if (UMaterialInstanceDynamic* M = Mids[MatIndex])
		{
			M->SetVectorParameterValue(TEXT("Tint"), Set == Set_Mail ? FLinearColor::White : TierTint(Tier));
			if (Set == Set_Classic) M->SetScalarParameterValue(TEXT("DesatGain"), ClassicGreyGain);
		}
	}

	// What one armour slot shows: Id is the worn armour piece (0 = none: only the chain mail). Calling it again with the same
	// piece changes nothing.
	inline void Wear(FSkin& S, int32 Slot, int32 Id)
	{
		const int32 i = ModelSlotIndex(Slot);
		if (i < 0 || !S.Comp || S.Worn[i] == Id) return;
		S.Worn[i] = Id;
		const FName MatName(SlotMaterial(Slot));
		const int32 Style = Id ? FMath::Clamp(JeebItems::StyleOf(Id), 0, JeebItems::NumArmourStyles - 1) : -1;
		// the mail is always drawn whole, the body under every piece (owner, 2026-11-03); it sits MAIL_INSET inside the plate
		// (knight_blender_export.py) so it does not poke through. Each piece reaches a little under its neighbours (the export's
		// overlap), so no gap opens where two styles meet.
		for (int32 Set = 0; Set < Set_Count; ++Set)
		{
			if (Set == Set_Mail) continue;   // (the mail always shows whole)
			const bool bThis = Set == Style;
			USkinnedMeshComponent* C = bThis ? SetComp(S, Set) : S.Sets[Set];
			if (!C) continue;
			const int32 Mat = C->GetMaterialIndex(MatName);
			ShowSection(C, Mat, bThis);
			if (bThis) PaintSet(S, Set, Mat, JeebItems::VariantOf(Id));
			// a style with no slot showing is not drawn at all (except the leader, whose bones the others follow)
			bool bAny = false;
			for (int32 k = 0; k < NumModelSlots; ++k) bAny |= S.Worn[k] > 0 && JeebItems::StyleOf(S.Worn[k]) == Set;
			if (C != S.Comp) C->SetVisibility(bAny);
		}
	}

	// Every frame once the pose is done: each bone goes where its joint is (the joint at rest = the bone at rest). The style
	// meshes follow the leader by themselves.
	inline void UpdateSkin(FSkin& S)
	{
		if (!S.Comp) return;
		const FTransform ToComp = S.Comp->GetComponentTransform().Inverse();
		for (int32 i = 0; i < S.Bone.Num(); ++i)
			S.Comp->SetBoneTransformByName(S.Bone[i], S.Bind[i] * S.Joint[i]->GetComponentTransform() * ToComp, EBoneSpaces::ComponentSpace);
	}
}

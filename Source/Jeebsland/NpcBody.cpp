#include "NpcBody.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/IConsoleManager.h"
#include "CoreGlobals.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	TAutoConsoleVariable<int32> CVarNpcModels(TEXT("jeeb.Npc.Models"), 1,
		TEXT("Townspeople's bodies: 1 the skinned Meshy characters (/Game/People, Content/Data/People.csv), 0 the old bodies built from simple shapes"));

	// A small CSV table: a header row, # notes, no commas inside cells
	struct FTable
	{
		TMap<FString, int32> Cols;
		TArray<TArray<FString>> Rows;
		bool Load(const TCHAR* Name)
		{
			TArray<FString> Lines;
			if (!FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectContentDir() / TEXT("Data") / Name)))
			{
				UE_LOG(LogTemp, Warning, TEXT("JEEBPEOPLE cannot read Content/Data/%s"), Name);
				return false;
			}
			for (FString L : Lines)
			{
				L.TrimStartAndEndInline();
				if (L.IsEmpty() || L.StartsWith(TEXT("#"))) continue;
				TArray<FString> Cells;
				L.ParseIntoArray(Cells, TEXT(","), false);
				for (FString& C : Cells) C.TrimStartAndEndInline();
				if (Cols.Num() == 0) { for (int32 i = 0; i < Cells.Num(); ++i) Cols.Add(Cells[i].ToLower(), i); continue; }
				Rows.Add(Cells);
			}
			return Cols.Num() > 0;
		}
		FString S(const TArray<FString>& Row, const TCHAR* Col) const
		{
			const int32* C = Cols.Find(FString(Col).ToLower());
			return (C && Row.IsValidIndex(*C)) ? Row[*C] : FString();
		}
	};

	TMap<FString, JeebPeople::FModel>& Models()
	{
		static TMap<FString, JeebPeople::FModel> M;
		static bool bRead = false;
		if (!bRead)
		{
			bRead = true;
			FTable T;
			if (T.Load(TEXT("People.csv")))
				for (const TArray<FString>& R : T.Rows)
				{
					JeebPeople::FModel Md;
					Md.Name = T.S(R, TEXT("Model"));
					if (Md.Name.IsEmpty()) continue;
					Md.WalkSpeed = FMath::Max(10.f, FCString::Atof(*T.S(R, TEXT("WalkSpeed"))));
					Md.WalkSink = FCString::Atof(*T.S(R, TEXT("WalkSink")));
					Md.GarmentLum = FMath::Max(0.005f, FCString::Atof(*T.S(R, TEXT("GarmentLum"))));
					const FString Y = T.S(R, TEXT("Yaw"));
					if (!Y.IsEmpty()) Md.Yaw = FCString::Atof(*Y);
					M.Add(Md.Name.ToLower(), Md);
				}
		}
		return M;
	}

	struct FWhoRow { FString Key, Model; FLinearColor Tint; float Scale = 1.f; FLinearColor Skin = FLinearColor(0.f, 0.f, 0.f, 0.f); };
	const TArray<FWhoRow>& WhoRows()
	{
		static TArray<FWhoRow> W;
		static bool bRead = false;
		if (!bRead)
		{
			bRead = true;
			FTable T;
			if (T.Load(TEXT("PeopleWho.csv")))
				for (const TArray<FString>& R : T.Rows)
				{
					FWhoRow Row;
					Row.Key = T.S(R, TEXT("Who"));
					Row.Model = T.S(R, TEXT("Model"));
					Row.Tint = FLinearColor(-1.f, 0.f, 0.f);
					TArray<FString> P;
					T.S(R, TEXT("Tint")).ParseIntoArrayWS(P);
					if (P.Num() == 3) Row.Tint = FLinearColor(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]));
					// (2026-11-18) Scale and Skin, both optional
					const FString Sc = T.S(R, TEXT("Scale"));
					if (!Sc.IsEmpty()) Row.Scale = FMath::Clamp(FCString::Atof(*Sc), 0.5f, 2.f);
					TArray<FString> Sk;
					T.S(R, TEXT("Skin")).ParseIntoArrayWS(Sk);
					if (Sk.Num() == 3) Row.Skin = FLinearColor(FCString::Atof(*Sk[0]), FCString::Atof(*Sk[1]), FCString::Atof(*Sk[2]), 1.f);
					if (!Row.Key.IsEmpty()) W.Add(Row);
				}
		}
		return W;
	}
}

namespace JeebPeople
{
	int32 Setting() { return CVarNpcModels.GetValueOnGameThread(); }
	bool Enabled() { return Setting() != 0 && !IsRunningDedicatedServer(); }

	FWho Find(const TArray<FString>& Keys)
	{
		FWho Out;
		for (const FString& K : Keys)
		{
			if (K.IsEmpty()) continue;
			for (const FWhoRow& R : WhoRows())
				if (R.Key.Equals(K, ESearchCase::IgnoreCase)) { Out.Model = R.Model; Out.Tint = R.Tint; Out.Scale = R.Scale; Out.Skin = R.Skin; Out.bFound = true; return Out; }
		}
		return Out;
	}

	const FModel* Model(const FString& Name)
	{
		FModel* M = Models().Find(Name.ToLower());
		if (!M) return nullptr;
		if (!M->bLoaded)
		{
			M->bLoaded = true;
			const FString Base = TEXT("/Game/People/");
			M->Mesh = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("%sSK_%s.SK_%s"), *Base, *M->Name, *M->Name));
			static const TCHAR* Keys[3] = { TEXT("Idle"), TEXT("Walk"), TEXT("Talk") };
			for (int32 i = 0; i < 3; ++i)
				M->Clips[i] = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%sA_%s_%s.A_%s_%s"), *Base, *M->Name, Keys[i], *M->Name, Keys[i]));
			// Kept alive for the whole run (2026-11-18): this cache outlives worlds, and the garbage collection between the menu
			// world and the game world freed the meshes, so an online client crashed on joining
			if (M->Mesh) M->Mesh->AddToRoot();
			for (int32 i = 0; i < 3; ++i) if (M->Clips[i]) M->Clips[i]->AddToRoot();
			UE_LOG(LogTemp, Log, TEXT("JEEBPEOPLE model %s: mesh %s, clips %d %d %d"), *M->Name, M->Mesh ? TEXT("ok") : TEXT("MISSING"),
				M->Clips[0] != nullptr, M->Clips[1] != nullptr, M->Clips[2] != nullptr);
		}
		return (M->Mesh && M->Clips[0]) ? M : nullptr;
	}
}

// ---- The animator

void UNpcAnimInstance::SetClips(UAnimSequence* Idle, UAnimSequence* Walk, UAnimSequence* Talk, float StartAt)
{
	Clip[0] = Idle; Clip[1] = Walk ? Walk : Idle; Clip[2] = Talk ? Talk : Idle;
	Start = StartAt;
	bNewClips = true;
}

FAnimInstanceProxy* UNpcAnimInstance::CreateAnimInstanceProxy() { return new FNpcAnimProxy(this); }
void UNpcAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) { delete static_cast<FNpcAnimProxy*>(InProxy); }

void FNpcAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	UNpcAnimInstance* A = CastChecked<UNpcAnimInstance>(InAnimInstance);
	if (A->bNewClips)
	{
		A->bNewClips = false;
		for (int32 i = 0; i < 3; ++i)
		{
			Clip[i] = A->Clip[i];
			Time[i] = Clip[i] ? FMath::Fmod(A->Start, FMath::Max(Clip[i]->GetPlayLength(), 0.01f)) : 0.f;
			Weight[i] = (i == A->Want) ? 1.f : 0.f;
		}
	}
	// Cross-fade toward the wanted clip over 0.3 s, and run every clip that shows
	const float Step = DeltaSeconds / 0.3f;
	for (int32 i = 0; i < 3; ++i)
	{
		Weight[i] = (i == A->Want) ? FMath::Min(1.f, Weight[i] + Step) : FMath::Max(0.f, Weight[i] - Step);
		if (!Clip[i]) continue;
		if (Weight[i] > 0.f || i == A->Want)
		{
			const float Len = FMath::Max(Clip[i]->GetPlayLength(), 0.01f);
			Time[i] = FMath::Fmod(Time[i] + DeltaSeconds * (i == 1 ? A->Rate : 1.f), Len);
		}
		else if (i == 2) Time[i] = 0.f;   // (a talk starts from its beginning)
	}
	Head = FMath::FInterpTo(Head, A->Head, DeltaSeconds, 5.f);
}

bool FNpcAnimProxy::Evaluate(FPoseContext& Output)
{
	float Sum = 0.f;
	for (int32 i = 0; i < 3; ++i) if (Clip[i]) Sum += Weight[i];
	if (Sum <= KINDA_SMALL_NUMBER) { Output.ResetToRefPose(); return true; }

	TArray<FCompactPose, TInlineAllocator<3>> Poses;
	TArray<FBlendedCurve, TInlineAllocator<3>> Curves;
	TArray<UE::Anim::FStackAttributeContainer, TInlineAllocator<3>> Attrs;
	TArray<float, TInlineAllocator<3>> W;
	for (int32 i = 0; i < 3; ++i)
	{
		if (!Clip[i] || Weight[i] <= 0.f) continue;
		FCompactPose& P = Poses.AddDefaulted_GetRef();
		P.SetBoneContainer(&Output.Pose.GetBoneContainer());
		FBlendedCurve& C = Curves.AddDefaulted_GetRef();
		C.InitFrom(Output.Curve);
		UE::Anim::FStackAttributeContainer& At = Attrs.AddDefaulted_GetRef();
		FAnimationPoseData PD(P, C, At);
		Clip[i]->GetAnimationPose(PD, FAnimExtractContext(double(Time[i]), false));
		W.Add(Weight[i] / Sum);
	}
	if (Poses.Num() == 1)
	{
		Output.Pose = Poses[0];
		Output.Curve = Curves[0];
	}
	else
	{
		FAnimationPoseData Out(Output);
		FAnimationRuntime::BlendPosesTogether(Poses, Curves, Attrs, W, Out);
	}

	// The head (60%) and neck (40%) turn toward the player about the body's up axis
	if (FMath::Abs(Head) > 0.5f)
	{
		const FBoneContainer& BC = Output.Pose.GetBoneContainer();
		const int32 NeckM = BC.GetPoseBoneIndexForBoneName(TEXT("neck")), HeadM = BC.GetPoseBoneIndexForBoneName(TEXT("Head"));
		if (NeckM != INDEX_NONE && HeadM != INDEX_NONE)
		{
			const FCompactPoseBoneIndex Neck = BC.MakeCompactPoseIndex(FMeshPoseBoneIndex(NeckM)), HeadB = BC.MakeCompactPoseIndex(FMeshPoseBoneIndex(HeadM));
			if (Neck != INDEX_NONE && HeadB != INDEX_NONE)
			{
				FCSPose<FCompactPose> CS;
				CS.InitPose(Output.Pose);
				const FCompactPoseBoneIndex NeckParent = Output.Pose.GetParentBoneIndex(Neck);
				const FQuat ParentQ = NeckParent != INDEX_NONE ? CS.GetComponentSpaceTransform(NeckParent).GetRotation() : FQuat::Identity;
				const float Rad = FMath::DegreesToRadians(Head);
				const FQuat QN(FVector::UpVector, Rad * 0.4f), QH(FVector::UpVector, Rad * 0.6f);
				FTransform& NL = Output.Pose[Neck];
				NL.SetRotation((ParentQ.Inverse() * QN * ParentQ * NL.GetRotation()).GetNormalized());
				const FQuat NeckQ = QN * CS.GetComponentSpaceTransform(Neck).GetRotation();   // (the neck's new turn)
				FTransform& HL = Output.Pose[HeadB];
				HL.SetRotation((NeckQ.Inverse() * QH * NeckQ * HL.GetRotation()).GetNormalized());
			}
		}
	}
	return true;
}

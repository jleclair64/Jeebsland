#include "ChoppableTree.h"
#include "Components/CapsuleComponent.h"

AChoppableTree::AChoppableTree()
{
	Skill = ESkillType::Woodcutting;
	GatherSeconds = 5.f;
	RespawnSeconds = 300.f; // 5 minutes
	XPReward = 20;
	ItemType = EItemType::Logs;
	ItemsPerGather = 3;
	MaxCharges = 1;
	StartText = TEXT("You swing your axe...");
	SuccessText = TEXT("You chop down the tree and collect some logs.");
	EmptyText = TEXT("This tree is a stump for now. Give it a few minutes.");

	TreeRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(TreeRoot);

	// Invisible trunk-sized collision so you cannot walk through the tree
	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("TrunkCollision"));
	Capsule->SetupAttachment(TreeRoot);
	Capsule->InitCapsuleSize(25.f, 110.f);
	Capsule->SetCollisionProfileName(TEXT("BlockAll"));
	Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore); // the camera slides past trunks
	Capsule->SetRelativeLocation(FVector(0.f, 0.f, 110.f));
}

void AChoppableTree::Setup(ETreeKind InKind, float Radius, float Height)
{
	Kind = InKind;

	const float HalfHeight = FMath::Max(FMath::Min(Height * 0.5f, 300.f), Radius + 1.f);
	Capsule->SetCapsuleSize(Radius, HalfHeight);
	Capsule->SetRelativeLocation(FVector(0.f, 0.f, HalfHeight));

	switch (Kind)
	{
	case ETreeKind::Oak:
		XPReward = 20; GatherSeconds = 5.f; ItemsPerGather = 3;
		ItemType = EItemType::Logs;
		break;
	case ETreeKind::Pine:
		XPReward = 15; GatherSeconds = 5.f; ItemsPerGather = 3;
		ItemType = EItemType::PineLogs;
		SuccessText = TEXT("You chop down the pine and collect some logs.");
		break;
	case ETreeKind::Redwood:
		XPReward = 40; GatherSeconds = 8.f; ItemsPerGather = 8;
		ItemType = EItemType::RedwoodLogs;
		StartText = TEXT("You swing your axe at the giant redwood...");
		SuccessText = TEXT("The redwood crashes down. You collect a pile of logs.");
		break;
	}
}

void AChoppableTree::OnChargesChanged(int32 Remaining, int32 Max)
{
	const float Ground = GetActorLocation().Z;
	const float Stump = (Kind == ETreeKind::Redwood) ? 0.12f : 0.2f;

	if (Remaining <= 0 && !bHidden)
	{
		// Chopped: leaves, branches and needles vanish, the trunk shrinks to a stump
		bHidden = true;
		Saved.Reset();
		for (const FTreePart& P : Parts)
		{
			UInstancedStaticMeshComponent* C = P.Comp.Get();
			if (!C) { continue; }
			for (int32 i = 0; i < P.Count; ++i)
			{
				FTransform T;
				C->GetInstanceTransform(P.Start + i, T, true);
				Saved.Add(T);
				if (P.bTrunk)
				{
					FVector Loc = T.GetLocation();
					Loc.Z = Ground + (Loc.Z - Ground) * Stump;
					FVector Sc = T.GetScale3D();
					Sc.Z *= Stump;
					T.SetLocation(Loc);
					T.SetScale3D(Sc);
				}
				else
				{
					T.SetScale3D(FVector(0.001f));
				}
				C->UpdateInstanceTransform(P.Start + i, T, true, i == P.Count - 1, true);
			}
		}
	}
	else if (Remaining > 0 && bHidden)
	{
		// Regrown: put everything back
		bHidden = false;
		int32 K = 0;
		for (const FTreePart& P : Parts)
		{
			UInstancedStaticMeshComponent* C = P.Comp.Get();
			if (!C) { continue; }
			for (int32 i = 0; i < P.Count; ++i)
			{
				if (Saved.IsValidIndex(K))
				{
					C->UpdateInstanceTransform(P.Start + i, Saved[K], true, i == P.Count - 1, true);
				}
				++K;
			}
		}
		Saved.Reset();
	}
}

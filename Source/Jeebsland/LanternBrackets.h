#pragma once
// Something for every hanging lantern to hang from (2026-11-20, owner: "fix these lamps so they don't look like they're floating").
// The town models' own brackets were cut off with their old lanterns (building_clean.py), and the TownLab decor lanterns (TownLabDecor.h)
// hung on air beside porch posts and walls. Each lantern now gets an iron arm from the nearest wall or post within 80 cm (traced against
// the town actor's own solid shapes, so call this after every building's solid is placed) out over its hook, with a wall plate and a
// brace under it; with nothing near, a slim iron lamp post beside it with an arm over the hook. Drawn in the lantern's own black iron,
// one instanced set per town. Client only and visual (no collision); the same on every machine, as the town is.
// Used by ATownLabTown (Oak Hill) and ASteeleCreekTown.
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "Terrain.h"

namespace JeebLanternBrackets
{
	struct FHook { FVector Top; float Height; UStaticMesh* Mesh; };

	// The hook of a lantern drawn with Mesh at Xf: the top middle of its bounds
	inline FHook HookOf(UStaticMesh* Mesh, const FTransform& Xf)
	{
		const FBox Wb = Mesh->GetBoundingBox().TransformBy(Xf);
		return FHook{ FVector(Wb.GetCenter().X, Wb.GetCenter().Y, Wb.Max.Z), float(Wb.GetSize().Z), Mesh };
	}

	inline void BuildNow(AActor* Owner, const TArray<FHook> Hooks, const FString Tag)
	{
		if (!Owner || Hooks.Num() == 0) return;
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Cube) return;
		// the solid shapes to hang from, gathered before the iron is added
		TArray<UPrimitiveComponent*> Solids;
		for (UActorComponent* C : Owner->GetComponents())
			if (UPrimitiveComponent* Pc = Cast<UPrimitiveComponent>(C))
				if (Pc->GetCollisionEnabled() != ECollisionEnabled::NoCollision) Solids.Add(Pc);
		UInstancedStaticMeshComponent* Iron = NewObject<UInstancedStaticMeshComponent>(Owner);
		Iron->SetStaticMesh(Cube);
		Iron->SetupAttachment(Owner->GetRootComponent());
		Iron->SetMobility(EComponentMobility::Static);
		Iron->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Iron->SetCastShadow(false);
		Iron->SetCullDistances(0, 9000);
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color")))   // (plain dark iron: the lantern's own first slot may be its glowing glass)
			if (UMaterialInstanceDynamic* Mi = UMaterialInstanceDynamic::Create(Base, Owner)) { Mi->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.018f, 0.017f, 0.016f)); Iron->SetMaterial(0, Mi); }
		Iron->RegisterComponent();
		auto Bar = [&](const FVector& A, const FVector& B, float T)   // (a 100 cm cube stretched from A to B, T cm thick)
		{
			const FVector D = B - A; const float L = D.Size();
			if (L < 1.f) return;
			Iron->AddInstance(FTransform(D.Rotation(), (A + B) * 0.5f, FVector(L / 100.f, T / 100.f, T / 100.f)), true);
		};
		FCollisionQueryParams Q(TEXT("LanternBracket"), true);
		int32 OnWall = 0, OnPost = 0;
		for (const FHook& H : Hooks)
		{
			const FVector Arm = H.Top + FVector(0.f, 0.f, 16.f);   // (the arm 16 cm over the hook, so the drop and the arm show even when the lantern hangs close to its post)
			float Best = 80.f; FVector Wall = FVector::ZeroVector, Nrm = FVector::ZeroVector;
			{   // (diagnostic: the nearest solid of any kind within 3 m, first lanterns only)
				static int32 Diag = 0;
				if (Diag < 8)
				{
					float Nd = 1.0e9f; FHitResult Nh;
					for (int32 k = 0; k < 16; ++k) { const float R = FMath::DegreesToRadians(22.5f * k); FHitResult Hh; if (Owner->GetWorld()->LineTraceSingleByChannel(Hh, Arm - FVector(0, 0, H.Height * 0.5f + 9.f), Arm - FVector(0, 0, H.Height * 0.5f + 9.f) + FVector(FMath::Cos(R), FMath::Sin(R), 0.f) * 300.f, ECC_Visibility, Q) && Hh.Distance < Nd) { Nd = Hh.Distance; Nh = Hh; } }
					UE_LOG(LogTemp, Log, TEXT("%s LANTERN near %d at %s: nearest solid %.0f cm (%s, normal z %.2f)"), *Tag, Diag, *H.Top.ToCompactString(), Nd, Nh.GetComponent() ? *Nh.GetComponent()->GetName() : TEXT("none"), Nh.ImpactNormal.Z);
					++Diag;
				}
			}
			for (int32 k = 0; k < 16; ++k)
			{
				const float R = FMath::DegreesToRadians(22.5f * k);
				const FVector Dir(FMath::Cos(R), FMath::Sin(R), 0.f);
				// (the world's own trace against every solid; walls and posts only: nothing facing up or down, like the ground or an eave's top)
				for (const float Up : { 0.f, -0.5f * H.Height - 9.f })
				{
					FHitResult Hit;
					const FVector From = Arm + FVector(0.f, 0.f, Up);
					if (Owner->GetWorld()->LineTraceSingleByChannel(Hit, From, From + Dir * 80.f, ECC_Visibility, Q) && FMath::Abs(Hit.ImpactNormal.Z) < 0.5f && Hit.Distance > 1.f && Hit.Distance < Best)
					{
						Best = Hit.Distance; Wall = Hit.ImpactPoint; Nrm = -Dir;
					}
				}
			}
			if (Best < 80.f)
			{
				const FVector W(Wall.X, Wall.Y, Arm.Z);
				const FVector Out = (FVector(Arm.X, Arm.Y, 0.f) - FVector(W.X, W.Y, 0.f)).GetSafeNormal();
				Bar(W + FVector(0, 0, -30.f), W + FVector(0, 0, 14.f), 6.f);                        // the wall plate
				Bar(W - Out * 2.f, Arm + Out * FMath::Max(6.f, 22.f - Best), 3.5f);                     // the arm, reaching a little past the hook
				Bar(Arm, H.Top - FVector(0, 0, 1.f), 1.5f);                                               // the drop to the hook
				Bar(W - FVector(0, 0, 22.f), W + (Arm - W) * 0.6f, 2.f);                                 // the brace
				++OnWall;
			}
			else
			{
				// a lamp post 40 cm off, on the side away from the town's middle, its arm over the hook
				FVector2D Off(H.Top.X - Owner->GetActorLocation().X, H.Top.Y - Owner->GetActorLocation().Y);
				Off = Off.GetSafeNormal();
				if (Off.IsNearlyZero()) Off = FVector2D(1.f, 0.f);
				const FVector2D P2(H.Top.X + Off.X * 40.f, H.Top.Y + Off.Y * 40.f);
				const FVector Base(P2.X, P2.Y, JeebTerrain::HeightAt(P2.X, P2.Y) - 10.f);
				const FVector Toward(-Off.X, -Off.Y, 0.f);
				Bar(Base, FVector(Base.X, Base.Y, Arm.Z + 10.f), 7.f);                                    // the post
				Bar(Base, Base + FVector(0, 0, 25.f), 14.f);                                              // its foot
				Bar(FVector(Base.X, Base.Y, Arm.Z), Arm + Toward * 3.f, 3.f);                             // the arm
				Bar(Arm, H.Top - FVector(0, 0, 1.f), 1.5f);                                               // the drop to the hook
				Bar(FVector(Base.X, Base.Y, Arm.Z - 24.f), FVector(Base.X, Base.Y, Arm.Z) + Toward * 24.f, 2.f);   // the brace
				++OnPost;
			}
		}
		UE_LOG(LogTemp, Log, TEXT("%s LANTERN brackets: %d lanterns, %d on a wall or post arm (traced), %d on their own lamp post"), *Tag, Hooks.Num(), OnWall, OnPost);
	}
	// (run a moment after the town is built: the solids' collision is in the physics scene by then, and the rest of the world too)
	inline void Build(AActor* Owner, const TArray<FHook>& Hooks, const TCHAR* Tag)
	{
		if (!Owner || Hooks.Num() == 0) return;
		TWeakObjectPtr<AActor> W(Owner);
		const TArray<FHook> Copy = Hooks; const FString T(Tag);
		FTimerHandle Hd;
		Owner->GetWorldTimerManager().SetTimer(Hd, FTimerDelegate::CreateLambda([W, Copy, T]() { if (W.IsValid()) BuildNow(W.Get(), Copy, T); }), 1.5f, false);
	}
}

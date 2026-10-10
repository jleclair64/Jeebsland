#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"

// Floating damage numbers: red = damage taken or dealt, blue = damage blocked, gold = critical hit.
// Header only; the HUD draws and expires them.
namespace JeebCombat
{
	enum ENumberType : int32 { N_Damage = 0, N_Blocked = 1, N_Crit = 2, N_Heal = 3 };
	struct FNumber { FVector Loc; float Born; int32 Amount; int32 Type; };
	inline TArray<FNumber>& Numbers()
	{
		static TArray<FNumber> N;
		return N;
	}
	inline void AddNumber(const UWorld* World, const FVector& Loc, int32 Amount, int32 Type)
	{
		if (!World) return;
		TArray<FNumber>& N = Numbers();
		if (N.Num() > 60) N.RemoveAt(0);
		N.Add(FNumber{ Loc + FVector(FMath::FRandRange(-25.f, 25.f), FMath::FRandRange(-25.f, 25.f), 0.f), (float)World->GetTimeSeconds(), Amount, Type });
	}
}

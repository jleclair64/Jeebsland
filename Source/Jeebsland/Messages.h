#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"

// Short game messages ("You take 12 ingots.", "Your pack is full."). The HUD shows the newest few
// above the weapon bar and fades them out. Header only.
namespace JeebMsg
{
	struct FMsg { FString Text; FLinearColor Color; double Born; float Life; };

	inline TArray<FMsg>& All()
	{
		static TArray<FMsg> M;
		return M;
	}

	inline void Post(float Seconds, const FColor& Color, const FString& Text)
	{
		TArray<FMsg>& M = All();
		if (M.Num() > 0 && M.Last().Text == Text) M.RemoveAt(M.Num() - 1);   // the same line again just refreshes
		if (M.Num() > 8) M.RemoveAt(0);
		M.Add(FMsg{ Text, FLinearColor(Color), FPlatformTime::Seconds(), FMath::Max(Seconds, 2.f) });
	}
}

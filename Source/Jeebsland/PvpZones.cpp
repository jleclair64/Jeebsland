#include "PvpZones.h"
#include "KnightCharacter.h"
#include "HitBoxes.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace JeebPvp
{
	const TArray<FZone>& Zones()
	{
		static const TArray<FZone> All = []()
		{
			TArray<FZone> Out;
			FString Text;
			const FString Path = FPaths::ProjectContentDir() / TEXT("Data") / TEXT("PvpZones.csv");
			if (!FFileHelper::LoadFileToString(Text, *Path)) { UE_LOG(LogTemp, Error, TEXT("Jeebsland PvP zones: cannot read %s"), *Path); return Out; }
			TArray<FString> Lines;
			Text.ParseIntoArrayLines(Lines);
			bool bHeader = true;
			for (const FString& Line : Lines)
			{
				if (Line.TrimStartAndEnd().IsEmpty() || Line.StartsWith(TEXT("#"))) continue;
				if (bHeader) { bHeader = false; continue; }
				TArray<FString> C;
				Line.ParseIntoArray(C, TEXT(","), false);
				if (C.Num() < 6) { UE_LOG(LogTemp, Error, TEXT("Jeebsland PvP zones: row needs Name,Title,X0,Y0,X1,Y1: %s"), *Line); continue; }
				FZone Z;
				Z.Name = C[0].TrimStartAndEnd(); Z.Title = C[1].TrimStartAndEnd();
				const float X0 = FCString::Atof(*C[2]) * 100.f, Y0 = FCString::Atof(*C[3]) * 100.f, X1 = FCString::Atof(*C[4]) * 100.f, Y1 = FCString::Atof(*C[5]) * 100.f;
				Z.Min = FVector2D(FMath::Min(X0, X1), FMath::Min(Y0, Y1)); Z.Max = FVector2D(FMath::Max(X0, X1), FMath::Max(Y0, Y1));
				Z.SignEvery = C.Num() > 6 ? FCString::Atof(*C[6]) * 100.f : 0.f;
				Out.Add(Z);
			}
			return Out;
		}();
		return All;
	}

	int32 ZoneAt(float X, float Y)
	{
		const TArray<FZone>& Z = Zones();
		for (int32 i = 0; i < Z.Num(); ++i)
			if (X >= Z[i].Min.X && X <= Z[i].Max.X && Y >= Z[i].Min.Y && Y <= Z[i].Max.Y) return i;
		return INDEX_NONE;
	}

	bool MayHurt(const AKnightCharacter* From, const AKnightCharacter* To)
	{
		if (!From || !To) return true;
		if (From == To) return true;
		if (JeebHit::InMoveLab(From->GetWorld())) return true;   // (the test area: players try their moves on each other)
		return IsInPvpZone(From->GetActorLocation()) && IsInPvpZone(To->GetActorLocation());
	}
}

bool IsInPvpZone(const FVector& Location)
{
	return JeebPvp::ZoneAt(float(Location.X), float(Location.Y)) != INDEX_NONE;
}

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GearWall.generated.h"

// The MoveLab gear wall (2026-11-03): every armour piece (each tier in each style), every shield and every weapon, hung on
// pegs so the owner can try gear without it being put in a starting pack. E on a peg puts a copy in the pack (the server
// decides: EInvOp::TakeFromWall, test builds only). The wall is built the same way on every machine (it is not replicated):
// a peg is named by its number, and the server looks the item up in its own wall.
UCLASS()
class AGearWall : public AActor
{
	GENERATED_BODY()

public:
	AGearWall();

	struct FPeg
	{
		int32 Id = 0;
		int32 Count = 1;
		FVector Pos = FVector::ZeroVector;   // world position of the item's centre
	};
	const TArray<FPeg>& GetPegs() const { return Pegs; }

	// The peg the view ray (From, Dir) points at most closely within MaxDist of Near; -1 if none
	int32 PegAt(const FVector& From, const FVector& Dir, const FVector& Near, float MaxDist) const;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() USceneComponent* Root = nullptr;
	TArray<FPeg> Pegs;
	void Build();
	void AddItem(int32 Id, int32 Count, const FVector& Local, float Size, bool bLay);   // bLay: a long thing lies along the board
	void AddMannequin(int32 Style, int32 Tier, const FVector& Local);   // a small knight wearing one style at one tier, a peg on each piece
	void AddBoard(const FVector& Centre, const FVector& Extent, const FLinearColor& Col);
	void AddSign(const FVector& Local, const FRotator& Rot, const FString& Text, float Size);
};

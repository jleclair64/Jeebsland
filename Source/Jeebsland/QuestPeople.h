#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "QuestPeople.generated.h"

class ANpc;

// Oak Hill's quest givers (2026-11-18): one ANpc (role QuestGiver) for each row of Content/Data/QuestsPeople.csv, standing where the
// row says. Built on every machine, the server too (it checks that a player asking about a quest stands beside the person), after the
// filler props (AWorldFill), so anyone whose spot is taken by a prop or a building steps aside (up to 3 m) the same way everywhere.
// Not replicated: the people only stand and talk; what they say comes from the player's own quest state (UQuestComponent).
UCLASS()
class AQuestPeople : public AActor
{
	GENERATED_BODY()

public:
	AQuestPeople();
	// The person with this key, in this world (null if there is none)
	static ANpc* Find(const UWorld* World, FName Key);

protected:
	virtual void BeginPlay() override;
};

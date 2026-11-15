#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "JeebPaletteTools.generated.h"

class UMaterialParameterCollection;

// Editor help for Tools/palette.py (2026-11-15; Docs/Palette.md). A material's CollectionParameter node stores the collection
// parameter's hidden Id and finds its name from it on load. Rewriting the collection's parameter list from Python gave every
// parameter a new Id, so every material built earlier lost its colours on the next load. This changes the list in place: existing
// parameters keep their Ids, new names are added at the end.
UCLASS()
class UJeebPaletteTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// sets each named vector's default value, adding the names the collection lacks; true if anything changed (the caller saves)
	UFUNCTION(BlueprintCallable, Category = "Jeebsland|Palette")
	static bool SyncVectorDefaults(UMaterialParameterCollection* Collection, const TArray<FName>& Names, const TArray<FLinearColor>& Values);
};

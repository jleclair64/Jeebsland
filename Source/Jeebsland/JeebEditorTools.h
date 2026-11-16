// Editor-only helpers for the import scripts in Tools/ (called from Python as unreal.JeebEditorTools).
// In a game build the functions exist but do nothing.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "JeebEditorTools.generated.h"

class USkeletalMesh;

UCLASS()
class UJeebEditorTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Reads a Megaplant *_DynamicWind.json (Fab's USD download) onto a tree's skeletal mesh as its Dynamic Wind data,
	 *  the same as the DynamicWind plugin's file-dialog import. Returns false if the file or the mesh is wrong. */
	UFUNCTION(BlueprintCallable, Category = "Jeebsland|Editor")
	static bool ImportDynamicWindJson(USkeletalMesh* Mesh, const FString& JsonPath);
};

// Editor-only static mesh helpers for the asset scripts in Tools/ (called from Python as unreal.JeebMeshTools), for the windowless
// -run=pythonscript editor, where the engine's StaticMeshEditorSubsystem is not available (fillassets, 2026-11-15:
// Tools/town_decor_copy.py gives the shipped filler props generated LODs with it). In a game build the functions do nothing.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "JeebMeshTools.generated.h"

class UStaticMesh;

UCLASS()
class UJeebMeshTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Replaces a mesh's LODs with generated ones: LOD i keeps Percent[i] of LOD 0's triangles (1 for LOD 0) and shows from
	 *  ScreenSize[i] down. Rebuilds the mesh; returns its LOD count after the build, or -1 on bad input. */
	UFUNCTION(BlueprintCallable, Category = "Jeebsland|Editor")
	static int32 SetGeneratedLods(UStaticMesh* Mesh, const TArray<float>& Percent, const TArray<float>& ScreenSize);

	/** Triangles in one LOD of the built mesh (0 if it has no such LOD). */
	UFUNCTION(BlueprintCallable, Category = "Jeebsland|Editor")
	static int32 LodTriangles(UStaticMesh* Mesh, int32 Lod);
};

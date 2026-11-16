#include "JeebEditorTools.h"

#include "Engine/SkeletalMesh.h"

#if WITH_EDITOR
#include "DynamicWindImportData.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#endif

bool UJeebEditorTools::ImportDynamicWindJson(USkeletalMesh* Mesh, const FString& JsonPath)
{
#if WITH_EDITOR
	if (!Mesh) return false;
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *JsonPath)) return false;
	FDynamicWindSkeletalImportData Data;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Data)) return false;
	Mesh->Modify();
	const bool bOk = DynamicWind::ImportSkeletalData(*Mesh, Data) != nullptr;
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();
	return bOk;
#else
	return false;
#endif
}

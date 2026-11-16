#include "JeebMeshTools.h"

#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"

int32 UJeebMeshTools::SetGeneratedLods(UStaticMesh* Mesh, const TArray<float>& Percent, const TArray<float>& ScreenSize)
{
#if WITH_EDITOR
	if (!Mesh || Percent.Num() == 0 || Percent.Num() != ScreenSize.Num() || Mesh->GetNumSourceModels() == 0) return -1;
	Mesh->Modify();
	Mesh->SetNumSourceModels(1);
	Mesh->SetAutoComputeLODScreenSize(false);
	Mesh->GetSourceModel(0).ScreenSize = ScreenSize[0];
	for (int32 i = 1; i < Percent.Num(); ++i)
	{
		FStaticMeshSourceModel& Src = Mesh->AddSourceModel();
		Src.BuildSettings = Mesh->GetSourceModel(0).BuildSettings;
		Src.ReductionSettings = Mesh->GetSourceModel(0).ReductionSettings;
		Src.ReductionSettings.PercentTriangles = FMath::Clamp(Percent[i], 0.f, 1.f);
		Src.ReductionSettings.PercentVertices = 1.f;
		Src.ReductionSettings.TerminationCriterion = EStaticMeshReductionTerimationCriterion::Triangles;
		Src.ScreenSize = ScreenSize[i];
	}
	Mesh->Build();
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();
	return Mesh->GetNumLODs();
#else
	return -1;
#endif
}

int32 UJeebMeshTools::LodTriangles(UStaticMesh* Mesh, int32 Lod)
{
	if (!Mesh || !Mesh->GetRenderData() || !Mesh->GetRenderData()->LODResources.IsValidIndex(Lod)) return 0;
	return Mesh->GetRenderData()->LODResources[Lod].GetNumTriangles();
}

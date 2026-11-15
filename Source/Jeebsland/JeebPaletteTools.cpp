#include "JeebPaletteTools.h"
#include "Materials/MaterialParameterCollection.h"

bool UJeebPaletteTools::SyncVectorDefaults(UMaterialParameterCollection* Collection, const TArray<FName>& Names, const TArray<FLinearColor>& Values)
{
#if WITH_EDITOR
	if (!Collection || Names.Num() != Values.Num()) return false;
	bool bChanged = false;
	for (int32 i = 0; i < Names.Num(); ++i)
	{
		const FCollectionVectorParameter* Have = Collection->VectorParameters.FindByPredicate([&](const FCollectionVectorParameter& P) { return P.ParameterName == Names[i]; });
		if (!Have || !Have->DefaultValue.Equals(Values[i], 1e-6f)) { bChanged = true; break; }
	}
	if (!bChanged) return false;
	Collection->PreEditChange(nullptr);   // (records the storage size: a new name re-lays the buffer and recompiles the readers)
	for (int32 i = 0; i < Names.Num(); ++i)
	{
		FCollectionVectorParameter* Have = Collection->VectorParameters.FindByPredicate([&](const FCollectionVectorParameter& P) { return P.ParameterName == Names[i]; });
		if (!Have)
		{
			Have = &Collection->VectorParameters.AddDefaulted_GetRef();   // (a fresh Id)
			Have->ParameterName = Names[i];
		}
		Have->DefaultValue = Values[i];
	}
	Collection->PostEditChange();
	Collection->MarkPackageDirty();
	return true;
#else
	return false;
#endif
}

// Copyright 2024 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Collections/PCGExActorCollection.h"

void FPCGExActorCollectionEntry::GetAssetPaths(TSet<FSoftObjectPath>& OutPaths) const
{
	FPCGExAssetCollectionEntry::GetAssetPaths(OutPaths);
}

bool FPCGExActorCollectionEntry::Validate(const UPCGExAssetCollection* ParentCollection)
{
	if (bIsSubCollection) { LoadSubCollection(SubCollection); }
	else if (!Actor && ParentCollection->bDoNotIgnoreInvalidEntries) { return false; }

	return Super::Validate(ParentCollection);
}

void FPCGExActorCollectionEntry::UpdateStaging(const UPCGExAssetCollection* OwningCollection, const int32 InInternalIndex, const bool bRecursive)
{
	if (bIsSubCollection)
	{
		Staging.Path = SubCollection.ToSoftObjectPath();
		if (bRecursive){ if(UPCGExActorCollection* Ptr = PCGExHelpers::ForceLoad(SubCollection)) { Ptr->RebuildStagingData(true); }}
		Super::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
		return;
	}

	Staging.Path = Actor.ToSoftObjectPath();
	const AActor* A = PCGExHelpers::ForceLoad(Actor);

	PCGExAssetCollection::UpdateStagingBounds(Staging, A);

	Super::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
}

void FPCGExActorCollectionEntry::SetAssetPath(const FSoftObjectPath& InPath)
{
	Actor = TSoftObjectPtr<AActor>(InPath);
}

void FPCGExActorCollectionEntry::OnSubCollectionLoaded()
{
	SubCollectionPtr = Cast<UPCGExActorCollection>(BaseSubCollectionPtr);
}

#if WITH_EDITOR
void UPCGExActorCollection::EDITOR_RefreshDisplayNames()
{
	Super::EDITOR_RefreshDisplayNames();
	for (FPCGExActorCollectionEntry& Entry : Entries)
	{
		Entry.DisplayName = Entry.bIsSubCollection ? FName(TEXT("[") + Entry.SubCollection.GetAssetName() + TEXT("]")) : FName(Entry.Actor.GetAssetName());
	}
}
#endif

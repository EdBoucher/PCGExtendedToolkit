﻿// Copyright 2025 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Collections/PCGExAssetCollection.h"

namespace PCGExStaging
{
	const FName SourceCollectionMapLabel = TEXT("Map");
	const FName OutputCollectionMapLabel = TEXT("Map");

	const FName Tag_CollectionPath = FName(PCGExCommon::PCGExPrefix + TEXT("Collection/Path"));
	const FName Tag_CollectionIdx = FName(PCGExCommon::PCGExPrefix + TEXT("Collection/Idx"));
	const FName Tag_EntryIdx = FName(PCGExCommon::PCGExPrefix + TEXT("CollectionEntry"));

	class PCGEXTENDEDTOOLKIT_API FPickPacker : public TSharedFromThis<FPickPacker>
	{
		FPCGExContext* Context = nullptr;

		TArray<const UPCGExAssetCollection*> AssetCollections;
		TMap<const UPCGExAssetCollection*, uint32> CollectionMap;
		mutable FRWLock AssetCollectionsLock;

		uint16 BaseHash = 0;

	public:
		FPickPacker(FPCGExContext* InContext)
			: Context(InContext)
		{
			BaseHash = static_cast<uint16>(InContext->GetInputSettings<UPCGSettings>()->UID);
		}

		uint64 GetPickIdx(const UPCGExAssetCollection* InCollection, const int16 InIndex, const int16 InSecondaryIndex)
		{
			const uint32 ItemHash = PCGEx::H32(InIndex, InSecondaryIndex + 1);

			{
				FReadScopeLock ReadScopeLock(AssetCollectionsLock);
				if (const uint32* ColIdxPtr = CollectionMap.Find(InCollection)) { return PCGEx::H64(*ColIdxPtr, ItemHash); }
			}

			{
				FWriteScopeLock WriteScopeLock(AssetCollectionsLock);
				if (const uint32* ColIdxPtr = CollectionMap.Find(InCollection)) { return PCGEx::H64(*ColIdxPtr, ItemHash); }

				uint32 ColIndex = PCGEx::H32(BaseHash, AssetCollections.Add(InCollection));
				CollectionMap.Add(InCollection, ColIndex);
				return PCGEx::H64(ColIndex, ItemHash);
			}
		}

		void PackToDataset(const UPCGParamData* InAttributeSet)
		{
			FPCGMetadataAttribute<int32>* CollectionIdx = InAttributeSet->Metadata->FindOrCreateAttribute<int32>(Tag_CollectionIdx, 0, false, true, true);
			FPCGMetadataAttribute<FSoftObjectPath>* CollectionPath = InAttributeSet->Metadata->FindOrCreateAttribute<FSoftObjectPath>(Tag_CollectionPath, FSoftObjectPath(), false, true, true);

			for (const TPair<const UPCGExAssetCollection*, uint32>& Pair : CollectionMap)
			{
				const int64 Key = InAttributeSet->Metadata->AddEntry();
				CollectionIdx->SetValue(Key, Pair.Value);
				CollectionPath->SetValue(Key, FSoftObjectPath(Pair.Key));
			}
		}
	};

	template <typename C = UPCGExAssetCollection, typename A = FPCGExAssetCollectionEntry>
	class PCGEXTENDEDTOOLKIT_API TPickUnpacker : public TSharedFromThis<TPickUnpacker<C, A>>
	{
		TMap<uint32, C*> CollectionMap;
		int32 NumUniqueEntries = 0;
		const UPCGBasePointData* PointData = nullptr;

	public:
		TMap<int64, TSharedPtr<TArray<int32>>> HashedPartitions;
		TMap<int64, int32> IndexedPartitions;

		TPickUnpacker()
		{
		}

		bool UnpackDataset(FPCGContext* InContext, const UPCGParamData* InAttributeSet)
		{
			const UPCGMetadata* Metadata = InAttributeSet->Metadata;
			TUniquePtr<FPCGAttributeAccessorKeysEntries> Keys = MakeUnique<FPCGAttributeAccessorKeysEntries>(Metadata);

			const int32 NumEntries = Keys->GetNum();
			if (NumEntries == 0)
			{
				PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Attribute set is empty."));
				return false;
			}

			CollectionMap.Reserve(CollectionMap.Num() + NumEntries);

			const FPCGMetadataAttribute<int32>* CollectionIdx = InAttributeSet->Metadata->GetConstTypedAttribute<int32>(Tag_CollectionIdx);
			const FPCGMetadataAttribute<FSoftObjectPath>* CollectionPath = InAttributeSet->Metadata->GetConstTypedAttribute<FSoftObjectPath>(Tag_CollectionPath);

			if (!CollectionIdx || !CollectionPath)
			{
				PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Missing required attributes, or unsupported type."));
				return false;
			}

			for (int i = 0; i < NumEntries; i++)
			{
				int32 Idx = CollectionIdx->GetValueFromItemKey(i);

				C* Collection = PCGExHelpers::LoadBlocking_AnyThread<C>(TSoftObjectPtr<C>(CollectionPath->GetValueFromItemKey(i)));

				if (!Collection)
				{
					PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Some collections could not be loaded."));
					return false;
				}

				if (CollectionMap.Contains(Idx))
				{
					if (CollectionMap[Idx] == Collection) { continue; }

					PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Collection Idx collision."));
					return false;
				}

				CollectionMap.Add(Idx, Collection);
				NumUniqueEntries += Collection->GetValidEntryNum();
			}

			return true;
		}

		void UnpackPin(FPCGContext* InContext, const FName InPinLabel)
		{
			for (TArray<FPCGTaggedData> Params = InContext->InputData.GetParamsByPin(InPinLabel);
			     const FPCGTaggedData& InTaggedData : Params)
			{
				const UPCGParamData* ParamData = Cast<UPCGParamData>(InTaggedData.Data);

				if (!ParamData) { continue; }
				const TSharedPtr<PCGEx::FAttributesInfos> Infos = PCGEx::FAttributesInfos::Get(ParamData->Metadata);

				if (!ParamData->Metadata->HasAttribute(Tag_CollectionIdx) || !ParamData->Metadata->HasAttribute(Tag_CollectionPath)) { continue; }

				UnpackDataset(InContext, ParamData);
			}
		}

		bool HasValidMapping() const { return !CollectionMap.IsEmpty(); }

		bool ResolveEntry(uint64 EntryHash, const A*& OutEntry, int16& OutSecondaryIndex)
		{
			const UPCGExAssetCollection* EntryHost = nullptr;

			uint32 CollectionIdx = 0;
			uint32 OutEntryIndices = 0;

			uint16 EntryIndex = 0;
			uint16 SecondaryIndex = 0;

			PCGEx::H64(EntryHash, CollectionIdx, OutEntryIndices);
			PCGEx::H32(OutEntryIndices, EntryIndex, SecondaryIndex);
			OutSecondaryIndex = SecondaryIndex - 1; // minus one because we do +1 during packing

			// TODO : Store material entry pick as part of the index

			C** Collection = CollectionMap.Find(CollectionIdx);
			if (!Collection || !(*Collection)->IsValidIndex(EntryIndex)) { return false; }

			return (*Collection)->GetEntryAt(OutEntry, EntryIndex, EntryHost);
		}

		bool BuildPartitions(const UPCGBasePointData* InPointData, TArray<FPCGMeshInstanceList>& InstanceLists)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(TPickUnpacker::BuildPartitions_Indexed);

			FPCGAttributePropertyInputSelector HashSelector;
			HashSelector.Update(Tag_EntryIdx.ToString());

			TUniquePtr<const IPCGAttributeAccessor> HashAttributeAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(InPointData, HashSelector);
			TUniquePtr<const IPCGAttributeAccessorKeys> HashKeys = PCGAttributeAccessorHelpers::CreateConstKeys(InPointData, HashSelector);

			if (!HashAttributeAccessor || !HashKeys) { return false; }

			TArray<int64> Hashes;
			Hashes.SetNumUninitialized(HashKeys->GetNum());

			if (!HashAttributeAccessor->GetRange<int64>(Hashes, 0, *HashKeys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible))
			{
				return false;
			}

			const int32 NumPoints = InPointData->GetNumPoints();
			const int32 SafeReserve = NumPoints / (NumUniqueEntries * 2);

			// Build partitions
			for (int i = 0; i < NumPoints; i++)
			{
				const uint64 EntryHash = Hashes[i];
				if (const int32* Index = IndexedPartitions.Find(EntryHash); !Index)
				{
					FPCGMeshInstanceList& NewInstanceList = InstanceLists.Emplace_GetRef();
					NewInstanceList.AttributePartitionIndex = EntryHash;
					NewInstanceList.PointData = InPointData;
					NewInstanceList.InstancesIndices.Reserve(SafeReserve);
					NewInstanceList.InstancesIndices.Emplace(i);

					IndexedPartitions.Add(EntryHash, InstanceLists.Num() - 1);
				}
				else
				{
					InstanceLists[*Index].InstancesIndices.Emplace(i);
				}
			}

			return !IndexedPartitions.IsEmpty();
		}

		void RetrievePartitions(const UPCGBasePointData* InPointData, TArray<FPCGMeshInstanceList>& InstanceLists)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(TPickUnpacker::BuildPartitions_Indexed);

			PointData = InPointData;

			for (FPCGMeshInstanceList& InstanceList : InstanceLists)
			{
				IndexedPartitions.Add(InstanceList.AttributePartitionIndex, InstanceLists.Num() - 1);
			}
		}

		void InsertEntry(const uint64 EntryHash, const int32 EntryIndex, TArray<FPCGMeshInstanceList>& InstanceLists)
		{
			if (const int32* Index = IndexedPartitions.Find(EntryHash); !Index)
			{
				FPCGMeshInstanceList& NewInstanceList = InstanceLists.Emplace_GetRef();
				NewInstanceList.AttributePartitionIndex = EntryHash;
				NewInstanceList.PointData = PointData;
				NewInstanceList.InstancesIndices.Reserve(PointData->GetNumPoints() / (NumUniqueEntries * 2));
				NewInstanceList.InstancesIndices.Emplace(EntryIndex);

				IndexedPartitions.Add(EntryHash, InstanceLists.Num() - 1);
			}
			else
			{
				InstanceLists[*Index].InstancesIndices.Emplace(EntryIndex);
			}
		}
	};
}

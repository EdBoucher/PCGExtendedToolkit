﻿// Copyright 2025 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "AssetStaging/PCGExAssetStaging.h"

#include "PCGExRandom.h"
#include "PCGExScopedContainers.h"


#define LOCTEXT_NAMESPACE "PCGExAssetStagingElement"
#define PCGEX_NAMESPACE AssetStaging

PCGEX_INITIALIZE_ELEMENT(AssetStaging)

TArray<FPCGPinProperties> UPCGExAssetStagingSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();

	if (CollectionSource == EPCGExCollectionSource::AttributeSet)
	{
		PCGEX_PIN_PARAM(PCGExAssetCollection::SourceAssetCollection, "Attribute set to be used as collection.", Required, {})
	}

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExAssetStagingSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	if (OutputMode == EPCGExStagingOutputMode::CollectionMap)
	{
		PCGEX_PIN_PARAM(PCGExStaging::OutputCollectionMapLabel, "Collection map generated by a staging node.", Required, {})
	}
	return PinProperties;
}

bool FPCGExAssetStagingElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(AssetStaging)

	if (Settings->bOutputMaterialPicks)
	{
		PCGEX_VALIDATE_NAME(Settings->MaterialAttributePrefix)
		Context->bPickMaterials = true;
	}

	if (Settings->CollectionSource == EPCGExCollectionSource::Asset)
	{
		Context->MainCollection = PCGExHelpers::LoadBlocking_AnyThread(Settings->AssetCollection);
		if (!Context->MainCollection)
		{
			PCGE_LOG(Error, GraphAndLog, FTEXT("Missing asset collection."));
			return false;
		}
	}
	else
	{
		if (Settings->OutputMode == EPCGExStagingOutputMode::CollectionMap)
		{
			PCGE_LOG(Error, GraphAndLog, FTEXT("Collection Map output is not supported with collections built from attribute sets."));
			return false;
		}

		Context->MainCollection = Settings->AttributeSetDetails.TryBuildCollection(Context, PCGExAssetCollection::SourceAssetCollection, false);
		if (!Context->MainCollection)
		{
			PCGE_LOG(Error, GraphAndLog, FTEXT("Failed to build collection from attribute set."));
			return false;
		}
	}

	if (Context->bPickMaterials && Context->MainCollection->GetType() != PCGExAssetCollection::EType::Mesh)
	{
		Context->bPickMaterials = false;
		PCGE_LOG(Warning, GraphAndLog, FTEXT("Pick Material is set to true, but the selected collection doesn't support material picking."));
	}

	PCGEX_VALIDATE_NAME(Settings->AssetPathAttributeName)

	if (Settings->WeightToAttribute == EPCGExWeightOutputMode::Raw || Settings->WeightToAttribute == EPCGExWeightOutputMode::Normalized)
	{
		PCGEX_VALIDATE_NAME_CONSUMABLE(Settings->WeightAttributeName)
	}

	if (Settings->OutputMode == EPCGExStagingOutputMode::CollectionMap)
	{
		Context->CollectionPickDatasetPacker = MakeShared<PCGExStaging::FPickPacker>(Context);
	}

	return true;
}


void FPCGExAssetStagingContext::RegisterAssetDependencies()
{
	FPCGExPointsProcessorContext::RegisterAssetDependencies();

	PCGEX_SETTINGS_LOCAL(AssetStaging)

	if (Settings->CollectionSource == EPCGExCollectionSource::AttributeSet)
	{
		MainCollection->GetAssetPaths(GetRequiredAssets(), PCGExAssetCollection::ELoadingFlags::Recursive);
	}
	else
	{
		MainCollection->GetAssetPaths(GetRequiredAssets(), PCGExAssetCollection::ELoadingFlags::RecursiveCollectionsOnly);
	}
}


void FPCGExAssetStagingElement::PostLoadAssetsDependencies(FPCGExContext* InContext) const
{
	FPCGExPointsProcessorElement::PostLoadAssetsDependencies(InContext);

	PCGEX_CONTEXT_AND_SETTINGS(AssetStaging)

	if (Settings->CollectionSource == EPCGExCollectionSource::AttributeSet)
	{
		// Internal collection, assets have been loaded at this point
		Context->MainCollection->RebuildStagingData(true);
	}
}

bool FPCGExAssetStagingElement::PostBoot(FPCGExContext* InContext) const
{
	PCGEX_CONTEXT_AND_SETTINGS(AssetStaging)

	if (Context->MainCollection->LoadCache()->IsEmpty())
	{
		if (!Settings->bQuietEmptyCollectionError)
		{
			PCGE_LOG_C(Error, GraphAndLog, Context, FTEXT("Selected asset collection is empty."));
		}

		return false;
	}

	return FPCGExPointsProcessorElement::PostBoot(InContext);
}

bool FPCGExAssetStagingElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExAssetStagingElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(AssetStaging)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartBatchProcessingPoints<PCGExPointsMT::TBatch<PCGExAssetStaging::FProcessor>>(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry) { return true; },
			[&](const TSharedPtr<PCGExPointsMT::TBatch<PCGExAssetStaging::FProcessor>>& NewBatch)
			{
				NewBatch->bRequiresWriteStep = Settings->bPruneEmptyPoints;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points to process."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::State_Done)

	Context->MainPoints->StageOutputs();

	if (Settings->OutputMode == EPCGExStagingOutputMode::CollectionMap)
	{
		UPCGParamData* OutputSet = NewObject<UPCGParamData>();
		Context->CollectionPickDatasetPacker->PackToDataset(OutputSet);

		FPCGTaggedData& OutData = Context->OutputData.TaggedData.Emplace_GetRef();
		OutData.Pin = PCGExStaging::OutputCollectionMapLabel;
		OutData.Data = OutputSet;
	}

	return Context->TryComplete();
}

bool FPCGExAssetStagingElement::CanExecuteOnlyOnMainThread(FPCGContext* Context) const
{
	// Loading collection and/or creating one from attributes
	return Context ? Context->CurrentPhase == EPCGExecutionPhase::PrepareData : false;
}

namespace PCGExAssetStaging
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InAsyncManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExAssetStaging::Process);

		// Must be set before process for filters
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InAsyncManager)) { return false; }

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)

		NumPoints = PointDataFacade->GetNum();

		if (Context->bPickMaterials)
		{
			CachedPicks.Init(nullptr, NumPoints);
			MaterialPick.Init(-1, NumPoints);
		}

		FittingHandler.ScaleToFit = Settings->ScaleToFit;
		FittingHandler.Justification = Settings->Justification;

		if (!FittingHandler.Init(ExecutionContext, PointDataFacade)) { return false; }

		Variations = Settings->Variations;
		Variations.Init(Settings->Seed);


		Helper = MakeUnique<PCGExAssetCollection::TDistributionHelper<UPCGExAssetCollection, FPCGExAssetCollectionEntry>>(Context->MainCollection, Settings->DistributionSettings);
		if (!Helper->Init(ExecutionContext, PointDataFacade)) { return false; }

		bOutputWeight = Settings->WeightToAttribute != EPCGExWeightOutputMode::NoOutput;
		bNormalizedWeight = Settings->WeightToAttribute != EPCGExWeightOutputMode::Raw;
		bOneMinusWeight = Settings->WeightToAttribute == EPCGExWeightOutputMode::NormalizedInverted || Settings->WeightToAttribute == EPCGExWeightOutputMode::NormalizedInvertedToDensity;

		if (Settings->WeightToAttribute == EPCGExWeightOutputMode::Raw)
		{
			WeightWriter = PointDataFacade->GetWritable<int32>(Settings->WeightAttributeName, PCGExData::EBufferInit::New);
		}
		else if (Settings->WeightToAttribute == EPCGExWeightOutputMode::Normalized)
		{
			NormalizedWeightWriter = PointDataFacade->GetWritable<double>(Settings->WeightAttributeName, PCGExData::EBufferInit::New);
		}

		if (Settings->OutputMode == EPCGExStagingOutputMode::Attributes)
		{
			bInherit = PointDataFacade->GetIn()->Metadata->HasAttribute(Settings->AssetPathAttributeName);
			PathWriter = PointDataFacade->GetWritable<FSoftObjectPath>(Settings->AssetPathAttributeName, bInherit ? PCGExData::EBufferInit::Inherit : PCGExData::EBufferInit::New);
		}
		else
		{
			bInherit = PointDataFacade->GetIn()->Metadata->HasAttribute(PCGExStaging::Tag_EntryIdx);
			HashWriter = PointDataFacade->GetWritable<int64>(PCGExStaging::Tag_EntryIdx, bInherit ? PCGExData::EBufferInit::Inherit : PCGExData::EBufferInit::New);
		}

		// Cherry pick native properties allocations

		EPCGPointNativeProperties AllocateFor = EPCGPointNativeProperties::None;

		AllocateFor |= EPCGPointNativeProperties::BoundsMin;
		AllocateFor |= EPCGPointNativeProperties::BoundsMax;
		AllocateFor |= EPCGPointNativeProperties::Transform;
		if (bOutputWeight && !WeightWriter && !NormalizedWeightWriter)
		{
			bUsesDensity = true;
			AllocateFor |= EPCGPointNativeProperties::Density;
		}

		PointDataFacade->GetOut()->AllocateProperties(AllocateFor);

		if (Settings->bPruneEmptyPoints) { Mask.Init(1, PointDataFacade->GetNum()); }

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops)
	{
		HighestSlotIndex = MakeShared<PCGExMT::TScopedNumericValue<int8>>(Loops, -1);
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::AssetStaging::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		UPCGBasePointData* OutPointData = PointDataFacade->GetOut();

		const TPCGValueRange<FTransform> OutTransforms = OutPointData->GetTransformValueRange(false);
		const TPCGValueRange<FVector> OutBoundsMin = OutPointData->GetBoundsMinValueRange(false);
		const TPCGValueRange<FVector> OutBoundsMax = OutPointData->GetBoundsMaxValueRange(false);
		const TConstPCGValueRange<int32> Seeds = OutPointData->GetConstSeedValueRange();
		const TPCGValueRange<float> Densities = bUsesDensity ? OutPointData->GetDensityValueRange(false) : TPCGValueRange<float>();

		int32 LocalNumInvalid = 0;

		auto InvalidPoint = [&](const int32 Index)
		{
			if (bInherit) { return; }

			if (Settings->bPruneEmptyPoints)
			{
				Mask[Index] = 0;
				LocalNumInvalid++;
				return;
			}

			if (PathWriter) { PathWriter->SetValue(Index, FSoftObjectPath{}); }
			else { HashWriter->SetValue(Index, -1); }

			if (bOutputWeight)
			{
				if (WeightWriter) { WeightWriter->SetValue(Index, -1); }
				else if (NormalizedWeightWriter) { NormalizedWeightWriter->SetValue(Index, -1); }
			}

			if (Context->bPickMaterials) { MaterialPick[Index] = -1; }
		};

		PCGEX_SCOPE_LOOP(Index)
		{
			if (!PointFilterCache[Index])
			{
				InvalidPoint(Index);
				continue;
			}

			PCGExData::FMutablePoint MutablePoint(OutPointData, Index);

			const FPCGExAssetCollectionEntry* Entry = nullptr;
			const UPCGExAssetCollection* EntryHost = nullptr;

			const int32 Seed = PCGExRandom::GetSeed(
				Seeds[Index], Helper->Details.SeedComponents,
				Helper->Details.LocalSeed, Settings, Context->GetComponent());

			Helper->GetEntry(Entry, Index, Seed, EntryHost);

			if (!Entry || !Entry->Staging.Bounds.IsValid)
			{
				InvalidPoint(Index);
				continue;
			}

			int16 SecondaryIndex = -1;

			if (Entry->MacroCache && Entry->MacroCache->GetType() == PCGExAssetCollection::EType::Mesh)
			{
				TSharedPtr<PCGExMeshCollection::FMacroCache> EntryMacroCache = StaticCastSharedPtr<PCGExMeshCollection::FMacroCache>(Entry->MacroCache);
				if (Context->bPickMaterials)
				{
					MaterialPick[Index] = EntryMacroCache->GetPickRandomWeighted(Seed);
					HighestSlotIndex->Set(Scope, FMath::Max(FMath::Max(0, EntryMacroCache->GetHighestIndex()), HighestSlotIndex->Get(Scope)));
					CachedPicks[Index] = Entry;
				}
				else
				{
					SecondaryIndex = EntryMacroCache->GetPickRandomWeighted(Seed);
				}
			}
			else if (Context->bPickMaterials)
			{
				MaterialPick[Index] = -1;
			}

			if (bOutputWeight)
			{
				double Weight = bNormalizedWeight ? static_cast<double>(Entry->Weight) / static_cast<double>(Context->MainCollection->LoadCache()->WeightSum) : Entry->Weight;
				if (bOneMinusWeight) { Weight = 1 - Weight; }
				if (WeightWriter) { WeightWriter->SetValue(Index, Weight); }
				if (NormalizedWeightWriter) { NormalizedWeightWriter->SetValue(Index, Weight); }
				else { Densities[Index] = Weight; }
			}

			if (PathWriter) { PathWriter->SetValue(Index, Entry->Staging.Path); }
			else { HashWriter->SetValue(Index, Context->CollectionPickDatasetPacker->GetPickIdx(EntryHost, Entry->Staging.InternalIndex, SecondaryIndex)); }

			FBox OutBounds = Entry->Staging.Bounds;
			PCGExData::FProxyPoint ProxyPoint(MutablePoint);

			if (Variations.bEnabledBefore)
			{
				if (EntryHost->GlobalVariationMode == EPCGExGlobalVariationRule::Overrule ||
					Entry->VariationMode == EPCGExEntryVariationMode::Global)
				{
					Variations.Apply(Seed, ProxyPoint, EntryHost->GlobalVariations, EPCGExVariationMode::Before);
				}
				else
				{
					Variations.Apply(Seed, ProxyPoint, Entry->Variations, EPCGExVariationMode::Before);
				}

				FittingHandler.ComputeTransform(Index, ProxyPoint.Transform, OutBounds);
			}
			else
			{
				FittingHandler.ComputeTransform(Index, ProxyPoint.Transform, OutBounds);
			}

			OutBoundsMin[Index] = OutBounds.Min;
			OutBoundsMax[Index] = OutBounds.Max;

			if (Variations.bEnabledAfter)
			{
				if (EntryHost->GlobalVariationMode == EPCGExGlobalVariationRule::Overrule ||
					Entry->VariationMode == EPCGExEntryVariationMode::Global)
				{
					Variations.Apply(Seed, ProxyPoint, EntryHost->GlobalVariations, EPCGExVariationMode::After);
				}
				else
				{
					Variations.Apply(Seed, ProxyPoint, Entry->Variations, EPCGExVariationMode::After);
				}
			}

			OutTransforms[Index] = ProxyPoint.Transform;
		}

		FPlatformAtomics::InterlockedAdd(&NumInvalid, LocalNumInvalid);
	}

	void FProcessor::CompleteWork()
	{
		if (Context->bPickMaterials)
		{
			int8 WriterCount = HighestSlotIndex->Max() + 1;
			if (Settings->MaxMaterialPicks > 0) { WriterCount = Settings->MaxMaterialPicks; }

			if (WriterCount > 0)
			{
				MaterialWriters.Init(nullptr, WriterCount);

				for (int i = 0; i < WriterCount; i++)
				{
					const FName AttributeName = FName(FString::Printf(TEXT("%s_%d"), *Settings->MaterialAttributePrefix.ToString(), i));
					MaterialWriters[i] = PointDataFacade->GetWritable<FSoftObjectPath>(AttributeName, FSoftObjectPath(), true, PCGExData::EBufferInit::New);
				}

				StartParallelLoopForRange(NumPoints);
				return;
			}

			PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("No material were picked -- no attribute will be written."));
		}

		PointDataFacade->WriteFastest(AsyncManager);
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		PCGEX_SCOPE_LOOP(Index)
		{
			const int32 Pick = MaterialPick[Index];

			if (Pick == -1 || (Settings->bPruneEmptyPoints && !Mask[Index])) { continue; }

			const FPCGExMeshCollectionEntry* Entry = static_cast<const FPCGExMeshCollectionEntry*>(CachedPicks[Index]);
			if (Entry->MaterialVariants == EPCGExMaterialVariantsMode::None) { continue; }
			if (Entry->MaterialVariants == EPCGExMaterialVariantsMode::Single)
			{
				if (!MaterialWriters.IsValidIndex(Entry->SlotIndex)) { continue; }
				MaterialWriters[Entry->SlotIndex]->SetValue(Index, Entry->MaterialOverrideVariants[Pick].Material.ToSoftObjectPath());
			}
			else if (Entry->MaterialVariants == EPCGExMaterialVariantsMode::Multi)
			{
				const FPCGExMaterialOverrideCollection& MEntry = Entry->MaterialOverrideVariantsList[Pick];

				for (int i = 0; i < MEntry.Overrides.Num(); i++)
				{
					const FPCGExMaterialOverrideEntry& SlotEntry = MEntry.Overrides[i];

					const int32 SlotIndex = SlotEntry.SlotIndex == -1 ? 0 : SlotEntry.SlotIndex;
					if (!MaterialWriters.IsValidIndex(SlotIndex)) { continue; }
					MaterialWriters[SlotIndex]->SetValue(Index, SlotEntry.Material.ToSoftObjectPath());
				}
			}
		}
	}

	void FProcessor::OnRangeProcessingComplete()
	{
		PointDataFacade->WriteFastest(AsyncManager);
	}

	void FProcessor::Write()
	{
		(void)PointDataFacade->Source->Gather(Mask);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE

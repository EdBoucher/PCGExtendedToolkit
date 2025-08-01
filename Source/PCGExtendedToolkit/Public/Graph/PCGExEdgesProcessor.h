﻿// Copyright 2025 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCluster.h"
#include "PCGExClusterMT.h"
#include "PCGExClusterUtils.h"
#include "PCGExPointsProcessor.h"

#include "PCGExEdgesProcessor.generated.h"

#define PCGEX_CLUSTER_BATCH_PROCESSING(_STATE) if (!Context->ProcessClusters(_STATE)) { return false; }

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), meta=(PCGExNodeLibraryDoc="TBD"))
class PCGEXTENDEDTOOLKIT_API UPCGExEdgesProcessorSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(EdgesProcessorSettings, "Edges Processor Settings", "TOOLTIP_TEXT");
	virtual FLinearColor GetNodeTitleColor() const override { return GetDefault<UPCGExGlobalSettings>()->NodeColorEdge; }
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual bool SupportsEdgeSorting() const;
	virtual bool RequiresEdgeSorting() const;
	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const;

	virtual FName GetMainInputPin() const override { return PCGExGraph::SourceVerticesLabel; }
	virtual FName GetMainOutputPin() const override { return PCGExGraph::OutputVerticesLabel; }

	virtual bool GetMainAcceptMultipleData() const override;
	//~End UPCGExPointsProcessorSettings

	/** Whether scoped attribute read is enabled or not. Disabling this on small dataset may greatly improve performance. It's enabled by default for legacy reasons. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Performance, meta=(PCG_NotOverridable, AdvancedDisplay))
	EPCGExOptionState ScopedIndexLookupBuild = EPCGExOptionState::Default;

	/** */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors", meta=(PCG_NotOverridable, AdvancedDisplay))
	bool bQuietMissingClusterPairElement = false;

	bool WantsScopedIndexLookupBuild() const;
};

struct PCGEXTENDEDTOOLKIT_API FPCGExEdgesProcessorContext : FPCGExPointsProcessorContext
{
	friend class UPCGExEdgesProcessorSettings;
	friend class FPCGExEdgesProcessorElement;

	virtual ~FPCGExEdgesProcessorContext() override;

	bool bQuietMissingClusterPairElement = false;

	TSharedPtr<PCGExData::FPointIOCollection> MainEdges;
	TSharedPtr<PCGExClusterUtils::FClusterDataLibrary> ClusterDataLibrary;
	TSharedPtr<PCGExData::FPointIOTaggedEntries> TaggedEdges;

	const TArray<FPCGExSortRuleConfig>* GetEdgeSortingRules() const;

	virtual bool AdvancePointsIO(const bool bCleanupKeys = true) override;

	TSharedPtr<PCGExCluster::FCluster> CurrentCluster;

	void OutputPointsAndEdges() const;

	FPCGExGraphBuilderDetails GraphBuilderDetails;

	int32 GetClusterProcessorsNum() const;

	template <typename T>
	void GatherClusterProcessors(TArray<TSharedPtr<T>>& OutProcessors)
	{
		OutProcessors.Reserve(GetClusterProcessorsNum());
		for (const TSharedPtr<PCGExClusterMT::IBatch>& Batch : Batches)
		{
			PCGExClusterMT::TBatch<T>* TypedBatch = static_cast<PCGExClusterMT::TBatch<T>*>(Batch.Get());
			OutProcessors.Append(TypedBatch->Processors);
		}
	}

	void OutputBatches() const;

protected:
	mutable FRWLock ClusterProcessingLock;
	TArray<TObjectPtr<const UPCGExHeuristicsFactoryData>> HeuristicsFactories;

public:
	virtual bool ProcessClusters(const PCGExCommon::ContextState NextStateId, const bool bIsNextStateAsync = false);

protected:
	virtual bool CompileGraphBuilders(const bool bOutputToContext, const PCGExCommon::ContextState NextStateId);

	TArray<FPCGExSortRuleConfig> EdgeSortingRules;

	TArray<TSharedPtr<PCGExClusterMT::IBatch>> Batches;
	TArray<TSharedRef<PCGExData::FFacade>> EdgesDataFacades;

	bool bScopedIndexLookupBuild = false;
	bool bHasValidHeuristics = false;

	bool bSkipClusterBatchCompletionStep = false;
	bool bDoClusterBatchWritingStep = false;

	bool bClusterWantsHeuristics = false;
	bool bClusterBatchInlined = false;
	int32 CurrentBatchIndex = -1;
	TSharedPtr<PCGExClusterMT::IBatch> CurrentBatch;

	template <typename T, class ValidateEntriesFunc, class InitBatchFunc>
	bool StartProcessingClusters(ValidateEntriesFunc&& ValidateEntries, InitBatchFunc&& InitBatch, const bool bInlined = false)
	{
		ResumeExecution();

		Batches.Empty();

		bClusterBatchInlined = bInlined;
		CurrentBatchIndex = -1;

		bBatchProcessingEnabled = false;
		bClusterWantsHeuristics = true;
		bSkipClusterBatchCompletionStep = false;
		bDoClusterBatchWritingStep = false;

		Batches.Reserve(MainPoints->Pairs.Num());

		EdgesDataFacades.Reserve(MainEdges->Pairs.Num());
		for (const TSharedPtr<PCGExData::FPointIO>& EdgeIO : MainEdges->Pairs)
		{
			TSharedPtr<PCGExData::FFacade> EdgeFacade = MakeShared<PCGExData::FFacade>(EdgeIO.ToSharedRef());
			EdgesDataFacades.Add(EdgeFacade.ToSharedRef());
		}

		while (AdvancePointsIO(false))
		{
			if (!TaggedEdges)
			{
				PCGE_LOG_C(Warning, GraphAndLog, this, FTEXT("Some input points have no bound edges."));
				continue;
			}

			if (!ValidateEntries(TaggedEdges)) { continue; }

			PCGEX_MAKE_SHARED(NewBatch, T, this, CurrentIO.ToSharedRef(), TaggedEdges->Entries);
			InitBatch(NewBatch);

			if (NewBatch->bRequiresWriteStep) { bDoClusterBatchWritingStep = true; }
			if (NewBatch->bSkipCompletion) { bSkipClusterBatchCompletionStep = true; }
			if (NewBatch->RequiresGraphBuilder()) { NewBatch->GraphBuilderDetails = GraphBuilderDetails; }

			if (NewBatch->WantsHeuristics())
			{
				bClusterWantsHeuristics = true;
				if (!bHasValidHeuristics)
				{
					PCGE_LOG_C(Error, GraphAndLog, this, FTEXT("Missing Heuristics."));
					return false;
				}
				NewBatch->HeuristicsFactories = &HeuristicsFactories;
			}

			NewBatch->EdgesDataFacades = &EdgesDataFacades;

			Batches.Add(NewBatch);
			if (!bClusterBatchInlined) { PCGExClusterMT::ScheduleBatch(GetAsyncManager(), NewBatch, bScopedIndexLookupBuild); }
		}

		if (Batches.IsEmpty()) { return false; }

		bBatchProcessingEnabled = true;
		if (!bClusterBatchInlined) { SetAsyncState(PCGExClusterMT::MTState_ClusterProcessing); }
		return true;
	}

	virtual void ClusterProcessing_InitialProcessingDone();
	virtual void ClusterProcessing_WorkComplete();
	virtual void ClusterProcessing_WritingDone();
	virtual void ClusterProcessing_GraphCompilationDone();

	void AdvanceBatch(const PCGExCommon::ContextState NextStateId, const bool bIsNextStateAsync);

	int32 CurrentEdgesIndex = -1;
};

class PCGEXTENDEDTOOLKIT_API FPCGExEdgesProcessorElement : public FPCGExPointsProcessorElement
{
	virtual void DisabledPassThroughData(FPCGContext* Context) const override;

protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(EdgesProcessor)
	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual void OnContextInitialized(FPCGExPointsProcessorContext* InContext) const override;
};

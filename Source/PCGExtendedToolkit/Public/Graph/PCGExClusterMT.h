// Copyright 2025 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExGraph.h"
#include "PCGExCluster.h"
#include "PCGExEdgeDirectionSettings.h"
#include "Data/PCGExData.h"
#include "Geometry/PCGExGeo.h"

#include "Pathfinding/Heuristics/PCGExHeuristics.h"


namespace PCGExClusterFilter
{
	class FManager;
}

namespace PCGExClusterMT
{
	PCGEX_CTX_STATE(MTState_ClusterProcessing)
	PCGEX_CTX_STATE(MTState_ClusterCompletingWork)
	PCGEX_CTX_STATE(MTState_ClusterWriting)

#pragma region Tasks

#define PCGEX_ASYNC_CLUSTER_PROCESSOR_LOOP(_NAME, _NUM, _PREPARE, _PROCESS, _COMPLETE, _INLINE) PCGEX_ASYNC_PROCESSOR_LOOP(_NAME, _NUM, _PREPARE, _PROCESS, _COMPLETE, _INLINE, GetClusterBatchChunkSize)

	template <typename T>
	class FStartClusterBatchProcessing final : public PCGExMT::FTask
	{
	public:
		PCGEX_ASYNC_TASK_NAME(FStartClusterBatchProcessing)

		FStartClusterBatchProcessing(TSharedPtr<T> InTarget,
		                             const bool bScoped)
			: FTask(),
			  Target(InTarget),
			  bScopedIndexLookupBuild(bScoped)
		{
		}

		TSharedPtr<T> Target;
		bool bScopedIndexLookupBuild = false;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& AsyncManager) override
		{
			Target->PrepareProcessing(AsyncManager, bScopedIndexLookupBuild);
		}
	};

#pragma endregion

	class IBatch;

	class PCGEXTENDEDTOOLKIT_API IProcessor : public TSharedFromThis<IProcessor>
	{
		friend class IBatch;

	protected:
		FPCGExContext* ExecutionContext = nullptr;
		TWeakPtr<PCGEx::FWorkPermit> WorkPermit;
		TSharedPtr<PCGExMT::FTaskManager> AsyncManager;

		const TArray<TObjectPtr<const UPCGExHeuristicsFactoryData>>* HeuristicsFactories = nullptr;
		FPCGExEdgeDirectionSettings DirectionSettings;

		bool bWantsProjection = false;
		FPCGExGeo2DProjectionDetails ProjectionDetails;
		TSharedPtr<TArray<FVector2D>> ProjectedVtxPositions;

		bool bBuildCluster = true;
		bool bWantsHeuristics = false;

		bool bDaisyChainProcessNodes = false;
		bool bDaisyChainProcessEdges = false;
		bool bDaisyChainProcessRange = false;

		int32 NumNodes = 0;
		int32 NumEdges = 0;

		virtual TSharedPtr<PCGExCluster::FCluster> HandleCachedCluster(const TSharedRef<PCGExCluster::FCluster>& InClusterRef);

		void ForwardCluster() const;

	public:
		TSharedRef<PCGExData::FFacade> VtxDataFacade;
		TSharedRef<PCGExData::FFacade> EdgeDataFacade;

		TSharedPtr<PCGEx::FIndexLookup> NodeIndexLookup;

		TWeakPtr<IBatch> ParentBatch;

		template <typename T>
		T* GetParentBatch() { return static_cast<T*>(ParentBatch.Pin().Get()); }

		TSharedPtr<PCGExMT::FTaskManager> GetAsyncManager() { return AsyncManager; }

		bool bAllowEdgesDataFacadeScopedGet = false;

		bool bIsProcessorValid = false;

		TSharedPtr<PCGExHeuristics::FHeuristicsHandler> HeuristicsHandler;

		bool bIsTrivial = false;
		bool bIsOneToOne = false;

		int32 BatchIndex = -1;

		TMap<uint32, int32>* EndpointsLookup = nullptr;
		TArray<int32>* ExpectedAdjacency = nullptr;

		TSharedPtr<PCGExCluster::FCluster> Cluster;

		TSharedPtr<PCGExGraph::FGraphBuilder> GraphBuilder;

		IProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade);

		virtual void SetExecutionContext(FPCGExContext* InContext);

		void SetProjectionDetails(const FPCGExGeo2DProjectionDetails& InDetails, const TSharedPtr<TArray<FVector2D>>& InProjectedVtxPositions, const bool InWantsProjection);

		virtual ~IProcessor() = default;

		virtual void RegisterConsumableAttributesWithFacade() const;

		virtual bool IsTrivial() const { return bIsTrivial; }

		void SetWantsHeuristics(const bool bRequired, const TArray<TObjectPtr<const UPCGExHeuristicsFactoryData>>* InHeuristicsFactories);

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InAsyncManager);

#pragma region Parallel loops

		void StartParallelLoopForNodes(const int32 PerLoopIterations = -1);
		virtual void PrepareLoopScopesForNodes(const TArray<PCGExMT::FScope>& Loops);
		virtual void ProcessNodes(const PCGExMT::FScope& Scope);
		virtual void OnNodesProcessingComplete();

		void StartParallelLoopForEdges(const int32 PerLoopIterations = -1);
		virtual void PrepareLoopScopesForEdges(const TArray<PCGExMT::FScope>& Loops);
		virtual void ProcessEdges(const PCGExMT::FScope& Scope);
		virtual void OnEdgesProcessingComplete();

		void StartParallelLoopForRange(const int32 NumIterations, const int32 PerLoopIterations = -1);
		virtual void PrepareLoopScopesForRanges(const TArray<PCGExMT::FScope>& Loops);
		virtual void ProcessRange(const PCGExMT::FScope& Scope);
		virtual void OnRangeProcessingComplete();

#pragma endregion

		virtual void CompleteWork();
		virtual void Write();
		virtual void Output();
		virtual void Cleanup();

		const TArray<TObjectPtr<const UPCGExFilterFactoryData>>* VtxFilterFactories = nullptr;
		TSharedPtr<TArray<int8>> VtxFilterCache;

		const TArray<TObjectPtr<const UPCGExFilterFactoryData>>* EdgeFilterFactories = nullptr;
		TArray<int8> EdgeFilterCache;

	protected:
		TSharedPtr<PCGExClusterFilter::FManager> VtxFiltersManager;
		virtual bool InitVtxFilters(const TArray<TObjectPtr<const UPCGExFilterFactoryData>>* InFilterFactories);
		virtual void FilterVtxScope(const PCGExMT::FScope& Scope);

		FORCEINLINE bool IsNodePassingFilters(const PCGExCluster::FNode& Node) const { return static_cast<bool>(*(VtxFilterCache->GetData() + Node.PointIndex)); }

		bool DefaultEdgeFilterValue = true;
		TSharedPtr<PCGExClusterFilter::FManager> EdgesFiltersManager;
		virtual bool InitEdgesFilters(const TArray<TObjectPtr<const UPCGExFilterFactoryData>>* InFilterFactories);
		virtual void FilterEdgeScope(const PCGExMT::FScope& Scope);
	};

	template <typename TContext, typename TSettings>
	class TProcessor : public IProcessor
	{
	protected:
		TContext* Context = nullptr;
		const TSettings* Settings = nullptr;

	public:
		TProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade):
			IProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		virtual void SetExecutionContext(FPCGExContext* InContext) override
		{
			IProcessor::SetExecutionContext(InContext);
			Context = static_cast<TContext*>(ExecutionContext);
			Settings = InContext->GetInputSettings<TSettings>();
		}

		TContext* GetContext() { return Context; }
		const TSettings* GetSettings() { return Settings; }
	};

	class PCGEXTENDEDTOOLKIT_API IBatch : public TSharedFromThis<IBatch>
	{
	protected:
		TSharedPtr<PCGEx::FIntTracker> InitializationTracker = nullptr;

		mutable FRWLock BatchLock;
		TSharedPtr<PCGEx::FIndexLookup> NodeIndexLookup;

		TSharedPtr<PCGExMT::FTaskManager> AsyncManager;
		TSharedPtr<PCGExData::FFacadePreloader> VtxFacadePreloader;

		const FPCGMetadataAttribute<int64>* RawLookupAttribute = nullptr;
		TArray<uint32> ReverseLookup;

		TMap<uint32, int32> EndpointsLookup;
		TArray<int32> ExpectedAdjacency;

		bool bPreparationSuccessful = false;
		bool bWantsHeuristics = false;
		bool bRequiresGraphBuilder = false;

		bool bWantsProjection = false;
		bool bWantsPerClusterProjection = false;
		FPCGExGeo2DProjectionDetails ProjectionDetails;
		TSharedPtr<TArray<FVector2D>> ProjectedVtxPositions;

	public:
		bool bIsBatchValid = true;
		FPCGExContext* ExecutionContext = nullptr;
		TWeakPtr<PCGEx::FWorkPermit> WorkPermit;
		const TArray<TObjectPtr<const UPCGExHeuristicsFactoryData>>* HeuristicsFactories = nullptr;

		const TSharedRef<PCGExData::FFacade> VtxDataFacade;
		bool bAllowVtxDataFacadeScopedGet = false;

		bool bSkipCompletion = false;
		bool bRequiresWriteStep = false;
		bool bWriteVtxDataFacade = false;
		EPCGPointNativeProperties AllocateVtxProperties = EPCGPointNativeProperties::None;

		TArray<TSharedPtr<PCGExData::FPointIO>> Edges;
		TArray<TSharedRef<PCGExData::FFacade>>* EdgesDataFacades = nullptr;
		TWeakPtr<PCGExData::FPointIOCollection> GraphEdgeOutputCollection;

		TSharedPtr<PCGExGraph::FGraphBuilder> GraphBuilder;
		FPCGExGraphBuilderDetails GraphBuilderDetails;

		TArray<TSharedPtr<PCGExCluster::FCluster>> ValidClusters;

		const TArray<TObjectPtr<const UPCGExFilterFactoryData>>* VtxFilterFactories = nullptr;
		const TArray<TObjectPtr<const UPCGExFilterFactoryData>>* EdgeFilterFactories = nullptr;
		bool DefaultVtxFilterValue = true;
		TSharedPtr<TArray<int8>> VtxFilterCache;

		virtual int32 GetNumProcessors() const { return -1; }

		bool PreparationSuccessful() const { return bPreparationSuccessful; }
		bool RequiresGraphBuilder() const { return bRequiresGraphBuilder; }
		bool WantsHeuristics() const { return bWantsHeuristics; }
		virtual void SetWantsHeuristics(const bool bRequired) { bWantsHeuristics = bRequired; }

		bool bDaisyChainProcessing = false;
		bool bDaisyChainCompletion = false;
		bool bDaisyChainWrite = false;

		IBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges);

		virtual ~IBatch() = default;

		virtual void SetExecutionContext(FPCGExContext* InContext);

		template <typename T>
		T* GetContext() { return static_cast<T*>(ExecutionContext); }

		bool WantsProjection() const { return bWantsProjection; }
		bool WantsPerClusterProjection() const { return bWantsPerClusterProjection; }
		virtual void SetProjectionDetails(const FPCGExGeo2DProjectionDetails& InProjectionDetails);

		virtual void PrepareProcessing(const TSharedPtr<PCGExMT::FTaskManager> AsyncManagerPtr, const bool bScopedIndexLookupBuild);
		virtual void RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader);
		virtual void OnProcessingPreparationComplete();

		virtual void Process();

	protected:
		virtual void OnInitialPostProcess();

	public:
		virtual void CompleteWork();
		virtual void Write();

		virtual const PCGExGraph::FGraphMetadataDetails* GetGraphMetadataDetails();

		virtual void CompileGraphBuilder(const bool bOutputToContext);

		virtual void Output();
		virtual void Cleanup();

	protected:
		void InternalInitProcessor(const TSharedPtr<IProcessor>& InProcessor, const int32 InIndex);

		virtual void AllocateVtxPoints();
	};

	template <typename T>
	class TBatch : public IBatch
	{
	public:
		TArray<TSharedRef<T>> Processors;
		TArray<TSharedRef<T>> TrivialProcessors;

		std::atomic<PCGExCommon::ContextState> CurrentState{PCGExCommon::State_InitialExecution};

		virtual int32 GetNumProcessors() const override { return Processors.Num(); }

		TBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, const TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges):
			IBatch(InContext, InVtx, InEdges)
		{
		}

		int32 GatherValidClusters()
		{
			ValidClusters.Empty();

			for (const TSharedPtr<T>& P : Processors)
			{
				if (!P->Cluster) { continue; }
				ValidClusters.Add(P->Cluster);
			}
			return ValidClusters.Num();
		}

		virtual void Process() override
		{
			if (!bIsBatchValid) { return; }

			IBatch::Process();
			if (!bIsBatchValid) { return; }

			CurrentState.store(PCGExCommon::State_Processing, std::memory_order_release);

			for (const TSharedPtr<PCGExData::FPointIO>& IO : Edges)
			{
				const TSharedPtr<T> NewProcessor = MakeShared<T>(VtxDataFacade, (*EdgesDataFacades)[IO->IOIndex]);

				InternalInitProcessor(NewProcessor, Processors.Num());

				if (!PrepareSingle(NewProcessor)) { continue; }

				Processors.Add(NewProcessor.ToSharedRef());

				NewProcessor->bIsTrivial = IO->GetNum() < GetDefault<UPCGExGlobalSettings>()->SmallClusterSize;
				if (NewProcessor->IsTrivial()) { TrivialProcessors.Add(NewProcessor.ToSharedRef()); }
			}

			StartProcessing();
		}

		virtual void StartProcessing()
		{
			if (!bIsBatchValid) { return; }

			InitializationTracker = MakeShared<PCGEx::FIntTracker>(
				[PCGEX_ASYNC_THIS_CAPTURE]()
				{
					PCGEX_ASYNC_THIS
					This->OnInitialPostProcess();
				});

			PCGEX_ASYNC_MT_LOOP_TPL(Process, bDaisyChainProcessing, { Processor->bIsProcessorValid = Processor->Process(This->AsyncManager); }, InitializationTracker)
		}

		virtual bool PrepareSingle(const TSharedPtr<T>& ClusterProcessor) { return true; }

		virtual void CompleteWork() override
		{
			if (bSkipCompletion) { return; }
			if (!bIsBatchValid) { return; }

			CurrentState.store(PCGExCommon::State_Completing, std::memory_order_release);
			PCGEX_ASYNC_MT_LOOP_VALID_PROCESSORS(CompleteWork, bDaisyChainCompletion, { Processor->CompleteWork(); })
			IBatch::CompleteWork();
		}

		virtual void Write() override
		{
			if (!bIsBatchValid) { return; }

			CurrentState.store(PCGExCommon::State_Writing, std::memory_order_release);
			PCGEX_ASYNC_MT_LOOP_VALID_PROCESSORS(Write, bDaisyChainWrite, { Processor->Write(); })
			IBatch::Write();
		}

		virtual void Output() override
		{
			if (!bIsBatchValid) { return; }
			for (const TSharedPtr<T>& P : Processors)
			{
				if (!P->bIsProcessorValid) { continue; }
				P->Output();
			}
		}

		virtual void Cleanup() override
		{
			IBatch::Cleanup();
			for (const TSharedRef<T>& P : Processors) { P->Cleanup(); }
			Processors.Empty();
		}
	};

	static void ScheduleBatch(const TSharedPtr<PCGExMT::FTaskManager>& AsyncManager, const TSharedPtr<IBatch>& Batch, const bool bScopedIndexLookupBuild)
	{
		PCGEX_LAUNCH(FStartClusterBatchProcessing<IBatch>, Batch, bScopedIndexLookupBuild)
	}

	static void CompleteBatches(const TArrayView<TSharedPtr<IBatch>> Batches)
	{
		for (const TSharedPtr<IBatch>& Batch : Batches) { Batch->CompleteWork(); }
	}

	static void WriteBatches(const TArrayView<TSharedPtr<IBatch>> Batches)
	{
		for (const TSharedPtr<IBatch>& Batch : Batches) { Batch->Write(); }
	}
}

﻿// Copyright Timothé Lapetite 2024
// Released under the MIT license https://opensource.org/license/MIT/

#include "Graph/Pathfinding/PCGExPathfindingFindContours.h"

#include "PCGExCompare.h"


#define LOCTEXT_NAMESPACE "PCGExFindContours"
#define PCGEX_NAMESPACE FindContours

TArray<FPCGPinProperties> UPCGExFindContoursSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINT(PCGExGraph::SourceSeedsLabel, "Seeds associated with the main input points", Required, {})
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExFindContoursSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINTS(PCGExGraph::OutputPathsLabel, "Contours", Required, {})
	if (bOutputFilteredSeeds)
	{
		PCGEX_PIN_POINT(PCGExFindContours::OutputGoodSeedsLabel, "GoodSeeds", Required, {})
		PCGEX_PIN_POINT(PCGExFindContours::OutputBadSeedsLabel, "BadSeeds", Required, {})
	}
	return PinProperties;
}

PCGExData::EIOInit UPCGExFindContoursSettings::GetEdgeOutputInitMode() const { return PCGExData::EIOInit::NoOutput; }
PCGExData::EIOInit UPCGExFindContoursSettings::GetMainOutputInitMode() const { return PCGExData::EIOInit::NoOutput; }

PCGEX_INITIALIZE_ELEMENT(FindContours)

bool FPCGExFindContoursElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExEdgesProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(FindContours)

	PCGEX_FWD(ProjectionDetails)

	if (Settings->bFlagDeadEnds) { PCGEX_VALIDATE_NAME(Settings->DeadEndAttributeName); }

	const TSharedPtr<PCGExData::FPointIO> SeedsPoints = PCGExData::TryGetSingleInput(Context, PCGExGraph::SourceSeedsLabel, true);
	if (!SeedsPoints) { return false; }

	Context->SeedsDataFacade = MakeShared<PCGExData::FFacade>(SeedsPoints.ToSharedRef());

	if (!Context->ProjectionDetails.Init(Context, Context->SeedsDataFacade)) { return false; }

	PCGEX_FWD(SeedAttributesToPathTags)
	if (!Context->SeedAttributesToPathTags.Init(Context, Context->SeedsDataFacade)) { return false; }
	Context->SeedForwardHandler = Settings->SeedForwarding.GetHandler(Context->SeedsDataFacade);

	Context->Paths = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->Paths->OutputPin = PCGExGraph::OutputPathsLabel;

	if (Settings->bOutputFilteredSeeds)
	{
		const int32 NumSeeds = SeedsPoints->GetNum();

		Context->SeedQuality.Init(false, NumSeeds);
		PCGEx::InitArray(Context->UdpatedSeedPoints, NumSeeds);

		Context->GoodSeeds = NewPointIO(SeedsPoints.ToSharedRef(), PCGExFindContours::OutputGoodSeedsLabel);
		Context->GoodSeeds->InitializeOutput(PCGExData::EIOInit::NewOutput);
		Context->GoodSeeds->GetOut()->GetMutablePoints().Reserve(NumSeeds);

		Context->BadSeeds = NewPointIO(SeedsPoints.ToSharedRef(), PCGExFindContours::OutputBadSeedsLabel);
		Context->BadSeeds->InitializeOutput(PCGExData::EIOInit::NewOutput);
		Context->BadSeeds->GetOut()->GetMutablePoints().Reserve(NumSeeds);
	}

	return true;
}

bool FPCGExFindContoursElement::ExecuteInternal(
	FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExFindContoursElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(FindContours)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters<PCGExFindContours::FBatch>(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries) { return true; },
			[&](const TSharedPtr<PCGExFindContours::FBatch>& NewBatch)
			{
				if (Settings->bFlagDeadEnds)
				{
					NewBatch->bRequiresWriteStep = true;
				}
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGEx::State_Done)

	if (Settings->bOutputFilteredSeeds)
	{
		const TArray<FPCGPoint>& InSeeds = Context->SeedsDataFacade->Source->GetIn()->GetPoints();
		TArray<FPCGPoint>& GoodSeeds = Context->GoodSeeds->GetOut()->GetMutablePoints();
		TArray<FPCGPoint>& BadSeeds = Context->BadSeeds->GetOut()->GetMutablePoints();

		for (int i = 0; i < Context->SeedQuality.Num(); i++)
		{
			if (Context->SeedQuality[i]) { GoodSeeds.Add(Context->UdpatedSeedPoints[i]); }
			else { BadSeeds.Add(InSeeds[i]); }
		}

		Context->GoodSeeds->StageOutput();
		Context->BadSeeds->StageOutput();
	}

	Context->Paths->StageOutputs();

	return Context->TryComplete();
}


namespace PCGExFindContours
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(TSharedPtr<PCGExMT::FTaskManager> InAsyncManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExFindContours::Process);


		if (!FClusterProcessor::Process(InAsyncManager)) { return false; }

		// Build constraint object
		CellsConstraints = MakeShared<PCGExTopology::FCellConstraints>(Settings->Constraints);
		CellsConstraints->DataBounds = Cluster->Bounds;

		if (Settings->bUseOctreeSearch) { Cluster->RebuildOctree(Settings->SeedPicking.PickingMethod); }
		Cluster->RebuildOctree(EPCGExClusterClosestSearchMode::Edge); // We need edge octree anyway

		ExpandedNodes = Cluster->ExpandedNodes;

		if (!ExpandedNodes)
		{
			ExpandedNodes = Cluster->GetExpandedNodes(false);
			bBuildExpandedNodes = true;
			StartParallelLoopForRange(NumNodes);
		}

		return true;
	}

	void FProcessor::ProcessSingleRangeIteration(const int32 Iteration, const int32 LoopIdx, const int32 Count)
	{
		*(ExpandedNodes->GetData() + Iteration) = PCGExCluster::FExpandedNode(Cluster, Iteration);
	}

	void FProcessor::TryFindContours(const int32 SeedIndex)
	{
		
		const TSharedPtr<PCGExData::FPointIO>& PathIO = Context->Paths->Pairs[SeedIndex];
		const FVector ProjectedSeedPosition = Context->ProjectionDetails.Project(Context->SeedsDataFacade->Source->GetInPoint(SeedIndex).Transform.GetLocation(), SeedIndex);

		const FVector RealSeedPosition = Context->SeedsDataFacade->Source->GetInPoint(SeedIndex).Transform.GetLocation();
		const int32 StartNodeIndex = Cluster->FindClosestNode(RealSeedPosition, Settings->SeedPicking.PickingMethod, 2);

		if (StartNodeIndex == -1) { return; } // Fail. Either single-node or single-edge cluster, or no connected edge

		const int32 NextEdge = Cluster->FindClosestEdge(StartNodeIndex, RealSeedPosition);

		if (NextEdge == -1) { return; } // Fail. Either single-node or single-edge cluster, or no connected edge

		const FVector StartPosition = Cluster->GetPos(StartNodeIndex);
		if (!Settings->SeedPicking.WithinDistance(StartPosition, RealSeedPosition)) { return; } // Fail. Not within radius.

		TSharedPtr<PCGExTopology::FCell> Cell = MakeShared<PCGExTopology::FCell>(CellsConstraints.ToSharedRef());

		const PCGExTopology::ECellResult Result = Cell->BuildFromCluster(StartNodeIndex, NextEdge, ProjectedSeedPosition, Cluster.ToSharedRef(), *ProjectedPositions, ExpandedNodes);
		if (Result != PCGExTopology::ECellResult::Success) { return; }

		PathIO->InitializeOutput(PCGExData::EIOInit::NewOutput);
		PCGExGraph::CleanupClusterTags(PathIO, true);
		PCGExGraph::CleanupVtxData(PathIO);

		TSharedPtr<PCGExData::FFacade> PathDataFacade = MakeShared<PCGExData::FFacade>(PathIO.ToSharedRef());

		TArray<FPCGPoint>& MutablePoints = PathIO->GetOut()->GetMutablePoints();
		MutablePoints.SetNumUninitialized(Cell->Nodes.Num());

		//const TArray<int32>& VtxPointIndices = Cluster->GetVtxPointIndices();
		for (int i = 0; i < Cell->Nodes.Num(); i++) { MutablePoints[i] = *Cluster->GetNodePoint(Cell->Nodes[i]); }
		Cell->PostProcessPoints(MutablePoints);
		
		Context->SeedAttributesToPathTags.Tag(SeedIndex, PathIO);
		Context->SeedForwardHandler->Forward(SeedIndex, PathDataFacade);

		if (Settings->bFlagDeadEnds)
		{
			const TSharedPtr<PCGExData::TBuffer<bool>> DeadEndBuffer = PathDataFacade->GetWritable(Settings->DeadEndAttributeName, false, true, PCGExData::EBufferInit::New);
			TArray<bool>& OutValues = *DeadEndBuffer->GetOutValues();
			for (int i = 0; i < Cell->Nodes.Num(); i++) { OutValues[i] = Cluster->GetNode(Cell->Nodes[i])->Adjacency.Num() == 1; }
		}

		if (!Cell->bIsClosedLoop) { if (Settings->bTagIfOpenPath) { PathIO->Tags->Add(Settings->IsOpenPathTag); } }
		else { if (Settings->bTagIfClosedLoop) { PathIO->Tags->Add(Settings->IsClosedLoopTag); } }

		PathDataFacade->Write(AsyncManager);

		if (Settings->bOutputFilteredSeeds)
		{
			Context->SeedQuality[SeedIndex] = true;
			FPCGPoint SeedPoint = Context->SeedsDataFacade->Source->GetInPoint(SeedIndex);
			Settings->SeedMutations.ApplyToPoint(Cell.Get(), SeedPoint, MutablePoints);
			Context->UdpatedSeedPoints[SeedIndex] = SeedPoint;
		}
	}

	void FProcessor::CompleteWork()
	{
		PCGEX_ASYNC_GROUP_CHKD_VOID(AsyncManager, ProcessSeedsTask)

		TWeakPtr<FProcessor> WeakPtr = SharedThis(this);

		const int32 NumSeeds = Context->SeedsDataFacade->Source->GetNum();
		
		for (int i = 0; i < NumSeeds; i++)
		{
			Context->Paths->Emplace_GetRef<UPCGPointData>(VtxDataFacade->Source, PCGExData::EIOInit::NoOutput);
		}

		ProcessSeedsTask->OnIterationCallback = [WeakPtr](const int32 Index, const int32 Count, const int32 LoopIdx)
		{
			if (const TSharedPtr<FProcessor> This = WeakPtr.Pin()) { This->TryFindContours(Index); }
		};

		ProcessSeedsTask->StartIterations(NumSeeds, 12, false, false);
	}

	void FBatch::Process()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(FindContours)

		// Project positions
		ProjectionDetails = Settings->ProjectionDetails;
		if (!ProjectionDetails.Init(Context, VtxDataFacade)) { return; }

		PCGEx::InitArray(ProjectedPositions, VtxDataFacade->GetNum());

		PCGEX_ASYNC_GROUP_CHKD_VOID(AsyncManager, ProjectionTaskGroup)

		ProjectionTaskGroup->OnCompleteCallback =
			[WeakThis = TWeakPtr<FBatch>(SharedThis(this))]()
			{
				if (TSharedPtr<FBatch> This = WeakThis.Pin()) { This->OnProjectionComplete(); }
			};

		ProjectionTaskGroup->OnSubLoopStartCallback =
			[WeakThis = TWeakPtr<FBatch>(SharedThis(this))]
			(const int32 StartIndex, const int32 Count, const int32 LoopIdx)
			{
				TSharedPtr<FBatch> This = WeakThis.Pin();
				if (!This) { return; }

				const int32 MaxIndex = StartIndex + Count;

				for (int i = StartIndex; i < MaxIndex; i++)
				{
					This->ProjectedPositions[i] = This->ProjectionDetails.ProjectFlat(This->VtxDataFacade->Source->GetInPoint(i).Transform.GetLocation(), i);
				}
			};

		ProjectionTaskGroup->StartSubLoops(VtxDataFacade->GetNum(), GetDefault<UPCGExGlobalSettings>()->GetPointsBatchChunkSize());
	}

	bool FBatch::PrepareSingle(const TSharedPtr<FProcessor>& ClusterProcessor)
	{
		ClusterProcessor->ProjectedPositions = &ProjectedPositions;
		TBatch<FProcessor>::PrepareSingle(ClusterProcessor);
		return true;
	}

	void FBatch::OnProjectionComplete()
	{
		TBatch<FProcessor>::Process();
	}

}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE

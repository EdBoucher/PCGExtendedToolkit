﻿// Copyright Timothé Lapetite 2024
// Released under the MIT license https://opensource.org/license/MIT/

#include "Topology/PCGExTopologyClusterSurface.h"

#define LOCTEXT_NAMESPACE "PCGExEdgesToPaths"
#define PCGEX_NAMESPACE TopologyEdgesProcessor

PCGEX_INITIALIZE_ELEMENT(TopologyClusterSurface)

bool FPCGExTopologyClusterSurfaceElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExTopologyEdgesProcessorElement::Boot(InContext)) { return false; }
	PCGEX_CONTEXT_AND_SETTINGS(TopologyClusterSurface)

	return true;
}

bool FPCGExTopologyClusterSurfaceElement::ExecuteInternal(
	FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExTopologyEdgesProcessorElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(TopologyClusterSurface)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters<PCGExTopologyEdges::TBatch<PCGExTopologyClusterSurface::FProcessor>>(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries) { return true; },
			[&](const TSharedPtr<PCGExTopologyEdges::TBatch<PCGExTopologyClusterSurface::FProcessor>>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGEx::State_Done)

	Context->OutputPointsAndEdges();
	Context->OutputBatches();

	return Context->TryComplete();
}

namespace PCGExTopologyClusterSurface
{
	void FProcessor::PrepareLoopScopesForEdges(const TArray<uint64>& Loops)
	{
		TProcessor<FPCGExTopologyClusterSurfaceContext, UPCGExTopologyClusterSurfaceSettings>::PrepareLoopScopesForEdges(Loops);
		PCGEx::InitArray(SubTriangulations, Loops.Num());
		for (int i = 0; i < Loops.Num(); i++)
		{
			// TODO : Find a way to pre-allocate memory here
			SubTriangulations[i] = MakeShared<TArray<PCGExGeo::FTriangle>>();
		}
	}

	void FProcessor::PrepareSingleLoopScopeForEdges(const uint32 StartIndex, const int32 Count)
	{
		EdgeDataFacade->Fetch(StartIndex, Count);
		FilterConstrainedEdgeScope(StartIndex, Count);
	}

	void FProcessor::ProcessSingleEdge(const int32 EdgeIndex, PCGExGraph::FIndexedEdge& Edge, const int32 LoopIdx, const int32 Count)
	{
		if (ConstrainedEdgeFilterCache[EdgeIndex]) { return; }
		TSharedPtr<PCGExTopology::FCell> Cell = MakeShared<PCGExTopology::FCell>(CellConstraints.ToSharedRef());

		const int32 StartNode = Cluster->GetEdgeStart(Edge)->NodeIndex;
		const int32 EndNode = Cluster->GetEdgeEnd(Edge)->NodeIndex;

		if (Cell->BuildFromCluster(
			Cluster->GetNode(StartNode)->NodeIndex, EdgeIndex,
			Cluster->GetPos(EndNode), Cluster.ToSharedRef(),
			*ProjectedPositions, ExpandedNodes) == PCGExTopology::ECellResult::Success)
		{
			Cell->Triangulate<true>(*ProjectedPositions, *SubTriangulations[LoopIdx].Get(), Cluster);
		}

		Cell.Reset();
		Cell = MakeShared<PCGExTopology::FCell>(CellConstraints.ToSharedRef());
		if (Cell->BuildFromCluster(
			Cluster->GetNode(EndNode)->NodeIndex, EdgeIndex,
			Cluster->GetPos(StartNode), Cluster.ToSharedRef(),
			*ProjectedPositions, ExpandedNodes) == PCGExTopology::ECellResult::Success)
		{
			Cell->Triangulate<true>(*ProjectedPositions, *SubTriangulations[LoopIdx].Get(), Cluster);
		}
	}

	void FProcessor::OnEdgesProcessingComplete()
	{
		if (!BuildValidNodeLookup()) { return; }

		InternalMesh->EditMesh(
			[&](FDynamicMesh3& InMesh)
			{
				for (TSharedPtr<TArray<PCGExGeo::FTriangle>> SubTriangulation : SubTriangulations)
				{
					const TArray<PCGExGeo::FTriangle>& Triangles = *SubTriangulation;

					for (const PCGExGeo::FTriangle& T : Triangles)
					{
						// Very slow, just checking if things work
						//InMesh.AppendTriangle(VerticesLookup->Get(T.Vtx[0]), VerticesLookup->Get(T.Vtx[1]), VerticesLookup->Get(T.Vtx[2]));
						InMesh.AppendTriangle(
							VerticesLookup->Get(Cluster->NodeIndexLookup->Get(T.Vtx[0])),
							VerticesLookup->Get(Cluster->NodeIndexLookup->Get(T.Vtx[1])),
							VerticesLookup->Get(Cluster->NodeIndexLookup->Get(T.Vtx[2])));
					}
				}
			}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::MeshTopology, false);
	}

	void FProcessor::CompleteWork()
	{
		StartParallelLoopForEdges(128);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE

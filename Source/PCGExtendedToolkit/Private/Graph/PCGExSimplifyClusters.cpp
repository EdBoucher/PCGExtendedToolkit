﻿// Copyright Timothé Lapetite 2024
// Released under the MIT license https://opensource.org/license/MIT/

#include "Graph/PCGExSimplifyClusters.h"

#include "Data/PCGExGraphDefinition.h"

#define LOCTEXT_NAMESPACE "PCGExGraphSettings"

#pragma region UPCGSettings interface

PCGExData::EInit UPCGExSimplifyClustersSettings::GetMainOutputInitMode() const { return PCGExData::EInit::NewOutput; }
PCGExData::EInit UPCGExSimplifyClustersSettings::GetEdgeOutputInitMode() const { return PCGExData::EInit::NoOutput; }

#pragma endregion

FPCGExSimplifyClustersContext::~FPCGExSimplifyClustersContext()
{
	PCGEX_TERMINATE_ASYNC

	PCGEX_DELETE(GraphBuilder)

	PCGEX_DELETE(IsPointFixtureGetter)
	PCGEX_DELETE(IsEdgeFixtureGetter)

	PCGEX_DELETE_TARRAY(Chains)
}

PCGEX_INITIALIZE_ELEMENT(SimplifyClusters)

bool FPCGExSimplifyClustersElement::Boot(FPCGContext* InContext) const
{
	if (!FPCGExEdgesProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(SimplifyClusters)

	Context->GraphBuilderSettings.bPruneIsolatedPoints = true;

	if (Settings->bUseLocalNodeMark)
	{
		Context->IsPointFixtureGetter = new PCGEx::FLocalBoolGetter();
		Context->IsPointFixtureGetter->Capture(Settings->NodeFixAttribute);
	}

	if (Settings->bUseLocalEdgeMark)
	{
		Context->IsEdgeFixtureGetter = new PCGEx::FLocalBoolGetter();
		Context->IsEdgeFixtureGetter->Capture(Settings->EdgeFixAttribute);
	}

	Context->FixedDotThreshold = PCGExMath::DegreesToDot(Settings->AngularThreshold);

	return true;
}

bool FPCGExSimplifyClustersElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExSimplifyClustersElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(SimplifyClusters)

	if (Context->IsSetup())
	{
		if (!Boot(Context)) { return true; }
		Context->SetState(PCGExMT::State_ReadyForNextPoints);
	}

	if (Context->IsState(PCGExMT::State_ReadyForNextPoints))
	{
		PCGEX_DELETE(Context->GraphBuilder)

		if (!Context->AdvancePointsIO()) { Context->Done(); }
		else
		{
			if (!Context->TaggedEdges) { return false; }

			if (Context->IsPointFixtureGetter) { Context->IsPointFixtureGetter->Grab(*Context->CurrentIO); }
			Context->GraphBuilder = new PCGExGraph::FGraphBuilder(*Context->CurrentIO, &Context->GraphBuilderSettings, 6, Context->MainEdges);
			Context->SetState(PCGExGraph::State_ReadyForNextEdges);
		}
	}

	if (Context->IsState(PCGExGraph::State_ReadyForNextEdges))
	{
		PCGEX_DELETE_TARRAY(Context->Chains)

		if (!Context->AdvanceEdges(true))
		{
			Context->SetState(PCGExGraph::State_WritingClusters);
			return false;
		}

		if (!Context->CurrentCluster) { return false; }

		// Insert current cluster into loose graph
		Context->GetAsyncManager()->Start<FPCGExFindClusterChainsTask>(Context->CurrentIO->IOIndex, Context->CurrentIO);
		Context->SetAsyncState(PCGExGraph::State_ProcessingGraph);
	}

	if (Context->IsState(PCGExGraph::State_ProcessingGraph))
	{
		if (!Context->IsAsyncWorkComplete()) { return false; }

		for (const PCGExGraph::FIndexedEdge& Edge : Context->CurrentCluster->Edges)
		{
			if (!Edge.bValid) { continue; }
			Context->GraphBuilder->Graph->InsertEdge(Edge);
		}

		PCGExGraph::FIndexedEdge NewEdge = PCGExGraph::FIndexedEdge{};
		for (const PCGExCluster::FNodeChain* Chain : Context->Chains)
		{
			Context->GraphBuilder->Graph->InsertEdge(
				Context->CurrentCluster->Nodes[Chain->First].PointIndex,
				Context->CurrentCluster->Nodes[Chain->Last].PointIndex, NewEdge);
		}

		Context->SetState(PCGExGraph::State_ReadyForNextEdges);
	}

	if (Context->IsState(PCGExGraph::State_WritingClusters))
	{
		Context->GraphBuilder->Compile(Context);
		Context->SetAsyncState(PCGExGraph::State_WaitingOnWritingClusters);
		return false;
	}

	if (Context->IsState(PCGExGraph::State_WaitingOnWritingClusters))
	{
		if (!Context->IsAsyncWorkComplete()) { return false; }
		if (Context->GraphBuilder->bCompiledSuccessfully) { Context->GraphBuilder->Write(Context); }

		Context->SetState(PCGExMT::State_ReadyForNextPoints);
	}

	if (Context->IsDone())
	{
		Context->OutputPoints();
	}

	return Context->IsDone();
}

bool FPCGExFindClusterChainsTask::ExecuteTask()
{
	FPCGExSimplifyClustersContext* Context = static_cast<FPCGExSimplifyClustersContext*>(Manager->Context);
	PCGEX_SETTINGS(SimplifyClusters)

	const TArray<PCGExCluster::FNode>& Nodes = Context->CurrentCluster->Nodes;
	TSet<int32> NodeFixtures;
	TSet<int32> VisitedNodes;
	TArray<int32> Candidates;

	// Find fixtures
	const PCGExCluster::FCluster& Cluster = *Context->CurrentCluster;

	if (Context->IsEdgeFixtureGetter &&
		Context->IsEdgeFixtureGetter->IsUsable(Cluster.Edges.Num()))
	{
		Context->IsEdgeFixtureGetter->Grab(*Context->CurrentEdges);
		const TArray<bool> FixedEdges = Context->IsEdgeFixtureGetter->Values;
		for (int i = 0; i < FixedEdges.Num(); i++)
		{
			if (!FixedEdges[i]) { continue; }

			const PCGExGraph::FIndexedEdge& E = Cluster.Edges[i];
			NodeFixtures.Add(Cluster.GetNodeFromPointIndex(E.Start).NodeIndex);
			NodeFixtures.Add(Cluster.GetNodeFromPointIndex(E.End).NodeIndex);
		}
	}

	for (const PCGExCluster::FNode& Node : Nodes)
	{
		if (const int32 NumNeighbors = Node.AdjacentNodes.Num();
			NumNeighbors <= 1 || NumNeighbors > 2)
		{
			if(NumNeighbors == 0)
			{
				Context->GraphBuilder->Graph->Nodes[Node.PointIndex].bValid = false;
			}
			
			NodeFixtures.Add(Node.NodeIndex);
			continue;
		}

		if (Context->IsPointFixtureGetter)
		{
			if (Context->IsPointFixtureGetter->SafeGet(Node.PointIndex, false))
			{
				NodeFixtures.Add(Node.NodeIndex);
				continue;
			}
		}

		if (Settings->bFixBelowThreshold)
		{
			const FVector A = (Cluster.Nodes[Node.AdjacentNodes[0]].Position - Node.Position).GetSafeNormal();
			const FVector B = (Node.Position - Cluster.Nodes[Node.AdjacentNodes[1]].Position).GetSafeNormal();
			if (FVector::DotProduct(A, B) < Context->FixedDotThreshold)
			{
				NodeFixtures.Add(Node.NodeIndex);
				continue;
			}
		}
	}

	// Find starting search points
	for (const PCGExCluster::FNode& Node : Nodes)
	{
		if (!NodeFixtures.Contains(Node.NodeIndex)) { continue; }
		
		for (const int32 AdjacentNode : Node.AdjacentNodes)
		{
			if (NodeFixtures.Contains(AdjacentNode)) { continue; }
			Candidates.AddUnique(AdjacentNode);
		}
	}

	for (const int32 CandidateIndex : Candidates)
	{
		const PCGExCluster::FNode& Node = Nodes[CandidateIndex];

		if (VisitedNodes.Contains(Node.NodeIndex)) { continue; }
		VisitedNodes.Add(Node.NodeIndex);

		if (NodeFixtures.Contains(Node.NodeIndex)) { continue; } // Skip

		PCGExCluster::FNodeChain* NewChain = new PCGExCluster::FNodeChain();
		Context->Chains.Add(NewChain);

		int32 NextNodeIndex = Node.NodeIndex;
		int32 PrevNodeIndex = NodeFixtures.Contains(Node.AdjacentNodes[0]) ? Node.AdjacentNodes[0] : Node.AdjacentNodes[1];

		NewChain->First = PrevNodeIndex;
		while (NextNodeIndex != -1)
		{
			const int32 CurrentNodeIndex = NextNodeIndex;
			VisitedNodes.Add(CurrentNodeIndex);

			const PCGExCluster::FNode& NextNode = Nodes[CurrentNodeIndex];

			const int32 EdgeIndex = NextNode.GetEdgeIndex(PrevNodeIndex);
			NewChain->Edges.Add(EdgeIndex);
			Context->CurrentCluster->Edges[EdgeIndex].bValid = false;

			if (NodeFixtures.Contains(NextNodeIndex))
			{
				NewChain->Last = NextNodeIndex;
				break;
			}

			NewChain->Nodes.Add(CurrentNodeIndex);

			NextNodeIndex = NextNode.AdjacentNodes[0] == PrevNodeIndex ? NextNode.AdjacentNodes[1] : NextNode.AdjacentNodes[0];
			PrevNodeIndex = CurrentNodeIndex;
		}

		for (const int32 Edge : NewChain->Edges) { Context->CurrentCluster->Edges[Edge].bValid = false; }
		for (const int32 NodeIndex : NewChain->Nodes) { Context->GraphBuilder->Graph->Nodes[Context->CurrentCluster->Nodes[NodeIndex].PointIndex].bValid = false; }
	}

	NodeFixtures.Empty();
	VisitedNodes.Empty();
	Candidates.Empty();

	return true;
}

#undef LOCTEXT_NAMESPACE
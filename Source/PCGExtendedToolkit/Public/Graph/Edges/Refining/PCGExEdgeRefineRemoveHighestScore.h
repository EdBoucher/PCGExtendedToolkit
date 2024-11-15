﻿// Copyright Timothé Lapetite 2024
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExEdgeRefineOperation.h"
#include "Graph/Pathfinding/Heuristics/PCGExHeuristics.h"
#include "PCGExEdgeRefineRemoveHighestScore.generated.h"

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, meta=(DisplayName="Remove Highest Score"))
class /*PCGEXTENDEDTOOLKIT_API*/ UPCGExEdgeRemoveHighestScore : public UPCGExEdgeRefineOperation
{
	GENERATED_BODY()

public:
	virtual bool RequiresHeuristics() override { return true; }
	virtual bool RequiresIndividualNodeProcessing() override { return true; }

	virtual void ProcessNode(PCGExCluster::FNode& Node) override
	{
		int32 BestIndex = -1;
		double HighestScore = MIN_dbl;

		for (const uint64 AdjacencyHash : Node.Adjacency)
		{
			uint32 OtherNodeIndex;
			uint32 EdgeIndex;
			PCGEx::H64(AdjacencyHash, OtherNodeIndex, EdgeIndex);

			const double Score = Heuristics->GetEdgeScore(Node, *Cluster->GetNode(OtherNodeIndex), *Cluster->GetEdge(EdgeIndex), *RoamingSeedNode, *RoamingGoalNode);
			if (Score > HighestScore)
			{
				HighestScore = Score;
				BestIndex = EdgeIndex;
			}
		}

		if (BestIndex == -1) { return; }
		//if (!*(EdgesFilters->GetData() + BestIndex)) { return; }

		FPlatformAtomics::InterlockedExchange(&Cluster->GetEdge(BestIndex)->bValid, 0);
	}
};

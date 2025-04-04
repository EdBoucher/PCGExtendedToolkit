﻿// Copyright 2025 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExHeuristicFeedback.h"
#include "PCGExHeuristicsFactoryProvider.h"


#include "Graph/Pathfinding/PCGExPathfinding.h"

#include "PCGExHeuristics.generated.h"

UENUM()
enum class EPCGExHeuristicScoreMode : uint8
{
	LowerIsBetter  = 0 UMETA(DisplayName = "Lower is Better", Tooltip="Lower values are considered more desirable."),
	HigherIsBetter = 1 UMETA(DisplayName = "Higher is Better", Tooltip="Higher values are considered more desirable."),
};

namespace PCGExHeuristics
{
	class PCGEXTENDEDTOOLKIT_API FLocalFeedbackHandler : public TSharedFromThis<FLocalFeedbackHandler>
	{
	public:
		FPCGExContext* ExecutionContext = nullptr;

		TSharedPtr<PCGExData::FFacade> VtxDataFacade;
		TSharedPtr<PCGExData::FFacade> EdgeDataFacade;

		TArray<UPCGExHeuristicFeedback*> Feedbacks;
		double TotalStaticWeight = 0;

		explicit FLocalFeedbackHandler(FPCGExContext* InContext):
			ExecutionContext(InContext)
		{
		}

		~FLocalFeedbackHandler()
		{
			for (UPCGExHeuristicFeedback* Feedback : Feedbacks) { ExecutionContext->ManagedObjects->Destroy(Feedback); }
		}

		double GetGlobalScore(
			const PCGExCluster::FNode& From,
			const PCGExCluster::FNode& Seed,
			const PCGExCluster::FNode& Goal) const
		{
			double GScore = 0;
			for (const UPCGExHeuristicFeedback* Feedback : Feedbacks) { GScore += Feedback->GetGlobalScore(From, Seed, Goal); }
			return GScore;
		}

		double GetEdgeScore(
			const PCGExCluster::FNode& From,
			const PCGExCluster::FNode& To,
			const PCGExGraph::FEdge& Edge,
			const PCGExCluster::FNode& Seed,
			const PCGExCluster::FNode& Goal,
			const TSharedPtr<PCGEx::FHashLookup> TravelStack = nullptr) const
		{
			double EScore = 0;
			for (const UPCGExHeuristicFeedback* Feedback : Feedbacks) { EScore += Feedback->GetEdgeScore(From, To, Edge, Seed, Goal, TravelStack); }
			return EScore;
		}

		void FeedbackPointScore(const PCGExCluster::FNode& Node)
		{
			for (UPCGExHeuristicFeedback* Feedback : Feedbacks) { Feedback->FeedbackPointScore(Node); }
		}

		void FeedbackScore(const PCGExCluster::FNode& Node, const PCGExGraph::FEdge& Edge)
		{
			for (UPCGExHeuristicFeedback* Feedback : Feedbacks) { Feedback->FeedbackScore(Node, Edge); }
		}
	};

	class PCGEXTENDEDTOOLKIT_API FHeuristicsHandler : public TSharedFromThis<FHeuristicsHandler>
	{
		FPCGExContext* ExecutionContext = nullptr;
		bool bIsValidHandler = false;

	public:
		mutable FRWLock HandlerLock;
		TSharedPtr<PCGExData::FFacade> VtxDataFacade;
		TSharedPtr<PCGExData::FFacade> EdgeDataFacade;

		TArray<UPCGExHeuristicOperation*> Operations;
		TArray<UPCGExHeuristicFeedback*> Feedbacks;
		TArray<TObjectPtr<const UPCGExHeuristicsFactoryData>> LocalFeedbackFactories;

		TSharedPtr<PCGExCluster::FCluster> Cluster;

		double ReferenceWeight = 1;
		double TotalStaticWeight = 0;
		bool bUseDynamicWeight = false;

		bool IsValidHandler() const { return bIsValidHandler; }
		bool HasGlobalFeedback() const { return !Feedbacks.IsEmpty(); };
		bool HasLocalFeedback() const { return !LocalFeedbackFactories.IsEmpty(); };
		bool HasAnyFeedback() const { return HasGlobalFeedback() || HasLocalFeedback(); };

		FHeuristicsHandler(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InVtxDataCache, const TSharedPtr<PCGExData::FFacade>& InEdgeDataCache, const TArray<TObjectPtr<const UPCGExHeuristicsFactoryData>>& InFactories);
		~FHeuristicsHandler();

		bool BuildFrom(FPCGExContext* InContext, const TArray<TObjectPtr<const UPCGExHeuristicsFactoryData>>& InFactories);
		void PrepareForCluster(const TSharedPtr<PCGExCluster::FCluster>& InCluster);
		void CompleteClusterPreparation();


		double GetGlobalScore(
			const PCGExCluster::FNode& From,
			const PCGExCluster::FNode& Seed,
			const PCGExCluster::FNode& Goal,
			const FLocalFeedbackHandler* LocalFeedback = nullptr) const;


		double GetEdgeScore(
			const PCGExCluster::FNode& From,
			const PCGExCluster::FNode& To,
			const PCGExGraph::FEdge& Edge,
			const PCGExCluster::FNode& Seed,
			const PCGExCluster::FNode& Goal,
			const FLocalFeedbackHandler* LocalFeedback = nullptr,
			const TSharedPtr<PCGEx::FHashLookup>& TravelStack = nullptr) const;

		void FeedbackPointScore(const PCGExCluster::FNode& Node);
		void FeedbackScore(const PCGExCluster::FNode& Node, const PCGExGraph::FEdge& Edge);

		FVector GetSeedUVW() const;
		FVector GetGoalUVW() const;

		const PCGExCluster::FNode* GetRoamingSeed() const { return Cluster->GetRoamingNode(GetSeedUVW()); }
		const PCGExCluster::FNode* GetRoamingGoal() const { return Cluster->GetRoamingNode(GetGoalUVW()); }

		TSharedPtr<FLocalFeedbackHandler> MakeLocalFeedbackHandler(const TSharedPtr<const PCGExCluster::FCluster>& InCluster);
	};
}

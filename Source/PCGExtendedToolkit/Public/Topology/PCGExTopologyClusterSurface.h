﻿// Copyright Timothé Lapetite 2024
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Graph/PCGExClusterMT.h"
#include "Graph/PCGExEdgesProcessor.h"

#include "PCGExTopologyEdgesProcessor.h"
#include "Components/DynamicMeshComponent.h"
#include "PCGExTopologyClusterSurface.generated.h"

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters")
class /*PCGEXTENDEDTOOLKIT_API*/ UPCGExTopologyClusterSurfaceSettings : public UPCGExTopologyEdgesProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(TopologyClusterSurface, "Topology : Cluster Surface", "Create a cluster surface topology");
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:

private:
	friend class FPCGExTopologyEdgesProcessorElement;
};

struct /*PCGEXTENDEDTOOLKIT_API*/ FPCGExTopologyClusterSurfaceContext final : FPCGExTopologyEdgesProcessorContext
{
	friend class FPCGExTopologyClusterSurfaceElement;
};

class /*PCGEXTENDEDTOOLKIT_API*/ FPCGExTopologyClusterSurfaceElement final : public FPCGExTopologyEdgesProcessorElement
{
public:
	virtual FPCGContext* Initialize(
		const FPCGDataCollection& InputData,
		TWeakObjectPtr<UPCGComponent> SourceComponent,
		const UPCGNode* Node) override;

protected:
	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;
};

namespace PCGExTopologyClusterSurface
{
	class FProcessor final : public PCGExTopologyEdges::TProcessor<FPCGExTopologyClusterSurfaceContext, UPCGExTopologyClusterSurfaceSettings>
	{
		TArray<TSharedPtr<TArray<PCGExGeo::FTriangle>>> SubTriangulations;

	public:
		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		virtual ~FProcessor() override
		{
		}

		virtual void CompleteWork() override;
		virtual void PrepareLoopScopesForEdges(const TArray<uint64>& Loops) override;
		virtual void PrepareSingleLoopScopeForEdges(const uint32 StartIndex, const int32 Count) override;
		virtual void ProcessSingleEdge(const int32 EdgeIndex, PCGExGraph::FIndexedEdge& Edge, const int32 LoopIdx, const int32 Count) override;
		virtual void OnEdgesProcessingComplete() override;

		virtual void Output() override
		{
			if (!bIsProcessorValid) { return; }

			TRACE_CPUPROFILER_EVENT_SCOPE(UPCGExPathSplineMesh::FProcessor::Output);

			// TODO : Resolve per-point target actor...? irk.
			AActor* TargetActor = Settings->TargetActor.Get() ? Settings->TargetActor.Get() : ExecutionContext->GetTargetActor(nullptr);

			if (!TargetActor)
			{
				PCGE_LOG_C(Error, GraphAndLog, ExecutionContext, FTEXT("Invalid target actor."));
				return;
			}

			const FString ComponentName = TEXT("PCGDynamicMeshComponent");
			const EObjectFlags ObjectFlags = (bIsPreviewMode ? RF_Transient : RF_NoFlags);
			UDynamicMeshComponent* DynamicMeshComponent = NewObject<UDynamicMeshComponent>(TargetActor, MakeUniqueObjectName(TargetActor, UDynamicMeshComponent::StaticClass(), FName(ComponentName)), ObjectFlags);

			if(Settings->Topology.bFlipOrientation)
			{
				GetInternalMesh()->GetMeshPtr()->ReverseOrientation();	
			}
			
			DynamicMeshComponent->SetDynamicMesh(GetInternalMesh());
			DynamicMeshComponent->SetDistanceFieldMode(Settings->Topology.DistanceFieldMode);
			Context->ManagedObjects->Remove(GetInternalMesh());
			
			Context->AttachManageComponent(
				TargetActor, DynamicMeshComponent,
				FAttachmentTransformRules(EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, false));

			Context->NotifyActors.Add(TargetActor);
		}
	};
}

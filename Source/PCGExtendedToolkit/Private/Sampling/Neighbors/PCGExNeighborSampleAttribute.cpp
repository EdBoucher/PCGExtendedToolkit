﻿// Copyright 2025 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sampling/Neighbors/PCGExNeighborSampleAttribute.h"

#include "Data/Blending/PCGExMetadataBlender.h"


#define LOCTEXT_NAMESPACE "PCGExCreateNeighborSample"
#define PCGEX_NAMESPACE PCGExCreateNeighborSample

void FPCGExNeighborSampleAttribute::PrepareForCluster(FPCGExContext* InContext, const TSharedRef<PCGExCluster::FCluster> InCluster, const TSharedRef<PCGExData::FFacade> InVtxDataFacade, const TSharedRef<PCGExData::FFacade> InEdgeDataFacade)
{
	FPCGExNeighborSampleOperation::PrepareForCluster(InContext, InCluster, InVtxDataFacade, InEdgeDataFacade);

	Blender.Reset();
	bIsValidOperation = false;

	if (SourceAttributes.IsEmpty())
	{
		PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("No source attribute set."));
		return;
	}

	TSet<FName> MissingAttributes;

	TArray<FName> SourceNames;
	SourceAttributes.GetSources(SourceNames);

	PCGExDataBlending::AssembleBlendingDetails(Blending, SourceNames, GetSourceIO(), MetadataBlendingDetails, MissingAttributes);

	for (const FName& Id : MissingAttributes)
	{
		if (SamplingConfig.NeighborSource == EPCGExClusterComponentSource::Vtx) { PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(FTEXT("Missing source attribute on vtx: {0}."), FText::FromName(Id))); }
		else { PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(FTEXT("Missing source attribute on edges: {0}."), FText::FromName(Id))); }
	}

	if (MetadataBlendingDetails.FilteredAttributes.IsEmpty())
	{
		//PCGE_LOG_C(Error, GraphAndLog, InContext, FText::Format(FTEXT("Missing all source attribute(s) on Sampler {0}."), FText::FromString(GetClass()->GetName()))); // TODO
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Missing all source attribute(s) on a Sampler."));
		return;
	}

	Blender = MakeShared<PCGExDataBlending::FMetadataBlender>();
	Blender->bBlendProperties = false;
	Blender->SetTargetData(InVtxDataFacade);
	Blender->SetSourceData(GetSourceDataFacade(), PCGExData::EIOSide::In);

	if (!Blender->Init(Context, MetadataBlendingDetails))
	{
		// 
		return;
	}

	SourceAttributes.SetOutputTargetNames(InVtxDataFacade);

	bIsValidOperation = true;
}

void FPCGExNeighborSampleAttribute::PrepareNode(const PCGExCluster::FNode& TargetNode) const
{
	Blender->PrepareForBlending(TargetNode.PointIndex);
}

void FPCGExNeighborSampleAttribute::SampleNeighborNode(const PCGExCluster::FNode& TargetNode, const PCGExGraph::FLink Lk, const double Weight)
{
	const int32 PrimaryIndex = TargetNode.PointIndex;
	Blender->Blend(PrimaryIndex, Cluster->GetNode(Lk)->PointIndex, PrimaryIndex, Weight);
}

void FPCGExNeighborSampleAttribute::SampleNeighborEdge(const PCGExCluster::FNode& TargetNode, const PCGExGraph::FLink Lk, const double Weight)
{
	const int32 PrimaryIndex = TargetNode.PointIndex;
	Blender->Blend(PrimaryIndex, Cluster->GetEdge(Lk)->PointIndex, PrimaryIndex, Weight);
}

void FPCGExNeighborSampleAttribute::FinalizeNode(const PCGExCluster::FNode& TargetNode, const int32 Count, const double TotalWeight)
{
	const int32 PrimaryIndex = TargetNode.PointIndex;
	Blender->CompleteBlending(PrimaryIndex, Count, TotalWeight);
}

void FPCGExNeighborSampleAttribute::CompleteOperation()
{
	FPCGExNeighborSampleOperation::CompleteOperation();
	Blender.Reset();
}

#if WITH_EDITOR
FString UPCGExNeighborSampleAttributeSettings::GetDisplayName() const
{
	if (Config.SourceAttributes.IsEmpty()) { return TEXT(""); }
	TArray<FName> SourceNames;
	Config.SourceAttributes.GetSources(SourceNames);

	if (SourceNames.Num() == 1) { return SourceNames[0].ToString(); }
	if (SourceNames.Num() == 2) { return SourceNames[0].ToString() + TEXT(" (+1 other)"); }

	return SourceNames[0].ToString() + FString::Printf(TEXT(" (+%d others)"), (SourceNames.Num() - 1));
}
#endif

TSharedPtr<FPCGExNeighborSampleOperation> UPCGExNeighborSamplerFactoryAttribute::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(NeighborSampleAttribute)
	PCGEX_SAMPLER_CREATE_OPERATION

	NewOperation->SourceAttributes = Config.SourceAttributes;
	NewOperation->Blending = Config.Blending;

	return NewOperation;
}

bool UPCGExNeighborSamplerFactoryAttribute::RegisterConsumableAttributes(FPCGExContext* InContext) const
{
	if (!Super::RegisterConsumableAttributes(InContext)) { return false; }
	for (const FPCGExAttributeSourceToTargetDetails& Entry : Config.SourceAttributes.Attributes)
	{
		if (Entry.WantsRemappedOutput()) { InContext->AddConsumableAttributeName(Entry.Source); }
	}
	return true;
}

void UPCGExNeighborSamplerFactoryAttribute::RegisterVtxBuffersDependencies(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterVtxBuffersDependencies(InContext, InVtxDataFacade, FacadePreloader);

	if (SamplingConfig.NeighborSource == EPCGExClusterComponentSource::Vtx)
	{
		TSharedPtr<PCGEx::FAttributesInfos> Infos = PCGEx::FAttributesInfos::Get(InVtxDataFacade->GetIn()->Metadata);

		TArray<FName> SourceNames;
		Config.SourceAttributes.GetSources(SourceNames);

		for (const FName AttrName : SourceNames)
		{
			const PCGEx::FAttributeIdentity* Identity = Infos->Find(AttrName);
			if (!Identity)
			{
				PCGEX_LOG_INVALID_ATTR_C(InContext, "", AttrName)
				return;
			}
			FacadePreloader.Register(InContext, *Identity);
		}
	}
}

UPCGExFactoryData* UPCGExNeighborSampleAttributeSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	if (!Config.SourceAttributes.ValidateNames(InContext)) { return nullptr; }

	UPCGExNeighborSamplerFactoryAttribute* SamplerFactory = InContext->ManagedObjects->New<UPCGExNeighborSamplerFactoryAttribute>();
	SamplerFactory->Config = Config;

	return Super::CreateFactory(InContext, SamplerFactory);
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE

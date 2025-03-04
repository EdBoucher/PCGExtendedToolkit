﻿// Copyright 2025 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


// Cherry picker merges metadata from varied sources into one.
// Initially to handle metadata merging for Fuse Clusters

#include "Data/Blending/PCGExUnionBlender.h"

#include "Data/PCGExData.h"
#include "Data/PCGExDataFilter.h"
#include "Data/Blending//PCGExDataBlendingProcessors.h"
#include "Data/Blending/PCGExPropertiesBlender.h"


namespace PCGExDataBlending
{
	FMultiSourceAttribute::FMultiSourceAttribute(const PCGEx::FAttributeIdentity& InIdentity)
		: Identity(InIdentity)
	{
	}

	void FMultiSourceAttribute::PrepareMerge(const EPCGMetadataTypes Type, const TSharedPtr<PCGExData::FFacade>& InTargetData, TArray<TSharedPtr<PCGExData::FFacade>>& Sources)
	{
		check(InTargetData);

		Buffer = nullptr;

		if (const FPCGMetadataAttributeBase* ExistingAttribute = InTargetData->FindConstAttribute(Identity.Name);
			ExistingAttribute && ExistingAttribute->GetTypeId() == static_cast<int16>(Type))
		{
			// This attribute exists
			Buffer = InTargetData->GetWritable(Type, ExistingAttribute, PCGExData::EBufferInit::Inherit);
		}
		else
		{
			Buffer = InTargetData->GetWritable(Type, DefaultValue, PCGExData::EBufferInit::New);
		}

		for (int i = 0; i < Sources.Num(); i++)
		{
			if (const TSharedPtr<FDataBlendingProcessorBase>& SubProc = SubBlendingProcessors[i]) { SubProc->PrepareForData(Buffer, Sources[i]); }
		}

		MainBlendingProcessor->PrepareForData(Buffer, InTargetData, PCGExData::ESource::Out);
	}

	void FMultiSourceAttribute::PrepareSoftMerge(const TSharedPtr<PCGExData::FFacade>& InTargetData, TArray<TSharedPtr<PCGExData::FFacade>>& Sources)
	{
		check(InTargetData);

		Buffer = nullptr;

		for (int i = 0; i < Sources.Num(); i++)
		{
			if (const TSharedPtr<FDataBlendingProcessorBase>& SrcProc = SubBlendingProcessors[i])
			{
				SrcProc->SoftPrepareForData(InTargetData, Sources[i]);
			}
		}

		MainBlendingProcessor->SoftPrepareForData(InTargetData, InTargetData, PCGExData::ESource::Out);
	}

	FUnionBlender::FUnionBlender(const FPCGExBlendingDetails* InBlendingDetails, const FPCGExCarryOverDetails* InCarryOverDetails):
		CarryOverDetails(InCarryOverDetails), BlendingDetails(InBlendingDetails)
	{
	}

	FUnionBlender::~FUnionBlender()
	{
	}

	void FUnionBlender::AddSource(const TSharedPtr<PCGExData::FFacade>& InFacade, const TSet<FName>* IgnoreAttributeSet)
	{
		const int32 SourceIndex = Sources.Add(InFacade);
		const int32 NumSources = Sources.Num();
		IOIndices.Add(InFacade->Source->IOIndex, SourceIndex);

		UniqueTags.Append(InFacade->Source->Tags->RawTags);

		for (const TSharedPtr<FMultiSourceAttribute>& MultiAttribute : MultiSourceAttributes) { MultiAttribute->SetNum(NumSources); }

		TArray<PCGEx::FAttributeIdentity> SourceAttributes;
		PCGEx::FAttributeIdentity::Get(InFacade->GetIn()->Metadata, SourceAttributes);
		CarryOverDetails->Prune(SourceAttributes);
		BlendingDetails->Filter(SourceAttributes);

		for (const PCGEx::FAttributeIdentity& Identity : SourceAttributes)
		{
			if (IgnoreAttributeSet && IgnoreAttributeSet->Contains(Identity.Name)) { continue; }

			const FPCGMetadataAttributeBase* SourceAttribute = InFacade->FindConstAttribute(Identity.Name);
			if (!SourceAttribute) { continue; }

			const EPCGExDataBlendingType* BlendTypePtr = BlendingDetails->AttributesOverrides.Find(Identity.Name);
			TSharedPtr<FMultiSourceAttribute> MultiAttribute;

			// Search for an existing multi attribute
			for (const TSharedPtr<FMultiSourceAttribute>& ExistingMultiAttribute : MultiSourceAttributes)
			{
				if (ExistingMultiAttribute->Identity.Name == Identity.Name)
				{
					MultiAttribute = ExistingMultiAttribute;
					break;
				}
			}

			if (MultiAttribute)
			{
				if (Identity.UnderlyingType != MultiAttribute->Identity.UnderlyingType)
				{
					// Type mismatch, ignore for this source
					TypeMismatches.Add(Identity.Name.ToString());
					continue;
				}
			}
			else
			{
				// Initialize new multi attribute

				MultiAttribute = MultiSourceAttributes.Add_GetRef(MakeShared<FMultiSourceAttribute>(Identity));
				MultiAttribute->SetNum(NumSources);

				MultiAttribute->DefaultValue = SourceAttribute;

				if (PCGEx::IsPCGExAttribute(Identity.Name)) { MultiAttribute->MainBlendingProcessor = CreateProcessor(EPCGExDataBlendingType::Copy, Identity); }
				else { MultiAttribute->MainBlendingProcessor = CreateProcessor(BlendTypePtr, BlendingDetails->DefaultBlending, Identity); }
			}

			check(MultiAttribute)

			MultiAttribute->Siblings[SourceIndex] = SourceAttribute;
			MultiAttribute->SubBlendingProcessors[SourceIndex] = CreateProcessor(BlendTypePtr, BlendingDetails->DefaultBlending, Identity);

			if (!SourceAttribute->AllowsInterpolation()) { MultiAttribute->AllowsInterpolation = false; }
		}
	}

	void FUnionBlender::AddSources(const TArray<TSharedRef<PCGExData::FFacade>>& InFacades, const TSet<FName>* IgnoreAttributeSet)
	{
		for (TSharedRef<PCGExData::FFacade> Facade : InFacades) { AddSource(Facade, IgnoreAttributeSet); }
	}

	void FUnionBlender::PrepareMerge(
		FPCGExContext* InContext,
		const TSharedPtr<PCGExData::FFacade>& TargetData,
		const TSharedPtr<PCGExData::FUnionMetadata>& InUnionMetadata)
	{
		CurrentUnionMetadata = InUnionMetadata;
		CurrentTargetData = TargetData;

		const FPCGExPropertiesBlendingDetails PropertiesBlendingDetails = BlendingDetails->GetPropertiesBlendingDetails();
		PropertiesBlender = PropertiesBlendingDetails.HasNoBlending() ? nullptr : MakeUnique<FPropertiesBlender>(PropertiesBlendingDetails);

		// Initialize blending operations
		for (const TSharedPtr<FMultiSourceAttribute>& MultiAttribute : MultiSourceAttributes)
		{
			MultiAttribute->PrepareMerge(MultiAttribute->Identity.UnderlyingType, CurrentTargetData, Sources);
		}

		Validate(InContext, false);
	}

	void FUnionBlender::MergeSingle(const int32 UnionIndex, const TSharedPtr<PCGExDetails::FDistances>& InDistanceDetails)
	{
		MergeSingle(UnionIndex, CurrentUnionMetadata->Get(UnionIndex), InDistanceDetails);
	}

	void FUnionBlender::MergeSingle(const int32 WriteIndex, const TSharedPtr<PCGExData::FUnionData>& InUnionData, const TSharedPtr<PCGExDetails::FDistances>& InDistanceDetails)
	{
		check(InUnionData)

		TArray<int32> IdxIO;
		TArray<int32> IdxPt;
		TArray<double> Weights;

		FPCGPoint& Target = CurrentTargetData->Source->GetMutablePoint(WriteIndex);

		InUnionData->ComputeWeights(
			Sources, IOIndices,
			Target, InDistanceDetails,
			IdxIO, IdxPt, Weights);

		const int32 UnionCount = IdxPt.Num();

		if (UnionCount == 0) { return; }

		// Blend Properties


		// Blend Properties
		BlendProperties(Target, IdxIO, IdxPt, Weights);

		// Blend Attributes

		for (const TSharedPtr<FMultiSourceAttribute>& MultiAttribute : MultiSourceAttributes)
		{
			MultiAttribute->MainBlendingProcessor->PrepareOperation(WriteIndex);

			int32 ValidUnions = 0;
			double TotalWeight = 0;

			for (int k = 0; k < UnionCount; k++)
			{
				const TSharedPtr<FDataBlendingProcessorBase>& Operation = MultiAttribute->SubBlendingProcessors[IdxIO[k]];
				if (!Operation) { continue; }

				const double Weight = Weights[k];

				Operation->DoOperation(
					WriteIndex, Sources[IdxIO[k]]->Source->GetInPoint(IdxPt[k]),
					WriteIndex, Weight, k == 0);

				ValidUnions++;
				TotalWeight += Weight;
			}

			if (ValidUnions == 0) { continue; } // No valid attribute to merge on any union source

			MultiAttribute->MainBlendingProcessor->CompleteOperation(WriteIndex, ValidUnions, TotalWeight);
		}
	}

	// Soft blending

	void FUnionBlender::PrepareSoftMerge(
		FPCGExContext* InContext,
		const TSharedPtr<PCGExData::FFacade>& TargetData,
		const TSharedPtr<PCGExData::FUnionMetadata>& InUnionMetadata)
	{
		CurrentUnionMetadata = InUnionMetadata;
		CurrentTargetData = TargetData;

		const FPCGExPropertiesBlendingDetails PropertiesBlendingDetails = BlendingDetails->GetPropertiesBlendingDetails();
		PropertiesBlender = PropertiesBlendingDetails.HasNoBlending() ? nullptr : MakeUnique<FPropertiesBlender>(PropertiesBlendingDetails);

		CarryOverDetails->Prune(UniqueTags);

		// Initialize blending operations
		for (const TSharedPtr<FMultiSourceAttribute>& MultiAttribute : MultiSourceAttributes)
		{
			MultiAttribute->Buffer = nullptr;
			MultiAttribute->PrepareSoftMerge(CurrentTargetData, Sources);
		}

		// Strip existing attribute names that may conflict with tags

		TArray<FName> ReservedNames;
		TArray<EPCGMetadataTypes> ReservedTypes;
		TargetData->Source->GetOut()->Metadata->GetAttributes(ReservedNames, ReservedTypes);
		for (FName ReservedName : ReservedNames) { UniqueTags.Remove(ReservedName.ToString()); }
		UniqueTagsList = UniqueTags.Array();
		TagAttributes.Reserve(UniqueTagsList.Num());

		for (FString TagName : UniqueTagsList)
		{
			TagAttributes.Add(TargetData->Source->FindOrCreateAttribute<bool>(FName(TagName), false));
		}

		Validate(InContext, false);
	}

	void FUnionBlender::SoftMergeSingle(const int32 UnionIndex, const TSharedPtr<PCGExDetails::FDistances>& InDistanceDetails)
	{
		SoftMergeSingle(UnionIndex, CurrentUnionMetadata->Get(UnionIndex), InDistanceDetails);
	}

	void FUnionBlender::SoftMergeSingle(const int32 UnionIndex, const TSharedPtr<PCGExData::FUnionData>& InUnionData, const TSharedPtr<PCGExDetails::FDistances>& InDistanceDetails)
	{
		TArray<int32> IdxIO;
		TArray<int32> IdxPt;
		TArray<double> Weights;
		TArray<int8> InheritedTags;

		FPCGPoint& Target = CurrentTargetData->Source->GetMutablePoint(UnionIndex);

		InUnionData->ComputeWeights(
			Sources, IOIndices,
			Target, InDistanceDetails,
			IdxIO, IdxPt, Weights);

		const int32 NumUnions = IdxPt.Num();
		if (NumUnions == 0) { return; }

		InheritedTags.Init(0, TagAttributes.Num());

		// Blend Properties
		BlendProperties(Target, IdxIO, IdxPt, Weights);

		// Blend Attributes
		for (const TSharedPtr<FMultiSourceAttribute>& MultiAttribute : MultiSourceAttributes)
		{
			MultiAttribute->MainBlendingProcessor->PrepareOperation(Target.MetadataEntry);

			int32 ValidUnions = 0;
			double TotalWeight = 0;

			for (int k = 0; k < NumUnions; k++)
			{
				const TSharedPtr<FDataBlendingProcessorBase>& Operation = MultiAttribute->SubBlendingProcessors[IdxIO[k]];
				if (!Operation) { continue; }

				const double Weight = Weights[k];

				Operation->DoOperation(
					Target.MetadataEntry, Sources[IdxIO[k]]->Source->GetInPoint(IdxPt[k]).MetadataEntry,
					Target.MetadataEntry, Weight, k == 0);

				ValidUnions++;
				TotalWeight += Weight;
			}

			if (ValidUnions == 0) { continue; } // No valid attribute to merge on any union source

			MultiAttribute->MainBlendingProcessor->CompleteOperation(Target.MetadataEntry, ValidUnions, TotalWeight);
		}

		// Tag flags

		for (const int32 IOI : IdxIO)
		{
			for (int i = 0; i < TagAttributes.Num(); i++)
			{
				if (Sources[IOI]->Source->Tags->IsTagged(UniqueTagsList[i])) { InheritedTags[i] = true; }
			}
		}

		for (int i = 0; i < TagAttributes.Num(); i++) { TagAttributes[i]->SetValue(Target.MetadataEntry, InheritedTags[i]); }
	}

	void FUnionBlender::BlendProperties(FPCGPoint& TargetPoint, TArray<int32>& IdxIO, TArray<int32>& IdxPt, TArray<double>& Weights)
	{
		if (!PropertiesBlender) { return; }

		PropertiesBlender->PrepareBlending(TargetPoint, TargetPoint);

		const int32 NumUnions = IdxIO.Num();
		double TotalWeight = 0;
		for (int k = 0; k < NumUnions; k++)
		{
			const double Weight = Weights[k];
			PropertiesBlender->Blend(TargetPoint, Sources[IdxIO[k]]->Source->GetInPoint(IdxPt[k]), TargetPoint, Weight);
			TotalWeight += Weight;
		}

		PropertiesBlender->CompleteBlending(TargetPoint, NumUnions, TotalWeight);
	}

	bool FUnionBlender::Validate(FPCGExContext* InContext, const bool bQuiet) const
	{
		if (TypeMismatches.IsEmpty()) { return true; }

		if (bQuiet) { return false; }

		PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(FTEXT("The following attributes have the same name but different types, and will not blend as expected: {0}"), FText::FromString(FString::Join(TypeMismatches.Array(), TEXT(", ")))));

		return false;
	}
}

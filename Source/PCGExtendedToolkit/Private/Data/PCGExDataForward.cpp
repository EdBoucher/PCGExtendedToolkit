﻿// Copyright 2025 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/PCGExDataForward.h"
#include "Data/PCGExData.h"


TSharedPtr<PCGExData::FDataForwardHandler> FPCGExForwardDetails::GetHandler(const TSharedPtr<PCGExData::FFacade>& InSourceDataFacade) const
{
	return MakeShared<PCGExData::FDataForwardHandler>(*this, InSourceDataFacade);
}

TSharedPtr<PCGExData::FDataForwardHandler> FPCGExForwardDetails::GetHandler(const TSharedPtr<PCGExData::FFacade>& InSourceDataFacade, const TSharedPtr<PCGExData::FFacade>& InTargetDataFacade) const
{
	return MakeShared<PCGExData::FDataForwardHandler>(*this, InSourceDataFacade, InTargetDataFacade);
}

TSharedPtr<PCGExData::FDataForwardHandler> FPCGExForwardDetails::TryGetHandler(const TSharedPtr<PCGExData::FFacade>& InSourceDataFacade) const
{
	return bEnabled ? GetHandler(InSourceDataFacade) : nullptr;
}

TSharedPtr<PCGExData::FDataForwardHandler> FPCGExForwardDetails::TryGetHandler(const TSharedPtr<PCGExData::FFacade>& InSourceDataFacade, const TSharedPtr<PCGExData::FFacade>& InTargetDataFacade) const
{
	return bEnabled ? GetHandler(InSourceDataFacade, InTargetDataFacade) : nullptr;
}

bool FPCGExAttributeToTagDetails::Init(const FPCGContext* InContext, const TSharedPtr<PCGExData::FFacade>& InSourceFacade)
{
	PCGExHelpers::AppendUniqueSelectorsFromCommaSeparatedList(CommaSeparatedAttributeSelectors, Attributes);

	for (FPCGAttributePropertyInputSelector& Selector : Attributes)
	{
		if (const TSharedPtr<PCGEx::TAttributeBroadcaster<FString>>& Getter = Getters.Add_GetRef(MakeShared<PCGEx::TAttributeBroadcaster<FString>>());
			!Getter->Prepare(Selector, InSourceFacade->Source))
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Missing specified Tag attribute."));
			Getters.Empty();
			return false;
		}
	}

	SourceDataFacade = InSourceFacade;
	return true;
}

void FPCGExAttributeToTagDetails::Tag(const int32 TagIndex, TSet<FString>& InTags) const
{
	if (bAddIndexTag) { InTags.Add(IndexTagPrefix + ":" + FString::Printf(TEXT("%d"), TagIndex)); }

	if (!Getters.IsEmpty())
	{
		const FPCGPoint& Point = SourceDataFacade->GetIn()->GetPoint(TagIndex);
		for (const TSharedPtr<PCGEx::TAttributeBroadcaster<FString>>& Getter : Getters)
		{
			FString Tag = Getter->SoftGet(TagIndex, Point, TEXT(""));
			if (Tag.IsEmpty()) { continue; }
			if (bPrefixWithAttributeName) { Tag = Getter->GetName() + ":" + Tag; }
			InTags.Add(Tag);
		}
	}
}

void FPCGExAttributeToTagDetails::Tag(const int32 TagIndex, const TSharedPtr<PCGExData::FPointIO>& PointIO) const
{
	TSet<FString> Tags;
	Tag(TagIndex, Tags);
	PointIO->Tags->Append(Tags);
}

void FPCGExAttributeToTagDetails::Tag(const int32 TagIndex, UPCGMetadata* InMetadata) const
{
	if (bAddIndexTag)
	{
		if (PCGEx::IsValidName(FName(IndexTagPrefix)))
		{
			InMetadata->FindOrCreateAttribute<FString>(FName(IndexTagPrefix), IndexTagPrefix + ":" + FString::Printf(TEXT("%d"), TagIndex));
		}
	}

	if (!Getters.IsEmpty())
	{
		const FPCGPoint& Point = SourceDataFacade->GetIn()->GetPoint(TagIndex);
		for (const TSharedPtr<PCGEx::TAttributeBroadcaster<FString>>& Getter : Getters)
		{
			FString Tag = Getter->SoftGet(TagIndex, Point, TEXT(""));
			if (Tag.IsEmpty()) { continue; }
			if (bPrefixWithAttributeName) { Tag = Getter->GetName() + ":" + Tag; }
			InMetadata->FindOrCreateAttribute<FString>(FName(Getter->GetName()), Tag);
		}
	}
}

namespace PCGExData
{
	FDataForwardHandler::FDataForwardHandler(const FPCGExForwardDetails& InDetails, const TSharedPtr<FFacade>& InSourceDataFacade):
		Details(InDetails), SourceDataFacade(InSourceDataFacade), TargetDataFacade(nullptr)
	{
		if (!Details.bEnabled) { return; }

		Details.Init();
		PCGEx::FAttributeIdentity::Get(InSourceDataFacade->GetIn()->Metadata, Identities);
		Details.Filter(Identities);
	}

	FDataForwardHandler::FDataForwardHandler(const FPCGExForwardDetails& InDetails, const TSharedPtr<FFacade>& InSourceDataFacade, const TSharedPtr<FFacade>& InTargetDataFacade):
		Details(InDetails), SourceDataFacade(InSourceDataFacade), TargetDataFacade(InTargetDataFacade)
	{
		Details.Init();
		PCGEx::FAttributeIdentity::Get(InSourceDataFacade->GetIn()->Metadata, Identities);
		Details.Filter(Identities);

		const int32 NumAttributes = Identities.Num();
		Readers.Reserve(NumAttributes);
		Writers.Reserve(NumAttributes);

		// Init forwarded attributes on target		
		for (int i = 0; i < NumAttributes; i++)
		{
			const PCGEx::FAttributeIdentity& Identity = Identities[i];

			PCGEx::ExecuteWithRightType(
				Identity.UnderlyingType, [&](auto DummyValue)
				{
					using T = decltype(DummyValue);
					TSharedPtr<TBuffer<T>> Reader = SourceDataFacade->GetReadable<T>(Identity.Name);
					TSharedPtr<TBuffer<T>> Writer = TargetDataFacade->GetWritable<T>(Reader->GetTypedInAttribute(), EBufferInit::Inherit);

					if (!Reader || !Writer) { return; }

					Readers.Add(Reader);
					Writers.Add(Writer);
				});
		}
	}

	void FDataForwardHandler::Forward(const int32 SourceIndex, const int32 TargetIndex)
	{
		const int32 NumAttributes = Identities.Num();

		for (int i = 0; i < NumAttributes; i++)
		{
			const PCGEx::FAttributeIdentity& Identity = Identities[i];

			PCGEx::ExecuteWithRightType(
				Identity.UnderlyingType, [&](auto DummyValue)
				{
					using T = decltype(DummyValue);
					TSharedPtr<TBuffer<T>> Reader = StaticCastSharedPtr<TBuffer<T>>(Readers[i]);
					TSharedPtr<TBuffer<T>> Writer = StaticCastSharedPtr<TBuffer<T>>(Writers[i]);
					Writer->GetMutable(TargetIndex) = Reader->Read(SourceIndex);
				});
		}
	}

	void FDataForwardHandler::Forward(const int32 SourceIndex, const TSharedPtr<FFacade>& InTargetDataFacade)
	{
		if (Identities.IsEmpty()) { return; }

		if (Details.bPreserveAttributesDefaultValue)
		{
			for (const PCGEx::FAttributeIdentity& Identity : Identities)
			{
				PCGEx::ExecuteWithRightType(
					Identity.UnderlyingType, [&](auto DummyValue)
					{
						using T = decltype(DummyValue);

						// 'template' spec required for clang on mac, and rider keeps removing it without the comment below.
						// ReSharper disable once CppRedundantTemplateKeyword
						const FPCGMetadataAttribute<T>* SourceAtt = SourceDataFacade->GetIn()->Metadata->template GetConstTypedAttribute<T>(Identity.Name);
						if (!SourceAtt) { return; }

						TSharedPtr<TBuffer<T>> Writer = InTargetDataFacade->GetWritable<T>(SourceAtt, EBufferInit::New);

						const T ForwardValue = SourceAtt->GetValueFromItemKey(SourceDataFacade->Source->GetInPoint(SourceIndex).MetadataEntry);
						TArray<T>& Values = *Writer->GetOutValues();
						for (T& Value : Values) { Value = ForwardValue; }
					});
			}

			return;
		}

		for (const PCGEx::FAttributeIdentity& Identity : Identities)
		{
			PCGEx::ExecuteWithRightType(
				Identity.UnderlyingType, [&](auto DummyValue)
				{
					using T = decltype(DummyValue);

					// 'template' spec required for clang on mac, and rider keeps removing it without the comment below.
					// ReSharper disable once CppRedundantTemplateKeyword
					const FPCGMetadataAttribute<T>* SourceAtt = SourceDataFacade->GetIn()->Metadata->template GetConstTypedAttribute<T>(Identity.Name);
					if (!SourceAtt) { return; }

					InTargetDataFacade->Source->DeleteAttribute(Identity.Name);
					InTargetDataFacade->Source->FindOrCreateAttribute<T>(
						Identity.Name,
						SourceAtt->GetValueFromItemKey(SourceDataFacade->Source->GetInPoint(SourceIndex).MetadataEntry),
						SourceAtt->AllowsInterpolation(), true, true);
				});
		}
	}

	void FDataForwardHandler::Forward(int32 SourceIndex, const TSharedPtr<FFacade>& InTargetDataFacade, const TArray<int32>& Indices)
	{
		if (Identities.IsEmpty()) { return; }

		for (const PCGEx::FAttributeIdentity& Identity : Identities)
		{
			PCGEx::ExecuteWithRightType(
				Identity.UnderlyingType, [&](auto DummyValue)
				{
					using T = decltype(DummyValue);

					// 'template' spec required for clang on mac, and rider keeps removing it without the comment below.
					// ReSharper disable once CppRedundantTemplateKeyword
					const FPCGMetadataAttribute<T>* SourceAtt = SourceDataFacade->GetIn()->Metadata->template GetConstTypedAttribute<T>(Identity.Name);
					if (!SourceAtt) { return; }

					TSharedPtr<TBuffer<T>> Writer = InTargetDataFacade->GetWritable<T>(SourceAtt, EBufferInit::Inherit);

					const T ForwardValue = SourceAtt->GetValueFromItemKey(SourceDataFacade->Source->GetInPoint(SourceIndex).MetadataEntry);
					TArray<T>& Values = *Writer->GetOutValues();
					for (int i : Indices) { Values[i] = ForwardValue; }
				});
		}
	}

	void FDataForwardHandler::Forward(const int32 SourceIndex, UPCGMetadata* InTargetMetadata)
	{
		if (Identities.IsEmpty()) { return; }

		for (const PCGEx::FAttributeIdentity& Identity : Identities)
		{
			PCGEx::ExecuteWithRightType(
				Identity.UnderlyingType, [&](auto DummyValue)
				{
					using T = decltype(DummyValue);

					// 'template' spec required for clang on mac, and rider keeps removing it without the comment below.
					// ReSharper disable once CppRedundantTemplateKeyword
					const FPCGMetadataAttribute<T>* SourceAtt = SourceDataFacade->GetIn()->Metadata->template GetConstTypedAttribute<T>(Identity.Name);
					if (!SourceAtt) { return; }

					InTargetMetadata->DeleteAttribute(Identity.Name);
					InTargetMetadata->FindOrCreateAttribute<T>(
						Identity.Name,
						SourceAtt->GetValueFromItemKey(SourceDataFacade->Source->GetInPoint(SourceIndex).MetadataEntry),
						SourceAtt->AllowsInterpolation(), true, true);
				});
		}
	}
}

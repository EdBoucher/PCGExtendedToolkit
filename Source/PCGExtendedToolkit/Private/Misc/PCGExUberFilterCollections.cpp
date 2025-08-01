﻿// Copyright 2025 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Misc/PCGExUberFilterCollections.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointFilter.h"
#include "Misc/Pickers/PCGExPicker.h"
#include "Misc/Pickers/PCGExPickerFactoryProvider.h"


#define LOCTEXT_NAMESPACE "PCGExUberFilterCollections"
#define PCGEX_NAMESPACE UberFilterCollections

#if WITH_EDITOR
bool UPCGExUberFilterCollectionsSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (InPin->Properties.Label == PCGExPicker::SourcePickersLabel) { return InPin->EdgeCount() > 0; }
	return Super::IsPinUsedByNodeExecution(InPin);
}
#endif

TArray<FPCGPinProperties> UPCGExUberFilterCollectionsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_PARAMS(PCGExPicker::SourcePickersLabel, "A precise selection of point that will be tested, as opposed to all of them.", Normal, {})
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExUberFilterCollectionsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINTS(PCGExPointFilter::OutputInsideFiltersLabel, "Collections that passed the filters.", Required, {})
	PCGEX_PIN_POINTS(PCGExPointFilter::OutputOutsideFiltersLabel, "Collections that didn't pass the filters.", Required, {})
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(UberFilterCollections)

FName UPCGExUberFilterCollectionsSettings::GetMainOutputPin() const
{
	// Ensure proper forward when node is disabled
	return PCGExPointFilter::OutputInsideFiltersLabel;
}

bool FPCGExUberFilterCollectionsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(UberFilterCollections)

	PCGExFactories::GetInputFactories(Context, PCGExPicker::SourcePickersLabel, Context->PickerFactories, {PCGExFactories::EType::IndexPicker}, false);

	Context->Inside = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->Outside = MakeShared<PCGExData::FPointIOCollection>(Context);

	Context->Inside->OutputPin = PCGExPointFilter::OutputInsideFiltersLabel;
	Context->Outside->OutputPin = PCGExPointFilter::OutputOutsideFiltersLabel;

	Context->DataIOInit = Context->bCleanupConsumableAttributes ? PCGExData::EIOInit::Duplicate : PCGExData::EIOInit::Forward;

	if (Settings->bSwap)
	{
		Context->Inside->OutputPin = PCGExPointFilter::OutputOutsideFiltersLabel;
		Context->Outside->OutputPin = PCGExPointFilter::OutputInsideFiltersLabel;
	}

	Context->bHasOnlyCollectionFilters = true;
	for (const TObjectPtr<const UPCGExFilterFactoryData>& FilterFactory : Context->FilterFactories)
	{
		if (!FilterFactory->SupportsCollectionEvaluation())
		{
			Context->bHasOnlyCollectionFilters = false;
			break;
		}
	}

	return true;
}

bool FPCGExUberFilterCollectionsElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExUberFilterCollectionsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(UberFilterCollections)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->bHasOnlyCollectionFilters)
		{
			Context->NumPairs = Context->MainPoints->Pairs.Num();

			if (!Context->StartBatchProcessingPoints<PCGExPointsMT::TBatch<PCGExUberFilterCollections::FProcessor>>(
				[&](const TSharedPtr<PCGExData::FPointIO>& Entry) { return true; },
				[&](const TSharedPtr<PCGExPointsMT::TBatch<PCGExUberFilterCollections::FProcessor>>& NewBatch)
				{
					NewBatch->bSkipCompletion = Context->bHasOnlyCollectionFilters;
				}))
			{
				return Context->CancelExecution(TEXT("Could not find any points to filter."));
			}
		}
		else
		{
			PCGEX_MAKE_SHARED(DummyFacade, PCGExData::FFacade, Context->MainPoints->Pairs[0].ToSharedRef())
			PCGEX_MAKE_SHARED(PrimaryFilters, PCGExPointFilter::FManager, DummyFacade.ToSharedRef())
			PrimaryFilters->Init(Context, Context->FilterFactories);

			while (Context->AdvancePointsIO())
			{
				if (PrimaryFilters->Test(Context->CurrentIO, Context->MainPoints)) { Context->Inside->Emplace_GetRef(Context->CurrentIO, Context->DataIOInit); }
				else { Context->Outside->Emplace_GetRef(Context->CurrentIO, Context->DataIOInit); }
			}

			Context->Done();
		}
	}

	if (!Context->bHasOnlyCollectionFilters)
	{
		PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::State_Done)
		Context->MainBatch->Output();
	}

	Context->Inside->StageOutputs();
	Context->Outside->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExUberFilterCollections
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InAsyncManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExUberFilterCollections::Process);

		// Must be set before process for filters
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InAsyncManager)) { return false; }

		bUsePicks = PCGExPicker::GetPicks(Context->PickerFactories, PointDataFacade, Picks);
		NumPoints = bUsePicks ? Picks.Num() : PointDataFacade->GetNum();

		if (Settings->Measure == EPCGExMeanMeasure::Discrete)
		{
			if ((Settings->Comparison == EPCGExComparison::StrictlyGreater ||
					Settings->Comparison == EPCGExComparison::EqualOrGreater) &&
				NumPoints < Settings->IntThreshold)
			{
				// Not enough points to meet requirements.
				Context->Outside->Emplace_GetRef(PointDataFacade->Source, PCGExData::EIOInit::Forward);
				return true;
			}
		}

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::UberFilterCollections::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		if (bUsePicks)
		{
			PCGEX_SCOPE_LOOP(Index)
			{
				if (!Picks.Contains(Index)) { continue; }
				if (PointFilterCache[Index]) { FPlatformAtomics::InterlockedAdd(&NumInside, 1); }
				else { FPlatformAtomics::InterlockedAdd(&NumOutside, 1); }
			}
		}
		else
		{
			PCGEX_SCOPE_LOOP(Index)
			{
				if (PointFilterCache[Index]) { FPlatformAtomics::InterlockedAdd(&NumInside, 1); }
				else { FPlatformAtomics::InterlockedAdd(&NumOutside, 1); }
			}
		}
	}

	void FProcessor::Output()
	{
		IProcessor::Output();

		switch (Settings->Mode)
		{
		default:
		case EPCGExUberFilterCollectionsMode::All:
			if (NumInside == NumPoints) { Context->Inside->Emplace_GetRef(PointDataFacade->Source, Context->DataIOInit); }
			else { Context->Outside->Emplace_GetRef(PointDataFacade->Source, Context->DataIOInit); }
			break;
		case EPCGExUberFilterCollectionsMode::Any:
			if (NumInside != 0) { Context->Inside->Emplace_GetRef(PointDataFacade->Source, Context->DataIOInit); }
			else { Context->Outside->Emplace_GetRef(PointDataFacade->Source, Context->DataIOInit); }
			break;
		case EPCGExUberFilterCollectionsMode::Partial:
			if (Settings->Measure == EPCGExMeanMeasure::Discrete)
			{
				if (PCGExCompare::Compare(Settings->Comparison, NumInside, Settings->IntThreshold, 0)) { Context->Inside->Emplace_GetRef(PointDataFacade->Source, Context->DataIOInit); }
				else { Context->Outside->Emplace_GetRef(PointDataFacade->Source, Context->DataIOInit); }
			}
			else
			{
				const double Ratio = static_cast<double>(NumInside) / static_cast<double>(NumPoints);
				if (PCGExCompare::Compare(Settings->Comparison, Ratio, Settings->DblThreshold, Settings->Tolerance)) { Context->Inside->Emplace_GetRef(PointDataFacade->Source, Context->DataIOInit); }
				else { Context->Outside->Emplace_GetRef(PointDataFacade->Source, Context->DataIOInit); }
			}
			break;
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE

﻿// Copyright 2025 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Paths/PCGExShiftPath.h"


#define LOCTEXT_NAMESPACE "PCGExShiftPathElement"
#define PCGEX_NAMESPACE ShiftPath

UPCGExShiftPathSettings::UPCGExShiftPathSettings(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bSupportClosedLoops = false;
}

#if WITH_EDITOR
void UPCGExShiftPathSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

PCGEX_INITIALIZE_ELEMENT(ShiftPath)

bool FPCGExShiftPathElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPathProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(ShiftPath)
	return true;
}


bool FPCGExShiftPathElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExShiftPathElement::Execute);

	PCGEX_CONTEXT(ShiftPath)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		PCGEX_ON_INVALILD_INPUTS(FTEXT("Some inputs have less than 2 points and won't be processed."))

		// TODO : Skip completion

		if (!Context->StartBatchProcessingPoints<PCGExPointsMT::TBatch<PCGExShiftPath::FProcessor>>(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry) { return true; },
			[&](const TSharedPtr<PCGExPointsMT::TBatch<PCGExShiftPath::FProcessor>>& NewBatch)
			{
				NewBatch->bPrefetchData = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any paths to fuse."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGEx::State_Done)

	Context->MainBatch->Output();
	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExShiftPath
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InAsyncManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExShiftPath::Process);

		if (!FPointsProcessor::Process(InAsyncManager)) { return false; }

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)

		MaxIndex = PointDataFacade->GetNum(PCGExData::ESource::In) - 1;
		PivotIndex = Settings->bReverseShift ? MaxIndex : 0;

		if (Settings->InputMode == EPCGExShiftPathMode::Relative)
		{
			PivotIndex = PCGEx::TruncateDbl(
				static_cast<double>(MaxIndex) * static_cast<double>(Settings->RelativeConstant),
				Settings->Truncate);
		}
		else if (Settings->InputMode == EPCGExShiftPathMode::Discrete)
		{
			PivotIndex = Settings->DiscreteConstant;
		}
		else if (Settings->InputMode == EPCGExShiftPathMode::Filter)
		{
			if (Context->FilterFactories.IsEmpty()) { return false; }

			PCGEX_ASYNC_GROUP_CHKD(AsyncManager, FilterTask)

			FilterTask->OnCompleteCallback =
				[PCGEX_ASYNC_THIS_CAPTURE]()
				{
					PCGEX_ASYNC_THIS

					if (This->Settings->bReverseShift)
					{
						for (int i = This->MaxIndex; i >= 0; i--)
						{
							if (This->PointFilterCache[i])
							{
								This->PivotIndex = i;
								return;
							}
						}
					}
					else
					{
						for (int i = 0; i <= This->MaxIndex; i++)
						{
							if (This->PointFilterCache[i])
							{
								This->PivotIndex = i;
								return;
							}
						}
					}
				};

			FilterTask->OnSubLoopStartCallback =
				[PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
				{
					PCGEX_ASYNC_THIS
					This->PrepareSingleLoopScopeForPoints(Scope);
				};

			FilterTask->StartSubLoops(PointDataFacade->GetNum(), GetDefault<UPCGExGlobalSettings>()->GetPointsBatchChunkSize());
			return true;
		}

		if (Settings->bReverseShift) { PivotIndex = MaxIndex - PivotIndex; }
		PivotIndex = PCGExMath::SanitizeIndex(PivotIndex, MaxIndex, Settings->IndexSafety);

		if (!FMath::IsWithinInclusive(PivotIndex, 0, MaxIndex))
		{
			PCGE_LOG_C(Warning, GraphAndLog, Context, FText::FromString(TEXT("Some data has invalid pivot index.")));
		}

		return true;
	}

	void FProcessor::PrepareSingleLoopScopeForPoints(const PCGExMT::FScope& Scope)
	{
		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);
	}


	void FProcessor::CompleteWork()
	{
		if (PivotIndex == 0 || PivotIndex == MaxIndex) { return; }

		if (!FMath::IsWithinInclusive(PivotIndex, 0, MaxIndex))
		{
			bIsProcessorValid = false;
			return;
		}

		TArray<FPCGPoint>& MutablePoints = PointDataFacade->GetMutablePoints();

		if (Settings->bReverseShift)
		{
			PCGExMath::ReverseRange(MutablePoints, 0, PivotIndex);
			PCGExMath::ReverseRange(MutablePoints, PivotIndex + 1, MaxIndex);
		}
		else
		{
			PCGExMath::ReverseRange(MutablePoints, 0, PivotIndex - 1);
			PCGExMath::ReverseRange(MutablePoints, PivotIndex, MaxIndex);
		}

		Algo::Reverse(MutablePoints);

		if (Settings->ShiftType == EPCGExShiftType::Index) { return; }

		const int32 NumPoints = MutablePoints.Num();
		const TArray<FPCGPoint>& InPoints = PointDataFacade->GetIn()->GetPoints();

		if (Settings->ShiftType == EPCGExShiftType::Metadata)
		{
			for (int i = 0; i < NumPoints; i++)
			{
				FPCGPoint& OutPoint = MutablePoints[i];
				const FPCGPoint& InPoint = InPoints[i];
				const PCGMetadataEntryKey OutKey = MutablePoints[i].MetadataEntry;
				OutPoint = InPoint;
				OutPoint.MetadataEntry = OutKey;
			}
		}
		else if (Settings->ShiftType == EPCGExShiftType::Properties)
		{
			for (int i = 0; i < NumPoints; i++)
			{
				FPCGPoint& OutPoint = MutablePoints[i];
				OutPoint.MetadataEntry = InPoints[i].MetadataEntry;
			}
		}
		else if (Settings->ShiftType == EPCGExShiftType::MetadataAndProperties)
		{
			for (int i = 0; i < NumPoints; i++)
			{
				MutablePoints[i].Transform = InPoints[i].Transform;
			}
		}
	}
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE

﻿// Copyright 2025 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Misc/PCGExCollocationCount.h"


#define LOCTEXT_NAMESPACE "PCGExCollocationCountElement"
#define PCGEX_NAMESPACE CollocationCount

PCGEX_INITIALIZE_ELEMENT(CollocationCount)

bool FPCGExCollocationCountElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(CollocationCount)

	PCGEX_VALIDATE_NAME(Settings->CollicationNumAttributeName)
	if (Settings->bWriteLinearOccurences) { PCGEX_VALIDATE_NAME(Settings->LinearOccurencesAttributeName) }

	return true;
}

bool FPCGExCollocationCountElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExCollocationCountElement::Execute);

	PCGEX_CONTEXT(CollocationCount)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartBatchProcessingPoints<PCGExPointsMT::TBatch<PCGExCollocationCount::FProcessor>>(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry) { return true; },
			[&](const TSharedPtr<PCGExPointsMT::TBatch<PCGExCollocationCount::FProcessor>>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points to process."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGEx::State_Done)

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExCollocationCount
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InAsyncManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExCollocationCount::Process);

		if (!FPointsProcessor::Process(InAsyncManager)) { return false; }

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)

		NumPoints = PointDataFacade->GetNum();
		ToleranceConstant = Settings->Tolerance;

		CollocationWriter = PointDataFacade->GetWritable(Settings->CollicationNumAttributeName, 0, true, PCGExData::EBufferInit::New);

		if (Settings->bWriteLinearOccurences)
		{
			LinearOccurencesWriter = PointDataFacade->GetWritable(Settings->LinearOccurencesAttributeName, 0, true, PCGExData::EBufferInit::New);
		}

		Octree = &PointDataFacade->Source->GetIn()->PCGEX_POINT_OCTREE_GET();

		StartParallelLoopForPoints();

		return true;
	}

#if PCGEX_ENGINE_VERSION < 506
#define PCGEX_POINTREF_INDEX const int32 OtherIndex = static_cast<int32>(PointRef.Point - InPoints.GetData());
#else
#define PCGEX_POINTREF_INDEX const int32 OtherIndex = PointRef.Index;
#endif

	void FProcessor::ProcessSinglePoint(const int32 Index, FPCGPoint& Point, const PCGExMT::FScope& Scope)
	{
		const TArray<FPCGPoint>& InPoints = PointDataFacade->Source->GetIn()->GetPoints();

		const FVector Center = Point.Transform.GetLocation();
		const double Tolerance = ToleranceConstant;
		const FBoxCenterAndExtent BCAE = FBoxCenterAndExtent(Center, FVector(Tolerance));

		CollocationWriter->GetMutable(Index) = 0;

		auto ProcessNeighbors = [&](const PCGEX_POINT_OCTREE_REF& PointRef)
		{
			PCGEX_POINTREF_INDEX
			if (OtherIndex == Index) { return; }
			if (FVector::Dist(Center, InPoints[OtherIndex].Transform.GetLocation()) > Tolerance) { return; }

			CollocationWriter->GetMutable(Index) += 1;
		};

		auto ProcessNeighbors2 = [&](const PCGEX_POINT_OCTREE_REF& PointRef)
		{
			PCGEX_POINTREF_INDEX
			if (OtherIndex == Index) { return; }
			if (FVector::Dist(Center, InPoints[OtherIndex].Transform.GetLocation()) > Tolerance) { return; }

			CollocationWriter->GetMutable(Index) += 1;

			if (OtherIndex < Index) { LinearOccurencesWriter->GetMutable(Index) += 1; }
		};

		if (LinearOccurencesWriter)
		{
			LinearOccurencesWriter->GetMutable(Index) = 0;
			Octree->FindElementsWithBoundsTest(BCAE, ProcessNeighbors2);
		}
		else
		{
			Octree->FindElementsWithBoundsTest(BCAE, ProcessNeighbors);
		}
	}

#undef PCGEX_POINTREF_INDEX

	void FProcessor::CompleteWork()
	{
		PointDataFacade->Write(AsyncManager);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE

﻿// Copyright 2025 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Paths/SubPoints/PCGExSubPointsOperation.h"

#include "Data/PCGExPointIO.h"


void UPCGExSubPointsOperation::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	Super::CopySettingsFrom(Other);
	if (const UPCGExSubPointsOperation* TypedOther = Cast<UPCGExSubPointsOperation>(Other))
	{
		bClosedLoop = TypedOther->bClosedLoop;
	}
}

void UPCGExSubPointsOperation::PrepareForData(const TSharedPtr<PCGExData::FFacade>& InTargetFacade, const TSet<FName>* IgnoreAttributeSet)
{
}

void UPCGExSubPointsOperation::ProcessSubPoints(
	const PCGExData::FConstPoint& From,
	const PCGExData::FConstPoint& To,
	const TArrayView<FPCGPoint>& SubPoints,
	const PCGExPaths::FPathMetrics& Metrics,
	const int32 StartIndex) const
{
}

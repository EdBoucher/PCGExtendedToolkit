﻿// Copyright Timothé Lapetite 2023
// Released under the MIT license https://opensource.org/license/MIT/


#include "Splines/SubPoints/DataBlending/PCGExSubPointsDataBlend.h"

#include "Data/PCGExPointIO.h"
#include "Data/Blending/PCGExMetadataBlender.h"

EPCGExDataBlendingType UPCGExSubPointsDataBlend::GetDefaultBlending()
{
	return EPCGExDataBlendingType::Copy;
}

void UPCGExSubPointsDataBlend::PrepareForData(const UPCGExPointIO* InData)
{
	Super::PrepareForData(InData);
	if (!InternalBlender) { InternalBlender = NewObject<UPCGExMetadataBlender>(); }
	PrepareForData(InData->Out, InData->Out);
}

void UPCGExSubPointsDataBlend::PrepareForData(const UPCGPointData* InPrimaryData, const UPCGPointData* InSecondaryData)
{
	InternalBlender->DefaultOperation = GetDefaultBlending();
	InternalBlender->PrepareForData(InPrimaryData, InSecondaryData, BlendingOverrides);
}

void UPCGExSubPointsDataBlend::ProcessSubPoints(const FPCGPoint& StartPoint, const FPCGPoint& EndPoint, TArrayView<FPCGPoint>& SubPoints, const double PathLength) const
{
	ProcessSubPoints(StartPoint, EndPoint, SubPoints, PathLength, InternalBlender);
}

void UPCGExSubPointsDataBlend::ProcessSubPoints(const FPCGPoint& StartPoint, const FPCGPoint& EndPoint, TArrayView<FPCGPoint>& SubPoints, const double PathLength, const UPCGExMetadataBlender* InBlender) const
{
}

UPCGExMetadataBlender* UPCGExSubPointsDataBlend::CreateBlender(const UPCGPointData* InPrimaryData, const UPCGPointData* InSecondaryData)
{
	UPCGExMetadataBlender* NewBlender = NewObject<UPCGExMetadataBlender>();
	NewBlender->DefaultOperation = GetDefaultBlending();
	NewBlender->PrepareForData(InPrimaryData, InSecondaryData, BlendingOverrides);
	return NewBlender;
}
﻿// Copyright 2025 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "OrientedBoxTypes.h"

#include "PCGExGlobalSettings.h"

#include "PCGExPointsProcessor.h"
#include "Data/PCGExAttributeHelpers.h"
#include "Data/Blending/PCGExMetadataBlender.h"

#include "PCGExPointsToBounds.generated.h"

UENUM()
enum class EPCGExPointsToBoundsOutputMode : uint8
{
	Collapse  = 0 UMETA(DisplayName = "Collapse", Tooltip="Collapse point set to a single point with the blended properties of the whole."),
	WriteData = 1 UMETA(DisplayName = "Write Data", Tooltip="Leave points unaffected and write the results to the data domain instead."),
};


USTRUCT(BlueprintType)
struct FPCGExPointsToBoundsDataDetails
{
	GENERATED_BODY()

	FPCGExPointsToBoundsDataDetails()
	{
	}

	/** */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteTransform = true;

	/** */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bWriteTransform"))
	FName TransformAttributeName = FName("@Data.Transform");

	/** */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteDensity = true;

	/** */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bWriteDensity"))
	FName DensityAttributeName = FName("@Data.Density");

	/** */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteBoundsMin = true;

	/** */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bWriteBoundsMin"))
	FName BoundsMinAttributeName = FName("@Data.BoundsMin");

	/** */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteBoundsMax = true;

	/** */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bWriteBoundsMax"))
	FName BoundsMaxAttributeName = FName("@Data.BoundsMax");

	/** */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteColor = true;

	/** */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bWriteColor"))
	FName ColorAttributeName = FName("@Data.Color");

	/** */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteSteepness = true;

	/** */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bWriteSteepness"))
	FName SteepnessAttributeName = FName("@Data.Steepness");

	/** */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteBestFitUp = true;

	/** */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bWriteBestFitUp"))
	FName BestFitUpAttributeName = FName("@Data.BestFitUp");

	/** */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ As Transform", EditCondition="bWriteBestFitUp", EditConditionHides, HideInlineEditCondition))
	EPCGExMinimalAxis AsTransformAxis = EPCGExMinimalAxis::None;

	void Output(const UPCGBasePointData* InBoundsData, UPCGBasePointData* OutData, const TArray<FPCGAttributeIdentifier>& AttributeIdentifiers) const;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="misc/points-to-bounds"))
class UPCGExPointsToBoundsSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(PointsToBounds, "Points to Bounds", "Merge points group to a single point representing their bounds.");
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spatial; }
	virtual FLinearColor GetNodeTitleColor() const override { return GetDefault<UPCGExGlobalSettings>()->WantsColor(GetDefault<UPCGExGlobalSettings>()->NodeColorMiscAdd); }
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Output Object Oriented Bounds. Note that this only accounts for positions and will ignore point bounds. **/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bOutputOrientedBoundingBox = false;

	/** Overlap overlap test mode */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExPointBoundsSource BoundsSource = EPCGExPointBoundsSource::ScaledBounds;

	/** How to reduce data */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExPointsToBoundsOutputMode OutputMode = EPCGExPointsToBoundsOutputMode::Collapse;

	/** Bound point is the result of its contents */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(InlineEditConditionToggle))
	bool bBlendProperties = true;

	/** Defines how fused point properties and attributes are merged into the final point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(EditCondition="bBlendProperties"))
	FPCGExBlendingDetails BlendingSettings = FPCGExBlendingDetails(EPCGExDataBlendingType::Average, EPCGExDataBlendingType::None);

	/** Which data to write. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(EditCondition="OutputMode == EPCGExPointsToBoundsOutputMode::WriteData"))
	FPCGExPointsToBoundsDataDetails DataDetails;

	/** Write point counts */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(InlineEditConditionToggle))
	bool bWritePointsCount = false;

	/** Attribute to write points count to */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(EditCondition="bWritePointsCount"))
	FName PointsCountAttributeName = "@Data.MergedPointsCount";

private:
	friend class FPCGExPointsToBoundsElement;
};

struct FPCGExPointsToBoundsContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExPointsToBoundsElement;
};

class FPCGExPointsToBoundsElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(PointsToBounds)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};

namespace PCGExPointsToBounds
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExPointsToBoundsContext, UPCGExPointsToBoundsSettings>
	{
		TSharedPtr<PCGExData::FPointIO> OutputIO;
		TSharedPtr<PCGExData::FFacade> OutputFacade;
		TArray<FPCGAttributeIdentifier> BlendedAttributes;
		TSharedPtr<PCGExDataBlending::FMetadataBlender> MetadataBlender;
		FBox Bounds;

		UE::Geometry::FOrientedBox3d OrientedBox;
		bool bOrientedBoxFound = false;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade):
			TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InAsyncManager) override;
		virtual void CompleteWork() override;
	};
}

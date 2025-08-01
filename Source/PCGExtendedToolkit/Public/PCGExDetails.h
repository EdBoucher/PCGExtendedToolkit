﻿// Copyright 2025 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGEx.h"
#include "CollisionQueryParams.h"
#include "Elements/PCGActorSelector.h"

#include "PCGExDetails.generated.h"

struct FPCGExContext;
struct FHitResult;
class UWorld;
class AActor;

#pragma region PCG exposition
// Exposed copy of the otherwise private PCG' spatial noise mode enum
UENUM(BlueprintType)
enum class PCGExSpatialNoiseMode : uint8
{
	/** Your classic perlin noise. */
	Perlin,
	/** Based on underwater fake caustic rendering, gives swirly look. */
	Caustic,
	/** Voronoi noise, result a the distance to edge and cell ID. */
	Voronoi,
	/** Based on fractional brownian motion. */
	FractionalBrownian,
	/** Used to create masks to blend out edges. */
	EdgeMask,
};

UENUM(BlueprintType)
enum class PCGExSpatialNoiseMask2DMode : uint8
{
	/** Your classic perlin noise. */
	Perlin,
	/** Based on underwater fake caustic rendering, gives swirly look. */
	Caustic,
	/** Based on fractional brownian motion. */
	FractionalBrownian,
};

#pragma endregion

UENUM()
enum class EPCGExInputValueType : uint8
{
	Constant  = 0 UMETA(DisplayName = "Constant", Tooltip="Constant."),
	Attribute = 1 UMETA(DisplayName = "Attribute", Tooltip="Attribute."),
};

UENUM()
enum class EPCGExDataInputValueType : uint8
{
	Constant  = 0 UMETA(DisplayName = "Constant", Tooltip="Constant."),
	Attribute = 1 UMETA(DisplayName = "@Data", Tooltip="Attribute. Can only read from @Data domain."),
};

UENUM()
enum class EPCGExFilterDataAction : uint8
{
	Keep = 0 UMETA(DisplayName = "Keep", ToolTip="Keeps only selected data"),
	Omit = 1 UMETA(DisplayName = "Omit", ToolTip="Omit selected data from output"),
	Tag  = 2 UMETA(DisplayName = "Tag", ToolTip="Keep all and Tag"),
};

UENUM()
enum class EPCGExSubdivideMode : uint8
{
	Distance  = 0 UMETA(DisplayName = "Distance", ToolTip="Number of subdivisions depends on length"),
	Count     = 1 UMETA(DisplayName = "Count", ToolTip="Number of subdivisions is fixed"),
	Manhattan = 2 UMETA(DisplayName = "Manhattan", ToolTip="Manhattan subdivision, number of subdivisions depends on spatial relationship between the points; will be in the [0..2] range."),
};

USTRUCT(BlueprintType)
struct PCGEXTENDEDTOOLKIT_API FPCGExCollisionDetails
{
	GENERATED_BODY()

	FPCGExCollisionDetails()
	{
	}

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bTraceComplex = false;

	/** Collision type to check against */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExCollisionFilterType CollisionType = EPCGExCollisionFilterType::Channel;

	/** Collision channel to check against */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="CollisionType == EPCGExCollisionFilterType::Channel", EditConditionHides, Bitmask, BitmaskEnum="/Script/Engine.ECollisionChannel"))
	TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_WorldDynamic;

	/** Collision object type to check against */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="CollisionType == EPCGExCollisionFilterType::ObjectType", EditConditionHides, Bitmask, BitmaskEnum="/Script/Engine.EObjectTypeQuery"))
	int32 CollisionObjectType = ObjectTypeQuery1;

	/** Collision profile to check against */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="CollisionType == EPCGExCollisionFilterType::Profile", EditConditionHides))
	FName CollisionProfileName = NAME_None;

	/** Ignore this graph' PCG content */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bIgnoreSelf = true;

	/** Ignore a procedural selection of actors */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bIgnoreActors = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bIgnoreActors"))
	FPCGActorSelectorSettings IgnoredActorSelector;

	TArray<AActor*> IgnoredActors;
	UWorld* World = nullptr;

	void Init(FPCGExContext* InContext);
	void Update(FCollisionQueryParams& InCollisionParams) const;
	bool Linecast(const FVector& From, const FVector& To, FHitResult& HitResult) const;
};

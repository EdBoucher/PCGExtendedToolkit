﻿// Copyright 2025 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExPointsProcessor.h"
#include "PCGExTensor.h"
#include "PCGExTensorFactoryProvider.h"
#include "PCGExTensorOperation.h"

#include "PCGExTensorPole.generated.h"

USTRUCT(BlueprintType)
struct FPCGExTensorPoleConfig : public FPCGExTensorConfigBase
{
	GENERATED_BODY()

	FPCGExTensorPoleConfig() :
		FPCGExTensorConfigBase()
	{
	}
};

/**
 * 
 */
class FPCGExTensorPole : public PCGExTensorPointOperation
{
public:
	FPCGExTensorPoleConfig Config;
	virtual bool Init(FPCGExContext* InContext, const UPCGExTensorFactoryData* InFactory) override;

	virtual PCGExTensor::FTensorSample Sample(int32 InSeedIndex, const FTransform& InProbe) const override;
};


UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExTensorPoleFactory : public UPCGExTensorPointFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExTensorPoleConfig Config;

	virtual TSharedPtr<PCGExTensorOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Tensors|Params", meta=(PCGExNodeLibraryDoc="tensors/effectors/tensor-pole"))
class UPCGExCreateTensorPoleSettings : public UPCGExTensorPointFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(TensorPole, "Tensor : Pole", "A tensor that pull and/or pushes")

#endif
	//~End UPCGSettings

	/** Tensor properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExTensorPoleConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};

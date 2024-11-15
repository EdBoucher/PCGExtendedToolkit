﻿// Copyright Timothé Lapetite 2024
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Geometry/PCGExGeoPrimtives.h"
#include "Graph/PCGExCluster.h"
#include "Components/BaseDynamicMeshComponent.h"

#include "PCGExTopology.generated.h"

UENUM()
enum class EPCGExTopologyOutputType : uint8
{
	PerItem = 1 UMETA(DisplayName = "Per-item Geometry", Tooltip="Output a geometry object per-item"),
	Merged  = 0 UMETA(DisplayName = "Merged Geometry", Tooltip="Output a single geometry that merges all generated topologies"),
};

UENUM()
enum class EPCGExCellShapeTypeOutput : uint8
{
	Both        = 0 UMETA(DisplayName = "Convex & Concave", ToolTip="Output both convex and concave cells"),
	ConvexOnly  = 1 UMETA(DisplayName = "Convex Only", ToolTip="Output only convex cells"),
	ConcaveOnly = 2 UMETA(DisplayName = "Concave Only", ToolTip="Output only concave cells")
};

UENUM()
enum class EPCGExCellSeedLocation : uint8
{
	Original         = 0 UMETA(DisplayName = "Original", ToolTip="Seed position is unchanged"),
	Centroid         = 1 UMETA(DisplayName = "Centroid", ToolTip="Place the seed at the centroid of the path"),
	PathBoundsCenter = 2 UMETA(DisplayName = "Path bounds center", ToolTip="Place the seed at the center of the path' bounds"),
	FirstNode        = 3 UMETA(DisplayName = "First Node", ToolTip="Place the seed on the position of the node that started the cell."),
	LastNode         = 4 UMETA(DisplayName = "Last Node", ToolTip="Place the seed on the position of the node that ends the cell.")
};

UENUM()
enum class EPCGExCellSeedBounds : uint8
{
	Original           = 0 UMETA(DisplayName = "Original", ToolTip="Seed bounds is unchanged"),
	MatchCell          = 1 UMETA(DisplayName = "Match Cell", ToolTip="Seed bounds match cell bounds"),
	MatchPathResetQuat = 2 UMETA(DisplayName = "Match Cell (with rotation reset)", ToolTip="Seed bounds match cell bounds, and rotation is reset"),
};

USTRUCT(BlueprintType)
struct /*PCGEXTENDEDTOOLKIT_API*/ FPCGExCellConstraintsDetails
{
	GENERATED_BODY()

	FPCGExCellConstraintsDetails()
	{
	}

	explicit FPCGExCellConstraintsDetails(bool InUsedForPaths)
		: bUsedForPaths(InUsedForPaths)
	{
	}

	UPROPERTY()
	bool bUsedForPaths = false;

	/**  */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExCellShapeTypeOutput AspectFilter = EPCGExCellShapeTypeOutput::Both;

	/** Ensure there's no duplicate cells. This can happen when using seed-based search where multiple seed yield the same final cell. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bUsedForPaths", EditConditionHides, HideEditConditionToggle))
	bool bDedupeCells = true;

	/** Keep only cells that closed gracefully; i.e connect to their start node */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bClosedCellsOnly = true;

	/** Whether to keep cells that include dead ends wrapping */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bKeepCellsWithDeadEnds = true;

	/** Whether to duplicate dead end points */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bKeepCellsWithDeadEnds && bUsedForPaths", EditConditionHides, HideEditConditionToggle))
	bool bDuplicateDeadEnds = false;

	/**  */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditConditionHides))
	bool bOmitWrappingBounds = true;

	/** Omit cells with bounds that closely match the ones from the input set */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bOmitWrappingBounds", ClampMin=0))
	double WrappingBoundsSizeTolerance = 100;

	/**  */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOmitBelowBoundsSize = false;

	/** Omit cells whose bounds size.length is smaller than the specified amount */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bOmitBelowBoundsSize", ClampMin=0))
	double MinBoundsSize = 3;

	/**  */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOmitAboveBoundsSize = false;

	/** Omit cells whose bounds size.length is larger than the specified amount */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bOmitAboveBoundsSize", ClampMin=0))
	double MaxBoundsSize = 500;

	/**  */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOmitBelowPointCount = false;

	/** Omit cells whose point count is smaller than the specified amount */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bOmitBelowPointCount", ClampMin=0))
	int32 MinPointCount = 3;

	/**  */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOmitAbovePointCount = false;

	/** Omit cells whose point count is larger than the specified amount */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bOmitAbovePointCount", ClampMin=0))
	int32 MaxPointCount = 500;
};

namespace PCGExTopology
{
	class FCell;
}

USTRUCT(BlueprintType)
struct /*PCGEXTENDEDTOOLKIT_API*/ FPCGExCellSeedMutationDetails
{
	GENERATED_BODY()

	FPCGExCellSeedMutationDetails()
	{
	}

	explicit FPCGExCellSeedMutationDetails(bool InUsedForPaths)
		: bUsedForPaths(InUsedForPaths)
	{
	}

	UPROPERTY()
	bool bUsedForPaths = false;

	/**  */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExCellShapeTypeOutput AspectFilter = EPCGExCellShapeTypeOutput::Both;

	/** Change the good seed position */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExCellSeedLocation Location = EPCGExCellSeedLocation::Centroid;

	/** */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bMatchCellBounds = true;

	/** */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bResetScale = true;

	/** */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bResetRotation = true;

	void ApplyToPoint(const PCGExTopology::FCell* InCell, FPCGPoint& OutPoint, const TArray<FPCGPoint>& CellPoints) const;
};

USTRUCT(BlueprintType)
struct /*PCGEXTENDEDTOOLKIT_API*/ FPCGExTopologyDetails
{
	GENERATED_BODY()

	FPCGExTopologyDetails()
	{
	}

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bFlipOrientation = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EDynamicMeshComponentDistanceFieldMode DistanceFieldMode = EDynamicMeshComponentDistanceFieldMode::NoDistanceField;
};

namespace PCGExTopology
{
	enum class ETriangulationResult : uint8
	{
		Unknown = 0,
		Success,
		InvalidCell,
		TooFewPoints,
		UnsupportedAspect,
		InvalidCluster,
	};

	enum class ECellResult : uint8
	{
		Unknown = 0,
		Success,
		Duplicate,
		DeadEnd,
		WrongAspect,
		ExceedPointsLimit,
		BelowPointsLimit,
		ExceedBoundsLimit,
		BelowBoundsLimit,
		OpenCell,
	};

	const FName SourceEdgeConstrainsFiltersLabel = FName("ConstrainedEdgeFilters");

	static FORCEINLINE void MarkTriangle(
		const TSharedPtr<PCGExCluster::FCluster>& InCluster,
		const PCGExGeo::FTriangle& InTriangle)
	{
		FPlatformAtomics::InterlockedExchange(&InCluster->GetNode(InTriangle.Vtx[0])->bValid, 1);
		FPlatformAtomics::InterlockedExchange(&InCluster->GetNode(InTriangle.Vtx[1])->bValid, 1);
		FPlatformAtomics::InterlockedExchange(&InCluster->GetNode(InTriangle.Vtx[2])->bValid, 1);
	}

	class FCell;

	class FCellConstraints : public TSharedFromThis<FCellConstraints>
	{
	public:
		mutable FRWLock UniquePathsBoxHashLock;
		mutable FRWLock UniquePathsStartHashLock;
		TSet<uint32> UniquePathsBoxHash;
		TSet<uint64> UniquePathsStartHash;

		bool bConcaveOnly = false;
		bool bConvexOnly = false;
		bool bKeepCellsWithDeadEnds = true;
		bool bDuplicateDeadEndPoints = false;
		bool bClosedLoopOnly = false;

		int32 MaxPointCount = MAX_int32;
		int32 MinPointCount = MIN_int32;

		double MaxBoundsSize = MAX_dbl;
		double MinBoundsSize = MIN_dbl;

		FBox DataBounds = FBox(ForceInit);
		bool bDoWrapperCheck = false;
		double WrapperCheckTolerance = MAX_dbl;

		bool bDedupe = true;

		FCellConstraints()
		{
		}

		explicit FCellConstraints(const FPCGExCellConstraintsDetails& InDetails)
		{
			bDedupe = true;
			bConcaveOnly = InDetails.AspectFilter == EPCGExCellShapeTypeOutput::ConcaveOnly;
			bConvexOnly = InDetails.AspectFilter == EPCGExCellShapeTypeOutput::ConvexOnly;
			bClosedLoopOnly = InDetails.bClosedCellsOnly;
			bKeepCellsWithDeadEnds = InDetails.bKeepCellsWithDeadEnds;
			bDuplicateDeadEndPoints = InDetails.bDuplicateDeadEnds;

			if (InDetails.bOmitBelowPointCount) { MinPointCount = InDetails.MinPointCount; }
			if (InDetails.bOmitAbovePointCount) { MaxPointCount = InDetails.MaxPointCount; }

			if (InDetails.bOmitBelowBoundsSize) { MinBoundsSize = InDetails.MinBoundsSize; }
			if (InDetails.bOmitAboveBoundsSize) { MaxBoundsSize = InDetails.MaxBoundsSize; }

			bDoWrapperCheck = InDetails.bOmitWrappingBounds;
			WrapperCheckTolerance = InDetails.WrappingBoundsSizeTolerance;
			bDedupe = InDetails.bDedupeCells;
		}

		bool ContainsSignedEdgeHash(const uint64 Hash) const;
		bool IsUniqueStartHash(const uint64 Hash);
		bool IsUniqueCellHash(const FCell* InCell);
	};

	class FCell : public TSharedFromThis<FCell>
	{
	protected:
		int32 Sign = 0;

	public:
		TArray<int32> Nodes;
		FBox Bounds = FBox(ForceInit);
		TSharedRef<FCellConstraints> Constraints;
		FVector Centroid = FVector::ZeroVector;

		bool bIsConvex = true;
		bool bCompiledSuccessfully = false;
		bool bIsClosedLoop = false;

		explicit FCell(const TSharedRef<FCellConstraints>& InConstraints)
			: Constraints(InConstraints)
		{
		}

		~FCell() = default;

		ECellResult BuildFromCluster(
			const int32 SeedNodeIndex,
			const int32 SeedEdgeIndex,
			const FVector& Guide,
			TSharedRef<PCGExCluster::FCluster> InCluster,
			const TArray<FVector>& ProjectedPositions,
			TSharedPtr<TArray<PCGExCluster::FExpandedNode>> ExpandedNodes);

		ECellResult BuildFromPath(
			const TArray<FVector>& ProjectedPositions);

		template <bool bMarkTriangles = false>
		ETriangulationResult Triangulate(
			const TArray<FVector>& ProjectedPositions,
			TArray<PCGExGeo::FTriangle>& OutTriangles,
			const TSharedPtr<PCGExCluster::FCluster> InCluster = nullptr)
		{
			if constexpr (bMarkTriangles) { if (!InCluster) { return ETriangulationResult::InvalidCluster; } }
			if (!bCompiledSuccessfully) { return ETriangulationResult::InvalidCell; }
			if (Nodes.Num() < 3) { return ETriangulationResult::TooFewPoints; }
			if (bIsConvex || Nodes.Num() == 3) { return TriangulateFan<bMarkTriangles>(ProjectedPositions, OutTriangles, InCluster); }
			else { return TriangulateEarClipping<bMarkTriangles>(ProjectedPositions, OutTriangles, InCluster); }
		}

		int32 GetTriangleNumEstimate() const;

		void PostProcessPoints(TArray<FPCGPoint>& InMutablePoints);

	protected:
		template <bool bMarkTriangles = false>
		ETriangulationResult TriangulateFan(
			const TArray<FVector>& ProjectedPositions,
			TArray<PCGExGeo::FTriangle>& OutTriangles,
			const TSharedPtr<PCGExCluster::FCluster> InCluster = nullptr)
		{
			if (!bCompiledSuccessfully) { return ETriangulationResult::InvalidCell; }
			if (!bIsConvex) { return ETriangulationResult::UnsupportedAspect; }
			if (Nodes.Num() < 3) { return ETriangulationResult::TooFewPoints; }
			const int32 MaxIndex = Nodes.Num() - 1;

			TArrayView<const FVector> Positions = MakeArrayView(ProjectedPositions);

			for (int i = 1; i < MaxIndex; i++)
			{
				PCGExGeo::FTriangle& T = OutTriangles.Emplace_GetRef(
					InCluster->GetNode(Nodes[0])->PointIndex,
					InCluster->GetNode(Nodes[i])->PointIndex,
					InCluster->GetNode(Nodes[i + 1])->PointIndex);
				T.FixWinding(Positions);

				if constexpr (bMarkTriangles) { MarkTriangle(InCluster, T); }
			}

			return ETriangulationResult::Success;
		}

		template <bool bMarkTriangles = false>
		ETriangulationResult TriangulateEarClipping(
			const TArray<FVector>& ProjectedPositions,
			TArray<PCGExGeo::FTriangle>& OutTriangles,
			const TSharedPtr<PCGExCluster::FCluster> InCluster = nullptr)
		{
			if (!bCompiledSuccessfully) { return ETriangulationResult::InvalidCell; }

			int32 NumNodes = Nodes.Num();
			if (NumNodes < 3) { return ETriangulationResult::TooFewPoints; }

			TArrayView<const FVector> Positions = MakeArrayView(ProjectedPositions);

			TArray<int32> NodeQueue;
			PCGEx::ArrayOfIndices(NodeQueue, NumNodes);

			int32 PrevIndex = NumNodes - 1;
			int32 CurrIndex = 0;
			int32 NextIndex = 1;

			while (NodeQueue.Num() > 2)
			{
				bool bEarFound = false;

				for (int32 i = 0; i < NodeQueue.Num(); i++)
				{
					int32 AIdx = NodeQueue[PrevIndex];
					int32 BIdx = NodeQueue[CurrIndex];
					int32 CIdx = NodeQueue[NextIndex];

					FVector A = ProjectedPositions[InCluster->GetNode(Nodes[AIdx])->PointIndex];
					FVector B = ProjectedPositions[InCluster->GetNode(Nodes[BIdx])->PointIndex];
					FVector C = ProjectedPositions[InCluster->GetNode(Nodes[CIdx])->PointIndex];

					FBox TBox = FBox(ForceInit);
					TBox += A;
					TBox += B;
					TBox += C;

					// Check if triangle ABC is an ear
					bool bIsEar = true;

					for (int32 j = 0; j < NodeQueue.Num(); j++)
					{
						if (j == AIdx || j == BIdx || j == CIdx) { continue; }

						const FVector& P = ProjectedPositions[InCluster->GetNode(Nodes[NodeQueue[j]])->PointIndex];
						if (!TBox.IsInside(P)) { continue; }

						if (PCGExGeo::IsPointInTriangle(P, A, B, C))
						{
							bIsEar = false;
							break;
						}
					}

					if (bIsEar)
					{
						// Add the ear triangle to the result
						PCGExGeo::FTriangle& T = OutTriangles.Emplace_GetRef(
							InCluster->GetNode(Nodes[0])->PointIndex,
							InCluster->GetNode(Nodes[i])->PointIndex,
							InCluster->GetNode(Nodes[i + 1])->PointIndex);

						T.FixWinding(Positions);

						if constexpr (bMarkTriangles) { MarkTriangle(InCluster, T); }

						NodeQueue.RemoveAtSwap(CurrIndex);
						NumNodes = NodeQueue.Num();
						PrevIndex = (CurrIndex - 1 + NumNodes) % NumNodes;
						CurrIndex = CurrIndex % NumNodes;
						NextIndex = (CurrIndex + 1) % NumNodes;

						bEarFound = true;
						break;
					}

					PrevIndex = CurrIndex;
					CurrIndex = NextIndex;
					NextIndex = (NextIndex + 1) % NodeQueue.Num();
				}

				if (!bEarFound) { return ETriangulationResult::InvalidCell; }
			}

			return ETriangulationResult::Success;
		}
	};
}

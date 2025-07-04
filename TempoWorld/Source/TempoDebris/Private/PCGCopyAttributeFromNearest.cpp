// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "PCGCopyAttributeFromNearest.h"

#include "PCGContext.h"
#include "PCGPin.h"
#include "Data/PCGSpatialData.h"
#include "Helpers/PCGAsync.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"

#if ENGINE_MINOR_VERSION >= 6
#include "Data/PCGBasePointData.h"
#else
#include "Data/PCGPointData.h"
#endif

#define LOCTEXT_NAMESPACE "PCGDistanceElement"

namespace PCGDistance
{
	const FName SourceLabel = TEXT("Source");
	const FName TargetLabel = TEXT("Target");
}

namespace
{
	FPCGDataCollection GatherDataForPin(const FPCGDataCollection& InputData, const FName InputLabel = PCGPinConstants::DefaultInputLabel, const FName OutputLabel = PCGPinConstants::DefaultOutputLabel)
	{
		TArray<FPCGTaggedData> GatheredData = InputData.GetInputsByPin(InputLabel);
		FPCGDataCollection Output;

		if (GatheredData.IsEmpty())
		{
			return Output;
		}

		if (GatheredData.Num() == InputData.TaggedData.Num())
		{
			Output = InputData;
		}
		else
		{
			Output.TaggedData = MoveTemp(GatheredData);
		}

		for(FPCGTaggedData& TaggedData : Output.TaggedData)
		{
			TaggedData.Pin = OutputLabel;
		}

		return Output;
	}
}

#if WITH_EDITOR
FText UPCGCopyAttributeFromNearestSettings::GetNodeTooltipText() const
{
	return LOCTEXT("PCGCopyAttributeFromNearestTooltip", "Copies an attribute to each of the source points from the nearest point in the target points.");
}
#endif // WITH_EDITOR

TArray<FPCGPinProperties> UPCGCopyAttributeFromNearestSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	FPCGPinProperties& PinPropertySource = PinProperties.Emplace_GetRef(PCGDistance::SourceLabel, EPCGDataType::Point);
	PinPropertySource.SetRequiredPin();
	FPCGPinProperties& PinPropertyTarget = PinProperties.Emplace_GetRef(PCGDistance::TargetLabel, EPCGDataType::Point);

#if WITH_EDITOR
	PinPropertySource.Tooltip = LOCTEXT("PCGSourcePinTooltip", "For each of the source points, a distance attribute will be calculated between it and the nearest target point.");
	PinPropertyTarget.Tooltip = LOCTEXT("PCGTargetPinTooltip", "The target points to conduct a distance check with each source point.");
#endif // WITH_EDITOR

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGCopyAttributeFromNearestSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	FPCGPinProperties& PinPropertyOutput = PinProperties.Emplace_GetRef(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point);

#if WITH_EDITOR
	PinPropertyOutput.Tooltip = LOCTEXT("PCGOutputPinTooltip", "The source points will be output with the new attribute copied from the nearest target point.");
#endif // WITH_EDITOR

	return PinProperties;
}

FPCGElementPtr UPCGCopyAttributeFromNearestSettings::CreateElement() const
{
	return MakeShared<FPCGCopyAttributeFromNearestElement>();
}

bool FPCGCopyAttributeFromNearestElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGCopyAttributeFromNearestElement::Execute);
	check(Context);

	if (Context->Node && !Context->Node->IsInputPinConnected(PCGDistance::TargetLabel))
	{
		// If Target pin is unconnected then we no-op and pass through all data from Source pin.
		Context->OutputData = GatherDataForPin(Context->InputData, PCGDistance::SourceLabel);
		return true;
	}

	const UPCGCopyAttributeFromNearestSettings* Settings = Context->GetInputSettings<UPCGCopyAttributeFromNearestSettings>();
	check(Settings);

	const double MaximumDistance = FMath::Max(0.0, Settings->MaximumDistance);

	TArray<FPCGTaggedData> Sources = Context->InputData.GetInputsByPin(PCGDistance::SourceLabel);
	TArray<FPCGTaggedData> Targets = Context->InputData.GetInputsByPin(PCGDistance::TargetLabel);
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

#if ENGINE_MINOR_VERSION >= 6
	TArray<const UPCGBasePointData*> TargetPointDatas;
	TargetPointDatas.Reserve(Targets.Num());
	TMap<const UPCGBasePointData*, TMap<int32, double>> AttributesByData;
#else
	TArray<const UPCGPointData*> TargetPointDatas;
	TargetPointDatas.Reserve(Targets.Num());
	TMap<const FPCGPoint*, double> Attributes;
#endif

	for (const FPCGTaggedData& Target : Targets)
	{
		const UPCGSpatialData* TargetData = Cast<UPCGSpatialData>(Target.Data);

		if (!TargetData)
		{
			PCGE_LOG(Error, GraphAndLog, FText::Format(LOCTEXT("TargetMustBeSpatial", "Target must be Spatial data, found '{0}'"), FText::FromString(Target.Data->GetClass()->GetName())));
			continue;
		}

#if ENGINE_MINOR_VERSION >= 6
		const UPCGBasePointData* TargetPointData = TargetData->ToBasePointData(Context);
#else
		const UPCGPointData* TargetPointData = TargetData->ToPointData(Context);
#endif
		if (!TargetPointData)
		{
			PCGE_LOG(Error, GraphAndLog, FText::Format(LOCTEXT("CannotConvertToPoint", "Cannot convert target '{0}' into Point data"), FText::FromString(Target.Data->GetClass()->GetName())));
			continue;
		}

		TUniquePtr<const IPCGAttributeAccessorKeys> InputKeys;
		TUniquePtr<const IPCGAttributeAccessor> InputAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(TargetPointData, Settings->InputAttribute);
		if (InputAccessor.IsValid())
		{
			using PCG::Private::MetadataTypes;

			if (!IsBroadcastableOrConstructible(InputAccessor->GetUnderlyingType(), MetadataTypes<double>::Id))
			{
				PCGE_LOG(Warning, GraphAndLog, LOCTEXT("InvalidAccessorType", "Selected type for Input Attribute is invalid - only double types supported."));
				InputAccessor = nullptr;
			}
		}

		if (InputAccessor.IsValid())
		{
			InputKeys = PCGAttributeAccessorHelpers::CreateConstKeys(TargetPointData, Settings->InputAttribute);

			if (!InputKeys.IsValid())
			{
				PCGE_LOG(Warning, GraphAndLog, LOCTEXT("CannotCreateAccessorKeys", "Cannot create accessor keys on target points."));
				InputAccessor = nullptr;
			}
		}

#if ENGINE_MINOR_VERSION >= 6
		const int32 NumTargetPoints = TargetPointData->GetNumPoints();
		TArray<double> InAttributes;
		InAttributes.SetNum(NumTargetPoints);
		TArrayView<double> TargetView(InAttributes.GetData(), NumTargetPoints);
		if (InputAccessor.IsValid() && !InputAccessor->GetRange(TargetView, 0, *InputKeys))
		{
			PCGE_LOG(Warning, GraphAndLog, LOCTEXT("GetInputValuesFailed", "Failed to get attribute values on target points."));
			InputAccessor = nullptr;
		}

		TMap<int32, double> AttributesByIndex;
		for (int32 I = 0; I < NumTargetPoints; ++I)
		{
			if (InAttributes.Num() > I)
			{
				AttributesByIndex.Add(I, InAttributes[I]);
			}
		}
		AttributesByData.Add(TargetPointData, AttributesByIndex);
#else
		const TArray<FPCGPoint>& Points = TargetPointData->GetPoints();
		TArray<double> InAttributes;
		InAttributes.SetNum(Points.Num());
		TArrayView<double> TargetView(InAttributes.GetData(), Points.Num());
		if (InputAccessor.IsValid() && !InputAccessor->GetRange(TargetView, 0, *InputKeys))
		{
			PCGE_LOG(Warning, GraphAndLog, LOCTEXT("GetInputValuesFailed", "Failed to get attribute values on target points."));
			InputAccessor = nullptr;
		}
		for (int32 I = 0; I < Points.Num(); ++I)
		{
			if (InAttributes.Num() > I)
			{
				Attributes.Add(&Points[I], InAttributes[I]);
			}
		}
#endif
		TargetPointDatas.Add(TargetPointData);
	}

	// First find the total Input bounds which will determine the size of each cell
	for (const FPCGTaggedData& Source : Sources)
	{
		// Add the point bounds to the input cell
		const UPCGSpatialData* SourceData = Cast<UPCGSpatialData>(Source.Data);

		if (!SourceData)
		{
			PCGE_LOG(Error, GraphAndLog, LOCTEXT("InvalidInputData", "Invalid input data"));
			continue;
		}

#if ENGINE_MINOR_VERSION >= 6
		const UPCGBasePointData* SourcePointData = SourceData->ToBasePointData(Context);
#else
		const UPCGPointData* SourcePointData = SourceData->ToPointData(Context);
#endif
		if (!SourcePointData)
		{
			PCGE_LOG(Error, GraphAndLog, LOCTEXT("CannotConvertToPointData", "Cannot convert input Spatial data to Point data"));
			continue;
		}

#if ENGINE_MINOR_VERSION >= 6
		UPCGBasePointData* OutputData = FPCGContext::NewPointData_AnyThread(Context);
		OutputData->InitializeFromData(SourcePointData);
		OutputData->SetNumPoints(SourcePointData->GetNumPoints(), /*bInitializeValues=*/false);

		if (!OutputData->HasSpatialDataParent())
		{
			OutputData->AllocateProperties(SourcePointData->GetAllocatedProperties());
		}
#else
		UPCGPointData* OutputData = NewObject<UPCGPointData>();
		OutputData->InitializeFromData(SourcePointData);
#endif
		Outputs.Add_GetRef(Source).Data = OutputData;

		OutputData->Metadata->FindOrCreateAttribute<double>(Settings->OutputAttribute.GetAttributeName());

#if ENGINE_MINOR_VERSION >= 6
		// Nothing to do for 5.6 - points are handled via ranges
#else
		TArray<FPCGPoint>& OutPoints = OutputData->GetMutablePoints();
		const TArray<FPCGPoint>& SourcePoints = SourcePointData->GetPoints();
		OutPoints.SetNumUninitialized(SourcePoints.Num());
#endif

		TUniquePtr<IPCGAttributeAccessorKeys> OutputKeys;
		TUniquePtr<IPCGAttributeAccessor> OutputAccessor = PCGAttributeAccessorHelpers::CreateAccessor(OutputData, Settings->OutputAttribute);
		if (OutputAccessor.IsValid())
		{
			using PCG::Private::MetadataTypes;

			if (!IsBroadcastableOrConstructible(OutputAccessor->GetUnderlyingType(), MetadataTypes<double>::Id))
			{
				PCGE_LOG(Warning, GraphAndLog, LOCTEXT("InvalidAccessorType", "Selected type for Output Attribute is invalid - only double types supported."));
				OutputAccessor = nullptr;
			}
		}

		if (OutputAccessor.IsValid())
		{
			OutputKeys = PCGAttributeAccessorHelpers::CreateKeys(OutputData, Settings->OutputAttribute);

			if (!OutputKeys.IsValid())
			{
				PCGE_LOG(Warning, GraphAndLog, LOCTEXT("CannotCreateAccessorKeys", "Cannot create accessor keys on output points."));
				OutputAccessor = nullptr;
			}
		}

		// Set up a cache so we can set all the attributes in a single range set
		TArray<double> ResultCache;
#if ENGINE_MINOR_VERSION >= 6
		ResultCache.SetNumUninitialized(SourcePointData->GetNumPoints());
#else
		ResultCache.SetNumUninitialized(SourcePointData->GetPoints().Num());
#endif

#if ENGINE_MINOR_VERSION >= 6
		auto ProcessDistanceFunc = [&TargetPointDatas, MaximumDistance, &AttributesByData, OutputData, SourcePointData, &ResultCache](int32 StartReadIndex, int32 StartWriteIndex, int32 Count)
		{
			check(StartReadIndex == StartWriteIndex);

			if (!OutputData->HasSpatialDataParent())
			{
				SourcePointData->CopyPointsTo(OutputData, StartReadIndex, StartWriteIndex, Count);
			}

			const TConstPCGValueRange<FTransform> SourceTransformRange = SourcePointData->GetConstTransformValueRange();
			const TConstPCGValueRange<FVector> SourceBoundsMinRange = SourcePointData->GetConstBoundsMinValueRange();
			const TConstPCGValueRange<FVector> SourceBoundsMaxRange = SourcePointData->GetConstBoundsMaxValueRange();

			for (int32 Index = StartReadIndex; Index < (StartReadIndex + Count); ++Index)
			{
				const FTransform& SourceTransform = SourceTransformRange[Index];
				const FVector& SourceBoundsMin = SourceBoundsMinRange[Index];
				const FVector& SourceBoundsMax = SourceBoundsMaxRange[Index];

				const FBoxSphereBounds SourceQueryBounds = FBoxSphereBounds(FBox(SourceBoundsMin - FVector(MaximumDistance), SourceBoundsMax + FVector(MaximumDistance))).TransformBy(SourceTransform);

				const FVector SourceCenter = SourceTransform.TransformPosition(PCGPointHelpers::GetLocalCenter(SourceBoundsMin, SourceBoundsMax));

				double NearestAttribute = 0.0;
				double MinDistanceSquared = TNumericLimits<double>::Max();

				// Signed distance field for calculating the closest point of source and target
				auto CalculateSDF = [Index, &NearestAttribute, &AttributesByData, &MinDistanceSquared, SourcePointData, SourceCenter](const UPCGBasePointData* TargetPointData, const int32 TargetPointIndex, const FBoxSphereBounds& Bounds)
				{
					// If the source pointer and target pointer are the same, ignore distance to the exact same point
					if (Index == TargetPointIndex && SourcePointData == TargetPointData)
					{
						return;
					}

					const double ThisDistanceSquared = (Bounds.Origin - SourceCenter).SquaredLength();

					if (ThisDistanceSquared < MinDistanceSquared)
					{
						if (AttributesByData.Contains(TargetPointData))
						{
							const TMap<int32, double>& AttributesByIndex = AttributesByData[TargetPointData];
							if (AttributesByIndex.Contains(TargetPointIndex))
							{
								NearestAttribute = AttributesByIndex[TargetPointIndex];
							}
						}
						MinDistanceSquared = ThisDistanceSquared;
					}
				};

				for (const UPCGBasePointData* TargetPointData : TargetPointDatas)
				{
					check(TargetPointData);

					const PCGPointOctree::FPointOctree& Octree = TargetPointData->GetPointOctree();

					Octree.FindElementsWithBoundsTest(FBoxCenterAndExtent(SourceQueryBounds.Origin, SourceQueryBounds.BoxExtent),
						[TargetPointData, &CalculateSDF](const PCGPointOctree::FPointRef& InPointRef)
						{
							CalculateSDF(TargetPointData, InPointRef.Index, InPointRef.Bounds);
						});
				}

				ResultCache[Index] = NearestAttribute;
			}

			return Count;
		};

		if (FPCGAsync::AsyncProcessingOneToOneRangeEx(
			&Context->AsyncState,
			SourcePointData->GetNumPoints(),
			/*InitializeFunc=*/[]{},
			ProcessDistanceFunc,
			/*bTimeSliceEnabled=*/false))
		{
			if (OutputAccessor.IsValid())
			{
				if (!OutputAccessor->SetRange<double>(ResultCache, 0, *OutputKeys))
				{
					PCGE_LOG_C(Error, GraphAndLog, Context, LOCTEXT("CopyFailed", "Failed to set attribute on output points"));
					return true;
				}
			}
		}
#else
		auto ProcessDistance = [&TargetPointDatas, MaximumDistance, &Attributes, &OutPoints, &SourcePoints, &ResultCache](int32 ReadIndex, int32 WriteIndex)
		{
			FPCGPoint& OutPoint = OutPoints[WriteIndex];
			const FPCGPoint& SourcePoint = SourcePoints[ReadIndex];

			OutPoint = SourcePoint;

			const FBoxSphereBounds SourceQueryBounds = FBoxSphereBounds(FBox(SourcePoint.BoundsMin - FVector(MaximumDistance), SourcePoint.BoundsMax + FVector(MaximumDistance))).TransformBy(SourcePoint.Transform);

			const FVector SourceCenter = SourcePoint.Transform.TransformPosition(SourcePoint.GetLocalCenter());

			double NearestAttribute = 0.0;
			double MinDistanceSquared = TNumericLimits<double>::Max();

			// Signed distance field for calculating the closest point of source and target
			auto CalculateSDF = [&NearestAttribute, &Attributes, &MinDistanceSquared, &SourcePoint, SourceCenter](const FPCGPointRef& TargetPointRef)
			{
				// If the source pointer and target pointer are the same, ignore distance to the exact same point
				if (&SourcePoint == TargetPointRef.Point)
				{
					return;
				}

				const double ThisDistanceSquared = (TargetPointRef.Bounds.Origin - SourceCenter).SquaredLength();

				if (ThisDistanceSquared < MinDistanceSquared)
				{
					if (Attributes.Contains(TargetPointRef.Point))
					{
						NearestAttribute = Attributes[TargetPointRef.Point];
					}
					MinDistanceSquared = ThisDistanceSquared;
				}
			};

			for (const UPCGPointData* TargetPointData : TargetPointDatas)
			{
				const UPCGPointData::PointOctree& Octree = TargetPointData->GetOctree();

				Octree.FindElementsWithBoundsTest(
						FBoxCenterAndExtent(SourceQueryBounds.Origin, SourceQueryBounds.BoxExtent),
						CalculateSDF);
			}

			ResultCache[WriteIndex] = NearestAttribute;

			return true;
		};

		if (FPCGAsync::AsyncProcessingOneToOneEx(&Context->AsyncState, SourcePoints.Num(), /*InitializeFunc=*/[]{}, ProcessDistance, /*bEnableTimeSlicing=*/false))
		{
			if (OutputAccessor.IsValid())
			{
				if (!OutputAccessor->SetRange<double>(ResultCache, 0, *OutputKeys))
				{
					PCGE_LOG_C(Error, GraphAndLog, Context, LOCTEXT("CopyFailed", "Failed to set attribute on output points"));
					return true;
				}
			}
		}
#endif
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

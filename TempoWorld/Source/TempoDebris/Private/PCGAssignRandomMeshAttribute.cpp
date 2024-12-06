// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "PCGAssignRandomMeshAttribute.h"

#include "PCGComponent.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "PCGModule.h"
#include "PCGNode.h"
#include "Data/PCGSpatialData.h"
#include "Data/PCGPointData.h"
#include "Elements/Metadata/PCGMetadataElementCommon.h"
#include "Elements/PCGActorSelector.h"
#include "Helpers/PCGAsync.h"
#include "Metadata/PCGMetadataTypesConstantStruct.h"
#include "Helpers/PCGDynamicTrackingHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Tests/Determinism/PCGDeterminismTestsCommon.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGAssignRandomMeshAttribute)

#define LOCTEXT_NAMESPACE "PCGAssignRandomMeshAttributeElement"

uint32 GetTypeHash(const FPCGSelectionKey& In)
{
	uint32 HashResult = HashCombine(GetTypeHash(In.ActorFilter), GetTypeHash(In.Selection));
	HashResult = HashCombine(HashResult, GetTypeHash(In.Tag));
	HashResult = HashCombine(HashResult, GetTypeHash(In.SelectionClass));
	HashResult = HashCombine(HashResult, GetTypeHash(In.OptionalExtraDependency));
	HashResult = HashCombine(HashResult, GetTypeHash(In.ObjectPath));

	return HashResult;
}

#if WITH_EDITOR
void UPCGAssignRandomMeshAttributeSettings::GetStaticTrackedKeys(FPCGSelectionKeyToSettingsMap& OutKeysToSettings, TArray<TObjectPtr<const UPCGGraph>>& OutVisitedGraphs) const
{
	if (IsPropertyOverriddenByPin(GET_MEMBER_NAME_CHECKED(UPCGAssignRandomMeshAttributeSettings, DataTable)) || DataTable.IsNull())
	{
		// Dynamic tracking or null settings
		return;
	}

	const FPCGSelectionKey Key = FPCGSelectionKey::CreateFromPath(DataTable.ToSoftObjectPath());

	OutKeysToSettings.FindOrAdd(Key).Emplace(this, /*bCulling=*/false);
}
#endif // WITH_EDITOR

TArray<FPCGPinProperties> UPCGAssignRandomMeshAttributeSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	FPCGPinProperties& InputPinProperty = PinProperties.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Any);
	InputPinProperty.SetRequiredPin();

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGAssignRandomMeshAttributeSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Any);

	return PinProperties;
}

FPCGElementPtr UPCGAssignRandomMeshAttributeSettings::CreateElement() const
{
	return MakeShared<FPCGAssignRandomMeshAttribute>();
}

EPCGDataType UPCGAssignRandomMeshAttributeSettings::GetCurrentPinTypes(const UPCGPin* InPin) const
{
	check(InPin);
	if (!InPin->IsOutputPin())
	{
		return Super::GetCurrentPinTypes(InPin);
	}

	// Output pin narrows to union of inputs on first pin
	const EPCGDataType PrimaryInputType = GetTypeUnionOfIncidentEdges(PCGPinConstants::DefaultInputLabel);
	return (PrimaryInputType != EPCGDataType::None) ? PrimaryInputType : EPCGDataType::Param; // No input (None) means param.
}

FString UPCGAssignRandomMeshAttributeSettings::GetAdditionalTitleInformation() const
{
	return FString::Printf(TEXT("%s[ %s ]"), DataTable ? *DataTable->GetFName().ToString() : TEXT("None"), *RowName.ToString());
}

bool FPCGAssignRandomMeshAttribute::PrepareDataInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGAssignRandomMeshAttribute::PrepareDataInternal);

	check(Context);

	const UPCGAssignRandomMeshAttributeSettings* Settings = Context->GetInputSettings<UPCGAssignRandomMeshAttributeSettings>();
	check(Settings);

	if (Settings->DataTable.IsNull())
	{
		return true;
	}

	FPCGAssignRandomMeshAttributeContext* ThisContext = static_cast<FPCGAssignRandomMeshAttributeContext*>(Context);

	if (!ThisContext->WasLoadRequested())
	{
		return ThisContext->RequestResourceLoad(ThisContext, { Settings->DataTable.ToSoftObjectPath() }, !Settings->bSynchronousLoad);
	}

	return true;
}

bool FPCGAssignRandomMeshAttribute::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGAssignRandomMeshAttribute::Execute);

	check(Context);
	const UPCGAssignRandomMeshAttributeSettings* Settings = Context->GetInputSettings<UPCGAssignRandomMeshAttributeSettings>();

	check(Settings);
	const TSoftObjectPtr<UDataTable> DataTablePtr = Settings->DataTable;
	const FName RowName = Settings->RowName;

	const UDataTable* DataTable = DataTablePtr.Get();
	if (!DataTable)
	{
		PCGE_LOG(Warning, GraphAndLog, LOCTEXT("DataTableInvalid", "FPCGAssignRandomMeshAttribute: Data table is invalid"));
		return true;
	}

	FStaticMeshDistribution* Row = DataTable->FindRow<FStaticMeshDistribution>(RowName, TEXT(""), false);
	if (!Row)
	{
		PCGE_LOG(Error, GraphAndLog, FText::Format(LOCTEXT("NoRowFound", "FPCGAssignRandomMeshAttribute: Data table '{0}' has no row named '{1}'"), FText::FromString(DataTable->GetPathName()), FText::FromName(RowName)));
		return true;
	}

	TMap<TSoftObjectPtr<UStaticMesh>, float> MeshDistribution;
	for (const FStaticMeshProportion& Elem : Row->StaticMeshDistribution)
	{
		MeshDistribution.Add(Elem.StaticMesh, Elem.Proportion);
	}
	TMap<TSoftObjectPtr<UStaticMesh>, float> NormalizedMeshDistribution;
	float TotalProbability = 0.0;
	for (const auto& Elem : MeshDistribution)
	{
		TotalProbability += Elem.Value;
	}
	for (const auto& Elem : MeshDistribution)
	{
		NormalizedMeshDistribution.Add(Elem.Key, Elem.Value / TotalProbability);
	}
	NormalizedMeshDistribution.KeySort([](const TSoftObjectPtr<UStaticMesh>& Left, const TSoftObjectPtr<UStaticMesh>& Right)
	{
		// Arbitrary choice - null is less than not null and if both are null left is less.
		if (!Left.IsValid())
		{
			return true;
		}
		if (!Right.IsValid())
		{
			return false;
		}
		return Left.Get()->GetName() < Right.Get()->GetName();
	});

	FRandomStream RandomStream(Context->SourceComponent.IsValid() ? Context->SourceComponent->Seed : Context->GetSeed());

	const FName MeshAttributeName = Settings->AttributeName;

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputs();
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	for (const FPCGTaggedData& Input : Inputs)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGTransformPointsElement::Execute::InputLoop);
		FPCGTaggedData& Output = Outputs.Add_GetRef(Input);

		const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(Input.Data);

		if (!SpatialData)
		{
			PCGE_LOG(Error, GraphAndLog, LOCTEXT("InputMissingSpatialData", "Unable to get Spatial data from input"));
			continue;
		}

		const UPCGPointData* PointData = SpatialData->ToPointData(Context);

		if (!PointData)
		{
			PCGE_LOG(Error, GraphAndLog, LOCTEXT("InputMissingPointData", "Unable to get Point data from input"));
			continue;
		}

		const TArray<FPCGPoint>& Points = PointData->GetPoints();

		UPCGPointData* OutputData = NewObject<UPCGPointData>();
		OutputData->InitializeFromData(PointData);
		TArray<FPCGPoint>& OutputPoints = OutputData->GetMutablePoints();
		Output.Data = OutputData;

		FPCGMetadataTypesConstantStruct Struct;
		Struct.Type = EPCGMetadataTypes::SoftObjectPath;
		auto CreateAttribute = [Metadata = OutputData->Metadata, MeshAttributeName](auto&& Value) -> FPCGMetadataAttributeBase*
		{
			return PCGMetadataElementCommon::ClearOrCreateAttribute(Metadata, MeshAttributeName, std::forward<decltype(Value)>(Value));
		};
		if (!Struct.Dispatcher(CreateAttribute))
		{
			PCGE_LOG(Error, GraphAndLog, FText::Format(LOCTEXT("ErrorCreatingAttribute", "Error while creating attribute '{0}'"), FText::FromName(MeshAttributeName)));
			return true;
		}

		check(OutputData && OutputData->Metadata);
		FPCGMetadataAttribute<FSoftObjectPath>* TargetAttribute = OutputData->Metadata->GetMutableTypedAttribute<FSoftObjectPath>(MeshAttributeName);
		TArray<TTuple<int64, int64>> AllMetadataEntries;
		AllMetadataEntries.SetNum(Points.Num());

		FPCGAsync::AsyncPointProcessing(Context, Points.Num(), OutputPoints, [&](int32 Index, FPCGPoint& OutPoint)
		{
			const FPCGPoint& InPoint = Points[Index];
			OutPoint = InPoint;
			
			FSoftObjectPath MeshPath;
			const float Draw = RandomStream.FRand();
			float CumProb = 0.0;
			for (const auto& Elem : NormalizedMeshDistribution)
			{
				CumProb += Elem.Value;
				if (CumProb > Draw)
				{
					MeshPath = Elem.Key.ToSoftObjectPath();
					break;
				}
			}

			OutPoint.MetadataEntry = OutputData->Metadata->AddEntryPlaceholder();
			AllMetadataEntries[Index] = MakeTuple(OutPoint.MetadataEntry, InPoint.MetadataEntry);
			TargetAttribute->SetValue(OutPoint.MetadataEntry, MeshPath);

			return true;
		});

		if (TargetAttribute)
		{
			OutputData->Metadata->AddDelayedEntries(AllMetadataEntries);
		}
	}

#if WITH_EDITOR
	// If we have an override, register for dynamic tracking.
	if (Context->IsValueOverriden(GET_MEMBER_NAME_CHECKED(UPCGAssignRandomMeshAttributeSettings, DataTable)))
	{
		FPCGDynamicTrackingHelper::AddSingleDynamicTrackingKey(Context, FPCGSelectionKey::CreateFromPath(DataTable), /*bIsCulled=*/false);
	}
#endif // WITH_EDITOR

	return true;
}

#undef LOCTEXT_NAMESPACE

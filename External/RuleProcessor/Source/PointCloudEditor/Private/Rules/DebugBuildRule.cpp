// Copyright Epic Games, Inc. All Rights Reserved.

#include "DebugBuildRule.h"
#include "Styling/SlateStyle.h"
#include "Brushes/SlateImageBrush.h"
#include "PointCloudAssetHelpers.h"
#include "PointCloudView.h"
#include "Misc/ScopedSlowTask.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"

#define LOCTEXT_NAMESPACE "RuleProcessorSpawnBlueprintRule"

namespace DebugConstants
{
	const FString Name = FString("Debug");
	const FString Description = FString("Debug PointCloud Viewer");
}

FDebugBuildRuleData::FDebugBuildRuleData()
{
	NamePattern = "$IN_VALUE_$RULEPROCESSOR_ASSET";
	RegisterOverrideableProperty("NamePattern");
}
 
FString UDebugBuildRule::Description() const
{
	return DebugConstants::Description;
}

FString UDebugBuildRule::RuleName() const
{
	return DebugConstants::Name;
}

UDebugBuildRule::UDebugBuildRule()
	: UPointCloudRule(&Data)
{
}

FString UDebugBuildRule::MakeName(UPointCloud* Pc, const FString& MetadataValue, const FString& InNamePattern, const FString& InNameValue)
{
	if (Pc == nullptr)
	{
		return FString();
	}

	FString Result = InNamePattern; 

	Result.ReplaceInline(TEXT("$IN_VALUE"), *InNameValue);
	Result.ReplaceInline(TEXT("$RULEPROCESSOR_ASSET"), *Pc->GetName());
	Result.ReplaceInline(TEXT("$MANTLE_ASSET"), *Pc->GetName());
	Result.ReplaceInline(TEXT("$METADATA_VALUE"), *MetadataValue);

	return Result;
}

bool UDebugBuildRule::Compile(FSliceAndDiceContext& Context) const
{
	FPointCloudSliceAndDiceRuleReporter Reporter(this, Context);

	bool Result = false;

	if (CompilationTerminated(Context))
	{
		// If compilation is intentionally terminated then the rule should return success 
		// as it is performing as expected
		return true;
	}

	for (FSliceAndDiceContext::FContextInstance& Instance : Context.Instances)
	{
		Instance.FinalizeInstance(MakeShareable(new FDebugBuildRuleInstance(this)));
	}

	return true;
}

void UDebugBuildRule::ReportParameters(FSliceAndDiceContext& Context) const
{
	UPointCloudRule::ReportParameters(Context);
	Context.ReportObject.AddParameter(TEXT("NamePattern"), Data.NamePattern);
	Context.ReportObject.AddParameter(TEXT("ScaleFactor"), FString::Printf(TEXT("%.4f"), Data.ScaleFactor));

	switch (Data.DebugMesh)
	{
		case EDebugBuildRuleMesh::DebugBuildRuleMesh_Sphere:
			Context.ReportObject.AddParameter(TEXT("DebugMesh"), TEXT("Sphere"));
			break; 
		case EDebugBuildRuleMesh::DebugBuildRuleMesh_Cube:
			Context.ReportObject.AddParameter(TEXT("DebugMesh"), TEXT("Cube"));
			break;
		case EDebugBuildRuleMesh::DebugBuildRuleMesh_Axis:
			Context.ReportObject.AddParameter(TEXT("DebugMesh"), TEXT("Axis"));
			break;
		default:
			Context.ReportObject.AddParameter(TEXT("DebugMesh"), TEXT("Unknown"));
			break;
	}
}

FString FDebugBuildRuleInstance::GetHash()
{
	return GetView()->GetValuesAndTransformsHash({ PointCloudAssetHelpers::GetUnrealAssetMetadataKey() });
}

bool FDebugBuildRuleInstance::Execute()
{	
	if (Data.World == nullptr)
	{
		return false;
	}

	TArray<FTransform> Transforms = GetView()->GetTransforms();

	if (Transforms.IsEmpty())
	{
		return false;
	}

	const FBox Bounds = GetView()->GetResultsBoundingBox();
	const FVector CenterPivot = Bounds.GetCenter();

	FScopedSlowTask SlowTask(Transforms.Num(), LOCTEXT("CreatingDebugInstances", "Creating Debug Instances"));
	SlowTask.MakeDialog();

	bool Result = false;

	// Make Debug Actor
	if (UEditorActorSubsystem* EditorActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>())
	{
		if (AActor* DebugActor = EditorActorSubsystem->SpawnActorFromClass(AActor::StaticClass(), FVector::ZeroVector))
		{
			DebugActor->SetActorLocation(CenterPivot);
			DebugActor->GetRootComponent()->SetMobility(EComponentMobility::Static);
			DebugActor->SetActorLabel(FString::Printf(TEXT("DEBUG_%s"), *PointCloud->GetName()));

			// Make ISM Component
			if (UInstancedStaticMeshComponent* ISMComponent = NewObject<UInstancedStaticMeshComponent>(DebugActor, UInstancedStaticMeshComponent::StaticClass(), FName(TEXT("DebugInstanceComponent")), RF_Transactional))
			{
				// Set DebugMesh
				UStaticMesh* DebugStaticMesh = nullptr;
				UObject* LoadedObject = nullptr;
				switch (Data.DebugMesh)
				{
				case EDebugBuildRuleMesh::DebugBuildRuleMesh_Sphere:
					LoadedObject = FSoftObjectPath(FString(TEXT("/Engine/BasicShapes/Sphere.Sphere"))).TryLoad();
					break;
				case EDebugBuildRuleMesh::DebugBuildRuleMesh_Cube:
					LoadedObject = FSoftObjectPath(FString(TEXT("/Engine/BasicShapes/Cube.Cube"))).TryLoad();
					break;
				case EDebugBuildRuleMesh::DebugBuildRuleMesh_Axis:
					LoadedObject = FSoftObjectPath(FString(TEXT("/Engine/EditorMeshes/Axis_Guide.Axis_Guide"))).TryLoad();
					break;
				default:
					LoadedObject = FSoftObjectPath(FString(TEXT("/Engine/BasicShapes/Sphere.Sphere"))).TryLoad();
					break;
				}

				if (LoadedObject)
				{
					DebugStaticMesh = Cast<UStaticMesh>(LoadedObject);
					ISMComponent->SetStaticMesh(DebugStaticMesh);
				}

				ISMComponent->SetMobility(EComponentMobility::Static);
				DebugActor->AddInstanceComponent(ISMComponent);
				DebugActor->FinishAddComponent(ISMComponent, false, FTransform::Identity);
				DebugActor->RerunConstructionScripts();

				// Add Instances
				TArray<FTransform> PointTransforms;
				for (int32 i = 0; i < Transforms.Num(); i++)
				{
					SlowTask.EnterProgressFrame();
					FTransform PointTransform = Transforms[i];
					PointTransform.SetLocation(PointTransform.GetLocation());
					// Multiply Scale by ScaleFactor
					PointTransform.SetScale3D(PointTransform.GetScale3D() * Data.ScaleFactor);
					PointTransforms.Add(PointTransform);
				}
				ISMComponent->AddInstances(PointTransforms, /*bShouldReturnIndices*/false, /*bWorldSpace*/true);

				NewActorAdded(DebugActor, GetView());
				Result = true;
			}
		}
	}

	// save the stats if we're in the right reporting mode
	if (GenerateReporting())
	{
		// record the statistics for the given view
		int32 ResultCount = GetView()->GetCount();
		ReportFrame->PushParameter(TEXT("Number Of Debug Instances"), FString::FromInt(ResultCount));		
	}

	return Result;
}

FString FDebugBuildFactory::Name() const
{
	return DebugConstants::Name;
}

FString FDebugBuildFactory::Description() const
{
	return DebugConstants::Description;
}

UPointCloudRule* FDebugBuildFactory::Create(UObject *Parent)
{
	UPointCloudRule* Result = NewObject<UDebugBuildRule>(Parent);	
	return Result;
}

FDebugBuildFactory::FDebugBuildFactory(TSharedPtr<ISlateStyle> Style)
{
	FSlateStyleSet* AsStyleSet = static_cast<FSlateStyleSet*>(Style.Get());
	if (AsStyleSet)
	{
		Icon = new FSlateImageBrush(AsStyleSet->RootToContentDir(TEXT("Resources/SingleObjectRule"), TEXT(".png")), FVector2D(128.f, 128.f));
		AsStyleSet->Set("RuleThumbnail.SingleObjectRule", Icon);
	}
	else
	{
		Icon = nullptr;
	}
}

FDebugBuildFactory::~FDebugBuildFactory()
{
	// Note: do not delete the icon as it is owned by the editor style
}

FSlateBrush* FDebugBuildFactory::GetIcon() const
{ 
	return Icon; 
}

#undef LOCTEXT_NAMESPACE
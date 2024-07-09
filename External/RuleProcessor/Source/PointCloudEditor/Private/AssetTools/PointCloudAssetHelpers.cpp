// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudAssetHelpers.h"
#include "PointCloudImpl.h"
#include "PointCloudEditorSettings.h"
#include "PointCloudView.h"
#include "PointCloudSliceAndDiceExecutionContext.h"
#include "PointCloudSliceAndDiceManager.h"
#include "AssetToolsModule.h"
#include "PointCloudFactory.h"
#include "EngineUtils.h"
#include "ContentBrowserModule.h"
#include "Misc/ScopedSlowTask.h"
#include "Math/NumericLimits.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet2/ComponentEditorUtils.h"
#include "IContentBrowserSingleton.h"
#include "IDesktopPlatform.h"
#include "DesktopPlatformModule.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Editor.h"
#include "Components/SceneComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "ObjectTools.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Algo/AnyOf.h"
#include "PackedLevelActor/PackedLevelActor.h"
#include "AssetRegistry/AssetData.h"
#include "Misc/FileHelper.h"
#include "Framework/Application/SlateApplication.h"

THIRD_PARTY_INCLUDES_START
#pragma warning(push)
#pragma warning(disable:4005) // TEXT macro redefinition
#include <Alembic/AbcCoreOgawa/All.h>
#include "Alembic/AbcGeom/All.h"
#include "Alembic/AbcCoreFactory/IFactory.h"
#pragma warning(pop)
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "PointCloudHelpers"

namespace PointCloudAssetHelpers
{
	void OpenFileDialog(const FString& DialogTitle, const FString& DefaultPath, const FString& FileTypes, TArray<FString>& OutFileNames) {
		void* ParentWindowPtr = FSlateApplication::Get().GetActiveTopLevelWindow()->GetNativeWindow()->GetOSWindowHandle();
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		if (DesktopPlatform)
		{
			uint32 SelectionFlag = 1; //A value of 0 represents single file selection while a value of 1 represents multiple file selection
			DesktopPlatform->OpenFileDialog(ParentWindowPtr, DialogTitle, DefaultPath, FString(""), FileTypes, SelectionFlag, OutFileNames);
		}
	}

	void SaveFileDialog(const FString& DialogTitle, const FString& DefaultPath, const FString& FileTypes, TArray<FString>& OutFileNames) {
		void* ParentWindowPtr = FSlateApplication::Get().GetActiveTopLevelWindow()->GetNativeWindow()->GetOSWindowHandle();
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		if (DesktopPlatform)
		{
			uint32 SelectionFlag = 1; //A value of 0 represents single file selection while a value of 1 represents multiple file selection
			DesktopPlatform->SaveFileDialog(ParentWindowPtr, DialogTitle, DefaultPath, FString(""), FileTypes, SelectionFlag, OutFileNames);
		}
	}

	template<class T> T* GetComponentFromActorAndRef(AActor* FromMe, FComponentReference& Ref)
	{
		const auto& Components = FromMe->GetInstanceComponents();

		for (auto A : Components)
		{			
			if (A->GetName() == Ref.PathToComponent)
				return Cast< T>(A);
		}

		return nullptr;
	}
		
	FString GetUnrealAssetMetadataKey()
	{
		return GetDefault<UPointCloudEditorSettings>()->DefaultMetadataKey;
	}
}

TArray<FString> FSpawnAndInitMaterialOverrideParameters::GetMetadataKeys() const
{
	TArray<FString> Keys;

	for (const auto& KeyToIndex : MetadataKeyToIndex)
	{
		Keys.Add(KeyToIndex.Key);
	}

	for (const auto& KeyToTemplate : MetadataKeyToTemplate)
	{
		Keys.Add(KeyToTemplate.Key);
	}

	for (const auto& KeyToSlotName : MetadataKeyToSlotName)
	{
		Keys.Add(KeyToSlotName.Key);
	}

	return Keys;
}

void FSpawnAndInitMaterialOverrideParameters::CopyValid(const FSpawnAndInitMaterialOverrideParameters& InMaterialOverrides, UPointCloud* PointCloud)
{
	check(PointCloud);
	MetadataKeyToIndex.Reset();
	MetadataKeyToTemplate.Reset();
	MetadataKeyToSlotName.Reset();

	for (const auto& Item : InMaterialOverrides.MetadataKeyToIndex)
	{
		if (PointCloud->HasMetaDataAttribute(Item.Key))
		{
			MetadataKeyToIndex.Add(Item.Key, Item.Value);
		}
		else
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Material override key %s does not exist in point cloud"), *Item.Key);
		}
	}

	for (const auto& Item : InMaterialOverrides.MetadataKeyToTemplate)
	{
		if (PointCloud->HasMetaDataAttribute(Item.Key))
		{
			MetadataKeyToTemplate.Add(Item.Key, Item.Value);
		}
		else
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Material override key %s does not exist in point cloud"), *Item.Key);
		}
	}

	for (const auto& Item : InMaterialOverrides.MetadataKeyToSlotName)
	{
		if (PointCloud->HasMetaDataAttribute(Item.Key))
		{
			MetadataKeyToSlotName.Add(Item.Key, Item.Value);
		}
		else
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Material override key %s does not exist in point cloud"), *Item.Key);
		}
	}
}

void FSpawnAndInitActorParameters::SetNameGetter(FSliceAndDiceExecutionContext* Context, FPointCloudRuleInstance* Rule)
{
	check(Context && Rule);
	SetNameGetter([Context, Rule, this]() {
		return World == Context->GetWorld() ? Context->GetActorName(Rule) : NAME_None;
		});
}

TArray<FName> UPointCloudAssetsHelpers::GetSelectedRuleProcessorItemsFromContentBrowser()
{
	TArray<FAssetData> AssetDatas;
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	IContentBrowserSingleton& ContentBrowserSingleton = ContentBrowserModule.Get();
	ContentBrowserSingleton.GetSelectedAssets(AssetDatas);

	TArray<FName> Result;

	UClass* PointCloudClass = UPointCloud::StaticClass();
	UClass* PointCloudImplClass = UPointCloudImpl::StaticClass();

	if (!PointCloudClass || !PointCloudImplClass)
	{
		return Result;
	}

	for (auto a : AssetDatas)
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		if(a.AssetClassPath == PointCloudClass->GetClassPathName() || a.AssetClassPath == PointCloudImplClass->GetClassPathName())
		{
			Result.Add(a.ObjectPath);
		}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	return Result;
}

namespace PointCloudAssetHelpers
{
	bool UpdateStaticMeshComponent(UStaticMeshComponent* Component, UPointCloudView* View, const FPointCloudManagedActorData& AsManaged)
	{
		if (!Component || !View || !AsManaged.Actor)
		{
			return false;
		}

		TArray<FTransform> Transforms = View->GetTransforms();

		if (Transforms.Num() == 1)
		{
			const FTransform InverseActorTransform = AsManaged.Actor->GetTransform().Inverse();

			Component->SetWorldTransform(Transforms[0] * InverseActorTransform);
			// If we save the asset in the same call hierarchy before an engine tick
			// the bounds won't have been updated, so we must do it here.
			Component->UpdateBounds();
			return true;
		}
		else
		{
			return false;
		}
	}

	bool UpdateIsmComponent(UInstancedStaticMeshComponent* Component, UPointCloudView *View, const FPointCloudManagedActorData& AsManaged)
	{
		if (!Component || !View || !AsManaged.Actor)
		{
			return false;
		}

		TArray<FTransform> Transforms = View->GetTransforms();
		// Currently we'll promote values that might be integers to floats, which might lead to data loss,
		// because we're pushing that to the custom data in any case.
		// If there are some instances where we'd want to copy integers as "float bits" we would need to do a few changes here
		TArray<float> PerModuleAttributes;

		const FTransform InverseActorTransform = AsManaged.Actor->GetTransform().Inverse();
		if (!InverseActorTransform.Equals(FTransform::Identity))
		{
			for (FTransform& Transform : Transforms)
			{
				Transform *= InverseActorTransform;
			}
		}

		if (AsManaged.ModuleAttributeKey.IsEmpty() == false)
		{
			// the user has requested a column be added to the modules as per instance attributes.
			// get the values for that column if it exists
			PerModuleAttributes = View->GetMetadataValuesArrayAsFloat(AsManaged.ModuleAttributeKey);
		}

		if (Transforms.Num())
		{
			Component->AddInstances(Transforms, false);
			// If we save the asset in the same call hierarchy before an engine tick
			// the bounds won't have been updated, so we must do it here.
			Component->UpdateBounds();
		}

		if (PerModuleAttributes.Num() == Transforms.Num())
		{
			Component->NumCustomDataFloats = 1;

			// "Write" one float per instance
			for (int32 InstanceIndex = 0; InstanceIndex < PerModuleAttributes.Num(); ++InstanceIndex)
			{
				Component->SetCustomData(InstanceIndex, { PerModuleAttributes[InstanceIndex] });
			}
		}

		return true;
	}
}

void UPointCloudAssetsHelpers::UpdateAllManagedActorInstances(const TMap<FString, FPointCloudManagedActorData>& ActorsToUpdate)
{
	if (ActorsToUpdate.Num() == 0)
	{
		return; 
	}
		
	FScopedSlowTask Task(ActorsToUpdate.Num(), LOCTEXT("BuildingActors", "Initializing Actors and Components"));
	Task.MakeDialogDelayed(0.1f);

	// Uncomment this to enable collection and reporting of cache hit stats
	//#define RULEPROCESSOR_CACHE_STATS

	TMap<FString, int>* CacheHitPtr = nullptr;

#if defined RULEPROCESSOR_CACHE_STATS
	TMap<FString, int> CacheHitCount;
	CacheHitPtr = &CacheHitCount;
#endif 

	int Count = 0; 

	// for each actor 
	for(const auto& ManagedActorData : ActorsToUpdate)
	{
		Task.EnterProgressFrame();
		UpdateManagedActorInstance(ManagedActorData.Value, CacheHitPtr);
	}
		
#if defined RULEPROCESSOR_CACHE_STATS
	int CacheHitCountTotal = 0;
	for (const auto& a : CacheHitCount)
	{
		//UE_LOG(PointCloudLog, Log, TEXT("%s = %d"), *a.Key, a.Value);
		CacheHitCountTotal += a.Value;
	}

	UE_LOG(PointCloudLog, Log, TEXT("******** TOTAL CACHE HITS %d *********"), CacheHitCountTotal);	
#endif 
}

void UPointCloudAssetsHelpers::UpdateManagedActorInstance(const FPointCloudManagedActorData& ManagedActorData, TMap<FString, int>* CacheHitCount)
{
	if (!ManagedActorData.Actor)
	{
		return;
	}

	// Get a handle to the instancers and Views
	for (const FPointCloudComponentData& ComponentData : ManagedActorData.ComponentsData)
	{
		UPointCloudView* View = ComponentData.View;

#if defined RULEPROCESSOR_CACHE_STATS
		if (CacheHitCount)
		{
			TArray<FString> Statements = View->GetFilterStatement();

			for (const FString& FilterStatement : Statements)
			{
				if (CacheHitCount->Contains(FilterStatement))
				{
					(*CacheHitCount)[FilterStatement]++;
				}
				else
				{
					CacheHitCount->Add(FilterStatement, 1);
				}
			}
		}
#endif

		FComponentReference ComponentRef = ComponentData.Component;

		if (View)
		{
			if (UInstancedStaticMeshComponent* AsIsmComponent = PointCloudAssetHelpers::GetComponentFromActorAndRef<UInstancedStaticMeshComponent>(ManagedActorData.Actor, ComponentRef))
			{
				PointCloudAssetHelpers::UpdateIsmComponent(AsIsmComponent, View, ManagedActorData);
			}
			else if (UStaticMeshComponent* AsStaticMeshComponent = PointCloudAssetHelpers::GetComponentFromActorAndRef<UStaticMeshComponent>(ManagedActorData.Actor, ComponentRef))
			{
				PointCloudAssetHelpers::UpdateStaticMeshComponent(AsStaticMeshComponent, View, ManagedActorData);
			}
		}
	}
}

TArray<UPointCloud*> UPointCloudAssetsHelpers::LoadPointCloud(const EPointCloudFileType InFileType)
{
	TArray<FString> OutFileNames;
	TArray<UPointCloud*> Result;

	switch (InFileType)
	{
	case EPointCloudFileType::Csv:
		{
			PointCloudAssetHelpers::OpenFileDialog(TEXT("Load PSV File"), TEXT(""), TEXT("psv"), OutFileNames);
		}
		break;
	case EPointCloudFileType::Alembic:
		{
			PointCloudAssetHelpers::OpenFileDialog(TEXT("Load PBC File"), TEXT(""), TEXT("pbc"), OutFileNames);
		}
		break;
	}
	
	if (OutFileNames.Num() == 0)
	{
		return Result;
	}

	UPointCloudFactory* NewFactory = NewObject<UPointCloudFactory>();

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	IContentBrowserSingleton& ContentBrowserSingleton = ContentBrowserModule.Get();
	FString Path = ContentBrowserSingleton.GetCurrentPath().GetInternalPathString();

	FAssetToolsModule& AssetToolsModule = FAssetToolsModule::GetModule();

	TArray<UObject*> ObjectsToSync;
	for (FString a : OutFileNames)
	{
		FString AssetName = FPaths::GetBaseFilename(a);

		UObject* NewAsset = AssetToolsModule.Get().CreateAssetWithDialog(AssetName, Path, NewFactory->GetSupportedClass(), NewFactory);
		if (NewAsset != nullptr)
		{
			ObjectsToSync.Add(NewAsset);
			UPointCloud* AsPointCloud = Cast<UPointCloud>(NewAsset);

			if (AsPointCloud)
			{
				bool SuccessfulLoad = false;
				switch (InFileType)
				{
				case EPointCloudFileType::Csv:
					{
						SuccessfulLoad = AsPointCloud->LoadFromCsv(a);
					}
					break;
				case EPointCloudFileType::Alembic:
					{
						SuccessfulLoad = AsPointCloud->LoadFromAlembic(a);
					}
					break;
				}

				if (SuccessfulLoad)
				{
					Result.Add(AsPointCloud);
				}
			}
		}
	}

	GEditor->SyncBrowserToObjects(ObjectsToSync);
	NewFactory->MarkAsGarbage();
	
	return Result;
}

TArray<UPointCloud*> UPointCloudAssetsHelpers::LoadPointCloudCSV()
{
	return LoadPointCloud(EPointCloudFileType::Csv);
}

TArray<UPointCloud*> UPointCloudAssetsHelpers::LoadPointCloudAlembic()
{
	return LoadPointCloud(EPointCloudFileType::Alembic);
}

void UPointCloudAssetsHelpers::InitActorComponents(FPointCloudManagedActorData& ManagedActor, int32 GroupId, TMap<FString, UStaticMesh*>* MeshCache, const FSpawnAndInitActorParameters& Params)
{
	check(ManagedActor.Actor);
	AActor* Actor = ManagedActor.Actor;

	TArray<FPointCloudComponentData>& ComponentsData = ManagedActor.ComponentsData;

	bool bSetModuleAsRootComponent = false;
	if (!Actor->GetRootComponent())
	{
		if (ComponentsData.Num() <= 1 && Params.PivotType == EPointCloudPivotType::Default)
		{
			bSetModuleAsRootComponent = true;
		}
		else
		{
			USceneComponent* RootComponent = NewObject<USceneComponent>(Actor, USceneComponent::GetDefaultSceneRootVariableName(), RF_Transactional);
			RootComponent->Mobility = EComponentMobility::Static;
			Actor->SetRootComponent(RootComponent);
			Actor->AddInstanceComponent(RootComponent);
			RootComponent->RegisterComponent();
		}
	}

	auto LoadObjectFromPath = [](const FString* ObjectToLoad) -> UObject*
	{
		if (!ObjectToLoad || ObjectToLoad->IsEmpty())
		{
			return nullptr;
		}
		else
		{
			FSoftObjectPath ObjectPath(*ObjectToLoad);
			UObject* Object = ObjectPath.TryLoad();

			if (!Object)
			{
				UE_LOG(PointCloudLog, Warning, TEXT("Cannot load object %s"), **ObjectToLoad);
			}

			return Object;
		}
	};

	int ComponentCount = 0;

	for (auto& ComponentData : ComponentsData)
	{
		const FString* ModuleNamePtr = ComponentData.MetadataValues.Find(Params.MeshKey);
		if (!ModuleNamePtr)
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Component data does not have the required module metadata key %s"), *Params.MeshKey);
			continue;
		}

		const FString& ModuleName = *ModuleNamePtr;
		int Count = ComponentData.Count;

		UStaticMesh* AsStaticMesh = nullptr;

		if (MeshCache && MeshCache->Contains(ModuleName))
		{
			AsStaticMesh = *MeshCache->Find(ModuleName);
		}
		else
		{
			FSoftObjectPath Mesh(ModuleName);
			UObject* MyAsset = Mesh.TryLoad();
			if (!MyAsset)
			{
				UE_LOG(PointCloudLog, Warning, TEXT("Cannot load Object %s"), *ModuleName);
			}
			else
			{
				AsStaticMesh = Cast<UStaticMesh>(MyAsset);

				if (MeshCache)
				{
					MeshCache->Add(ModuleName, AsStaticMesh);
				}
			}
		}

		if (Params.OverrideMap.Contains(AsStaticMesh))
		{
			AsStaticMesh = Params.OverrideMap[AsStaticMesh];
		}

		const FString SanitizedModuleName = FString::Printf(TEXT("%s_%d"), *ObjectTools::SanitizeObjectName(ModuleName), ComponentCount);

		UStaticMeshComponent* PerModuleComponent = nullptr;

		if (AsStaticMesh && (Params.bSingleInstanceAsStaticMesh == false || Count > 1))
		{
			if (Params.bUseHierarchicalInstancedStaticMeshComponent)
			{
				PerModuleComponent = NewObject<UHierarchicalInstancedStaticMeshComponent>(Actor, FName(*SanitizedModuleName), RF_Transactional, Params.TemplateHISM);

				if (Params.StatsObject)
				{
					Params.StatsObject->IncrementCounter(TEXT("HISM"));
				}
			}
			else
			{
				PerModuleComponent = NewObject<UInstancedStaticMeshComponent>(Actor, FName(*SanitizedModuleName), RF_Transactional, Params.TemplateIsm);

				if (Params.StatsObject)
				{
					Params.StatsObject->IncrementCounter(TEXT("ISM"));
				}
			}
		}
		else if (AsStaticMesh && Params.bSingleInstanceAsStaticMesh == true && Count == 1 && Params.TemplateStaticMeshComponent)
		{
			PerModuleComponent = NewObject<UStaticMeshComponent>(Actor, FName(*SanitizedModuleName), RF_Transactional, Params.TemplateStaticMeshComponent);

			if (Params.StatsObject)
			{
				Params.StatsObject->IncrementCounter(TEXT("Static Mesh Component"));
			}
		}
		
		if (PerModuleComponent != nullptr)
		{
			PerModuleComponent->SetStaticMesh(AsStaticMesh);
			PerModuleComponent->RayTracingGroupId = Params.bManualGroupId ? Params.GroupId : GroupId;

			PerModuleComponent->RegisterComponent();
			Actor->AddInstanceComponent(PerModuleComponent);

			if (bSetModuleAsRootComponent)
			{
				PerModuleComponent->SetMobility(EComponentMobility::Static);
				Actor->SetRootComponent(PerModuleComponent);
			}
			else
			{
				PerModuleComponent->SetMobility(Actor->GetRootComponent()->Mobility);
				PerModuleComponent->SetComponentToWorld(Actor->GetActorTransform());
				PerModuleComponent->AttachToComponent(Actor->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
			}

			// Finally, override material if needed
			for (const auto& IndexedMaterialOverride : Params.MaterialOverrides.MetadataKeyToIndex)
			{
				const FString& MaterialKey = IndexedMaterialOverride.Key;
				int32 MaterialIndex = IndexedMaterialOverride.Value;

				if (MaterialIndex < 0 || PerModuleComponent->GetNumMaterials() <= MaterialIndex)
				{
					continue;
				}

				UMaterialInterface* Material = Cast<UMaterialInterface>(LoadObjectFromPath(ComponentData.MetadataValues.Find(MaterialKey)));
				if (Material)
				{
					PerModuleComponent->SetMaterial(MaterialIndex, Material);
				}
			}

			for (const auto& TemplateMaterialOverride : Params.MaterialOverrides.MetadataKeyToTemplate)
			{
				const FString& MaterialKey = TemplateMaterialOverride.Key;
				const FString& TemplateMaterialName = TemplateMaterialOverride.Value;

				const TArray<UMaterialInterface*> ModuleMaterials = PerModuleComponent->GetMaterials();
				int32 TemplateIndex = ModuleMaterials.IndexOfByPredicate([&TemplateMaterialName](const UMaterialInterface* Material) { return Material && Material->GetName() == TemplateMaterialName; });

				if (TemplateIndex < 0 || PerModuleComponent->GetNumMaterials() <= TemplateIndex)
				{
					continue;
				}

				UMaterialInterface* Material = Cast<UMaterialInterface>(LoadObjectFromPath(ComponentData.MetadataValues.Find(MaterialKey)));
				if (Material)
				{
					PerModuleComponent->SetMaterial(TemplateIndex, Material);
				}
			}

			for (const auto& SlotMaterialOverride : Params.MaterialOverrides.MetadataKeyToSlotName)
			{
				const FString& MaterialKey = SlotMaterialOverride.Key;
				const FString& SlotName = SlotMaterialOverride.Value;

				int32 SlotIndex = PerModuleComponent->GetMaterialIndex(*SlotName);

				if (SlotIndex < 0)
				{
					continue;
				}

				UMaterialInterface* Material = Cast<UMaterialInterface>(LoadObjectFromPath(ComponentData.MetadataValues.Find(MaterialKey)));
				if (Material)
				{
					PerModuleComponent->SetMaterial(SlotIndex, Material);
				}
			}

			ComponentData.Component = FComponentEditorUtils::MakeComponentReference(Actor, PerModuleComponent);
		}

		++ComponentCount;
	}
}

void UPointCloudAssetsHelpers::InitActorComponentData(FPointCloudManagedActorData& ManagedActor)
{
	check(ManagedActor.Actor);
	check(ManagedActor.GroupOnMetadataKeys.Num() > 0);
	check(ManagedActor.ActorView);
	
	// Perform query to find unique metadata value associations, and their count
	TArray<TPair<TArray<FString>, int32>> UniqueMetadataValues = ManagedActor.ActorView->GetUniqueMetadataValuesAndCounts(ManagedActor.GroupOnMetadataKeys);

	for (const auto& MetadataValueSet : UniqueMetadataValues)
	{
		FPointCloudComponentData& ComponentData = ManagedActor.ComponentsData.Emplace_GetRef();
			
		for (int32 MetadataValueIndex = 0; MetadataValueIndex < MetadataValueSet.Key.Num(); ++MetadataValueIndex)
		{
			ComponentData.MetadataValues.Add(ManagedActor.GroupOnMetadataKeys[MetadataValueIndex], MetadataValueSet.Key[MetadataValueIndex]);
		}

		ComponentData.Count = MetadataValueSet.Value;

		ComponentData.View = ManagedActor.ActorView->MakeChildView();
		for (const auto& MetadataValue : ComponentData.MetadataValues)
		{
			ComponentData.View->FilterOnMetadata(MetadataValue.Key, MetadataValue.Value, EFilterMode::FILTER_And);
		}

		ComponentData.View->Rename(nullptr, ManagedActor.Actor);
	}
}

AActor* UPointCloudAssetsHelpers::GetManagedActor(const FString& Label, const FSpawnAndInitActorParameters& Params)
{
	AActor* Result = nullptr;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = nullptr;
	SpawnParams.Template = Params.TemplateActor;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Name = Params.GetName();

	Result = Params.World->SpawnActor(AActor::StaticClass(), &FVector::ZeroVector, &FRotator::ZeroRotator, SpawnParams);

	if (Params.StatsObject)
	{
		Params.StatsObject->IncrementCounter(TEXT("Actors"));
	}

	if (Result != nullptr)
	{
		if (Params.FolderPath.IsNone() == false)
		{
			Result->SetFolderPath(Params.FolderPath);
		}

		Result->SetActorLabel(Label);
	}

	return Result;
 }

 int32  UPointCloudAssetsHelpers::CalculateGroupId(UPointCloudView* PointCloudView, const FString& MetadataKey, const FString& MetadataValue)
 {
	 const FString HashString = FString::Printf(TEXT("%s_%s_%s"), *PointCloudView->GetPointCloud()->GetName(), *MetadataKey, *MetadataValue);
	 // Return the hash mapped to the 0...TNumericLimits< int32 >::Max space.  -1 is reserved by the RayTracingGroupId system as meaning "no group"
	 return GetTypeHash(HashString) % TNumericLimits< int32 >::Max();	
 }

AActor* UPointCloudAssetsHelpers::CreateActorFromView(	UPointCloudView* PointCloudView, 
														const FString &Label, 																
														const FSpawnAndInitActorParameters& Params)
{	
	if (PointCloudView == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Point cloud view is null. Please provide a valid point cloud view for CreateActorFromView"));
		return nullptr;
	}

	if (Params.World==nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Null world passed to CreateActorFromView"));
		return nullptr;
	}							
				
	AActor* NewActor = GetManagedActor(Label, Params);

	if(NewActor)
	{
		FPointCloudManagedActorData AsManaged;
		AsManaged.Actor = NewActor;
		AsManaged.OriginatingView = PointCloudView;
		AsManaged.ModuleAttributeKey = Params.PerModuleAttributeKey;
		AsManaged.ActorView = PointCloudView->MakeChildView();
		AsManaged.GroupOnMetadataKeys.Add(Params.MeshKey);

		// Add material overrides as component separators
		const TArray<FString> MaterialOverrideMetadataKeys = Params.MaterialOverrides.GetMetadataKeys();

		for (const FString& MaterialOverride : MaterialOverrideMetadataKeys)
		{
			AsManaged.GroupOnMetadataKeys.AddUnique(MaterialOverride);
		}

		int32 GroupId = CalculateGroupId(PointCloudView, Label, FString(TEXT("Single Actor")));

		InitActorComponentData(AsManaged);
		InitActorComponents(AsManaged, GroupId, nullptr, Params);
		UpdateManagedActorInstance(AsManaged);
		SetActorPivots({ NewActor }, Params.PivotType);
	}
	else
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Cannot Spawn Managed Actor Instance"));
	}
	
	return NewActor;
}

TMap<FString, FString> UPointCloudAssetsHelpers::MakeNamesFromMetadataValues(UPointCloudView* PointCloudView, const FString& MetadataKey, const FString& NameTemplate)
 {
	TMap<FString, FString> Result;

	if (MetadataKey.IsEmpty())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Empty MetadataKey. Please provide a valid MetadataKey for MakeNamesFromMetadataValues"));
		return Result;
	}

	if (NameTemplate.IsEmpty())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Empty NameTemplate. Please provide a valid Template for MakeNamesFromMetadataValues"));
		return Result;
	}

	if (PointCloudView == nullptr || PointCloudView->GetPointCloud() == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Point cloud view is null. Please provide a valid view for MakeNamesFromMetadataValues"));
		return Result;
	}

	if (PointCloudView->GetPointCloud()->HasMetaDataAttribute(MetadataKey) == false)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("%s is not a MetadataKey in the given PointCloud (%s)."), *MetadataKey, *PointCloudView->GetPointCloud()->GetName());
		return Result;
	}

	// Make a view to get the unique values
	TMap<FString,int> UniqueValues = PointCloudView->GetUniqueMetadataValuesAndCounts(MetadataKey);

	if (UniqueValues.Num() == 0)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("No Values found for Key %s"), *MetadataKey);
		return Result;
	}
		
	for (const auto& Value : UniqueValues)
	{
		const FString& ValueString = Value.Key;
		const int32 ValueCount = Value.Value;

		FString Name = NameTemplate;

		Name.ReplaceInline(TEXT("$RULEPROCESSOR_ASSET"), *PointCloudView->GetPointCloud()->GetName());
		Name.ReplaceInline(TEXT("$MANTLE_ASSET"), *PointCloudView->GetPointCloud()->GetName());
		Name.ReplaceInline(TEXT("$METADATAKEY"), *MetadataKey);
		Name.ReplaceInline(TEXT("$METADATAVALUE"), *ValueString);
		
		Result.Add(ValueString, Name);
	}

	return Result;
 }

TMap<FString, FPointCloudManagedActorData>  UPointCloudAssetsHelpers::BulkCreateManagedActorsFromView(UPointCloudView* PointCloudView, const FString& MetadataKey, const TMap<FString, FString>& ValuesAndLabels, const FSpawnAndInitActorParameters& Params)
{
	TMap<FString, FPointCloudManagedActorData> ManagedActors;
	
	if (PointCloudView == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Point cloud view is null. Please provide a valid view to BuildCreateManagedActorsFromView"));
		return ManagedActors;
	}
		
	UWorld* World = GEditor->GetEditorWorldContext().World();
	
	FScopedSlowTask SlowTask(ValuesAndLabels.Num(), LOCTEXT("GeneratingDataText", "Creating Rule Processor Managed Actors"));
	SlowTask.MakeDialogDelayed(0.1f);

	TMap<FString, UStaticMesh*> MeshCache;

	int Count = 0;
	int Max = ValuesAndLabels.Num();

	for (auto ValueAndLabel : ValuesAndLabels)
	{
		FString Value = ValueAndLabel.Key;
		FString Label = ValueAndLabel.Value;
		SlowTask.EnterProgressFrame();

		AActor* NewActor = GetManagedActor(Label, Params);

		if (NewActor)
		{
			FPointCloudManagedActorData& ManagedActor = ManagedActors.Emplace(Value);

			// set the booking data on the new actor
			ManagedActor.Actor = NewActor;
			ManagedActor.OriginatingView = PointCloudView;
			ManagedActor.ModuleAttributeKey = Params.PerModuleAttributeKey;
			ManagedActor.ActorView = PointCloudView->MakeChildView();
			ManagedActor.ActorView->FilterOnMetadata(MetadataKey, Value);
			ManagedActor.GroupOnMetadataKeys.Add(Params.MeshKey);

			// Add material overrides as component separators
			const TArray<FString> MaterialOverrideMetadataKeys = Params.MaterialOverrides.GetMetadataKeys();

			for (const FString& MaterialOverride : MaterialOverrideMetadataKeys)
			{
				ManagedActor.GroupOnMetadataKeys.AddUnique(MaterialOverride);
			}

			InitActorComponentData(ManagedActor);

			int32 GroupId = CalculateGroupId(PointCloudView, MetadataKey, Value);
			InitActorComponents(ManagedActor, GroupId, &MeshCache, Params);

			if (!Params.PivotKey.IsEmpty() && !Params.PivotValue.IsEmpty())
			{
				if (UPointCloudView* ChildView = PointCloudView->MakeChildView())
				{
					ChildView->FilterOnMetadata(MetadataKey, Value);
					ChildView->FilterOnMetadata(Params.PivotKey, Params.PivotValue);

					TArray<FTransform> Transforms = ChildView->GetTransforms();

					if (Transforms.Num() > 0)
					{
						FTransform PivotTransform = Transforms[0];
						PivotTransform.RemoveScaling();
						NewActor->SetActorTransform(PivotTransform);
					}
				}
			}
		}
	}

	if(!ManagedActors.IsEmpty())
	{
		UpdateAllManagedActorInstances(ManagedActors);

		TArray<AActor*> NewActors;
		NewActors.Reserve(ManagedActors.Num());
		for (const auto& ManagedActor : ManagedActors)
		{
			NewActors.Add(ManagedActor.Value.Actor);
		}

		SetActorPivots(NewActors, Params.PivotType);
	}

	return ManagedActors;
}

UPointCloud* UPointCloudAssetsHelpers::LoadPointCloudAssetFromPath(const FString& Path)
{
	FSoftObjectPath MyAssetPath(Path);
	UObject* MyAsset = MyAssetPath.TryLoad();

	if (!MyAsset)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Cannot load Point Cloud Asset %s"), *Path);
		return nullptr;
	}

	UPointCloud* AsPointCloud = Cast<UPointCloud>(MyAsset);

	if (AsPointCloud == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Cannot Cast Asset To Point Cloud (%s)"), *Path);
		return nullptr;
	}

	return AsPointCloud;
}

UPointCloud* UPointCloudAssetsHelpers::CreateEmptyPointCloudAsset(const FString& InPackageName)
{
	UPointCloud* PointCloud = nullptr;
	UPointCloudFactory* NewFactory = NewObject<UPointCloudFactory>();

	FAssetToolsModule& AssetToolsModule = FAssetToolsModule::GetModule();

	if (InPackageName.IsEmpty() || !FPackageName::IsValidObjectPath(InPackageName))
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		IContentBrowserSingleton& ContentBrowserSingleton = ContentBrowserModule.Get();
		FString Path = ContentBrowserSingleton.GetCurrentPath().GetInternalPathString();

		PointCloud = Cast<UPointCloud>(AssetToolsModule.Get().CreateAssetWithDialog(TEXT("PointCloud"), Path, NewFactory->GetSupportedClass(), NewFactory));
	}
	else
	{
		const FString AssetName = FPackageName::GetLongPackageAssetName(InPackageName);
		const FString PackagePath = FPackageName::GetLongPackagePath(InPackageName);

		PointCloud = Cast<UPointCloud>(AssetToolsModule.Get().CreateAsset(AssetName, PackagePath, NewFactory->GetSupportedClass(), NewFactory));
	}

	return PointCloud;
}

void UPointCloudAssetsHelpers::DeleteAllActorsOnDataLayer(UWorld* InWorld, const UDataLayerInstance* InDataLayerInstance)
{
	UWorld* World = InWorld;
	if (!World)
	{
		World = GEditor->GetEditorWorldContext().World();
	}

	if (!World || !World->GetWorldPartition())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Invalid world or not World Partition enabled world"));
		return;
	}

	UWorldPartition* WorldPartition = World->GetWorldPartition();
	if (!WorldPartition)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Unable to query world partition"));
		return;
	}

	if (InDataLayerInstance == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Invalid data layer"));
		return;
	}

	TArray<const FWorldPartitionActorDescInstance*> ActorsToDelete;

	FWorldPartitionHelpers::ForEachActorDescInstance(WorldPartition, [InDataLayerInstance, &ActorsToDelete](const FWorldPartitionActorDescInstance* ActorDescInstance)
		{
			if (ActorDescInstance != nullptr && ActorDescInstance->GetDataLayerInstanceNames().ToArray().Contains(InDataLayerInstance->GetDataLayerFName()))
			{
				ActorsToDelete.Add(ActorDescInstance);
			}
			return true;
		});

	TArray<FString> PackagesToDeleteFromSCC;

	for (const FWorldPartitionActorDescInstance* ActorDescInstance : ActorsToDelete)
	{
		// Already loaded case
		if (ActorDescInstance->GetActor())
		{
			World->DestroyActor(ActorDescInstance->GetActor());
		}
		else
		{
			PackagesToDeleteFromSCC.Emplace(ActorDescInstance->GetActorPackage().ToString());
			FGuid ActorGuid = ActorDescInstance->GetGuid();
			WorldPartition->RemoveActor(ActorGuid);
		}
	}

	// Delete scc packages if needed
	if (PackagesToDeleteFromSCC.Num() > 0)
	{
		FPackageSourceControlHelper PackageHelper;
		if (!PackageHelper.Delete(PackagesToDeleteFromSCC))
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Unable to delete all files from SCC; deleted actors will come back on map reload."));
		}
	}
}

void UPointCloudAssetsHelpers::DeleteAllActorsByPrefixInPartitionedWorld(UWorld* InWorld, const FString& InPrefix)
{
	UWorld* World = InWorld;
	if (!World)
	{
		World = GEditor->GetEditorWorldContext().World();
	}

	if (!World || !World->GetWorldPartition())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Invalid world"));
		return;
	}

	UWorldPartition* WorldPartition = World->GetWorldPartition();
	if (!WorldPartition)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Unable to query world partition"));
		return;
	}

	TArray<const FWorldPartitionActorDescInstance*> ActorsToDelete;

	FWorldPartitionHelpers::ForEachActorDescInstance(WorldPartition, [&InPrefix, &ActorsToDelete](const FWorldPartitionActorDescInstance* ActorDescInstance)
		{
			if (ActorDescInstance != nullptr && ActorDescInstance->GetActorLabel().ToString().StartsWith(InPrefix))
			{
				ActorsToDelete.Add(ActorDescInstance);
			}
			return true;
		});

	TArray<FString> PackagesToDeleteFromSCC;

	for (const FWorldPartitionActorDescInstance* ActorDescInstance : ActorsToDelete)
	{
		// Already loaded case
		if (ActorDescInstance->GetActor())
		{
			World->DestroyActor(ActorDescInstance->GetActor());
		}
		else
		{
			PackagesToDeleteFromSCC.Emplace(ActorDescInstance->GetActorPackage().ToString());
			FGuid ActorGuid = ActorDescInstance->GetGuid();
			WorldPartition->RemoveActor(ActorGuid);
		}
	}

	// Delete scc packages if needed
	if (PackagesToDeleteFromSCC.Num() > 0)
	{
		FPackageSourceControlHelper PackageHelper;
		if (!PackageHelper.Delete(PackagesToDeleteFromSCC))
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Unable to delete all files from SCC; deleted actors will come back on map reload."));
		}
	}
}

void UPointCloudAssetsHelpers::SetActorPivots(const TArray<AActor*>& InActors, EPointCloudPivotType InPivotType)
{
	if (InPivotType == EPointCloudPivotType::Default)
	{
		return;
	}

	for (AActor* Actor : InActors)
	{
		// Continue if the actor already has it's pivot set from the point cloud.
		const FTransform ActorTransform = Actor->GetTransform();
		if (!ActorTransform.Equals(FTransform::Identity))
		{
			continue;
		}

		FTransform TargetTransform;
		if (InPivotType != EPointCloudPivotType::WorldOrigin)
		{
			FVector ActorAABBOrigin(EForceInit::ForceInitToZero);
			FVector ActorAABBExtents(EForceInit::ForceInitToZero);
			Actor->GetActorBounds(false, ActorAABBOrigin, ActorAABBExtents);

			if (InPivotType == EPointCloudPivotType::CenterMinZ)
			{
				ActorAABBOrigin.Z -= ActorAABBExtents.Z;
			}

			TargetTransform.SetLocation(ActorAABBOrigin);
		}

		const FTransform RelativeTransform = ActorTransform.GetRelativeTransform(TargetTransform);

		if (RelativeTransform.Equals(FTransform::Identity))
		{
			return;
		}

		Actor->SetActorTransform(TargetTransform);

		for (UActorComponent* ActorComponent : Actor->GetInstanceComponents())
		{
			if (UInstancedStaticMeshComponent* ISMC = Cast<UInstancedStaticMeshComponent>(ActorComponent))
			{
				for (int32 i = 0, e = ISMC->GetInstanceCount(); i != e; ++i)
				{
					FTransform InstanceTransform;
					if (ISMC->GetInstanceTransform(i, InstanceTransform))
					{
						ISMC->UpdateInstanceTransform(i, InstanceTransform * RelativeTransform);
					}
				}
			}
			else if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(ActorComponent))
			{
				const FTransform& SMCTransform = SMC->GetComponentTransform();
				SMC->SetWorldTransform(SMCTransform * RelativeTransform);
			}
		}
		Actor->UpdateComponentTransforms();
	}
}

void UPointCloudAssetsHelpers::ParseModulesOnActor(AActor* InActor, const TArray<const UDataLayerInstance*>& InDataLayerInstances, TArray<FPointCloudPoint>& OutPoints)
{
	if (!InActor)
	{
		return;
	}

	auto AddActorInfo = [InActor, &InDataLayerInstances](TMap<FString, FString>& AttributesToEdit) {
		AttributesToEdit.Add(TEXT("ActorLabel"), InActor->GetActorLabel());
		AttributesToEdit.Add(TEXT("ActorName"), InActor->GetName());

		for (int DataLayerIndex = 0; DataLayerIndex < InDataLayerInstances.Num(); ++DataLayerIndex)
		{
			bool bActorInDataLayer = InActor->ContainsDataLayer(InDataLayerInstances[DataLayerIndex]);
			AttributesToEdit.Add(InDataLayerInstances[DataLayerIndex]->GetDataLayerShortName(), bActorInDataLayer ? TEXT("1") : TEXT("0"));
		}
	};

	const FString InstanceKey = PointCloudAssetHelpers::GetUnrealAssetMetadataKey();
	const FString CustomDataKey = TEXT("primitive_data");
	const FString DefaultCustomDataValue = TEXT("-1.0");

	// If blueprint -> return original name
	if (InActor->GetClass()->IsChildOf(UBlueprint::StaticClass()))
	{
		FPointCloudPoint& Point = OutPoints.Emplace_GetRef();
		Point.Transform = InActor->GetTransform();
		AddActorInfo(Point.Attributes);
		Point.Attributes.Add(InstanceKey, FAssetData(InActor->GetClass()).GetExportTextName());
		Point.Attributes.Add(CustomDataKey, DefaultCustomDataValue);
	}
	// If packed level actor -> get source blueprint
	else if (APackedLevelActor* PackedLevelActor = Cast<APackedLevelActor>(InActor))
	{
		FPointCloudPoint& Point = OutPoints.Emplace_GetRef();
		Point.Transform = InActor->GetTransform();
		AddActorInfo(Point.Attributes);
		Point.Attributes.Add(InstanceKey, FAssetData(PackedLevelActor->GetClass()->ClassGeneratedBy).GetExportTextName());
		Point.Attributes.Add(CustomDataKey, DefaultCustomDataValue);
	}
	else // Otherwise -> parse SM, ISM, HISM, niagara
	{
		for (UActorComponent* ActorComponent : InActor->GetInstanceComponents())
		{
			if (UInstancedStaticMeshComponent* ISMC = Cast<UInstancedStaticMeshComponent>(ActorComponent))
			{
				TMap<FString, FString> Attributes;
				AddActorInfo(Attributes);
				Attributes.Add(InstanceKey, FAssetData(ISMC->GetStaticMesh()).GetExportTextName());

				const bool bHasCustomData = (ISMC->NumCustomDataFloats == 1);

				if (!bHasCustomData)
				{
					Attributes.Add(CustomDataKey, DefaultCustomDataValue);
				}

				for (int32 i = 0, e = ISMC->GetInstanceCount(); i != e; ++i)
				{
					FTransform InstanceTransform;
					if (ISMC->GetInstanceTransform(i, InstanceTransform, /*bWorldSpace=*/true))
					{
						FPointCloudPoint& Point = OutPoints.Emplace_GetRef();
						Point.Transform = InstanceTransform;
						Point.Attributes = Attributes;

						if (bHasCustomData)
						{
							Point.Attributes.Add(CustomDataKey, FString::Format(TEXT("{0}"), { ISMC->PerInstanceSMCustomData[i] }));
						}
					}
				}
			}
			else if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(ActorComponent))
			{
				FPointCloudPoint& Point = OutPoints.Emplace_GetRef();
				Point.Transform = SMC->GetComponentTransform();
				AddActorInfo(Point.Attributes);
				Point.Attributes.Add(InstanceKey, FAssetData(SMC->GetStaticMesh()).GetExportTextName());
				Point.Attributes.Add(CustomDataKey, DefaultCustomDataValue);
			}
			else if (UNiagaraComponent* NC = Cast<UNiagaraComponent>(ActorComponent))
			{
				FPointCloudPoint& Point = OutPoints.Emplace_GetRef();
				Point.Transform = NC->GetComponentTransform();
				AddActorInfo(Point.Attributes);
				Point.Attributes.Add(InstanceKey, FAssetData(NC->GetAsset()).GetExportTextName());
				Point.Attributes.Add(CustomDataKey, DefaultCustomDataValue);
			}
		}
	}
}

TArray<FPointCloudPoint> UPointCloudAssetsHelpers::GetModulesFromDataLayers(UWorld* InWorld, const TArray<UDataLayerAsset*>& InDataLayerAssets)
{
	TArray<FPointCloudPoint> Points;
	UWorld* World = InWorld;
	if (!World)
	{
		World = GEditor->GetEditorWorldContext().World();
	}

	if (!World || !World->GetWorldPartition())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Invalid world or not World Partition enabled world"));
		return Points;
	}

	UWorldPartition* WorldPartition = World->GetWorldPartition();
	if (!WorldPartition)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Unable to query world partition"));
		return Points;
	}

	if(InDataLayerAssets.Num() == 0)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Invalid data layer assets"));
		return Points;
	}

	UDataLayerEditorSubsystem* DataLayerEditorSubsystem = UDataLayerEditorSubsystem::Get();

	if (!DataLayerEditorSubsystem)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Unable to get data layer subsystem"));
		return Points;
	}

	TArray<const UDataLayerInstance*> DataLayerInstances;
	for (const UDataLayerAsset* DataLayerAsset : InDataLayerAssets)
	{
		if (const UDataLayerInstance* DataLayerInstance = DataLayerEditorSubsystem->GetDataLayerInstance(DataLayerAsset))
		{
			DataLayerInstances.AddUnique(DataLayerInstance);
		}
		else
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Data layer name does not match to any existing data layer"));
			return Points;
		}
	}

	TArray<const FWorldPartitionActorDescInstance*> ActorsToProcess;

	FWorldPartitionHelpers::ForEachActorDescInstance(WorldPartition, [&DataLayerInstances, &ActorsToProcess](const FWorldPartitionActorDescInstance* ActorDescInstance)
		{
			if (ActorDescInstance)
			{
				bool bHasMatchingDataLayer = Algo::AnyOf(ActorDescInstance->GetDataLayerInstanceNames().ToArray(), [&DataLayerInstances](const FName& DLName)
					{
						auto IsDataLayerInstanceOfName = [DLName](const UDataLayerInstance* DataLayerInstance) {return DataLayerInstance->GetDataLayerFName() == DLName;};
						return DataLayerInstances.FindByPredicate(IsDataLayerInstanceOfName);
					});

				if (bHasMatchingDataLayer)
				{
					ActorsToProcess.Add(ActorDescInstance);
				}
			}
			return true;
		});

	FScopedSlowTask Task(ActorsToProcess.Num(), LOCTEXT("ParsingActors", "Parsing actors..."));
	Task.MakeDialogDelayed(0.1f);

	for (const FWorldPartitionActorDescInstance* ActorDescInstance : ActorsToProcess)
	{
		Task.EnterProgressFrame();
		FWorldPartitionReference ActorRef(WorldPartition, ActorDescInstance->GetGuid());
		ParseModulesOnActor(ActorDescInstance->GetActor(), DataLayerInstances, Points);
	}

	return Points;
}

TArray<FPointCloudPoint> UPointCloudAssetsHelpers::GetModulesFromMapping(USliceAndDiceMapping* InMapping)
{
	TArray<FPointCloudPoint> Points;

	if (!InMapping)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Invalid mapping"));
		return Points;
	}

	TArray<FSliceAndDiceManagedActorsEntry> ActorEntries;
	InMapping->GatherManagedActorEntries(ActorEntries, /*bGatherDisabled=*/true);
	TArray<TSoftObjectPtr<AActor>> ActorsToProcess = SliceAndDiceManagedActorsHelpers::ToActorList(ActorEntries);

	UWorld* World = GEditor->GetEditorWorldContext().World();

	if (!World)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Invalid world"));
		return Points;
	}

	UWorldPartition* WorldPartition = World->GetWorldPartition();

	FScopedSlowTask Task(ActorsToProcess.Num(), LOCTEXT("ParsingActors", "Parsing actors..."));
	Task.MakeDialogDelayed(0.1f);

	const TArray<const UDataLayerInstance*> DummyDataLayers;

	for (const TSoftObjectPtr<AActor>& Actor : ActorsToProcess)
	{
		Task.EnterProgressFrame();

		if (WorldPartition)
		{
			// Make sure it's loaded/unloaded propertly
			const FWorldPartitionActorDescInstance* ActorDescInstance = WorldPartition->GetActorDescInstanceByPath(Actor.ToSoftObjectPath());
			FWorldPartitionReference ActorRef(WorldPartition, ActorDescInstance->GetGuid());
			ParseModulesOnActor(ActorDescInstance->GetActor(), DummyDataLayers, Points);
		}
		else
		{
			ParseModulesOnActor(Actor.Get(), DummyDataLayers, Points);
		}
	}

	return Points;
}

void UPointCloudAssetsHelpers::ExportToCSV(const FString& InFilename, const TArray<FPointCloudPoint>& InPoints)
{
	if (InPoints.Num() == 0 || InFilename.IsEmpty())
	{
		UE_LOG(PointCloudLog, Log, TEXT("Exporting to CSV file failed, either because the path is empty or there are no points to export"));
		return;
	}

	// Build string, we'll assume that all points have the same columns.
	const TMap<FString, FString>& FirstPointAttributes = InPoints[0].Attributes;

	TArray<FString> AttributeKeys;

	// example Id,Px,Py,Pz,orientx,orienty,orientz,orientw,scalex,scaley,scalez, [unreal_instance..]
	TStringBuilder<4096> Builder;
	Builder.Append(TEXT("Id,Px,Py,Pz,orientx,orienty,orientz,orientw,scalex,scaley,scalez"));
	for (const auto& Attributes : FirstPointAttributes)
	{
		AttributeKeys.Add(Attributes.Key);
		Builder.Appendf(TEXT(",%s"), *Attributes.Key);
	}

	for(int PointIndex = 0; PointIndex < InPoints.Num(); ++PointIndex)
	{
		Builder.Append(TEXT("\n"));

		const FPointCloudPoint& Point = InPoints[PointIndex];
		const FTransform& Transform = Point.Transform;

		Builder.Appendf(TEXT("%d,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f"),
			PointIndex,
			Transform.GetTranslation().X, // px
			Transform.GetTranslation().Z, // py (swapped)
			Transform.GetTranslation().Y, // pz (swapped)
			Transform.GetRotation().X, // orientx
			Transform.GetRotation().Z, // orienty (swapped)
			Transform.GetRotation().Y, // orientz (swapped)
			-Transform.GetRotation().W, // orientw (inverted)
			Transform.GetScale3D().X, // scalex
			Transform.GetScale3D().Z, // scaley (swapped)
			Transform.GetScale3D().Y); // scalez (swapped)

		for(const auto& AttributeKey : AttributeKeys)
		{
			Builder.Appendf(TEXT(",%s"), *Point.Attributes[AttributeKey]);
		}
	}

	// Finally, save to file
	FFileHelper::SaveStringToFile(Builder.ToString(), *InFilename);
}

void UPointCloudAssetsHelpers::ExportToAlembic(const FString& InFilename, const TArray<FPointCloudPoint>& InPoints)
{
	if (InPoints.Num() == 0 || InFilename.IsEmpty())
	{
		UE_LOG(PointCloudLog, Log, TEXT("Exporting to Alembic file failed, either because the path is empty or there are no points to export"));
		return;
	}

	UE_LOG(PointCloudLog, Log, TEXT("Exporting to Alembic File: %s"), *InFilename);

	Alembic::AbcCoreOgawa::WriteArchive ArchiveWriter;
	Alembic::AbcCoreAbstract::ArchiveWriterPtr WriterPtr = ArchiveWriter(TCHAR_TO_UTF8(*InFilename), Alembic::Abc::MetaData());

	Alembic::Abc::OArchive Archive(WriterPtr, Alembic::Abc::kWrapExisting, Alembic::Abc::ErrorHandler::kThrowPolicy);

	Alembic::Abc::OObject TopObject = Archive.getTop();

	Alembic::Abc::TimeSampling TimeSampling(1.0 / 24.0, 0.0);
	Archive.addTimeSampling(TimeSampling);

	const int32 NumPoints = InPoints.Num();

	TArray<FQuat4f> Rotations;
	TArray<FVector3f> Translations;
	TArray<FVector3f> Scales;

	Rotations.SetNum(NumPoints);
	Translations.SetNum(NumPoints);
	Scales.SetNum(NumPoints);

	TMap<FString, TArray<std::string>> ExportMetaData;

	for (int32 i = 0; i != NumPoints; ++i)
	{
		const FPointCloudPoint& PointCloudPoint = InPoints[i];
		const FTransform& Transform = PointCloudPoint.Transform;
		
		Rotations[i] = FQuat4f(FQuat(Transform.GetRotation().X, Transform.GetRotation().Z, Transform.GetRotation().Y, -Transform.GetRotation().Z));
		Translations[i] = FVector3f(FVector(Transform.GetTranslation().X, Transform.GetTranslation().Z, Transform.GetTranslation().Y));
		Scales[i] = FVector3f(FVector(Transform.GetScale3D().X, Transform.GetScale3D().Z, Transform.GetScale3D().Y));

		for (const auto& PointMetaData : PointCloudPoint.Attributes)
		{
			const FString Key = PointMetaData.Key;
			const FString Value = PointMetaData.Value;

			TArray<std::string>& ValueArray = ExportMetaData.FindOrAdd(Key);
			ValueArray.SetNum(NumPoints);

			ValueArray[i] = std::string(TCHAR_TO_UTF8(*Value));
		}
	}

	Alembic::AbcGeom::OPoints Points(TopObject, "points", 1);
	Alembic::AbcGeom::OPointsSchema PointsSchema = Points.getSchema();
	Alembic::AbcGeom::OPointsSchema::Sample PointsSample;

	PointsSample.setPositions(Alembic::Abc::P3fArraySample((const Imath::V3f*)Translations.GetData(), NumPoints));
	PointsSample.setVelocities(Alembic::Abc::V3fArraySample(nullptr, 0));
	PointsSample.setIds(Alembic::Abc::UInt64ArraySample(nullptr, 0));
	PointsSchema.set(PointsSample);

	Alembic::AbcGeom::OCompoundProperty Parameters = PointsSchema.getArbGeomParams();

	Alembic::Abc::QuatfArraySample OrientsSample((const Imath::Quatf*)Rotations.GetData(), NumPoints);
	Alembic::Abc::OQuatfArrayProperty QuatProperty(Parameters, "orient");
	QuatProperty.set(OrientsSample);

	Alembic::Abc::V3fArraySample ScaleSample((const Imath::V3f*)Scales.GetData(), NumPoints);
	Alembic::Abc::OV3fArrayProperty ScaleParam(Parameters, "scale");
	ScaleParam.set(ScaleSample);

	for (const auto& ExportMetaDataItr : ExportMetaData)
	{
		const FString Key = ExportMetaDataItr.Key;
		const TArray<std::string> Values = ExportMetaDataItr.Value;

		Alembic::Abc::StringArraySample MetaDataSample(Values.GetData(), Values.Num());
		Alembic::Abc::OStringArrayProperty MetaDataProperty(Parameters, std::string(TCHAR_TO_UTF8(*Key)));

		MetaDataProperty.set(MetaDataSample);
	}
}

#undef LOCTEXT_NAMESPACE

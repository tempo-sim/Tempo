// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "PointCloudSliceAndDiceRule.h"
#include "PointCloudStats.h"
#include "Components/ActorComponent.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PointCloudAssetHelpers.generated.h"

class UInstancedStaticMeshComponent;
class UHierarchicalInstancedStaticMeshComponent;
class FPointCloudSliceAndDiceExecutionContext;
class FPointCloudRuleInstance;
class USliceAndDiceMapping;

namespace PointCloudAssetHelpers
{
	/**
	* Open A File Dialog Configured for loading PointCloud PSV Files
	* @param DialogTitle The Text for the title bar of the dialog
	* @param DefaultPath The default path to display upon opening the dialog
	* @param OutFilenames The list of filenames selected by the user. This is empty on Cancel
	* @param FileTypes List of file extensions to load
	*/
	void OpenFileDialog(const FString& DialogTitle, const FString& DefaultPath, const FString& FileTypes, TArray<FString>& OutFileNames);

	/**
	* Open A File Dialog Configured for Saving PointCloud PSV Files
	* @param DialogTitle The Text for the title bar of the dialog
	* @param DefaultPath The default path to display upon opening the dialog
	* @param OutFilenames The list of filenames selected by the user. This is empty on Cancel
	* @param FileTypes List of file extensions to save
	*/
	void SaveFileDialog(const FString& DialogTitle, const FString& DefaultPath, const FString& FileTypes, TArray<FString>& OutFileNames);

	/**
	* Returns the default metadata field in the point cloud attributes
	*/
	FString GetUnrealAssetMetadataKey();
}

USTRUCT()
struct POINTCLOUDEDITOR_API FPointCloudComponentData
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UPointCloudView> View = nullptr;

	UPROPERTY()
	FComponentReference Component;

	UPROPERTY()
	TMap<FString, FString> MetadataValues;

	UPROPERTY()
	int32 Count = 0;
};

USTRUCT()
struct POINTCLOUDEDITOR_API FPointCloudManagedActorData
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<AActor> Actor = nullptr;

	/** If the user chooses to create multiple actors, store which Metadata key Value this actor was created with */
	UPROPERTY()
	FString ModuleAttributeKey;

	/** Holds the original view at the rule level */
	UPROPERTY(Transient)
	TObjectPtr<UPointCloudView> OriginatingView = nullptr;

	/** Holds the sub-view containing only this actor's information */
	UPROPERTY(Transient)
	TObjectPtr<UPointCloudView> ActorView = nullptr;

	/** Slice and dice uses views to extract information from the point cloud. This maps between the static mesh in question and the view that extracts the points associated with that mesh from the PC */
	UPROPERTY(Transient)
	TArray<FPointCloudComponentData> ComponentsData;

	/** Metadata keys used to separate this actor from others in the originating view */
	TArray<FString> GroupOnMetadataKeys;
};

USTRUCT(BlueprintType)
struct POINTCLOUDEDITOR_API FSpawnAndInitMaterialOverrideParameters
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides)
	TMap<FString, int32> MetadataKeyToIndex;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides)
	TMap<FString, FString> MetadataKeyToTemplate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides)
	TMap<FString, FString> MetadataKeyToSlotName;

	/** Returns all metadata keys referred to by these overrides */
	TArray<FString> GetMetadataKeys() const;

	/**
	* Sets valid material overrides in the settings based on whether the keys exist in the provided point cloud
	* @param MaterialOverrides the overrides to apply
	* @param PointCloud the point cloud we are running the rules against
	*/
	void CopyValid(const FSpawnAndInitMaterialOverrideParameters& InMaterialOverrides, UPointCloud* PointCloud);
};

USTRUCT(BlueprintType)
struct POINTCLOUDEDITOR_API FSpawnAndInitActorParameters
{
	GENERATED_BODY()

	// This is a copy of the original map which should make it easier to multi-thread the actor creation down the line
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides)
	TMap<TObjectPtr<UStaticMesh>, TObjectPtr<UStaticMesh>> OverrideMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides)
	FSpawnAndInitMaterialOverrideParameters MaterialOverrides;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Templates)
	TObjectPtr<UInstancedStaticMeshComponent> TemplateIsm = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Templates)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TemplateHISM = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Templates)
	TObjectPtr<UStaticMeshComponent> TemplateStaticMeshComponent = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides)
	bool bSingleInstanceAsStaticMesh = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides)
	bool bUseHierarchicalInstancedStaticMeshComponent = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Templates)
	TObjectPtr<AActor>	TemplateActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides)
	TObjectPtr<UWorld>	World = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	EPointCloudPivotType PivotType = EPointCloudPivotType::Default;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	FString PivotKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	FString PivotValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	FString PerModuleAttributeKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides)
	bool bManualGroupId = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides)
	int32 GroupId = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides)
	FName FolderPath;

	/** Metadata key in point cloud that maps to mesh module. */
	UPROPERTY()
	FString MeshKey = PointCloudAssetHelpers::GetUnrealAssetMetadataKey();

	/**
	* Returns the name to give to a new actor. Will recycle actor names/files that would be deleted otherwise in WP.
	*/
	FName GetName() const { return NameGetter ? NameGetter() : NAME_None; }

	/**
	* Sets a function to return a name for the actor to spawn
	* @param InNameGetter The new getter function for the name
	*/
	void SetNameGetter(TFunction<FName(void)> InNameGetter) { NameGetter = InNameGetter; }

	/**
	* Builds the default function to return a name with actor reuse
	* @param Context The rule execution context
	* @param Rule The rule creating the actor(s)
	*/
	void SetNameGetter(FSliceAndDiceExecutionContext* Context, FPointCloudRuleInstance* Rule);

	TFunction<FName(void)> NameGetter;
	FPointCloudStatsPtr StatsObject;
};

/** A suite of helper blueprint functions to make life easier when using PointClouds and associated classes */
UCLASS(BlueprintType)
class POINTCLOUDEDITOR_API UPointCloudAssetsHelpers : public UBlueprintFunctionLibrary
{
private:
	/**
	* Common code to initialize component data (see FPointCloudComponentData)
	* from the information present in the managed actor (namely, the actor view, group data keys, etc.).
	* This method is intended to be called before InitActorComponents.
	* @param Actor - The actor we're preparing for creation.
	*/
	static void InitActorComponentData(FPointCloudManagedActorData& ManagedActor);

	/**
	* Common code for both the single & multi actor code paths
	* Initializes the components on the actor using the module counts given
	* @param Actor - The actor we're adding components onto, must be non null
	* @param GroupId - A unique  number that defines the grouping of the components. This value will be added to each component in RayTracingGroupId attribute	
	* @param MeshCache - Pointer to a common mesh cache (used in the multi actor code path), can be null
	* @param Params - Parameters controlling use of templates and so on.
	*/
	static void InitActorComponents(FPointCloudManagedActorData& ManagedActor, int32 GroupId, TMap<FString, UStaticMesh*>* MeshCache, const FSpawnAndInitActorParameters& Params);

	GENERATED_BODY()
public:
	
	/**
	* Return an array containing the selected RuleProcessor items from the content browser
	* @return - An Array of FAssetData items for any RuleProcessorItems selected in the content browser. An empty Array if no RuleProcessor Items are selected
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloud")
	static TArray<FName> GetSelectedRuleProcessorItemsFromContentBrowser();

	/**
	* Open a file Open dialog to load a PSV file into a new PointCloud asset
	* @return - An array of pointers to the newly created PointCloud Asset or NULL on failure / cancel
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloud")
	static TArray<UPointCloud*> LoadPointCloudCSV();

	/**
	* Open a file Open dialog to load an Alembic file into a new PointCloud asset
	* @return - An array of pointers to the newly created PointCloud Asset or NULL on failure / cancel
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloud")
	static TArray<UPointCloud*> LoadPointCloudAlembic();

	/**
	* Loads a point cloud from a given file path
	* @param The file path to load
	* @return A pointer to the newly created PointCloud Asset or NULL on failure / cancel
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloud")
	static UPointCloud* LoadPointCloudAssetFromPath(const FString &Path);

	/**
	* Creates an empty point cloud.
	* @param The file path to load from. If empty or invalid, will ask for a path in a dialog. Can overwrite existing files.
	* @return Pointer to the point cloud loaded/created
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloud")
	static UPointCloud* CreateEmptyPointCloudAsset(const FString& PackageName);

	/**
	* Given a Metadata Key, find the unique values for that key and using a template create A Map from each Unique Value to an Actor Name
	* The name for each actor is created by substituting tokens in the NameTemplate, the tokens that can be replaced are
	*	$RULEPROCESSOR_ASSET - The name of the point cloud. This is always the Name of the incoming point cloud
	*	$METADATAKEY - The metadata key name. This is always the value of MetadataKey passed in
	*	$METADATAVALUE - The unique value. This changes for each unique value found for MetadataKey
	*
	* @param PointCloudView - The point cloud view to initialize the objects with
	* @param MetadataKey - The Metadata Key to Filter the Actors on
	* @param NameTemplate - A String containing the name template to be generated for each unique value of MetadataKey
	* @return - A map of the unqiue values of Metadatakey the names generated for each value from the template
	*/
	static TMap<FString, FString> MakeNamesFromMetadataValues(	UPointCloudView* PointCloudView,
																const FString& MetadataKey,
																const FString& NameTemplate);

	/**
	* Bulk Create Rule Processor Managed Actors given a list of Labels
	* @param ValuesAndLabels - The List of Metadata values to use for each actor and the associated label
	* @param MetadataKey - The Metadata Key to Filter the Actors on
	* @param PointCloud - The point cloud to initialize the objects with
	* @param InOverrideMap - A map of Static mesh components to Static Mesh Components. This is used when populating the ISMs to provide overrides. I.e. if the key is found, use the value instead
	* @return - A map of the labels and the associated actors r
	*/
	static TMap<FString, FPointCloudManagedActorData> BulkCreateManagedActorsFromView(UPointCloudView* PointCloudView,
																				const FString& MetadataKey,
																				const TMap<FString, FString>& ValuesAndLabels,
																				const FSpawnAndInitActorParameters& Params);
		
	/**
	* Create a single actor to represent all of the points in the given pointcloud view
	* @param PointCloud - The point cloud to initialize the objects with
	* @param PointCloudView - The point cloud view to initialize the objects with
	* @param Label - The label to give to this new actor
	* @param World - The world into which the new actor should be created
	* @param InTemplateActor - A template actor from which the parameters will be copied
	* @param InTemplateIsm - A template ISM from which the parameters will be copied
	* @param InOverrideMap - A map of Static mesh components to Static Mesh Components. This is used when populating the ISMs to provide overrides. I.e. if the key is found, use the value instead
	* @return - A point to the new Managed Actor
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloudView")
	static AActor* CreateActorFromView(UPointCloudView* PointCloudView, const FString& Label, const FSpawnAndInitActorParameters& Params);

	/**
	* Given a list of preinitialized ManagedActors, use their PointCloudCursors to fetch and update ALL instances required.
	* @param ActorsToUpdate - The list of actors that need updating
	*/
	static void UpdateAllManagedActorInstances(const TMap<FString, FPointCloudManagedActorData>& ActorsToUdpate);
	static void UpdateManagedActorInstance(const FPointCloudManagedActorData& ManagedActor, TMap<FString, int>* CacheHitCount = nullptr);

	UFUNCTION(BlueprintCallable, Category = "PointCloud")
	static void DeleteAllActorsOnDataLayer(UWorld* InWorld, const UDataLayerInstance* InDataLayerInstance);

	UFUNCTION(BlueprintCallable, Category = "PointCloud")
	static void DeleteAllActorsByPrefixInPartitionedWorld(UWorld* InWorld, const FString& InPrefix);

	/**
	* Transform actors and their meshes to a certain pivot type.
	* @param InActors - This list of actors to transform
	* @param InPivotType - The type of pivot (Default, WorldOrigin, AABBCenter, AABBCenterMinZ)
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloud")
	static void SetActorPivots(const TArray<AActor*>& InActors, EPointCloudPivotType InPivotType);

	/**
	* Given a spawn parameters item, return either an Actor pointer or NULL, depending on the Existing Actor Behavior set in the Params
	* @param Params - Rule Processor Specific Construction parameters
	* @return - The point to a new or existing Managed actor, or nullptr
	*/
	static AActor* GetManagedActor(const FString& Label, const FSpawnAndInitActorParameters& Params);

	/** Parses an actor for "modules" (SM, ISM, HISM, BP, Packed LI, ... and adds points to the array */
	static void ParseModulesOnActor(AActor* InActor, const TArray<const UDataLayerInstance*>& InDataLayerInstances, TArray<FPointCloudPoint>& OutModules);

	/** Builds an array of points containing the modules found on the actors in the provided data layers */
	UFUNCTION(BlueprintCallable, Category = "PointCloudUtils")
	static TArray<FPointCloudPoint> GetModulesFromDataLayers(UWorld* InWorld, const TArray<UDataLayerAsset*>& InDataLayerAssets);

	/** Builds an array of points containing the modules found on the actors in the provided Slice & Dice mapping */
	UFUNCTION(BlueprintCallable, Category = "PointCloudUtils")
	static TArray<FPointCloudPoint> GetModulesFromMapping(USliceAndDiceMapping* InMapping);

	/** Exports an array of points to a CSV file */
	UFUNCTION(BlueprintCallable, Category = "PointCloudUtils")
	static void ExportToCSV(const FString& InFilename, const TArray<FPointCloudPoint>& InPoints);

	/** Exports an array of points to an Alembic file */
	UFUNCTION(BlueprintCallable, Category = "PointCloudUtils")
	static void ExportToAlembic(const FString& InFilename, const TArray<FPointCloudPoint>& InPoints);

private:
	/* accepted file types */
	enum class EPointCloudFileType : uint8
	{
		Csv,
		Alembic
	};

	/**
	* Given a point cloud, a key and a value calculate a unique hash id for the given values
	* @param PointCloudView  - The view from which the values were calculated
	* @param MetadataKey  - The key, i.e. "building_id" 
	* @param MetadataValue  - The value associated with the key i.e. "building_123_downtown"
	*/
	static int32 CalculateGroupId(UPointCloudView* PointCloudView, const FString& MetadataKey, const FString& MetadataValue);

	static TArray<UPointCloud*> LoadPointCloud(const EPointCloudFileType InFileType);
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Internationalization/Text.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "PointCloudConfig.h"

#include "PointCloud.generated.h"

/** This enum can be used to control how sets of results or data are combined in the Pointcloud. It represents the basic boolen operations */
UENUM(BlueprintType)
enum class EFilterMode : uint8
{
	FILTER_Or UMETA(DisplayName = "OR"),
	FILTER_And UMETA(DisplayName = "AND"),
	FILTER_Not UMETA(DisplayName = "NOT"),
	FILTER_MAX
};

USTRUCT(BlueprintType)
struct POINTCLOUD_API FPointCloudPoint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FTransform Transform;

	/** Map from metadata type to value */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<FString, FString> Attributes;
};

/**
 * Implements a prototype point cloud data structure using SQLLITE as the backend
 */
UCLASS(Abstract, BlueprintType, hidecategories=(Object))
class POINTCLOUD_API UPointCloud : public UObject
{
	friend class UPointCloudView;

	GENERATED_BODY()

public:	

	/**
	* Controls newly loaded files interact with existing point cloud data. 
	*/ 
	enum ELoadMode
	{
		ADD,		// Add the points in the given file to the point cloud
		REPLACE		// Replace the contents of this pointcloud with the contents of the given file
	};

	/** Returns whether the point cloud is editor only */
	virtual bool IsEditorOnly() const override;
		
	/**
	* Return true if this point cloud is initialized and is ready for use
	* @return True If this point cloud is initialized and ready for use
	*/ 
	virtual bool IsInitialized() const PURE_VIRTUAL(UPointCloud::IsInitialized, return false;);

	/**
	* Attemp to convert this point cloud to the current schema
	* @return True If a transaction was ended sucessfully
	*/
	virtual bool AttemptToUpdate() PURE_VIRTUAL(UPointCloud::AttempToUpdate, return false;)

	/** Query if this point cloud is using an out of date schema and needs updating
	* @return True if this point cloud needs updating
	*/
	virtual bool NeedsUpdating() const PURE_VIRTUAL(UPointCloud::NeedsUpdating, return false;)

public:	// ~ View Interface

	/**
	* Create a new view onto this PointCloud Asset. The view is used to access data in the point cloud. Each view is indepdentant and can be used to filter and modify the data as it is read from the PointCloud.
	* The views do not modify the source data in the point cloud unless explicitly requested to. The view implements an override system on top of the PointCloud Data.
	* @return A point to a new UPointCloudView Object that references this PointCloud
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloud|View")
	virtual UPointCloudView* MakeView() PURE_VIRTUAL(UPointCloud::MakeView, return nullptr;);

public:	// ~ Attribute interface
		
	/**
	* Return the default attributes each item in the point cloud have by default. Data in the point cloud is divided into Default attributes and Metadata.
	* @return The list of attribute names that each item in the point cloud has by default
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloud|Info")
	virtual TArray<FString> GetDefaultAttributes() const PURE_VIRTUAL(UPointCloud::GetDefaultAttributes, return TArray<FString>(););

	/**
	* Return the names of the metadata items in this point cloud. Each point may have zero, one or more items of metadata associated with it.
	* Metadata is sparse and not all Points in the cloud may have each item of Metadata.
	* @return The list of Metadata names represented in this PointCloud
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloud|Info")	
	virtual TSet<FString> GetMetadataAttributes() const PURE_VIRTUAL(UPointCloud::GetMetadataAttributes, return TSet<FString>();)

	/**
	* Query if this point cloud supports a given, Named default attribute. The default attributes are those attributes shared by all points in the PointCloud
	* @param Name - The name of the default attribute to query
	* @return True if a default attribute with the given name exists
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloud|Info")
	bool HasDefaultAttribute(const FString& Name) const;

	/**
	* Query if this point cloud contains a given, Named metadata attribute. 
	* @param Name - The name of the metadata attribute to query
	* @return True if at least point in this pointcloud has a metadata attribute with the given name
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloud|Info")
	bool HasMetaDataAttribute(const FString& Name) const;

public:	// ~ Info interface

	// return the number of points in the point cloud 
	UFUNCTION(BlueprintCallable, Category = "PointCloud|Info")
	virtual int GetCount() const PURE_VIRTUAL(UPointCloud::GetCount, return 0;)

	/**
	* Return the bounding box of all of the points in this PointCloud
	* @return the bounding box that contains all of the points in this PointCloud
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloud|Info")
	virtual FBox GetBounds() const PURE_VIRTUAL(UPointCloud::GetBounds, return FBox(EForceInit::ForceInit);)

public: // I/O Interface

	/**
	* Load a point cloud from a CSV file.
	* @return True if the file was loaded sucessfully, False on failure
	* @param Mode - Controls if any exisiting data apended or replaced by the contents of the CSV file
	* @param FileName - The path to the CSV file to load
	* @param InImportBounds - Optional bounding box to define an import zone. Only points within the Import zone will be imported
	* @param Warn - Pointer to a FeedbackContext item to provide updates during loading
	*/
	virtual bool LoadFromCsv(const FString& FileName, const FBox& InImportBounds = FBox(EForceInit::ForceInit), ELoadMode Mode = UPointCloud::REPLACE, FFeedbackContext* Warn = nullptr) PURE_VIRTUAL(UPointCloud::LoadFromCsv, return false;)

	/**
	* Load a point cloud from an Alembic file.
	* @return True if the file was loaded successfully, False on failure
	* @param Mode - Controls if any existing data appended or replaced by the contents of the Alembic file
	* @param FileName - The path to the Alembic file to load
	* @param ImportBounds - Optional bounding box to define an import zone. Only points within the Import zone will be imported
	* @param Warn - Pointer to a FeedbackContext item to provide updates during loading
	*/
	virtual bool LoadFromAlembic(const FString& FileName, const FBox& InImportBounds = FBox(EForceInit::ForceInit), ELoadMode Mode = UPointCloud::REPLACE, FFeedbackContext* Warn = nullptr) PURE_VIRTUAL(UPointCloud::LoadFromAlembic, return false;)

	/**
	* Load a point cloud from structured points.
	* @return True if the data was loaded successfully, false otherwise
	* @param Points Array holding the points to add to the point cloud
	* @param ImportBounds Optional bounding box to define an import zone. Only points within the Import zone will be imported.
	* @param Warn - Pointer to a FeedbackContext item to provide updates during loading
	*/
	virtual bool LoadFromStructuredPoints(const TArray<FPointCloudPoint>& InPoints, const FBox& InImportBounds = FBox(EForceInit::ForceInit), FFeedbackContext* Warn = nullptr) PURE_VIRTUAL(UPointCloud::LoadFromStructuredPoints, return false;)

	/**
	* Load a point cloud from structured points.
	* @return True if the data was loaded successfully, false otherwise
	* @param Points Array holding the points to add to the point cloud
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloud|Construction")
	bool LoadFromPoints(const TArray<FPointCloudPoint>& InPoints);

	/**
	* Save this PointCloud to a file on disk. This currently only saves .db files compatible with SQLLITE3. More formats may be added in the future. 
	* No name or extension checking is performed. You get what you ask for if the file can be written. 
	* @return True if the file could be saved to disk correctly, False if the saving failed
	* @param FileName - The name of the file to write
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloud|Database")
	virtual bool SaveToDisk(const FString& FileName) PURE_VIRTUAL(UPointCloud::SaveToDisk, return false;)

	/**
	* Call this function to start logging sql calls
	* @return True if logging can be started. This may return false if SetSqlLog has not been called with a valid log file name	
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloud|Logging")
	virtual bool StartLogging(const FString& InFileName) PURE_VIRTUAL(UPointCloud::StartLogging, return false;)

	/**
	* Call this function to start logging sql calls
	* @return True if logging can be started. This may return false if SetSqlLog has not been called with a valid log file name
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloud|Logging")
	virtual bool StopLogging() PURE_VIRTUAL(UPointCloud::StopLogging, return false;)

	/**
	* Query if SQL logging is enabled
	* @return True if logging is currently active, false otherwise
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloud|Logging")
	bool LoggingEnabled() const;

	/**
	* A point cloud may be composed of various different input files. This method returns the paths of the files loaded into this pointcloud
	* @return The list of paths of files that were loaded to make this pointcloud
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloud|Files")	 
	virtual TArray<FString> GetLoadedFiles() const PURE_VIRTUAL(UPointCloud::GetLoadedFiles, return TArray<FString>();)

	/** Reload the point cloud from the original files, if all of the files can be found. This will fail if any of the original files, as returned by GetLoadedFiles, cannot be found 
	* @param ReimportBounds - Optional Box defining the zone that should be reimported
	* @return True if the point cloud was reloaded sucessfully, false otherwise
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloud|Files")
	bool Reimport(const FBox &ReimportBounds);   

	/** Replace the data in this point cloud from another file
	* @param FileName - The path to the file to load
	* @param ReimportBounds - Optional Box defining the zone that should be reimported
	* @return True if the point cloud was reloaded sucessfully, false otherwise
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloud|Files")
	bool ReplacePoints(const FString& FileName, const FBox& ReimportBounds);
	
	/**
	* A point cloud may be composed of various different input files. This method allows the caller to check if a given file is included in this pointcloud
	* @param Name - The Name of the file to check
	* @return True if the given file is included in this pointcloud
	*/
	bool IsFileLoaded(const FString& Name) const;
	
public:

	/**
	* Initialize From Prepared Data. PointCloud will potentially modify, take ownership or otherwise mess with The prepared data. Callers should assume the provided arrays are invalid afer this call
	* @param PreparedTransforms - An Array of Transforms, one for each point to be added to the point cloud
	* @param ObjectName - The name to associated with this set of data in the PointCloud
	* @param MetadataColumnNames - The names of the Metadata values for each point
	* @param MetadataCountPerVertex - The number of entries in PreparedMetadata associated to a given point
	* @param PreparedMetadata - An array containing the Metadata for points in the point cloud. Each entry contains an index for a column in MetadataColumnNames and a string with the actual metadata value.
	* @param Warn - Optional Feedback context
	* @return True if the insert succeeds, false otherwise
	*/
	virtual bool InitFromPreparedData(	const FString&						ObjectName,
										TArray<FTransform>&					PreparedTransforms,
										TArray<FString>&					MetadataColumnNames,
										TArray<int>&						MetadataCountPerVertex,
										TArray< TPair<int, FString> >&		PreparedMetadata,
										const FBox							&ImportBounds,
										FFeedbackContext*					Warn = nullptr
	) PURE_VIRTUAL(UPointCloud::InitFromPreparedData, return false;)

	/**
	* Clear the root views that were generated while executing rules so they can be garbage collected.
	* Should be used after a mapping has finished executing.*/	
	void ClearRootViews();

protected:

	/**
	* Private method for reloading points from files
	*/
	virtual bool ReloadInternal(const TArray<FString >&Files, const FBox & ReimportBounds) PURE_VIRTUAL(UPointCloud::ReloadInternal, return false;)

protected: //Data Section

	// Store a flag to enable / disable logging of SQL to DISK
	bool bLoggingEnabled = false;

	/** Store pointers to the root views so that they don't get garbage collected while we are processing */
	UPROPERTY(Transient)
	TSet<TObjectPtr<UPointCloudView>> RootViews;
};
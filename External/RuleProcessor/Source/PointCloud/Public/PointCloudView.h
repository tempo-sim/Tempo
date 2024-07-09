// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "PointCloud.h"
#include "PointCloudView.generated.h"

class UPointCloudImpl;

/**
 * Data within a PointCloud cannot be accessed directly. It must be accessed via a PointCloudView. A view encapsualtes the concept of reading from and modifying data in a PointCloud. 
 * The general usage pattern is to create a PointCloud, Create a view onto the PointCloud using the CreateView Method and then configure the view to extract the information 
 * you want from the point cloud. As many views as required can be made on a PointCloud
 */
UCLASS(BlueprintType, hidecategories=(Object))
class POINTCLOUD_API UPointCloudView : public UObject
{	
	GENERATED_BODY()

	friend class UPointCloudImpl;
	friend struct FPointCloudDataAccessHolder;

public:

	UPointCloudView();	
	~UPointCloudView();

	/** 
	* Creates a child view for view stacks and parents this view to it.
	* @return A child view to this view
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloudView|Management")
	UPointCloudView* MakeChildView();

	/**
	* Clear children views so they can be garbage collected. Should be used after a rule mapping has finished executing.
	* @property ChildViews
	*/
	void ClearChildViews();

	/**
	* Removes a child view from the child views, exposing it to be garbage collected
	* @property ChildViews
	*/
	void RemoveChildView(UPointCloudView* ChildView);

public:	// ~ Transform interface 

	/**
	* Gets the transforms of all points in the current view
	* @return The transforms array
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloudView|Transforms")
	TArray<FTransform> GetTransforms() const;

	/**
	* Get transforms and optionally the point ids from this view. If no FilterOn methods have been called this will return all of the points, if not it will return the result of applying the filter
	* This method uses a pathway that utilizes intermediate tables
	* @return The number of transforms returned by this call
	* @param OutTransforms - Array to contain the transforms
	* @param OutIds - Array to contain the point ids if OutputIds == TRUE
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloudView|Transforms")
	int GetTransformsAndIds(TArray<FTransform>& OutTransforms, TArray<int32>& OutIds) const;

	/**
	* Get transforms and the point ids from this view.
	* This method uses a pathway that utilizes intermediate tables
	* @return The pairs of ids to transforms from the view
	*/
	TArray<TPair<int32, FTransform>> GetPerIdTransforms() const;

	/**
	* Get the Ids of the points from this view
	* This method uses a pathway that utilizes intermediate tables
	* @return The number of points returned by this call		
	* @param OutIds - Array to contain the point ids if OutputIds == TRUE
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloudView|Ids")
	int GetIndexes(TArray<int32>& OutIds) const;

	/**
	* Get the bounding box of the points that pass the filter for this view. This bounding box is axis aligned but should be fast to calculate and doesn't require accessing all of the data returned by the filter
	* @return The bounding box of the points that will be returned by this view	
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloudView|Bounds")
	FBox GetResultsBoundingBox() const;

	/**
	* Return the number of points passing the filter that are also inside the given bounding box
	* @return The bounding box of the points that will be returned by this view
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloudView|Bounds")
	int32 CountResultsInBox(const FBox &Box) const;
	
	/**
	* Return the Metadata associated with a given point
	* @return Metadata (Name,Value) associated with the given point
	* @param PointId - The Id of the point to query. 
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloudView|Metadata")
	TMap<FString, FString> GetMetadata(int32 PointId) const;
	
	/**
	* Return the values associated with a given Metadata Key as an array of Ints
	* @return The values of the given Metadata item and the PointId for each result
	* @param Key - The name of the Metadata Item to Query
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloudView|Metadata")
	TArray<int> GetMetadataValuesArrayAsInt(const FString& Key) const;

	/**
	* Return the values associated with a given Metadata Key as an array of floats
	* @return The values of the given Metadata item and the PointId for each result
	* @param Key - The name of the Metadata Item to Query
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloudView|Metadata")
	TArray<float> GetMetadataValuesArrayAsFloat(const FString& Key) const;
	
	/**
	* Return the values associated with a given Metadata Key and the ID of the points on which the Metadata appears
	* @return The values of the given Metadata item and the PointId for each result
	* @param Key - The name of the Metadata Item to Query
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloudView|Metadata")
	TMap<int, FString> GetMetadataValues(const FString& Key) const;

	/**
	* Get the unique values for the given metadata values and the associated occurance Count for each item
	* @return A map containing the names of the unique metadata values and the number of times that value appeared in the results
	* @param Key - The name of the Metadata Item to Query
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloudView|Metadata")
	TMap<FString, int> GetUniqueMetadataValuesAndCounts(const FString& Key) const;

	/**
	* Get the unique values for the given metadata values elements and the associated occurence count for each item
	* @param Keys - The list of keys on which to group
	* @return An array of the unique values (an array of strings) and their count (an int), in a pair
	*/
	TArray<TPair<TArray<FString>, int>> GetUniqueMetadataValuesAndCounts(const TArray<FString>& Keys) const;

	/**
	* Return the unique values associated with a given Metadata Key
	* @return The unique values of the given Metadata item
	* @param Key - The name of the Metadata Item to Query
	* @param ApplyFilters - Set to true if any add filters should be applied, i.e. get the unique metadatavalues represented by the result set. If false then all unique values will be returned
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloudView|Metadata")
	TArray<FString> GetUniqueMetadataValues(const FString& Key) const;

	/**
	* Return the number of points returned from this view after applying all filters and modifications
	* @return The number of points returned from this view
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloudView|Results")
	int32 GetCount() const;

	/**
	* Returns the hash of the results in the current view after applying all filters and modifications
	* @return String containing the hash
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloudView|Results")
	const FString& GetHash() const;

	/**
	* Specialized hash methods for [attribute1, attribute2, ..., vertex] result hashing.
	* Note that this eschews the vertex ids that are normally used in the views
	* which are not very stable w.r.t data changes
	* @param Keys The relevant attribute keys that should be considered in the hash
	* @return String containing the hash
	*/
	FString GetValuesAndTransformsHash(const TArray<FString>& Keys) const;

public: // ~ Filter interface 
	
	/**
	* Add a filter to this view that only includes point if they Pass a Metadata test, i.e. Key=Value
	* @param MetaData - The name of the Metadata Item to test
	* @param Value - The value to search for, i.e. only points where MetaData=Value are added to the result set
	* @param Mode - How the results of this filter are combined with the result set. Allows inclusion, exclusion and intersection of matching results	
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloudView|Filters")
	void FilterOnMetadata(const FString&MetaData, const FString &Value, EFilterMode Mode = EFilterMode::FILTER_Or);

	/**
	* Add a filter to this view that only includes point if they Pass a Metadata pattern , i.e. Key LIKE (Value)
	* @param MetaData - The name of the Metadata Item to test
	* @param Pattern - The value to search for, i.e. only points where MetaData=Value are added to the result set
	* @param Mode - How the results of this filter are combined with the result set. Allows inclusion, exclusion and intersection of matching results
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloudView|Filters")
	void FilterOnMetadataPattern(const FString& MetaData, const FString& Pattern, EFilterMode Mode = EFilterMode::FILTER_Or);

	/**
	* Add a filter to this view that only includes point if they Pass an expression of the form P.x>? or P.y!=? and so on.
	* @param Expression - The expression to run on each potential point. If the expression resolves to true then the point is included in the result set
	* @param Mode - How the results of this filter are combined with the result set. Allows inclusion, exclusion and intersection of matching results	
	*/	
	UFUNCTION(BlueprintCallable, Category = "PointCloudView|Filters")
	void FilterOnPointExpression(const FString& Expression, EFilterMode Mode = EFilterMode::FILTER_Or);
	
	/**
	* Add a filter to this view that only includes point if it's within a given bounding box
	* @param BoundingBox - The bounding box to test the points against
	* @param bInvertSelection - Flag whether to include or exclude the points inside the bounding box
	* @param Mode - How the results of this filter are combined with the result set. Allows inclusion, exclusion and intersection of matching results
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloudView|Filters")
	void FilterOnBoundingBox(const FBox& BoundingBox, bool bInvertSelection, EFilterMode Mode = EFilterMode::FILTER_Or);

	/**
	* Add a filter to this view that only includes point if it's within a given oriented bounding box
	* @param InOBB - The oriented bounding box to test the points against (FTransform containing center location, orientation and extent)
	* @param bInvertSelection - Flag whether to include or exclude the points inside the oriented bounding box
	* @param Mode - How the results of this filter are combined with the result set. Allows inclusion, exclusion and intersection of matching results
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloudView|Filters")
	void FilterOnOrientedBoundingBox(const FTransform& InOBB, bool bInvertSelection, EFilterMode Mode = EFilterMode::FILTER_Or);

	/**
	 * Add a filter to this view that only includes points that are within of a given tile in a grid on a bounding box.
	 * @param QueryGridBounds - The bounding box that holds the grid to test against
	 * @param InNumTiles(X|Y|Z) - The number of tiles in each dimension
	 * @param InTile(X|Y|Z) - The tile index per dimension
	 * @param bInvertSection - If true, will include only points outside of the given tile
	 * @param Mode - How the results of this filter are combined with the result set. Allows inclusion, exclusion and intersection of matching results
	 */
	UFUNCTION(BlueprintCallable, Category = "PointCloudView|Filters")
	void FilterOnTile(const FBox& QueryGridBounds, int InNumTilesX, int InNumTilesY, int InNumTilesZ, int InTileX, int InTileY, int InTileZ, bool bInvertSelection, EFilterMode Mode = EFilterMode::FILTER_Or);

	/**
	 * Add a filter to this view that only includes points that are within of a given tile in the current results' bounding box.
	 * @param InNumTiles(X|Y|Z) - The number of tiles in each dimension
	 * @param InTile(X|Y|Z) - The tile index per dimension
	 * @param bInvertSection - If true, will include only points outside of the given tile
	 * @param Mode - How the results of this filter are combined with the result set. Allows inclusion, exclusion and intersection of matching results
	 */
	void FilterOnTile(int InNumTilesX, int InNumTilesY, int InNumTilesZ, int InTileX, int InTileY, int InTileZ, bool bInvertSelection, EFilterMode Mode = EFilterMode::FILTER_Or);

	/**
	* Add a filter to this view that only includes point if the are within a given bounding sphere
	* @param Center - The center of the bounding sphere
	* @param Radius - The Radius of the bounding sphere
	* @param Mode - How the results of this filter are combined with the result set. Allows inclusion, exclusion and intersection of matching results
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloudView|Filters")
	void FilterOnBoundingSphere(const FVector& Center, float Radius, EFilterMode Mode = EFilterMode::FILTER_Or);

	/**
	* Add a filter to this view that only includes point if point indexes are within a given range. Pass -1 for both StartIndex and EndIndex to return all points.
	* @param StartIndex - The start index of the range to test (-1) for no lower bound.
	* @param EndIndex - The end index of the range to test (-1) for no upper bound.
	* @param Mode - How the results of this filter are combined with the result set. Allows inclusion, exclusion and intersection of matching results
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloudView|Filters")
	void FilterOnRange(int32 StartIndex=-1, int32 EndIndex=-1, EFilterMode Mode = EFilterMode::FILTER_Or);


	/**
	* Add a filter to this view that only includes point if point indexes are within a given range. Pass -1 for both StartIndex and EndIndex to return all points.
	* @param StartIndex - The start index of the range to test (-1) for no lower bound.
	* @param EndIndex - The end index of the range to test (-1) for no upper bound.
	* @param Mode - How the results of this filter are combined with the result set. Allows inclusion, exclusion and intersection of matching results
	*/
	UFUNCTION(BlueprintCallable, Category = "PointCloudView|Filters")
	void FilterOnIndex(int32 Index=-1, EFilterMode Mode = EFilterMode::FILTER_Or);

public: 

	/** Returns the point cloud this view is associated to */
	UFUNCTION(BlueprintCallable, Category = "PointCloudView|Management")
	UPointCloud* GetPointCloud() const;
	
	/** Return the list of filter statements for this view and the associated, sanitized hashes for each Statement. 
	*/
	TArray<FString> GetFilterStatements() const;

protected:

	/**
	* Set the point cloud for this view. This should only be done by the construction point cloud passing in 'this'
	* @param InCloud - A pointer to the point cloud this view refers to
	*/
	void SetPointCloud(UPointCloudImpl* InCloud);

	/**
	 * Sets the parent view & point cloud. This should only be done by the construction point cloud view passing 'this'
	 * @param InParentView - A pointer to the parent view.
	 */
	void SetParentView(UPointCloudView* InParentView);

	/** Reset cached result hash, must be called after any transforming operation */
	void DirtyHash();

private:

	/** Returns whether this contains initialized views (incl. parent view) */
	bool HasFiltersApplied() const;

	/** Returns the number of views this contains (incl. parent view) */
	int GetFilterCount() const;
	
public:

	/** Return the table containing the results of the View */
	FString GetFilterResultTable(bool bSilentOnNoFilter = false) const;

	/** Precache the filter results */
	void PreCacheFilters();

private:

	/** Return the union of all of the Metadata queries */
	FString GetMetadataQuery() const; 

	/** Add a statement to the list of view creation statements. This will be added at the end of the list and executed after all previous entries
	* This should be a valid SQL statement. 
	*/
	void AddFilterStatement(const FString &Statement);

	/** Clear the list of create view statements */
	void ClearFilterStatements();

	/** Computes the hash of a query */
	FString HashQueryResults(const FString& Query) const;

	/** Performs value retrieval on templated type */
	template<typename T>
	TArray<T> GetMetadataValuesArray(const FString& Key) const;
	
private: /** Data */

	/** The View Guid is the unique identifier for this PointCloudView. It is used to name the internal database views and keep track of them */
	UPROPERTY()
	FGuid ViewGuid;
	
	/** A pointer to the point cloud this view refers to */
	UPROPERTY()
	TObjectPtr<UPointCloudImpl> PointCloud;

	/** A point to the parent view */
	UPROPERTY()
	TObjectPtr<UPointCloudView> ParentView;

	/** The array of Statements required to generate this view. As there are dependencies between the statements these should be executed in order */
	TArray<FString> FilterStatementList; 
	
	/** A flag to indicate if this view is in GetData State. */
	bool bInGetDataState;
	
	// Store a pointer to child views in a UProperty so that they don't get garbage collected while we are executing rules
	UPROPERTY(Transient)
	TSet<TObjectPtr<UPointCloudView>> ChildViews;

	FCriticalSection ChildViewsLock;

	/** Contains cached hash of current view results, or empty if not computed */
	mutable FString CachedResultHash;
};

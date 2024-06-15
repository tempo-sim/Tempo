// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PointCloud.h"
#include "PointCloudSqliteHelpers.h"
#include "PointCloudTablesCache.h"

#include "PointCloudImpl.generated.h"

class FPointCloudQuery;

namespace PointCloud
{
	struct QueryLogger;
}

/**
* Private implementation of the UPointCloud class
*/
UCLASS(meta = (DisplayName = "Point Cloud"))
class POINTCLOUD_API UPointCloudImpl : public UPointCloud
{
	GENERATED_BODY()

	friend class UPointCloudView;
	friend class FPointCloudQuery;
	friend struct PointCloud::QueryLogger;
	
	enum EPointCloudSchemaVersions
	{
		POINTCLOUD_VERSION_INVALID = 0,  // This is an invalid version number. Something is wrong with the point cloud
		POINTCLOUD_VERSION_1 = 1,  // The default schema version. This is implicit in PCs created before version 2
		POINTCLOUD_VERSION_2 = 2   // 2/9/2021 - Update to deduplicate metadata values and added Schema Versioning
		// NOTE : When adding new versions to this enum, make sure to update PointCloudSchemaVersion Below
	};

protected:

	UPointCloudImpl();
	~UPointCloudImpl();

public: // enums

	// This is used when passing a string to certain functions to indicate that the string represents either a table or a query
	enum class EArgumentType
	{
		Table,		// Argument is a table 
		Query		// Argument is query and contains a SELECT statement
	};

	static const EPointCloudSchemaVersions PointCloudSchemaVersion = EPointCloudSchemaVersions::POINTCLOUD_VERSION_2;

public:

	/**
	* Return true if this point cloud is initialized and is ready for use
	* @return True If this point cloud is initialized and ready for use
	*/
	virtual bool IsInitialized() const override;

	/**
	* Start a transaction, return true on success
	* @return True If a new transaction was started
	*/
	bool BeginTransaction();

	/**
	* End any current transaction, return true on success
	* @return True If a transaction was ended sucessfully
	*/
	bool EndTransaction();

	/**
	* Finish and rollback the current transaction
	* @return True If a transaction was ended sucessfully
	*/
	bool RollbackTransaction();

	/**
	* Return the schema version  for the current point cloud
	* @return The Schema version of the currently loaded point cloud
	*/
	EPointCloudSchemaVersions GetSchemaVersion() const;

	/**
	* Return the current latest schema version
	* @return The latest schema version number
	*/
	static EPointCloudSchemaVersions GetLatestSchemaVersion();

	/**
	* Attemp to convert this point cloud to the current schema
	* @return True If a transaction was ended sucessfully
	*/
	virtual bool AttemptToUpdate() override;

	/** Query if this point cloud is using an out of date schema and needs updating
	* @return True if this point cloud needs updating
	*/
	virtual bool NeedsUpdating() const override;

public: // ~ View interface

	/**
	* Create a new view onto this PointCloud Asset. The view is used to access data in the point cloud. Each view is indepdentant and can be used to filter and modify the data as it is read from the PointCloud.
	* The views do not modify the source data in the point cloud unless explicitly requested to. The view implements an override system on top of the PointCloud Data.
	* @return A point to a new UPointCloudView Object that references this PointCloud
	*/
	virtual UPointCloudView* MakeView() override;

public:	// ~ Attribute interface
		
	/**
	* Return the default attributes each item in the point cloud have by default. Data in the point cloud is divided into Default attributes and Metadata.
	* @return The list of attribute names that each item in the point cloud has by default
	*/
	virtual TArray<FString> GetDefaultAttributes() const override;

	/**
	* Return the names of the metadata items in this point cloud. Each point may have zero, one or more items of metadata associated with it.
	* Metadata is sparse and not all Points in the cloud may have each item of Metadata.
	* @return The list of Metadata names represented in this PointCloud
	*/
	virtual TSet<FString> GetMetadataAttributes() const override;

public:	// ~ Info interface

	// return the number of points in the point cloud 
	virtual int GetCount() const override;

	/**
	* Return the bounding box of all of the points in this PointCloud	
	* @return the bounding box that contains all of the points in this PointCloud
	*/
	virtual FBox GetBounds() const override;

public: // I/O Interface

	/**
	* Load a point cloud from a CSV file.
	* @return True if the file was loaded sucessfully, False on failure
	* @param Mode - Controls if any exisiting data apended or replaced by the contents of the CSV file
	* @param FileName - The path to the CSV file to load
	* @param InImportBounds - Optional bounding box to define an import zone. Only points within the Import zone will be imported
	* @param Warn - Pointer to a FeedbackContext item to provide updates during loading
	*/
	virtual bool LoadFromCsv(const FString& FileName, const FBox& InImportBounds = FBox(EForceInit::ForceInit), ELoadMode Mode = UPointCloud::REPLACE, FFeedbackContext* Warn = nullptr) override;

	/**
	* Load a point cloud from an Alembic file.
	* @return True if the file was loaded successfully, False on failure
	* @param Mode - Controls if any existing data appended or replaced by the contents of the Alembic file
	* @param FileName - The path to the Alembic file to load
	* @param ImportBounds - Optional bounding box to define an import zone. Only points within the Import zone will be imported
	* @param Warn - Pointer to a FeedbackContext item to provide updates during loading
	*/
	virtual bool LoadFromAlembic(const FString& FileName, const FBox& InImportBounds = FBox(EForceInit::ForceInit), ELoadMode Mode = UPointCloud::REPLACE, FFeedbackContext* Warn = nullptr) override;

	/**
	* Load a point cloud from structured points.
	* @return True if the data was loaded successfully, false otherwise
	* @param Points Array holding the points to add to the point cloud
	* @param ImportBounds Optional bounding box to define an import zone. Only points within the Import zone will be imported.
	* @param Warn - Pointer to a FeedbackContext item to provide updates during loading
	*/
	virtual bool LoadFromStructuredPoints(const TArray<FPointCloudPoint>& InPoints, const FBox& InImportBounds = FBox(EForceInit::ForceInit), FFeedbackContext* Warn = nullptr) override;

	/**
	* Save this PointCloud to a file on disk. This currently only saves .db files compatible with SQLLITE3. More formats may be added in the future. 
	* No name or extension checking is performed. You get what you ask for if the file can be written. 
	* @return True if the file could be saved to disk correctly, False if the saving failed
	* @param FileName - The name of the file to write
	*/
	virtual bool SaveToDisk(const FString& FileName) override;

	/**
	* Set a file name for an SQL Log. This records all of the queries executed by the database
	* @return True if the file name is valid, false otherwise
	* @param FileName - The name of the file to write
	*/
	bool SetSqlLog(const FString& FileName);

	/**
	* Call this function to start logging sql calls
	* @return True if logging can be started. This may return false if SetSqlLog has not been called with a valid log file name	
	*/
	virtual bool StartLogging(const FString& InFileName) override;

	/**
	* Call this function to start logging sql calls
	* @return True if logging can be started. This may return false if SetSqlLog has not been called with a valid log file name
	*/
	virtual bool StopLogging() override;

	/**
	* A point cloud may be composed of various different input files. This method returns the paths of the files loaded into this pointcloud
	* @return The list of paths of files that were loaded to make this pointcloud
	*/
	virtual TArray<FString> GetLoadedFiles() const override;

	/** Invalidate the WholeDb Hash. This will cause it to be recalculated the next time GetHashString() is called. This is expensive so don't do this unless you REALLY need to, and you know
	* that the Pointcloud database is out of date. In general this will be done automatically and you won't need to be conernced. You'll know if you've done something to invalidate the hash.
	*/
	void InvalidateHash();

	/** Return true if the whole DB hash for this object is invalid
	* @return True if the hash is currently invalid and needs to be recalculated
	*/
	bool IsHashInvalid() const;

	/** Return the string version of the whole database hash
	* @return A string containing the hash of the entire database
	*/
	FString GetHashAsString() const;

	/** Return the SHA256 hash of the entire database. This is calculated when requested and may be stored. Any insert or update calls to the database will invalidate this hash and
	* it will be recalculated. This is an expensive operation and shouldn't be done on frequently changing data
	* @return The hash of the entire database
	*/
	FSHA1 GetHash() const;

	/**
	* Serialize this pointcloud to and from an FArchive
	* @param Ar - A reference to the Archive to serialized to / from
	*/
	void Serialize(FArchive& Ar) override;

public: // ~ Query interface

	/**
	* Run a query over the database and return true if the query executed without error
	* @param Query - The SQL query to execute on this pointcloud
	* @param InOriginatingFile - The filename from which the query is called, used only if RULEPROCESSOR_ENABLE_LOGGING is defined, optional.
	* @param InOriginatingLine - The line from which the query is called, used only if RULEPROCESSOR_ENABLE_LOGGING is defined, optional.
	* @return True if query executed correctly
	*/
	bool RunQuery(const FString& Query, const FString& InOriginatingFile = FString(), const uint32 InOriginatingLine = 0);

	/**
	* Run a query over the database and return true if the query executed without error.
	* @param Query - The SQL query to execute on this pointcloud
	* @param Callback - A pointer to a function that will be called on each row returned from the query
	* @param UsrData - An optional pointer to some user data that will be passed into the callback function (can be null)
	* @param InOriginatingFile - The filename from which the query is called, used only if RULEPROCESSOR_ENABLE_LOGGING is defined, optional.
	* @param InOriginatingLine - The line from which the query is called, used only if RULEPROCESSOR_ENABLE_LOGGING is defined, optional.
	* @return True if query executed correctly
	*/
	bool RunQuery(const FString& Query, int (*Callback)(void*, int, char**, char**), void* UsrData, const FString& InOriginatingFile = FString(), const uint32 InOriginatingLine = 0);

private:

	/** Internal version of the RunQuery method that remove extraneous parameters */
	bool RunQueryInternal(const FString& Query);

	/** Internal version of the RunQuery method that remove extraneous parameters */
	bool RunQueryInternal(const FString& Query, int (*Callback)(void*, int, char**, char**), void* UsrData);

public:

	/**
	* Run a query over the database and return a single column value.
	* Note that only some types are supported (int, float, double, FString, FBox, FTransform, TArray)
	* @param Query - The SQL query to execute on this pointcloud
	* @param ColumnNames - List of columns to return; use multiple only if type is array.
	* @return The value found in the last row of the given column of the result set
	*/
	template<typename T>
	T GetValue(const FString& Query, const TArray<FString>& ColumnNames) const;

	/**
	* Run a query over the database and return a single column value.
	* Note that only some types are supported (int, float, double, FString, FBox, FTransform, TArray)
	* @param Query - The SQL query to execute on this pointcloud
	* @param ColumnName - Name of the column to return, if empty will return the first column.
	* @return The value found in the last row of the given column of the result set
	*/
	template<typename T>
	T GetValue(const FString& Query, const FString& ColumnName = FString()) const { return GetValue<T>(Query, TArray<FString>({ ColumnName })); }

	/**
	* Run a query over the database an array containing one entry per row.
	* Note that only some types are supported (int, float, double, FString, FBox, FTransform, TArray)
	* @param Query - The SQL query to execute on this pointcloud
	* @param ColumnNames - List of columns to return; use multiple only if type is array.
	* @return The values found in the given column(s) of the result set
	*/
	template<typename T>
	TArray<T> GetValueArray(const FString& Query, const TArray<FString>& ColumnNames) const;

	/**
	* Run a query over the database an array containing one entry per row.
	* Note that only some types are supported (int, float, double, FString, FBox, FTransform, TArray)
	* @param Query - The SQL query to execute on this pointcloud
	* @param ColumnName - Name of the column to return, if empty will return the first column.
	* @return The values found in the given column(s) of the result set
	*/
	template<typename T>
	TArray<T> GetValueArray(const FString& Query, const FString& ColumnName = FString()) const { return GetValueArray<T>(Query, TArray<FString>({ ColumnName })); }

	/**
	* Run a query over the database an array containing one pair per row.
	* Note that only some types are supported (int, float, double, FString, FBox, FTransform, TArray)
	* @param Query - The SQL query to execute on this pointcloud
	* @param ColumnNames - List of columns to return; use multiple only if type is array.
	* @return The values found in the given column(s) of the result set
	*/
	template<typename T, typename U>
	TArray<TPair<T, U>> GetValuePairArray(const FString& Query, const TArray<FString>& FirstColumnNames, const TArray<FString>& SecondColumnNames) const;

	/**
	* Run a query over the database an array containing one pair per row.
	* Note that only some types are supported (int, float, double, FString, FBox, FTransform, TArray)
	* @param Query - The SQL query to execute on this pointcloud
	* @param ColumnName - Name of the column to return, if empty will return the first column.
	* @return The values found in the given column(s) of the result set
	*/
	template<typename T, typename U>
	TArray<TPair<T, U>> GetValuePairArray(const FString& Query, const FString& FirstColumnName = FString(), const FString& SecondColumnName = FString()) const { return GetValuePairArray<T, U>(Query, TArray<FString>({ FirstColumnName }), TArray<FString>({ SecondColumnName })); }

	/**
	* Run a query over the database an array containing one entry in the map per row, assuming all keys are different
	* Note that only some types are supported (int, float, double, FString, FBox, FTransform, TArray)
	* @param Query - The SQL query to execute on this pointcloud
	* @param ColumnNames - List of columns to return; use multiple only if type is array.
	* @return The values found in the given column(s) of the result set
	*/
	template<typename T, typename U>
	TMap<T, U> GetValueMap(const FString& Query, const TArray<FString>& KeyNames, const TArray<FString>& ValueNames) const;

	/**
	* Run a query over the database an array containing one entry in the map per row, assuming all keys are different
	* Note that only some types are supported (int, float, double, FString, FBox, FTransform, TArray)
	* @param Query - The SQL query to execute on this pointcloud
	* @param ColumnName - Name of the column to return, if empty will return the first column.
	* @return The values found in the given column(s) of the result set
	*/
	template<typename T, typename U>
	TMap<T, U> GetValueMap(const FString& Query, const FString& KeyName = FString(), const FString& ValueName = FString()) const { return GetValueMap<T, U>(Query, TArray<FString>({ KeyName }), TArray<FString>({ ValueName })); }

private:

	/** Generic method to get values from a query. Contains all the common boilerplate, but the helper functions do the retrieval */
	void GetValues(const FString& Query, const TArray<FString>& ColumnNames, TFunction<void(sqlite3_stmt*, int*)> Retrieval) const;

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
	virtual bool InitFromPreparedData(const FString&						ObjectName,
								TArray<FTransform>&					PreparedTransforms,
								TArray<FString>&					MetadataColumnNames,
								TArray<int>&						MetadataCountPerVertex,
								TArray< TPair<int, FString> >&		PreparedMetadata,
								const FBox							&ImportBounds,
								FFeedbackContext*					Warn = nullptr
	) override;

	/**
	* Clear any temporary Tables
	*/
	void ClearTemporaryTables();

	/** Return information about the temporary table cache misses
	* @return An array of <TableName, Cache Miss Count> Pairs
	*/
	TArray<TPair<FString, int32>> GetQueryCacheMissCounts() const;

public:

	/** Return the number of temporary tables to keep around, this controls the size of TemporaryTables
	* @return - The number of temporary tables to cache in the LRU
	*/
	static int32 GetTemporaryTableCacheSize();

	/** PointCloud calls Optimize periodically to optimize temporary table usage. This method returns how many tables need to be created for
	* an optimize run to occur. Well optimized tables are quicker, but optimizing is costly.
	*/
	static uint32 GetTemporaryTableOptimizeFrequency();

	/** Calculating indexes on Temporary tables is expensive and there are cases when it is not needed. This number controls how many times a table
	* needs to be used before an index is calculated. Higher numbers reduces the index cost but can slow down queries.
	*/
	static uint32 GetCacheHitBeforeIndexCount();

private:

	/**
	* Private method for reloading points from files
	*/
	virtual bool ReloadInternal(const TArray<FString >& Files, const FBox& ReimportBounds) override;

	/**
	* Run any required optimizations on the database
	*/
	void OptimizeIfRequired();

	/**
	* Initialize the internal DB. This should only be called once. It will fail if called on an pre-initialized database
	*/
	void InitDb();

	/**
	* Copy the Internal Database into the Serialized Version. Used as part of the internal serialization process
	*/
	void SerializeDb(FArchive& Ar);

	/**
	* Copy the Serialized database into the internal; Used as part of the internal serialization process
	*/
	void DeSerializeDb(FArchive& Ar);

	/**
	* Setup the schema on the database, return true on success
	* @return True if the internal database was initialized correctly with the schema
	*/
	bool SetupSchema();

	/** Internal Method to Update PCs from Schema Version 1 to 2*/
	bool UpdateFromSchemaVersionOneToVersionTwo();

	/**
	* Helper function to sanitize and escape strings correctly for insertion in the database
	* @param InString - The string to sanitize
	* @return A version of the string with correct escaping applied
	*/
	FString SanitizeAndEscapeString(const FString& InString) const;

	/**
	* Calculate the whole DB Hash
	* @param Data - A pointer to the serialized data for the this asset
	* @param Size - The size in bytes of the buffer pointer to by Data
	*/
	void CalculateWholeDbHash(void* Data = nullptr, uint64 Size = 0) const;

private:

	using LogEntry = TTuple<FString, uint32>;

	struct LogRecord
	{
		size_t  Calls;
		float   CumulativeTime;
		FString Query;
	};

	LogEntry LogSql(const FString& FileName, uint32 Line, const FString& Query) const;
	bool	 SetTiming(const LogEntry& Entry, float Time) const;

	/**
	* Make a temporary lookup table from Vertex Id to the given Metadata Key
	* @param MetadataKey - The Metadata Key to create the lookup table for
	* @return The name of the Temporary Lookup table on success, or false otherwise
	*/
	FString GetTemporaryAttributeTable(const FString& MetadataKey);

	/**
	* Make a temporary lookup table from A user supplied query
	* @param Query - The statement that will be executed to create the temporary table
	* @return The name of the Temporary Lookup table on success, or false otherwise
	*/
	FString GetTemporaryQueryTable(const FString& Query);

	/** Make a temporary table from either the inner join of two tables
	*
	* @param ArgumentA		The first table in operation
	* @param ArgumentAType Flag Indicating if this argument A is a Table or query
	* @param ArgumentBType Flag Indicating if this argument B is a Table or query
	* @param ArgumentB		The second table in the operation
	* @return The name of the temporary table caching the results
	*/
	FString GetTemporaryIntersectionTable(EArgumentType ArgumentAType, const FString& ArgumentA, EArgumentType ArgumentBType, const FString& ArgumentB);

	/**
	* Query if a temporary lookup table exists for a given Metadata key
	* @param MetadataKey - The Metadata Key to query
	* @return True if there is a temporary lookup table for the given Metadata Key
	*/
	bool HasTemporaryTable(const FString& Key) const;

	/** Once a temporary table is created this method adds it to the LRU and performs any cache management that is needed
	* @param Key - The key associated with the temporary table
	* @param Value - The name of the temporary table
	*/
	void AddTemporaryTable(const FString& Key, const FString& Name);

private: // Data Section

	// This is set to true if the pointcloud is already in a BeginTransaction without a matching EndTransaction. Used to detect nested transactions
	bool bInTransaction;

	// The point cloud uses sqlite3 internally to store data. This is a handle to the sqlite3 context for this db. 
	sqlite3* InternalDatabase;

	// This holds the SHA256 hash of the entire database		
	mutable FSHA1 WholeDbHash;

	// A handle to the log file to which SQL queries should be written
	mutable IFileHandle* LogFile;

	// A record of the queries executed, the line, filename, cumulative calls and call count
	mutable TMap<LogEntry, LogRecord> LogRecords;

	// the current schema version. This is set on creation, loading or conversion
	mutable EPointCloudSchemaVersions SchemaVersion;

	// cache of the Metadata attributes available on this point cloud
	mutable TSet<FString> MetadataAttributeCache;

	// The number of temporary tables added since the last optimize call. This is compared against the 
	// value returned from GetTemporaryTableOptimizeFrequency to decide when a optimize run is needed
	std::atomic<uint32> NumTablesSinceOptimize;

	/** Thread-safe cache for temporary table names in the DB */
	FPointCloudTemporaryTablesCache TemporaryTables;
};

// Template implementations
template<typename T>
T UPointCloudImpl::GetValue(const FString& Query, const TArray<FString>& ColumnNames) const
{
	T Value = T();
	GetValues(Query, ColumnNames, [&Value, &ColumnNames](sqlite3_stmt* stmt, int* ColumnIndices) {
		int ReadColumns = 0;
		PointCloudSqliteHelpers::ResultRetrieval(stmt, ColumnNames.Num(), ColumnIndices, ReadColumns, Value);
		});
	return Value;
}

template<typename T>
TArray<T> UPointCloudImpl::GetValueArray(const FString& Query, const TArray<FString>& ColumnNames) const
{
	TArray<T> Values;
	GetValues(Query, ColumnNames, [&Values, &ColumnNames](sqlite3_stmt* stmt, int* ColumnIndices) {
		int ReadColumns = 0;
		PointCloudSqliteHelpers::ResultRetrieval(stmt, ColumnNames.Num(), ColumnIndices, ReadColumns, Values.Emplace_GetRef());
		});
	return Values;
}

template<typename T, typename U>
TArray<TPair<T, U>> UPointCloudImpl::GetValuePairArray(const FString& Query, const TArray<FString>& FirstColumnNames, const TArray<FString>& SecondColumnNames) const
{
	TArray<FString> MergedColumnNames = FirstColumnNames;
	MergedColumnNames.Append(SecondColumnNames);

	TArray<TPair<T, U>> Values;
	GetValues(Query, MergedColumnNames, [&Values, &FirstColumnNames, &SecondColumnNames](sqlite3_stmt* stmt, int* ColumnIndices) {
		int ReadColumns = 0;
		TPair<T, U>& Value = Values.Emplace_GetRef();
		PointCloudSqliteHelpers::ResultRetrieval(stmt, FirstColumnNames.Num(), ColumnIndices, ReadColumns, Value.Key);
		PointCloudSqliteHelpers::ResultRetrieval(stmt, SecondColumnNames.Num(), ColumnIndices + FirstColumnNames.Num(), ReadColumns, Value.Value);
		});
	return Values;
}

template<typename T, typename U>
TMap<T, U> UPointCloudImpl::GetValueMap(const FString& Query, const TArray<FString>& KeyNames, const TArray<FString>& ValueNames) const
{
	TArray<FString> MergedColumnNames = KeyNames;
	MergedColumnNames.Append(ValueNames);

	TMap<T, U> Values;
	GetValues(Query, MergedColumnNames, [&Values, &KeyNames, &ValueNames](sqlite3_stmt* stmt, int* ColumnIndices) {
		int ReadColumns = 0;
		T Key;
		PointCloudSqliteHelpers::ResultRetrieval(stmt, KeyNames.Num(), ColumnIndices, ReadColumns, Key);

		U Value;
		PointCloudSqliteHelpers::ResultRetrieval(stmt, ValueNames.Num(), ColumnIndices + KeyNames.Num(), ReadColumns, Value);

		Values.Add(Key, Value);
		});

	return Values;
}
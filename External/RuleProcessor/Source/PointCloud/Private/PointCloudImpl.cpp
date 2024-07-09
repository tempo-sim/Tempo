// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudImpl.h"

#include "Algo/AnyOf.h"
#include "HAL/PlatformFileManager.h"
#include "IncludeSQLite.h"
#include "Misc/FeedbackContext.h"
#include "Misc/Paths.h"
#include "PointCloudAlembicHelpers.h"
#include "PointCloudCsv.h"
#include "PointCloudQuery.h"
#include "PointCloudSchema.h"
#include "PointCloudSQLExtensions.h"
#include "PointCloudTransactionHolder.h"
#include "PointCloudUtils.h"
#include "Runtime/Core/Public/Async/ParallelFor.h"
#include "Runtime/Core/Public/Misc/SecureHash.h"
#include "UObject/UE5MainStreamObjectVersion.h"

#if WITH_EDITOR
THIRD_PARTY_INCLUDES_START
#include "Alembic/AbcGeom/All.h"
#include "Alembic/AbcCoreFactory/IFactory.h"
THIRD_PARTY_INCLUDES_END
#endif 

DEFINE_LOG_CATEGORY(PointCloudLog)
#define LOCTEXT_NAMESPACE "PointCloudImpl"

// Convenience macros
#define RUN_QUERY(Query) RunQuery(Query, __FILE__, __LINE__)
#define RUN_QUERY_P(PointCloud, Query) PointCloud->RunQuery(Query, __FILE__, __LINE__)
#define LOG_QUERY(Query) PointCloud::QueryLogger Logger(this, Query, FString(), __FILE__, __LINE__)
#define LOG_QUERY_LABEL(Query, Label) PointCloud::QueryLogger Logger(this, Query, Label, __FILE__, __LINE__)

namespace
{
	void UpdateProgress(FFeedbackContext* Warn, int ActualProgress, int ExpectedTotal)
	{
		if (Warn)
		{
			Warn->UpdateProgress(ActualProgress, ExpectedTotal);
		}
	}
}

namespace PointCloudPrivateNamespace
{
#if WITH_EDITOR

	// This method will move data out of the incoming FPointCloudCsv and into the outgoing array. The 
	// columns in the doc can potentially be very large and saving a copy here is useful.
	// It should be assumed that once this called, the doc no longer contains data in the given column
	bool TryTakeColumn(FPointCloudCsv& doc,
		const FString& InColumnName,
		const FString& OutColumName,
		TMap<FString, TArray<FString> >& OutValues)
	{
		TArray<FString>* Column = doc.GetColumn(InColumnName);
		if (Column != nullptr)
		{
			OutValues.Add(OutColumName, MoveTemp(*Column));
			return true;
		}
		else
		{
			return false;
		}
	}
#endif

	FString SanitizeTableName(const FString& InTableName)
	{
		// Hash the string and return the hashed name
		uint32 Hash = GetTypeHash(InTableName);
		return FString::Printf(TEXT("%u"), Hash);
	}

	// Drop any indexs on the point cloud, this should be done before bulk inserts
	void DropIndexes(UPointCloudImpl* PointCloud)
	{
		if (PointCloud == nullptr)
		{
			return;
		}

		RUN_QUERY_P(PointCloud, "DROP TABLE IF EXISTS SpatialQuery;");
		RUN_QUERY_P(PointCloud, "DROP INDEX IF EXISTS PointIndex;");
		RUN_QUERY_P(PointCloud, "DROP INDEX IF EXISTS VertexToAttribIndex");
		RUN_QUERY_P(PointCloud, "DROP INDEX IF EXISTS ValueIndex");
		RUN_QUERY_P(PointCloud, "DROP INDEX IF EXISTS VertexToAttribIndexInv");
		RUN_QUERY_P(PointCloud, "DROP INDEX IF EXISTS VertexKeytoValue");
		RUN_QUERY_P(PointCloud, "DROP INDEX IF EXISTS VertexToValue");
		RUN_QUERY_P(PointCloud, "DROP INDEX IF EXISTS VertexToKey");

		PointCloud->ClearTemporaryTables();
	}

	// create any required indexes
	void CreateIndexes(UPointCloudImpl* PointCloud, FFeedbackContext* Warn = nullptr)
	{
		PointCloud::UtilityTimer Timer;
		if (PointCloud == nullptr)
		{
			return;
		}

		RUN_QUERY_P(PointCloud, "CREATE VIRTUAL TABLE if not exists SpatialQuery USING rtree(id, Minx , Maxx , Miny , Maxy , Minz, Maxz);");
		UpdateProgress(Warn, 80, 100);

		RUN_QUERY_P(PointCloud, "INSERT INTO SpatialQuery SELECT rowid, x, x, y, y, z, z from Vertex");
		UpdateProgress(Warn, 85, 100);

		Timer.Report("Build Spatial Index");

		RUN_QUERY_P(PointCloud, "CREATE INDEX VertexKeytoValue 	ON VertexToAttribute(key_id, value_id)");
		UpdateProgress(Warn, 90, 100);

		RUN_QUERY_P(PointCloud, "CREATE INDEX VertexToValue 	ON VertexToAttribute(vertex_id, value_id)");
		UpdateProgress(Warn, 93, 100);

		RUN_QUERY_P(PointCloud, "CREATE INDEX VertexToKey 	ON VertexToAttribute(vertex_id, key_id)");
		UpdateProgress(Warn, 100, 100);

		Timer.Report("Create Indexes");
	}
}

uint32 UPointCloudImpl::GetTemporaryTableOptimizeFrequency()
{
	// magic number warning. This maybe become user configurable at some point, hence using a static method rather than a const int or similar
	// the number below controls how frequently Optimize is called on the database. 
	return 100;
}

FString UPointCloudImpl::GetTemporaryIntersectionTable(EArgumentType ArgumentAType, const FString& ArgumentA, EArgumentType ArgumentBType, const FString& ArgumentB)
{

	const FString TableNameA = ArgumentAType == EArgumentType::Table ? ArgumentA : GetTemporaryQueryTable(ArgumentA);
	const FString TableNameB = ArgumentBType == EArgumentType::Table ? ArgumentB : GetTemporaryQueryTable(ArgumentB);

	if (TableNameA == TableNameB)
	{
		// If we're asking for the interesction of the table and itself, just return the first table
		return TableNameA;
	}

	const FString UnionQuery = FString::Printf(TEXT("SELECT %s.ID FROM %s INNER JOIN %s on %s.ID = %s.ID"), *TableNameA, *TableNameA, *TableNameB, *TableNameA, *TableNameB);

	return GetTemporaryQueryTable(UnionQuery);
}

TArray<TPair<FString, int32>>  UPointCloudImpl::GetQueryCacheMissCounts() const
{
	TArray< TPair<FString, int32> > Result;
#if defined RULEPROCESSOR_ENABLE_LOGGING
	for (const auto& Record : TemporaryTables.GetCacheMisses())
	{
		if (Record.Value > 1)
		{
			Result.Add(TPair<FString, int32>(Record.Key, Record.Value));
		}
	}

	Result.Sort([](const TPair<FString, int32>& ip1, const TPair<FString, int32>& ip2) {
		return  ip1.Value < ip2.Value;
		});
#endif 

	return Result;
}

uint32 UPointCloudImpl::GetCacheHitBeforeIndexCount()
{
	// Magic number alert. This seems to be a good trade off between index creation and query speed.
	// Down the line we should add hinting to the temporary table creation to indicate the access pattern
	// the table will be used with (linear scan, random access) and use that to drive the index creation
	// at that point this will go away
	return 3;
}

FString UPointCloudImpl::GetTemporaryQueryTable(const FString& Query)
{
	// hash the string to find a unique ID
	const FString SanitizedQuery = PointCloudPrivateNamespace::SanitizeTableName(Query);
	const FString KeyName = FString::Printf(TEXT("QUERY_TABLE_%s"), *SanitizedQuery);
	const FString TempName = "Temp_" + KeyName + "_Table";

	// Check if the table already exists
	int32 CacheHitCount = 0;
	FString CachedTableName = TemporaryTables.GetFromCache(KeyName, &CacheHitCount);

	// Build index if needed (note: if cache hit count != 0 then the table already exists)
	if (CacheHitCount == GetCacheHitBeforeIndexCount())
	{
		const FString IndexName = "Temp_" + KeyName + "_Index";
		const FString CreateIndexQuery = FString::Printf(TEXT("CREATE INDEX IF NOT EXISTS %s ON %s(ID);"), *IndexName, *TempName);
		if (RUN_QUERY(CreateIndexQuery) == false)
		{
			UE_LOG(PointCloudLog, Log, TEXT("Cannot create index on temporary table for query %s"), *Query);
		}
	}

	// If table already exists, just return that
	if (!CachedTableName.IsEmpty())
	{
		return CachedTableName;
	}

	// Otherwise, create the table
	const FString CreateTableQuery = FString::Printf(TEXT("CREATE TEMPORARY TABLE IF NOT EXISTS %s AS %s"), *TempName, *Query);
	if (RUN_QUERY(CreateTableQuery) == false)
	{
		return FString();
	}

	AddTemporaryTable(KeyName, TempName);

	return TempName;
}

void UPointCloudImpl::AddTemporaryTable(const FString& Key, const FString& Name)
{
	check(Key.IsEmpty() == false);
	check(Name.IsEmpty() == false);

	FString TableToDrop = TemporaryTables.AddToCache(Key, Name);

	if (!TableToDrop.IsEmpty())
	{
		FString DeleteTableQuery = FString::Printf(TEXT("DROP TABLE IF EXISTS %s"), *TableToDrop);
		RUN_QUERY(DeleteTableQuery);
	}

	if (NumTablesSinceOptimize++ > GetTemporaryTableOptimizeFrequency())
	{
		FString Analyze = FString::Printf(TEXT("PRAGMA optimize"));
		RUN_QUERY(Analyze);
		NumTablesSinceOptimize = 0;
	}
}

FString UPointCloudImpl::GetTemporaryAttributeTable(const FString& MetadataKey)
{
	FString CachedTableName = TemporaryTables.GetFromCache(MetadataKey);
	if (!CachedTableName.IsEmpty())
	{
		// Table already exists, just return that
		return CachedTableName;
	}

	if (HasMetaDataAttribute(MetadataKey) == false)
	{
		UE_LOG(PointCloudLog, Log, TEXT("Cannot find MetadataKet %s to create temporary table"), *MetadataKey);
		return FString();
	}

	FString TempName = "Temp_" + PointCloudPrivateNamespace::SanitizeTableName(MetadataKey) + "_Table";
	FString IndexName = "Temp_" + PointCloudPrivateNamespace::SanitizeTableName(MetadataKey) + "_Index";

	FString GetAttributeQuery = FString::Printf(TEXT("SELECT rowid AS ID from AttributeKeys where AttributeKeys.Name = \'%s\'"), *MetadataKey);
	int MetadataIndex = GetValue<int>(GetAttributeQuery, "ID");

	FString CreateTableQuery = FString::Printf(TEXT("CREATE  TEMPORARY TABLE IF NOT EXISTS %s AS Select VertexToAttribute.vertex_id as Id, VertexToAttribute.value_id as ValueId From VertexToAttribute where key_id=%d"), *TempName, MetadataIndex);
	RUN_QUERY(CreateTableQuery);

	FString CreateIndexQuery = FString::Printf(TEXT("CREATE INDEX IF NOT EXISTS %s ON %s(ID,ValueId);"), *IndexName, *TempName);
	RUN_QUERY(CreateIndexQuery);

	FString Analyze = FString::Printf(TEXT("ANALYZE %s"), *TempName);
	RUN_QUERY(Analyze);

	AddTemporaryTable(MetadataKey, TempName);

	return TempName;
}

bool UPointCloudImpl::HasTemporaryTable(const FString& MetadataKey) const
{
	return TemporaryTables.Contains(MetadataKey);
}

void UPointCloudImpl::ClearTemporaryTables()
{
	bool bContinueCleanup = true;

	while (bContinueCleanup)
	{
		FString TableName = TemporaryTables.RemoveLeastRecentNotThreadSafe();

		if (TableName.IsEmpty())
		{
			bContinueCleanup = false;
		}
		else
		{
			FString DeleteTableQuery = FString::Printf(TEXT("DROP TABLE IF EXISTS %s"), *TableName);
			RUN_QUERY(DeleteTableQuery);
		}
	}
}

UPointCloudImpl::UPointCloudImpl() : bInTransaction(false), InternalDatabase(nullptr)
{
	LogFile = nullptr;
	NumTablesSinceOptimize = 0;
	SchemaVersion = EPointCloudSchemaVersions::POINTCLOUD_VERSION_INVALID;
	InvalidateHash();

#if WITH_EDITOR
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		InitDb();
	}
#endif
}

UPointCloudImpl::EPointCloudSchemaVersions UPointCloudImpl::GetSchemaVersion() const
{
	if (IsInitialized() == false)
	{
		return EPointCloudSchemaVersions::POINTCLOUD_VERSION_INVALID;
	}

	int Value = 0;
	Value = GetValue<int>("PRAGMA user_version", "user_version");

	switch (Value)
	{
	case 0:
		// if the user version isn't set, default to 1;
		SchemaVersion = EPointCloudSchemaVersions::POINTCLOUD_VERSION_1;
		break;
	case 1:
		// There shouldn't be any with version 1, but just for completeness
		SchemaVersion = EPointCloudSchemaVersions::POINTCLOUD_VERSION_1;
		break;
	case 2:
		SchemaVersion = EPointCloudSchemaVersions::POINTCLOUD_VERSION_2;
		break;
	default:
		SchemaVersion = EPointCloudSchemaVersions::POINTCLOUD_VERSION_INVALID;
		break;
	}

	return SchemaVersion;
}

UPointCloudImpl::EPointCloudSchemaVersions UPointCloudImpl::GetLatestSchemaVersion()
{
	return EPointCloudSchemaVersions::POINTCLOUD_VERSION_2;
}

bool UPointCloudImpl::NeedsUpdating() const
{
	return GetSchemaVersion() != GetLatestSchemaVersion();
}

bool UPointCloudImpl::UpdateFromSchemaVersionOneToVersionTwo()
{
	check(GetSchemaVersion() == EPointCloudSchemaVersions::POINTCLOUD_VERSION_1);
	PointCloudPrivateNamespace::DropIndexes(this);

	RUN_QUERY(PointCloud::ConvertFromSchemaOneToTwoQuery);

	PointCloudPrivateNamespace::CreateIndexes(this);
	OptimizeIfRequired();

	SchemaVersion = EPointCloudSchemaVersions::POINTCLOUD_VERSION_2;

	MarkPackageDirty();

	return true;
}

bool UPointCloudImpl::AttemptToUpdate()
{
	if (NeedsUpdating() == false)
	{
		// The point cloud is at the latest version, nothing to do
		UE_LOG(PointCloudLog, Warning, TEXT("Point Cloud Does Not Need Updating"));
		return true;
	}

	switch (GetSchemaVersion())
	{
	case EPointCloudSchemaVersions::POINTCLOUD_VERSION_1:
		// Convert from one to 2 
		UE_LOG(PointCloudLog, Warning, TEXT("Attempting to convert from Schema Version 1 to Schema Version 2"));
		return UpdateFromSchemaVersionOneToVersionTwo();
	default:
		UE_LOG(PointCloudLog, Warning, TEXT("Unkown Schema Version"));
		// cannot convert from the get version
		return false;
	}
}

UPointCloudImpl::~UPointCloudImpl()
{
	if (InternalDatabase)
	{
		sqlite3_close(InternalDatabase);
	}
}

// start a transaction, return true on sucess
bool UPointCloudImpl::BeginTransaction()
{
	if (InternalDatabase == nullptr)
	{
		return false;
	}

	if (bInTransaction)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Already in a Transaction"));
		return false;
	}

	bInTransaction = true;

	bool ReturnValue = RUN_QUERY("BEGIN TRANSACTION");

	if (ReturnValue)
	{
		bInTransaction = true;
	}

	return ReturnValue;
}

// Invalidate the WholeDb Hash
void UPointCloudImpl::InvalidateHash()
{
	WholeDbHash.Reset();
}

bool UPointCloudImpl::IsHashInvalid() const
{
	for (int i = 0; i < WholeDbHash.DigestSize; i++)
	{
		if (WholeDbHash.m_digest[i] != 0)
		{
			// non zero values means this is a valid hash
			return false;
		}
	}
	// all values are zero, this is an invalid hash
	return true;
}

// recalculate the whole DB Hash
void UPointCloudImpl::CalculateWholeDbHash(void* Data, uint64 Size) const
{
	if (!IsInitialized())
	{
		return;
	}

	// calculate the DB hash
	if (IsHashInvalid())
	{
		if (Data)
		{
			WholeDbHash.Update((const uint8*)Data, Size);
			WholeDbHash.Final();
		}
		else
		{
			// the calling function has not provided us with any data, derialize the database and use that data for the hash

			sqlite3_int64 piSize = 0;

			unsigned char* SerializedData = sqlite3_serialize(
				InternalDatabase,           /* The database connection */
				"main",						/* Which DB to serialize. ex: "main", "temp", ... */
				&piSize,					/* Write size of the DB here, if not NULL */
				0							/* Zero or more SQLITE_SERIALIZE_* flags */
			);

			if (SerializedData != nullptr && piSize != 0)
			{
				CalculateWholeDbHash(SerializedData, piSize);
			}

			sqlite3_free((void*)SerializedData);

		}
	}
}

FString UPointCloudImpl::GetHashAsString() const
{
	FString Out;

	for (int i = 0; i < WholeDbHash.DigestSize; i++)
	{
		Out.Append(FString::Printf(TEXT("%x"), WholeDbHash.m_digest[i]));
	}

	return Out;
}

FSHA1 UPointCloudImpl::GetHash() const
{
	// call recaluldate hash if required
	CalculateWholeDbHash();
	return WholeDbHash;
}

bool UPointCloudImpl::RollbackTransaction()
{
	if (IsInitialized() == false)
	{
		return false;
	}

	if (!bInTransaction)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Not in a Transaction"));
		return false;
	}

	bool ReturnValue = RUN_QUERY("ROLLBACK TRANSACTION");

	bInTransaction = false;

	return ReturnValue;
}

// end the current transaction, return true on sucess
bool UPointCloudImpl::EndTransaction()
{
	if (IsInitialized() == false)
	{
		return false;
	}

	if (!bInTransaction)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Not in a Transaction"));
		return false;
	}

	bool ReturnValue = RUN_QUERY("END TRANSACTION");

	bInTransaction = false;

	return ReturnValue;
}

bool UPointCloudImpl::IsInitialized() const
{
	return (InternalDatabase != nullptr);
}

UPointCloudView* UPointCloudImpl::MakeView()
{
	if (SchemaVersion < GetLatestSchemaVersion())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("PointCloud Schmea Version Out Of Date, Try Updating. Version=%d Latest=%d"), SchemaVersion, GetLatestSchemaVersion());
		return nullptr;
	}

	UPointCloudView* NewView = NewObject<UPointCloudView>();
	NewView->SetPointCloud(this);

	// add the new view to a UProperty container so that it doesn't immediately get garbage collected.
	RootViews.Add(NewView);

	return NewView;
}

FBox UPointCloudImpl::GetBounds() const
{
	if (!IsInitialized())
	{
		return FBox(EForceInit::ForceInit);
	}

	FBox Bounds = SQLExtension::query_rtree_bbox(InternalDatabase, "SpatialQuery");

	return Bounds;
}

int UPointCloudImpl::GetCount() const
{
	if (!IsInitialized())
	{
		return 0;
	}
	int Count = GetValue<int>("SELECT COUNT(*) FROM VERTEX", "COUNT(*)");

	return Count;
}

FString UPointCloudImpl::SanitizeAndEscapeString(const FString& InString) const
{
	FString EscapedNewValue = InString.Replace(TEXT("\'"), TEXT("\'\'"));
	return EscapedNewValue;
}

bool UPointCloudImpl::InitFromPreparedData(const FString& ObjectName,
	TArray<FTransform>& PreparedTransforms,
	TArray<FString>& MetadataColumnNames,
	TArray<int>& MetadataCountPerVertex,
	TArray< TPair<int, FString> >& PreparedMetadata,
	const FBox& ImportBounds,
	FFeedbackContext* Warn)
{
	// Clear the MetadataAttributeCache
	MetadataAttributeCache.Empty();

	if (PreparedTransforms.Num() == 0)
	{
		return false;
	}

	// Check that we have the right number of Metadata points
	if (PreparedTransforms.Num() != MetadataCountPerVertex.Num())
	{
		UE_LOG(PointCloudLog, Log, TEXT("Incorrect number of metadata entries %d vs %d expected Points\n"), MetadataCountPerVertex.Num(), PreparedTransforms.Num());
		return false;
	}

	PointCloud::UtilityTimer Timer;

	InvalidateHash();

	FPointCloudTransactionHolder Holder(this);

	RUN_QUERY(FString::Printf(TEXT("INSERT INTO Object VALUES(\"%s\"); "), *ObjectName));

	PointCloudPrivateNamespace::DropIndexes(this);

	for (const FString& Name : MetadataColumnNames)
	{
		if (RUN_QUERY(FString::Printf(TEXT("INSERT INTO AttributeKeys(Name) VALUES('%s');"), *Name)) == false)
		{
			Holder.RollBack();
			return false;
		}
	}

	uint32 index = 0;

	int Count = PreparedTransforms.Num();
	int NumMetadataColumns = MetadataColumnNames.Num();

	FPointCloudQuery InsertVertexQuery(this);
	FPointCloudQuery InsertAttributeQuery(this);
	FPointCloudQuery VertexToAttributeQuery(this);

	FString Query;

	FString GetObjectIdQuery = FString::Printf(TEXT("SELECT rowid as ID from Object where Name=\"%s\""), *ObjectName);
	FString ObjectId = GetValue<FString>(GetObjectIdQuery, "ID");

	Query += FString::Printf(TEXT("INSERT INTO Vertex(ObjectId, x,y,z,nx,ny,nz,nw,u,v,sx,sy,sz)  VALUES"));
	Query += FString::Printf(TEXT("( %s, ?,?,?,?,?,?,?,0,0,?,?,?)"), *ObjectId);
	InsertVertexQuery.SetQuery(Query);

	Query = FString::Printf(TEXT("INSERT OR IGNORE INTO AttributeValues VALUES(?);"));
	InsertAttributeQuery.SetQuery(Query);

	Query = FString::Printf(TEXT("INSERT INTO VertexToAttribute(vertex_id, key_id, value_id) VALUES(?,?,?)"));
	VertexToAttributeQuery.SetQuery(Query);

	TArray<float> VertexValues;
	VertexValues.SetNum(10);

	// We now need to update the Key Ids in the incoming Metadata to refer to the DB ID's returned after inserting the attribute Keys
	TMap<FString, FString> AttributeKeys = GetValueMap<FString, FString>("SELECT rowid as ID,Name from AttributeKeys", "Name", "ID");

	// Preconvert the String ID into an int so we don't have to do it in the loop
	TMap<FString, int> AttributeKeysIndex;

	// Make a map from KeyName to Index In Database
	for (const auto& a : AttributeKeys)
	{
		AttributeKeysIndex.Add(a.Key, FCString::Atoi(*a.Value));
	}

	PointCloud::UtilityTimer InsertTimer;

	// find the set of unique Metadata values
	TSet<FString> MetadataValueSet;
	for (const auto& Elem : PreparedMetadata)
	{
		MetadataValueSet.Add(Elem.Value);
	}

	InsertAttributeQuery.Begin();
	// now insert all of the Metadata values	
	for (const FString& Value : MetadataValueSet)
	{
		FTCHARToUTF8 EchoStrUtf8(*Value);
		TArray<char> UTF8Value(EchoStrUtf8.Get(), EchoStrUtf8.Length() + 1);
		InsertAttributeQuery.Step(UTF8Value);
	}
	InsertAttributeQuery.End();

	// Get the unique metadata DB ID's after inserting them all
	TMap<FString, FString> ValueKeys = GetValueMap<FString, FString>("SELECT rowid as ID,Value from AttributeValues", "Value", "ID");

	// Preconvert the String ID into an int so we don't have to do it in the loop
	TMap<FString, int> ValueKeysIndex;

	// Make a map from KeyName to Index In Database
	for (const auto& a : ValueKeys)
	{
		ValueKeysIndex.Add(a.Key, FCString::Atoi(*a.Value));
	}

	// Convert the incoming Metadata Key Ids and Values from those given, mapping to the incoming column names and values, to the IDs as stored in the DB
	TArray<TPair<int, int>> PreparedMetadataIndices;
	PreparedMetadataIndices.SetNum(PreparedMetadata.Num()); // we may not use all of these, but we want the elements to line up with the PreparedMetadata elements
	ParallelFor(PreparedMetadata.Num(), [&](int32 i)
		{
			int LocalColumnKey = PreparedMetadata[i].Key;
			const FString& IncomingColumnName = MetadataColumnNames[LocalColumnKey];
			int ColumnNameIndexInDb = AttributeKeysIndex[IncomingColumnName];
			PreparedMetadataIndices[i].Key = ColumnNameIndexInDb;

			const FString& IncomingValue = PreparedMetadata[i].Value;
			int ValueIndexInDb = ValueKeysIndex[IncomingValue];
			PreparedMetadataIndices[i].Value = ValueIndexInDb;
		});

	int CurrentProgress = 40;
	int ProgressShare = 30;
	int UpdateFreq = Count / ProgressShare;

	if (UpdateFreq == 0)
	{
		UpdateFreq = 1;
	}

	int CurrentTopVertexRowId = GetValue<int>("SELECT Max(rowid) from Vertex");
	int CurrentMetadataIndex = 0;

	InsertVertexQuery.Begin();
	VertexToAttributeQuery.Begin();

	for (int Index = 0; Index < Count; Index++)
	{
		if ((Index % UpdateFreq) == 0)
		{
			float Percent = Index / (float)Count;
			int Progress = Percent * ProgressShare;

			UpdateProgress(Warn, CurrentProgress + Progress, 100);
		}

		FTransform& Transform = PreparedTransforms[Index];

		if (ImportBounds.IsValid && !ImportBounds.IsInside(Transform.GetTranslation()))
		{
			// The given point is not within the bounding box, so skip it
			continue;
		}

		VertexValues[0] = Transform.GetTranslation().X;
		VertexValues[1] = Transform.GetTranslation().Y;
		VertexValues[2] = Transform.GetTranslation().Z;
		VertexValues[3] = Transform.GetRotation().X;
		VertexValues[4] = Transform.GetRotation().Y;
		VertexValues[5] = Transform.GetRotation().Z;
		VertexValues[6] = Transform.GetRotation().W;
		VertexValues[7] = Transform.GetScale3D().X;
		VertexValues[8] = Transform.GetScale3D().Y;
		VertexValues[9] = Transform.GetScale3D().Z;

#define CHECK_VALUE(Value, Message) if(FMath::IsFinite(VertexValues[Value])==false) UE_LOG(PointCloudLog, Warning, TEXT("Found Nan or Infinite on Vertex %d Value %hs"), Index, Message);

		CHECK_VALUE(0, "Translation.x");
		CHECK_VALUE(1, "Translation.y");
		CHECK_VALUE(2, "Translation.z");
		CHECK_VALUE(3, "Rotation.x");
		CHECK_VALUE(4, "Rotation.y");
		CHECK_VALUE(5, "Rotation.z");
		CHECK_VALUE(6, "Rotation.w");
		CHECK_VALUE(7, "Scale.x");
		CHECK_VALUE(8, "Scale.y");
		CHECK_VALUE(9, "Scale.z");

#undef CHECK_VALUE

		if (!InsertVertexQuery.Step(VertexValues))
		{
			Holder.RollBack();
			return false;
		}

		CurrentTopVertexRowId++;

		int32 IndexInToMetadata = Index * NumMetadataColumns;

		for (int i = 0; i < MetadataCountPerVertex[Index]; ++i, ++CurrentMetadataIndex)
		{
			if (!VertexToAttributeQuery.Step(CurrentTopVertexRowId, PreparedMetadataIndices[CurrentMetadataIndex].Key, PreparedMetadataIndices[CurrentMetadataIndex].Value))
			{
				Holder.RollBack();
				return false;
			}
		}
	}

	InsertVertexQuery.End();
	VertexToAttributeQuery.End();

	InsertTimer.Report("Time To Insert Points");

	PointCloudPrivateNamespace::CreateIndexes(this, Warn);

	if (Holder.EndTransaction())
	{
		UE_LOG(PointCloudLog, Log, TEXT("Inserted %d Points and %d Attributes\n"), PreparedTransforms.Num(), PreparedMetadata.Num());
	}
	else
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Failed To Insert Object %s\n"), *ObjectName);
		Holder.RollBack();
		return false;
	}

	UE_LOG(PointCloudLog, Log, TEXT("Took %.2f Seconds to Insert Object\n"), Timer.ToSeconds());

	// Calculate the hash of the database
	CalculateWholeDbHash();

	return true;
}

namespace
{

#if WITH_EDITOR

	void PrepareMetadata(int Count, TMap<FString, int>& AttributeKeys, TMap<FString, TArray<FString> >& MetadataColumnValues, TArray< TPair< int, FString >>& PreparedMetadata, TArray<int>& MetadataCountPerVertex)
	{
		PointCloud::UtilityTimer Timer;

		// This prepares a flat array of KeyId, MetadataValue pairs 
		// The array is packed with 
		// Point1.Item1
		// Point1.Item2
		// Point1.Item3
		// Point1.Item4
		// Point1.Item...
		// Point2.Item1
		// Point2.Item2
		// Point2.Item3
		// Point2.Item4
		// Point2.Item...

		// The output array needs to be big enough to store PointClount * Number Of MetadataItems 
		PreparedMetadata.SetNum(Count * AttributeKeys.Num());
		MetadataCountPerVertex.SetNum(Count);

		// Store a direct pointer that maps from Attribute Keys to the Metadata column
		TArray< TArray<FString>*> IndexToMetadata;
		TArray< int>			  IndexToId;
		TMap<FString, int>		  KeyToId;

		// for each of the metadata keys, get a direct pointer to Array storing that value in the Incoming Metadata
		// Also store the Index of each metadata key 
		for (auto a : AttributeKeys)
		{
			FString& MetadataItem = a.Key;
			int Id = a.Value;
			IndexToMetadata.Add(MetadataColumnValues.Find(a.Key));
			IndexToId.Add(Id);
			KeyToId.Add(MetadataItem, Id);
		}

		int NumKeys = AttributeKeys.Num();

		// Iterate over all Points
		ParallelFor(Count, [&](int32 Index)
			{
				for (int i = 0; i < NumKeys; i++)
				{
					// Get the point for the current key
					TArray<FString>* ValueArray = IndexToMetadata[i];

					FString& Value = ValueArray->operator[](Index);

					// Get the DB Id for the current Key
					int KeyId = IndexToId[i];

					// calculate the offset into the output array for this value
					int IndexIntoOutput = (Index * NumKeys) + i;

					// Set the 
					PreparedMetadata[IndexIntoOutput].Key = KeyId;
					PreparedMetadata[IndexIntoOutput].Value = Value;
				}

				MetadataCountPerVertex[Index] = NumKeys;
			});

		Timer.Report("Prepare Metadata");
	}


	void PrepareTransforms(int32 Count, bool FlipW, TMap<FString, TArray<FString> >& DefaultColumnValues, TArray<FTransform>& PreparedTransforms)
	{
		PointCloud::UtilityTimer Timer;
		// Use a parallel for loop to prepare all of the transforms		
		PreparedTransforms.SetNum(Count);

		const int32 NX_Index = 0;
		const int32 NY_Index = 1;
		const int32 NZ_Index = 2;
		const int32 NW_Index = 3;

		const int32 PX_Index = 4;
		const int32 PY_Index = 5;
		const int32 PZ_Index = 6;

		const int32 SX_Index = 7;
		const int32 SY_Index = 8;
		const int32 SZ_Index = 9;

		TArray< TArray<FString>*> ColumnPtrs;
		ColumnPtrs.SetNum(DefaultColumnValues.Num());

		ColumnPtrs[NX_Index] = DefaultColumnValues.Find("nx");
		ColumnPtrs[NY_Index] = DefaultColumnValues.Find("ny");
		ColumnPtrs[NZ_Index] = DefaultColumnValues.Find("nz");
		ColumnPtrs[NW_Index] = DefaultColumnValues.Find("nw");

		ColumnPtrs[PX_Index] = DefaultColumnValues.Find("px");
		ColumnPtrs[PY_Index] = DefaultColumnValues.Find("py");
		ColumnPtrs[PZ_Index] = DefaultColumnValues.Find("pz");

		ColumnPtrs[SX_Index] = DefaultColumnValues.Find("sx");
		ColumnPtrs[SY_Index] = DefaultColumnValues.Find("sy");
		ColumnPtrs[SZ_Index] = DefaultColumnValues.Find("sz");

		ParallelFor(Count, [&](int32 Index)
			{
				float RotX = FCString::Atof(*ColumnPtrs[NX_Index]->operator[](Index));
				float RotY = FCString::Atof(*ColumnPtrs[NY_Index]->operator[](Index));
				float RotZ = FCString::Atof(*ColumnPtrs[NZ_Index]->operator[](Index));
				float RotW = FCString::Atof(*ColumnPtrs[NW_Index]->operator[](Index));

				if (FlipW)
				{
					RotW = -RotW;
				}

				float ScaleX = FCString::Atof(*ColumnPtrs[SX_Index]->operator[](Index));
				float ScaleY = FCString::Atof(*ColumnPtrs[SY_Index]->operator[](Index));
				float ScaleZ = FCString::Atof(*ColumnPtrs[SZ_Index]->operator[](Index));
				float PosX = FCString::Atof(*ColumnPtrs[PX_Index]->operator[](Index));
				float PosY = FCString::Atof(*ColumnPtrs[PY_Index]->operator[](Index));
				float PosZ = FCString::Atof(*ColumnPtrs[PZ_Index]->operator[](Index));

				FQuat Q(RotX, RotY, RotZ, RotW);
				Q.Normalize();

				PreparedTransforms[Index] = FTransform(Q, FVector(PosX, PosY, PosZ), FVector(ScaleX, ScaleY, ScaleZ));
			});

		Timer.Report("Prepare Transforms");
	}

	bool HasColumn(const FString& Name, const TMap<FString, TArray<FString> >& Data)
	{

		bool Found = Data.Contains(Name);

		if (!Found)
		{
			UE_LOG(PointCloudLog, Log, TEXT("Cannot find default column %s\n"), *Name);
		}
		return Found;
	}

	bool ProcessCsvPrepared(
		UPointCloudImpl* Cloud,
		const FString& FileName,
		TMap<FString, TArray<FString> >& DefaultColumnValues,
		TMap<FString, TArray<FString > >& MetadataColumnValues,
		TSet<FString>& MetadataColumnNames,
		bool FlipW,
		const FBox& ImportBounds,
		FFeedbackContext* Warn)
	{
		if (!Cloud)
		{
			return false;
		}

		PointCloud::UtilityTimer Timer;

		// check that the default columns are there
		if (!HasColumn("Id", DefaultColumnValues) ||
			!HasColumn("px", DefaultColumnValues) ||
			!HasColumn("py", DefaultColumnValues) ||
			!HasColumn("pz", DefaultColumnValues) ||
			!HasColumn("nx", DefaultColumnValues) ||
			!HasColumn("ny", DefaultColumnValues) ||
			!HasColumn("nz", DefaultColumnValues) ||
			!HasColumn("nw", DefaultColumnValues) ||
			!HasColumn("sx", DefaultColumnValues) ||
			!HasColumn("sy", DefaultColumnValues) ||
			!HasColumn("sz", DefaultColumnValues))
		{
			return false;
		}

		int32 Count = DefaultColumnValues["px"].Num();

		// We need to prepare a transform for each Point from the text version from the CSV		
		TArray<FTransform> PreparedTransforms;
		PrepareTransforms(Count, FlipW, DefaultColumnValues, PreparedTransforms);

		TArray<FString> ArrayOfColumnNames = MetadataColumnNames.Array();// Make a mapping 

		// We need to easily map between the KeyName as a string and the Index in the ArrayOfColumnNames 
		TMap<FString, int> AttributeKeys;
		for (int i = 0; i < ArrayOfColumnNames.Num(); i++)
		{
			AttributeKeys.Add(ArrayOfColumnNames[i], i);
		}

		// We need to prepare a UTF-8, santized version of each Metadata Value
		TArray< TPair<int, FString> > PreparedMetadata;
		TArray<int> MetadataCountPerVertex;
		PrepareMetadata(Count, AttributeKeys, MetadataColumnValues, PreparedMetadata, MetadataCountPerVertex);

		bool ReturnValue = Cloud->InitFromPreparedData(FileName, PreparedTransforms, ArrayOfColumnNames, MetadataCountPerVertex, PreparedMetadata, ImportBounds, Warn);

		Timer.Report("Time To Insert Points");

		UE_LOG(PointCloudLog, Log, TEXT("Rule Processor DB Hash %s\n"), *Cloud->GetHashAsString());

		return true;
	}

	void MakeColumn(int32 Count, const FString& Name, const FString& Value, TMap<FString, TArray<FString> >& Here)
	{
		TArray<FString> Values;
		Values.Init(Value, Count);
		Here[Name] = MoveTemp(Values);
	}

	/*
	** This function is used to load the contents of a database file on disk
	** into the "main" database of open database connection pInMemory, or
	** to save the current contents of the database opened by pInMemory into
	** a database file on disk. pInMemory is probably an in-memory database,
	** but this function will also work fine if it is not.
	**
	** Parameter zFilename points to a nul-terminated string containing the
	** name of the database file on disk to load from or save to. If parameter
	** isSave is non-zero, then the contents of the file zFilename are
	** overwritten with the contents of the database opened by pInMemory. If
	** parameter isSave is zero, then the contents of the database opened by
	** pInMemory are replaced by data loaded from the file zFilename.
	**
	** If the operation is successful, SQLITE_OK is returned. Otherwise, if
	** an error occurs, an SQLite error code is returned.
	*/
	int loadOrSaveDb(sqlite3* pInMemory, const char* zFilename, int32 isSave) {
		int rc;                   /* Function return code */
		sqlite3* pFile;           /* Database connection opened on zFilename */
		sqlite3_backup* pBackup;  /* Backup object used to copy data */
		sqlite3* pTo;             /* Database to copy to (pFile or pInMemory) */
		sqlite3* pFrom;           /* Database to copy from (pFile or pInMemory) */

		PointCloud::UtilityTimer Timer;

		/* Open the database file identified by zFilename. Exit early if this fails
		** for any reason. */
		rc = sqlite3_open(zFilename, &pFile);
		if (rc == SQLITE_OK) {

			/* If this is a 'load' operation (isSave==0), then data is copied
			** from the database file just opened to database pInMemory.
			** Otherwise, if this is a 'save' operation (isSave==1), then data
			** is copied from pInMemory to pFile.  Set the variables pFrom and
			** pTo accordingly. */
			pFrom = (isSave ? pInMemory : pFile);
			pTo = (isSave ? pFile : pInMemory);

			/* Set up the backup procedure to copy from the "main" database of
			** connection pFile to the main database of connection pInMemory.
			** If something goes wrong, pBackup will be set to NULL and an error
			** code and message left in connection pTo.
			**
			** If the backup object is successfully created, call backup_step()
			** to copy data from pFile to pInMemory. Then call backup_finish()
			** to release resources associated with the pBackup object.  If an
			** error occurred, then an error code and message will be left in
			** connection pTo. If no error occurred, then the error code belonging
			** to pTo is set to SQLITE_OK.
			*/
			pBackup = sqlite3_backup_init(pTo, "main", pFrom, "main");
			if (pBackup) {
				(void)sqlite3_backup_step(pBackup, -1);
				(void)sqlite3_backup_finish(pBackup);
			}
			rc = sqlite3_errcode(pTo);
		}

		if (isSave)
		{
			Timer.Report(TEXT("Save"));
		}
		else
		{
			Timer.Report(TEXT("Load"));
		}

		/* Close the database connection opened on database file zFilename
		** and return the result of this function. */
		(void)sqlite3_close(pFile);
		return rc;
	}

#endif // WITH_EDITOR
}

bool UPointCloudImpl::SaveToDisk(const FString& FileName)
{
#if WITH_EDITOR
	if (!IsInitialized())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("SaveToDisk : Not Initialized %s\n"), *FileName);
		return false;
	}

	int Rc = loadOrSaveDb(InternalDatabase, TCHAR_TO_ANSI(*FileName), 1);

	if (Rc == SQLITE_OK)
	{
		UE_LOG(PointCloudLog, Log, TEXT("SaveToDisk : Saved Database %s\n"), *FileName);
		return true;
	}
	else
	{
		UE_LOG(PointCloudLog, Warning, TEXT("SaveToDisk : Failed To Save Database %s\n"), *FileName);
		return false;
	}
#else
	return false;
#endif
}

void UPointCloudImpl::OptimizeIfRequired()
{
	// for the moment always call analyze	
	RUN_QUERY("PRAGMA analysis_limit=400;PRAGMA optimize; ");
	RUN_QUERY("ANALYZE");
}

bool UPointCloudImpl::LoadFromCsv(const FString& FileName, const FBox& InImportBounds, ELoadMode Mode, FFeedbackContext* Warn)
{
#if WITH_EDITOR
	if (FileName == FString())
	{
		return false;
	}

	PointCloud::UtilityTimer Timer;
	FPointCloudCsv doc = FPointCloudCsv::Open(FileName, Warn);

	FString SQLiteVersion = GetValue<FString>("select sqlite_version() as VERSION", "VERSION");

	TArray<FString> CompileOptions;

	CompileOptions = GetValueArray<FString>("PRAGMA compile_options");

	UE_LOG(PointCloudLog, Log, TEXT("SQLLite Library Version : %s\n"), *SQLiteVersion);

	for (FString Option : CompileOptions)
	{
		UE_LOG(PointCloudLog, Log, TEXT("SQLLite Compile Option : %s"), *Option);
	}

	UpdateProgress(Warn, 10, 100);

	if (doc.GetIsOpen() == false)
	{
		UE_LOG(PointCloudLog, Log, TEXT("Cannot read from stream for CSV: %s\n"), *FileName);
		return false;
	}

	TMap<FString, TArray<FString> >			DefaultColumnValues;
	TMap<FString, TArray<FString > >		MetadataColumnValues;
	TSet<FString>							MetadataColumnNames;

	UE_LOG(PointCloudLog, Log, TEXT("Reading CSV: %s\n"), *FileName);

	TMap< FString, FString > DefaultColumns = {
															TTuple<FString, FString>(FString("point"), FString("Id")),
															TTuple<FString, FString>(FString("Px"), FString("px")),
															TTuple<FString, FString>(FString("Py"), FString("pz")),			// Note swapped Py and Pz
															TTuple<FString, FString>(FString("Pz"), FString("py")),

															TTuple<FString, FString>(FString("orientx"), FString("nx")),
															TTuple<FString, FString>(FString("orienty"), FString("nz")),	// Note swapped Py and Pz
															TTuple<FString, FString>(FString("orientz"), FString("ny")),
															TTuple<FString, FString>(FString("orientw"), FString("nw")),

															TTuple<FString, FString>(FString("scalex"), FString("sx")),
															TTuple<FString, FString>(FString("scalez"), FString("sy")),		// Note swapped Py and Pz
															TTuple<FString, FString>(FString("scaley"), FString("sz")),

	};

	TMap< FString, FString> DefaultValues =
	{
					TTuple<FString, FString>(FString("Id"), FString("-1")),
					TTuple<FString, FString>(FString("px"), FString("0.0")),
					TTuple<FString, FString>(FString("py"), FString("0.0")),
					TTuple<FString, FString>(FString("pz"), FString("0.0")),
					TTuple<FString, FString>(FString("nx"), FString("0.0")),
					TTuple<FString, FString>(FString("ny"), FString("0.0")),
					TTuple<FString, FString>(FString("nz"), FString("0.0")),
					TTuple<FString, FString>(FString("nw"), FString("1.0")),
					TTuple<FString, FString>(FString("sx"), FString("1.0")),
					TTuple<FString, FString>(FString("sy"), FString("1.0")),
					TTuple<FString, FString>(FString("sz"), FString("1.0")),
	};

	UpdateProgress(Warn, 20, 100);

	// Try and read in the default columns
	for (auto i : DefaultColumns)
	{
		if (!PointCloudPrivateNamespace::TryTakeColumn(doc, i.Key, i.Value, DefaultColumnValues))
		{
			// if the column can't be loaded, created an entry with the default values. Not very efficient but will do for now.
			FString DefaultValue = DefaultValues[i.Value];

			TArray<FString> Defaults;

			Defaults.Init(DefaultValue, doc.GetRowCount());

			DefaultColumnValues.FindOrAdd(i.Value) = MoveTemp(Defaults);
		}
	}

	Timer.Report("Initialize Default columns");

	UpdateProgress(Warn, 30, 100);

	// Now find the Other, Metadata columns
	for (int i = 0; i < doc.GetColumnCount(); i++)
	{
		FString ColumnName = doc.GetColumnName(i);
		// If this is not one of the default columns
		if (DefaultColumns.Contains(ColumnName) == false)
		{
			// load it into the Metadata columns
			UE_LOG(PointCloudLog, Log, TEXT("Metadata Colmun %s\n"), *ColumnName);

			TArray<FString>* Column = doc.GetColumn(ColumnName);

			if (Column != nullptr)
			{
				MetadataColumnValues.Add(ColumnName, MoveTemp(*Column));
				MetadataColumnNames.Add(ColumnName);
			}
			else
			{
				UE_LOG(PointCloudLog, Log, TEXT("Cannot Find Metadata Colmun %s\n"), *ColumnName);
			}

		}
	}

	Timer.Report("Metadata Columns");

	UpdateProgress(Warn, 40, 100);

	bool Result = ProcessCsvPrepared(this,
		FileName,
		DefaultColumnValues,
		MetadataColumnValues,
		MetadataColumnNames, true,
		InImportBounds,
		Warn);

	Timer.Report(TEXT("LoadCsvFromStream"));

	return Result;
#else
	return false;
#endif // WITH_EDITOR
}

bool UPointCloudImpl::LoadFromAlembic(const FString& FileName, const FBox& InImportBounds, ELoadMode Mode, FFeedbackContext* Warn)
{
#if WITH_EDITOR
	/** Factory used to generate objects*/
	Alembic::AbcCoreFactory::IFactory Factory;
	Alembic::AbcCoreFactory::IFactory::CoreType CompressionType = Alembic::AbcCoreFactory::IFactory::kUnknown;
	/** Archive-typed ABC file */
	Alembic::Abc::IArchive Archive;
	/** Alembic typed root (top) object*/
	Alembic::Abc::IObject TopObject;

	Factory.setPolicy(Alembic::Abc::ErrorHandler::kQuietNoopPolicy);
	Factory.setOgawaNumStreams(12);

	// Extract Archive and compression type from file
	Archive = Factory.getArchive(TCHAR_TO_UTF8(*FileName), CompressionType);
	if (!Archive.valid())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Failed to open %s: Not a valid Rule Processor Alembic file."), *FileName);
		return false;
	}

	// Get Top/root object
	TopObject = Alembic::Abc::IObject(Archive, Alembic::Abc::kTop);
	if (!TopObject.valid())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Failed to import %s: Root not is not valid."), *FileName);
		return false;
	}

	TArray<FTransform> PreparedTransforms;
	TArray<FString> MetadataColumnNames;
	TMap<FString, TArray<FString>> MetadataValues;
	ParseAlembicObject(TopObject, PreparedTransforms, MetadataColumnNames, MetadataValues);

	// metadata properties
	// We need to easily map between the KeyName as a string and the Index in the ArrayOfColumnNames 
	TMap<FString, int> AttributeKeys;
	for (int i = 0; i < MetadataColumnNames.Num(); i++)
	{
		AttributeKeys.Add(MetadataColumnNames[i], i);
	}

	TArray < TPair<int, FString>> PreparedMetadata;
	TArray<int> MetadataCountPerVertex;

	PrepareMetadata(PreparedTransforms.Num(), AttributeKeys, MetadataValues, PreparedMetadata, MetadataCountPerVertex);

	return InitFromPreparedData(FileName, PreparedTransforms, MetadataColumnNames, MetadataCountPerVertex, PreparedMetadata, InImportBounds, Warn);
#else
	return false;
#endif 
}

bool UPointCloudImpl::LoadFromStructuredPoints(const TArray<FPointCloudPoint>& InPoints, const FBox& InImportBounds, FFeedbackContext* Warn)
{
	TArray<FTransform> PreparedTransforms;
	TMap<FString, int> MetadataColumns;
	TArray<TPair<int, FString>> PreparedMetadata;
	TArray<int> MetadataCountPerVertex;

	PreparedTransforms.Reserve(InPoints.Num());
	PreparedMetadata.Reserve(InPoints.Num());
	MetadataCountPerVertex.Reserve(InPoints.Num());

	for (const FPointCloudPoint& Point : InPoints)
	{
		PreparedTransforms.Add(Point.Transform);

		for (const auto& KVPair : Point.Attributes)
		{
			// Get column index
			int ColumnIndex;
			if (MetadataColumns.Contains(KVPair.Key))
			{
				ColumnIndex = MetadataColumns[KVPair.Key];
			}
			else
			{
				ColumnIndex = MetadataColumns.Num();
				MetadataColumns.Add(KVPair.Key, ColumnIndex);
			}

			// Add property
			PreparedMetadata.Emplace(ColumnIndex, KVPair.Value);
		}

		MetadataCountPerVertex.Add(Point.Attributes.Num());
	}

	TArray<FString> MetadataColumnNames;
	MetadataColumnNames.SetNum(MetadataColumns.Num());
	for (const auto& KVPair : MetadataColumns)
	{
		MetadataColumnNames[KVPair.Value] = KVPair.Key;
	}

	return InitFromPreparedData(FString(), PreparedTransforms, MetadataColumnNames, MetadataCountPerVertex, PreparedMetadata, InImportBounds, Warn);
}

int32 UPointCloudImpl::GetTemporaryTableCacheSize()
{
	// Magic Number alert. This is a method for the moment but it may become dynamic down the line, hence using a method 
	// rather than a static variable 
	return 5000;
}

namespace
{
#if WITH_EDITOR
	int GetColumnNamesCallBack(void* Out, int argc, char** argv, char** azColName)
	{

		TArray<FString >* Result = (TArray<FString > *)Out;

		for (int i = 0; i < argc; i++)
		{
#if PLATFORM_WINDOWS
			if (_strcmpi(azColName[i], "name") == 0)
#else
			if (strcasecmp(azColName[i], "name") == 0)
#endif
			{
				Result->Add(FString(ANSI_TO_TCHAR(argv[i])));
			}

		}
		return 0;
	}
#endif // WITH_EDITOR
}

// return a list of the default attributes exposed by this pointcloud
TArray<FString> UPointCloudImpl::GetDefaultAttributes() const
{
	TArray<FString> Result;

#if WITH_EDITOR
	if (!IsInitialized())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("No Database Initialized"));
		return Result;
	}

	const FString Query = TEXT("PRAGMA table_info(\'Vertex\')");

	LOG_QUERY_LABEL(Query, TEXT("Get Default Attributes"));

	char* zErrMsg = nullptr;
	int rc = sqlite3_exec(InternalDatabase, TCHAR_TO_ANSI(*Query), GetColumnNamesCallBack, &Result, &zErrMsg);

	if (rc != SQLITE_OK)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("SQL error: %s\n"), *FString(zErrMsg).Left(1000));
		sqlite3_free(zErrMsg);
		return Result;
	}
#endif

	return Result;
}

// return a list of the Metadata attributes exposed by this point cloud
TSet<FString> UPointCloudImpl::GetMetadataAttributes() const
{
	if (MetadataAttributeCache.Num())
	{
		return MetadataAttributeCache;
	}

	TArray<FString> Values;

#if WITH_EDITOR
	if (!IsInitialized())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("No Database Initialized"));
		return MetadataAttributeCache;
	}

	const FString SelectQuery = FString::Printf(TEXT("SELECT DISTINCT Attribute_Name as name From Metadata"));

	LOG_QUERY_LABEL(SelectQuery, TEXT("GetMetadataAttributes"));

	char* zErrMsg = nullptr;
	int rc = sqlite3_exec(InternalDatabase, TCHAR_TO_ANSI(*SelectQuery), GetColumnNamesCallBack, &Values, &zErrMsg);

	if (rc != SQLITE_OK)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("SQL error: %s\n"), ANSI_TO_TCHAR(zErrMsg));
		sqlite3_free(zErrMsg);
	}

	MetadataAttributeCache = TSet<FString>(Values);
#endif // WITH_EDITOR

	return MetadataAttributeCache;
}

bool UPointCloudImpl::ReloadInternal(const TArray<FString >& Files, const FBox& ReimportBounds)
{
	PointCloud::UtilityTimer Timer;

	// create a new database and store the original
	sqlite3* CopyInternalDatabase = InternalDatabase;

	InternalDatabase = nullptr;

	InitDb();

	bool Sucess = true;

	FBox ImportBounds = ReimportBounds;

	if (ImportBounds.GetSize() == FVector::ZeroVector)
	{
		// This is a check to catch unitialized boxes, but doesn't make this robust to negative sized boxes etc
		ImportBounds.IsValid = false;
	}

	// and load the files 
	for (const FString& FileName : Files)
	{
		const FString Extension = FPaths::GetExtension(FileName).ToLower();

		UE_LOG(PointCloudLog, Log, TEXT("Reloading Point Cloud: %s\n"), *FileName);

		if (Extension == "psv")
		{
			Sucess = LoadFromCsv(FileName, ImportBounds, UPointCloud::ELoadMode::ADD, nullptr);
		}
		else if (Extension == "pbc")
		{
			Sucess = LoadFromAlembic(FileName, ImportBounds, UPointCloud::ELoadMode::ADD, nullptr);
		}
		else
		{
			UE_LOG(PointCloudLog, Log, TEXT("Unrecognised File Type : %s\n"), *Extension);
		}

		if (!Sucess)
		{
			break;
		}
	}

	// on failre, delete the new database and return false
	if (!Sucess)
	{
		sqlite3_close(InternalDatabase);
		InternalDatabase = CopyInternalDatabase;
	}
	else
	{
		MarkPackageDirty();
		sqlite3_close(CopyInternalDatabase);
	}

	Timer.Report(TEXT("Reload"));

	return true;
}

// return a list of the Files that make up this pointcloud
TArray<FString> UPointCloudImpl::GetLoadedFiles() const
{
	TArray<FString> Result;

#if WITH_EDITOR
	if (!IsInitialized())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("No Database Initialized"));
		return Result;
	}

	const FString SelectQuery = FString::Printf(TEXT("SELECT DISTINCT Name From Object"));

	LOG_QUERY_LABEL(SelectQuery, TEXT("Get Loaded Files"));

	char* zErrMsg = nullptr;
	int rc = sqlite3_exec(InternalDatabase, TCHAR_TO_ANSI(*SelectQuery), GetColumnNamesCallBack, &Result, &zErrMsg);

	if (rc != SQLITE_OK)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("SQL error: %s\n"), ANSI_TO_TCHAR(zErrMsg));
		sqlite3_free(zErrMsg);
	}

#endif // WITH_EDITOR

	return Result;
}

namespace
{
	const char* OBJECT_ADDED_NAME = "OBJECTADDED";
	const char* OBJECT_REMOVED_NAME = "OBJECTREMOVED";
}

// Initialize the database
void UPointCloudImpl::InitDb()
{
	// if we already have a database, then return
	if (IsInitialized())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Init DB Called On Initialized PointCloud\n"));
		return;
	}

	PointCloud::UtilityTimer Timer;

	int rc = 0;

	//rc = sqlite3_open(":memory:", &InternalDatabase);
	rc = sqlite3_open(":memory:", &InternalDatabase);

	if (rc)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Can't open database: %hs\n"), sqlite3_errmsg(InternalDatabase));
		sqlite3_close(InternalDatabase);
		InternalDatabase = 0;
		return;
	}

	// This bit of Dark Voodoo is required because there is a poorly documented default limit to 
	// the maximum size of in-memory databases that can be deserialized. Phew. That limit is by default
	// 1Gb. This totally removes that limit. I'm still not sure that in memory DBs over 2GB are supported due to 
	// way the SQLLites malloc implementation works, but this gets us some space for the moment. 
	// See more here - https://www.sqlite.org/compile.html , specifically SQLITE_MEMDB_DEFAULT_MAXSIZE
	int64 MaxSize = TNumericLimits<int64>::Max();
	sqlite3_file_control(InternalDatabase, "main", SQLITE_FCNTL_SIZE_LIMIT, &MaxSize);

	// Register custom functions that will get called when certain evens happen in the DB
	sqlite3_create_function(InternalDatabase, OBJECT_ADDED_NAME, 4, SQLITE_UTF8 | SQLITE_DETERMINISTIC, nullptr, &SQLExtension::objectadded, nullptr, nullptr);
	sqlite3_create_function(InternalDatabase, OBJECT_REMOVED_NAME, 4, SQLITE_UTF8 | SQLITE_DETERMINISTIC, nullptr, &SQLExtension::objectremoved, nullptr, nullptr);
	sqlite3_create_function(InternalDatabase, "SQRT", 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC, nullptr, &SQLExtension::sqlsqrt, nullptr, nullptr);
	sqlite3_create_function(InternalDatabase, "POW", 2, SQLITE_UTF8 | SQLITE_DETERMINISTIC, nullptr, &SQLExtension::sqlpow, nullptr, nullptr);
	sqlite3_create_function(InternalDatabase, "IN_SPHERE", 7, SQLITE_UTF8 | SQLITE_DETERMINISTIC, nullptr, &SQLExtension::sqlIsInSphere, nullptr, nullptr);
	sqlite3_create_function(InternalDatabase, "IN_OBB", 12, SQLITE_UTF8 | SQLITE_DETERMINISTIC, nullptr, &SQLExtension::sqlIsInOBB, nullptr, nullptr);

	sqlite3_create_function(InternalDatabase, "SHA3", 1, SQLITE_UTF8 | SQLITE_INNOCUOUS | SQLITE_DETERMINISTIC, nullptr, &SQLExtension::sha3Func, nullptr, nullptr);
	sqlite3_create_function(InternalDatabase, "SHA3", 2, SQLITE_UTF8 | SQLITE_INNOCUOUS | SQLITE_DETERMINISTIC, nullptr, &SQLExtension::sha3Func, nullptr, nullptr);
	sqlite3_create_function(InternalDatabase, "SHA3_QUERY", 1, SQLITE_UTF8 | SQLITE_DIRECTONLY, nullptr, &SQLExtension::sha3QueryFunc, nullptr, nullptr);
	sqlite3_create_function(InternalDatabase, "SHA3_QUERY", 2, SQLITE_UTF8 | SQLITE_DIRECTONLY, nullptr, &SQLExtension::sha3QueryFunc, nullptr, nullptr);
	sqlite3_create_function(InternalDatabase, "SHA3_QUERY", 3, SQLITE_UTF8 | SQLITE_DIRECTONLY, nullptr, &SQLExtension::sha3QueryFunc, nullptr, nullptr);

	if (!SetupSchema())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Can't create schema"));
		sqlite3_close(InternalDatabase);
		InternalDatabase = 0;
	}

	// This needs to be called now to set the internal Schema Version 
	EPointCloudSchemaVersions Version = GetSchemaVersion();

	UE_LOG(PointCloudLog, Log, TEXT("Created PointCloud With Schema Version %d"), Version);

	Timer.Report(TEXT("Init"));
}

void UPointCloudImpl::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);

	if (Ar.IsSaving())
	{
		// Check we're actually writing out data
		if (!Ar.ShouldSkipBulkData() && !Ar.IsObjectReferenceCollector() && Ar.IsPersistent())
		{
			// Add a flag into the stream to allow us to know if the stream has serialized data to load or not
			// in this case we are writing data so set the flag to true
			bool DoIHaveData = true;
			Ar << DoIHaveData;
			SerializeDb(Ar);
		}
		else
		{
			// we are not serializing any data, so set to false
			bool DoIHaveData = false;
			Ar << DoIHaveData;
		}
	}
	else if (Ar.IsLoading())
	{
		int32 Ver = Ar.CustomVer(FUE5MainStreamObjectVersion::GUID);

		if (Ver >= FUE5MainStreamObjectVersion::MantleDbSerialize)
		{
			// read in the flag indicating if data was written to the stream or not
			bool DoIHaveData = false;
			Ar << DoIHaveData;

			if (DoIHaveData == true)
			{
				// the flag is true so we're safe to deserialize data
				DeSerializeDb(Ar);
				UE_LOG(PointCloudLog, Log, TEXT("Rule Processor DB Hash %s\n"), *GetHashAsString());
			}
		}
		else
		{
			UE_LOG(PointCloudLog, Warning, TEXT("This Rule Processor Asset Is Out Of Date And Cannot Be Loaded. Please Recreate From Orginial Files. Sorry this shouldn't happen again"));
		}

	}
}

bool UPointCloudImpl::SetupSchema()
{
	return RUN_QUERY(PointCloud::SchemaQuery);
}

bool UPointCloudImpl::SetSqlLog(const FString& FileName)
{
	if (FileName.IsEmpty())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Invalid Empty Filename passed to UPointCloud::SetSqlLog"));
		return false;
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	LogFile = PlatformFile.OpenWrite(*FileName);

	if (LogFile == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Cannot Open File For SQL Logging : %s"), *FileName);
		return false;
	}

	return true;
}

bool UPointCloudImpl::StartLogging(const FString& InFileName)
{
	SetSqlLog(InFileName);

	if (LogFile == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Cannot Start Logging Log File Is Not Open"));
		return false;
	}

	if (bLoggingEnabled == true)
	{
		return true;
	}

	bLoggingEnabled = true;

	return true;
}

bool UPointCloudImpl::StopLogging()
{
	if (bLoggingEnabled == false)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Logging is Not Enabled"));
		return true;
	}
	if (LogFile == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Log File Is Not Open"));
		return false;
	}

	float TimeTotal = 0.0f;
	int TotalCalls = 0;
	for (const auto& Entry : LogRecords)
	{
		const FString& FileName = Entry.Key.Key;
		size_t LineNum = Entry.Key.Value;
		const LogRecord& Record = Entry.Value;

		FString Line = FString::Printf(TEXT(" %-25s (%04d) %05d %.2fs Avg (%.2fs) --> %s\n"), *FPaths::GetCleanFilename(FileName), (int)LineNum, Record.Calls, Record.CumulativeTime, Record.CumulativeTime / (Record.Calls != 0 ? Record.Calls : 1), *Record.Query);
		LogFile->Write((const uint8*)TCHAR_TO_ANSI(*Line), Line.Len());
		TimeTotal += Record.CumulativeTime;
		TotalCalls += Record.Calls;
	}

	FString Line = FString::Printf(TEXT("\n\nTotal Calls = (%05d) Total Time = %.2fs \n"), TotalCalls, TimeTotal);
	LogFile->Write((const uint8*)TCHAR_TO_ANSI(*Line), Line.Len());

	TArray< TPair<FString, int32> > CacheMissReport = GetQueryCacheMissCounts();

	Line = "Cache Miss Counts\n\n";
	LogFile->Write((const uint8*)TCHAR_TO_ANSI(*Line), Line.Len());

	for (const auto& CacheMissRecord : CacheMissReport)
	{
		Line = FString::Printf(TEXT("(%05d) %s \n"), CacheMissRecord.Value, *CacheMissRecord.Key);
		LogFile->Write((const uint8*)TCHAR_TO_ANSI(*Line), Line.Len());
	}

	bLoggingEnabled = false;
	delete LogFile;
	LogFile = nullptr;

	return true;
}

UPointCloudImpl::LogEntry UPointCloudImpl::LogSql(const FString& FileName, uint32 Line, const FString& Query) const
{
	if (bLoggingEnabled == false)
	{
		return LogEntry();
	}

	LogEntry Entry(FileName, Line);

	if (LogRecords.Contains(Entry) == true)
	{
		LogRecords[Entry].Calls++;
	}
	else
	{
		LogRecords.Add(Entry, { 0,0,Query });
	}

	return Entry;
}

bool UPointCloudImpl::SetTiming(const UPointCloudImpl::LogEntry& Entry, float Time) const
{
	if (bLoggingEnabled == false)
	{
		return false;
	}

	if (LogRecords.Contains(Entry) == false)
	{
		return false;
	}

	LogRecords[Entry].CumulativeTime += Time;

	return true;
}

bool UPointCloud::LoggingEnabled() const
{
	return bLoggingEnabled;
}

// Copy the Internal Database into the Serialized Version
void UPointCloudImpl::SerializeDb(FArchive& Ar)
{
	if (!IsInitialized())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("No Database Initialized"));
		return;
	}

	sqlite3_int64 piSize = 0;

	PointCloud::UtilityTimer Timer;

	// This number is the maximum buffer size that sqlite will allocate. Have a look at  void *sqlite3Malloc(u64 n) which is at Line 27408 in sqlite3.inl at the time of writing
	// sqlite3_serialize will fail to create the buffer for databases above this size. 
	static unsigned int MAX_SQLITE_ALLOC_SIZE = 0x7fffff00;

	unsigned char* Data = sqlite3_serialize(
		InternalDatabase,           /* The database connection */
		"main",						/* Which DB to serialize. ex: "main", "temp", ... */
		&piSize,					/* Write size of the DB here, if not NULL */
		0							/* Zero or more SQLITE_SERIALIZE_* flags */
	);

#if WITH_EDITOR

	// If no data was allocated and the reported size is above the maximum allocatable size
	if (!Data && piSize >= MAX_SQLITE_ALLOC_SIZE)
	{
		// try a file based fall back, this is less efficient but should work for all sizes of databases
		FString TempFileName = FPaths::CreateTempFilename(FPlatformProcess::UserTempDir(), TEXT("PointCloudDatabase-"), TEXT(".temp"));

		int Rc = loadOrSaveDb(InternalDatabase, TCHAR_TO_ANSI(*TempFileName), 1);

		if (Rc == SQLITE_OK)
		{
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			IFileHandle* TempFileHandle = PlatformFile.OpenRead(*TempFileName);

			if (TempFileHandle)
			{
				// check the size of the file is the same as returned from sqlite3_serialize
				Data = (unsigned char*)FMemory::Malloc(TempFileHandle->Size());

				int64 LoadedSize = TempFileHandle->Read(Data, TempFileHandle->Size());

				if (LoadedSize == false)
				{
					FMemory::Free(Data);
					UE_LOG(PointCloudLog, Warning, TEXT("Failed To Load Temporary Database - Mismatched sizes\n"), *TempFileName);
					Ar.SetCriticalError();
					return;
				}
			}
			else
			{
				UE_LOG(PointCloudLog, Warning, TEXT("Failed To Load Temporary Database %s\n"), *TempFileName);
				Ar.SetCriticalError();
				return;
			}
		}
		else
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Failed To Save Temporary Database %s\n"), *TempFileName);
			Ar.SetCriticalError();
			return;
		}
	}
#endif 

	UE_LOG(PointCloudLog, Log, TEXT("Precompress Rule Processor Asset Size :%.2fMb"), piSize / 1024.0 / 1024.0);

	if (piSize == 0)
	{
		UE_LOG(PointCloudLog, Log, TEXT("Zero Sized Data return from sqlite3_serialize"));
		sqlite3_free(static_cast<void*>(Data));
		return;
	}

	if (Data == nullptr)
	{
		UE_LOG(PointCloudLog, Log, TEXT("Null Ptr Returned from sqlite3_serialize"));
		sqlite3_free(static_cast<void*>(Data));
		return;
	}

	// Use this oppertunity to calculate the hash if the data is out of date
	CalculateWholeDbHash(Data, piSize);

	int64 Size = (int64)piSize;
	Ar << Size;
	Ar.SerializeCompressedNew(Data, Size);
	Ar.Serialize(WholeDbHash.m_digest, WholeDbHash.DigestSize);

	sqlite3_free(static_cast<void*>(Data));
}

// copy the Serialized database into the internal;
void UPointCloudImpl::DeSerializeDb(FArchive& Ar)
{
	// freeup an existing database state
	if (!IsInitialized())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("No Database Initialized"));
		return;
	}

	PointCloud::UtilityTimer Timer;

	int64 Size = 0;
	Ar << Size;
	uint8* Copy = static_cast<uint8*>(FMemory::Malloc(Size * 2)); //note: we do not use sqlite3_malloc64 here, because it fails for allocations over 32b.
	Ar.SerializeCompressedNew(Copy, Size);
	Ar.Serialize(WholeDbHash.m_digest, WholeDbHash.DigestSize);

	// Calculate the hash of the database on loading to ensure it is up to date	

	int rc = sqlite3_deserialize(
		InternalDatabase,				/* The database connection */
		"main",							/* Which DB to reopen with the deserialization */
		Copy,							/* The serialized database content */
		Size,							/* Number bytes in the deserialization */
		Size * 2,							/* Total size of buffer pData[] */
		SQLITE_DESERIALIZE_FREEONCLOSE | SQLITE_DESERIALIZE_RESIZEABLE
	);

	if (NeedsUpdating())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Point Cloud '%s' Uses An Old Schema (PointCloud=%d Current=%d), Please Update Or Recreate"), *GetPathName(), SchemaVersion, GetLatestSchemaVersion());
	}

	OptimizeIfRequired();

	// Calculate the hash if required
	CalculateWholeDbHash(Copy, Size);

	Timer.Report(TEXT("Deserialize"));
}

namespace
{
	int PrintCallBack(void* NotUsed, int argc, char** argv, char** azColName)
	{
		for (int i = 0; i < argc; i++)
		{
			UE_LOG(PointCloudLog, Log, TEXT("%s = %s\n"), ANSI_TO_TCHAR(azColName[i]),
				ANSI_TO_TCHAR(argv[i] ? argv[i] : "NULL"));
		}
		return 0;
	}
}

bool UPointCloudImpl::RunQuery(const FString& Query, int (*Callback)(void*, int, char**, char**), void* UsrData, const FString& InOriginatingFile, const uint32 InOriginatingLine)
{
	PointCloud::QueryLogger(this, Query, FString(), InOriginatingFile, InOriginatingLine);
	return RunQueryInternal(Query, Callback, UsrData);
}

bool UPointCloudImpl::RunQueryInternal(const FString& Query, int (*Callback)(void*, int, char**, char**), void* UsrData)
{
	if (!IsInitialized())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("No Database Initialized"));
		return false;
	}

	if (Query.Len() == 0)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Empty Query"));
		return false;
	}

	char* zErrMsg = nullptr;
	int rc = sqlite3_exec(InternalDatabase, TCHAR_TO_ANSI(*Query), Callback, UsrData, &zErrMsg);

	if (rc != SQLITE_OK)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("SQL error: %s with query %s\n"), ANSI_TO_TCHAR(zErrMsg), *Query.Left(1000));
		sqlite3_free(zErrMsg);
		return false;
	}

	return true;
}

bool UPointCloudImpl::RunQuery(const FString& Query, const FString& InOriginatingFile, const uint32 InOriginatingLine)
{
	PointCloud::QueryLogger(this, Query, FString(), InOriginatingFile, InOriginatingLine);
	return RunQueryInternal(Query);
}

bool UPointCloudImpl::RunQueryInternal(const FString& Query)
{
	return RunQueryInternal(Query, PrintCallBack, nullptr);
}

void UPointCloudImpl::GetValues(const FString& Query, const TArray<FString>& ColumnNames, TFunction<void(sqlite3_stmt*, int*)> Retrieval) const
{
	if (!IsInitialized())
	{
		return;
	}

	if (Query.IsEmpty())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Empty Query (%s)\n"), *Query);
		return;
	}

	LOG_QUERY(Query);

	sqlite3_stmt* stmt = nullptr;

	// execute statement
	int retval = sqlite3_prepare_v2(InternalDatabase, TCHAR_TO_ANSI(*Query), -1, &stmt, 0);

	if (retval)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Error Fetching Value (%d) : %s (%s)\n"), retval, ANSI_TO_TCHAR(sqlite3_errmsg(InternalDatabase)), (*Query));
		return;
	}

	// Identify given column names
	TArray<int> ColumnIndices;
	ColumnIndices.SetNum(ColumnNames.Num());

	bool bNeedToFetchColumnNames = ColumnNames.Num() > 0 && Algo::AnyOf(ColumnNames, [](const FString& ColumnName) { return !ColumnName.IsEmpty(); });

	if (bNeedToFetchColumnNames)
	{
		TMap<FString, int> NamesToIndexMap;

		for (int32 i = 0; i < sqlite3_column_count(stmt); i++)
		{
			const char* ColumnName = sqlite3_column_name(stmt, i);
			NamesToIndexMap.Add(FString(ANSI_TO_TCHAR(ColumnName)).ToLower(), i);
		}

		for (int Index = 0; Index < ColumnNames.Num(); ++Index)
		{
			const FString& ColumnName = ColumnNames[Index];

			if (ColumnName.IsEmpty())
			{
				ColumnIndices[Index] = -1;
			}
			else
			{
				int* ColumnIndex = NamesToIndexMap.Find(ColumnName);
				if (ColumnIndex)
				{
					ColumnIndices[Index] = *ColumnIndex;
				}
				else
				{
					UE_LOG(PointCloudLog, Warning, TEXT("Column Not Found (%s)\n"), *ColumnName);
					sqlite3_finalize(stmt);
					return;
				}
			}
		}
	}
	else
	{
		// Implicit column indices, will be on the caller to select properly
		for (int& ColumnIndex : ColumnIndices)
		{
			ColumnIndex = -1;
		}
	}

	// iterate rows	
	while (1)
	{
		// fetch a row's status
		retval = sqlite3_step(stmt);

		if (retval == SQLITE_ROW)
		{
			Retrieval(stmt, ColumnIndices.GetData());
		}
		else if (retval == SQLITE_DONE)
		{
			break;
		}
		else
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Error Fetching Value (%d) : %s\n"), retval, ANSI_TO_TCHAR(sqlite3_errmsg(InternalDatabase)));
			break;
		}
	}

	sqlite3_finalize(stmt);
}

// Undef convenience macros
#undef RUN_QUERY
#undef RUN_QUERY_P
#undef LOG_QUERY
#undef LOG_QUERY_LABEL

#undef LOCTEXT_NAMESPACE

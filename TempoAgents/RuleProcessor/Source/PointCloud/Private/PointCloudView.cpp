// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudView.h"
#include "PointCloudImpl.h"
#include "PointCloudSQLExtensions.h"

UPointCloudView::~UPointCloudView()
{

}

int32 UPointCloudView::GetCount() const
{
	if (PointCloud==nullptr)
	{
		return 0;
	}

	if(HasFiltersApplied()==false)
	{
		return PointCloud->GetValue<int>(FString(TEXT("SELECT COUNT(*) FROM Vertex")), "COUNT(*)");
	}
	else
	{
		int Count = 0;

		const FString ResultTableName = GetFilterResultTable();

		Count = PointCloud->GetValue<int>(FString::Printf(TEXT("SELECT COUNT(*) FROM (%s)"), *ResultTableName), "COUNT(*)");

		return Count;
	}
}

const FString& UPointCloudView::GetHash() const
{
	if (CachedResultHash.IsEmpty())
	{
		if (PointCloud == nullptr)
		{
			// Nothing
		}
		else if (HasFiltersApplied() == false)
		{
			// Return hash from point cloud
			CachedResultHash = PointCloud->GetHashAsString();
		}
		else
		{
			const FString ResultTableName = GetFilterResultTable();
			const FString ViewQuery = FString::Printf(TEXT("SELECT * FROM %s"), *ResultTableName);
			CachedResultHash = HashQueryResults(ViewQuery);
		}
	}

	return CachedResultHash;
}

FString UPointCloudView::HashQueryResults(const FString& Query) const
{
	const int32 HashType = 256;
	const bool bIncludeQuery = false;
	const FString HashQuery = FString::Printf(TEXT("SELECT SHA3_QUERY(\"%s\", %i, %i)"), *Query, HashType, (int8)bIncludeQuery);

	FString QueryHash;

	if (PointCloud)
	{
		PointCloud->RunQuery(HashQuery, SQLExtension::Sha3CallBack, &QueryHash, __FILE__, __LINE__);
	}

	return QueryHash;
}

void UPointCloudView::DirtyHash()
{
	// Not threadsafe; should never be called by a non-owning user
	CachedResultHash = FString();

	// Need to dirty in child views as well, as any changes in this view could have an impact
	// in child views
	for (UPointCloudView* View : ChildViews)
	{
		View->DirtyHash();
	}
}

void UPointCloudView::SetPointCloud(UPointCloudImpl* InCloud)
{
	PointCloud = InCloud;	
}

void UPointCloudView::SetParentView(UPointCloudView* InParentView)
{		
	if (InParentView)
	{
		ParentView = InParentView;
		PointCloud = ParentView->PointCloud;
	}
	else
	{
		ParentView = nullptr;
		PointCloud = nullptr;
	}
}

UPointCloudView::UPointCloudView() : PointCloud(nullptr), ParentView(nullptr),  bInGetDataState(false)
{
	ViewGuid = FGuid::NewGuid();
}

UPointCloudView* UPointCloudView::MakeChildView()
{
	UPointCloudView* ChildView = NewObject<UPointCloudView>();
	ChildView->SetParentView(this);

	ChildViewsLock.Lock();
	ChildViews.Add(ChildView);
	ChildViewsLock.Unlock();

	return ChildView;
}

void UPointCloudView::RemoveChildView(UPointCloudView* ChildView)
{
	ChildViewsLock.Lock();
	ChildViews.Remove(ChildView);
	ChildViewsLock.Unlock();
}

void UPointCloudView::ClearChildViews()
{
	// Not threadsafe, should not be called by non-owning caller
	for (UPointCloudView* Child : ChildViews)
	{
		Child->ClearChildViews();
	}

	ChildViews.Empty();
}
	
void UPointCloudView::FilterOnBoundingSphere(const FVector& Center, float Radius, EFilterMode Mode)
{
	if (PointCloud==nullptr)
	{
		return ;
	}

	FString FullQuery = FString::Printf(TEXT("SELECT Id FROM SpatialQuery WHERE IN_SPHERE( %f, %f, %f, %f, Minx, Miny, Minz)>0"), 										
										Center.X, Center.Y, Center.Z, 
										Radius);

	AddFilterStatement(FullQuery);	
	
	return;
}

void UPointCloudView::FilterOnBoundingBox(const FBox& Query, bool bInvertSelection,  EFilterMode Mode)
{
	if (PointCloud==nullptr)
	{
		return ;
	}

	FString FullQuery;

	// We will increase/decrease values by a small value around the last digit we'll display to make sure we do the proper query
	const double Iota = 1.0e-6;

	FVector QueryMin = FVector(
		Query.Min.X * (1.0 - FMath::Sign(Query.Min.X) * Iota),
		Query.Min.Y * (1.0 - FMath::Sign(Query.Min.Y) * Iota),
		Query.Min.Z * (1.0 - FMath::Sign(Query.Min.Z) * Iota));

	FVector QueryMax = FVector(
		Query.Max.X * (1.0 + FMath::Sign(Query.Max.X) * Iota),
		Query.Max.Y * (1.0 + FMath::Sign(Query.Max.Y) * Iota),
		Query.Max.Z * (1.0 + FMath::Sign(Query.Max.Z) * Iota));
	
	if (!bInvertSelection)
	{
		FullQuery = FString::Printf(TEXT("SELECT Id FROM SpatialQuery WHERE (Minx>=%f AND Maxx<=%f) AND (Miny>=%f AND Maxy<=%f) AND (Minz>=%f AND Maxz<=%f)"),			
			QueryMin.X, QueryMax.X,
			QueryMin.Y, QueryMax.Y,
			QueryMin.Z, QueryMax.Z);
	}
	else
	{
		FullQuery = FString::Printf(TEXT("SELECT Id FROM SpatialQuery WHERE (Minx<%f OR Maxx>%f) OR (Miny<%f OR Maxy>%f) OR (Minz<%f OR Maxz>%f)"),			
			QueryMin.X, QueryMax.X,
			QueryMin.Y, QueryMax.Y,
			QueryMin.Z, QueryMax.Z);
	}
	AddFilterStatement(FullQuery);

	return;
}

void UPointCloudView::FilterOnOrientedBoundingBox(const FTransform& InOBB, bool bInvertSelection, EFilterMode Mode /*= EFilterMode::FILTER_Or*/)
{
	if (!PointCloud)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Point Cloud Is NULL"));
		return;
	}

	if (!InOBB.IsValid())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Invalid OBB transform"));
		return;
	}

	const FRotator Rotation = InOBB.GetRotation().Rotator();
	const FVector Translation = InOBB.GetTranslation();
	const FVector Scale = InOBB.GetScale3D();

	const FString FullQuery = FString::Printf(TEXT("SELECT Id FROM SpatialQuery WHERE %s IN_OBB(%f,%f,%f,%f,%f,%f,%f,%f,%f,Minx,Miny,Minz)"),		
		bInvertSelection ? TEXT("NOT") : TEXT(""),
		Rotation.Pitch, Rotation.Yaw, Rotation.Roll,
		Translation.X, Translation.Y, Translation.Z,
		Scale.X, Scale.Y, Scale.Z);

	AddFilterStatement(FullQuery);
}

void UPointCloudView::FilterOnTile(int InNumTilesX, int InNumTilesY, int InNumTilesZ, int InTileX, int InTileY, int InTileZ, bool bInvertSelection, EFilterMode Mode)
{
	return FilterOnTile(GetResultsBoundingBox(), InNumTilesX, InNumTilesY, InNumTilesZ, InTileX, InTileY, InTileZ, bInvertSelection, Mode);
}

void UPointCloudView::FilterOnTile(
	const FBox& QueryGridBounds,
	int InNumTilesX,
	int InNumTilesY,
	int InNumTilesZ,
	int InTileX,
	int InTileY,
	int InTileZ,
	bool bInvertSelection,
	EFilterMode Mode)
{
	// Validate the input parameters
	if (InNumTilesX <= 0 || InNumTilesY <= 0 || InNumTilesZ <= 0)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Invalid number of tiles in FilterOnTile"));
		return;
	}

	const FVector TileOffset = QueryGridBounds.Min;
	const FVector TileSize = (QueryGridBounds.Max - QueryGridBounds.Min) / FVector(InNumTilesX, InNumTilesY, InNumTilesZ);
	const FVector TileMin = TileOffset + TileSize * FVector(InTileX, InTileY, InTileZ);
	const FVector TileMax = TileOffset + TileSize * FVector(InTileX + 1, InTileY + 1, InTileZ + 1);

	return FilterOnBoundingBox(FBox(TileMin, TileMax), bInvertSelection, Mode);
} 

void  UPointCloudView::FilterOnPointExpression(const FString &Query, EFilterMode Mode)
{
	if (PointCloud==nullptr)
	{
		return;
	}

	FString FullQuery; 
	
	if ( Query.Len() != 0)
	{
		if (Mode == EFilterMode::FILTER_Not)
		{
			FullQuery = FString::Printf(TEXT("SELECT Id FROM SpatialQuery WHERE NOT( %s)"), *Query);
		}
		else
		{
			FullQuery = FString::Printf(TEXT("SELECT Id FROM SpatialQuery WHERE %s"), *Query);
		}		
	}
	else
	{
		FullQuery = FString::Printf(TEXT("SELECT Id FROM SpatialQuery"));		
	}

	AddFilterStatement(FullQuery);

	return;
}

void UPointCloudView::FilterOnIndex(int32 Index, EFilterMode Mode)
{
	if (PointCloud == nullptr)
	{
		return;
	}

	FString FullQuery;
	
	if (Index == -1)
	{
		FullQuery = FString::Printf(TEXT("Select Id from SpatialQuery"));
	}
	else
	{
		FullQuery = FString::Printf(TEXT("Select Id from SpatialQuery where Id=%d"), Index);
	}

	AddFilterStatement(FullQuery);

	return;
}

void UPointCloudView::FilterOnRange(int32 StartIndex, int32 EndIndex, EFilterMode Mode)
{
	if (PointCloud==nullptr)
	{
		return;
	}
	if (EndIndex < StartIndex)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("End Index (%d) is less than start Index (%d)"), EndIndex, StartIndex);
		return;
	}

	FString FullQuery;

	if (StartIndex == -1 && EndIndex == -1)
	{
		FullQuery = FString::Printf(TEXT("SELECT Id FROM SpatialQuery"));
	}
	else if (StartIndex == EndIndex)
	{
		FullQuery = FString::Printf(TEXT("SELECT Id FROM SpatialQuery WHERE Id=%d"), StartIndex);
	}
	else
	{
		FullQuery = FString::Printf(TEXT("SELECT Id FROM SpatialQuery WHERE Id>=%d AND Id<=%d"),StartIndex, EndIndex);
	}

	AddFilterStatement(FullQuery);				

	return;
}

int32 UPointCloudView::CountResultsInBox(const FBox& Box) const
{
	int32 Result =0 ;
	
	if (PointCloud == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("View Not Initialized"));
		return Result;
	}
	
	FString SelectQuery;

	if (HasFiltersApplied())
	{
		FString ResultTable = GetFilterResultTable();
						
		SelectQuery = FString::Printf(TEXT("SELECT COUNT(%s.Id) AS NumPoints FROM (%s) INNER JOIN SpatialQuery ON %s.Id=SpatialQuery.id WHERE (SpatialQuery.Minx>%f AND SpatialQuery.Maxx<%f) AND (SpatialQuery.Miny>%f AND SpatialQuery.Maxy<%f) AND (SpatialQuery.Minz>%f and SpatialQuery.Maxz<%f)"),
												*ResultTable,*ResultTable,*ResultTable, Box.Min.X, Box.Max.X, Box.Min.Y, Box.Max.Y, Box.Min.Z, Box.Max.Z);
		
	}
	else
	{
		SelectQuery = FString::Printf(TEXT("SELECT COUNT(SpatialQuery.id) AS NumPoints FROM SpatialQuery WHERE (SpatialQuery.Minx>%f AND SpatialQuery.Maxx<%f) AND (SpatialQuery.Miny>%f AND SpatialQuery.Maxy<%f) AND (SpatialQuery.Minz>%f and SpatialQuery.Maxz<%f)"),
			Box.Min.X, Box.Max.X, Box.Min.Y, Box.Max.Y, Box.Min.Z, Box.Max.Z);
	}

	Result = PointCloud->GetValue<int>(SelectQuery, "NumPoints");
	
	return Result;	
}

FBox UPointCloudView::GetResultsBoundingBox() const
{
	FBox Result;

	if (PointCloud == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("View Not Initialized"));
		return Result;
	}
	
	if (HasFiltersApplied())
	{		
		FString ResultTable = GetFilterResultTable();
		
		const FString SelectQuery = FString::Printf(TEXT("SELECT MIN(Minx) AS minx, MIN(Miny) AS miny, MIN(Minz) AS minz, MAX(maxx) AS maxx, MAX(maxy) AS maxy, MAX(maxz) AS maxz FROM SpatialQuery INNER JOIN (%s) ON SpatialQuery.id = %s.id"), *ResultTable, *ResultTable);

		Result = PointCloud->GetValue<FBox>(SelectQuery);

		return Result;
	}
	else
	{
		// if there are no filters set on this view, then just return the bounding box of the entire point cloud
		return PointCloud->GetBounds();		
	}
}

void UPointCloudView::FilterOnMetadataPattern(const FString& MetaData, const FString& Pattern, EFilterMode Mode)
{
	if (PointCloud == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("PointCloud Is NULL"));
		return;
	}

	if (MetaData.IsEmpty())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Metadata String Is Empty"));
		return;
	}

	if (Pattern.IsEmpty())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Pattern String Is Empty"));
		return;
	}

	const FString MetaDataQuery = GetMetadataQuery();

	FString MetadataPattern = PointCloud->SanitizeAndEscapeString(Pattern);
	
	FString FullQuery;

	switch (Mode)
	{
	case EFilterMode::FILTER_Or:
	case EFilterMode::FILTER_And:
		FullQuery = FString::Printf(TEXT("SELECT Vertex_Id AS Id FROM %s WHERE Attribute_Name='%s' AND Attribute_Value GLOB('%s')"),*MetaDataQuery, *MetaData, *MetadataPattern);
		break;
	case EFilterMode::FILTER_Not:
		FullQuery = FString::Printf(TEXT("SELECT Vertex_Id AS Id FROM %s WHERE Attribute_Name='%s' AND NOT(Attribute_Value GLOB('%s'))"),*MetaDataQuery, *MetaData, *MetadataPattern);
		break;
	default:
		UE_LOG(PointCloudLog, Warning, TEXT("Mode Not Supported Defauting to OR"));
		FullQuery = FString::Printf(TEXT("SELECT Vertex_Id AS Id FROM %s WHERE Attribute_Name='%s' AND Attribute_Value GLOB('%s')"), *MetaDataQuery, *MetaData, *MetadataPattern);
	}

	AddFilterStatement(FullQuery);	
}

// Set a query that finds points that gave a given metadata value
void  UPointCloudView::FilterOnMetadata(const FString& MetaData, const FString& Value, EFilterMode Mode)
{
	if (PointCloud==nullptr)
	{
		return;
	}

	if (MetaData.IsEmpty())
	{
		return;
	}

	const FString MetaDataQuery   = GetMetadataQuery();

	FString MetadataValue = PointCloud->SanitizeAndEscapeString(Value);
	
	FString FullQuery;
	
	switch (Mode)
	{
	case EFilterMode::FILTER_Or:
	case EFilterMode::FILTER_And:
		FullQuery = FString::Printf(TEXT("SELECT Vertex_Id AS Id FROM %s WHERE Attribute_Name='%s' AND Attribute_Value='%s'"), *MetaDataQuery, *MetaData, *MetadataValue);
		break; 
	case EFilterMode::FILTER_Not:
		FullQuery = FString::Printf(TEXT("SELECT Vertex_Id AS Id FROM %s WHERE Attribute_Name='%s' AND NOT(Attribute_Value='%s')"), *MetaDataQuery, *MetaData, *MetadataValue);
		break;
	default:
		UE_LOG(PointCloudLog, Warning, TEXT("Mode Not Supported Defauting to OR"));
		FullQuery = FString::Printf(TEXT("SELECT Vertex_Id AS Id FROM %s WHERE Attribute_Name='%s' AND Attribute_Value='%s'"), *MetaDataQuery, *MetaData, *MetadataValue);		
	}
	

	AddFilterStatement(FullQuery);
					
	return ;
}

void UPointCloudView::AddFilterStatement(const FString& Statement)
{
	if (Statement.IsEmpty())
	{
		// ignore empty statements
		return; 
	}

	// check that the new filter statement is not the same as the last filter statement added to the list
	if (FilterStatementList.Num() && Statement == FilterStatementList.Last())
	{
		return; 
	}

	FilterStatementList.Add(Statement);
	DirtyHash();
}

/** Clear the list of create view statements */
void UPointCloudView::ClearFilterStatements()
{
	FilterStatementList.Empty();
}

TArray< FString > UPointCloudView::GetUniqueMetadataValues(const FString& Key) const
{
	TArray< FString > Result;

	if (PointCloud == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Point Cloud Is NULL"));
		return Result;
	}

	if (Key.IsEmpty())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Empty Name For Metadata"));
		return Result;
	}

	char* zErrMsg = nullptr;
	const FString MetaDataQuery = GetMetadataQuery();	

	FString SelectQuery;
	
	if (HasFiltersApplied() == false)
	{
		SelectQuery = FString::Printf(TEXT("SELECT DISTINCT Attribute_Value FROM %s WHERE Attribute_Name=\'%s\'"), *MetaDataQuery, *Key);
	}
	else
	{	
		FString ResultTable = GetFilterResultTable();

		if (ResultTable.IsEmpty())
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Cannot Get Temporary Table for Attribute %s"), *Key);
			return Result;
		}

		SelectQuery = FString::Printf(TEXT("SELECT DISTINCT Attribute_Value FROM %s INNER JOIN (%s) ON %s.Vertex_ID = ID WHERE Attribute_Name=\'%s\'"), *MetaDataQuery, *ResultTable, *MetaDataQuery, *Key);
	}

	Result = PointCloud->GetValueArray<FString>(SelectQuery);

	return Result;
}

TMap<FString, int> UPointCloudView::GetUniqueMetadataValuesAndCounts(const FString& Key) const
{
	TMap<FString, int> Result;

	if (PointCloud == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Point Cloud Is NULL"));
		return Result;
	}

	if (Key.IsEmpty())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Empty Name For Metadata"));
		return Result;
	}
	
	const FString MetaDataQuery = GetMetadataQuery();
	FString GetInstanceAndCountQuery;

	if (HasFiltersApplied() == false)
	{
		// Get the temporary table for the given attribute
		FString AttributeTempTable = PointCloud->GetTemporaryAttributeTable(Key);
		if (AttributeTempTable.IsEmpty())
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Cannot Get Temporary Table for Attribute %s"), *Key);
			return Result;
		}
		 
		TArray<FStringFormatArg> args;
		args.Add(FStringFormatArg(AttributeTempTable));
		
		GetInstanceAndCountQuery = FString::Format(TEXT("SELECT AttributeValues.Value AS VALUE, COUNT({0}.ValueId) AS COUNT from Vertex INNER JOIN {0}  ON Vertex.rowid = {0}.Id JOIN AttributeValues ON ValueId=AttributeValues.rowid GROUP BY {0}.ValueId"), args);
	}
	else
	{
		FString ResultTable = GetFilterResultTable();

		if (ResultTable.IsEmpty())
		{
			return Result;
		}
		
		// Get the temporary table for the given attribute
		FString AttributeTempTable =  PointCloud->GetTemporaryAttributeTable(Key);
		if (AttributeTempTable.IsEmpty())
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Cannot Get Temporary Table for Attribute %s"), *Key);
			return Result;
		}

		TArray< FStringFormatArg > args;
		args.Add(FStringFormatArg(AttributeTempTable));
		args.Add(FStringFormatArg(ResultTable));
		
		GetInstanceAndCountQuery = FString::Format(TEXT("SELECT AttributeValues.Value AS VALUE, COUNT({0}.ValueId) AS COUNT from {1} INNER JOIN {0}  ON {1}.Id = {0}.Id JOIN AttributeValues ON ValueId=AttributeValues.rowid GROUP BY {0}.ValueId"), args);
	}

	Result = PointCloud->GetValueMap<FString, int>(GetInstanceAndCountQuery, "VALUE", "COUNT");

	return Result;
}

TArray<TPair<TArray<FString>, int32>> UPointCloudView::GetUniqueMetadataValuesAndCounts(const TArray<FString>& Keys) const
{
	TArray<TPair<TArray<FString>, int32>> Result;

	if (Keys.Num() == 0)
	{
		return Result;
	}
	else if (TSet<FString>(Keys).Num() != Keys.Num())
	{
		UE_LOG(PointCloudLog, Error, TEXT("Cannot use duplicate metadata keys"));
		return Result;
	}

	TArray<FString> AttributeTempTables;

	for (const FString& Key : Keys)
	{
		const FString AttributeTempTable = PointCloud->GetTemporaryAttributeTable(Key);
		if (AttributeTempTable.IsEmpty())
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Cannot Get Temporary Table for Attribute %s"), *Key);
			return Result;
		}

		AttributeTempTables.Add(AttributeTempTable);
	}

	FString SpatialResultTable = HasFiltersApplied() ? GetFilterResultTable() : TEXT("Vertex");
	FString SpatialIdField = HasFiltersApplied() ? TEXT("Id") : TEXT("rowid");

	if (SpatialResultTable.IsEmpty())
	{
		return Result;
	}

	TStringBuilder<4096> Builder;
	Builder.Append(TEXT("SELECT "));
		
	for (int32 AttributeIndex = 0; AttributeIndex < AttributeTempTables.Num(); ++AttributeIndex)
	{
		Builder.Appendf(TEXT("AT%d.Value as %s, "), AttributeIndex + 1, *Keys[AttributeIndex]);
	}
		
	Builder.Appendf(TEXT("COUNT(*) as COUNT FROM %s "), *SpatialResultTable);
		
	for (int32 AttributeIndex = 0; AttributeIndex < AttributeTempTables.Num(); ++AttributeIndex)
	{
		Builder.Appendf(TEXT("INNER JOIN %s ON %s.%s = %s.Id "), *AttributeTempTables[AttributeIndex], *SpatialResultTable, *SpatialIdField, *AttributeTempTables[AttributeIndex]);
	}
		
	for (int32 AttributeIndex = 0; AttributeIndex < AttributeTempTables.Num(); ++AttributeIndex)
	{
		Builder.Appendf(TEXT("JOIN AttributeValues as AT%d ON %s.ValueId=AT%d.rowid "), AttributeIndex + 1, *AttributeTempTables[AttributeIndex], AttributeIndex + 1);
	}

	Builder.Appendf(TEXT("GROUP BY %s.ValueId"), *AttributeTempTables[0]);

	for (int32 AttributeIndex = 1; AttributeIndex < AttributeTempTables.Num(); ++AttributeIndex)
	{
		Builder.Appendf(TEXT(", %s.ValueId"), *AttributeTempTables[AttributeIndex]);
	}

	const FString SelectQuery = Builder.ToString();

	if (!SelectQuery.IsEmpty())
	{
		Result = PointCloud->GetValuePairArray<TArray<FString>, int>(SelectQuery, Keys, { "COUNT" });
	}

	return Result;
}

template<typename T>
TArray<T> UPointCloudView::GetMetadataValuesArray(const FString& Key) const
{
	TArray<T> Result;

	if (PointCloud == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Point Cloud Is NULL"));
		return Result;
	}

	if (Key.IsEmpty())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Empty Key For Metadata"));
		return Result;
	}

	FString SelectQuery;

	if (HasFiltersApplied() == false)
	{
		const FString MetaDataQuery = GetMetadataQuery();
		SelectQuery = FString::Printf(TEXT("SELECT Attribute_Value FROM %s WHERE Attribute_Name=\'%s\'"), *MetaDataQuery, *Key);
	}
	else
	{
		FString ResultTable = GetFilterResultTable();

		if (ResultTable.IsEmpty())
		{
			return Result;
		}

		// Get the temporary table for the given attribute
		FString AttributeTempTable = PointCloud->GetTemporaryAttributeTable(Key);
		if (AttributeTempTable.IsEmpty())
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Cannot Get Temporary Table for Attribute %s"), *Key);
			return Result;
		}

		TArray< FStringFormatArg > args;
		args.Add(FStringFormatArg(AttributeTempTable));
		args.Add(FStringFormatArg(ResultTable));

		SelectQuery = FString::Format(TEXT("SELECT AttributeValues.Value AS Attribute_Value FROM {1} INNER JOIN {0} ON {1}.Id = {0}.Id JOIN AttributeValues ON ValueId=AttributeValues.rowid"), args);
	}

	Result = PointCloud->GetValueArray<T>(SelectQuery);

	return Result;
}

TArray<int> UPointCloudView::GetMetadataValuesArrayAsInt(const FString& Key) const
{
	return GetMetadataValuesArray<int>(Key);
}

TArray<float> UPointCloudView::GetMetadataValuesArrayAsFloat(const FString& Key) const
{
	return GetMetadataValuesArray<float>(Key);
}

TMap<int, FString> UPointCloudView::GetMetadataValues(const FString& Key) const
{
	TMap<int, FString> Result;

	if (PointCloud == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Point Cloud Is NULL"));
		return Result;
	}

	if (Key.IsEmpty())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Empty Key For Metadata"));
		return Result;
	}

	FString SelectQuery;

	if (HasFiltersApplied()==false)
	{
		const FString MetaDataQuery = GetMetadataQuery();
		SelectQuery = FString::Printf(TEXT("SELECT Vertex_Id, Attribute_Value FROM %s WHERE Attribute_Name=\'%s\'"), *MetaDataQuery, *Key);
	}
	else
	{
		FString ResultTable = GetFilterResultTable();

		if (ResultTable.IsEmpty())
		{
			return Result;
		}

		// Get the temporary table for the given attribute
		FString AttributeTempTable = PointCloud->GetTemporaryAttributeTable(Key);
		if (AttributeTempTable.IsEmpty())
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Cannot Get Temporary Table for Attribute %s"), *Key);
			return Result;
		}

		TArray< FStringFormatArg > args;
		args.Add(FStringFormatArg(AttributeTempTable));
		args.Add(FStringFormatArg(ResultTable));

		SelectQuery = FString::Format(TEXT("SELECT {1}.Id AS Vertex_Id, AttributeValues.Value AS Attribute_Value from {1} INNER JOIN {0} ON {1}.Id = {0}.Id JOIN AttributeValues ON ValueId=AttributeValues.rowid"), args);
	}

	Result = PointCloud->GetValueMap<int, FString>(SelectQuery, TEXT("Vertex_Id"), TEXT("Attribute_Value"));

	return Result;
}

TMap<FString, FString> UPointCloudView::GetMetadata(int32 Index) const
{
	TMap<FString, FString> Result;

	if (PointCloud == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Point Cloud Is NULL"));
		return Result;
	}

	const FString SelectQuery = FString::Printf(TEXT("SELECT Attribute_Name, Attribute_Value FROM %s WHERE Vertex_Id=%d"), *GetMetadataQuery(), Index);
	Result = PointCloud->GetValueMap<FString, FString>(SelectQuery, TEXT("Attribute_Name"), TEXT("Attribute_Value"));

	return Result;
}

FString UPointCloudView::GetMetadataQuery() const
{	
	return FString("Metadata");	
}

bool UPointCloudView::HasFiltersApplied() const
{
	return (FilterStatementList.Num() != 0 || (ParentView && ParentView->HasFiltersApplied()));
}

int UPointCloudView::GetFilterCount() const
{
	return FilterStatementList.Num() + (ParentView ? ParentView->GetFilterCount() : 0);
}

void UPointCloudView::PreCacheFilters()
{
	// requesting the table causes it to be cached
	GetFilterResultTable(/*bSilentOnNoFilter=*/true);
}

FString UPointCloudView::GetFilterResultTable(bool bSilentOnNoFilter) const
{
	const TArray<FString> Filters = GetFilterStatements();

	if (Filters.Num() == 0)
	{
		if (!bSilentOnNoFilter)
		{
			UE_LOG(PointCloudLog, Warning, TEXT("No Filters On View. GetTransformsIntermediates Only works with Views on which filters have been applied"));
		}

		return FString();
	}

	FString TableName;

	if (Filters.Num() == 1)
	{
		TableName = PointCloud->GetTemporaryQueryTable(Filters[0]);
	}
	else if (Filters.Num() > 1)
	{
		TableName = PointCloud->GetTemporaryQueryTable(Filters[0]);
		// go through each pair of filters, creating intersection tables for them in order
		for (int i = 1; i < Filters.Num(); i++)
		{
			TableName = PointCloud->GetTemporaryIntersectionTable(UPointCloudImpl::EArgumentType::Table, TableName, UPointCloudImpl::EArgumentType::Query, Filters[i]);
		}
	}
	
	return TableName;
}

TArray<FString> UPointCloudView::GetFilterStatements() const
{
	TArray<FString> Result;

	if (ParentView != nullptr)
	{
		Result += ParentView->GetFilterStatements();
	}

	Result.Append(FilterStatementList);
	
	return Result;
}

int UPointCloudView::GetIndexes(UPARAM(ref) TArray<int32>& OutIds) const
{
	if (PointCloud == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Point Cloud Is NULL"));
		return 0;
	}

	if (HasFiltersApplied() == false)
	{
		// return all of the vertex ids
		const FString GetIdsQuery = FString::Printf(TEXT("SELECT rowid AS Id FROM Vertex; "));		
		OutIds = PointCloud->GetValueArray<int>(GetIdsQuery);
		return OutIds.Num();
	}

	const FString ResultTableName = GetFilterResultTable();

	if (ResultTableName.IsEmpty())
	{
		return 0;
	}

	const FString GetIdsQuery = FString::Printf(TEXT("SELECT Id FROM %s; "), *ResultTableName);

	OutIds = PointCloud->GetValueArray<int>(GetIdsQuery);

	return OutIds.Num();
}

TArray<FTransform> UPointCloudView::GetTransforms() const
{
	if (PointCloud == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Point Cloud Is NULL"));
		return TArray<FTransform>();
	}

	FString GetTransformsQuery;

	if (HasFiltersApplied() == false)
	{
		GetTransformsQuery = FString::Printf(TEXT("SELECT Vertex.x, Vertex.y, Vertex.z, Vertex.nx, Vertex.ny, Vertex.nz, Vertex.nw, Vertex.sx, Vertex.sy, Vertex.sz FROM Vertex"));
	}
	else
	{
		// return all of the transforms remaining after the filters have been applied
		const FString ResultTableName = GetFilterResultTable();

		if (ResultTableName.IsEmpty())
		{
			return TArray<FTransform>();
		}

		GetTransformsQuery = FString::Printf(TEXT("SELECT Vertex.x, Vertex.y, Vertex.z, Vertex.nx, Vertex.ny, Vertex.nz, Vertex.nw, Vertex.sx, Vertex.sy, Vertex.sz FROM %s INNER JOIN Vertex ON Id = Vertex.rowid; "), *ResultTableName);
	}

	return PointCloud->GetValueArray<FTransform>(GetTransformsQuery);
}

TArray<TPair<int32, FTransform>> UPointCloudView::GetPerIdTransforms() const
{
	if (PointCloud == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Point Cloud Is NULL"));
		return TArray<TPair<int32, FTransform>>();
	}

	FString GetIdAndTransformQuery;

	if (HasFiltersApplied() == false)
	{
		GetIdAndTransformQuery = FString::Printf(TEXT("SELECT rowid as Id, Vertex.x, Vertex.y, Vertex.z, Vertex.nx, Vertex.ny, Vertex.nz, Vertex.nw, Vertex.sx, Vertex.sy, Vertex.sz FROM Vertex"));
	}
	else
	{
		// return all of the transforms / ids remaining after the filters have been applied
		const FString ResultTableName = GetFilterResultTable();

		if (ResultTableName.IsEmpty())
		{
			return TArray<TPair<int32, FTransform>>();
		}

		GetIdAndTransformQuery = FString::Printf(TEXT("SELECT Id, Vertex.x, Vertex.y, Vertex.z, Vertex.nx, Vertex.ny, Vertex.nz, Vertex.nw, Vertex.sx, Vertex.sy, Vertex.sz FROM %s INNER JOIN Vertex ON Id = Vertex.rowid; "), *ResultTableName);
	}

	return PointCloud->GetValuePairArray<int, FTransform>(GetIdAndTransformQuery);
}

int UPointCloudView::GetTransformsAndIds(TArray<FTransform>& OutTransforms, TArray<int32>& OutIds) const
{
	TArray<TPair<int32, FTransform>> TransformsAndIds = GetPerIdTransforms();

	OutTransforms.Reserve(TransformsAndIds.Num());
	OutIds.Reserve(TransformsAndIds.Num());

	for (const auto& Entry : TransformsAndIds)
	{
		OutTransforms.Add(Entry.Value);
		OutIds.Add(Entry.Key);
	}

	return OutTransforms.Num();
}

FString UPointCloudView::GetValuesAndTransformsHash(const TArray<FString>& Keys) const
{
	if (Keys.Num() == 0)
	{
		return FString();
	}
	else if (TSet<FString>(Keys).Num() != Keys.Num())
	{
		UE_LOG(PointCloudLog, Error, TEXT("Cannot use duplicate metadata keys in hash computation"));
		return FString();
	}

	TArray<FString> AttributeTempTables;

	for (const FString& Key : Keys)
	{
		const FString AttributeTempTable = PointCloud->GetTemporaryAttributeTable(Key);
		if (AttributeTempTable.IsEmpty())
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Cannot Get Temporary Table for Attribute %s"), *Key);
			return FString();
		}

		AttributeTempTables.Add(AttributeTempTable);
	}

	FString SpatialResultTable = HasFiltersApplied() ? GetFilterResultTable() : TEXT("Vertex");
	FString SpatialIdField = HasFiltersApplied() ? TEXT("Id") : TEXT("rowid");

	if (SpatialResultTable.IsEmpty())
	{
		return FString();
	}

	TStringBuilder<4096> Builder;
	Builder.Append(TEXT("SELECT "));

	for (int32 AttributeIndex = 0; AttributeIndex < AttributeTempTables.Num(); ++AttributeIndex)
	{
		Builder.Appendf(TEXT("AT%d.Value as %s, "), AttributeIndex + 1, *Keys[AttributeIndex]);
	}

	Builder.Append(TEXT("Vertex.x, Vertex.y, Vertex.z, Vertex.nx, Vertex.ny, Vertex.nz, Vertex.nw, Vertex.sx, Vertex.sy, Vertex.sz "));

	Builder.Appendf(TEXT("FROM %s "), *SpatialResultTable);

	if (HasFiltersApplied())
	{
		Builder.Appendf(TEXT("INNER JOIN Vertex ON %s.Id = Vertex.rowid "), *SpatialResultTable);
	}

	for (int32 AttributeIndex = 0; AttributeIndex < AttributeTempTables.Num(); ++AttributeIndex)
	{
		Builder.Appendf(TEXT("INNER JOIN %s ON %s.%s = %s.Id "), *AttributeTempTables[AttributeIndex], *SpatialResultTable, *SpatialIdField, *AttributeTempTables[AttributeIndex]);
	}

	for (int32 AttributeIndex = 0; AttributeIndex < AttributeTempTables.Num(); ++AttributeIndex)
	{
		Builder.Appendf(TEXT("JOIN AttributeValues as AT%d ON %s.ValueId=AT%d.rowid "), AttributeIndex + 1, *AttributeTempTables[AttributeIndex], AttributeIndex + 1);
	}

	const FString QueryToHash = Builder.ToString();

	return HashQueryResults(QueryToHash);
}

UPointCloud* UPointCloudView::GetPointCloud() const
{
	return PointCloud.Get();
}
// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudSqliteHelpers.h"
#include "IncludeSQLite.h"

namespace PointCloudSqliteHelpers
{
	void ResultRetrieval(sqlite3_stmt* stmt, int, int* ColumnIndices, int& ReadColumns, int& Value)
	{
		int ColumnIndex = *ColumnIndices == -1 ? ReadColumns : *ColumnIndices;
		++ReadColumns;
		Value = (int)sqlite3_column_int(stmt, ColumnIndex);
	}

	void ResultRetrieval(sqlite3_stmt* stmt, int, int* ColumnIndices, int& ReadColumns, float& Value)
	{
		int ColumnIndex = *ColumnIndices == -1 ? ReadColumns : *ColumnIndices;
		++ReadColumns;
		Value = (float)sqlite3_column_double(stmt, ColumnIndex);
	}

	void ResultRetrieval(sqlite3_stmt* stmt, int, int* ColumnIndices, int& ReadColumns, double& Value)
	{
		int ColumnIndex = *ColumnIndices == -1 ? ReadColumns : *ColumnIndices;
		++ReadColumns;
		Value = sqlite3_column_double(stmt, ColumnIndex);
	}

	void ResultRetrieval(sqlite3_stmt* stmt, int, int* ColumnIndices, int& ReadColumns, FString& Value)
	{
		int ColumnIndex = *ColumnIndices == -1 ? ReadColumns : *ColumnIndices;
		++ReadColumns;
		Value = (const char*)sqlite3_column_text(stmt, ColumnIndex);
	}

	void ResultRetrieval(sqlite3_stmt* stmt, int, int* ColumnIndices, int& ReadColumns, FBox& Value)
	{
		FVector MinValue(FVector::ZeroVector);
		FVector MaxValue(FVector::ZeroVector);

		int Index = *ColumnIndices == -1 ? ReadColumns : *ColumnIndices;
		ReadColumns += 6;
		MinValue.X = sqlite3_column_double(stmt, Index++);
		MinValue.Y = sqlite3_column_double(stmt, Index++);
		MinValue.Z = sqlite3_column_double(stmt, Index++);
		MaxValue.X = sqlite3_column_double(stmt, Index++);
		MaxValue.Y = sqlite3_column_double(stmt, Index++);
		MaxValue.Z = sqlite3_column_double(stmt, Index++);

		Value = FBox(MinValue, MaxValue);
	}

	void ResultRetrieval(sqlite3_stmt* stmt, int, int* ColumnIndices, int& ReadColumns, FTransform& Value)
	{
		FTransform Transform;
		FVector Translation;
		FVector Scale(FVector::OneVector);
		FVector4 Orient;

		int Index = *ColumnIndices == -1 ? ReadColumns : *ColumnIndices;
		ReadColumns += 10;

		Translation.X = sqlite3_column_double(stmt, Index++);
		Translation.Y = sqlite3_column_double(stmt, Index++);
		Translation.Z = sqlite3_column_double(stmt, Index++);
		Orient.X = sqlite3_column_double(stmt, Index++);
		Orient.Y = sqlite3_column_double(stmt, Index++);
		Orient.Z = sqlite3_column_double(stmt, Index++);
		Orient.W = sqlite3_column_double(stmt, Index++);
		Scale.X = sqlite3_column_double(stmt, Index++);
		Scale.Y = sqlite3_column_double(stmt, Index++);
		Scale.Z = sqlite3_column_double(stmt, Index++);

		Transform.SetTranslation(Translation);
		Transform.SetRotation(FQuat(Orient.X, Orient.Y, Orient.Z, Orient.W));
		Transform.SetScale3D(Scale);

		Value = Transform;
	}

	template<typename U>
	void ResultRetrieval(sqlite3_stmt* stmt, int NumElements, int* ColumnIndices, int& ReadColumns, TArray<U>& Values)
	{
		check(NumElements > 0);

		Values.SetNum(NumElements);

		for (int ElementIndex = 0; ElementIndex < NumElements; ++ElementIndex)
		{
			ResultRetrieval(stmt, 1, &ColumnIndices[ElementIndex], ReadColumns, Values[ElementIndex]);
		}
	}

	// Template instantiation
	template void ResultRetrieval(sqlite3_stmt* stmt, int NumElements, int* ColumnIndices, int& ReadColumns, TArray<int>& Values);
	template void ResultRetrieval(sqlite3_stmt* stmt, int NumElements, int* ColumnIndices, int& ReadColumns, TArray<float>& Values);
	template void ResultRetrieval(sqlite3_stmt* stmt, int NumElements, int* ColumnIndices, int& ReadColumns, TArray<double>& Values);
	template void ResultRetrieval(sqlite3_stmt* stmt, int NumElements, int* ColumnIndices, int& ReadColumns, TArray<FString>& Values);
	template void ResultRetrieval(sqlite3_stmt* stmt, int NumElements, int* ColumnIndices, int& ReadColumns, TArray<FBox>& Values);
	template void ResultRetrieval(sqlite3_stmt* stmt, int NumElements, int* ColumnIndices, int& ReadColumns, TArray<FTransform>& Values);
}
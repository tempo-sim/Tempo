// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct sqlite3;
struct sqlite3_stmt;

namespace PointCloudSqliteHelpers
{
	void ResultRetrieval(sqlite3_stmt* stmt, int NumElements, int* ColumnIndices, int& ReadColumns, int& Value);
	void ResultRetrieval(sqlite3_stmt* stmt, int NumElements, int* ColumnIndices, int& ReadColumns, float& Value);
	void ResultRetrieval(sqlite3_stmt* stmt, int NumElements, int* ColumnIndices, int& ReadColumns, double& Value);
	void ResultRetrieval(sqlite3_stmt* stmt, int NumElements, int* ColumnIndices, int& ReadColumns, FString& Value);
	void ResultRetrieval(sqlite3_stmt* stmt, int NumElements, int* ColumnIndices, int& ReadColumns, FBox& Value);
	void ResultRetrieval(sqlite3_stmt* stmt, int NumElements, int* ColumnIndices, int& ReadColumns, FTransform& Value);

	template<typename T>
	void ResultRetrieval(sqlite3_stmt* stmt, int NumElements, int* ColumnIndices, int& ReadColumns, TArray<T>& Values);
}
// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudQuery.h"
#include "PointCloudUtils.h"
#include "PointCloudSQLExtensions.h"

#include "IncludeSQLite.h"

#define LOCTEXT_NAMESPACE "PointCloudQuery"

// Convenience macro
#define QUERY_LOG(Query, Label) PointCloud::QueryLogger Logger(Cloud, Query, Label, __FILE__, __LINE__)

FPointCloudQuery::FPointCloudQuery(UPointCloudImpl* InCloud) : Cloud(InCloud), Statement(nullptr)
{

} 

FPointCloudQuery::~FPointCloudQuery()
{
	End();
}

bool FPointCloudQuery::SetQuery(const FString& InQuery)
{
	Query = InQuery;

	if (!Cloud)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Cloud is Null"));
		return false;
	}

	if (Statement)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Statement already initialized"));
		return false;
	}

	QUERY_LOG(Query, TEXT("Set Query"));

	if (sqlite3_prepare_v2(Cloud->InternalDatabase, TCHAR_TO_ANSI(*Query), -1, &Statement, 0) != SQLITE_OK)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Prepare Statement Failed"));
		return false;
	}

	return true;
}

bool FPointCloudQuery::Begin()
{
	if (!Cloud || !Statement)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Null Cloud or Statement"));
		return false;
	}

	return true;
}

bool FPointCloudQuery::Step(const TArray<FString>& Values)
{
	if (!Cloud || !Statement)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Null Cloud or Statement"));
		return false;
	}

	QUERY_LOG(FString(), TEXT("Step(const TArray<FString>& Values)"));

	for (int i = 0; i < Values.Num(); i++)
	{
		if (sqlite3_bind_text16(Statement, i + 1, *Values[i], -1, SQLITE_TRANSIENT) != SQLITE_OK)
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Bind Attribute %d failed"), i);
			return false;
		}
	}

	int StepResult = sqlite3_step(Statement);

	if (StepResult != SQLITE_DONE)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Step Failed - %s"), ANSI_TO_TCHAR(sqlite3_errstr(StepResult)));
		return false;
	}
	
	sqlite3_clear_bindings(Statement);
	int rc = sqlite3_reset(Statement);

	if (rc != SQLITE_OK)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Cleanup Failed"));
		return false;
	}

	return true;
}

bool FPointCloudQuery::Step(int32 ValueA, int32 ValueB, int32 ValueC)
{
	if (!Cloud || !Statement)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Null Cloud or Statement"));
		return false;
	}

	QUERY_LOG(FString(), TEXT("const TArray<char>& Values"));

	if (sqlite3_bind_int(Statement, 1, ValueA) != SQLITE_OK)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Bind Attribute 1 failed"));
		return false;
	}

	if (sqlite3_bind_int(Statement, 2, ValueB) != SQLITE_OK)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Bind Attribute 2 failed"));
		return false;
	}


	if (sqlite3_bind_int(Statement, 3, ValueC) != SQLITE_OK)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Bind Attribute 3 failed"));
		return false;
	}

	int StepResult = sqlite3_step(Statement);

	if (StepResult != SQLITE_DONE)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Step Failed - %s"), ANSI_TO_TCHAR(sqlite3_errstr(StepResult)));
		return false;
	}
	
	sqlite3_clear_bindings(Statement);
	int rc = sqlite3_reset(Statement);

	if (rc != SQLITE_OK)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Cleanup Failed"));
		return false;
	}

	return true;
}

bool FPointCloudQuery::Step(const TArray<char>& Values)
{
	if (!Cloud || !Statement)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Null Cloud or Statement"));
		return false;
	}

	QUERY_LOG(FString(), TEXT("const TArray<char>& Values"));

	if (sqlite3_bind_text(Statement, 1, Values.GetData(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Bind Attribute 1 failed"));
		return false;
	}
	

	int StepResult = sqlite3_step(Statement);

	if (StepResult != SQLITE_DONE)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Step Failed - %s"), ANSI_TO_TCHAR(sqlite3_errstr(StepResult)));
		return false;
	}
	
	sqlite3_clear_bindings(Statement);
	int rc = sqlite3_reset(Statement);

	if (rc != SQLITE_OK)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Cleanup Failed"));
		return false;
	}

	return true;
}

bool FPointCloudQuery::Step()
{
	if (!Cloud || !Statement)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Null Cloud or Statement"));
		return false;
	}

	QUERY_LOG(FString(), TEXT("bool FPointCloudQuery::Step()"));

	int StepResult = sqlite3_step(Statement);

	if (StepResult != SQLITE_DONE)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Step Failed - %s"), ANSI_TO_TCHAR(sqlite3_errstr(StepResult)));
		return false;
	}
	
	sqlite3_clear_bindings(Statement);
	int rc = sqlite3_reset(Statement);

	if (rc != SQLITE_OK)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Cleanup Failed"));
		return false;
	}

	return true;
}

bool FPointCloudQuery::Step(const TPair<int, TArray<char> >& Value)
{
	if (!Cloud || !Statement)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Null Cloud or Statement"));
		return false;
	}

	QUERY_LOG(FString(), TEXT("Step(const TPair<int, TArray<char> >& Value)"));

	if (sqlite3_bind_int(Statement, 1, Value.Key) != SQLITE_OK)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Bind Attribute %d failed"), 1);
		return false;
	}

	if (sqlite3_bind_text(Statement, 2, Value.Value.GetData(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Bind Attribute %d failed"), 2);
		return false;
	}

	int StepResult = sqlite3_step(Statement);

	if (StepResult != SQLITE_DONE)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Step Failed - %s"), ANSI_TO_TCHAR(sqlite3_errstr(StepResult)));
		return false;
	}
	
	sqlite3_clear_bindings(Statement);
	int rc = sqlite3_reset(Statement);

	if (rc != SQLITE_OK)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Cleanup Failed"));
		return false;
	}

	return true;
}
 
bool FPointCloudQuery::Step(const TArray<float>& Values)
{
	if (!Cloud || !Statement)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Null Cloud or Statement"));
		return false;
	}

	QUERY_LOG(FString(), TEXT("bool FPointCloudQuery::Step(const TArray<float>& Values)"));

	for (int i = 0; i < Values.Num(); i++)
	{
		if (sqlite3_bind_double(Statement, i + 1, static_cast<double>(Values[i])) != SQLITE_OK)
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Bind Attribute %d failed"), i);
			return false;
		}
	}

	int StepResult = sqlite3_step(Statement);

	if (StepResult != SQLITE_DONE)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Step Failed - %s"), ANSI_TO_TCHAR(sqlite3_errstr(StepResult)));
		return false;
	}

	sqlite3_clear_bindings(Statement);
	int rc = sqlite3_reset(Statement);

	if (rc != SQLITE_OK)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Cleanup Failed"));
		return false;
	}

	return true;
}

bool FPointCloudQuery::Step(const TArray<int>& Values, FPointCloudQuery::FRowHandler* Handler)
{
	if (!Cloud || !Statement)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Null Cloud or Statement"));
		return false;
	}

	QUERY_LOG(FString(), TEXT("bool FPointCloudQuery::Step(const TArray<int>&Values, FPointCloudQuery::FRowHandler * Handler)"));

	for (int i = 0; i < Values.Num(); i++)
	{
		if (sqlite3_bind_int(Statement, i + 1, Values[i]) != SQLITE_OK)
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Bind Attribute %d failed"), i);
			return false;
		}
	}

	int Rc = 0;

	do
	{
		Rc = sqlite3_step(Statement);
		if (Rc == SQLITE_ROW)
		{
			if (Handler)
			{
				Handler->Handle(Statement);
			}
		}		
	} while (Rc == SQLITE_ROW);

	if (Rc != SQLITE_DONE)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Step Failed - %s"), ANSI_TO_TCHAR(sqlite3_errstr(Rc)));
		return false;
	}
			
	sqlite3_clear_bindings(Statement);
	int rc = sqlite3_reset(Statement);

	if (rc != SQLITE_OK)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Cleanup Failed"));
		return false;
	}
	
	return true;
}

bool FPointCloudQuery::End()
{
	if (!Cloud || !Statement)
	{
		return false;
	}

	if (sqlite3_finalize(Statement) != SQLITE_OK)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Finalize Failed"));
		return false;
	}

	Statement = nullptr;
	Cloud = nullptr;
	Query = FString();

	return true;
}

FString FPointCloudQuery::GetHash(int HashType/* = 256*/, bool bIncludeQuery/* = true*/) const
{
	FString Result;

	if (!Cloud || !Statement)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Null Cloud or Statement"));
		return Result;
	}

	FString HashQuery = Query;
	HashQuery.RemoveFromEnd(TEXT(" LIMIT ? OFFSET ?"));

	const FString FinalQuery = FString::Printf(TEXT("SELECT SHA3_QUERY(\"%s\", %i, %i)"), *HashQuery, HashType, (int8)bIncludeQuery);

	Cloud->RunQuery(FinalQuery, SQLExtension::Sha3CallBack, &Result, __FILE__, __LINE__);

	return Result;
}

#undef LOCTEXT_NAMESPACE
#undef QUERY_LOG

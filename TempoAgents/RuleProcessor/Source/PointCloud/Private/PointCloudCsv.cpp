// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudCsv.h"
#include "PointCloud.h"
#include "PointCloudUtils.h"
#include "Runtime/Core/Public/Async/ParallelFor.h"
#include "Misc/FileHelper.h"

#define LOCTEXT_NAMESPACE "PointCloudCsv"

FPointCloudCsv::FPointCloudCsv(const TArray<FString> &InStrings, FFeedbackContext* Warn)
{
	PointCloud::UtilityTimer Timer;
	IsOpen = true;
							
	// if we don't have at least the column names line and one data line, consider this file invalid
	if (InStrings.Num() < 2)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Malformed CSV. Less than 2 Lines\n"));				
		IsOpen = false;
		return;
	}

	// read in the column names
	if (InStrings[0].ParseIntoArray(ColumnNames, TEXT(","), true) == 0)
	{
		// if we don't have at least one column name then consider this file invalid
		UE_LOG(PointCloudLog, Warning, TEXT("Malformed CSV. Cannot Read Column Names From Line 0\n"));				
		IsOpen = false;
		return;
	}

	// We want to have direct index access to the column arrays
	// So we'll cache those in this array
	TArray<TArray<FString>*> IndexToColumns;
	IndexToColumns.SetNum(GetColumnCount());
			
	// resize each of the column records to the right value
	for (int i = 0; i < GetColumnCount(); i++)
	{
		Columns.FindOrAdd(GetColumnName(i));	
	}

	// Number of lines excluding the Column Headers
	int32 NumLines = InStrings.Num()-1;

	// Get pointers to the Columns Arrays
	for (int i = 0; i < GetColumnCount(); i++)
	{
		IndexToColumns[i] = Columns.Find(GetColumnName(i));
		// Resize each Column Record to the correct Size 
		IndexToColumns[i]->SetNum(NumLines);
	}

	// Cache the number of columns
	int32 ColumnCount = GetColumnCount();	

	FThreadSafeCounter SafeRowCount(0);


	// In Parallel loop over all of the incoming rows and parse them into the corrisponding columns
	ParallelFor(NumLines, [&](int32 i) 
	{
		TArray<FString> Values;

		// Note +1 to skip over the column headers
		if (InStrings[i+1].ParseIntoArray(Values, TEXT(","), false) != ColumnCount)
		{
			// malformed line
			UE_LOG(PointCloudLog, Warning, TEXT("Malformed CSV Line %d\n"), i);
		}
		else
		{
			SafeRowCount.Increment();
			// Add each of the values to relevant column record
			for (int c = 0; c < Values.Num(); c++)
			{
				// Use Move Semanics to Avoid a copy into the Column record
				IndexToColumns[c]->operator[](i) = MoveTemp(Values[c]);	
			}
		}
	});

	RowCount = SafeRowCount.GetValue();

	UE_LOG(PointCloudLog, Log, TEXT("Row Count %d\n"), RowCount);

	// Trim the columns back to the number of valid rows
	for (auto &a : Columns)
	{
		if(a.Value.Num()>RowCount)
		{
			a.Value.RemoveAt(RowCount, a.Value.Num() - RowCount);
		}				
	}
		
	Timer.Report(TEXT("Process PSV"));
}
				
FPointCloudCsv FPointCloudCsv::Open(const FString& Name, FFeedbackContext* Warn)
{
	TArray<FString> InStrings;

	PointCloud::UtilityTimer Timer;
						
	if (!FFileHelper::LoadANSITextFileToStrings(*Name, nullptr, InStrings))
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Cannot open file CSV: %s\n"), *Name);
		return FPointCloudCsv();
	}

	Timer.Report(TEXT("Load PSV From Disk"));

	return FPointCloudCsv(InStrings,Warn);
}

bool FPointCloudCsv::GetIsOpen()
{
	return IsOpen;
}

const TArray<FString> & FPointCloudCsv::GetColumnNames() const
{			
	return ColumnNames;
}

TArray<FString> * FPointCloudCsv::GetColumn(const FString &Name) 
{			
	if (ColumnNames.Contains(Name) == false)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Requested column (%s) Not Found"), *Name);
		return nullptr;
	}
	return Columns.Find(Name);
}

FString FPointCloudCsv::GetColumnName(int32 Index) const
{
	if (Index < 0 || Index >= GetColumnCount())
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Requested column (%d) is out of range (%d)"), Index, GetColumnCount());
		return FString();
	}
	return ColumnNames[Index];
}

int32 FPointCloudCsv::GetColumnCount() const
{			
	return ColumnNames.Num();
}

int32 FPointCloudCsv::GetRowCount() const
{			
	return RowCount;
}	

#undef LOCTEXT_NAMESPACE 

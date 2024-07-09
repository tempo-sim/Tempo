// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Object.h"

/**
 * Small, simple CSV file reader for RuleProcessor. Loads a given file and can return the values from the CSV file as columns 
 */
class FPointCloudCsv
{
public:

	/**
	* Constructor taking in a list of strings from the CSV file
	* @param InStrings - The lines of the incoming CSV file
	* @param Warn - A feedback context giving information about the loading progress
	*/
	FPointCloudCsv(const TArray<FString>& InStrings, FFeedbackContext* Warn = nullptr);

	/**
	* Default Constructor
	*/
	FPointCloudCsv() = default;

public:

	/**
	* Default Destructor
	*/
	~FPointCloudCsv() = default;

	/**
	* Open a new document from a filename
	* @param Name - The name of the file to open
	* @return Warn - Feedback object to retrieve information about the progress
	*/
	static FPointCloudCsv Open(const FString& Name, FFeedbackContext* Warn = nullptr);
	
	/**
	* Query if this document is sucessfully open 	
	* @return True if the document opened correctly
	*/
	bool GetIsOpen();
	
	/**
	* Return the name of the columns in this CSV document	
	* @return Array of column names found in the first line of the CSV document
	*/
	const TArray<FString>& GetColumnNames() const;
	
	/**
	* Return the contents of the given column
	* @param Name - The name of the column to return
	* @return Array containing the Columns Contents
	*/
	TArray<FString>* GetColumn(const FString& Name);
	
	/**
	* Given an index, return the name column
	* @param Index - The column index to return 
	* @return The name of the column with the given index
	*/
	FString GetColumnName(int32 Index) const;
	
	/**
	* Return the number of columns in this document	
	* @return the number of columns in the document
	*/
	int32 GetColumnCount() const;
	
	/**
	* Return the number of rows in this document
	* @return The count of the rows
	*/
	int32 GetRowCount() const;

private:

	/** Flag recording the open status of this document */
	bool							IsOpen = false;
	/** The names of the columns found on the first line of the document */
	TArray<FString>					ColumnNames;
	/** The columns themselves */
	TMap<FString, TArray<FString>>	Columns;
	/** The number of rows in this document */
	int32							RowCount = 0;
};

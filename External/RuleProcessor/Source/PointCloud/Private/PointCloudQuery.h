// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PointCloudImpl.h"

struct sqlite3_stmt;

/** This object represents a predefined query onto the pointcloud. Using this object users can efficiently run the same query multiple times using different parameters. 
	The query is defined once with optional tokenized parameters and then the caller may step the query, providing a new set of parameters on each step. 
	
	An example might be the following SQL Query 
	
	SELECT * From Table Limit WHERE ID>? and ID<?

	By providing values for the two ?s the user can step through the table in blocks i.e. 

	TArray<int>& Values;
	Values.Add(0);
	Values.Add(10);
	Query.Step(Values);

	Will execute the following statement 

	SELECT * From Table Limit WHERE ID>0 and ID<10

	And then 

	TArray<int>& Values;
	Values.Add(11);
	Values.Add(20);
	Query.Step(Values);

	Will execute

	SELECT * From Table Limit WHERE ID>11 and ID<20

	And so on
*/
class FPointCloudQuery
{
public:
	
	/** Subclass this object to handle each result as it is returned from this point cloud query */
	class FRowHandler
	{
	public:

		virtual ~FRowHandler() = default;

		/**
		* This is called once for each row in the result set. It should read the required values from the SQLLite3 statement. 
		* @param Statement - Statement object representing the current state of the query.		
		* @return True if the query should continue running, false if the query should stop and this be the last result
		*/
		virtual bool Handle(sqlite3_stmt* Statement) = 0; 
	};

	/**
	* Constructor for a new query to be run on a given PointCloud
	* @param Cloud - This should be a non null pointer to an initialized and valid pointcloud	
	*/
	FPointCloudQuery(UPointCloudImpl* Cloud);

	/** Destructor */
	virtual ~FPointCloudQuery();

	/**
	* Initialize this object with a given SQL statement. For more information about how to structure this query see the SQLITE3 documentation on prepared statements. https://www.sqlite.org/c3ref/stmt.html
	* @param Query - A valid, optionally parameterized SQL Statement
	* @return True if the query is valid and can be run. False otherwise
	*/
	bool SetQuery(const FString& Query);

	/**
	* This must be called before the Query can be stepped
	* @param Statement - Statement object representing the current state of the query.
	* @return True if the query should continue running, false if the query should stop and this be the last result
	*/
	bool Begin();

	/**
	* Run this prepared statement substituting parameters with strings. parameters will be replaced in the order they appear in the statement.
	* @param Values - Parameter substitution values. This Array must contain one value for each expected parameter in the Query.
	* @return True if the query can be stepped given the provided values
	*/
	bool Step(const TArray<FString>& Values);

	/**
	* Special case step function for metadata insertion. This should all be cleaned up by using a Variant or similar
	* @param ValueA - The first value to substitute in the query
	* @param ValueB - The second value to substitute in the query
	* @param ValueC - The third value to substitute in the query
	* @return True if the query can be stepped given the provided values
	*/
	bool Step(int32 ValueA, int32 ValueB, int32 ValueC);

	/**
	* Run this prepared statement substituting parameters with characters. This represents a single 8-bit string parameter
	* @param Values - Parameter substitution values. This Array must contain one value for each expected parameter in the Query.
	* @return True if the query can be stepped given the provided values
	*/
	bool Step(const TArray<char>& Value);

	/**
	* Run this prepared statement substituting parameters with floats. parameters will be replaced in the order they appear in the statement.
	* @param Values - Parameter substitution values. This Array must contain one value for each expected parameter in the Query.
	* @return True if the query can be stepped given the provided values
	*/
	bool Step(const TArray<float>& Values);

	/**
	* Run this prepared statement substituting parameters with an int and a string. parameters will be replaced in the order they appear in the statement.
	* @param Values - Parameter substitution values. This Array must contain one value for each expected parameter in the Query.
	* @return True if the query can be stepped given the provided values
	*/
	bool Step(const TPair<int, TArray<char>>& Value);

	/**
	* Run this prepared statement substituting parameters with ints. parameters will be replaced in the order they appear in the statement.
	* @param Values - Parameter substitution values. This Array must contain one value for each expected parameter in the Query.
	* @return True if the query can be stepped given the provided values
	*/
	bool Step(const TArray<int>& Values, FRowHandler *Handler=0);

	/**
	* Run this prepared statement substituting without any parameters. 	
	* @return True if the query can be stepped 
	*/
	bool Step();

	/**
	* Finish running this query. Step cannot be called after end is called and any internal state is cleared
	* @return True if the query can be stepped
	*/
	bool End();

	FString GetHash(int HashType = 256, bool bIncludeQuery = true) const;

private:
	
	UPointCloudImpl* Cloud;
	sqlite3_stmt* Statement;
	FString Query; 
};



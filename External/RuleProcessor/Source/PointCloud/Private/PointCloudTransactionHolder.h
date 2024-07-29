// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PointCloudView.h"

class UPointCloudImpl;

/**
* Little utility class that releases transactions on destruction. Point clouds also check for nested transactions
*/
class FPointCloudTransactionHolder
{
public:

	/**
	* Construct a new Transaction Holder. Doing this will start a new Transaction on the Given Point Cloud	
	* @param Warn - InPointCloud The Point cloud on which to start and hold a new transaction
	*/
	FPointCloudTransactionHolder(UPointCloudImpl* InPointCloud);

	/**
	* Destruction will call endTransaction automatically if required
	*/ 
	~FPointCloudTransactionHolder();

	/** If Something has gone wrong, rollback the transaction */
	void RollBack();

	/**
	* End the current managed transation. 
	* @reurn True if the transaction was ended sucessfully. False if there was not valid transaction or if there was a problem. 
	*/
	bool EndTransaction();
	
private:
	TObjectPtr<UPointCloudImpl> PointCloud;
};

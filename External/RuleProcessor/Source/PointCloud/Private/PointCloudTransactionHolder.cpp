// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudTransactionHolder.h"
#include "PointCloudImpl.h"

FPointCloudTransactionHolder::FPointCloudTransactionHolder(UPointCloudImpl* InPointCloud) : PointCloud(InPointCloud)
{
	if (PointCloud != nullptr)
	{
		if (!PointCloud->BeginTransaction())
		{
			PointCloud = nullptr;
		}
	}
}

void  FPointCloudTransactionHolder::RollBack()
{
	if (PointCloud != nullptr)
	{
		PointCloud->RollbackTransaction();
		PointCloud = nullptr;		
	}
}

bool FPointCloudTransactionHolder::EndTransaction()
{
	bool ReturnValue = false;

	if (PointCloud != nullptr)
	{
		ReturnValue = PointCloud->EndTransaction();
		PointCloud = nullptr;
	}

	return ReturnValue;
}
 
FPointCloudTransactionHolder::~FPointCloudTransactionHolder()
{
	EndTransaction();
}
// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloud.h"

#include "Containers/UnrealString.h"
#include "Misc/Paths.h"
#include "PointCloudImpl.h"
#include "PointCloudView.h"

#define LOCTEXT_NAMESPACE "PointCloud"

bool UPointCloud::IsEditorOnly() const
{
	return true;
}

// return true if the given file is loaded into this point cloud
bool UPointCloud::IsFileLoaded(const FString& Name) const
{
	for (auto a : GetLoadedFiles())
	{
		if (*a == Name)
		{
			return true;
		}
	}
	return false;
}

// return true if this pointcloud as the given default attribute 
bool UPointCloud::HasDefaultAttribute(const FString& Name) const
{
	for (auto a : GetDefaultAttributes())
	{
		if (*a == Name)
		{
			return true;
		}
	}
	return false;
}

// return true if this point cloud as the given metadata attribute
bool UPointCloud::HasMetaDataAttribute(const FString& Name) const
{
	return GetMetadataAttributes().Contains(Name);		
}

bool UPointCloud::LoadFromPoints(const TArray<FPointCloudPoint>& InPoints)
{
	return LoadFromStructuredPoints(InPoints, FBox(EForceInit::ForceInit));
}

void UPointCloud::ClearRootViews()
{
	for (UPointCloudView* View : RootViews)
	{
		View->ClearChildViews();
	}
	RootViews.Empty();
}

bool UPointCloud::ReplacePoints(const FString& FileName, const FBox& ReimportBounds)
{
	const TArray<FString> Files = { FileName };

	if (!FPaths::FileExists(FileName))
	{
		// if a file can't be found then return 
		UE_LOG(PointCloudLog, Warning, TEXT("Cannot find file %s to reload\n"), *FileName);
		return false;
	}

	return ReloadInternal(Files, ReimportBounds);
}

bool UPointCloud::Reimport(const FBox& ReimportBounds)
{
	// first all of, check that the files can all be found
	const TArray<FString> Files = GetLoadedFiles();

	for (const FString& a : Files)
	{
		if (!FPaths::FileExists(a))
		{
			// if a file can't be found then return 
			UE_LOG(PointCloudLog, Warning, TEXT("Cannot find file %s to reload\n"), *a);
			return false;
		}
	}

	return ReloadInternal(Files, ReimportBounds);
}

#undef LOCTEXT_NAMESPACE
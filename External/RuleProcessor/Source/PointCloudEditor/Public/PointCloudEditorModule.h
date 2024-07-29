// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "AssetTypeCategories.h"

class POINTCLOUDEDITOR_API IPointCloudEditorModule : public IModuleInterface
{
public:
	virtual EAssetTypeCategories::Type GetAssetCategory() const = 0;
};

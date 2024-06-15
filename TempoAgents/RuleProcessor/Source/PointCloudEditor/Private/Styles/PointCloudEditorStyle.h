// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/IPluginManager.h"
#include "Brushes/SlateImageBrush.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

#define IMAGE_BRUSH(RelativePath, ...) FSlateImageBrush(RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define BOX_BRUSH(RelativePath, ...) FSlateBoxBrush(RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define BORDER_BRUSH(RelativePath, ...) FSlateBorderBrush(RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define TTF_FONT(RelativePath, ...) FSlateFontInfo(RootToContentDir(RelativePath, TEXT(".ttf")), __VA_ARGS__)
#define OTF_FONT(RelativePath, ...) FSlateFontInfo(RootToContentDir(RelativePath, TEXT(".otf")), __VA_ARGS__)

class FPointCloudEditorStyle : public FSlateStyleSet
{
public:

	/** Default constructor. */
	 FPointCloudEditorStyle()
		 : FSlateStyleSet(TEXT("PointCloudEditorStyle"))
	 {
		const FVector2D Icon16x16(16.0f, 16.0f);
		const FVector2D Icon20x20(20.0f, 20.0f);
		const FVector2D Icon40x40(40.0f, 40.0f);

		const FString BaseDir = IPluginManager::Get().FindPlugin(TEXT("RuleProcessor"))->GetBaseDir();
		SetContentRoot(BaseDir / TEXT("Content"));

		FSlateImageBrush* ThumbnailBrush = new FSlateImageBrush(RootToContentDir(TEXT("Resources/Icon128"), TEXT(".png")), FVector2D(128.f, 128.f));
	
		// set new styles here, for example...
		Set("ClassThumbnail.PointCloud", ThumbnailBrush);

		FSlateImageBrush* DiceThumbnailBrush	= new FSlateImageBrush(RootToContentDir(TEXT("Resources/DiceIcon256"), TEXT(".png")), FVector2D(128.f, 128.f));

		FSlateImageBrush* GeneratorRuleBrush	= new FSlateImageBrush(RootToContentDir(TEXT("Resources/GeneratorRule"), TEXT(".png")), FVector2D(128.f, 128.f));
		FSlateImageBrush* FilterRuleBrush		= new FSlateImageBrush(RootToContentDir(TEXT("Resources/FilterRule"), TEXT(".png")), FVector2D(128.f, 128.f));
		FSlateImageBrush* UnknownRuleBrush		= new FSlateImageBrush(RootToContentDir(TEXT("Resources/UnknownRule"), TEXT(".png")), FVector2D(128.f, 128.f));

		FSlateImageBrush* UpArrowBrush			= new FSlateImageBrush(RootToContentDir(TEXT("Resources/UpArrow"), TEXT(".png")), FVector2D(16.f, 16.f));
		FSlateImageBrush* DownArrowBrush		= new FSlateImageBrush(RootToContentDir(TEXT("Resources/DownArrow"), TEXT(".png")), FVector2D(16.f, 16.f));
		FSlateImageBrush* DeleteBrush			= new FSlateImageBrush(RootToContentDir(TEXT("Resources/Delete"), TEXT(".png")), FVector2D(16.f, 16.f));

		Set("RuleThumbnail.GeneratorRule",	GeneratorRuleBrush);
		Set("RuleThumbnail.FilterRule",		FilterRuleBrush);
		Set("RuleThumbnail.UnknownRule",	UnknownRuleBrush);

		Set("UIElements.MoveUpIcon",		UpArrowBrush);
		Set("UIElements.MoveDownIcon",		DownArrowBrush);
		Set("UIElements.DeleteIcon",		DeleteBrush);

		Set("ClassThumbnail.PointCloudSliceAndDiceRuleSet", DiceThumbnailBrush);
		
		
		FSlateStyleRegistry::RegisterSlateStyle(*this);
	 }

	 /** Destructor. */
	 ~FPointCloudEditorStyle()
	 {
		FSlateStyleRegistry::UnRegisterSlateStyle(*this);
	 }
};


#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef BORDER_BRUSH
#undef TTF_FONT
#undef OTF_FONT

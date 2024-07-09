// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPointCloudEditor.h"

#include "Containers/UnrealString.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Internationalization/Text.h"
#include "PointCloud.h"
#include "PointCloudView.h"
#include "UObject/Class.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "PointCloudEditorSettings.h"


#define LOCTEXT_NAMESPACE "SPointCloudEditor"

/** Names of the columns in the metadata attribute table of the point cloud asset viewer */
FName SPointCloudEditor::NAME_MetadataAttributeColumn = FName(*NSLOCTEXT("SPointCloudEditor", "MetadataAttributeColumn", "MetadataAttribute").ToString());
FName SPointCloudEditor::NAME_PointCountColumn = FName(*NSLOCTEXT("SPointCloudEditor", "PointCountColumn", "PointCount").ToString());
FName SPointCloudEditor::NAME_ValueCountColumn = FName(*NSLOCTEXT("SPointCloudEditor", "ValueCountColumn", "ValueCount").ToString());
FName SPointCloudEditor::NAME_DatasetsColumn = FName(*NSLOCTEXT("SPointCloudEditor", "DatasetsColumn", "Datasets").ToString());

/** Labels of the columns */
FText SPointCloudEditor::TEXT_MetadataAttributeLabel = NSLOCTEXT("SPointCloudEditor", "MetadataAttributeLabel", "Metadata Attribute");
FText SPointCloudEditor::TEXT_PointCountLabel = NSLOCTEXT("SPointCloudEditor", "PointCountLabel", "Point Count");
FText SPointCloudEditor::TEXT_ValueCountLabel = NSLOCTEXT("SPointCloudEditor", "ValueCountLabel", "Value Count");
FText SPointCloudEditor::TEXT_DatasetsLabel = NSLOCTEXT("SPointCloudEditor", "DatasetsLabel", "Point Cloud Datasets");


/* SPointCloudEditor interface
 *****************************************************************************/

SPointCloudEditor::~SPointCloudEditor()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
}

namespace
{
	TArray< TSharedPtr< FString> > ConvertToArrayOfPtrs(const TArray<FString>& In)
	{
		TArray< TSharedPtr< FString> > Result;

		// We just duplicate all the data, but the data isn't directly
		// accessible because it's in a database so we need to duplicate it
		// eventually
		for(const FString& str : In)
		{
			Result.Add(MakeShareable(new FString(str)));
		}
		return Result;
	}
}

void SPointCloudEditor::Construct(const FArguments& InArgs, UPointCloud* InPointCloud, const TSharedRef<ISlateStyle>& InStyle)
{
	PointCloud = InPointCloud;

	auto Settings = GetDefault<UPointCloudEditorSettings>();

	DefaultAttributes = ConvertToArrayOfPtrs(InPointCloud->GetDefaultAttributes());
	Datasets = ConvertToArrayOfPtrs(InPointCloud->GetLoadedFiles());

	FText PointCount = FText::Format(LOCTEXT("PointCoint", "Number of points: {0}"), FText::AsNumber(PointCloud->GetCount()));
	FText DefaultAttributesNames = FText::Format(LOCTEXT("DefaultAttributes", "Default Attributes: {0}"), FText::FromString(FString::Join(PointCloud->GetDefaultAttributes(), TEXT(", "))));

	/*
	 * the Slate UI requires a TArray of items for a list view, but information
	 * about the point cloud isn't available as an object because it's stored
	 * in a database. Instead we cache some data from the database in these
	 * simple objects so that the UI can read them out later. This is
	 * duplicating data, but the number of Metadata attributes is relatively
	 * small.
	 */
	UPointCloudView* View = InPointCloud->MakeView();
	if (View == nullptr)
	{
		// UPointCloud::MakeView already logs an error if the version is out of date, so we only show a notification
		FNotificationInfo Info(LOCTEXT("OutdatedSchema", "Point cloud schema out of date, try updating to view metadata statistics."));
		FSlateNotificationManager::Get().AddNotification(Info);
	}
	else
	{
		for (const FString& Attribute : InPointCloud->GetMetadataAttributes())
		{
			TSharedPtr<FMetadataHolder> Holder(new FMetadataHolder());
			Holder->MetadataName = Attribute;

			TMap<FString, int> AttributeMap = View->GetUniqueMetadataValuesAndCounts(Attribute);

			TArray<FString> AttributeValues;
			AttributeMap.GetKeys(AttributeValues);

			int MetadataPointCount = 0;
			for (const FString& Value : AttributeValues)
			{
				MetadataPointCount += AttributeMap[Value];
			}

			Holder->PointCount = MetadataPointCount;
			Holder->ValueCount = AttributeMap.Num();

			MetadataAttributes.Add(Holder);
		}
	}

	// build the lists of items	

	ChildSlot
	[
		SNew(SHorizontalBox) 
		+ SHorizontalBox::Slot()
		.FillWidth(0.5f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(0.1f)
			[
				SNew(STextBlock)
				.Text(PointCount)
			]
			+ SVerticalBox::Slot()
			.FillHeight(0.1f)
			[
				SNew(STextBlock)
				.Text(DefaultAttributesNames)
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SListView<TSharedPtr<FMetadataHolder>>)
				.ItemHeight(24)
				.ListItemsSource(&MetadataAttributes)
				.OnGenerateRow(this, &SPointCloudEditor::OnGenerateMetadataRowForList)
				.HeaderRow
				(
					SNew(SHeaderRow)
					+ SHeaderRow::Column(NAME_MetadataAttributeColumn)
					.DefaultLabel(TEXT_MetadataAttributeLabel)
					+ SHeaderRow::Column(NAME_PointCountColumn)
					.DefaultLabel(TEXT_PointCountLabel)
					+ SHeaderRow::Column(NAME_ValueCountColumn)
					.DefaultLabel(TEXT_ValueCountLabel) 
				)
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			//The actual list view creation			
			[
				SAssignNew(LoadedFiles, SListView<TSharedPtr<FString>>)
				.ItemHeight(24)
				.ListItemsSource(&(Datasets)) //The Items array is the source of this listview
				.OnGenerateRow(this, &SPointCloudEditor::OnGenerateRowForList)
				.HeaderRow
				(
					SNew(SHeaderRow)
					+ SHeaderRow::Column(NAME_DatasetsColumn)
					.DefaultLabel(TEXT_DatasetsLabel)
				)
			]
		]
	];

	FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &SPointCloudEditor::HandlePointCloudPropertyChanged);
}

TSharedRef<ITableRow> SPointCloudEditor::OnGenerateRowForList(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	//Create the row
	return
		SNew(STableRow< TSharedPtr<FString> >, OwnerTable)
		.Padding(2.0f)
		[
			SNew(STextBlock).Text(FText::FromString(*Item.Get()))
		];
}

TSharedRef<ITableRow> SPointCloudEditor::OnGenerateMetadataRowForList(TSharedPtr<FMetadataHolder> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return
		SNew(SMetadataTableRow, OwnerTable)
		.Metadata(Item);
}

/* SPointCloudEditor callbacks
 *****************************************************************************/

void SPointCloudEditor::HandleEditableTextBoxTextChanged(const FText& NewText)
{
	PointCloud->MarkPackageDirty();
}


void SPointCloudEditor::HandleEditableTextBoxTextCommitted(const FText& Comment, ETextCommit::Type CommitType)
{
	
}


void SPointCloudEditor::HandlePointCloudPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent)
{
	if (Object == PointCloud)
	{
		//EditableTextBox->SetText("");
	}
}

/* SMetadataTableRow
******************************************************************************/

void
SPointCloudEditor::SMetadataTableRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	Metadata = InArgs._Metadata;
	SMultiColumnTableRow<TSharedPtr<FMetadataHolder>>::Construct(SMultiColumnTableRow<TSharedPtr<FMetadataHolder>>::FArguments(), InOwnerTableView);
}

TSharedRef<SWidget>
SPointCloudEditor::SMetadataTableRow::GenerateWidgetForColumn(const FName& ColumnId)
{
	FText ColumnData = NSLOCTEXT("SMetadataTableRow", "ColumnError", "Unrecognized Column");
	if (ColumnId == SPointCloudEditor::NAME_MetadataAttributeColumn)
	{
		ColumnData = FText::FromString(Metadata->MetadataName);
	}
	else if (ColumnId == SPointCloudEditor::NAME_PointCountColumn)
	{
		ColumnData = FText::FromString(FString::FromInt(Metadata->PointCount));
	}
	else if (ColumnId == SPointCloudEditor::NAME_ValueCountColumn)
	{
		ColumnData = FText::FromString(FString::FromInt(Metadata->ValueCount));
	}

	return SNew(STextBlock).Text(ColumnData);
}

#undef LOCTEXT_NAMESPACE

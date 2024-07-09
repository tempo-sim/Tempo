// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PointCloud.h"
#include "Templates/SharedPointer.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Views/STableRow.h"
#include "SlateBasics.h"

class FText;
class ISlateStyle;
class UPointCloud;

/**
 * Implements the UPointCloud asset editor widget.
 */
class SPointCloudEditor : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SPointCloudEditor) { }
	SLATE_END_ARGS()

public:

	/** Virtual destructor. */
	virtual ~SPointCloudEditor();

	/**
	 * Construct this widget
	 *
	 * @param InArgs The declaration data for this widget.
	 * @param InPointCloud The UPointCloud asset to edit.
	 * @param InStyleSet The style set to use.
	 */
	void Construct(const FArguments& InArgs, UPointCloud* InPointCloud, const TSharedRef<ISlateStyle>& InStyle);

public:
	/** Names of the columns in the metadata attribute table of the point cloud asset viewer */
	static FName NAME_MetadataAttributeColumn;
	static FName NAME_PointCountColumn;
	static FName NAME_ValueCountColumn;
	static FName NAME_DatasetsColumn;
	/** Labels of the columns */
	static FText TEXT_MetadataAttributeLabel;
	static FText TEXT_PointCountLabel;
	static FText TEXT_ValueCountLabel;
	static FText TEXT_DatasetsLabel;

private:

	/**
	 * Struct for holding metadata info since the Slate UI requires an object
	 * for list views. These just hold onto the data from the above info
	 * functions so that the UI can reference them later.
	 */
	struct FMetadataHolder
	{
		FString MetadataName;
		int64 PointCount;
		int64 ValueCount;
	};

	/** Callback for text changes in the editable text box. */
	void HandleEditableTextBoxTextChanged(const FText& NewText);

	/** Callback for committed text in the editable text box. */
	void HandleEditableTextBoxTextCommitted(const FText& Comment, ETextCommit::Type CommitType);

	/** Callback for property changes in the text asset. */
	void HandlePointCloudPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent);

	TSharedRef<ITableRow> OnGenerateRowForList(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable);

	TSharedRef<ITableRow> OnGenerateMetadataRowForList(TSharedPtr<FMetadataHolder> Item, const TSharedRef<STableViewBase>& OwnerTable);

private:

	/** Holds the editable text box widget. */
	TSharedPtr<SMultiLineEditableTextBox> EditableTextBox;

	/** Holds the editable text box widget. */
	TSharedPtr<SListView<TSharedPtr<FString>>> DefaultColumns;

	/** Files that have been added to this pointcloud */
	TSharedPtr<SListView<TSharedPtr<FString>>> LoadedFiles;

	TArray<TSharedPtr<FString>> DefaultAttributes;
	TArray<TSharedPtr<FMetadataHolder>> MetadataAttributes;
	TArray<TSharedPtr<FString>> Datasets;

	/** Pointer to the text asset that is being edited. */
	UPointCloud* PointCloud;

	/**
	 * Implements the FMetadataHolder table row widget.
	 */
	class SMetadataTableRow : public SMultiColumnTableRow<TSharedPtr<FMetadataHolder>>
	{
	public:

		SLATE_BEGIN_ARGS(SMetadataTableRow) {}
			SLATE_ARGUMENT(TSharedPtr<FMetadataHolder>, Metadata)
		SLATE_END_ARGS()

	public:
		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView);

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnId) override;

	protected:
		TSharedPtr<FMetadataHolder> Metadata;
	};
};

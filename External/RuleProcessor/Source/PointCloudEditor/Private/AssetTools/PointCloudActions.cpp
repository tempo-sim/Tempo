// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudActions.h"

#include "PointCloudImpl.h"
#include "PointCloudAssetHelpers.h"
#include "PointCloudEditorModule.h"
#include "PointCloudEditorToolkit.h"

#include "Engine/GameEngine.h"
#include "EngineGlobals.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Runtime/Engine/Classes/Engine/GameViewportClient.h"
#include "SlateCore.h"
#include "Styling/SlateStyle.h"
#include "Widgets/Notifications/SNotificationList.h"


#define LOCTEXT_NAMESPACE "AssetTypeActions"


/* FPointCloudActions constructors
 *****************************************************************************/

FPointCloudActions::FPointCloudActions(const TSharedRef<ISlateStyle>& InStyle)
	: Style(InStyle)
{ }


/* FAssetTypeActions_Base overrides
 *****************************************************************************/

bool FPointCloudActions::CanFilter()
{
	return true;
}

void FPointCloudActions::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	FAssetTypeActions_Base::GetActions(InObjects, MenuBuilder);

	auto PointClouds = GetTypedWeakObjectPtrs<UPointCloud>(InObjects);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("PointCloud_Export", "Export"),
		LOCTEXT("PointCloud_ExportToolTip", "Export to a database file"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([PointClouds] {
				for (auto& PointCloud : PointClouds)
				{
					if (PointCloud.IsValid() && PointCloud->IsInitialized())
					{
						TArray<FString> OutFileNames;
						PointCloudAssetHelpers::SaveFileDialog(TEXT("Export Pointcloud"), TEXT(""), TEXT("Database | *.db"), OutFileNames);
						if (OutFileNames.Num() == 1)
						{
							PointCloud->SaveToDisk(OutFileNames[0]);							
						};
					}
				}
				}),
			FCanExecuteAction::CreateLambda([PointClouds] {
					for (auto& PointCloud : PointClouds)
					{
						if (PointCloud.IsValid() && PointCloud->IsInitialized())
						{
							return true;
						}
					}
					return false;
				})
					)
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("PointCloud_AppendFromCsv", "Add Points"),
		LOCTEXT("PointCloud_AppendFromCsvToolTip", "Append a CSV file to this point cloud"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([PointClouds]{
				for (auto& PointCloud : PointClouds)
				{
					if (PointCloud.IsValid() && PointCloud->IsInitialized())
					{
						TArray<FString> OutFileNames;
						PointCloudAssetHelpers::OpenFileDialog(TEXT("Import CSV"), TEXT(""), TEXT("psv"), OutFileNames);
						if (OutFileNames.Num() == 1)
						{
							if (PointCloud->LoadFromCsv(OutFileNames[0],FBox(EForceInit::ForceInit), UPointCloud::ADD))
							{
								PointCloud->PostEditChange();
								PointCloud->MarkPackageDirty();
							}
						};
					}
				}
			}),
			FCanExecuteAction::CreateLambda([PointClouds] {
				for (auto& PointCloud : PointClouds)
				{
					if (PointCloud.IsValid() && PointCloud->IsInitialized())
					{
						return true;
					}
				}
				return false;
			})
		)
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("PointCloud_LoadFromCsv", "Reimport With New File"),
		LOCTEXT("PointCloud_LoadFromCsvToolTip", "Replace this pointcloud with data from another file"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([PointClouds] {
				for (auto& PointCloud : PointClouds)
				{
					if (PointCloud.IsValid() && PointCloud->IsInitialized())
					{
						TArray<FString> OutFileNames;
						PointCloudAssetHelpers::OpenFileDialog(TEXT("Replace Point Cloud"), TEXT(""), TEXT("csv,pbc"), OutFileNames);
						if (OutFileNames.Num() == 1)
						{
							// FBox is odd as it doesn't initialize itself 							
							PointCloud->ReplacePoints(OutFileNames[0], FBox(EForceInit::ForceInit));
						};
					}
				}
				}),
			FCanExecuteAction::CreateLambda([PointClouds] {
					for (auto& PointCloud : PointClouds)
					{
						if (PointCloud.IsValid() && PointCloud->IsInitialized())
						{
							return true;
						}
					}
					return false;
				})
		)
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("PointCloud_Reimport", "Reimport Points"),
		LOCTEXT("PointCloud_ReimportToolTip", "Reload data for this pointcloud from the original source files"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([PointClouds] {
				for (auto& PointCloud : PointClouds)
				{
					if (PointCloud.IsValid() && PointCloud->IsInitialized())
					{
						PointCloud->Reimport(FBox(EForceInit::ForceInit));
					}
				}
				}),
			FCanExecuteAction::CreateLambda([PointClouds] {
					for (auto& PointCloud : PointClouds)
					{
						if (PointCloud.IsValid() && PointCloud->IsInitialized())
						{
							return true;
						}
					}
					return false;
				})
					)
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("PointCloud_AttemptUpdate", "Attempt Update"),
		LOCTEXT("PointCloud_AttemptUpdateToolTip", "Attempt to Update This Point Cloud to the Latest Version"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([PointClouds] {
				for (auto& PointCloud : PointClouds)
				{
					if (PointCloud.IsValid() && PointCloud->IsInitialized() && PointCloud->NeedsUpdating())
					{
						if (PointCloud->AttemptToUpdate() == true)
						{
							const FText NotificationText = FText::Format(LOCTEXT("PointCloud_AttemptUpdateMessageSucess", "Update Schema Success.\n'{0}' needs to be saved."), FText::AsCultureInvariant(PointCloud->GetName()));
							FNotificationInfo Info(NotificationText);
							Info.ExpireDuration = 2.0f;
							FSlateNotificationManager::Get().AddNotification(Info);							
						}
						else
						{
							const FText NotificationText = FText::Format(LOCTEXT("PointCloud_AttemptUpdateMessageFailure", "Update Schema Failed.\n'{0}' cannot be converted."), FText::AsCultureInvariant(PointCloud->GetName()));
							FNotificationInfo Info(NotificationText);
							Info.ExpireDuration = 2.0f;
							FSlateNotificationManager::Get().AddNotification(Info);							
						}
					}
				}
				}),
			FCanExecuteAction::CreateLambda([PointClouds] {
					for (auto& PointCloud : PointClouds)
					{
						if (PointCloud.IsValid() && PointCloud->IsInitialized() && PointCloud->NeedsUpdating())
						{
							return true;
						}
					}
					return false;
				})
					)
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("PointCloud_RunTestQuery", "Test Query"),
		LOCTEXT("PointCloud_RunTestQueryToolTip", "Run a test query"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([PointClouds] {
				for (auto& PointCloud : PointClouds)
				{
					if (PointCloud.IsValid() && PointCloud->IsInitialized())
					{
						
					}
				}
				}),
			FCanExecuteAction::CreateLambda([PointClouds] {
					for (auto& PointCloud : PointClouds)
					{
						if (PointCloud.IsValid() && PointCloud->IsInitialized())
						{
							return true;
						}
					}
					return false;
				})
					)
	);
}

uint32 FPointCloudActions::GetCategories()
{
	IPointCloudEditorModule* PointCloudModule = FModuleManager::GetModulePtr<IPointCloudEditorModule>("PointCloudEditor");
	return PointCloudModule->GetAssetCategory();
}

FText FPointCloudActions::GetName() const
{
	return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_PointCloud", "Point Cloud");
}

UClass* FPointCloudActions::GetSupportedClass() const
{
	return UPointCloudImpl::StaticClass();
}

FColor FPointCloudActions::GetTypeColor() const
{
	return FColor::White;
}

void FPointCloudActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid()
		? EToolkitMode::WorldCentric
		: EToolkitMode::Standalone;

	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		auto PointCloud = Cast<UPointCloud>(*ObjIt);

		if (PointCloud != nullptr)
		{
			TSharedRef<FPointCloudEditorToolkit> EditorToolkit = MakeShareable(new FPointCloudEditorToolkit(Style));
			EditorToolkit->Initialize(PointCloud, Mode, EditWithinLevelEditor);
		}
	}
}

#undef LOCTEXT_NAMESPACE

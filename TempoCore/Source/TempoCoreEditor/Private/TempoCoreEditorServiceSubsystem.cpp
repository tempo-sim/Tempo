// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCoreEditorServiceSubsystem.h"

#include "FileHelpers.h"
#include "GameMapsSettings.h"
#include "TempoCoreEditor/TempoCoreEditor.grpc.pb.h"

#include "Kismet2/DebuggerCommands.h"

using TempoCoreEditorService = TempoCoreEditor::TempoCoreEditorService;
using TempoCoreEditorAsyncService = TempoCoreEditor::TempoCoreEditorService::AsyncService;
using SaveLevelRequest = TempoCoreEditor::SaveLevelRequest;
using OpenLevelRequest = TempoCoreEditor::OpenLevelRequest;

void UTempoCoreEditorServiceSubsystem::RegisterScriptingServices(FTempoScriptingServer& ScriptingServer)
{
	ScriptingServer.RegisterService<TempoCoreEditorService>(
			SimpleRequestHandler(&TempoCoreEditorAsyncService::RequestPlayInEditor, &UTempoCoreEditorServiceSubsystem::PlayInEditor),
			SimpleRequestHandler(&TempoCoreEditorAsyncService::RequestSimulate, &UTempoCoreEditorServiceSubsystem::Simulate),
			SimpleRequestHandler(&TempoCoreEditorAsyncService::RequestStop, &UTempoCoreEditorServiceSubsystem::Stop),
			SimpleRequestHandler(&TempoCoreEditorAsyncService::RequestSaveLevel, &UTempoCoreEditorServiceSubsystem::SaveLevel),
			SimpleRequestHandler(&TempoCoreEditorAsyncService::RequestOpenLevel, &UTempoCoreEditorServiceSubsystem::OpenLevel),
			SimpleRequestHandler(&TempoCoreEditorAsyncService::RequestNewLevel, &UTempoCoreEditorServiceSubsystem::NewLevel)
		);
}

void UTempoCoreEditorServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FTempoScriptingServer::Get().ActivateService<TempoCoreEditorService>(this);
}

void UTempoCoreEditorServiceSubsystem::Deinitialize()
{
	Super::Deinitialize();

	FTempoScriptingServer::Get().DeactivateService<TempoCoreEditorService>();
}

void UTempoCoreEditorServiceSubsystem::PlayInEditor(const TempoScripting::Empty& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation)
{
	if (!FPlayWorldCommands::GlobalPlayWorldActions->ExecuteAction(FPlayWorldCommands::Get().PlayInViewport.ToSharedRef()))
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::NOT_FOUND, "Action not found?"));
		return;
	}

	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
}

void UTempoCoreEditorServiceSubsystem::Simulate(const TempoScripting::Empty& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation)
{
	if (!FPlayWorldCommands::GlobalPlayWorldActions->ExecuteAction(FPlayWorldCommands::Get().Simulate.ToSharedRef()))
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::NOT_FOUND, "Action not found?"));
		return;
	}

	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
}

void UTempoCoreEditorServiceSubsystem::Stop(const TempoScripting::Empty& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation)
{
	if (!FPlayWorldCommands::GlobalPlayWorldActions->ExecuteAction(FPlayWorldCommands::Get().StopPlaySession.ToSharedRef()))
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::NOT_FOUND, "Action not found?"));
		return;
	}

	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
}

void SanitizeLevelPath(FString& Path)
{
	FPaths::NormalizeFilename(Path);
	FPaths::CollapseRelativeDirectories(Path);
	if (FPaths::IsRelative(Path))
	{
		Path = FPaths::Combine(FPaths::ProjectContentDir(), Path);
	}
	if (FPaths::GetExtension(Path).IsEmpty())
	{
		Path.Append(TEXT(".umap"));
	}
}

void UTempoCoreEditorServiceSubsystem::SaveLevel(const SaveLevelRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation)
{
	FString RequestedPath = FString(UTF8_TO_TCHAR(Request.path().c_str()));
	SanitizeLevelPath(RequestedPath);

	FString CurrentLevelName = FEditorFileUtils::GetFilename(GetEditorWorld());
	FPaths::NormalizeFilename(CurrentLevelName);
	FPaths::CollapseRelativeDirectories(CurrentLevelName);

	if (RequestedPath.IsEmpty())
	{
		if (CurrentLevelName.IsEmpty())
		{
			ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Level name must be specified"));
			return;
		}
		RequestedPath = CurrentLevelName;
	}
	else 
	{
		FText ErrorReason;
		if (!FFileHelper::IsFilenameValidForSaving(RequestedPath, ErrorReason))
		{
			const FString ErrorMessage = FString::Printf(TEXT("Invalid level name specified. Error: %s"), *ErrorReason.ToString());
			ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, TCHAR_TO_UTF8(*ErrorMessage)));
			return;
		}
		if (RequestedPath != CurrentLevelName)
		{
			if (FPaths::FileExists(RequestedPath) && !Request.overwrite())
			{
				ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Specified level name already exists. Use overwrite flag to replace."));
				return;
			}
		}
	}

	// Always collect garbage before saving.
	CollectGarbage(EObjectFlags::RF_NoFlags);

	if (!FEditorFileUtils::SaveMap(GetEditorWorld(), RequestedPath))
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::UNKNOWN, "Failed to save map"));
		return;
	}
	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
}

void UTempoCoreEditorServiceSubsystem::OpenLevel(const OpenLevelRequest& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation)
{
	FString RequestedPath = FString(UTF8_TO_TCHAR(Request.path().c_str()));
	SanitizeLevelPath(RequestedPath);
	if (RequestedPath.IsEmpty())
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Level name must be specified"));
	}

	if (!FPaths::FileExists(RequestedPath))
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::FAILED_PRECONDITION, "Could not find specified level"));
		return;
	}

	if (!FEditorFileUtils::LoadMap(RequestedPath))
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::UNKNOWN, "Failed to open map"));
		return;
	}
	
	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
}

void UTempoCoreEditorServiceSubsystem::NewLevel(const TempoScripting::Empty& Request, const TResponseDelegate<TempoScripting::Empty>& ResponseContinuation)
{
	const UGameMapsSettings* GameMapsSettings = GetDefault<UGameMapsSettings>();
	if (!GameMapsSettings)
	{
		ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status(grpc::UNKNOWN, "Failed to find game maps settings"));
		return;
	}

	UEditorLoadingAndSavingUtils::NewMapFromTemplate(GameMapsSettings->EditorStartupMap.ToString(), false);
	ResponseContinuation.ExecuteIfBound(TempoScripting::Empty(), grpc::Status_OK);
}

// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoActorLabeler.h"

#include "TempoSensors.h"
#include "TempoLabelTypes.h"
#include "TempoInstancedStaticMeshComponent.h"
#include "TempoSensors/Labels.grpc.pb.h"

#include "TempoSensorsConstants.h"
#include "TempoSensorsSettings.h"
#include "TempoSensorsUtils.h"

#include "TempoClassUtils.h"
#include "TempoCoreUtils.h"
#include "DefaultActorClassifier.h"

#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/StaticMesh.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraSystem.h"

FInstanceIdAllocator::FInstanceIdAllocator(int32 MinIdIn, int32 MaxIdIn)
	: MinId(MinIdIn), MaxId(MaxIdIn)
{
	TSet<int32>& Ids = AvailableIds.AddDefaulted_GetRef();
	for (int32 I = MinId; I <= MaxId; ++I)
	{
		Ids.Add(I);
	}
}

TOptional<int32> FInstanceIdAllocator::Allocate()
{
	for (TSet<int32>& Ids : AvailableIds)
	{
		if (auto IdIt = Ids.CreateIterator())
		{
			const int32 Id = *IdIt;
			IdIt.RemoveCurrent();
			return Id;
		}
	}

	const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
	if (TempoSensorsSettings->GetInstantaneouslyUniqueInstanceLabels())
	{
		// We are not allowed to reuse allocated IDs
		return TOptional<int32>();
	}

	// Reuse allocated IDs by making them available, at a higher count
	TSet<int32>& NextAvailableIds = AvailableIds.AddDefaulted_GetRef();
	for (int32 I = MinId + 1; I <= MaxId; ++I)
	{
		NextAvailableIds.Add(I);
	}

	// We reserved MinId above to be the one we will allocate
	return MinId;
}

void FInstanceIdAllocator::Return(int32 Id)
{
	const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
	if (TempoSensorsSettings->GetGloballyUniqueInstanceLabels())
	{
		// We are not allowed to reuse IDs once they have been allocated
		return;
	}
	if (!ensureMsgf(Id >= MinId && Id <= MaxId, TEXT("Reclaimed Available Id %d outside original min/max"), Id))
	{
		return;
	}
	for (auto AvailableIdsIt = AvailableIds.CreateIterator(); AvailableIdsIt; ++AvailableIdsIt)
	{
		if (AvailableIdsIt->Contains(Id))
		{
			AvailableIdsIt->Remove(Id);
			// If this wasn't the first element in AvailableIds, make this ID available at the lower count
			if (auto PrevAvailableIdsIt = AvailableIdsIt - 1)
			{
				PrevAvailableIdsIt->Add(Id);
			}
			// If that was the last ID in the group, we don't need this group anymore.
			if (AvailableIdsIt->IsEmpty())
			{
				AvailableIdsIt.RemoveCurrent();
			}
			return;
		}
	}

	// If we never found the ID at a higher count, make sure we mark it available at count 0
	if (ensureMsgf(AvailableIds.Num() > 0, TEXT("AvailableIds was empty!")))
	{
		AvailableIds[0].Add(Id);
	}
}

using LabelService = TempoSensors::LabelService;
using LabelAsyncService = TempoSensors::LabelService::AsyncService;

namespace
{
	// The sentinel FInstanceSemanticIdPair carries when no instance ID was ever allocated for the
	// object. The allocator hands out 1..GTempoCamera_Max_Label, so 0 can never be a live ID.
	constexpr int32 NoInstanceId = 0;

	// Every Set*SemanticId RPC takes the same semantic ID domain: a label the camera can encode, or
	// -1 meaning "forget the override and let the table decide". Returns false and answers the
	// request when the ID is outside it.
	bool ValidateSemanticIdRange(int32 SemanticId, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation)
	{
		if (SemanticId >= -1 && SemanticId <= GTempoCamera_Max_Label)
		{
			return true;
		}

		const FString ErrorMsg = FString::Printf(TEXT("semantic_id must be -1 (revert) or 0-%d"), GTempoCamera_Max_Label);
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(),
			grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, TCHAR_TO_UTF8(*ErrorMsg)));
		return false;
	}

	// Invert one of the label table's key -> row-name maps into semantic ID -> keys, then apply the
	// runtime overrides on top: an overridden key moves out of whichever ID the table gave it and
	// into the one the override names, which is what the labeler will actually draw.
	template <typename KeyType>
	TMap<int32, TArray<KeyType>> BuildSemanticIdToKeys(const TMap<KeyType, FName>& TableLabels,
		const TMap<FName, int32>& SemanticIds, const TMap<KeyType, int32>& Overrides)
	{
		TMap<int32, TArray<KeyType>> SemanticIdToKeys;
		for (const auto& [Key, LabelName] : TableLabels)
		{
			if (const int32* SemanticId = SemanticIds.Find(LabelName))
			{
				SemanticIdToKeys.FindOrAdd(*SemanticId).Add(Key);
			}
		}

		for (const auto& [Key, OverrideSemanticId] : Overrides)
		{
			for (auto& [Id, Keys] : SemanticIdToKeys)
			{
				Keys.Remove(Key);
			}
			SemanticIdToKeys.FindOrAdd(OverrideSemanticId).Add(Key);
		}

		return SemanticIdToKeys;
	}
}

void UTempoActorLabeler::RegisterServices(FTempoServer& Server)
{
	Server.RegisterService<LabelService>(
		SimpleRequestHandler(&LabelAsyncService::RequestGetInstanceToSemanticIdMap, &UTempoActorLabeler::GetInstanceToSemanticIdMap),
		SimpleRequestHandler(&LabelAsyncService::RequestGetAllActorLabels, &UTempoActorLabeler::HandleGetAllActorLabels),
		SimpleRequestHandler(&LabelAsyncService::RequestGetLabeledActorTypes, &UTempoActorLabeler::HandleGetLabeledActorTypes),
		SimpleRequestHandler(&LabelAsyncService::RequestGetSemanticClasses, &UTempoActorLabeler::HandleGetSemanticClasses),
		SimpleRequestHandler(&LabelAsyncService::RequestSetActorTypeSemanticId, &UTempoActorLabeler::HandleSetActorTypeSemanticId),
		SimpleRequestHandler(&LabelAsyncService::RequestGetAllStaticMeshTypes, &UTempoActorLabeler::HandleGetAllStaticMeshTypes),
		SimpleRequestHandler(&LabelAsyncService::RequestSetStaticMeshTypeSemanticId, &UTempoActorLabeler::HandleSetStaticMeshTypeSemanticId),
		SimpleRequestHandler(&LabelAsyncService::RequestGetAllSkeletalMeshTypes, &UTempoActorLabeler::HandleGetAllSkeletalMeshTypes),
		SimpleRequestHandler(&LabelAsyncService::RequestSetSkeletalMeshTypeSemanticId, &UTempoActorLabeler::HandleSetSkeletalMeshTypeSemanticId),
		SimpleRequestHandler(&LabelAsyncService::RequestSetActorTagSemanticId, &UTempoActorLabeler::HandleSetActorTagSemanticId),
		SimpleRequestHandler(&LabelAsyncService::RequestGetLabelTableAsJson, &UTempoActorLabeler::HandleGetLabelTableAsJson),
		SimpleRequestHandler(&LabelAsyncService::RequestSetLabelType, &UTempoActorLabeler::HandleSetLabelType),
		SimpleRequestHandler(&LabelAsyncService::RequestLoadLabelTable, &UTempoActorLabeler::HandleLoadLabelTable),
		SimpleRequestHandler(&LabelAsyncService::RequestSetInstanceLabelUniqueness, &UTempoActorLabeler::HandleSetInstanceLabelUniqueness),
		SimpleRequestHandler(&LabelAsyncService::RequestSetLabelRowOverrides, &UTempoActorLabeler::HandleSetLabelRowOverrides)
	);
}

void UTempoActorLabeler::HandleGetLabeledActorTypes(const TempoCore::Empty& Request, const TResponseDelegate<TempoSensors::GetLabeledActorTypesResponse>& ResponseContinuation)
{
	TempoSensors::GetLabeledActorTypesResponse Response;

	// Check if instance ID mode is enabled. If not, this request is not applicable.
	const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
	if (TempoSensorsSettings->GetLabelType() != ELabelType::Instance)
	{
		ResponseContinuation.ExecuteIfBound(Response, grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "Instance label mode is not enabled"));
		return;
	}

	for (const FName& ClassName : LabeledActorClassNames)
	{
		Response.add_types(TCHAR_TO_UTF8(*ClassName.ToString()));
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoActorLabeler::HandleGetSemanticClasses(const TempoCore::Empty& Request, const TResponseDelegate<TempoSensors::GetSemanticClassesResponse>& ResponseContinuation)
{
	TempoSensors::GetSemanticClassesResponse Response;

	// The overrides are keyed on class name, so reduce the table's class keys to names to match.
	TMap<FName, FName> ActorTypeLabels;
	for (const auto& [ActorClass, LabelName] : ActorSemanticLabels)
	{
		ActorTypeLabels.Add(ActorClass->GetFName(), LabelName);
	}

	const TMap<int32, TArray<FName>> SemanticIdToActorTypes = BuildSemanticIdToKeys(ActorTypeLabels, SemanticIds, ActorTypeSemanticIdOverrides);
	const TMap<int32, TArray<FString>> SemanticIdToMeshPaths = BuildSemanticIdToKeys(StaticMeshLabels, SemanticIds, StaticMeshTypeSemanticIdOverrides);
	const TMap<int32, TArray<FString>> SemanticIdToSkeletalMeshPaths = BuildSemanticIdToKeys(SkeletalMeshLabels, SemanticIds, SkeletalMeshTypeSemanticIdOverrides);
	const TMap<int32, TArray<FName>> SemanticIdToActorTags = BuildSemanticIdToKeys(ActorTagLabels, SemanticIds, ActorTagSemanticIdOverrides);
	// Component tags have no runtime override RPC, so the table is the whole story for them.
	const TMap<int32, TArray<FName>> SemanticIdToComponentTags = BuildSemanticIdToKeys(ComponentTagLabels, SemanticIds, TMap<FName, int32>());

	for (const auto& [LabelName, SemanticId] : SemanticIds)
	{
		auto* ClassInfo = Response.add_classes();
		ClassInfo->set_name(TCHAR_TO_UTF8(*LabelName.ToString()));
		ClassInfo->set_label_id(SemanticId);

		if (const TArray<FName>* Types = SemanticIdToActorTypes.Find(SemanticId))
		{
			for (const FName& TypeName : *Types)
			{
				ClassInfo->add_actor_types(TCHAR_TO_UTF8(*TypeName.ToString()));
			}
		}

		if (const TArray<FString>* MeshPaths = SemanticIdToMeshPaths.Find(SemanticId))
		{
			for (const FString& MeshPath : *MeshPaths)
			{
				ClassInfo->add_static_mesh_types(TCHAR_TO_UTF8(*MeshPath));
			}
		}

		if (const TArray<FString>* SkeletalMeshPaths = SemanticIdToSkeletalMeshPaths.Find(SemanticId))
		{
			for (const FString& MeshPath : *SkeletalMeshPaths)
			{
				ClassInfo->add_skeletal_mesh_types(TCHAR_TO_UTF8(*MeshPath));
			}
		}

		if (const TArray<FName>* ComponentTags = SemanticIdToComponentTags.Find(SemanticId))
		{
			for (const FName& ComponentTag : *ComponentTags)
			{
				ClassInfo->add_component_tags(TCHAR_TO_UTF8(*ComponentTag.ToString()));
			}
		}

		if (const TArray<FName>* ActorTags = SemanticIdToActorTags.Find(SemanticId))
		{
			for (const FName& ActorTag : *ActorTags)
			{
				ClassInfo->add_actor_tags(TCHAR_TO_UTF8(*ActorTag.ToString()));
			}
		}
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoActorLabeler::HandleSetActorTypeSemanticId(const TempoSensors::SetActorTypeSemanticIdRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation)
{
	const int32 SemanticId = Request.semantic_id();

	if (!ValidateSemanticIdRange(SemanticId, ResponseContinuation))
	{
		return;
	}

	// Resolve to an actual class so that either spelling of a Blueprint class name is accepted, the
	// override is keyed on the real class name we report elsewhere, and a name we can't place is
	// rejected rather than silently registering an override no actor will ever match.
	const FString ActorTypeName(UTF8_TO_TCHAR(Request.actor_type().c_str()));
	const UClass* ActorClass = GetSubClassWithName<AActor>(ActorTypeName);
	if (!ActorClass)
	{
		const FString ErrorMsg = FString::Printf(TEXT("No actor class with name '%s' found"), *ActorTypeName);
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(),
			grpc::Status(grpc::StatusCode::NOT_FOUND, TCHAR_TO_UTF8(*ErrorMsg)));
		return;
	}
	const FName ActorType = ActorClass->GetFName();

	// Store or clear override
	if (SemanticId < 0)
	{
		ActorTypeSemanticIdOverrides.Remove(ActorType);
	}
	else
	{
		ActorTypeSemanticIdOverrides.Add(ActorType, SemanticId);
	}

	// Re-label all existing actors of this type
	for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
	{
		if (ActorItr->GetClass()->GetFName() == ActorType)
		{
			UnLabelActor(*ActorItr);
			LabelActor(*ActorItr);
		}
	}

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

void UTempoActorLabeler::CountMeshInstances(bool bSkeletal, TMap<FString, int32>& OutMeshInstanceCounts) const
{
	for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
	{
		AActor* Actor = *ActorItr;

		if (bSkeletal)
		{
			// USkinnedMeshComponent covers USkeletalMeshComponent and the other skinned variants,
			// matching what GetComponentMeshPaths reads a skeletal path from.
			TInlineComponentArray<USkinnedMeshComponent*> SkinnedComponents(Actor);
			for (USkinnedMeshComponent* SkinnedComponent : SkinnedComponents)
			{
				if (const USkinnedAsset* SkinnedAsset = SkinnedComponent->GetSkinnedAsset())
				{
					OutMeshInstanceCounts.FindOrAdd(SkinnedAsset->GetPathName())++;
				}
			}
			continue;
		}

		// 1. Handle regular UStaticMeshComponent (non-instanced)
		TInlineComponentArray<UStaticMeshComponent*> MeshComponents(Actor);
		for (UStaticMeshComponent* MeshComponent : MeshComponents)
		{
			// Skip ISMCs - they'll be handled separately below
			if (Cast<UInstancedStaticMeshComponent>(MeshComponent))
			{
				continue;
			}

			if (const UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh())
			{
				OutMeshInstanceCounts.FindOrAdd(StaticMesh->GetPathName())++;
			}
		}

		// 2. Handle UInstancedStaticMeshComponent (PCG, foliage, etc.)
		// ISMCs store multiple instances of the same mesh
		TInlineComponentArray<UInstancedStaticMeshComponent*> ISMComponents(Actor);
		for (UInstancedStaticMeshComponent* ISMC : ISMComponents)
		{
			if (const UStaticMesh* StaticMesh = ISMC->GetStaticMesh())
			{
				OutMeshInstanceCounts.FindOrAdd(StaticMesh->GetPathName()) += ISMC->GetInstanceCount();
			}
		}

		// 3. Handle the meshes a Niagara mesh renderer instances. The live particle count varies
		// every frame, so count the components drawing the mesh rather than the particles. Niagara
		// mesh renderers instance static meshes only, so this has no skeletal counterpart.
		TInlineComponentArray<UNiagaraComponent*> NiagaraComponents(Actor);
		for (UNiagaraComponent* NiagaraComponent : NiagaraComponents)
		{
			TArray<FString> MeshPaths;
			GetComponentMeshPaths(NiagaraComponent, MeshPaths);
			for (const FString& MeshPath : MeshPaths)
			{
				OutMeshInstanceCounts.FindOrAdd(MeshPath)++;
			}
		}
	}
}

void UTempoActorLabeler::HandleGetAllStaticMeshTypes(const TempoCore::Empty& Request, const TResponseDelegate<TempoSensors::GetAllStaticMeshTypesResponse>& ResponseContinuation)
{
	TempoSensors::GetAllStaticMeshTypesResponse Response;

	TMap<FString, int32> MeshInstanceCounts;
	CountMeshInstances(/*bSkeletal=*/false, MeshInstanceCounts);

	for (const auto& [MeshPath, InstanceCount] : MeshInstanceCounts)
	{
		auto* MeshInfo = Response.add_mesh_types();
		MeshInfo->set_mesh_path(TCHAR_TO_UTF8(*MeshPath));

		// Extract display name from path (e.g., "/Game/Meshes/SM_Tree.SM_Tree" -> "SM_Tree")
		MeshInfo->set_display_name(TCHAR_TO_UTF8(*FPaths::GetBaseFilename(MeshPath)));

		MeshInfo->set_instance_count(InstanceCount);

		// Determine current semantic ID: check overrides first, then DataTable
		MeshInfo->set_current_semantic_id(ResolveMeshSemanticId(MeshPath).Get(-1));
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoActorLabeler::HandleGetAllSkeletalMeshTypes(const TempoCore::Empty& Request, const TResponseDelegate<TempoSensors::GetAllSkeletalMeshTypesResponse>& ResponseContinuation)
{
	TempoSensors::GetAllSkeletalMeshTypesResponse Response;

	TMap<FString, int32> MeshInstanceCounts;
	CountMeshInstances(/*bSkeletal=*/true, MeshInstanceCounts);

	for (const auto& [MeshPath, InstanceCount] : MeshInstanceCounts)
	{
		auto* MeshInfo = Response.add_mesh_types();
		MeshInfo->set_mesh_path(TCHAR_TO_UTF8(*MeshPath));
		MeshInfo->set_display_name(TCHAR_TO_UTF8(*FPaths::GetBaseFilename(MeshPath)));
		MeshInfo->set_instance_count(InstanceCount);
		MeshInfo->set_current_semantic_id(ResolveMeshSemanticId(MeshPath).Get(-1));
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoActorLabeler::SetMeshTypeSemanticIdOverride(const FString& MeshPath, int32 SemanticId, TMap<FString, int32>& Overrides)
{
	// Store or clear override
	if (SemanticId < 0)
	{
		Overrides.Remove(MeshPath);
	}
	else
	{
		Overrides.Add(MeshPath, SemanticId);
	}

	// Re-label all components rendering this mesh, Niagara mesh renderers included.
	for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
	{
		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(*ActorItr);
		for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			TArray<FString> MeshPaths;
			GetComponentMeshPaths(PrimitiveComponent, MeshPaths);
			if (MeshPaths.Contains(MeshPath))
			{
				UnLabelComponent(PrimitiveComponent);
				if (const FInstanceSemanticIdPair* ActorIdPair = LabeledObjects.Find(*ActorItr))
				{
					LabelComponent(PrimitiveComponent, *ActorIdPair);
				}
			}
		}
	}
}

void UTempoActorLabeler::HandleSetStaticMeshTypeSemanticId(const TempoSensors::SetStaticMeshTypeSemanticIdRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation)
{
	const FString MeshPath = UTF8_TO_TCHAR(Request.static_mesh_path().c_str());
	const int32 SemanticId = Request.semantic_id();

	if (!ValidateSemanticIdRange(SemanticId, ResponseContinuation))
	{
		return;
	}

	// An override has to land in the map the reporting RPCs read for this asset kind, so a path
	// naming something else belongs in the skeletal RPC (or nowhere). Resolving it also rejects a
	// typo rather than recording an override no component will ever match. The table's own paths
	// are already typed by the column they came from, so they need no load.
	if (!StaticMeshLabels.Contains(MeshPath) && !Cast<UStaticMesh>(FSoftObjectPath(MeshPath).TryLoad()))
	{
		const FString ErrorMsg = FString::Printf(TEXT("'%s' does not name a static mesh. Skeletal meshes are set with SetSkeletalMeshTypeSemanticId."), *MeshPath);
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(),
			grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, TCHAR_TO_UTF8(*ErrorMsg)));
		return;
	}

	SetMeshTypeSemanticIdOverride(MeshPath, SemanticId, StaticMeshTypeSemanticIdOverrides);

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

void UTempoActorLabeler::HandleSetSkeletalMeshTypeSemanticId(const TempoSensors::SetSkeletalMeshTypeSemanticIdRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation)
{
	const FString MeshPath = UTF8_TO_TCHAR(Request.skeletal_mesh_path().c_str());
	const int32 SemanticId = Request.semantic_id();

	if (!ValidateSemanticIdRange(SemanticId, ResponseContinuation))
	{
		return;
	}

	if (!SkeletalMeshLabels.Contains(MeshPath) && !Cast<USkinnedAsset>(FSoftObjectPath(MeshPath).TryLoad()))
	{
		const FString ErrorMsg = FString::Printf(TEXT("'%s' does not name a skeletal mesh. Static meshes are set with SetStaticMeshTypeSemanticId."), *MeshPath);
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(),
			grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, TCHAR_TO_UTF8(*ErrorMsg)));
		return;
	}

	SetMeshTypeSemanticIdOverride(MeshPath, SemanticId, SkeletalMeshTypeSemanticIdOverrides);

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

void UTempoActorLabeler::HandleSetLabelType(const TempoSensors::SetLabelTypeRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation)
{
	ELabelType LabelType;
	switch (Request.label_type())
	{
	case TempoSensors::LT_SEMANTIC:
		{
			LabelType = ELabelType::Semantic;
			break;
		}
	case TempoSensors::LT_INSTANCE:
		{
			LabelType = ELabelType::Instance;
			break;
		}
	default:
		{
			ResponseContinuation.ExecuteIfBound(TempoCore::Empty(),
				grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "label_type must be LT_SEMANTIC or LT_INSTANCE"));
			return;
		}
	}

	// Broadcasts back to OnLabelSettingsChanged, which re-labels the world in the new mode.
	GetMutableDefault<UTempoSensorsSettings>()->SetLabelType(LabelType);

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

void UTempoActorLabeler::HandleLoadLabelTable(const TempoSensors::LoadLabelTableRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation)
{
	FString Json = UTF8_TO_TCHAR(Request.json().c_str());
	const FString RequestedJsonFile = UTF8_TO_TCHAR(Request.json_file().c_str());

	if (!Json.IsEmpty() && !RequestedJsonFile.IsEmpty())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(),
			grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "At most one of json and json_file may be set"));
		return;
	}

	if (Json.IsEmpty() && RequestedJsonFile.IsEmpty())
	{
		// Neither field set means "go back to the table configured in Project Settings". Without
		// this there is no way out of a runtime table once one is loaded: it lives on the settings
		// CDO and supersedes the configured asset for as long as it is set.
		GetMutableDefault<UTempoSensorsSettings>()->SetRuntimeSemanticLabelTable(nullptr);
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
		return;
	}

	if (!RequestedJsonFile.IsEmpty())
	{
		const FString JsonFile = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), RequestedJsonFile);
		if (!FFileHelper::LoadFileToString(Json, *JsonFile))
		{
			const FString ErrorMsg = FString::Printf(TEXT("Could not read label table file '%s'"), *JsonFile);
			ResponseContinuation.ExecuteIfBound(TempoCore::Empty(),
				grpc::Status(grpc::StatusCode::NOT_FOUND, TCHAR_TO_UTF8(*ErrorMsg)));
			return;
		}
	}

	// Import into a fresh table so a table that fails to parse never reaches the world. Only a
	// clean import is installed; on any problem the previously active table stays in place.
	UDataTable* NewSemanticLabelTable = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
	NewSemanticLabelTable->RowStruct = FSemanticLabel::StaticStruct();
	// A row that assigns only actor types shouldn't have to spell out empty StaticMeshTypes and
	// ComponentTags, so let an omitted column keep the row struct's default. Unrecognized columns
	// stay an error: those are typos, and silently dropping one would silently drop its labels.
	NewSemanticLabelTable->bIgnoreMissingFields = true;
	const TArray<FString> ImportProblems = NewSemanticLabelTable->CreateTableFromJSONString(Json);
	if (!ImportProblems.IsEmpty())
	{
		const FString ErrorMsg = FString::Printf(TEXT("Could not import label table: %s"), *FString::Join(ImportProblems, TEXT(" ")));
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(),
			grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, TCHAR_TO_UTF8(*ErrorMsg)));
		return;
	}

	// The importer only reports what it could not parse. A table that parses can still be unusable
	// — an unencodable label ID corrupts the depth of every pixel it covers, an unresolved asset
	// path labels nothing — and none of that is visible to the client from a table it can no longer
	// see. Check before installing, so a bad table is rejected here rather than discovered in the
	// images.
	const TArray<FString> ValidationProblems = ValidateSemanticLabelTable(NewSemanticLabelTable);
	if (!ValidationProblems.IsEmpty())
	{
		const FString ErrorMsg = FString::Printf(TEXT("Label table is not usable: %s"), *FString::Join(ValidationProblems, TEXT(" ")));
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(),
			grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, TCHAR_TO_UTF8(*ErrorMsg)));
		return;
	}

	// Broadcasts back to OnLabelSettingsChanged, which rebuilds the label maps and re-labels the
	// world, and to the sensors, which re-resolve the overridable/overriding label pair.
	GetMutableDefault<UTempoSensorsSettings>()->SetRuntimeSemanticLabelTable(NewSemanticLabelTable);

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

void UTempoActorLabeler::HandleSetInstanceLabelUniqueness(const TempoSensors::SetInstanceLabelUniquenessRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation)
{
	// Governs future instance ID allocations only. Objects already labeled keep the IDs they have.
	UTempoSensorsSettings* TempoSensorsSettings = GetMutableDefault<UTempoSensorsSettings>();
	TempoSensorsSettings->SetGloballyUniqueInstanceLabels(Request.globally_unique());
	TempoSensorsSettings->SetInstantaneouslyUniqueInstanceLabels(Request.instantaneously_unique());

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

void UTempoActorLabeler::HandleSetLabelRowOverrides(const TempoSensors::SetLabelRowOverridesRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation)
{
	const FString OverridableRowName = UTF8_TO_TCHAR(Request.overridable_row_name().c_str());
	const FString OverridingRowName = UTF8_TO_TCHAR(Request.overriding_row_name().c_str());

	// The substitution only happens when both rows resolve, so a request naming just one of them
	// would silently do nothing.
	if (OverridableRowName.IsEmpty() != OverridingRowName.IsEmpty())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(),
			grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
			"overridable_row_name and overriding_row_name must both be set, or both be empty to disable overriding"));
		return;
	}

	// Reject a row name the active table doesn't have, rather than accepting a setting that can
	// only ever resolve to "no override". Checked against the table in effect rather than this
	// subsystem's cached copy, which is null until OnWorldBeginPlay and would skip the check.
	if (!OverridableRowName.IsEmpty())
	{
		const UDataTable* ActiveSemanticLabelTable = GetDefault<UTempoSensorsSettings>()->GetSemanticLabelTable();
		if (!ActiveSemanticLabelTable)
		{
			ResponseContinuation.ExecuteIfBound(TempoCore::Empty(),
				grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "No semantic label table is set"));
			return;
		}

		for (const FString& RowName : { OverridableRowName, OverridingRowName })
		{
			if (!ActiveSemanticLabelTable->GetRowMap().Contains(FName(*RowName)))
			{
				const FString ErrorMsg = FString::Printf(TEXT("Semantic label table has no row named '%s'"), *RowName);
				ResponseContinuation.ExecuteIfBound(TempoCore::Empty(),
					grpc::Status(grpc::StatusCode::NOT_FOUND, TCHAR_TO_UTF8(*ErrorMsg)));
				return;
			}
		}
	}

	// Broadcasts to the sensors, which re-push the resolved pair onto their post-process materials.
	GetMutableDefault<UTempoSensorsSettings>()->SetLabelRowNameOverrides(
		OverridableRowName.IsEmpty() ? NAME_None : FName(*OverridableRowName),
		OverridingRowName.IsEmpty() ? NAME_None : FName(*OverridingRowName));

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

void UTempoActorLabeler::HandleSetActorTagSemanticId(const TempoSensors::SetActorTagSemanticIdRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation)
{
	const int32 SemanticId = Request.semantic_id();

	if (!ValidateSemanticIdRange(SemanticId, ResponseContinuation))
	{
		return;
	}

	const FString ActorTagName(UTF8_TO_TCHAR(Request.actor_tag().c_str()));
	if (ActorTagName.IsEmpty())
	{
		ResponseContinuation.ExecuteIfBound(TempoCore::Empty(),
			grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "actor_tag must not be empty"));
		return;
	}
	const FName ActorTag(*ActorTagName);

	// Store or clear override
	if (SemanticId < 0)
	{
		ActorTagSemanticIdOverrides.Remove(ActorTag);
	}
	else
	{
		ActorTagSemanticIdOverrides.Add(ActorTag, SemanticId);
	}

	// Re-label every Actor carrying this tag. Unlike an Actor type, a tag can be added to an Actor
	// at any time, so there is no set of "already tagged" Actors to consult — walk the world.
	for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
	{
		if (ActorItr->Tags.Contains(ActorTag))
		{
			UnLabelActor(*ActorItr);
			LabelActor(*ActorItr);
		}
	}

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

void UTempoActorLabeler::HandleGetLabelTableAsJson(const TempoCore::Empty& Request, const TResponseDelegate<TempoSensors::GetLabelTableAsJsonResponse>& ResponseContinuation) const
{
	const UDataTable* ActiveSemanticLabelTable = GetDefault<UTempoSensorsSettings>()->GetSemanticLabelTable();
	if (!ActiveSemanticLabelTable)
	{
		ResponseContinuation.ExecuteIfBound(TempoSensors::GetLabelTableAsJsonResponse(),
			grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "No semantic label table is set"));
		return;
	}

	TempoSensors::GetLabelTableAsJsonResponse Response;
	Response.set_json(TCHAR_TO_UTF8(*ExportSemanticLabelTableToJson(ActiveSemanticLabelTable)));

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoActorLabeler::HandleGetAllActorLabels(const TempoCore::Empty& Request, const TResponseDelegate<TempoSensors::GetAllActorLabelsResponse>& ResponseContinuation)
{
	TempoSensors::GetAllActorLabelsResponse Response;

	for (const auto& LabeledObjectPair : LabeledObjects)
	{
		const AActor* Actor = Cast<AActor>(LabeledObjectPair.Key);
		if (!Actor)
		{
			continue;
		}

		const FInstanceSemanticIdPair& IdPair = LabeledObjectPair.Value;

		auto* ActorInfo = Response.add_actors();
		ActorInfo->set_actor_name(TCHAR_TO_UTF8(*UTempoCoreUtils::GetActorIdentifier(Actor)));
		ActorInfo->set_actor_type(TCHAR_TO_UTF8(*Actor->GetClass()->GetName()));
		ActorInfo->set_semantic_id(IdPair.SemanticId);
		ActorInfo->set_instance_id(IdPair.InstanceId);
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

TMap<uint8, uint8> UTempoActorLabeler::GetInstanceToSemanticIdMap() const
{
	TMap<uint8, uint8> Result;
	for (const auto& LabeledObject : LabeledObjects)
	{
		if (LabeledObject.Value.InstanceId != NoInstanceId)
		{
			Result.Add(LabeledObject.Value.InstanceId, LabeledObject.Value.SemanticId);
		}
	}
	return Result;
}

void UTempoActorLabeler::GetInstanceToSemanticIdMap(const TempoCore::Empty& Request, const TResponseDelegate<TempoSensors::InstanceToSemanticIdMap>& ResponseContinuation) const
{
	TMap<uint8, uint8> Map = GetInstanceToSemanticIdMap();

	TempoSensors::InstanceToSemanticIdMap ProtoResponse;
	for (const auto& Pair : Map)
	{
		auto* ProtoPair = ProtoResponse.add_instance_semantic_id_pairs();
		ProtoPair->set_instance_id(Pair.Key);
		ProtoPair->set_semantic_id(Pair.Value);
	}
	ResponseContinuation.ExecuteIfBound(ProtoResponse, grpc::Status_OK);
}

void UTempoActorLabeler::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Only for game worlds
	if (!UTempoCoreUtils::IsGameWorld(&InWorld))
	{
		return;
	}

	SemanticLabelTable = GetDefault<UTempoSensorsSettings>()->GetSemanticLabelTable();

	// Parse the label table into a more convenient structure.
	BuildLabelMaps();

	// Label all actors *after* BeginPlay (UWorldSubsystem::OnWorldBeginPlay is called *before* BeginPlay).
	GetWorld()->OnWorldBeginPlay.AddUObject(this, &UTempoActorLabeler::LabelAllActors);

	// Label all newly spawned actors.
	GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UTempoActorLabeler::LabelActor));

	// UnLabel any destroyed actors.
	GetWorld()->AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateUObject(this, &UTempoActorLabeler::UnLabelActor));

	// Handles labeling or re-labeling any component whose render state is marked dirty (for example their mesh changed).
	UActorComponent::MarkRenderStateDirtyEvent.AddWeakLambda(this, [this](UActorComponent& Component)
	{
		LabelComponent(&Component);
	});

	// Handles labeling any component with a physics state who is created after their Actor is spawned.
	UActorComponent::GlobalCreatePhysicsDelegate.AddWeakLambda(this, [this](UActorComponent* Component)
	{
		LabelComponent(Component);
	});

	// Handles un-labeling any component with a physics state who is destroyed although their Actor is not.
	UActorComponent::GlobalDestroyPhysicsDelegate.AddWeakLambda(this, [this](UActorComponent* Component)
	{
		UnLabelComponent(Component);
	});

	// Handles labeling TempoInstancedStaticMeshComponents when they are registered.
	UTempoInstancedStaticMeshComponent::TempoInstancedStaticMeshRegisteredEvent.AddWeakLambda(this, [this](UActorComponent* Component)
	{
		LabelComponent(Component);
	});

	// Handles un-labeling TempoInstancedStaticMeshComponents when they are unregistered.
	UTempoInstancedStaticMeshComponent::TempoInstancedStaticMeshUnRegisteredEvent.AddWeakLambda(this, [this](UActorComponent* Component)
	{
		UnLabelComponent(Component);
	});

	GetMutableDefault<UTempoSensorsSettings>()->TempoSensorsLabelSettingsChangedEvent.AddUObject(this, &UTempoActorLabeler::OnLabelSettingsChanged);
}

void UTempoActorLabeler::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FTempoServer::Get().ActivateService<LabelService>(this);
}

void UTempoActorLabeler::Deinitialize()
{
	Super::Deinitialize();

	// Drop our binding before clearing the table below, so the resulting broadcast doesn't send us
	// re-labeling a world that is going away.
	GetMutableDefault<UTempoSensorsSettings>()->TempoSensorsLabelSettingsChangedEvent.RemoveAll(this);

	// A table loaded over the API lives on the rooted settings CDO, not the world, so left in place
	// it would silently supersede the configured asset in every later session of this process —
	// including the next PIE run, where picking a different table in Project Settings would then
	// appear to do nothing.
	GetMutableDefault<UTempoSensorsSettings>()->SetRuntimeSemanticLabelTable(nullptr);

	FTempoServer::Get().DeactivateService<LabelService>();
}

void UTempoActorLabeler::BuildLabelMaps()
{
	// Everything below is derived wholly from SemanticLabelTable, and this runs again whenever that
	// table is replaced, so start from empty rather than accumulating the previous table's entries.
	ActorSemanticLabels.Reset();
	StaticMeshLabels.Reset();
	SkeletalMeshLabels.Reset();
	ComponentTagLabels.Reset();
	ActorTagLabels.Reset();
	SemanticIds.Reset();

	// A table set in the editor never passes through LoadLabelTable's check, so report the same
	// problems here. Building the maps anyway is deliberate: whatever the table gets right still
	// labels the world, and each problem below describes an entry that will be missing or wrong.
	for (const FString& Problem : ValidateSemanticLabelTable(SemanticLabelTable))
	{
		UE_LOG(LogTempoSensors, Error, TEXT("Semantic label table: %s"), *Problem);
	}

	if (!SemanticLabelTable)
	{
		return;
	}

	// Ingest one of the row's tag columns. The validator has already reported every key two rows
	// both claim, so here the first claimant simply wins. An empty tag is dropped rather than
	// keyed on NAME_None, which no Actor or component means to match.
	auto IngestColumn = [](auto& Labels, const auto& Keys, const FName& Label)
	{
		for (const FName& Key : Keys)
		{
			if (!Key.IsNone())
			{
				Labels.FindOrAdd(Key, Label);
			}
		}
	};

	SemanticLabelTable->ForeachRow<FSemanticLabel>(TEXT(""), [this, &IngestColumn](const FName& Label, const FSemanticLabel& Value)
	{
		for (const TSubclassOf<AActor>& ActorType : Value.ActorTypes)
		{
			if (ActorType.Get())
			{
				ActorSemanticLabels.FindOrAdd(ActorType, Label);
			}
		}

		for (const TSoftObjectPtr<UStaticMesh>& StaticMeshAsset : Value.StaticMeshTypes)
		{
			if (const UStaticMesh* StaticMesh = StaticMeshAsset.LoadSynchronous())
			{
				StaticMeshLabels.FindOrAdd(StaticMesh->GetPathName(), Label);
			}
		}

		for (const TSoftObjectPtr<USkeletalMesh>& SkeletalMeshAsset : Value.SkeletalMeshTypes)
		{
			if (const USkeletalMesh* SkeletalMesh = SkeletalMeshAsset.LoadSynchronous())
			{
				SkeletalMeshLabels.FindOrAdd(SkeletalMesh->GetPathName(), Label);
			}
		}

		IngestColumn(ActorTagLabels, Value.ActorTags, Label);
		IngestColumn(ComponentTagLabels, Value.ComponentTags, Label);

		SemanticIds.Add(Label, Value.Label);
	});
}

void UTempoActorLabeler::LabelAllActors()
{
	for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
	{
		LabelActor(*ActorItr);
	}
}

void UTempoActorLabeler::LabelActor(AActor* Actor)
{
	if (const FInstanceSemanticIdPair* ActorIdPairPtr = LabeledObjects.Find(Actor))
	{
		// We've labeled this Actor before. Make sure all the components are labeled.
		// Copy to avoid dangling reference if LabeledObjects is modified during labeling.
		FInstanceSemanticIdPair ActorIdPair = *ActorIdPairPtr;
		LabelAllComponents(Actor, ActorIdPair);
		return;
	}

	if (!SemanticLabelTable)
	{
		UE_LOG(LogTempoSensors, Error, TEXT("Semantic Label table was not set"));
		return;
	}

	FInstanceSemanticIdPair ActorIdPair;
	if (const TOptional<int32> SemanticId = ResolveActorSemanticId(Actor))
	{
		ActorIdPair.SemanticId = *SemanticId;
		if (const TOptional<int32> InstanceId = InstanceIdAllocator.Allocate())
		{
			ActorIdPair.InstanceId = *InstanceId;
			// Track actor class names that have been assigned instance IDs
			if (GetDefault<UTempoSensorsSettings>()->GetLabelType() == ELabelType::Instance)
			{
				LabeledActorClassNames.Add(Actor->GetClass()->GetFName());
			}
		}
	}

	LabeledObjects.Add(Actor, ActorIdPair);

	LabelAllComponents(Actor, ActorIdPair);
}

TOptional<int32> UTempoActorLabeler::ResolveActorSemanticId(const AActor* Actor) const
{
	// Most specific rule wins, mirroring ResolveComponentSemanticId. An Actor tag names particular
	// Actors, so it beats the type rules, which name every Actor of a class at once. An Actor
	// carrying tags for two different labels resolves to whichever its Tags array lists first —
	// the same first-match rule component tags follow.

	// Runtime overrides take precedence over the label table, so every tag is offered to the
	// override map before any tag is offered to the table. Checking both per tag instead would let
	// a table entry on an earlier tag beat an override on a later one.
	for (const FName& ActorTag : Actor->Tags)
	{
		if (const int32* OverrideSemanticId = ActorTagSemanticIdOverrides.Find(ActorTag))
		{
			return *OverrideSemanticId;
		}
	}

	for (const FName& ActorTag : Actor->Tags)
	{
		if (const FName* TagLabel = ActorTagLabels.Find(ActorTag))
		{
			if (const int32* TagLabelId = SemanticIds.Find(*TagLabel))
			{
				return *TagLabelId;
			}
			UE_LOG(LogTempoSensors, Error, TEXT("Label %s did not have an associated ID"), *TagLabel->ToString());
		}
	}

	if (const int32* OverrideSemanticId = ActorTypeSemanticIdOverrides.Find(Actor->GetClass()->GetFName()))
	{
		return *OverrideSemanticId;
	}

	// Most derived matching type wins, so a row listing a superclass as a catch-all doesn't
	// outrank a row naming the Actor's own class. Single inheritance makes every pair of matching
	// types comparable — both are ancestors of this class — so there is no ambiguous case to
	// report; the table's own duplicate entries are the validator's business.
	TOptional<int32> ResolvedSemanticId;
	const UClass* BestActorType = nullptr;
	for (const auto& [ActorType, ActorLabel] : ActorSemanticLabels)
	{
		if (!Actor->GetClass()->IsChildOf(ActorType.Get()))
		{
			continue;
		}

		if (BestActorType && !ActorType->IsChildOf(BestActorType))
		{
			continue;
		}

		if (const int32* SemanticId = SemanticIds.Find(ActorLabel))
		{
			BestActorType = ActorType.Get();
			ResolvedSemanticId = *SemanticId;
		}
		else
		{
			UE_LOG(LogTempoSensors, Error, TEXT("Label %s did not have an associated ID"), *ActorLabel.ToString());
		}
	}

	return ResolvedSemanticId;
}

TOptional<FName> UTempoActorLabeler::ResolveSemanticIdRowName(int32 SemanticId) const
{
	for (const auto& [LabelName, RowSemanticId] : SemanticIds)
	{
		if (RowSemanticId == SemanticId)
		{
			return LabelName;
		}
	}

	return TOptional<FName>();
}

void UTempoActorLabeler::LabelAllComponents(const AActor* Actor, FInstanceSemanticIdPair ActorIdPair)
{
	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(Actor);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		LabelComponent(PrimitiveComponent, ActorIdPair);
	}
}

void UTempoActorLabeler::LabelComponent(UActorComponent* Component)
{
	if (!IsValid(Component) || !Component->IsValidLowLevel() || Component->IsBeingDestroyed() || !Component->GetOwner() || !UTempoCoreUtils::IsGameWorld(Component))
	{
		return;
	}

	if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component))
	{
		if (const FInstanceSemanticIdPair* ActorIdPairPtr = LabeledObjects.Find(PrimitiveComponent->GetOwner()))
		{
			// Copy to avoid dangling reference if LabeledObjects is modified during labeling.
			FInstanceSemanticIdPair ActorIdPair = *ActorIdPairPtr;
			LabelComponent(PrimitiveComponent, ActorIdPair);
			return;
		}

		// We've never labeled this component's owner, label the whole Actor instead of just this component.
		LabelActor(PrimitiveComponent->GetOwner());
	}
}

void UTempoActorLabeler::LabelComponent(UPrimitiveComponent* Component, FInstanceSemanticIdPair ActorIdPair)
{
	if (const TOptional<int32> ComponentSemanticId = ResolveComponentSemanticId(Component))
	{
		if (LabeledObjects.Contains(Component))
		{
			// This component is already labeled.
			return;
		}

		FInstanceSemanticIdPair IdPair;
		IdPair.SemanticId = *ComponentSemanticId;
		if (TOptional<int32> InstanceId = InstanceIdAllocator.Allocate())
		{
			IdPair.InstanceId = *InstanceId;
		}

		// Label using the component's own label rather than the owning Actor's.
		LabeledObjects.Add(Component, IdPair);
		AssignId(Component, IdPair);
		return;
	}

	// No component label found. Label with its owning Actor's label.
	LabeledObjects.Add(Component, ActorIdPair);
	AssignId(Component, ActorIdPair);
}

TOptional<int32> UTempoActorLabeler::ResolveComponentSemanticId(const UPrimitiveComponent* Component) const
{
	// Most specific rule wins. A component tag names one particular component, so it beats the
	// mesh rules, which name an asset every component sharing that asset is subject to.
	for (const FName& ComponentTag : Component->ComponentTags)
	{
		if (const FName* TagLabel = ComponentTagLabels.Find(ComponentTag))
		{
			if (const int32* TagLabelId = SemanticIds.Find(*TagLabel))
			{
				return *TagLabelId;
			}
			UE_LOG(LogTempoSensors, Error, TEXT("Label %s did not have an associated ID"), *TagLabel->ToString());
		}
	}

	TArray<FString> MeshPaths;
	GetComponentMeshPaths(Component, MeshPaths);
	for (const FString& MeshPath : MeshPaths)
	{
		if (const TOptional<int32> MeshSemanticId = ResolveMeshSemanticId(MeshPath))
		{
			return MeshSemanticId;
		}
	}

	return TOptional<int32>();
}

TOptional<int32> UTempoActorLabeler::ResolveMeshSemanticId(const FString& MeshPath) const
{
	// Runtime overrides take precedence over the label table. Static and skeletal overrides live in
	// separate maps so each Set RPC reports back exactly what it accepts, but they share one asset
	// path space, so at most one of them can hold any given path.
	if (const int32* OverrideSemanticId = StaticMeshTypeSemanticIdOverrides.Find(MeshPath))
	{
		return *OverrideSemanticId;
	}
	if (const int32* OverrideSemanticId = SkeletalMeshTypeSemanticIdOverrides.Find(MeshPath))
	{
		return *OverrideSemanticId;
	}

	// Static and skeletal meshes are labeled from separate table columns but share one asset path
	// space, so a given path can only ever be in one of these maps.
	const FName* MeshLabel = StaticMeshLabels.Find(MeshPath);
	if (!MeshLabel)
	{
		MeshLabel = SkeletalMeshLabels.Find(MeshPath);
	}

	if (MeshLabel)
	{
		if (const int32* MeshLabelId = SemanticIds.Find(*MeshLabel))
		{
			return *MeshLabelId;
		}
		UE_LOG(LogTempoSensors, Error, TEXT("Label %s did not have an associated ID"), *MeshLabel->ToString());
	}

	return TOptional<int32>();
}

void UTempoActorLabeler::GetComponentMeshPaths(const UPrimitiveComponent* Component, TArray<FString>& OutMeshPaths)
{
	if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
	{
		if (const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh())
		{
			OutMeshPaths.Add(StaticMesh->GetPathName());
		}
		return;
	}

	// USkinnedMeshComponent covers USkeletalMeshComponent and the other skinned variants, and its
	// asset is a USkinnedAsset — USkeletalMesh among them.
	if (const USkinnedMeshComponent* SkinnedMeshComponent = Cast<USkinnedMeshComponent>(Component))
	{
		if (const USkinnedAsset* SkinnedAsset = SkinnedMeshComponent->GetSkinnedAsset())
		{
			OutMeshPaths.Add(SkinnedAsset->GetPathName());
		}
		return;
	}

	// A Niagara system's mesh renderers instance static meshes that belong to no component of their
	// own, so gather them from the asset. Every particle the component draws shares one stencil
	// value (it is one scene proxy), so the first labeled mesh decides the whole component's label.
	if (const UNiagaraComponent* NiagaraComponent = Cast<UNiagaraComponent>(Component))
	{
		const UNiagaraSystem* NiagaraSystem = NiagaraComponent->GetAsset();
		if (!NiagaraSystem)
		{
			return;
		}

		for (const FNiagaraEmitterHandle& EmitterHandle : NiagaraSystem->GetEmitterHandles())
		{
			if (!EmitterHandle.GetIsEnabled())
			{
				continue;
			}
			const FVersionedNiagaraEmitterData* EmitterData = EmitterHandle.GetEmitterData();
			if (!EmitterData)
			{
				continue;
			}
			for (const UNiagaraRendererProperties* RendererProperties : EmitterData->GetRenderers())
			{
				const UNiagaraMeshRendererProperties* MeshRenderer = Cast<UNiagaraMeshRendererProperties>(RendererProperties);
				if (!MeshRenderer)
				{
					continue;
				}
				for (const FNiagaraMeshRendererMeshProperties& MeshProperties : MeshRenderer->Meshes)
				{
					if (MeshProperties.Mesh)
					{
						OutMeshPaths.AddUnique(MeshProperties.Mesh->GetPathName());
					}
				}
			}
		}
	}
}

void UTempoActorLabeler::UnLabelAllActors()
{
	for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
	{
		UnLabelActor(*ActorItr);
	}

	// Clear the set of labeled actor class names
	LabeledActorClassNames.Empty();
}

void UTempoActorLabeler::UnLabelActor(AActor* Actor)
{
	if (!LabeledObjects.Contains(Actor))
	{
		// We've never labeled this Actor.
		return;
	}

	UnLabelAllComponents(Actor);

	// LabelActor allocates an instance ID whatever the label type is, so reclaim it whatever the
	// label type is. Gating this on Instance mode would strand every ID held at the moment the mode
	// changed — SetLabelType flips the type before broadcasting, so the unlabel pass that precedes
	// re-labeling would already see the new one — plus every ID an object spawned and destroyed in
	// Semantic mode passed through.
	if (const FInstanceSemanticIdPair* IdPair = LabeledObjects.Find(Actor); IdPair->InstanceId != NoInstanceId)
	{
		InstanceIdAllocator.Return(IdPair->InstanceId);
	}

	LabeledObjects.Remove(Actor);
}

void UTempoActorLabeler::UnLabelAllComponents(const AActor* Actor)
{
	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(Actor);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		UnLabelComponent(PrimitiveComponent);
	}
}

void UTempoActorLabeler::UnLabelComponent(UActorComponent* Component)
{
	if (!Component->GetOwner() || !UTempoCoreUtils::IsGameWorld(Component))
	{
		return;
	}

	if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component))
	{
		UnLabelComponent(PrimitiveComponent);
	}
}

void UTempoActorLabeler::UnLabelComponent(UPrimitiveComponent* Component)
{
	Component->SetRenderCustomDepth(false);
	Component->SetCustomDepthStencilValue(0);

	// Reclaim an ID the component holds in its own right, not one it merely inherited from its
	// owning Actor — that one is the Actor's to return. Unconditional for the same reason as in
	// UnLabelActor: LabelComponent allocates whatever the label type is.
	if (const FInstanceSemanticIdPair* ComponentIdPair = LabeledObjects.Find(Component); ComponentIdPair && ComponentIdPair->InstanceId != NoInstanceId)
	{
		const FInstanceSemanticIdPair* ActorIdPair = LabeledObjects.Find(Component->GetOwner());
		if (!ActorIdPair || ActorIdPair->InstanceId != ComponentIdPair->InstanceId)
		{
			InstanceIdAllocator.Return(ComponentIdPair->InstanceId);
		}
	}

	LabeledObjects.Remove(Component);
}

void UTempoActorLabeler::OnLabelSettingsChanged()
{
	// Unlabel first, while the maps still describe the labels the world is currently wearing, so
	// every object gives its instance ID back before the labels it was resolved from change.
	UnLabelAllActors();

	UDataTable* ActiveSemanticLabelTable = GetDefault<UTempoSensorsSettings>()->GetSemanticLabelTable();
	if (ActiveSemanticLabelTable != SemanticLabelTable)
	{
		SemanticLabelTable = ActiveSemanticLabelTable;
		BuildLabelMaps();

		// The row names naming the overridable/overriding pair are held separately from the table
		// and outlive it, so a table that doesn't carry those rows silently disables the
		// substitution. Say so once here rather than once per sensor tile.
		const FName OverridableLabelRowName = GetDefault<UTempoSensorsSettings>()->GetOverridableLabelRowName();
		int32 OverridableLabel = 0, OverridingLabel = 0;
		if (!OverridableLabelRowName.IsNone() && !ResolveLabelRowOverrides(SemanticLabelTable, OverridableLabel, OverridingLabel))
		{
			UE_LOG(LogTempoSensors, Warning, TEXT("Label row overrides ('%s' overridden by '%s') do not resolve against the current label table. Per-pixel label overriding is disabled until both rows exist."),
				*OverridableLabelRowName.ToString(), *GetDefault<UTempoSensorsSettings>()->GetOverridingLabelRowName().ToString());
		}
	}

	LabelAllActors();
}

void UTempoActorLabeler::AssignId(UPrimitiveComponent* Component, FInstanceSemanticIdPair IdPair)
{
	if (!Component->bRenderCustomDepth)
	{
		Component->SetRenderCustomDepth(true);
	}
	const int32 StencilValue = GetDefault<UTempoSensorsSettings>()->GetLabelType() == ELabelType::Instance ? IdPair.InstanceId : IdPair.SemanticId;
	if (Component->CustomDepthStencilValue != StencilValue)
	{
		Component->SetCustomDepthStencilValue(StencilValue);
	}
}

FName UTempoActorLabeler::GetActorClassification(const AActor* Actor) const
{
	// Route through the same resolution the label image is drawn from, so an Actor tag or a runtime
	// override doesn't leave TempoWorld's overlap events reporting a different class than the
	// camera renders for the same Actor. An ID no row carries — reachable by overriding to an ID
	// the table doesn't define — has no name to report, so it falls through like no match at all.
	if (const TOptional<int32> SemanticId = ResolveActorSemanticId(Actor))
	{
		if (const TOptional<FName> RowName = ResolveSemanticIdRowName(*SemanticId))
		{
			return *RowName;
		}
	}

	return UDefaultActorClassifier::GetDefaultActorClassification(Actor);
}

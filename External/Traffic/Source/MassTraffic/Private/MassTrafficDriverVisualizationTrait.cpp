// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficDriverVisualizationTrait.h"
#include "MassTrafficFragments.h"
#include "MassTrafficSubsystem.h"
#include "MassExecutionContext.h"
#include "MassEntityTemplateRegistry.h"
#include "MassRepresentationSubsystem.h"
#include "Animation/AnimClassInterface.h"
#include "MassEntityUtils.h"
#include "MassCommonUtils.h"


void UMassTrafficDriverVisualizationTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const 
{
	FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(World);

	UMassRepresentationSubsystem* RepresentationSubsystem = Cast<UMassRepresentationSubsystem>(World.GetSubsystemBase(RepresentationSubsystemClass));
	check(RepresentationSubsystem);
	
	BuildContext.AddFragment<FMassTrafficDriverVisualizationFragment>();

	// Make a mutable copy of Params so we can register the driver meshes and assign the description IDs
	FMassTrafficDriversParameters RegisteredParams = Params;
	if (IsValid(RegisteredParams.DriverTypesData))
	{
		for (const FMassTrafficDriverTypeData& DriverType : RegisteredParams.DriverTypesData->DriverTypes)
		{
			// Register visual types
			FStaticMeshInstanceVisualizationDesc StaticMeshInstanceVisualizationDesc;
			StaticMeshInstanceVisualizationDesc.Meshes.Reserve(DriverType.Meshes.Num());
			for (const FMassTrafficDriverMesh& DriverMesh : DriverType.Meshes)
			{
				FMassStaticMeshInstanceVisualizationMeshDesc& MeshDesc = StaticMeshInstanceVisualizationDesc.Meshes.AddDefaulted_GetRef();
				MeshDesc.Mesh = DriverMesh.StaticMesh;
				MeshDesc.MaterialOverrides = DriverMesh.MaterialOverrides;
				MeshDesc.MinLODSignificance = DriverMesh.MinLODSignificance;
				MeshDesc.MaxLODSignificance = DriverMesh.MaxLODSignificance;
			}
			FStaticMeshInstanceVisualizationDescHandle DriverTypeStaticMeshDescHandle = RepresentationSubsystem->FindOrAddStaticMeshDesc(StaticMeshInstanceVisualizationDesc);
			RegisteredParams.DriverTypesStaticMeshDescHandle.Add(DriverTypeStaticMeshDescHandle);
		}
	}

	// Register & add shared fragment
	const FConstSharedStruct ParamsSharedFragment = EntityManager.GetOrCreateConstSharedFragment(RegisteredParams);
	BuildContext.AddConstSharedFragment(ParamsSharedFragment);
}

UMassTrafficDriverInitializer::UMassTrafficDriverInitializer()
	: EntityQuery(*this)
{
	ObservedType = FMassTrafficDriverVisualizationFragment::StaticStruct();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 7
	Operation = EMassObservedOperation::Add;
#else
	ObservedOperations = EMassObservedOperationFlags::Add;
#endif
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
void UMassTrafficDriverInitializer::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
#else
void UMassTrafficDriverInitializer::InitializeInternal(UObject& Owner, const TSharedRef<FMassEntityManager>& EntityManager)
{
	Super::InitializeInternal(Owner, EntityManager);
#endif

	// Seed RandomStream
	const int32 TrafficRandomSeed = UE::Mass::Utils::OverrideRandomSeedForTesting(GetDefault<UMassTrafficSettings>()->RandomSeed);
	if (TrafficRandomSeed >= 0 || UE::Mass::Utils::IsDeterministic())
	{
		RandomStream.Initialize(TrafficRandomSeed);
	}
	else
	{
		RandomStream.GenerateNewSeed();
	}
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
void UMassTrafficDriverInitializer::ConfigureQueries()
#else
void UMassTrafficDriverInitializer::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
#endif
{
	EntityQuery.AddConstSharedRequirement<FMassTrafficDriversParameters>();
	EntityQuery.AddRequirement<FMassTrafficDriverVisualizationFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassTrafficDriverInitializer::Execute(FMassEntityManager& EntityManager,
	FMassExecutionContext& Context)
{
	// Generate random fractions
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& QueryContext)
#else
	EntityQuery.ForEachEntityChunk(Context, [this](FMassExecutionContext& QueryContext)
#endif
	{
		// Get driver types
		const FMassTrafficDriversParameters& Params = QueryContext.GetConstSharedFragment<FMassTrafficDriversParameters>();
		const int32 NumDriverTypes = IsValid(Params.DriverTypesData) ? Params.DriverTypesData->DriverTypes.Num() : 0;
		check(NumDriverTypes <= FMassTrafficDriverVisualizationFragment::InvalidDriverTypeIndex);

		// If no driver types defined, leave DriverTypeIndex at its default InvalidDriverTypeIndex
		if (NumDriverTypes == 0)
		{
			return;
		}
		
		const TArrayView<FMassTrafficDriverVisualizationFragment> DriverVisualizationFragments = QueryContext.GetMutableFragmentView<FMassTrafficDriverVisualizationFragment>();
		for (FMassTrafficDriverVisualizationFragment& DriverVisualizationFragment : DriverVisualizationFragments)
		{
			// Assign a random driver type
			DriverVisualizationFragment.DriverTypeIndex = RandomStream.RandHelper(NumDriverTypes);
		}
	});
}

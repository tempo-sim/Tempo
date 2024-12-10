// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "MassAgentTraits.h"
#include "MassCommonFragments.h"
#include "MassEntityTraitBase.h"

#include "TempoMassAgentSyncTraits.generated.h"

UCLASS(BlueprintType, EditInlineNew, CollapseCategories, meta = (DisplayName = "Agent Brightness Meter Sync"))
class TEMPOAGENTSSHARED_API UTempoMassAgentBrightnessMeterSyncTrait : public UMassAgentSyncTrait
{
	GENERATED_BODY()

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};

USTRUCT()
struct FSceneComponentWrapperFragment : public FObjectWrapperFragment
{
	GENERATED_BODY()
	TWeakObjectPtr<USceneComponent> Component;
};

USTRUCT()
struct FTempoMassTransformCopyToMassTag : public FMassTag
{
	GENERATED_BODY()
};

UCLASS()
class MASSACTORS_API UTempoMassSceneComponentTransformToMassTranslator : public UMassTranslator
{
	GENERATED_BODY()
public:
	UTempoMassSceneComponentTransformToMassTranslator();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};

/** The trait keeps the actor's and entity's transforms in sync. */
UCLASS(BlueprintType, EditInlineNew, CollapseCategories, meta = (DisplayName = "Agent Transform Sync"))
class MASSACTORS_API UMassAgentTransformSyncTrait : public UMassAgentSyncTrait
{
	GENERATED_BODY()

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};

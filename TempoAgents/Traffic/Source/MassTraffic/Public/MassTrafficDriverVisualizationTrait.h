// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficDrivers.h"

#include "MassEntityTraitBase.h"
#include "MassObserverProcessor.h"
#include "MassRepresentationSubsystem.h"

#include "MassTrafficDriverVisualizationTrait.generated.h"

UCLASS(meta=(DisplayName="Traffic Vehicle Driver Visualization"))
class MASSTRAFFIC_API UMassTrafficDriverVisualizationTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:
	
	UPROPERTY(EditAnywhere, Category = "Mass Traffic|Drivers")
	FMassTrafficDriversParameters Params;

	/** Allow subclasses to override the representation subsystem to use */
	UPROPERTY()
	TSubclassOf<UMassRepresentationSubsystem> RepresentationSubsystemClass = UMassRepresentationSubsystem::StaticClass();

	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};

UCLASS()
class MASSTRAFFIC_API UMassTrafficDriverInitializer : public UMassObserverProcessor
{
	GENERATED_BODY()
	
public:
	UMassTrafficDriverInitializer();	

protected:
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

protected:
	
	FMassEntityQuery EntityQuery;
	FRandomStream RandomStream;
};

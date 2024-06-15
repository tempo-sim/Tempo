// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "EditorUtilityActor.h"

#include "MassTrafficEditor.h"

// Last
#include "MassTrafficEditorBaseActor.generated.h"


#define USE_CUSTOM_EVENT false


UCLASS(BlueprintType)
class MASSTRAFFICEDITOR_API AMassTrafficEditorBaseActor : public AEditorUtilityActor
{
	GENERATED_BODY()
	
public:

	AMassTrafficEditorBaseActor();

	virtual void Tick(float DeltaTime) override;
	virtual bool ShouldTickIfViewportsOnly() const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mass Traffic|Mass Traffic Editor|Tick")
	bool CanTickInEditor = true;

	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Editor|Mass Traffic Builder|Editor")
    void RefreshEditor();
};

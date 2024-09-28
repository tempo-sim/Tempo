// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "TempoCoreUtils.generated.h"

UCLASS()
class TEMPOCORESHARED_API UTempoCoreUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	template <typename TEnum>
	static FString GetEnumValueAsString(const TEnum Value, bool bQualified=false)
	{
		FString ValueString = UEnum::GetValueAsString(Value);
		if (!bQualified)
		{
			if (int32 LastColonIdx; ValueString.FindLastChar(':', LastColonIdx))
			{
				ValueString.RightChopInline(LastColonIdx + 1);
			}
		}
		return ValueString;
	}

	UFUNCTION(BlueprintCallable, Category="TempoCoreUtils", meta=(WorldContext="WorldContextObject"))
	static AWorldSettings* GetWorldSettings(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category="TempoCoreUtils",  meta=(WorldContext="WorldContextObject", DeterminesOutputType="Interface"))
	static UWorldSubsystem* GetSubsystemImplementingInterface(const UObject* WorldContextObject, TSubclassOf<UInterface> Interface);

	// Is the world owning this object a PIE or Game world?
	// Note that UWorld::GetWorld() considers GamePreview and GameRPC worlds to be Game worlds, which we do not.
	UFUNCTION(BlueprintCallable, Category="TempoCoreUtils",  meta=(WorldContext="WorldContextObject"))
	static bool IsGameWorld(const UObject* WorldContextObject);

	// Calculates a tight bounding box of all the Actor's components,
	// axis-aligned with the Actor's local coordinates.
	UFUNCTION(BlueprintCallable, Category="TempoCoreUtils")
	static FBox GetActorLocalBounds(const AActor* Actor);

	template <typename BaseClass>
	static bool IsMostDerivedSubclass(UClass* Class)
	{
		// RF_NoFlags to include CDO
		for (TObjectIterator<BaseClass> DerivedClass(EObjectFlags::RF_NoFlags); DerivedClass; ++DerivedClass)
		{
			if (DerivedClass->GetClass() != Class && DerivedClass->IsA(Class))
			{
				// There is a more derived version of Class
				return false;
			}
		}

		return true;
	}

	// Wraps all BP calls in FEditorScriptExecutionGuard when in the Editor, which prevents early termination (and lurking
	// bugs due to calls silently being cancelled and returning default values) due to erroneous runaway loop detection.
	template <typename ObjectType, typename FuncType, typename... ArgTypes>
	static auto CallBlueprintFunction(ObjectType* Object, FuncType Function, ArgTypes&&... Args)
	{
		if (Object->GetWorld()->WorldType != EWorldType::Editor)
		{
			return Function(Object, Args...);
		}

		using RetValType = decltype(Function(Object, std::forward<ArgTypes>(Args)...)); // Deduce return type
		if constexpr (std::is_void_v<RetValType>)
		{
			if (!ensureMsgf(IsValid(Object), TEXT("Tried to call Blueprint function on invalid object")))
			{
				return;
			}
			{
				FEditorScriptExecutionGuard ScriptExecutionGuard;
				Function(Object, std::forward<ArgTypes>(Args)...);
			}
		}
		else
		{
			if (!ensureMsgf(IsValid(Object), TEXT("Tried to call Blueprint function on invalid object")))
			{
				return RetValType();
			}
			RetValType RetVal;
			{
				FEditorScriptExecutionGuard ScriptExecutionGuard;
				RetVal = Function(Object, std::forward<ArgTypes>(Args)...);
			}
			return RetVal;
		}
	}
};

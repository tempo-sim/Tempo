// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "TempoCoreUtils.generated.h"

UCLASS()
class TEMPOCORE_API UTempoCoreUtils : public UBlueprintFunctionLibrary
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

	UFUNCTION(BlueprintCallable, Category="TempoCoreUtils",  meta=(WorldContext="WorldContextObject", DeterminesOutputType="Interface"))
	static UWorldSubsystem* GetSubsystemImplementingInterface(const UObject* WorldContextObject, TSubclassOf<UInterface> Interface);

	// Is the world owning this object a PIE or Game world?
	// Note that UWorld::GetWorld() considers GamePreview and GameRPC worlds to be Game worlds, which we do not.
	UFUNCTION(BlueprintCallable, Category="TempoCoreUtils",  meta=(WorldContext="WorldContextObject"))
	static bool IsGameWorld(const UObject* WorldContextObject);

	// Calculates a tight bounding box of all the Actor's components,
	// axis-aligned with the Actor's local coordinates.
	UFUNCTION(BlueprintCallable, Category="TempoCoreUtils")
	static FBox GetActorLocalBounds(const AActor* Actor, bool bIncludeHiddenComponents);

	// Returns a stable, round-trippable name for an actor, suitable for handing to an external
	// client and using later to look the same actor back up (e.g. via GetActorWithName).
	//
	// AActor::GetActorNameOrLabel() is not stable over an actor's lifetime in editor builds: it
	// returns the FName (which keeps the Blueprint "_C" suffix, e.g. "BP_Foo_C_0") until an actor
	// label is lazily created, after which it returns the de-"_C"'d label ("BP_Foo_0"). A name
	// returned to a client before its label exists therefore fails a later lookup. Materializing
	// the label here pins the value so every subsequent GetActorNameOrLabel() call agrees. This is
	// a no-op in cooked builds, which have no labels (GetActorNameOrLabel() is always GetName()).
	static FString GetActorIdentifier(const AActor* Actor);

	// Returns the name Tempo's API uses to refer to a class, both when reporting a class to a
	// client and when a client names one back to us.
	//
	// Blueprint-generated classes are named "<BlueprintName>_C" (e.g. "BP_Foo_C"), which is an
	// implementation detail clients shouldn't have to know about. This strips that suffix, so the
	// class of a "BP_Foo" Blueprint is reported as "BP_Foo" -- consistent with GetActorIdentifier,
	// which likewise reports actor names without it. Native class names are returned unchanged.
	//
	// This is the outbound half of the convention; GetSubClassWithName (TempoWorldUtils.h) is the
	// inbound half, and accepts either spelling.
	static FString GetClassIdentifier(const UClass* Class);

	// GetClassIdentifier as an FName, for use as a map key.
	static FName GetClassIdentifierName(const UClass* Class);

	// Removes a trailing Blueprint "_C" from a class name, if present. Prefer GetClassIdentifier
	// when you have the UClass; this is for names that arrive as bare strings (e.g. over an RPC).
	static FString StripBlueprintClassSuffix(const FString& ClassName);

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

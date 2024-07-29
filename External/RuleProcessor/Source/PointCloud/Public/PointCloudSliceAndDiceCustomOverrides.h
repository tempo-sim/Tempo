// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/StructOnScope.h"
#include "InstancedStruct.h"

#include "PointCloudSliceAndDiceCustomOverrides.generated.h"

USTRUCT(BlueprintType)
struct POINTCLOUD_API FCustomOverrides
{
	GENERATED_BODY()

	FCustomOverrides() = default;
	~FCustomOverrides();
	FCustomOverrides(const FCustomOverrides& InOverrides);
	FCustomOverrides(FCustomOverrides&& InOverrides);
	FCustomOverrides& operator=(const FCustomOverrides& InOverrides);
	FCustomOverrides& operator=(FCustomOverrides&& InOverrides);

	/**
	* Serialize this rule data to and from an FArchive
	* @param Ar - A reference to the Archive to serialized to / from
	*/
	bool Serialize(FArchive& Ar);

	/**
	* Set the package owner (associates created class with this package)
	*/
	void SetOwner(UObject* InOwner) { Owner = InOwner; }

#if WITH_EDITOR
	/** Adds a property */
	void AddProperty(const FName& InName, FProperty* InProperty, const uint8* InSourceData);

	/** Removes a property */
	void RemoveProperty(const FName& InName);
#endif

	/** Returns list of property names */
	TArray<FName> GetProperties() const;

	/** Returns a property matching the name criterion if any */
	FProperty* GetPropertyByName(const FName& InPropertyName) const;

	/** Returns the memory location of a given property */
	const uint8* GetPropertyValuePtr(FProperty* InProperty) const;

protected:
	/** Holds the dynamically created struct class */
	UPROPERTY(Transient)
	TObjectPtr<UScriptStruct> StructClass = nullptr;

	bool bStructClassOwner = false;

	/** Holds the struct instance */
	UPROPERTY(EditAnywhere, Transient, meta =(StructTypeConst, DisplayName = "Custom override values"))
	FInstancedStruct StructInstance;

private:
#if WITH_EDITOR
	/** Creates a new class, migrates data to new instance */
	void UpdateClass(FProperty* InPropertyToAdd, const uint8* InSourceData, const FName& InPropertyToRemove);
#endif

	/** Package owner */
	UObject* Owner = nullptr;
};

template<> struct TStructOpsTypeTraits<FCustomOverrides> : public TStructOpsTypeTraitsBase2<FCustomOverrides>
{
	enum
	{
		WithSerializer = true
	};
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "PointCloudSliceAndDiceCustomOverrides.h"

#include "PointCloudSliceAndDiceRuleData.generated.h"

class UPointCloudRule;

USTRUCT(BlueprintType)
struct POINTCLOUD_API FPointCloudRuleData
{
	GENERATED_BODY()

	FPointCloudRuleData();
	virtual ~FPointCloudRuleData() = default;

	/** Returns the StaticStruct() of the derived class, should always be overriden */
	virtual UScriptStruct* GetStruct() const;

public:
	/** Applies local override based on the source data overrides */
	void ApplyOverrides(const FPointCloudRuleData* InSourceData);

	/** Register a property as being over-ridable. Overridable properties are managed automatically by the rules system
	* and can be set in a calling rule to control the behaviour of child rules. If for example a property "ActorName" is set
	* as Overridable then if no override is provided, the default value is used, but if the propertie is set as overridable
	* calling SetOverRide("ActorName", "MyNewName") in a parent rule will cause "MyNewName" to be used instead
	* @param PropertyName The name of the property to make overridable
	* @return True if the property can be overridden
	*/
	bool RegisterOverrideableProperty(const FName& InPropertyName);
	
	/** Remove a property from the override system
	* @param PropertyName The name of the property to remove from the set of overridable properties
	* @return True if the property was sucessfully removed
	*/
	bool UnregisterOverrideableProperty(const FName& InPropertyName);

	/** Return true if the property is marked as overrideable
	* @param PropertyName The name of the property to check
	* @return True If the property has been registered as overridable using RegisterOverrideProperty, false otherwise
	*/
	bool PropertyIsOverrideable(const FName& InPropertyName) const;

	/** Marks property as overriden, so it will not be overriden further */
	void AddOverridenProperty(const FName& InPropertyName);

	/** Marks property as not overriden, but does not change its value */
	void RemoveOverridenProperty(const FName& InPropertyName);

	/** Returns whether a property is currently marked as overriden */
	bool PropertyIsOverriden(const FName& InPropertyName) const;

	/** Return the list of names of overridable properties 
	* @return The list of names of the properties which can be overridden
	*/
	const TArray<FName>& GetOverridableProperties() const;

	/** Return the list of names of properties that have been overriden
	* @return The name of the properties that have been overridden
	*/
	TArray<FName> GetOverriddenProperties() const;

#if WITH_EDITOR
	/** Adds a custom override from anoter rule data 
	* @param InName Name of the parameter to add
	* @param InData Pointer to the data holding that value
	*/
	virtual void AddCustomOverride(const FName& InName, const FPointCloudRuleData* InData);

	/** Adds a custom override (e.g. not of this data, but propagated to other rules below)
	* @param InName Name of the parameter to add
	* @param InProperty Pointer to the property to duplicate
	*/
	virtual void AddCustomOverride(const FName& InName, FProperty* InProperty, const uint8* InSourceData);

	/** Removes a custom override (e.g. not of this data)
	* @param InName Property name to remove
	*/
	virtual void RemoveCustomOverride(const FName& InName);

	/** Returns list of custom overrides */
	TArray<FName> GetCustomOverrides() const;
#endif

	/** Anonymous property so we can target rules to different world */
	UPROPERTY(Transient)
	TObjectPtr<UWorld> World = nullptr;

	/** Runtime transient value so we can have complex naming patterns based on $IN_VALUE */
	FString NameValue;

	/** User defined custom overrides */
	UPROPERTY(EditAnywhere, Category = Attributes, meta = (ShowOnlyInnerProperties))
	FCustomOverrides CustomOverrides;

protected:
	virtual void ApplyOverride(const FPointCloudRuleData* InSourceData, const FName& InPropertyName);	

	FProperty* GetPropertyByName(const FName& InPropertyName) const;
	const uint8* GetPropertyValuePtr(FProperty* InProperty) const;

private:
	/** Holds list of properties that can be overridden upstream in this rule data */
	UPROPERTY()
	TArray<FName> OverrideableProperties;

	/** Holds list of properties that are overridden for downstream rule data */
	UPROPERTY()
	TArray<FName> OverridenProperties;
};

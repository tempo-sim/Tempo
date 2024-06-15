// Copyright Epic Games, Inc. All Rights Reserved.
#include "PointCloudSliceAndDiceRuleData.h"
#include "PointCloudSliceAndDiceRule.h"

#include "PointCloud.h"

#define LOCTEXT_NAMESPACE "PointCloudRuleData"

FPointCloudRuleData::FPointCloudRuleData()
{
	RegisterOverrideableProperty(TEXT("World"));

	NameValue = TEXT("RuleProcessor");
}

UScriptStruct* FPointCloudRuleData::GetStruct() const
{
	return StaticStruct();
}

void FPointCloudRuleData::ApplyOverrides(const FPointCloudRuleData* InSourceData)
{
	// Early out - nothing to override here or bad source data
	if (OverrideableProperties.Num() == 0 || InSourceData == nullptr)
	{
		return;
	}

	TArray<FName> SourceDataOverridenProperties = InSourceData->GetOverriddenProperties();

	if (SourceDataOverridenProperties.Num() == 0)
	{
		return; // nothing to override with
	}

	// Find matching properties in source data
	for (int32 PropertyIndex = 0; PropertyIndex < OverrideableProperties.Num(); ++PropertyIndex)
	{
		const FName& PropertyName = OverrideableProperties[PropertyIndex];

		// Note: we don't need to check in the custom properties for additional override props here
		// since they never apply to this data
		if (OverridenProperties.Contains(PropertyName) ||
			!SourceDataOverridenProperties.Contains(PropertyName))
		{
			continue;
		}

		ApplyOverride(InSourceData, PropertyName);
	}
}

void FPointCloudRuleData::ApplyOverride(const FPointCloudRuleData* InSourceData, const FName& InPropertyName)
{
	// Note: we're not overriding anything present in the custom overrides, so look locally only
	FProperty* Property = GetStruct()->FindPropertyByName(InPropertyName);

	if (!Property)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Cannot find property %s for this rule data"), *InPropertyName.ToString());
		return;
	}

	bool bOverriden = false;

	// First, cover the normal case where we override property to property
	FProperty* OtherProperty = InSourceData->GetPropertyByName(InPropertyName);

	if (OtherProperty)
	{
		// Allow property copy only for the same types
		if (Property->SameType(OtherProperty))
		{
			uint8* PropertyValuePtr = Property->ContainerPtrToValuePtr<uint8>(this);
			const uint8* OtherPropertyValuePtr = InSourceData->GetPropertyValuePtr(OtherProperty);
			Property->CopyCompleteValue(PropertyValuePtr, OtherPropertyValuePtr);
			bOverriden = true;
		}
		else
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Property %s type mismatch in hierarchy"), *InPropertyName.ToString());
		}
	}

	if (bOverriden)
	{
		// Finally, mark property as overriden
		OverridenProperties.Add(InPropertyName);
	}
}

bool FPointCloudRuleData::RegisterOverrideableProperty(const FName& InPropertyName)
{
	FProperty* Property = GetStruct()->FindPropertyByName(InPropertyName);
	if (Property)
	{
		OverrideableProperties.Add(InPropertyName);
		return true;
	}
	else
	{
		UE_LOG(PointCloudLog, Log, TEXT("Cannot mark %s property overrideable"), *InPropertyName.ToString());
		return false;
	}
}

bool FPointCloudRuleData::UnregisterOverrideableProperty(const FName& InPropertyName)
{
	if (OverrideableProperties.Contains(InPropertyName))
	{
		OverrideableProperties.Remove(InPropertyName);

		// Warn if the property was overriden
		if (OverridenProperties.Contains(InPropertyName))
		{
			UE_LOG(PointCloudLog, Warning, TEXT("Property %s was overriden and is unregistered"), *InPropertyName.ToString());
		}

		return true;
	}
	else
	{
		return false;
	}
}

const TArray<FName>& FPointCloudRuleData::GetOverridableProperties() const
{
	return OverrideableProperties;
}

TArray<FName> FPointCloudRuleData::GetOverriddenProperties() const
{
	TArray<FName> AllOverridenProperties = OverridenProperties;
	AllOverridenProperties.Append(CustomOverrides.GetProperties());

	return AllOverridenProperties;
}

bool FPointCloudRuleData::PropertyIsOverrideable(const FName& InPropertyName) const
{
	return OverrideableProperties.Contains(InPropertyName);
}

void FPointCloudRuleData::AddOverridenProperty(const FName& InPropertyName)
{
	OverridenProperties.Add(InPropertyName);
}

void FPointCloudRuleData::RemoveOverridenProperty(const FName& InPropertyName)
{
	OverridenProperties.Remove(InPropertyName);
}

bool FPointCloudRuleData::PropertyIsOverriden(const FName& InPropertyName) const
{
	return OverridenProperties.Contains(InPropertyName);
}

#if WITH_EDITOR
void FPointCloudRuleData::AddCustomOverride(const FName& InName, const FPointCloudRuleData* InData)
{
	if (InData)
	{
		if (FProperty* Property = InData->GetStruct()->FindPropertyByName(InName))
		{
			AddCustomOverride(InName, Property, InData->GetPropertyValuePtr(Property));
		}
	}
}

void FPointCloudRuleData::AddCustomOverride(const FName& InName, FProperty* InProperty, const uint8* InSourceData)
{
	CustomOverrides.AddProperty(InName, InProperty, InSourceData);
}

void FPointCloudRuleData::RemoveCustomOverride(const FName& InName)
{
	CustomOverrides.RemoveProperty(InName);
}

TArray<FName> FPointCloudRuleData::GetCustomOverrides() const
{
	return CustomOverrides.GetProperties();
}
#endif

FProperty* FPointCloudRuleData::GetPropertyByName(const FName& InPropertyName) const
{
	FProperty* Prop = GetStruct()->FindPropertyByName(InPropertyName);

	if (!Prop)
	{
		Prop = CustomOverrides.GetPropertyByName(InPropertyName);
	}

	return Prop;
}

const uint8* FPointCloudRuleData::GetPropertyValuePtr(FProperty* InProperty) const
{
	check(InProperty);

	if(FProperty* LocalProperty = GetStruct()->FindPropertyByName(InProperty->GetFName()))
	{
		return LocalProperty->ContainerPtrToValuePtr<const uint8>(this);
	}
	else
	{
		return CustomOverrides.GetPropertyValuePtr(InProperty);
	}
}

#undef LOCTEXT_NAMESPACE
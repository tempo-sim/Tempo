// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudSliceAndDiceCustomOverrides.h"

FCustomOverrides::~FCustomOverrides()
{
	// Always reset manually since otherwise the struct class
	// can get deleted before the struct instance is freed
	StructInstance.Reset();

	if (bStructClassOwner && StructClass)
	{
		StructClass->RemoveFromRoot();
	}
}

FCustomOverrides::FCustomOverrides(const FCustomOverrides& InOverrides)
{
	StructClass = InOverrides.StructClass;
	bStructClassOwner = false;
	StructInstance = InOverrides.StructInstance;
	Owner = InOverrides.Owner;
}

FCustomOverrides::FCustomOverrides(FCustomOverrides&& InOverrides)
{
	Swap(StructClass, InOverrides.StructClass);
	Swap(bStructClassOwner, InOverrides.bStructClassOwner);
	Swap(StructInstance, InOverrides.StructInstance);
	Swap(Owner, InOverrides.Owner);
}

FCustomOverrides& FCustomOverrides::operator=(const FCustomOverrides& InOverrides)
{
	if (&InOverrides == this)
	{
		return *this;
	}

	StructClass = InOverrides.StructClass;
	bStructClassOwner = false;
	StructInstance = InOverrides.StructInstance;
	Owner = InOverrides.Owner;

	return *this;
}

FCustomOverrides& FCustomOverrides::operator=(FCustomOverrides&& InOverrides)
{
	if (&InOverrides == this)
	{
		return *this;
	}

	Swap(StructClass, InOverrides.StructClass);
	Swap(bStructClassOwner, InOverrides.bStructClassOwner);
	Swap(StructInstance, InOverrides.StructInstance);
	Swap(Owner, InOverrides.Owner);

	return *this;
}

bool FCustomOverrides::Serialize(FArchive& Ar)
{
	bool bHasData = (StructClass != nullptr);
	Ar << bHasData;

	if (bHasData)
	{
		if (Ar.IsLoading())
		{
			StructClass = NewObject<UScriptStruct>(GetTransientPackage(), NAME_None, RF_Standalone);
			StructClass->AddToRoot();
			bStructClassOwner = true;
		}

		StructClass->Serialize(Ar);

		if (Ar.IsLoading())
		{
			StructInstance = FInstancedStruct(StructClass);
		}
		
		StructClass->SerializeItem(Ar, StructInstance.GetMutableMemory(), nullptr);
	}

	return true;
}

#if WITH_EDITOR
void FCustomOverrides::AddProperty(const FName& InName, FProperty* InProperty, const uint8* InSourceData)
{
	if (InProperty == nullptr)
	{
		return;
	}

	if (GetProperties().Contains(InName))
	{
		return;
	}

	UpdateClass(InProperty, InSourceData, NAME_None);
}

void FCustomOverrides::RemoveProperty(const FName& InName)
{
	if (!StructClass)
	{
		return;
	}

	UpdateClass(nullptr, nullptr, InName);
}

void FCustomOverrides::UpdateClass(FProperty* InPropertyToAdd, const uint8* InSourceData, const FName& InPropertyToRemove)
{
	check(InPropertyToAdd || InPropertyToRemove != NAME_None);

	UScriptStruct* OldClass = StructClass;
	FInstancedStruct OldInstance = MoveTemp(StructInstance);

	if (OldClass && bStructClassOwner)
	{
		OldClass->SetFlags(RF_NewerVersionExists);
		OldClass->ClearFlags(RF_Public | RF_Standalone);
		OldClass->SetStructTrashed(/*bIsTrash*/true);
		OldClass->RemoveFromRoot();
	}

	FGuid NewClassGuid = FGuid::NewGuid();
	FString NewClassName = FString::Format(TEXT("CustomOverrideClass_{0}_{1}_{2}_{3}"), { FString::FromInt(NewClassGuid.A), FString::FromInt(NewClassGuid.B), FString::FromInt(NewClassGuid.C), FString::FromInt(NewClassGuid.D) });
	StructClass = NewObject<UScriptStruct>(GetTransientPackage(), *NewClassName, RF_Standalone);
	StructClass->AddToRoot();

	bool bAddedPropertiesFromOldClass = false;

	if (OldClass)
	{
		// Copy properties from old class
		for (FProperty* P = OldClass->PropertyLink; P; P = P->PropertyLinkNext)
		{
			if (InPropertyToRemove != NAME_None && InPropertyToRemove == P->GetFName())
			{
				continue;
			}

			FProperty* DuplicateProperty = CastField<FProperty>(FField::Duplicate(P, StructClass, P->GetFName()));

			{
				FArchive Ar;
				DuplicateProperty->LinkWithoutChangingOffset(Ar);
			}

			FField::CopyMetaData(P, DuplicateProperty);
			StructClass->AddCppProperty(DuplicateProperty);

			bAddedPropertiesFromOldClass = true;
		}
	}

	if (InPropertyToAdd)
	{
		// Add new property
		FProperty* NewProperty = CastField<FProperty>(FField::Duplicate(InPropertyToAdd, StructClass, InPropertyToAdd->GetFName()));

		{
			FArchive Ar;
			NewProperty->LinkWithoutChangingOffset(Ar);
		}

		FField::CopyMetaData(InPropertyToAdd, NewProperty);
		StructClass->AddCppProperty(NewProperty);
	}

	// Special exit: if we've removed the last propery don't keep the class & instance around
	if (!bAddedPropertiesFromOldClass && !InPropertyToAdd)
	{
		StructClass->RemoveFromRoot();
		StructClass = nullptr;
		StructInstance.Reset();
		bStructClassOwner = false;
		return;
	}

	bStructClassOwner = true;
	StructClass->Bind();
	StructClass->StaticLink(true);

	// Build new instance
	StructInstance = FInstancedStruct(StructClass);

	// Perform copy from old instance
	if (OldInstance.IsValid())
	{
		if (OldClass)
		{
			for (FProperty* P = OldClass->PropertyLink; P; P = P->PropertyLinkNext)
			{
				FProperty* MatchingProperty = StructClass->FindPropertyByName(P->GetFName());
				if (MatchingProperty && P->SameType(MatchingProperty))
				{
					uint8* NewInstanceValuePtr = MatchingProperty->ContainerPtrToValuePtr<uint8>(StructInstance.GetMutableMemory());
					const uint8* OldInstanceValuePtr = P->ContainerPtrToValuePtr<const uint8>(OldInstance.GetMemory());
					MatchingProperty->CopyCompleteValue(NewInstanceValuePtr, OldInstanceValuePtr);
				}
			}
		}

		OldInstance.Reset();
	}

	// Set value in newly created property
	if (InPropertyToAdd && InSourceData)
	{
		FProperty* MatchingProperty = StructClass->FindPropertyByName(InPropertyToAdd->GetFName());
		check(MatchingProperty);

		// We'll treat instanced parameters differently, as we'll need to create a new object for these
		// Otherwise, we'll just copy
		if (CastField<FObjectProperty>(InPropertyToAdd) != nullptr &&
			(InPropertyToAdd->PropertyFlags & (CPF_InstancedReference | CPF_ContainsInstancedReference | CPF_PersistentInstance)) != 0)
		{
			UObject* ObjectTemplate = *((UObject**)InSourceData);
			FObjectProperty* ObjProperty = CastField<FObjectProperty>(MatchingProperty);

			if (ObjProperty && ObjProperty->PropertyClass)
			{
				UObject** TemplateInstanceValuePtr = MatchingProperty->ContainerPtrToValuePtr<UObject*>(StructInstance.GetMutableMemory());
				*TemplateInstanceValuePtr = StaticDuplicateObject(ObjectTemplate, Owner);
			}
		}
		else
		{
			uint8* NewPropertyValuePtr = MatchingProperty->ContainerPtrToValuePtr<uint8>(StructInstance.GetMutableMemory());
			MatchingProperty->CopyCompleteValue(NewPropertyValuePtr, InSourceData);
		}
	}
}
#endif // WITH_EDITOR

TArray<FName> FCustomOverrides::GetProperties() const
{
	TArray<FName> Names;

	if (StructClass)
	{
		for (FProperty* P = StructClass->PropertyLink; P; P = P->PropertyLinkNext)
		{
			Names.Add(P->GetFName());
		}
	}

	return Names;
}

FProperty* FCustomOverrides::GetPropertyByName(const FName& InPropertyName) const
{
	if (StructClass)
	{
		for (FProperty* P = StructClass->PropertyLink; P; P = P->PropertyLinkNext)
		{
			if (P->GetFName() == InPropertyName)
			{
				return P;
			}
		}
	}
	
	return nullptr;
}

const uint8* FCustomOverrides::GetPropertyValuePtr(FProperty* InProperty) const
{
	check(InProperty && GetPropertyByName(InProperty->GetFName()));
	return StructInstance.IsValid() ? InProperty->ContainerPtrToValuePtr<const uint8>(StructInstance.GetMemory()) : nullptr;
}
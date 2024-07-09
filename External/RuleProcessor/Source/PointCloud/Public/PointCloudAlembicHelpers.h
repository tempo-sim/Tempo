// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#endif

THIRD_PARTY_INCLUDES_START
#include "Alembic/AbcGeom/All.h"
THIRD_PARTY_INCLUDES_END

/**
 * Parse the Alembic attribute from the Property Header and add it to the list of metadata attributes.
 * @Param InDataExtent - The number of elements of the array that correspond to a single point (used for array attributes that were flattened into a single large array).
 * @Param InParameters - The property header containing the attribute to parse.
 * @Param InPropName - The name of the attribute to parse.
 * @Param InNumPoints - The number of points to parse the attribute on.
 * @Param OutMetadataColumnNames - The names of each metadata property found on the alembic object.
 * @Param OutMetadataValues - A map between metadata column names and arrays of the metadata values found on the alembic object.
 */
template <typename AbcParamType, typename AbcArraySampleType, typename AttributeType>
void ParseAlembicArrayAttribute(
	int InDataExtent,
	const Alembic::AbcGeom::ICompoundProperty& Parameters,
	const FString& InPropName,
	int InNumPoints,
	TArray<FString>& OutMetadataColumnNames,
	TMap<FString, TArray<FString>>& OutMetadataValues);

/**
 * Parse the Alembic attribute from the Property Header and add it to the list of metadata attributes.
 * @Param InParameters - The property header containing the attribute to parse.
 * @Param InPropName - The name of the attribute to parse.
 * @Param InNumPoints - The number of points to parse the attribute on.
 * @Param OutMetadataColumnNames - The names of each metadata property found on the alembic object.
 * @Param OutMetadataValues - A map between metadata column names and arrays of the metadata values found on the alembic object.
 */
template <typename AbcParamType, typename AbcArraySampleType, typename AttributeType>
void ParseAlembicVectorAttribute(
	const Alembic::AbcGeom::ICompoundProperty& Parameters,
	const FString& InPropName,
	int InNumPoints,
	int InExtent,
	TArray<FString>& OutMetadataColumnNames,
	TMap<FString, TArray<FString>>& OutMetadataValues);

/**
 * Parse the given Alembic object, adding all found points to the database
 * @Param InObject - The Alembic object to parse
 * @Param OutPreparedTransforms - The transforms of each point found in the alembic object. Currently supports translation, orientation, and scale.
 * @Param OutMetadataColumnNames - The names of each metadata property found on the alembic object.
 * @Param OutMetadataValues - A map between metadata column names and arrays of the metadata values found on the alembic object.
 */
void ParseAlembicObject(
	const Alembic::Abc::IObject& InObject,
	TArray<FTransform>& OutPreparedTransforms,
	TArray<FString>& OutMetadataColumnNames,
	TMap<FString, TArray<FString>>& OutMetadataValues);

#endif 

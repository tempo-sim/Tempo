// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "PointCloudAlembicHelpers.h"

#include "Async/ParallelFor.h"
#include "Containers/UnrealString.h"
#include "PointCloud.h"

THIRD_PARTY_INCLUDES_START
#include "Alembic/AbcCoreFactory/IFactory.h"
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "PointCloudAlembic"

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
	TMap<FString, TArray<FString>>& OutMetadataValues)
{
	TArray<FString> MetadataNames;
	if (InDataExtent == 1)
	{
		MetadataNames.Add(InPropName);
	}
	else
	{
		for (int i = 0; i < InDataExtent; i++)
		{
			MetadataNames.Add(InPropName + "." + FString::Printf(TEXT("%d"), i));
		}
	}

	// initialize the metadata value arrays
	TArray<TArray<FString>> MetadataValues;
	MetadataValues.SetNum(MetadataNames.Num());
	for (TArray<FString>& ValueArray : MetadataValues)
	{
		ValueArray.SetNum(InNumPoints);
	}

	// actually read in the values from the Parameters
	AbcParamType Param(Parameters, std::string(TCHAR_TO_UTF8(*InPropName)));

	if (!Param.valid())
	{
		UE_LOG(PointCloudLog, Log, TEXT("Invalid metadata property type for attribute: %s"), *InPropName);
		return;
	}

	AbcArraySampleType SamplePtr = Param.getExpandedValue().getVals();
	ParallelFor(InNumPoints, [&](int32 PointIndex)
	{
		for (int i = 0; i < InDataExtent; i++)
		{
			AttributeType Value = (*SamplePtr)[PointIndex * InDataExtent + i];
			MetadataValues[i][PointIndex] = FString::Format(TEXT("{0}"), { Value });
		}
	});

	for (int i = 0; i < MetadataNames.Num(); i++)
	{
		OutMetadataValues.Add(MetadataNames[i], MoveTemp(MetadataValues[i]));
	}
	OutMetadataColumnNames.Append(MoveTemp(MetadataNames));
}

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
	TMap<FString, TArray<FString>>& OutMetadataValues)
{
	TArray<FString> MetadataNames;
	for (int i = 0; i < InExtent; i++)
	{
		MetadataNames.Add(InPropName + "." + FString::Printf(TEXT("%d"), i));
	}

	// initialize the metadata value arrays
	TArray<TArray<FString>> MetadataValues;
	MetadataValues.SetNum(MetadataNames.Num());
	for (TArray<FString>& ValueArray : MetadataValues)
	{
		ValueArray.SetNum(InNumPoints);
	}

	// actually read in the values from the Parameters
	AbcParamType Param(Parameters, std::string(TCHAR_TO_UTF8(*InPropName)));

	if (!Param.valid())
	{
		UE_LOG(PointCloudLog, Log, TEXT("Invalid metadata property type for attribute: %s"), *InPropName);
		return;
	}

	AbcArraySampleType SamplePtr = Param.getExpandedValue().getVals();
	ParallelFor(InNumPoints, [&](int32 PointIndex)
	{
		for (int Index = 0; Index < InExtent; Index++)
		{
			MetadataValues[Index][PointIndex] = FString::Format(TEXT("{0}"), { (*SamplePtr)[PointIndex][Index] });
		}
	});

	for (int i = 0; i < MetadataNames.Num(); i++)
	{
		OutMetadataValues.Add(MetadataNames[i], MoveTemp(MetadataValues[i]));
	}
	OutMetadataColumnNames.Append(MoveTemp(MetadataNames));
}

/**
 * Parse the given Alembic object, adding all found points to the database
 * @Param InObject - The Alembic object to parse
 * @Param OutPreparedTransforms - The transforms of each point found in the alembic object. Currently supports translation, orientation, and scale.
 * @Param OutMetadataColumnNames - The names of each metadata property found on the alembic object.
 * @Param OutMetadataValues - A map between metadata column names and arrays of the metadata values found on the alembic object.
 */
void ParseAlembicObject(const Alembic::Abc::IObject& InObject, TArray<FTransform>& OutPreparedTransforms, TArray<FString>& OutMetadataColumnNames, TMap<FString, TArray<FString>>& OutMetadataValues)
{
	// Get MetaData info from current Alembic Object
	const Alembic::Abc::MetaData& ObjectMetaData = InObject.getMetaData();
	const uint32 NumChildren = InObject.getNumChildren();

	if (Alembic::AbcGeom::IPoints::matches(ObjectMetaData))
	{
		Alembic::AbcGeom::IPoints Points = Alembic::AbcGeom::IPoints(InObject, Alembic::Abc::kWrapExisting);
		Alembic::AbcGeom::IPoints::schema_type::Sample Sample = Points.getSchema().getValue();

		Alembic::Abc::P3fArraySamplePtr Positions = Sample.getPositions();
		uint32 NumPoints = Positions ? Positions->size() : 0;

		// position has a hard coded sample in alembic, but the rest of the
		// transform must be extracted from the arbitrary geometry parameters
		// and then combined with the positions afterwards
		Alembic::Abc::QuatfArraySamplePtr Orients;
		Alembic::Abc::FloatArraySamplePtr Scales;

		Alembic::AbcGeom::ICompoundProperty Parameters = Points.getSchema().getArbGeomParams();
		for (int Index = 0; Index < Parameters.getNumProperties(); ++Index)
		{
			Alembic::Abc::PropertyHeader PropertyHeader = Parameters.getPropertyHeader(Index);
			FString PropName(PropertyHeader.getName().c_str());

			Alembic::Abc::PropertyType PropType = PropertyHeader.getPropertyType();
			Alembic::Abc::DataType DataType = PropertyHeader.getDataType();
			int TypeExtent = DataType.getExtent();

			// this is a string representation of the number of elements in a
			// flat array that correspond to a single point. Houdini array
			// attributes that are not vector types get converted in this way
			FString MetadataExtent(PropertyHeader.getMetaData().get("arrayExtent").c_str());
			const int SubExtent = (MetadataExtent.Compare("") == 0 ? 1 : FCString::Atoi(*MetadataExtent));

			if (PropName.Compare("orient") == 0)
			{
				Alembic::Abc::IQuatfArrayProperty Param(Parameters, std::string(TCHAR_TO_UTF8(*PropName)));

				if (!Param.valid())
				{
					UE_LOG(PointCloudLog, Log, TEXT("Invalid metadata property type for attribute: %s"), *PropName);
					break;
				}

				Orients = Param.getValue();
			}
			else if (PropName.Compare("scale") == 0)
			{
				Alembic::AbcGeom::IFloatGeomParam Param(Parameters, std::string(TCHAR_TO_UTF8(*PropName)));

				if (!Param.valid())
				{
					UE_LOG(PointCloudLog, Log, TEXT("Invalid metadata property type for attribute: %s"), *PropName);
					break;
				}

				Scales = Param.getExpandedValue().getVals();
			}
			else
			{
				Alembic::Abc::ArraySamplePtr ArraySamplePtr;
				switch (DataType.getPod())
				{
				case Alembic::Util::kInt32POD:
				{
					switch (TypeExtent)
					{
					case 1:
					{
						ParseAlembicArrayAttribute<Alembic::AbcGeom::IInt32GeomParam, Alembic::Abc::Int32ArraySamplePtr, Alembic::Abc::Int32ArraySample::value_type>
							(SubExtent, Parameters, PropName, NumPoints, OutMetadataColumnNames, OutMetadataValues);
					}
					break;
					case 2:
					{
						ParseAlembicVectorAttribute<Alembic::AbcGeom::IV2iGeomParam, Alembic::Abc::V2iArraySamplePtr, FVector2D>
							(Parameters, PropName, NumPoints, TypeExtent, OutMetadataColumnNames, OutMetadataValues);
					}
					break;
					case 3:
					{
						ParseAlembicVectorAttribute<Alembic::AbcGeom::IV3iGeomParam, Alembic::Abc::V3iArraySamplePtr, FVector>
							(Parameters, PropName, NumPoints, TypeExtent, OutMetadataColumnNames, OutMetadataValues);
					}
					break;
					default:
					{
						UE_LOG(PointCloudLog, Warning, TEXT("Skipping unsupported metadata property type (PropType, TypeExtent, DataExtent, DataType, Name): %d, %d, %s, %d, %s "),
							static_cast<int>(PropType), TypeExtent, *MetadataExtent, static_cast<int>(DataType.getPod()), *PropName);
					}
					break;
					}
				}
				break;
				case Alembic::Util::kFloat32POD:
				{
					switch (TypeExtent)
					{
					case 1:
					{
						ParseAlembicArrayAttribute<Alembic::AbcGeom::IFloatGeomParam, Alembic::Abc::FloatArraySamplePtr, Alembic::Abc::FloatArraySample::value_type>
							(SubExtent, Parameters, PropName, NumPoints, OutMetadataColumnNames, OutMetadataValues);
					}
					break;
					case 2:
					{
						ParseAlembicVectorAttribute<Alembic::AbcGeom::IV3fGeomParam, Alembic::Abc::V3fArraySamplePtr, FVector2D>
							(Parameters, PropName, NumPoints, TypeExtent, OutMetadataColumnNames, OutMetadataValues);
					}
					break;
					case 3:
					{
						ParseAlembicVectorAttribute<Alembic::AbcGeom::IV3fGeomParam, Alembic::Abc::V3fArraySamplePtr, FVector>
							(Parameters, PropName, NumPoints, TypeExtent, OutMetadataColumnNames, OutMetadataValues);
					}
					break;
					default:
					{
						UE_LOG(PointCloudLog, Warning, TEXT("Skipping unsupported metadata property type (PropType, TypeExtent, DataExtent, DataType, Name): %d, %d, %s, %d, %s "),
							static_cast<int>(PropType), TypeExtent, *MetadataExtent, static_cast<int>(DataType.getPod()), *PropName);
					}
					break;
					}
				}
				break;
				// sometimes the string attributes come in as unknown type, this is dangerous but Houdini sometimes exports them like this so we need to handle it as strings
				case Alembic::Util::kUnknownPOD:
				{
					UE_LOG(PointCloudLog, Log, TEXT("Unknown metadata property type is being interpretted as string type for attribute: %s"), *PropName);

					/*
					* fallthrough!!
					*/
				}
				case Alembic::Util::kStringPOD:
				{
					Alembic::AbcGeom::IStringGeomParam Param(Parameters, std::string(TCHAR_TO_UTF8(*PropName)));

					if (!Param.valid())
					{
						UE_LOG(PointCloudLog, Log, TEXT("Invalid metadata property type for attribute: %s"), *PropName);
						break;
					}

					if (SubExtent != 1 && SubExtent != NumPoints)
					{
						UE_LOG(PointCloudLog, Log, TEXT("Attribute %s is not a per-point attribute / string arrays are not supported"), *PropName);
						break;
					}

					Alembic::Abc::StringArraySamplePtr SamplePtr = Param.getExpandedValue().getVals();

					TArray<FString> Values;
					for (uint32 PointIndex = 0; PointIndex < NumPoints; PointIndex++) {
						Alembic::Abc::StringArraySample::value_type Value = (*SamplePtr)[PointIndex];
						Values.Add(Value.c_str());
					}

					OutMetadataColumnNames.Add(PropName);
					OutMetadataValues.Add(PropName, Values);
				}
				break;
				default:
				{
					UE_LOG(PointCloudLog, Warning, TEXT("Skipping unsupported metadata property type (PropType, TypeExtent, DataExtent, DataType, Name): %d, %d, %s, %d, %s "),
						static_cast<int>(PropType), TypeExtent, *MetadataExtent, static_cast<int>(DataType.getPod()), *PropName);
				}
				break;
				}
			}
		}

		// default properties
		for (uint32 PointIndex = 0; PointIndex < NumPoints; PointIndex++)
		{
			Alembic::Abc::P3fArraySample::value_type Position = (*Positions)[PointIndex];

			// note the flipped y and z
			FVector Pos = FVector(Position.x, Position.z, Position.y);

			FQuat Ori = FQuat::Identity;
			if (Orients.get() != nullptr)
			{
				Alembic::Abc::QuatfArraySample::value_type Orientation = (*Orients)[PointIndex];

				// these come in really out of order, I'm not sure why. Orientation.r and Orientation.axis().y may still need to be flipped.
				Ori = FQuat(Orientation.r, Orientation.axis().y, Orientation.axis().x, - Orientation.axis().z);
				Ori.Normalize();
			}

			FVector Scale = FVector::OneVector;
			if (Scales.get() != nullptr)
			{
				Alembic::Abc::FloatArraySample::value_type Scalex = (*Scales)[3 * PointIndex];
				Alembic::Abc::FloatArraySample::value_type Scaley = (*Scales)[3 * PointIndex + 1];
				Alembic::Abc::FloatArraySample::value_type Scalez = (*Scales)[3 * PointIndex + 2];

				// note the flipped y and z
				Scale = FVector(Scalex, Scalez, Scaley);
			}

			FTransform Transform = FTransform(Ori, Pos, Scale);
			OutPreparedTransforms.Add(Transform);
		}
	}

	if (NumChildren > 0)
	{
		for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
		{
			ParseAlembicObject(InObject.getChild(ChildIndex), OutPreparedTransforms, OutMetadataColumnNames, OutMetadataValues);
		}
	}
}

#undef LOCTEXT_NAMESPACE
#endif 

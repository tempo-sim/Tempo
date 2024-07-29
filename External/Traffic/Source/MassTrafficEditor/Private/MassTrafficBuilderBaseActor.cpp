// Copyright Epic Games, Inc. All Rights Reserved.


#include "MassTrafficBuilderBaseActor.h"

#include "MassTrafficBuilderMarkerActor.h"
#include "MassTrafficEditorFunctionLibrary.h"

#include "MassTrafficUtils.h"

#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "Misc/DefaultValueHelper.h"
#include "UObject/UObjectGlobals.h"
#include "DrawDebugHelpers.h"
#include "ZoneGraphSubsystem.h"
#include "WorldPartition/DataLayer/DataLayer.h"


typedef TArray<uint32> FPointZoneLaneProfileIndex_to_UniquePerPointLaneProfileIndex;
typedef THierarchicalHashGrid2D<1, 1, int32/*lane index*/> FBasicHGrid;


/*static*/ const FName AMassTrafficBuilderBaseActor::TrafficBuilder_CreatedDebugActorTagName = "TrafficBuilderCreatedDebugActor"; 
/*static*/ const FName AMassTrafficBuilderBaseActor::TrafficBuilder_CreatedZoneShapeActorTagName = "TrafficBuilderCreatedZoneShapeActor"; 
/*static*/ const FName AMassTrafficBuilderBaseActor::TrafficBuilder_CreatedZoneShapeComponentTagName = "TrafficBuilderCreatedZoneShapeComponent";


static const FActorSpawnParameters DefaultActorSpawnParameters;
static const FAttachmentTransformRules RelativeAttachmentTransformRules(EAttachmentRule::KeepRelative, EAttachmentRule::KeepRelative, EAttachmentRule::KeepRelative, false);


// Debug color tints.
static const FLinearColor RoadSegmentDebugColorTint(1.0f, 0.0f, 1.0f, 1.0f);
static const FLinearColor RoadSplineDebugColorTint(0.0f, 1.0f, 1.0f, 1.0f);
static const FLinearColor IntersectionDebugColorTint(1.0f, 1.0f, 0.0f, 1.0f);


AMassTrafficBuilderBaseActor::AMassTrafficBuilderBaseActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	SetActorTickEnabled(true);

	// Conversion transform defaults to a Houdini->UE transform.
	HoudiniToUEConversionTransform = FTransform(FRotator(0.0f, 0.0f, -90.0f), FVector::ZeroVector, FVector(1.0f, 1.0f, -1.0f));
}


/** 
 * Math
 */

FVector AMassTrafficBuilderBaseActor::FlatVectorToFlatRightVector(FVector Vector) const
{
	return FVector(-Vector.Y, Vector.X, 0.0f);
}


FVector AMassTrafficBuilderBaseActor::ConvertPositionFromHoudini(FVector Position, bool bDoConvert) const
{
	return bDoConvert ? HoudiniToUEConversionTransform.TransformPosition(Position) : Position;
}


FVector AMassTrafficBuilderBaseActor::ConvertVectorFromHoudini(FVector Vector, bool bDoConvert) const
{
	return bDoConvert ? HoudiniToUEConversionTransform.TransformVector(Vector) : Vector;
}


/** 
 * Debug
 */

FLinearColor AMassTrafficBuilderBaseActor::MakeDebugColorFromID(FString ID, FLinearColor ColorTint) const
{
	int Total = 0;
	const TArray<TCHAR> Chars = ID.GetCharArray();
	for (TCHAR Char : Chars)
	{
		Total += int(Char);
	}

	FRandomStream RandomStream(Total);
	const FVector RGBRandom = DebugRandomStream.GetUnitVector();
	const FVector RGBTint(ColorTint.R, ColorTint.G, ColorTint.B);
	const FVector RGB = FMath::Lerp(RGBRandom, RGBTint, DebugColorTintBlend);
	
	return FLinearColor(RGB.X, RGB.Y, RGB.Z);
}


FLinearColor AMassTrafficBuilderBaseActor::JitterColor(FLinearColor Color) const
{
	const FVector JitterRGB = DebugRandomStream.GetUnitVector() * DebugColorJitter;

	return FLinearColor(
		FMath::Clamp(Color.R + JitterRGB.X, 0.0f, 1.0f),
		FMath::Clamp(Color.G + JitterRGB.Y, 0.0f, 1.0f),
		FMath::Clamp(Color.B + JitterRGB.Z, 0.0f, 1.0f),
		Color.A);
}


FVector AMassTrafficBuilderBaseActor::JitterPoint(FVector Point) const
{
	return Point + DebugRandomStream.GetUnitVector() * DebugPointJitter;
}


void AMassTrafficBuilderBaseActor::AddDebugMarker(FVector Location, FString Prefix, FString ID, FLinearColor Color)
{
	if (!bDoAddDebugMarkers) return;

	UWorld* World = GetWorld();
	if (!World)	return;

	const bool bDoShowBrightly = DebugLocateMarkerIDs.Contains(ID);

	const float DebugArrowHeight = DebugArrowSize * DebugArrowSize * 0.5;

	AMassTrafficBuilderMarkerActor* MassTrafficBuilderMarkerActor = nullptr;
	{
		const FVector MarkerLocation = FVector(Location.X, Location.Y,  Location.Z + DebugArrowHeight);
		MassTrafficBuilderMarkerActor = World->SpawnActor<AMassTrafficBuilderMarkerActor>(MarkerLocation, FRotator::ZeroRotator, DefaultActorSpawnParameters);
	}

	// Add this marker to the same data layers as this object is and if it's spatially loaded..
	for (const UDataLayerInstance* DataLayer : GetDataLayerInstances())
	{
		MassTrafficBuilderMarkerActor->AddDataLayer(DataLayer);
	}
	MassTrafficBuilderMarkerActor->SetIsSpatiallyLoaded(GetIsSpatiallyLoaded());

	{
		const FText PrefixedIDText = FText::FromString(FString::Printf(TEXT("%s %s"), *Prefix, *ID));
		MassTrafficBuilderMarkerActor->ErrorDescription = PrefixedIDText; 

		UArrowComponent * ArrowComponent = MassTrafficBuilderMarkerActor->ArrowComponent;
		ArrowComponent->ArrowLength = DebugArrowSize;
		ArrowComponent->ArrowSize = DebugArrowSize * 0.5;

		ArrowComponent->ArrowColor = (bDoShowBrightly ? FColor(0, 255, 0, 255) : Color.ToFColor(true));
	}

	{
		MassTrafficBuilderMarkerActor->AttachToActor(GetZoneShapeParentActor(), RelativeAttachmentTransformRules, /*socket*/ NAME_None);

		MassTrafficBuilderMarkerActor->Tags.Add(FName(TrafficBuilder_CreatedDebugActorTagName));

		const FString Label = FString::Printf(TEXT("Marker_Debug_%s_%s"), *Prefix, *ID);
		MassTrafficBuilderMarkerActor->SetActorLabel(Label);
	}
}


void AMassTrafficBuilderBaseActor::AddDebugErrorMarker(FVector Location, FString Prefix, FString ID, FString Error, FString Caller, int SequenceNumber)
{

	if (SequenceNumber >= 0)
	{
		UE_LOG(LogMassTrafficEditor, Error, TEXT("%s - Caller '%s' - %s %s.%d"), *Error, *Caller, *Prefix, *ID, SequenceNumber);
	}
	else
	{
		UE_LOG(LogMassTrafficEditor, Error, TEXT("%s - Caller '%s' - %s %s"), *Error, *Caller, *Prefix, *ID);
	}

	if (!bDoAddDebugMarkers) return;

	UWorld* World = GetWorld();
	if (!World)	return;

	const bool bDoShowBrightly = DebugLocateMarkerIDs.Contains(ID);

	const float DebugArrowHeight = DebugArrowSize * DebugArrowSize * 0.5;
	
	AMassTrafficBuilderMarkerActor* MassTrafficBuilderMarkerActor = nullptr;
	{
		const FVector MarkerLocation = FVector(Location.X, Location.Y,  Location.Z + DebugArrowHeight);
		MassTrafficBuilderMarkerActor = World->SpawnActor<AMassTrafficBuilderMarkerActor>(MarkerLocation, FRotator::ZeroRotator, DefaultActorSpawnParameters);
	}

	// Add this marker to the same data layers as this object is and if it's spatially loaded..
	for (const UDataLayerInstance* DataLayer : GetDataLayerInstances())
	{
		MassTrafficBuilderMarkerActor->AddDataLayer(DataLayer);
	}
	MassTrafficBuilderMarkerActor->SetIsSpatiallyLoaded(GetIsSpatiallyLoaded());

	{
		FFormatNamedArguments Args
		{
			{ TEXT("prefix"), FText::FromString(Prefix) },
			{ TEXT("id"), FText::FromString(ID) },
			{ TEXT("error"), FText::FromString(Error) }
		};
		const FText PrefixedIDErrorText = FText::Format( FText::FromString("{prefix} {id}\n{error}"), Args);
		MassTrafficBuilderMarkerActor->ErrorDescription = PrefixedIDErrorText; 

		UArrowComponent * ArrowComponent = MassTrafficBuilderMarkerActor->ArrowComponent;
		ArrowComponent->ArrowLength = DebugArrowSize;
		ArrowComponent->ArrowSize = DebugArrowSize * 0.5;

		ArrowComponent->ArrowColor = (bDoShowBrightly ? FColor(0, 255, 0, 255) : FColor(255, 0, 0, 255));
	}

	{
		MassTrafficBuilderMarkerActor->AttachToActor(GetZoneShapeParentActor(), RelativeAttachmentTransformRules, /*socket*/ NAME_None);

		MassTrafficBuilderMarkerActor->Tags.Add(FName(TrafficBuilder_CreatedDebugActorTagName));

		const FString Label = FString::Printf(TEXT("Marker_Error_%s"), *ID);
		MassTrafficBuilderMarkerActor->SetActorLabel(Label);
	}
}


void AMassTrafficBuilderBaseActor::DrawDebugPoint(FMassTrafficDebugPoint DebugPoint) 
{
	const FVector Location = FVector(DebugPoint.Point.X, DebugPoint.Point.Y, DebugPoint.Point.Z + DebugLineSegmentThickness / 2.0f);  
	const FColor Color = JitterColor(DebugPoint.Color).ToFColor(true);
	const float Size = DebugPoint.Size; 
	::DrawDebugPoint(GetWorld(), Location, Size, Color, false, 0.0f, 0);
}


void AMassTrafficBuilderBaseActor::DrawDebugLineSegment(FMassTrafficDebugLineSegment DebugLineSegment) 
{
	const FVector& Location1 = DebugLineSegment.Point1;
	const FVector& Location2 = DebugLineSegment.Point2;
	const FColor Color = JitterColor(DebugLineSegment.Color).ToFColor(true);
	const float Thickness = DebugLineSegment.Thickness * DebugLineSegmentThickness;
	::DrawDebugLine(GetWorld(), Location1, Location2, Color, false, 0.0f, 0, Thickness);
}


void AMassTrafficBuilderBaseActor::DrawDebugPoints(const TArray<FMassTrafficDebugPoint>& DebugPoints) 
{
	for (const FMassTrafficDebugPoint& DebugPoint : DebugPoints)
	{
		DrawDebugPoint(DebugPoint);
	}
}


void AMassTrafficBuilderBaseActor::DrawDebugLineSegments(const TArray<FMassTrafficDebugLineSegment>& DebugLineSegments) 
{
	for (const FMassTrafficDebugLineSegment& DebugLineSegment : DebugLineSegments)
	{
		DrawDebugLineSegment(DebugLineSegment);
	}
}


void AMassTrafficBuilderBaseActor::ClearDebug()
{
	TArray<AActor*> Actors;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), TrafficBuilder_CreatedDebugActorTagName, Actors);
	for (AActor* Actor : Actors)
	{
		if (Actor->GetAttachParentActor() == GetZoneShapeParentActor())
		{
			Actor->K2_DestroyActor();
		}
	}

}



/** 
 * Utilities
 */

FString AMassTrafficBuilderBaseActor::FindAsString(const TMap<FString,FString>& StringMap, FString Key, FString Default, bool& bIsValid, bool bDoAllowMissingKey, bool bDoPrintErrors) const
{
	bIsValid = false;
	FString Value = Default;

	if (StringMap.Contains(Key))
	{
		bIsValid = true;
		Value = StringMap[Key];
	}
	else 
	{
		bIsValid = bDoAllowMissingKey;

		if (bDoPrintErrors && !bDoAllowMissingKey)
		{
			UE_LOG(LogMassTrafficEditor, Error, TEXT("%s - Key '%s' not found in string map."), ANSI_TO_TCHAR(__FUNCTION__), *Key);
		}
	}

	return Value;
}


FName AMassTrafficBuilderBaseActor::FindAsName(const TMap<FString,FString>& StringMap, FString Key, FName Default, bool& bIsValid, bool bDoAllowMissingKey, bool bDoPrintErrors) const
{
	bIsValid = false;
	FName Value = Default;

	if (StringMap.Contains(Key))
	{
		bIsValid = true;
		Value = FName(StringMap[Key]);
	}
	else 
	{
		bIsValid = bDoAllowMissingKey;

		if (bDoPrintErrors && !bDoAllowMissingKey)
		{
			UE_LOG(LogMassTrafficEditor, Error, TEXT("%s - Key '%s' not found in string map."), ANSI_TO_TCHAR(__FUNCTION__), *Key);
		}
	}

	return Value;
}


bool AMassTrafficBuilderBaseActor::FindAsBool(const TMap<FString,FString>& StringMap, FString Key, bool Default, bool& bIsValid, bool bDoAllowMissingKey, bool bDoPrintErrors) const
{
	bIsValid = false;
	bool bValue = Default;

	if (StringMap.Contains(Key))
	{
		int Tmp = 0;
		bIsValid = FDefaultValueHelper::ParseInt(StringMap[Key], Tmp);
		bValue = !!Tmp;
	}
	else 
	{
		bIsValid = bDoAllowMissingKey;

		if (bDoPrintErrors && !bDoAllowMissingKey)
		{
			UE_LOG(LogMassTrafficEditor, Error, TEXT("%s - Key '%s' not found in string map."), ANSI_TO_TCHAR(__FUNCTION__), *Key);
		}
	}
	
	return bValue;
}


int AMassTrafficBuilderBaseActor::FindAsInt(const TMap<FString,FString>& StringMap, FString Key, int Default, bool& bIsValid, bool bDoAllowMissingKey, bool bDoPrintErrors) const
{
	bIsValid = false;
	int Value = Default;

	if (StringMap.Contains(Key))
	{
		bIsValid = FDefaultValueHelper::ParseInt(StringMap[Key], Value);
	}
	else 
	{
		bIsValid = bDoAllowMissingKey;

		if (bDoPrintErrors && !bDoAllowMissingKey)
		{
			UE_LOG(LogMassTrafficEditor, Error, TEXT("%s - Key '%s' not found in string map."), ANSI_TO_TCHAR(__FUNCTION__), *Key);
		}
	}

	return Value;
}


float AMassTrafficBuilderBaseActor::FindAsFloat(const TMap<FString,FString>& StringMap, FString Key, float Default, bool& bIsValid, bool bDoAllowMissingKey, bool bDoPrintErrors, bool bDoCheckForNaNs) const
{
	bIsValid = false;
	float Value = Default;

	if (StringMap.Contains(Key))
	{
		bIsValid = FDefaultValueHelper::ParseFloat(StringMap[Key], Value);

		if (bDoCheckForNaNs && isnan(Value))
		{
			UE_LOG(LogMassTrafficEditor, Warning, TEXT("WARNING - AMassTrafficBuilderBaseActor::FindAsFloat() - Key '%s' - Found NaN. Returning zero."), *Key);
			Value = 0.0f;
		}
		else if (bDoCheckForNaNs && isinf(Value))
		{
			UE_LOG(LogMassTrafficEditor, Warning, TEXT("WARNING - AMassTrafficBuilderBaseActor::FindAsFloat() - Key '%s' - Found infinite value. Returning zero."), *Key);
			Value = 0.0f;
		}
	}
	else 
	{
		bIsValid = bDoAllowMissingKey;

		if (bDoPrintErrors && !bDoAllowMissingKey)
		{
			UE_LOG(LogMassTrafficEditor, Error, TEXT("%s - Key '%s' not found in string map."), ANSI_TO_TCHAR(__FUNCTION__), *Key);
		}
	}

	return Value;
}


FVector AMassTrafficBuilderBaseActor::FindAsVector(const TMap<FString, FString>& StringMap, FString XKey, FString YKey, FString ZKey, FVector Default, bool& bIsValid, bool bDoAllowMissingKeys, bool bDoPrintErrors, bool bDoCheckForNaNs) const
{
	bIsValid = false;

	bool bXIsValid = false;
	const float X = FindAsFloat(StringMap, XKey, Default.X, bXIsValid, bDoAllowMissingKeys, bDoPrintErrors, bDoCheckForNaNs);
	if (!bXIsValid && !bDoAllowMissingKeys)
	{
		return FVector::ZeroVector;
	}

	bool bYIsValid = false;
	const float Y = FindAsFloat(StringMap, YKey, Default.Y, bYIsValid, bDoAllowMissingKeys, bDoPrintErrors, bDoCheckForNaNs);
	if (!bYIsValid && !bDoAllowMissingKeys)
	{
		return FVector::ZeroVector;
	}

	bool bZIsValid = false;
	const float Z = FindAsFloat(StringMap, ZKey, Default.Z, bZIsValid, bDoAllowMissingKeys, bDoPrintErrors, bDoCheckForNaNs);
	if (!bZIsValid && !bDoAllowMissingKeys)
	{
		return FVector::ZeroVector;
	}
	
	bIsValid = (bXIsValid && bYIsValid && bZIsValid);

	return FVector(X, Y, Z);
}


FQuat AMassTrafficBuilderBaseActor::FindAsQuaternion(const TMap<FString, FString>& StringMap, FString WKey, FString XKey, FString YKey, FString ZKey, FQuat Default, bool& bIsValid, bool bDoAllowMissingKeys, bool bDoPrintErrors, bool bDoCheckForNaNs) const
{
	bIsValid = false;
	
	bool bWIsValid = false;
	const float W = FindAsFloat(StringMap, WKey, Default.W, bWIsValid, bDoAllowMissingKeys, bDoPrintErrors, bDoCheckForNaNs);
	if (!bWIsValid && !bDoAllowMissingKeys) return FQuat::Identity;

	bool bXIsValid = false;
	const float X = FindAsFloat(StringMap, XKey, Default.X, bXIsValid, bDoAllowMissingKeys, bDoPrintErrors, bDoCheckForNaNs);
	if (!bXIsValid && !bDoAllowMissingKeys) return FQuat::Identity;

	bool bYIsValid = false;
	const float Y = FindAsFloat(StringMap, YKey, Default.Y, bYIsValid, bDoAllowMissingKeys, bDoPrintErrors, bDoCheckForNaNs);
	if (!bYIsValid && !bDoAllowMissingKeys) return FQuat::Identity;
	
	bool bZIsValid = false;
	const float Z = FindAsFloat(StringMap, ZKey, Default.Z, bZIsValid, bDoAllowMissingKeys, bDoPrintErrors, bDoCheckForNaNs);
	if (!bZIsValid && !bDoAllowMissingKeys) return FQuat::Identity;
	
	bIsValid = (bWIsValid && bXIsValid && bYIsValid && bZIsValid);

	return FQuat(W, X, Y, Z);
}


FString AMassTrafficBuilderBaseActor::VectorToMapKey(FVector Vector) const
{
	const float FactorOfTenScale = FractionalFloatPrecisionForMapKeys > 0 ? powf(10.0f, float(FractionalFloatPrecisionForMapKeys)) : 1.0f;
	const FVector VectorScaled = Vector * FactorOfTenScale;
	return FString::Printf(TEXT("%d %d %d"), int(VectorScaled.X), int(VectorScaled.Y), int(VectorScaled.Z));
}


/**
 * Road Segments
 */

void AMassTrafficBuilderBaseActor::AddRoadSegment(FString RoadSegmentID, FMassTrafficPoint StartPoint, FMassTrafficPoint EndPoint, int NumberOfLanes, bool bHasCenterDivider, float LaneWidthCM, float CenterDividerWidthCM, bool bCanSupportLongVehicles, bool bIsFreeway, bool bIsMainPartOfFreeway, int32 UserDensity, EMassTrafficUser User)
{
	const FMassTrafficRoadSegmentMapKey RoadSegmentMapKey{RoadSegmentID, User};
	if (RoadSegmentsMap.Contains(RoadSegmentMapKey)) return;

	FMassTrafficRoadSegment RoadSegment;
	{
		RoadSegment.RoadSegmentID = RoadSegmentID;
		RoadSegment.User = User;
		RoadSegment.DebugColor = MakeDebugColorFromID(RoadSegmentID, RoadSegmentDebugColorTint);
		RoadSegment.StartPoint = StartPoint;
		RoadSegment.EndPoint = EndPoint;
		RoadSegment.NumberOfLanes = NumberOfLanes;
		RoadSegment.bHasCenterDivider = bHasCenterDivider;
		RoadSegment.LaneWidthCM = LaneWidthCM;
		RoadSegment.CenterDividerWidthCM = CenterDividerWidthCM;
		RoadSegment.bCanSupportLongVehicles = bCanSupportLongVehicles;
		RoadSegment.bIsFreeway = bIsFreeway;
		RoadSegment.bIsMainPartOfFreeway = bIsMainPartOfFreeway;
		RoadSegment.UserDensity = UserDensity;
	}
	
	RoadSegmentsMap.Add(RoadSegmentMapKey, RoadSegment);

	AddPointHints(StartPoint.Position, true, true, false, false, false, false, RoadSegmentID, "", "");
	AddPointHints(EndPoint.Position, true, false, true, false, false, false, RoadSegmentID, "", "");
}


/**
 * Road Splines
 */

void AMassTrafficBuilderBaseActor::AddRoadSpline(FString RoadSplineID, int NumberOfLanes, bool bHasCenterDivider, float LaneWidthCM, float CenterDividerWidthCM, bool bIsUnidirectional, bool bIsClosed, bool bCanSupportLongVechicles, bool bIsFreeway, bool bIsMainPartOfFreeway, bool bIsFreewayOnramp, bool bIsFreewayOfframp, int32 UserDensity, EMassTrafficUser User)
{
	const FMassTrafficRoadSplineMapKey RoadSplineMapKey{RoadSplineID, User};
	if (RoadSplinesMap.Contains(RoadSplineMapKey)) return;

	FMassTrafficRoadSpline RoadSpline;
	{
		RoadSpline.RoadSplineID = RoadSplineID;
		RoadSpline.User = User;
		RoadSpline.DebugColor = MakeDebugColorFromID(RoadSplineID, RoadSplineDebugColorTint);;
		RoadSpline.Points.Empty(); // later
		RoadSpline.NumberOfLanes = NumberOfLanes;
		RoadSpline.bHasCenterDivider = bHasCenterDivider;
		RoadSpline.LaneWidthCM = LaneWidthCM;
		RoadSpline.CenterDividerWidthCM = CenterDividerWidthCM;
		RoadSpline.bIsUnidirectional = bIsUnidirectional;
		RoadSpline.bIsClosed = bIsClosed;
		RoadSpline.bCanSupportLongVehicles = bCanSupportLongVechicles;
		RoadSpline.bIsFreeway = bIsFreeway;
		RoadSpline.bIsMainPartOfFreeway = bIsMainPartOfFreeway;
		RoadSpline.bIsFreewayOnramp = bIsFreewayOnramp;
		RoadSpline.bIsFreewayOfframp = bIsFreewayOfframp;
		RoadSpline.UserDensity = UserDensity;
	}
	
	RoadSplinesMap.Add(RoadSplineMapKey, RoadSpline);
}


void AMassTrafficBuilderBaseActor::AddRoadSplinePoint(FString RoadSplineID, int RoadSplineSequenceNumber, FMassTrafficPoint Point, EMassTrafficUser User)
{
	const FMassTrafficRoadSplineMapKey RoadSplineMapKey{RoadSplineID, User};
	if (!RoadSplinesMap.Contains(RoadSplineMapKey)) return;

	FMassTrafficRoadSpline& RoadSpline = RoadSplinesMap[RoadSplineMapKey];

	if (RoadSpline.Points.Num() < RoadSplineSequenceNumber + 1)
	{
		RoadSpline.Points.SetNum(RoadSplineSequenceNumber + 1);
	}
	RoadSpline.Points[RoadSplineSequenceNumber] = Point;

	AddPointHints(Point.Position, false, false, false, true, false, false, "", RoadSplineID, "");
}


// DEPRECATED
void AMassTrafficBuilderBaseActor::LoopAllClosedRoadSplines()
{
	// Loop closed road splines.

	for (auto& RoadSplineMapElem : RoadSplinesMap)
	{
		FMassTrafficRoadSpline& RoadSpline = RoadSplineMapElem.Value;

		if (RoadSpline.bIsClosed)
		{
			if (RoadSpline.Points.Num() < 3)
			{
				UE_LOG(LogMassTrafficEditor, Warning, TEXT("WARNING - AMassTrafficBuilderBaseActor::LoopAllClosedRoadSplines() - RoadSpline ID '%s' has IsClosed, but only has %d points. Needs at least 3."),
					*RoadSpline.RoadSplineID, RoadSpline.Points.Num());
				continue;
			}

			const FVector NewOptionalTangentVector = /*next*/RoadSpline.Points[1].Position - /*prev*/RoadSpline.Points[RoadSpline.Points.Num() - 1].Position; 

			// Set a new valid tangent vector on the first point.
			RoadSpline.Points[0].OptionalTangentVector = NewOptionalTangentVector;
			
			// Add a new point, on top of the first point.
			// Copy first point forward and up vector to it.
			// Set a new valid tangent vector on this new point.
			AddRoadSplinePoint(RoadSpline.RoadSplineID, RoadSpline.Points.Num(), RoadSpline.Points[0], RoadSpline.User);
		}
	}
}


void AMassTrafficBuilderBaseActor::AdjustTangentsForCoincidentRoadSplineEndPoints()
{
	// Adjust tangents for end points of road splines that are coincident.

	TSet<FMassTrafficRoadSplineMapKey> KeySet;
	RoadSplinesMap.GetKeys(KeySet);
	for (FMassTrafficRoadSplineMapKey& Key : KeySet)
	{
		FMassTrafficRoadSpline* ThisRoadSpline = RoadSplinesMap.Find(Key);
		if (!ThisRoadSpline) continue;
		if (ThisRoadSpline->Points.Num() < 2) continue;

		const FString& ThisRoadSplineID = ThisRoadSpline->RoadSplineID;
		const EMassTrafficUser ThisUser = ThisRoadSpline->User;

		// To (hopefully) clarify code below..
		const FVector& ThisFirstPoint = ThisRoadSpline->Points[0].Position;
		const FVector& ThisSecondPoint = ThisRoadSpline->Points[1].Position;
		const FVector& ThisSecondToLastPoint = ThisRoadSpline->Points[ThisRoadSpline->Points.Num() - 2].Position;
		const FVector& ThisLastPoint = ThisRoadSpline->Points[ThisRoadSpline->Points.Num() - 1].Position;

		// To (hopefully) clarify code below..
		static const int32 kEndPointID_First = 0; 
		static const int32 kEndPointID_Last = 1;

		// Look at both end-points of this spline.
		for (int32 ThisEndPointID = kEndPointID_First; ThisEndPointID <= kEndPointID_Last; ThisEndPointID++)
		{
			FMassTrafficPointHints PointHints;
			GetPointHints(ThisEndPointID == kEndPointID_First ? ThisFirstPoint : ThisLastPoint, PointHints);
			if (!PointHints.bIsValid) continue;

			// Compare this end point to the nearby end points of other splines.
			for (const FString& OtherRoadSplineID : PointHints.RoadSplineIDs)
			{
				if (OtherRoadSplineID == ThisRoadSplineID) continue;

				const FMassTrafficRoadSplineMapKey OtherKey{OtherRoadSplineID, ThisUser /*yes, use this*/};
				const FMassTrafficRoadSpline* OtherRoadSpline = RoadSplinesMap.Find(OtherKey);
				if (!OtherRoadSpline) continue;
				if (OtherRoadSpline->Points.Num() < 2) continue;

				// To (hopefully) clarify code below..
				const FVector& OtherFirstPoint = OtherRoadSpline->Points[0].Position;
				const FVector& OtherSecondPoint = OtherRoadSpline->Points[1].Position;
				const FVector& OtherSecondToLastPoint = OtherRoadSpline->Points[OtherRoadSpline->Points.Num() - 2].Position;
				const FVector& OtherLastPoint = OtherRoadSpline->Points[OtherRoadSpline->Points.Num() - 1].Position;

				if (ThisEndPointID == kEndPointID_First && (ThisFirstPoint - OtherLastPoint).IsNearlyZero())
				{
					// This spline's head is joined to another spline's tail.
					// This spline's first tangent vector should be from other spline's second-to-last point to this spline's second point.
					ThisRoadSpline->Points[0].OptionalTangentVector = ThisSecondPoint - OtherSecondToLastPoint;
				}
				else if (ThisEndPointID == kEndPointID_Last && (ThisLastPoint - OtherFirstPoint).IsNearlyZero())
				{
					// This spline's tail is joined to another spline's head.
					// This spline's last tangent vector should be from this spline's second-to-last point to other spline's second point.
					ThisRoadSpline->Points[ThisRoadSpline->Points.Num() - 1].OptionalTangentVector = OtherSecondPoint - ThisSecondToLastPoint;
				}
				// NOTE: Splines that are joined head-to-head or tail-to-tail are not considered.
			}
		}
	}
}


void AMassTrafficBuilderBaseActor::ChopUpAllRoadSplines(int MaxPointsInChunk, float MaxAngleInChunk)
{
	if (MaxPointsInChunk < 0 && MaxAngleInChunk < 0.0f)
	{
		return;	
	}
	
	if (MaxPointsInChunk < 0) // Ignore this?
	{
		MaxPointsInChunk = TNumericLimits<int32>::Max(); // Essentially ends up getting ignored.
	}
	if (MaxPointsInChunk == 0 || MaxPointsInChunk == 1) // But can't be 0 or 1.
	{
		MaxPointsInChunk = 2;
	}

	// MaxAngle < 0? Ends up getting ignored.
	if (MaxAngleInChunk > 179.99f) // But must be under 180 degrees.
	{
		MaxAngleInChunk = 179.99f; 
	}

	TMap<FMassTrafficRoadSplineMapKey,FMassTrafficRoadSpline> OldRoadSplinesMap = RoadSplinesMap;
	RoadSplinesMap.Empty();

	for (auto& OldRoadSplineMapElem : OldRoadSplinesMap)
	{
		const FMassTrafficRoadSpline& OldRoadSpline = OldRoadSplineMapElem.Value;

		int32 NumChunks = 0;
		int32 NumPointsInChunk = 0;
		bool bDoAddPrevPoint = false;
		FVector TangentVectorAtStartOfChunk = (OldRoadSpline.Points[1].Position - OldRoadSpline.Points[0].Position);

		
		FMassTrafficPoint PrevPoint;

		const int32 NumOldRoadSplinePoints = OldRoadSpline.Points.Num();
		for (int32 OldRoadSplinePointIndex = 0; OldRoadSplinePointIndex < NumOldRoadSplinePoints; OldRoadSplinePointIndex++)
		{
			const FString NewRoadSplineID = FString::Printf(TEXT("%s_%ld"), *OldRoadSpline.RoadSplineID, NumChunks);

			AddRoadSpline(NewRoadSplineID, OldRoadSpline.NumberOfLanes, OldRoadSpline.bHasCenterDivider, OldRoadSpline.LaneWidthCM, OldRoadSpline.CenterDividerWidthCM, OldRoadSpline.bIsUnidirectional, false /*not closed now*/, OldRoadSpline.bCanSupportLongVehicles, OldRoadSpline.bIsFreeway, OldRoadSpline.bIsMainPartOfFreeway, OldRoadSpline.bIsFreewayOnramp, OldRoadSpline.bIsFreewayOfframp, OldRoadSpline.UserDensity, OldRoadSpline.User); 

			if (bDoAddPrevPoint)
			{
				AddRoadSplinePoint(NewRoadSplineID, NumPointsInChunk, PrevPoint, OldRoadSpline.User);
				++NumPointsInChunk;
				bDoAddPrevPoint = false;
			}

			// Can't use const references here. Causes crash when adding to arrays, below.
			FMassTrafficPoint Point = OldRoadSpline.Points[OldRoadSplinePointIndex];

			// Get a working tangent vector. We want this regardless of any optional tangent vectors already set on the
			// point.
			FVector TangentVector;
			{
				if (OldRoadSplinePointIndex == 0)
				{					
					TangentVector = /*next*/OldRoadSpline.Points[1].Position - /*prev*/OldRoadSpline.Points[0].Position;
				}
				else if (OldRoadSplinePointIndex == OldRoadSpline.Points.Num() - 1)
				{					
					TangentVector = /*next*/OldRoadSpline.Points[NumOldRoadSplinePoints - 1].Position - /*prev*/OldRoadSpline.Points[NumOldRoadSplinePoints - 2].Position;
				}
				else
				{
					TangentVector = /*next*/OldRoadSpline.Points[OldRoadSplinePointIndex + 1].Position - /*prev*/OldRoadSpline.Points[OldRoadSplinePointIndex - 1].Position;
				}
			}
			
			const bool bMakeNewChunk =
				(NumPointsInChunk == MaxPointsInChunk - 1) ||
				(MaxAngleInChunk >= 0.0f && FVector::DotProduct(TangentVector.GetSafeNormal(), TangentVectorAtStartOfChunk.GetSafeNormal()) <= FMath::Cos(MaxAngleInChunk * 3.14159f / 180.0f));
				
			if (Point.OptionalTangentVector.IsNearlyZero() && /*Tangent vector was not already set (i.e. by the looping or tangent-adjusting functions.)*/
				bMakeNewChunk && /*At end of chunk, and time to make new spline.*/ 
				OldRoadSplinePointIndex > 0 && /*First point's tangent should only ever be set by looping or tangent-adjusting functions.*/
				OldRoadSplinePointIndex < OldRoadSpline.Points.Num() - 1 /*Last point's tangent should only ever be set by looping or tangent-adjusting functions.*/)
			{
				Point.OptionalTangentVector = TangentVector;
			}

			AddRoadSplinePoint(NewRoadSplineID, NumPointsInChunk, Point, OldRoadSpline.User);
			
			++NumPointsInChunk;
			if (bMakeNewChunk)
			{
				NumChunks++;
				NumPointsInChunk = 0;

				PrevPoint = Point;
				
				TangentVectorAtStartOfChunk = TangentVector;
				
				bDoAddPrevPoint = true;
			}
		}
	}
}


/** 
 * Intersections
 */

EMassTrafficSpecialConnectionType AMassTrafficBuilderBaseActor::StringToSpecialConnectionType(FString String) const
{	
	if (String == "" ||
		String.Compare(FString("None"), ESearchCase::IgnoreCase) == 0)
	{
		return EMassTrafficSpecialConnectionType::None;
	}
	else if (String.Compare(FString("CityIntersectionLinkIsConnectionIsBlocked"), ESearchCase::IgnoreCase) == 0 ||
			 String.Compare(FString("blocked"), ESearchCase::IgnoreCase) == 0 /*DEPRECATED but in use*/)
	{
		return EMassTrafficSpecialConnectionType::CityIntersectionLinkIsConnectionIsBlocked;
	}
	else if (String.Compare(FString("CityIntersectionLinkConnectsRoadSegmentNeedingToBeBuilt"), ESearchCase::IgnoreCase) == 0 ||
			 String.Compare(FString("build"), ESearchCase::IgnoreCase) == 0 /*DEPRECATED but in use*/)
	{
		return EMassTrafficSpecialConnectionType::CityIntersectionLinkConnectsRoadSegmentNeedingToBeBuilt;
	}
	else if (String.Compare(FString("CityIntersectionLinkConnectsToIncomingFreewayRamp"), ESearchCase::IgnoreCase) == 0 ||
			 String.Compare(FString("freeway_in"), ESearchCase::IgnoreCase) == 0 /*DEPRECATED but in use*/ ||
			 String.Compare(FString("in"), ESearchCase::IgnoreCase) == 0 /*DEPRECATED*/)
	{
		return EMassTrafficSpecialConnectionType::CityIntersectionLinkConnectsToIncomingFreewayRamp;
	}
	else if (String.Compare(FString("CityIntersectionLinkConnectsToOutgoingFreewayRamp"), ESearchCase::IgnoreCase) == 0 ||
			 String.Compare(FString("freeway_out"), ESearchCase::IgnoreCase) == 0 /*DEPRECATED but in use*/ ||
			 String.Compare(FString("out"), ESearchCase::IgnoreCase) == 0 /*DEPRECATED*/)
	{
		return EMassTrafficSpecialConnectionType::CityIntersectionLinkConnectsToOutgoingFreewayRamp;
	}
	else if (String.Compare(FString("FreewayIntersectionLinkConnectsToIncomingFreewayRamp"), ESearchCase::IgnoreCase) == 0)
	{
		// Included for completeness. Does not appear in data as a string.
		return EMassTrafficSpecialConnectionType::FreewayIntersectionLinkConnectsToIncomingFreewayRamp;
	}
	else if (String.Compare(FString("FreewayIntersectionLinkConnectsToOutgoingFreewayRamp"), ESearchCase::IgnoreCase) == 0)
	{
		// Included for completeness. Does not appear in data as a string.
		return EMassTrafficSpecialConnectionType::FreewayIntersectionLinkConnectsToOutgoingFreewayRamp;
	}
	else if (String.Compare(FString("IntersectionLinkConnectsAsStraightLaneAdapter"), ESearchCase::IgnoreCase) == 0)
	{
		// Included for completeness. Does not appear in data as a string.
		return EMassTrafficSpecialConnectionType::IntersectionLinkConnectsAsStraightLaneAdapter;
	}
	else
	{
		return EMassTrafficSpecialConnectionType::Unknown;
	}
}


void AMassTrafficBuilderBaseActor::AddIntersection(FString IntersectionID, FString ParentIntersectionID, bool bCanSupportLongVehicles, bool bIsFreeway, bool bIsMainPartOfFreeway, bool bIsFreewayOnramp, bool bIsFreewayOfframp, EMassTrafficUser User, bool bIsCrosswalk)
{
	const FMassTrafficIntersectionMapKey IntersectionMapKey{IntersectionID, User};
	if (IntersectionsMap.Contains(IntersectionMapKey)) return;

	FMassTrafficIntersection Intersection;
	{
		Intersection.IntersectionID = IntersectionID;
		Intersection.ParentIntersectionID = ParentIntersectionID;
		Intersection.User = User;
		Intersection.IntersectionLinks.Empty();
		Intersection.DebugColor = MakeDebugColorFromID(IntersectionID, IntersectionDebugColorTint);   
		Intersection.bIsCenterPointValid = false;
		Intersection.CenterPoint = FVector(0.0f, 0.0f, 0.0f);
		Intersection.bIsCrosswalk = bIsCrosswalk;
		Intersection.bCanSupportLongVehicles = bCanSupportLongVehicles;
		Intersection.bIsFreeway = bIsFreeway;
		Intersection.bIsFreewayOnramp = bIsFreewayOnramp;
		Intersection.bIsFreewayOfframp = bIsFreewayOfframp;
		Intersection.bIsMainPartOfFreeway = bIsMainPartOfFreeway;
	}

	IntersectionsMap.Add(IntersectionMapKey, Intersection);
}


void AMassTrafficBuilderBaseActor::AddIntersectionLink(FString IntersectionID, int IntersectionSequenceNumber, FMassTrafficPoint Point, FString ConnectedIntersectionID, int ConnectedIntersectionSequenceNumber, int NumberOfLanes, bool bHasCenterDivider, float LaneWidthCM, float CenterDividerWidthCM, bool bIsUnidirectional, bool bHasTrafficLight, FVector TrafficLightPosition, EMassTrafficSpecialConnectionType SpecialConnectionType, int32 UserDensity, EMassTrafficUser User)
{
	const FMassTrafficIntersectionMapKey IntersectionMapKey{IntersectionID, User};
	if (!IntersectionsMap.Contains(IntersectionMapKey)) return;

	FMassTrafficIntersection& Intersection = IntersectionsMap[IntersectionMapKey];

	TArray<FMassTrafficIntersectionLink>& IntersectionLinks = Intersection.IntersectionLinks;
	if (IntersectionLinks.Num() < IntersectionSequenceNumber + 1)
	{
		IntersectionLinks.SetNum(IntersectionSequenceNumber + 1);
	}

	FMassTrafficIntersectionLink& IntersectionLink = IntersectionLinks[IntersectionSequenceNumber];
	{
		IntersectionLink.SpecialConnectionType = SpecialConnectionType;
		IntersectionLink.IntersectionID = IntersectionID;
		IntersectionLink.IntersectionSequenceNumber = IntersectionSequenceNumber;
		IntersectionLink.bIsValid = true;
		IntersectionLink.User = User;
		IntersectionLink.Point = Point;
		IntersectionLink.ConnectedIntersectionID = ConnectedIntersectionID;
		IntersectionLink.ConnectedIntersectionSequenceNumber = ConnectedIntersectionSequenceNumber;
		IntersectionLink.NumberOfLanes = NumberOfLanes;
		IntersectionLink.bHasCenterDivider = bHasCenterDivider;
		IntersectionLink.LaneWidthCM = LaneWidthCM;
		IntersectionLink.CenterDividerWidthCM = CenterDividerWidthCM;
		IntersectionLink.bIsUnidirectional = bIsUnidirectional;
		IntersectionLink.bHasTrafficLight = bHasTrafficLight;
		IntersectionLink.TrafficLightPosition = TrafficLightPosition;
		IntersectionLink.UserDensity = UserDensity;
	}	

	AddPointHints(Point.Position, false, false, false, false, true, false, "", "", IntersectionID);
}


void AMassTrafficBuilderBaseActor::ClearLanesFromIntersectionLink(FString IntersectionID, EMassTrafficUser User, int IntersectionSequenceNumber)
{
	FMassTrafficIntersectionMapKey IntersectionMapKey;
	{
		IntersectionMapKey.User = User;
		IntersectionMapKey.IntersectionID = IntersectionID;
	}
	
	if (!IntersectionsMap.Contains(IntersectionMapKey))
	{
		return;
	}
	
	FMassTrafficIntersection* Intersection = IntersectionsMap.Find(IntersectionMapKey);
	for (FMassTrafficIntersectionLink& IntersectionLink : Intersection->IntersectionLinks)
	{
		if (IntersectionLink.IntersectionSequenceNumber == IntersectionSequenceNumber)
		{
			IntersectionLink.NumberOfLanes = 0;
		}
	}
}


void AMassTrafficBuilderBaseActor::AddIntersectionCenter(FString IntersectionID, FVector Point, EMassTrafficUser User)
{
	const FMassTrafficIntersectionMapKey IntersectionMapKey{IntersectionID, User};
	if (!IntersectionsMap.Contains(IntersectionMapKey))
	{
		return;
	}

	FMassTrafficIntersection& Intersection = IntersectionsMap[IntersectionMapKey];
	Intersection.CenterPoint = Point;
	Intersection.bIsCenterPointValid = true;

	AddPointHints(Point, false, false, false, false, false, true, "", "", IntersectionID);
}


bool AMassTrafficBuilderBaseActor::AddIntersectionLinkForwardAndUpVectors(FString IntersectionID, int IntersectionSequenceNumber, FVector ForwardVector, FVector UpVector, EMassTrafficUser User)
{
	const FMassTrafficIntersectionMapKey IntersectionMapKey{IntersectionID, User};
	if (!IntersectionsMap.Contains(IntersectionMapKey)) return false;

	FMassTrafficIntersection& Intersection = IntersectionsMap[IntersectionMapKey];

	TArray<FMassTrafficIntersectionLink>& IntersectionLinks = Intersection.IntersectionLinks;
	if (IntersectionSequenceNumber >= IntersectionLinks.Num()) return false;

	FMassTrafficIntersectionLink& IntersectionLink = IntersectionLinks[IntersectionSequenceNumber];

	IntersectionLink.Point.ForwardVector = ForwardVector;
	IntersectionLink.Point.UpVector = UpVector;

	return true;
}


int AMassTrafficBuilderBaseActor::SegmentCrossesRoadEnteringOrLeavingIntersectionSide(FString IntersectionID, EMassTrafficUser User, FVector SegmentPointA, FVector SegmentPointB) const
{
	FMassTrafficIntersectionMapKey IntersectionMapKey;
	{
		IntersectionMapKey.User = User;
		IntersectionMapKey.IntersectionID = IntersectionID;
	}
	
	if (!IntersectionsMap.Contains(IntersectionMapKey))
	{
		return -1;
	}
	
	const FMassTrafficIntersection* Intersection = IntersectionsMap.Find(IntersectionMapKey);
	for (const FMassTrafficIntersectionLink& IntersectionLink : Intersection->IntersectionLinks)
	{
		const FVector From_IntersectionCenter_To_SideMidpoint = (IntersectionLink.Point.Position - Intersection->CenterPoint).GetSafeNormal();
		const FVector From_IntersectionCenter_To_SegmentPointA = (IntersectionLink.Point.Position - SegmentPointA).GetSafeNormal();
		const FVector From_IntersectionCenter_To_SegmentPointB = (IntersectionLink.Point.Position - SegmentPointB).GetSafeNormal();
		
		const FVector CrossA = FVector::CrossProduct(From_IntersectionCenter_To_SideMidpoint, From_IntersectionCenter_To_SegmentPointA);
		const FVector CrossB = FVector::CrossProduct(From_IntersectionCenter_To_SideMidpoint, From_IntersectionCenter_To_SegmentPointB);
		if (CrossA.Z * CrossB.Z < 0.0f)
		{
			return IntersectionLink.IntersectionSequenceNumber;
		}
	}

	return -1;
}


bool AMassTrafficBuilderBaseActor::CompareNumberOfLanesOnIntersectionLinks(FString IntersectionID1, EMassTrafficUser User1, int IntersectionSequenceNumber1, FString IntersectionID2, EMassTrafficUser User2, int IntersectionSequenceNumber2) const
{
	const FMassTrafficIntersectionMapKey IntersectionMapKey1{IntersectionID1, User1};
	if (!IntersectionsMap.Contains(IntersectionMapKey1)) return false;
	
	const FMassTrafficIntersectionMapKey IntersectionMapKey2{IntersectionID2, User2};
	if (!IntersectionsMap.Contains(IntersectionMapKey2)) return false;

	
	const FMassTrafficIntersection& Intersection1 = IntersectionsMap[IntersectionMapKey1];	
	const FMassTrafficIntersection& Intersection2 = IntersectionsMap[IntersectionMapKey2];

	return (Intersection1.IntersectionLinks[IntersectionSequenceNumber1].NumberOfLanes == Intersection2.IntersectionLinks[IntersectionSequenceNumber2].NumberOfLanes);	
}


bool AMassTrafficBuilderBaseActor::CompareLaneWidthsOnIntersectionLinks(FString IntersectionID1, EMassTrafficUser User1, int IntersectionSequenceNumber1, FString IntersectionID2, EMassTrafficUser User2, int IntersectionSequenceNumber2) const
{
	const FMassTrafficIntersectionMapKey IntersectionMapKey1{IntersectionID1, User1};
	if (!IntersectionsMap.Contains(IntersectionMapKey1)) return false;
	
	const FMassTrafficIntersectionMapKey IntersectionMapKey2{IntersectionID2, User2};
	if (!IntersectionsMap.Contains(IntersectionMapKey2)) return false;


	const FMassTrafficIntersection& Intersection1 = IntersectionsMap[IntersectionMapKey1];	
	const FMassTrafficIntersection& Intersection2 = IntersectionsMap[IntersectionMapKey2];

	return (Intersection1.IntersectionLinks[IntersectionSequenceNumber1].LaneWidthCM == Intersection2.IntersectionLinks[IntersectionSequenceNumber2].LaneWidthCM);		
}


/**
 * Point Hints
 */

void AMassTrafficBuilderBaseActor::AddPointHints(FVector Point, bool bIsRoadSegmentPoint, bool bIsRoadSegmentStartPoint, bool bIsRoadSegmentEndPoint, bool bIsRoadSplinePoint, bool bIsIntersectionLinkPoint, bool bIsIntersectionCenterPoint, FString RoadSegmentID, FString RoadSplineID, FString IntersectionID)
{
	const FString PointHintsID = VectorToMapKey(Point);

	FMassTrafficPointHints PointHints; 
	if (PointHintsMap.Contains(PointHintsID))
	{
		PointHints = PointHintsMap[PointHintsID];
	}

	if (!RoadSegmentID.IsEmpty())
	{
		PointHints.RoadSegmentIDs.Add(RoadSegmentID);
	}
	
	if (!RoadSplineID.IsEmpty())
	{
		PointHints.RoadSplineIDs.Add(RoadSplineID);
	}
	
	if (!IntersectionID.IsEmpty())
	{
		PointHints.IntersectionIDs.Add(IntersectionID);
	}

	PointHints.bIsRoadSegmentPoint |= bIsRoadSegmentPoint;
	PointHints.bIsRoadSegmentStartPoint |= bIsRoadSegmentStartPoint;
	PointHints.bIsRoadSegmentEndPoint |= bIsRoadSegmentEndPoint;
	PointHints.bIsRoadSplinePoint |= bIsRoadSplinePoint;
	PointHints.bIsIntersectionLinkPoint |= bIsIntersectionLinkPoint;
	PointHints.bIsIntersectionCenterPoint |= bIsIntersectionCenterPoint;

	PointHints.bIsValid = true;


	PointHintsMap.Add(PointHintsID, PointHints);
}


bool AMassTrafficBuilderBaseActor::GetPointHints(FVector Point, FMassTrafficPointHints& PointHints)
{
	const FString PointHintsID = VectorToMapKey(Point);
	if (PointHintsMap.Contains(PointHintsID))
	{
		PointHints = *PointHintsMap.Find(PointHintsID);
		return true;
	}
	else
	{
		PointHints.bIsValid = false;
		return false;
	}
}


/** 
 * RuleProcessor
 */

UPointCloudView* AMassTrafficBuilderBaseActor::GetRuleProcessorPoints(UPointCloud* PointCloud, TArray<FTransform>& Transforms, TArray<int32>& IDs, bool& bIsValid) 
{
	bIsValid = false;
	Transforms.Empty();
	IDs.Empty();

	if (!PointCloud)
	{
		UE_LOG(LogMassTrafficEditor, Error, TEXT("%s - Rule Processor Point Cloud is null"), ANSI_TO_TCHAR(__FUNCTION__));
		return nullptr;
	}

	UPointCloudView* PointCloudView = PointCloud->MakeView();
	if (!PointCloudView) 
	{
		UE_LOG(LogMassTrafficEditor, Error, TEXT("%s - Rule Processor Point Cloud is valid, but could not create Point Cloud View"), ANSI_TO_TCHAR(__FUNCTION__));
		return nullptr;
	}

	PointCloudView->GetTransformsAndIds(Transforms, IDs);
	bIsValid = true;

	return PointCloudView;
}


/**
 * Zone Graph
 */

UZoneShapeComponent* AMassTrafficBuilderBaseActor::BuildZoneShapeAsRoadSegment(FName Label, const FMassTrafficRoadSegment& RoadSegment, FZoneLaneProfileRef ZoneLaneProfileRef, bool bDoReverseZoneLaneProfile, FZoneGraphTagMask ZoneGraphTags)
{
	// Get or spawn Zone Shape actor, and create a Zone Shape component in it.

	AActor* Actor = nullptr;
	UZoneShapeComponent* ZoneShapeComponent = AddZoneShape(Label, Actor);
	if (!ZoneShapeComponent) return nullptr;

	
	// Set Zone Shape Type.
	ZoneShapeComponent->SetShapeType(FZoneShapeType::Spline);


	// Set top-level Zone Lane Profile.
	ZoneShapeComponent->SetCommonLaneProfile(ZoneLaneProfileRef);


	// Clear the per-point Zone Lane Profiles.
	// Not needed for spline types.
	ZoneShapeComponent->ClearPerPointLaneProfiles();


	// Set transform. 
	// NOTE - 
	// We could just set the points in world space, and leave the transform as is.
	// But having a meaningful transform helps.
	// For road segments - it's very helpful to have the forward direction of the transform pointing
	// in the direction of the 'Forward' lanes.
	{
		const FVector Translation(RoadSegment.StartPoint.Position);
		
		const FVector XAxis((RoadSegment.EndPoint.Position - RoadSegment.StartPoint.Position).GetSafeNormal());
		static const FVector ZAxis(0.0f, 0.0f, 1.0f);
		const FRotator Rotator = UKismetMathLibrary::MakeRotFromZX(ZAxis, XAxis);

		const FVector Scale3D(1.0f, 1.0f, 1.0f);

		const FTransform Transform(Rotator, Translation, Scale3D);

		ZoneShapeComponent->SetWorldTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	}


	// Set points.
	{
		TArray<FZoneShapePoint>& ZoneShapePoints = ZoneShapeComponent->GetMutablePoints();
		ZoneShapePoints.Empty();


		// Start position at zero. Transform will make it right.
		static const FVector LocalStartPosition(0.0f, 0.0f, 0.0f);

		FZoneShapePoint ZoneShapeLocalStartPoint;
		ZoneShapeLocalStartPoint.Position = LocalStartPosition;
		ZoneShapeLocalStartPoint.SetRotationFromForwardAndUp(RoadSegment.StartPoint.ForwardVector, RoadSegment.StartPoint.UpVector);
		ZoneShapeLocalStartPoint.Type = FZoneShapePointType::Sharp;
		ZoneShapeLocalStartPoint.LaneProfile = FZoneShapePoint::InheritLaneProfile;
		ZoneShapeLocalStartPoint.bReverseLaneProfile = bDoReverseZoneLaneProfile;
		ZoneShapeLocalStartPoint.SetLaneConnectionRestrictions(
			(RoadSegment.StartPoint.bLanesMergeToOneDestination ? EZoneShapeLaneConnectionRestrictions::MergeLanesToOneDestinationLane : EZoneShapeLaneConnectionRestrictions::None) |
			(RoadSegment.StartPoint.bLanesConnectWithOneLanePerDestination ? EZoneShapeLaneConnectionRestrictions::OneLanePerDestination : EZoneShapeLaneConnectionRestrictions::None) |
			(RoadSegment.StartPoint.bLanesConnectWithNoLeftTurn ? EZoneShapeLaneConnectionRestrictions::NoLeftTurn : EZoneShapeLaneConnectionRestrictions::None) |
			(RoadSegment.StartPoint.bLanesConnectWithNoRightTurn ? EZoneShapeLaneConnectionRestrictions::NoRightTurn : EZoneShapeLaneConnectionRestrictions::None)
			);

		ZoneShapePoints.Add(ZoneShapeLocalStartPoint);


		// End position only on X. Transform will make it right.
		const FVector LocalEndPosition((RoadSegment.EndPoint.Position - RoadSegment.StartPoint.Position).Size(), 0.0f, 0.0f); 

		FZoneShapePoint ZoneShapeLocalEndPoint;
		ZoneShapeLocalEndPoint.Position = LocalEndPosition;
		ZoneShapeLocalStartPoint.SetRotationFromForwardAndUp(RoadSegment.EndPoint.ForwardVector, RoadSegment.EndPoint.UpVector);
		ZoneShapeLocalEndPoint.Type = FZoneShapePointType::Sharp;
		ZoneShapeLocalEndPoint.LaneProfile = FZoneShapePoint::InheritLaneProfile;
		ZoneShapeLocalEndPoint.bReverseLaneProfile = bDoReverseZoneLaneProfile;
		ZoneShapeLocalStartPoint.SetLaneConnectionRestrictions(
			(RoadSegment.EndPoint.bLanesMergeToOneDestination ? EZoneShapeLaneConnectionRestrictions::MergeLanesToOneDestinationLane : EZoneShapeLaneConnectionRestrictions::None) |
			(RoadSegment.EndPoint.bLanesConnectWithOneLanePerDestination ? EZoneShapeLaneConnectionRestrictions::OneLanePerDestination : EZoneShapeLaneConnectionRestrictions::None) |
			(RoadSegment.EndPoint.bLanesConnectWithNoLeftTurn ? EZoneShapeLaneConnectionRestrictions::NoLeftTurn : EZoneShapeLaneConnectionRestrictions::None) |
			(RoadSegment.EndPoint.bLanesConnectWithNoRightTurn ? EZoneShapeLaneConnectionRestrictions::NoRightTurn : EZoneShapeLaneConnectionRestrictions::None)
			);

		ZoneShapePoints.Add(ZoneShapeLocalEndPoint);
	}

	// Add Zone Graph tags.
	{
		AddUserTagToZoneGraphTagMask(RoadSegment.User, ZoneGraphTags);
		ZoneShapeComponent->SetTags(ZoneGraphTags);
	}
	

	// Update shape.
	ZoneShapeComponent->UpdateShape();

	// Necessary (to make component appear in editor.)
	if (BuildType == EMassTrafficBuildType::Components)
	{
		ZoneShapeComponent->RegisterComponent();
	}

	// TODO: Without this, some Zone Shapes don't link lanes with other Zone Shapes - when built as actors.
	UZoneGraphSubsystem* ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(GetWorld());
	ZoneGraphSubsystem->GetBuilder().OnZoneShapeComponentChanged(*ZoneShapeComponent);

	
	return ZoneShapeComponent;
}


UZoneShapeComponent* AMassTrafficBuilderBaseActor::BuildZoneShapeAsRoadSpline(FName Label, const FMassTrafficRoadSpline& RoadSpline, FZoneLaneProfileRef ZoneLaneProfileRef, bool bDoReverseZoneLaneProfile, FZoneGraphTagMask ZoneGraphTags) 
{
	if (RoadSpline.Points.Num() < 2)
	{
		UE_LOG(LogMassTrafficEditor, Error, TEXT("%s - RoadSpline '%s' has %d<2 points."), ANSI_TO_TCHAR(__FUNCTION__), *RoadSpline.RoadSplineID, RoadSpline.Points.Num());
		return nullptr;
	}

	// Get or spawn Zone Shape actor, and create a Zone Shape component in it.
	AActor* Actor = nullptr;
	UZoneShapeComponent* ZoneShapeComponent = AddZoneShape(Label, Actor);
	if (!ZoneShapeComponent) return nullptr;

	// Set Zone Shape Type.
	ZoneShapeComponent->SetShapeType(FZoneShapeType::Spline);

	// Set top-level Zone Lane Profile.
	ZoneShapeComponent->SetCommonLaneProfile(ZoneLaneProfileRef);

	// Clear the per-point Zone Lane Profiles.
	// Not needed for spline types.
	ZoneShapeComponent->ClearPerPointLaneProfiles();

	// Set transform. 
	// NOTE - 
	// We could just set the points in world space, and leave the transform as is.
	// But having a meaningful transform helps.
	{
		const FVector Translation(RoadSpline.Points[0].Position);
		const FRotator Rotator(0.0f, 0.0f, 0.0f); // Rotation is not very relevant for intersections.
		const FVector Scale3D(1.0f, 1.0f, 1.0f);
		const FTransform Transform(Rotator, Translation, Scale3D);

		ZoneShapeComponent->SetWorldTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	}

	// Set points.
	{
		TArray<FZoneShapePoint>& ZoneShapePoints = ZoneShapeComponent->GetMutablePoints();
		ZoneShapePoints.Empty();

		for (int P = 0; P < RoadSpline.Points.Num(); P++)
		{
			const FVector LocalPosition = RoadSpline.Points[P].Position - RoadSpline.Points[0].Position;
			const FVector& OptionalTangentVector = RoadSpline.Points[P].OptionalTangentVector;
			const FVector& UpVector = RoadSpline.Points[P].UpVector;

			FZoneShapePoint ZoneShapeLocalPoint;
			ZoneShapeLocalPoint.Position = LocalPosition;
			ZoneShapeLocalPoint.LaneProfile = FZoneShapePoint::InheritLaneProfile;          
			ZoneShapeLocalPoint.bReverseLaneProfile = bDoReverseZoneLaneProfile;
			ZoneShapeLocalPoint.SetLaneConnectionRestrictions( 
				(RoadSpline.Points[P].bLanesMergeToOneDestination ? EZoneShapeLaneConnectionRestrictions::MergeLanesToOneDestinationLane : EZoneShapeLaneConnectionRestrictions::None) |
				(RoadSpline.Points[P].bLanesConnectWithOneLanePerDestination ? EZoneShapeLaneConnectionRestrictions::OneLanePerDestination : EZoneShapeLaneConnectionRestrictions::None) |
				(RoadSpline.Points[P].bLanesConnectWithNoLeftTurn ? EZoneShapeLaneConnectionRestrictions::NoLeftTurn : EZoneShapeLaneConnectionRestrictions::None) |
				(RoadSpline.Points[P].bLanesConnectWithNoRightTurn ? EZoneShapeLaneConnectionRestrictions::NoRightTurn : EZoneShapeLaneConnectionRestrictions::None)
				);

			if (OptionalTangentVector.IsNearlyZero()) // this point doesn't need a particular tangent vector
			{
				ZoneShapeLocalPoint.Type = FZoneShapePointType::AutoBezier;

				ZoneShapeLocalPoint.SetRotationFromForwardAndUp(RoadSpline.Points[P].ForwardVector, RoadSpline.Points[P].UpVector);
			}
			else // this point needs a particular tangent vector
			{
				ZoneShapeLocalPoint.Type = FZoneShapePointType::Bezier;
			
				// Set Zone Shape Point's rotation pitch/yaw from tangent vector rotation. 
				// ** Code modified from UZoneShapeComponent::PostLoad() **
				// FVector::Rotation() only gives a pitch and yaw - and roll is 0.
				const FVector BezierTangentVector = OptionalTangentVector * 0.5f / 3.0f; // for Bezier basis
				const FRotator BezierTangentPitchYawRotation = BezierTangentVector.Rotation();
				ZoneShapeLocalPoint.Rotation.Pitch = BezierTangentPitchYawRotation.Pitch;
				ZoneShapeLocalPoint.Rotation.Yaw = BezierTangentPitchYawRotation.Yaw;

				// Set Zone Shape Point's rotation roll from up vector.
				// ** Code modified from FZoneShapePoint::SetRotationFromForwardAndUp() **
				// I think we need to also set roll, since it was not set by the above step.
				const FVector LocalUpVector = BezierTangentPitchYawRotation.Quaternion().UnrotateVector(UpVector);
				ZoneShapeLocalPoint.Rotation.Roll = FMath::RadiansToDegrees(FMath::Atan2(LocalUpVector.Y, LocalUpVector.Z));            

				// Set Zone Shape Point's tangent length.
				ZoneShapeLocalPoint.TangentLength = BezierTangentVector.Size();
			}

			ZoneShapePoints.Add(ZoneShapeLocalPoint);
		}
	}

	// Add Zone Graph tags.
	{
		AddUserTagToZoneGraphTagMask(RoadSpline.User, ZoneGraphTags);
		ZoneShapeComponent->SetTags(ZoneGraphTags);
	}

	// Update shape.
	ZoneShapeComponent->UpdateShape();

	// Necessary (to make component appear in editor.)
	if (BuildType == EMassTrafficBuildType::Components)
	{
		ZoneShapeComponent->RegisterComponent();
	}

	// Without this, some Zone Shapes don't link lanes with other Zone Shapes - when built as actors.
	UZoneGraphSubsystem* ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(GetWorld());
	ZoneGraphSubsystem->GetBuilder().OnZoneShapeComponentChanged(*ZoneShapeComponent);

	return ZoneShapeComponent;
}


UZoneShapeComponent* AMassTrafficBuilderBaseActor::BuildZoneShapeAsIntersection(FName Label, const FMassTrafficIntersection& Intersection, TArray<FZoneLaneProfileRef> PerPointZoneLaneProfileRefs, TArray<bool> DoReverseZoneLaneProfiles, bool bAutomaticallySetConnectionRestrictionsWithSpecialConnections, bool bUseArcsForLanes, FZoneGraphTagMask ZoneGraphTags) 
{
	if (Intersection.IntersectionLinks.Num() < 2)
	{
		UE_LOG(LogMassTrafficEditor, Error, TEXT("%s - Intersections.IntersectionLinks.Num:%d < 2 - Impossible intersection."), ANSI_TO_TCHAR(__FUNCTION__), Intersection.IntersectionLinks.Num());
		return nullptr;
	}

	if (PerPointZoneLaneProfileRefs.Num() > 0 && Intersection.IntersectionLinks.Num() != PerPointZoneLaneProfileRefs.Num())
	{
		UE_LOG(LogMassTrafficEditor, Error, TEXT("%s - Intersections.IntersectionLinks.Num:%d != PerPointZoneLaneProfileRefs.Num:%d"), ANSI_TO_TCHAR(__FUNCTION__), Intersection.IntersectionLinks.Num(), PerPointZoneLaneProfileRefs.Num());
		return nullptr;
	}

	if (DoReverseZoneLaneProfiles.Num() > 0 && Intersection.IntersectionLinks.Num() != DoReverseZoneLaneProfiles.Num())
	{
		UE_LOG(LogMassTrafficEditor, Error, TEXT("%s - Intersections.IntersectionLinks.Num:%d != DoReverseZoneLaneProfiles.Num:%d"), ANSI_TO_TCHAR(__FUNCTION__), Intersection.IntersectionLinks.Num(), DoReverseZoneLaneProfiles.Num());
		return nullptr;
	}

	// Get or spawn Zone Shape actor, and create a Zone Shape component in it.
	AActor* Actor = nullptr;
	UZoneShapeComponent* ZoneShapeComponent = AddZoneShape(Label, Actor);
	if (!ZoneShapeComponent) return nullptr;

	// Set Zone Shape Type.
	ZoneShapeComponent->SetShapeType(FZoneShapeType::Polygon);

	// Set top-level Lane Profile to be the first one available.
	if (PerPointZoneLaneProfileRefs.Num() > 0)
	{
		ZoneShapeComponent->SetCommonLaneProfile(PerPointZoneLaneProfileRefs[0]);
	}

	// Set the per-point Zone Lane Profiles.
	FPointZoneLaneProfileIndex_to_UniquePerPointLaneProfileIndex PointZoneLaneProfileIndex_to_UniquePerPointLaneProfileIndex;
	if (PerPointZoneLaneProfileRefs.Num() > 0)
	{
		ZoneShapeComponent->ClearPerPointLaneProfiles();

		for (const FZoneLaneProfileRef& PointZoneLaneProfileRef : PerPointZoneLaneProfileRefs)
		{
			const uint32 UniquePerPointLaneProfileIndex = ZoneShapeComponent->AddUniquePerPointLaneProfile(PointZoneLaneProfileRef);
			PointZoneLaneProfileIndex_to_UniquePerPointLaneProfileIndex.Add(UniquePerPointLaneProfileIndex);
		}
	}

	// Set transform. 
	// NOTE - 
	// We could just set the points in world space, and leave the transform as is.
	// But having a meaningful transform helps.
	FVector AveragePosition(0.0f, 0.0f, 0.0f);
	{
		float TotalWeight = 0.0f;
		for (const FMassTrafficIntersectionLink& IntersectionLink : Intersection.IntersectionLinks)
		{
			AveragePosition += IntersectionLink.Point.Position;
			TotalWeight += 1.0f;
		}
		AveragePosition /= TotalWeight; // Won't have a divide by zero. See checks at top of method.

		const FVector Translation(AveragePosition);
		const FRotator Rotator(0.0f, 0.0f, 0.0f); // Rotation is not very relevant for intersections.
		const FVector Scale3D(1.0f, 1.0f, 1.0f);
		const FTransform Transform(Rotator, Translation, Scale3D);

		ZoneShapeComponent->SetWorldTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	}

	bool bUseArcRouting = bUseArcsForLanes && !Intersection.bIsFreeway; 
	if (bUseArcRouting)
	{
		ZoneShapeComponent->SetPolygonRoutingType(EZoneShapePolygonRoutingType::Arcs);
	}

	// If needed, find out about any freeway connections.
	bool bIntersectionHasOnRamp = false;
	bool bIntersectionHasOffRamp = false;
	{
		if (bAutomaticallySetConnectionRestrictionsWithSpecialConnections)
		{
			if (Intersection.bIsFreeway)
			{
				for (const FMassTrafficIntersectionLink& IntersectionLink : Intersection.IntersectionLinks)
				{
					if (IntersectionLink.SpecialConnectionType == EMassTrafficSpecialConnectionType::CityIntersectionLinkConnectsToIncomingFreewayRamp ||
						IntersectionLink.SpecialConnectionType == EMassTrafficSpecialConnectionType::FreewayIntersectionLinkConnectsToIncomingFreewayRamp)
					{
						bIntersectionHasOnRamp = true;
					}
					else if (IntersectionLink.SpecialConnectionType == EMassTrafficSpecialConnectionType::CityIntersectionLinkConnectsToOutgoingFreewayRamp ||
							 IntersectionLink.SpecialConnectionType == EMassTrafficSpecialConnectionType::FreewayIntersectionLinkConnectsToOutgoingFreewayRamp)
					{
						bIntersectionHasOffRamp = true;
					}
				}
			}
		}
	}

	// Set points.
	{
		TArray<FZoneShapePoint>& ZoneShapePoints = ZoneShapeComponent->GetMutablePoints();
		ZoneShapePoints.Empty();
		int Count = 0;
		for (const FMassTrafficIntersectionLink& IntersectionLink : Intersection.IntersectionLinks)
		{
			const FVector LocalPosition = IntersectionLink.Point.Position - AveragePosition;

			FZoneShapePoint ZoneShapePoint;
			ZoneShapePoint.Position = LocalPosition;
			ZoneShapePoint.SetRotationFromForwardAndUp(IntersectionLink.Point.ForwardVector, IntersectionLink.Point.UpVector);        	
			ZoneShapePoint.Type = FZoneShapePointType::LaneProfile;

			EZoneShapeLaneConnectionRestrictions LaneConnectionRestrictions = EZoneShapeLaneConnectionRestrictions::None;
			{
				if (bAutomaticallySetConnectionRestrictionsWithSpecialConnections)
				{
					if (Intersection.bIsFreeway)
					{
						if (bIntersectionHasOnRamp && bIntersectionHasOffRamp)
						{
							// This freeway intersection has an on-ramp and an off-ramp - and this intersection link has incoming
							// lanes. We don't care if it's marked as being a special connection type. All intersection
							// links need to have one-lane-per-destination set - regardless of whether they're marked as
							// being a special connection.
							if (FVector::DotProduct(IntersectionLink.Point.TrafficForwardVector, IntersectionLink.Point.ForwardVector) > 0.0f)
							{
								LaneConnectionRestrictions = EZoneShapeLaneConnectionRestrictions::OneLanePerDestination;
							}
						}
						else if (bIntersectionHasOnRamp && !bIntersectionHasOffRamp)
						{
							// This freeway intersection has an on-ramp but no off-ramp - and this intersection link is marked as
							// being an incoming freeway ramp. This one link needs to have merge-lanes-to-one-destination set.
							if (IntersectionLink.SpecialConnectionType == EMassTrafficSpecialConnectionType::FreewayIntersectionLinkConnectsToIncomingFreewayRamp)
							{
								LaneConnectionRestrictions = EZoneShapeLaneConnectionRestrictions::MergeLanesToOneDestinationLane;							
							}
						}
						else if (IntersectionLink.SpecialConnectionType == EMassTrafficSpecialConnectionType::IntersectionLinkConnectsAsStraightLaneAdapter)
						{
							LaneConnectionRestrictions = (EZoneShapeLaneConnectionRestrictions::NoLeftTurn | EZoneShapeLaneConnectionRestrictions::NoRightTurn);													
						}
					}
				}
				else
				{
					LaneConnectionRestrictions =  
						(IntersectionLink.Point.bLanesMergeToOneDestination ? EZoneShapeLaneConnectionRestrictions::MergeLanesToOneDestinationLane : EZoneShapeLaneConnectionRestrictions::None) |
						(IntersectionLink.Point.bLanesConnectWithOneLanePerDestination ? EZoneShapeLaneConnectionRestrictions::OneLanePerDestination : EZoneShapeLaneConnectionRestrictions::None) |
						(IntersectionLink.Point.bLanesConnectWithNoLeftTurn ? EZoneShapeLaneConnectionRestrictions::NoLeftTurn : EZoneShapeLaneConnectionRestrictions::None) |
						(IntersectionLink.Point.bLanesConnectWithNoRightTurn ? EZoneShapeLaneConnectionRestrictions::NoRightTurn : EZoneShapeLaneConnectionRestrictions::None);
				}
			}

			ZoneShapePoint.SetLaneConnectionRestrictions(LaneConnectionRestrictions);

			if (bUseArcRouting)
			{
				ZoneShapePoint.InnerTurnRadius = 500.0f;
			}

			if (PerPointZoneLaneProfileRefs.Num() > 0)
			{
				ZoneShapePoint.LaneProfile = PointZoneLaneProfileIndex_to_UniquePerPointLaneProfileIndex[Count];
			}
			if (DoReverseZoneLaneProfiles.Num() > 0)
			{
				ZoneShapePoint.bReverseLaneProfile = DoReverseZoneLaneProfiles[Count];
			}
			
			ZoneShapePoints.Add(ZoneShapePoint);

			++Count;
		}
	}


	// Add Zone Graph tags.
	{
		AddUserTagToZoneGraphTagMask(Intersection.User, ZoneGraphTags);
		ZoneShapeComponent->SetTags(ZoneGraphTags);
	}

	// Update shape.
	ZoneShapeComponent->UpdateShape();

	// Necessary (to make component appear in editor.)
	if (BuildType == EMassTrafficBuildType::Components)
	{
		ZoneShapeComponent->RegisterComponent();
	}

	// TODO: Without this, some Zone Shapes don't link lanes with other Zone Shapes - when built as actors.
	UZoneGraphSubsystem* ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(GetWorld());
	ZoneGraphSubsystem->GetBuilder().OnZoneShapeComponentChanged(*ZoneShapeComponent);

	return ZoneShapeComponent;
}


void AMassTrafficBuilderBaseActor::ClearAll() 
{
	ClearDebug();

	// Delete any created Zone Shape actors under the Zone Shape parent actor.
	{
		TArray<AActor*> Actors;
		UGameplayStatics::GetAllActorsWithTag(GetWorld(), TrafficBuilder_CreatedZoneShapeActorTagName, Actors);
		for (AActor* Actor : Actors)
		{
			if (Actor->GetAttachParentActor() == GetZoneShapeParentActor())
			{
				Actor->K2_DestroyActor();
			}
		}
	}

	// Delete any created Zone Shape components inside the Zone Shape parent actor.
	{
		AActor* Actor = GetZoneShapeParentActor();

		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			UZoneShapeComponent* ZoneShapeComponent = Cast<UZoneShapeComponent>(Component);
			if (ZoneShapeComponent)
			{
				ZoneShapeComponent->DestroyComponent();
			}
		}
	}

	PointHintsMap.Empty();
	RoadSegmentsMap.Empty();
	RoadSplinesMap.Empty();
	IntersectionsMap.Empty();
}


AActor* AMassTrafficBuilderBaseActor::GetZoneShapeParentActor() 
{
	return ZoneShapeParentActor == nullptr ? this : ZoneShapeParentActor;
}


UZoneShapeComponent* AMassTrafficBuilderBaseActor::AddZoneShape(FName Label, AActor*& Actor)
{
	// Get or spawn Zone Shape actor, and create a Zone Shape component in it.
	UZoneShapeComponent* ZoneShapeComponent = nullptr;
	if (BuildType == EMassTrafficBuildType::Components)
	{
		Actor = GetZoneShapeParentActor();

		ZoneShapeComponent = NewObject<UZoneShapeComponent>(Actor, UZoneShapeComponent::StaticClass(), Label);
		Actor->AddInstanceComponent(ZoneShapeComponent);
		ZoneShapeComponent->ComponentTags.Add(*TrafficBuilder_CreatedZoneShapeComponentTagName.ToString());
	}
	else if (BuildType == EMassTrafficBuildType::Actors)
	{
		UWorld* World = GetWorld();
		if (!World)	return nullptr;

		Actor = World->SpawnActor<AZoneShape>(FVector::ZeroVector, FRotator::ZeroRotator, DefaultActorSpawnParameters);
		Actor->SetActorLabel(Label.ToString(), true);
		Actor->Tags.Add(TrafficBuilder_CreatedZoneShapeActorTagName);
		Actor->AttachToActor(GetZoneShapeParentActor(), RelativeAttachmentTransformRules, /*socket*/ NAME_None);

		ZoneShapeComponent = Cast<UZoneShapeComponent>(Actor->GetComponentByClass(UZoneShapeComponent::StaticClass()));
	}


	return ZoneShapeComponent;
}


void AMassTrafficBuilderBaseActor::MarkAllCrosswalkRoadSegments()
{
	TArray<FMassTrafficRoadSegmentMapKey> RoadSegmentMapKeys;
	for (const auto& Elem : RoadSegmentsMap)
	{
		RoadSegmentMapKeys.Add(Elem.Key);
	}

	
	// Create a 3D hash grid - to store lane indices, at their mid point.
	FBasicHGrid PedestrianRoadSegmentMidpoint_HGrid(100.0f);
	{
		int32 HGridSize = 0;
		for (int I = 0; I < RoadSegmentMapKeys.Num(); I++)
		{
			const FMassTrafficRoadSegmentMapKey& RoadSegmentMapKey = RoadSegmentMapKeys[I];  
			FMassTrafficRoadSegment& RoadSegment = RoadSegmentsMap[RoadSegmentMapKey];
			
			if (RoadSegment.User != EMassTrafficUser::Pedestrian)
			{
				continue;
			}

			// Important. 
			RoadSegment.bIsCrosswalk = false;
			
			PedestrianRoadSegmentMidpoint_HGrid.Add(I, FBox::BuildAABB(RoadSegment.Midpoint(), FVector::ZeroVector));
			++HGridSize;
		}

		if (HGridSize == 0)
		{
			return;
		}
	}

	
	// Go through all vehicle intersections..
	for (const auto& IntersectionElem : IntersectionsMap)
	{
		const FMassTrafficIntersection& Intersection = IntersectionElem.Value;

		// Important.
		if (Intersection.User != EMassTrafficUser::Vehicle)
		{
			continue;
		}

		for (const FMassTrafficIntersectionLink& IntersectionLink : Intersection.IntersectionLinks)
		{
			const float SearchDistance = (Intersection.CenterPoint - IntersectionLink.Point.Position).Length();
			const FVector SearchExtent = FVector(SearchDistance);

			// (1) Remember, hash grid stores array indices for pedestrian road segment map keys, by road segment midpoint.
			// (2) Look for any of those that are close to the start point only - search distance will include end point too.
			TArray<int32/*lane index*/> QueryResults;
			PedestrianRoadSegmentMidpoint_HGrid.Query(FBox::BuildAABB(IntersectionLink.Point.Position, SearchExtent), QueryResults);

			for (int32 I : QueryResults)
			{
				const FMassTrafficRoadSegmentMapKey& RoadSegmentMapKey = RoadSegmentMapKeys[I];  
				FMassTrafficRoadSegment& RoadSegment = RoadSegmentsMap[RoadSegmentMapKey];

				// Important.
				if (RoadSegment.User != EMassTrafficUser::Pedestrian)
				{
					continue;
				}

				// It's already been identified as a crosswalk. Avoid costly check again.
				if (RoadSegment.bIsCrosswalk)
				{
					continue;
				}
				
				const bool bIsNear = UE::MassTraffic::PointIsNearSegment(
					IntersectionLink.Point.Position, 
					RoadSegment.StartPoint.Position, RoadSegment.EndPoint.Position,
					IntersectionSideToCrosswalkSearchDistance);
				
				RoadSegment.bIsCrosswalk |= bIsNear;
			}
		}
	}
}


void AMassTrafficBuilderBaseActor::AddUserTagToZoneGraphTagMask(const EMassTrafficUser& User, FZoneGraphTagMask& ZoneGraphTagMask) const
{
	if (User == EMassTrafficUser::Vehicle)
	{
		ZoneGraphTagMask.Add(ZoneGraphTagForVehicles);
	}
	else if (User == EMassTrafficUser::Pedestrian)
	{
		ZoneGraphTagMask.Add(ZoneGraphTagForPedestrians);
	}
}

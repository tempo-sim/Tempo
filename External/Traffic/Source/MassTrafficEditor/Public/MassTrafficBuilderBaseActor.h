// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "ZoneShapeComponent.h"
#include "ZoneShapeActor.h"
#include "PointCloudView.h"

#include "MassTrafficEditor.h"
#include "MassTrafficBuilderTypes.h"
#include "MassTrafficEditorBaseActor.h"

#include "MassTrafficBuilderBaseActor.generated.h"


UCLASS(BlueprintType)
class MASSTRAFFICEDITOR_API AMassTrafficBuilderBaseActor : public AMassTrafficEditorBaseActor
{
	GENERATED_BODY()

public:
	AMassTrafficBuilderBaseActor();

private:
	/**  Actor tag name that debug actors that this creates. */
	static const FName TrafficBuilder_CreatedDebugActorTagName; 

	/**  Actor tag name for Zone Shape actors that contain components that this creates. */
	static const FName TrafficBuilder_CreatedZoneShapeActorTagName; 	

	/**  Component tag name for Zone Shape components that this creates. */
	static const FName TrafficBuilder_CreatedZoneShapeComponentTagName; 	

public:
	/**  The transform used in converting from Houdini to Unreal coordinate spaces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Math")
	FTransform HoudiniToUEConversionTransform;
 
	/** Form a proper right-vector from another vector. DEPRECATED */
	UFUNCTION(BlueprintPure, Category = "Mass Traffic|Mass Traffic Builder|Math")
	FVector FlatVectorToFlatRightVector(FVector Vector) const;

	/** Converts a position from Houdini coordinate space to Unreal coordinate space. */
	UFUNCTION(BlueprintPure, Category = "Mass Traffic|Mass Traffic Builder|Math")
	FVector ConvertPositionFromHoudini(FVector Vector, bool bDoConvert = true) const;

	/** Converts a vector (normal, direction, etc.) from Houdini coordinate space to Unreal coordinate space. */
	UFUNCTION(BlueprintPure, Category = "Mass Traffic|Mass Traffic Builder|Math")
	FVector ConvertVectorFromHoudini(FVector Vector, bool bDoConvert = true) const;

	
	/** Whether to add debug (and error) markers to the scene. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bDoAddDebugMarkers = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	float DebugArrowSize = 50.0f;

	/** Debug point size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	float DebugPointSize = 20.0f;

	/** Debug line thickness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	float DebugLineSegmentThickness = 10.0f;

	/** Debug point jitter magnitude. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	float DebugPointJitter = 0.0f;

	/** Debug color jitter magnitude. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	float DebugColorJitter = 0.0f;

	/** Debug colors are random, but strongly blended towards specific debug colors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	float DebugColorTintBlend = 0.75f;

	/** Random number stream used for jittering, and other random numbers, used when debugging. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	FRandomStream DebugRandomStream;

	/** Used for locating any markers that contain any of these strings in their debug text.
	 * If they do, these markers are rendered differently, to stand out strongly.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	TSet<FString> DebugLocateMarkerIDs;

	/** Debug text Z value, when using DebugLocateText. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	float DebugLocateTextZ = 10000.0f;

	/** Debug text size, when using DebugLocateText. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	float DebugLocateTextSize = 120.0f;

	/** Makes a color from an ID string. */
	UFUNCTION(BlueprintPure, Category = "Mass Traffic|Mass Traffic Builder|Debug")
	FLinearColor MakeDebugColorFromID(FString ID, FLinearColor ColorTint) const;

	/** Jitters a color. Quality of this jitter is controlled by other class properties. */
	UFUNCTION(BlueprintPure, Category = "Mass Traffic|Mass Traffic Builder|Debug")
	FLinearColor JitterColor(FLinearColor Color) const;

	/** Jitters a vector. Quality of this jitter is controlled by other class properties. */
	UFUNCTION(BlueprintPure, Category = "Mass Traffic|Mass Traffic Builder|Debug")
	FVector JitterPoint(FVector Point) const;

	/** Adds a 'debug marker' - which is a line segment pointing to a problem area, and a MassTrafficBuilderMarkerActor.
	 * The text in the marker actor is former from Prefix and ID.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Debug")
	void AddDebugMarker(FVector Location, FString Prefix, FString ID, FLinearColor Color);

	/**
	 * Adds an 'error marker' - which is a special 'debug marker' (see above.)
	 * Provides an additional error string, caller names, and a 'sequence number' used if the error regards sequenced data.
	 * Also prints error to log, whether or not DoAddDebugMarkers is true.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Debug")
	void AddDebugErrorMarker(FVector Location, FString Prefix, FString ID, FString Error, FString Caller, int SequenceNumber = -1);

	/** Draws a debug point. */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Debug")
	void DrawDebugPoint(FMassTrafficDebugPoint DebugPoint);

	/** Draws a debug line segment. */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Debug")
	void DrawDebugLineSegment(FMassTrafficDebugLineSegment DebugLineSegment);

	/** Draws debug points. */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Debug")
	void DrawDebugPoints(const TArray<FMassTrafficDebugPoint>& DebugPoints);

	/** Draws debug line segments. */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Debug")
	void DrawDebugLineSegments(const TArray<FMassTrafficDebugLineSegment>& DebugLineSegments);

	/**
	 * Destroys all actors that:
	 *   - Are added as a child of us.
	 *   - Have the DebugTagName (see above) added as a tag.
	 * Also clears -
	 *   - DebugMarkerLineSegments
	 *   - PointHintsMap
	 *   - RoadSegmentsMap.
	 *   - RoadSplinesMap
	 *   - IntersectionsMap
	 */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Debug")
	void ClearDebug();


	/** Precision used in making strings returned by VectorToMapKey(). */
	UPROPERTY(BlueprintReadWrite, Category = "Utilities", meta=(ClampMin="1"))
	int FractionalFloatPrecisionForMapKeys = 0;

	/** Finds a string, in a string-to-string map. */
	UFUNCTION(BlueprintPure, Category = "Mass Traffic|Mass Traffic Builder|Utilities")
	FString FindAsString(const TMap<FString, FString>& StringMap, FString Key, FString Default, bool& bIsValid, bool bDoAllowMissingKey = false, bool bDoPrintErrors = true) const;

	/** Finds a name, in a string-to-string map. */
	UFUNCTION(BlueprintPure, Category = "Mass Traffic|Mass Traffic Builder|Utilities")
	FName FindAsName(const TMap<FString, FString>& StringMap, FString Key, FName Default, bool& bIsValid, bool bDoAllowMissingKey = false, bool bDoPrintErrors = true) const;

	/** Finds a bool, in a string-to-string map. */
	UFUNCTION(BlueprintPure, Category = "Mass Traffic|Mass Traffic Builder|Utilities")
	bool FindAsBool(const TMap<FString, FString>& StringMap, FString Key, bool Default, bool& bIsValid, bool bDoAllowMissingKey = false, bool bDoPrintErrors = true) const;

	/** Finds an int, in a string-to-string map. */
	UFUNCTION(BlueprintPure, Category = "Mass Traffic|Mass Traffic Builder|Utilities")
	int FindAsInt(const TMap<FString, FString>& StringMap, FString Key, int Default, bool& bIsValid, bool bDoAllowMissingKey = false, bool bDoPrintErrors = true) const;

	/** Finds a float, in a string-to-string map. */
	UFUNCTION(BlueprintPure, Category = "Mass Traffic|Mass Traffic Builder|Utilities")
	float FindAsFloat(const TMap<FString, FString>& StringMap, FString Key, float Default, bool& bIsValid, bool bDoAllowMissingKey = false, bool bDoPrintErrors = true, bool bDoCheckForNaNs = true) const;

	/** Finds a vector, in a string-to-string map. */
	UFUNCTION(BlueprintPure, Category = "Mass Traffic|Mass Traffic Builder|Utilities")
	FVector FindAsVector(const TMap<FString, FString>& StringMap, FString XKey, FString YKey, FString ZKey, FVector Default, bool& bIsValid, bool bDoAllowMissingKeys = false, bool bDoPrintErrors = true, bool bDoCheckForNaNs = true) const;

	/** Finds a quaternion, in a string-to-string map. */
	UFUNCTION(BlueprintPure, Category = "Mass Traffic|Mass Traffic Builder|Utilities")
	FQuat FindAsQuaternion(const TMap<FString, FString>& StringMap, FString WKey, FString XKey, FString YKey, FString ZKey, FQuat Default, bool& bIsValid, bool bDoAllowMissingKeys = false, bool bDoPrintErrors = true, bool bDoCheckForNaNs = true) const;

	/**
	 * Turns a vector into a string, often used to map keys.
	 * The FractionalFloatPercisionForMapKeys class property controls percision used in generating this map key string.
	 */
	UFUNCTION(BlueprintPure, Category = "Mass Traffic|Mass Traffic Builder|Utilities")
	FString VectorToMapKey(FVector Vector) const;

	
	/** Map that stores all internal road segments. */
	UPROPERTY(BlueprintReadWrite, Category = "Road Segments")
	TMap<FMassTrafficRoadSegmentMapKey,FMassTrafficRoadSegment> RoadSegmentsMap;

	/** Adds a road segment to the internal road segments map. */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Road Segments")
	void AddRoadSegment(FString RoadSegmentID,
						FMassTrafficPoint StartPoint,
						FMassTrafficPoint EndPoint,
						int NumberOfLanes,
						bool bHasCenterDivider,
						float LaneWidthCM,
						float CenterDividerWidthCM,
						bool bCanSupportLongVehicles,
						bool bIsFreeway,
						bool bIsMainPartOfFreeway,
						int32 UserDensity,
						EMassTrafficUser User);


	/** Map that stores all internal road splines. */
	UPROPERTY(BlueprintReadWrite, Category = "Road Splines")
	TMap<FMassTrafficRoadSplineMapKey,FMassTrafficRoadSpline> RoadSplinesMap;

	/** Adds a road spline to the internal road splines map. */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Road Splines")
	void AddRoadSpline(FString RoadSplineID,
						int NumberOfLanes,
						bool bHasCenterDivider,
						float LaneWidthCM,
						float CenterDividerWidthCM,
						bool bIsUnidirectional,
						bool bIsClosed,
						bool bCanSupportLongVehicles,
						bool bIsFreeway,
						bool bIsMainPartOfFreeway,
						bool bIsFreewayOnramp,
						bool bIsFreewayOfframp,
						int32 UserDensity,
						EMassTrafficUser User);

	/** Adds a point to a road spline. The road spline must have been previously already be added. */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Road Splines")
	void AddRoadSplinePoint(FString RoadSpineID, int RoadSplineSequenceNumber, FMassTrafficPoint Point, EMassTrafficUser User);

	/**
	 * For all road splines that are marked as 'closed' - makes sure the last point is joined to the first point,
	 * and with a smooth tangent.
	 * DEPRECATED
	 */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Road Splines")
	void LoopAllClosedRoadSplines();

	/**
	 * Finds road splines that are joined head-to-tail or tail-to-head, and adjusts the end-point tangents to be smooth.
	 * An end-point of a road spline is considered joined to the end-point on another spline when these points are coincident.
	 * (Splines with joined heads or joined tails are ignored.)
	 */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Road Splines")
	void AdjustTangentsForCoincidentRoadSplineEndPoints();

	/** Chops up road splines into smaller pieces. */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Road Splines")
	void ChopUpAllRoadSplines(int32 MaxPointsInChunk = -1/*means ignore*/, float MaxAngleInChunk = -1.0f/*means ignore*/);


	UFUNCTION(BlueprintPure, Category = "Intersections")
	EMassTrafficSpecialConnectionType StringToSpecialConnectionType(FString String) const;

	
	/** Map that stores all internal intersections. */
	UPROPERTY(BlueprintReadWrite, Category = "Intersections")
	TMap<FMassTrafficIntersectionMapKey,FMassTrafficIntersection> IntersectionsMap;


	/** Adds an intersection to the internal intersections map. */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Intersections")
	void AddIntersection(FString IntersectionID,
						FString ParentIntersectionID,
						bool bCanSupportLongVehicles,
						bool bIsFreeway,
						bool bIsMainPartOfFreeway,
						bool bIsFreewayOnramp,
						bool bIsFreewayOfframp,
						EMassTrafficUser User,
						bool bIsCrosswalk);

	/** Adds an intersection link (intersection side) to an intersection. The intersection must have been previously already be added. */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Intersections")
	void AddIntersectionLink(FString IntersectionID,
							int IntersectionSequenceNumber,
							FMassTrafficPoint Point,
							FString ConnectedIntersectionID,
							int ConnectedIntersectionSequenceNumber,
							int NumberOfLanes,
							bool bHasCenterDivider,
							float LaneWidthCM,
							float CenterDividerWidthCM,
							bool bIsUnidirectional,
							bool bHasTrafficLight,
							FVector TrafficLightPosition,
							EMassTrafficSpecialConnectionType SpecialConnectionType,
							int32 UserDensity,
							EMassTrafficUser User);

	/** Adds a center pointer to an intersection. The intersection must have been previously already be added. */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Intersections")
	void AddIntersectionCenter(FString IntersectionID, FVector Point, EMassTrafficUser User);

	/** Adds forward and up vectors an intersection. The intersection must have been previously already be added. */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Intersections")
	bool AddIntersectionLinkForwardAndUpVectors(FString IntersectionID, int IntersectionSequenceNumber, FVector ForwardVector, FVector UpVector, EMassTrafficUser User);
		
	/** Removes an intersection link (or intersection side) from an intersection. */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Intersections")
	void ClearLanesFromIntersectionLink(FString IntersectionID, EMassTrafficUser User, int IntersectionSequenceNumber);
		
	/**
	 * Find intersection side (sequence number) that has a road that enters or leaves intersection and crosses over a segment.
	 * Returns intersection side (sequence number), or -1 if none found.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Intersections")
	int SegmentCrossesRoadEnteringOrLeavingIntersectionSide(FString IntersectionID, EMassTrafficUser User, FVector SegmentPointA, FVector SegmentPointB) const;

	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Intersections")
	bool CompareNumberOfLanesOnIntersectionLinks(FString IntersectionID1,
												EMassTrafficUser User1,
												int IntersectionSequenceNumber1,
												FString IntersectionID2,
												EMassTrafficUser User2,
												int IntersectionSequenceNumber2) const;

	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Intersections")
	bool CompareLaneWidthsOnIntersectionLinks(FString IntersectionID1,
											EMassTrafficUser User1,
											int IntersectionSequenceNumber1,
											FString IntersectionID2,
											EMassTrafficUser User2,
											int IntersectionSequenceNumber2) const;


	/** Hints for points, Use class function VectorToMapKey() to access the map. */
	UPROPERTY(BlueprintReadWrite, Category = "Point Hints")
	TMap<FString,FMassTrafficPointHints> PointHintsMap;

	/** Adds hints about a point in space. */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Point Hints")
	void AddPointHints(FVector Point, bool bIsRoadSegmentPoint, bool bIsRoadSegmentStartPoint, bool bIsRoadSegmentEndPoint, bool bIsRoadSplinePoint, bool bIsIntersectionLinkPoint, bool bIsIntersectionCenterPoint, FString RoadSegmentID, FString RoadSplineID, FString IntersectionID);

	/**
	 * Gets hints about a point in space. 	
	 * The FractionalFloatPercisionForMapKeys class property controls percision for this lookup.
	 */
	UFUNCTION(BlueprintPure, Category = "Mass Traffic|Mass Traffic Builder|Point Hints")
	bool GetPointHints(FVector Point, FMassTrafficPointHints& PointHints);


	/** Gets all points and IDs from a RuleProcessor point cloud. */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Rule Processor")
	UPointCloudView* GetRuleProcessorPoints(UPointCloud* PointCloud, TArray<FTransform>& Transforms, TArray<int32>& IDs, bool& bIsValid);


	/**
	 * Max distance (cm) a crosswalk lane can be from an intersection side point, to be controlled by that intersection side.
	 * The default value here was experimentally found, and is the best for this demo.
	 */
	UPROPERTY(EditAnywhere, Category="Mass Traffic|Mass Traffic Builder|Advanced")
	float IntersectionSideToCrosswalkSearchDistance = 410.0f;

	/**
	 * Identifies all road segments that look like crosswalks, and marks them.
	 * Call this AFTER road segment and intersection maps are filled, and BEFORE generating Zone Shapes.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Advanced")
	void MarkAllCrosswalkRoadSegments();


	/** Parent under which generated Zone Shapes are placed. (Null means this class.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone Graph")
	AActor* ZoneShapeParentActor = nullptr;

	/** How and where to store Zone Shapes - either as there own actors under the Zone Shape parent, or as components inside the Zone Shape parent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone Graph")
	EMassTrafficBuildType BuildType = EMassTrafficBuildType::Components;

	
	/** Allows storage and lookup of Zone Lane Profiles given specific conditions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone Graph")
	TMap<FMassTrafficExplicitLaneProfileRefMapKey,FZoneLaneProfileRef> ExplicitLaneProfileRefMap; 

	/**
	 * Zone Graph Tag to use for Zone Shapes that are for vehicles.
	 * Only used to redundantly tag Zone Shapes. Lane profiles should provide their own per-lane 'vehicle' tags.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone Graph")
	FZoneGraphTag ZoneGraphTagForVehicles;	

	/**
	 * Zone Graph Tag to use for Zone Shapes that are for pedestrians.
	 * Only used to redundantly tag Zone Shapes. Lane profiles should provide their own per-lane 'pedestrian' tags.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone Graph")
	FZoneGraphTag ZoneGraphTagForPedestrians;	

	/** Zone Graph Tag to use for Zone Shapes that are for intersections. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone Graph")
	FZoneGraphTag ZoneGraphTagForIntersections;	

	/** Zone Graph Tag to use for Zone Shapes that are for intersections. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone Graph")
	FZoneGraphTag ZoneGraphTagForCity;	

	/** Zone Graph Tag to use for Zone Shapes that are for intersections. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone Graph")
	FZoneGraphTag ZoneGraphTagForFreeway;	

	/** Zone Graph Tag to use for Zone Shapes that are for intersections. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone Graph")
	FZoneGraphTag ZoneGraphTagForCrosswalks;	

	/** Zone Graph Tag to use for Zone Shapes that are for intersections. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone Graph")
	FZoneGraphTag ZoneGraphTagForFreewayOnramps;	

	/** Zone Graph Tag to use for Zone Shapes that are for intersections. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone Graph")
	FZoneGraphTag ZoneGraphTagForFreewayOfframps;	

	/** Zone Graph Tag to use for Zone Shapes that are for intersections. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone Graph")
	FZoneGraphTag ZoneGraphTagForPedestrianDensity0;	

	/** Zone Graph Tag to use for Zone Shapes that are for intersections. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone Graph")
	FZoneGraphTag ZoneGraphTagForPedestrianDensity1;	

	/** Zone Graph Tag to use for Zone Shapes that are for intersections. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone Graph")
	FZoneGraphTag ZoneGraphTagForPedestrianDensity2;	

	/** Zone Graph Tag to use for Zone Shapes that are for intersections. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone Graph")
	FZoneGraphTag ZoneGraphTagForPedestrianDensity3;	

	/** Zone Graph Tag to use for Zone Shapes that are for intersections. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone Graph")
	FZoneGraphTag ZoneGraphTagForVehicleDensity0;	

	/** Zone Graph Tag to use for Zone Shapes that are for intersections. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone Graph")
	FZoneGraphTag ZoneGraphTagForVehicleDensity1;	

	/** Zone Graph Tag to use for Zone Shapes that are for intersections. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone Graph")
	FZoneGraphTag ZoneGraphTagForVehicleDensity2;	

	/** Zone Graph Tag to use for Zone Shapes that are for intersections. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone Graph")
	FZoneGraphTag ZoneGraphTagForVehicleDensity3;	

	/** Creates and builds a Zone Shape component as a straight road segment. */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Zone Graph")
	UZoneShapeComponent* BuildZoneShapeAsRoadSegment(FName Label, const FMassTrafficRoadSegment& RoadSegment, FZoneLaneProfileRef ZoneLaneProfileRef, bool bDoReverseZoneLaneProfile, FZoneGraphTagMask ZoneGraphTags);

	/** Creates and builds a Zone Shape component as a road spline. */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Zone Graph")
	UZoneShapeComponent* BuildZoneShapeAsRoadSpline(FName Label, const FMassTrafficRoadSpline& RoadSpline, FZoneLaneProfileRef ZoneLaneProfileRef, bool bDoReverseZoneLaneProfile, FZoneGraphTagMask ZoneGraphTags);

	/** Creates and builds a Zone Shape component as an intersection. */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Zone Graph")
	UZoneShapeComponent* BuildZoneShapeAsIntersection(FName Label, const FMassTrafficIntersection& Intersection, TArray<FZoneLaneProfileRef> PerPointZoneLaneProfileRefs, TArray<bool> DoReverseZoneLaneProfiles, bool bAutomaticallySetConnectionRestrictionsWithSpecialConnections, bool bUseArcsForLanes, FZoneGraphTagMask ZoneGraphTags);

	/**
	 * Destroys all Zone Shape actors that:
	 *   - Have been added.
	 *   - Have the ZoneShapeTagName (see above) added as a tag.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Mass Traffic Builder|Zone Graph")
	void ClearAll();


private:
	AActor* GetZoneShapeParentActor();

	UZoneShapeComponent* AddZoneShape(FName Label, AActor*& Actor);

	void AddUserTagToZoneGraphTagMask(const EMassTrafficUser& User, FZoneGraphTagMask& ZoneGraphTagMask) const;
};

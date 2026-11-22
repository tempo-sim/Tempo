// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "TempoSensorsTypes.generated.h"

UENUM(BlueprintType)
enum EMeasurementType: uint8
{
	COLOR_IMAGE = 0 UMETA(DisplayName="ColorImage"),
	DEPTH_IMAGE = 1 UMETA(DisplayName="DepthImage"),
	LABEL_IMAGE = 2 UMETA(DisplayName="LabelImage"),
	LIDAR_SCAN = 3 UMETA(DisplayName="LidarScan"),
	BOUNDING_BOXES = 4 UMETA(DisplayName="BoundingBoxes"),
	VIDEO = 5 UMETA(DisplayName="Video"),
};

UENUM(BlueprintType)
enum ELabelType : uint8
{
	// Every object of a class carries the class's Label.
	Semantic = 0,
	// Every labeled Actor, and every component that resolves a label of its own, carries an instance
	// ID of its own.
	Instance = 1,
	// Instance IDs for things and class Labels for stuff: as Instance, but an instance ID names one
	// object, and what is no one object carries its class's Label instead (see
	// FSemanticLabel::bPanopticInstanced).
	Panoptic = 2
};

UENUM(BlueprintType)
enum EColorImageEncoding: uint8
{
	RGB8 = 0,
	BGR8 = 1
};

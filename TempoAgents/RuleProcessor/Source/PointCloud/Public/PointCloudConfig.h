// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

POINTCLOUD_API DECLARE_LOG_CATEGORY_EXTERN(PointCloudLog, Log, All);

// This flag controls all logging-related code, needed for the others to have any effect
//#define RULEPROCESSOR_ENABLE_LOGGING

// This flag controls compilation of timing information into RuleProcessor. If enabled a record is kept of all sql query calls and the time taken for each invocation
//#define RULEPROCESSOR_INCLUDE_TIMING

// This flag controls whether Timers will output in the log, independently of the logging
//#define RULEPROCESSOR_TIMERS_REPORT
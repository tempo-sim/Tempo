// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "PointCloudSliceAndDiceShared.h"

class UPointCloudRule;
class FPointCloudRuleInstance;
class FSliceAndDiceContext;
struct FPointCloudSliceAndDiceReportEntry;

struct POINTCLOUD_API FPointCloudSliceAndDiceReportFrame
{
	FPointCloudSliceAndDiceReportFrame(EPointCloudReportLevel InReportingLevel, const FString& InName = FString(), const UPointCloudRule* InRule = nullptr)
		: Name(InName)
		, Rule(InRule)
		, ReportingLevel(InReportingLevel)
	{}

	/**
	* Print the contents of this frame to the given String, including indentation depth
	* @param Depth - The depth of the current stack of frames, used for indentation
	* @param OutString - The string into which the report should be written
	*/
	void ToString(FString& InOutString, int Depth = 0) const;

	/**
	* Add a message into the current frame
	* @param Message - The messages to Log for the current frame
	*/
	void AddMessage(const FString& Message)
	{
		Entries.Emplace(MakeShared<FPointCloudSliceAndDiceReportEntry>(Message));
	}

	/**
	* Add a message into the current frame
	* @param Message - The messages to Log for the current frame
	*/
	void PushMessage(const FString& Message)
	{
		Entries.EmplaceAt(0,MakeShared<FPointCloudSliceAndDiceReportEntry>(Message));
	}

	/**
	* Push A Name / Value pair record to the start of the current message list. This will ensure the message is reported before existing messages
	* @oaram Name - The name of the value
	* @oaram Value - The Value
	*/
	void PushParameter(const FString& InName, const FString& InValue)
	{
		PushMessage(InName + " = " + InValue);
	}

	/**
	* Push A Name / Value pair record to the start of the current message list. This will ensure the message is reported before existing messages
	* @oaram Name - The name of the value
	* @oaram Value - The Value
	*/
	void PushParameter(const FString& InName, const int32& InValue)
	{
		PushMessage(InName + " = " + FString::FromInt(InValue));
	}

	/**
	* Add a record of a name / value pair into the current frame
	* @oaram Name - The name of the value
	* @oaram Value - The Value
	*/
	void AddParameter(const FString& InName, const FString& InValue)
	{
		AddMessage(InName + " = " + InValue);		
	}

	// A human readable name for this frame
	FString Name;

	// The set of messages and sub frames
	TArray<TSharedPtr<FPointCloudSliceAndDiceReportEntry>> Entries;

	// Optional Pointer that associates this frame with the rule that generated it
	const UPointCloudRule* Rule;

	// Reporting level for this frame
	EPointCloudReportLevel ReportingLevel;
};

using FPointCloudSliceAndDiceReportFramePtr = TSharedPtr<FPointCloudSliceAndDiceReportFrame>;

struct POINTCLOUD_API FPointCloudSliceAndDiceReportEntry
{
	enum class EntryType
	{
		Message,
		Frame
	};

	FPointCloudSliceAndDiceReportEntry(const FPointCloudSliceAndDiceReportFramePtr InFrame)
		: Type(EntryType::Frame)
		, Frame(InFrame)
	{}


	FPointCloudSliceAndDiceReportEntry(const FString& InMessage) : Type(EntryType::Message)
	{		
			MessageString = InMessage;			
	}

	EntryType Type;
	FString	MessageString;
	FPointCloudSliceAndDiceReportFramePtr Frame;
};

using FPointCloudSliceAndDiceReportEntryPtr = TSharedPtr<FPointCloudSliceAndDiceReportEntry>;

class POINTCLOUD_API FPointCloudSliceAndDiceRuleReporter
{
public:

	FPointCloudSliceAndDiceRuleReporter(const UPointCloudRule* InRule, FSliceAndDiceContext& InContext);
	~FPointCloudSliceAndDiceRuleReporter();

protected:

	const UPointCloudRule* Rule;
	FSliceAndDiceContext& Context;
};

class POINTCLOUD_API FPointCloudSliceAndDiceReport
{	
					
public:

	FPointCloudSliceAndDiceReport(bool IsActive, EPointCloudReportLevel ReportingLevel);
	
	/**
	* Start a new reporting frame. Frames should be used to group related information together and can be nested by multiple calls to Push / Pop
	* @param Name - The name for the new frame.	
	*/
	void PushFrame(const FString& Name);

	/**
	* Start a new reporting frame. Frames should be used to group related information together and can be nested by multiple calls to Push / Pop
	* @param Rule - The rule for which we are starting a new frame
	*/
	void PushFrame(const UPointCloudRule* Rule);
	
	/**
	* Add a message into the current frame
	* @param Message - The messages to Log for the current frame
	*/
	void AddMessage(const FString& Message);

	/**
	* Add a message to the front of the current set of messages. This ensures it will be reported before all other values
	* @param Message - The messages to Log for the current frame
	*/
	void PushMessage(const FString& Message);

	/** Report Overrides set on the given rule *
	* @param The Rule to report the overrides on. This will add a record of which properties can be overridden, if they are and what their value is (if printable)
	*/
	void ReportOverrides(const UPointCloudRule* Rule);
	
	/**
	* Return a string version of this Report
	* @return A String containing the human readable version of this report
	*/
	FString ToString() const;

	/**
	* Add a record of a name / value pair into the current frame
	* @oaram Name - The name of the value
	* @oaram Value - The Value
	*/
	void AddParameter(const FString& Name, const FString& Value);

	/**
	* Push A Name value pair record to the start of the current message list. This will ensure the message is reported before existing messages
	* @oaram Name - The name of the value
	* @oaram Value - The Value
	*/
	void PushParameter(const FString& Name, const FString& Value);

	/** Add a break into the report, used to separate sections 
	*/
	void AddBreak();

	/**
	* Add a record of a name / value pair into the current frame
	* @oaram Name - The name of the value
	* @oaram Value - The Value
	*/
	void AddParameter(const FString& Name, int Value);
		
	/** End the current Frame and pop it off the stack
	*/
	void PopFrame();

	/** Return the current reporting level 
	*@return The granulatiry level for this report
	*/
	EPointCloudReportLevel GetReportingLevel() const;

	/** Return true if this reporting object is active 
	* @param Return True if this reporting object is active
	*/
	bool GetIsActive() const;

	/**
	* Return a pointer to the current frame. 
	* @return The pointer to the current frame or nullptr if there is none
	*/
	FPointCloudSliceAndDiceReportFramePtr CurrentFrame();

private:
		
	TArray<FPointCloudSliceAndDiceReportFramePtr> Reports;
	TArray<FPointCloudSliceAndDiceReportFramePtr> Frames;

	/** Set to true if the this report object is currently active */
	bool bIsActive;

	/** Control how much information is returned during a reporting run */
	EPointCloudReportLevel ReportingLevel = EPointCloudReportLevel::Basic;	
};

// Copyright Epic Games, Inc. All Rights Reserved.
#include "PointCloudSliceAndDiceReport.h"
#include "PointCloudSliceAndDiceRule.h"
#include "PointCloudSliceAndDiceRuleData.h"

namespace FPointCloudSliceAndDiceReportHelpers
{
	void MakeIndents(int Num, FString &InString)
	{
		for (int i = 0; i < Num; i++)
		{
			InString += "        ";
		}		
	}

	void PrintLine(int Depth, const FString &Line, FString& Out)
	{
		FPointCloudSliceAndDiceReportHelpers::MakeIndents(Depth, Out);
		Out += Line + LINE_TERMINATOR;
	}
}

EPointCloudReportLevel FPointCloudSliceAndDiceReport::GetReportingLevel() const
{
	return ReportingLevel;
}

bool FPointCloudSliceAndDiceReport::GetIsActive() const
{
	return bIsActive;
}

FPointCloudSliceAndDiceReport::FPointCloudSliceAndDiceReport(bool InIsActive, EPointCloudReportLevel InReportingLevel) : bIsActive(InIsActive), ReportingLevel(InReportingLevel)
{

}

#define PRINT_LINE(a) FPointCloudSliceAndDiceReportHelpers::PrintLine(Depth, a, InOutString)
#define PRINT_LINE_INDENT(a) FPointCloudSliceAndDiceReportHelpers::PrintLine(Depth+1, a, InOutString)

void FPointCloudSliceAndDiceReport::PushFrame(const FString& Name)
{
	Frames.Emplace(MakeShared<FPointCloudSliceAndDiceReportFrame>(ReportingLevel, Name));
}

void FPointCloudSliceAndDiceReport::PushFrame(const UPointCloudRule* InRule)
{
	if (InRule == nullptr)
	{
		return; 
	}

	FString Name = FString::Printf(TEXT("%s (%s)"), *InRule->RuleName(), *InRule->Label);

	if (InRule->IsEnabled() == false)
	{
		Name += TEXT(" DISABLED");
	}

	Frames.Emplace(MakeShared<FPointCloudSliceAndDiceReportFrame>(ReportingLevel, Name, InRule));

	if (ReportingLevel > EPointCloudReportLevel::Basic)
	{
		ReportOverrides(InRule);
	}
}

namespace PointCloudSliceAndDiceReport
{
	bool EndsWithTwoEmptyLines(const FString& InString)
	{
		return InString.EndsWith(FString(LINE_TERMINATOR) + FString(LINE_TERMINATOR));
	}
}

void FPointCloudSliceAndDiceReportFrame::ToString(FString& InOutString, int Depth) const
{
	if (PointCloudSliceAndDiceReport::EndsWithTwoEmptyLines (InOutString) == false)
	{
		InOutString += LINE_TERMINATOR;
	}

	PRINT_LINE(Name);	

	for (const auto &Entry : Entries)
	{
		if (Entry != nullptr)
		{
			switch (Entry->Type)
			{
			case FPointCloudSliceAndDiceReportEntry::EntryType::Message:				
				PRINT_LINE_INDENT(Entry->MessageString);				
				break; 
			case FPointCloudSliceAndDiceReportEntry::EntryType::Frame:								
				Entry->Frame->ToString(InOutString, Depth +1);									
				break;
			}
		}
	}

	if (PointCloudSliceAndDiceReport::EndsWithTwoEmptyLines(InOutString) == false)
	{
		InOutString += LINE_TERMINATOR;
	}
}

void FPointCloudSliceAndDiceReport::AddBreak()
{
	AddMessage(LINE_TERMINATOR);
}

void FPointCloudSliceAndDiceReport::ReportOverrides(const UPointCloudRule* Rule)
{
	if (Rule == nullptr || Rule->GetData() == nullptr)
	{
		return;
	}
		
	const TArray<FName> OverriddenProperties = Rule->GetData()->GetOverriddenProperties();
	
	if (OverriddenProperties.Num())
	{
		PushFrame(TEXT("Overrides"));

		for (const FName& Name : OverriddenProperties)
		{			
			AddParameter(Name.ToString(), true);
		}

		PopFrame();
	}

}

void FPointCloudSliceAndDiceReport::AddMessage(const FString& Message)
{
	if (CurrentFrame())
	{
		CurrentFrame()->AddMessage(Message);
	}	
}

void FPointCloudSliceAndDiceReport::PushMessage(const FString& Message)
{
	if (CurrentFrame())
	{
		CurrentFrame()->PushMessage(Message);
	}	
}

void FPointCloudSliceAndDiceReport::PushParameter(const FString& Name, const FString& Value)
{
	TStringBuilder<64> Builder;
	Builder.Append(Name).Append(TEXT("=")).Append(Value);

	PushMessage(Builder.ToString());
}

FString FPointCloudSliceAndDiceReport::ToString() const
{
	FString Result;
	for (const FPointCloudSliceAndDiceReportFramePtr& Frame : Reports)
	{
		Frame->ToString(Result);
	}

	return Result;
}

void FPointCloudSliceAndDiceReport::AddParameter(const FString& Name, const FString& Value)
{
	TStringBuilder<64> Builder;
	Builder.Append(Name).Append(TEXT("=")).Append(Value);
	
	AddMessage(Builder.ToString());
}

void FPointCloudSliceAndDiceReport::AddParameter(const FString& Name, int Value)
{
	AddParameter(Name, FString::FromInt(Value));
}

FPointCloudSliceAndDiceReportFramePtr FPointCloudSliceAndDiceReport::CurrentFrame()
{
	return Frames.Num() == 0 ? nullptr : Frames.Last();		
}

void FPointCloudSliceAndDiceReport::PopFrame()
{
	if (CurrentFrame() == nullptr)
	{
		return;
	}

	// Copy the current frame
	FPointCloudSliceAndDiceReportFramePtr Old = CurrentFrame();

	// Remove it from the end of the list of frames
	Frames.Pop();

	// If there is a new current frame, make the previous frame its child
	if (CurrentFrame() != nullptr)
	{
		FPointCloudSliceAndDiceReportEntryPtr NewEntry = MakeShared< FPointCloudSliceAndDiceReportEntry >(Old);
		CurrentFrame()->Entries.Add(NewEntry);
	}
	else
	{
		// otherwise add the report to the list of full reports
		Reports.Add(Old);
	}
}

FPointCloudSliceAndDiceRuleReporter::FPointCloudSliceAndDiceRuleReporter(const UPointCloudRule* InRule, FSliceAndDiceContext& InContext) : Rule(InRule), Context(InContext)
{
	if (Rule != nullptr)
	{
		Context.ReportObject.PushFrame(InRule);
		Rule->ReportParameters(Context);
	}
}

FPointCloudSliceAndDiceRuleReporter::~FPointCloudSliceAndDiceRuleReporter()
{
	if (Rule != nullptr)
	{
		Context.ReportObject.PopFrame();
	}
}
#undef PRINT_LINE
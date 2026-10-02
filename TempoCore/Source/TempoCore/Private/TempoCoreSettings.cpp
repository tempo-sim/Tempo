// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCoreSettings.h"

UTempoCoreSettings::UTempoCoreSettings()
	: bRenderMainViewport(!FParse::Param(FCommandLine::Get() , TEXT("RenderOffScreen")))
{
	CategoryName = TEXT("Tempo");
}

#if WITH_EDITOR
FText UTempoCoreSettings::GetSectionText() const
{
	return FText::FromString(FString(TEXT("Core")));
}
#endif

void UTempoCoreSettings::PostInitProperties()
{
	Super::PostInitProperties();

	// Naming either endpoint on the command line also selects its transport, so a one-off run
	// needs a single argument. -ServerSocket= is parsed second and so wins if both are given.
	int32 CommandLineServerPort;
	if (FParse::Value(FCommandLine::Get(), TEXT("ServerPort="), CommandLineServerPort))
	{
		ServerPort = CommandLineServerPort;
		ServerTransport = EServerTransport::Tcp;
	}

	FString CommandLineServerSocketPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("ServerSocket="), CommandLineServerSocketPath))
	{
		ServerSocketPath = CommandLineServerSocketPath;
		ServerTransport = EServerTransport::UnixSocket;
	}
}

void UTempoCoreSettings::SetTimeMode(ETimeMode TimeModeIn)
{
	TimeMode = TimeModeIn;

	TempoCoreTimeSettingsChangedEvent.Broadcast();
}

void UTempoCoreSettings::SetSimulatedStepsPerSecond(int32 SimulatedStepsPerSecondIn)
{
	SimulatedStepsPerSecond = SimulatedStepsPerSecondIn;

	TempoCoreTimeSettingsChangedEvent.Broadcast();
}

void UTempoCoreSettings::SetRenderMainViewport(bool bInRenderMainViewport)
{
	if (bRenderMainViewport != bInRenderMainViewport)
	{
		bRenderMainViewport = bInRenderMainViewport;
		TempoCoreRenderingSettingsChanged.Broadcast();
	}
}

#if WITH_EDITOR
void UTempoCoreSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property->GetName() == GET_MEMBER_NAME_CHECKED(UTempoCoreSettings, TimeMode) ||
		(TimeMode == ETimeMode::FixedStep && PropertyChangedEvent.Property->GetName() == GET_MEMBER_NAME_CHECKED(UTempoCoreSettings, SimulatedStepsPerSecond)))
	{
		TempoCoreTimeSettingsChangedEvent.Broadcast();
	}
	else if (PropertyChangedEvent.Property->GetName() == GET_MEMBER_NAME_CHECKED(UTempoCoreSettings, bRenderMainViewport))
	{
		TempoCoreRenderingSettingsChanged.Broadcast();
	}
}
#endif

// Copyright Tempo Simulation, LLC. All Rights Reserved.

#pragma once

#include "TempoAgentsEditorStyle.h"

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

class FTempoAgentsEditorCommands : public TCommands<FTempoAgentsEditorCommands>
{
public:

	FTempoAgentsEditorCommands()
		: TCommands<FTempoAgentsEditorCommands>(TEXT("TempoAgents"), NSLOCTEXT("Contexts", "TempoAgents", "TempoAgents Plugin"), NAME_None, FTempoAgentsEditorStyle::GetStyleSetName())
	{
	}

	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > PluginAction;
};

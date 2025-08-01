﻿// Copyright Tempo Simulation, LLC. All Rights Reserved

using UnrealBuildTool;

public class TempoVehicleControl : ModuleRules
{
    public TempoVehicleControl(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "ChaosVehicles",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                // Unreal
                "AIModule",
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                // Tempo
                "TempoMovementShared",
            }
        );
    }
}

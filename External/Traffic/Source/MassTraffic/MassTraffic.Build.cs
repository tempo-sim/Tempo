// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MassTraffic : ModuleRules
{
	public MassTraffic(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
			}
			
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				// AI/MassAI Plugin Modules
				"MassAIBehavior",
				"MassAIDebug",
				"MassNavigation",
				"MassZoneGraphNavigation",
				
				// AI/MassCrowd Plugin Modules
				"MassCrowd",

				// Runtime/MassEntity Plugin Modules
				"MassEntity",

				// Runtime/MassGameplay Plugin Modules
				"MassActors",
				"MassCommon",
				"MassGameplayDebug",
				"MassLOD",
				"MassRepresentation",
				"MassSpawner",
				
				// Misc
				"AIModule",
				"Core",
				"Engine",
				"NetCore",
				"StateTreeModule",
				"ZoneGraph",
				"AnimToTexture",
				"ChaosVehicles",
				"ChaosVehiclesCore",
			}
			);

		// StructUtils plugin was deprecated in 5.5 and moved into CoreUObject
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion < 5)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}

		// UE 5.8 split the core Mass types (FMassFragment, FMassTag, FMassEntityHandle, etc.) out of
		// the MassEntity plugin into a new MassCore runtime module. Depend on it so their reflection
		// symbols are available at link time.
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion >= 8)
		{
			PublicDependencyModuleNames.Add("MassCore");
		}

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// Runtime/MassGameplay Plugin Modules
				"MassMovement",
				"MassReplication",
				"MassSimulation",
				
				// Misc
				"CoreUObject",
				"GameplayTasks",
				"PointCloud",
				"RHI",
				"RenderCore",
				"Slate",
				"SlateCore",
				"PhysicsCore",
				"Chaos",
				"ChaosCore",
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
			);
		
		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd",
				}
			);
		}
	}
}

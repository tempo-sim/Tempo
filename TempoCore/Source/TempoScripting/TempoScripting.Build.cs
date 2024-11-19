// Copyright Tempo Simulation, LLC. All Rights Reserved.

using UnrealBuildTool;

public class TempoScripting : TempoModuleRules
{
	public TempoScripting(ReadOnlyTargetRules Target) : base(Target)
	{
		// Disallow hot reload for TempoScripting. See TempoScripting.cpp for a full explanation.
		bCanHotReload = false;
		
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"gRPC",
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// Unreal
				"CoreUObject",
				"DeveloperSettings",
				"Engine",
				// Tempo
				"TempoCoreShared",
			}
			);
		
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("HotReload");
		}
		
		// These definitions are used when building gRPC and must match here so that the headers
		// we use in the Tempo build match those in the built libraries exactly.
		// These are here, not in gRPC.Build.cs, because we import the gRPC symbols statically here
		// but export them dynamically to other modules.
		PublicDefinitions.Add("ABSL_BUILD_DLL=1");
		PublicDefinitions.Add("PROTOBUF_USE_DLLS=1");
		// Defining these privately will cause symbols to be __declspec(dllexport) within TempoScripring
		// and __declspec(dllimport) from elsewhere.
		PrivateDefinitions.Add("LIBPROTOBUF_EXPORTS=1");
		PrivateDefinitions.Add("LIBPROTOC_EXPORTS=1");
		PrivateDefinitions.Add("GRPC_DLL_EXPORTS=1");
		PrivateDefinitions.Add("GRPCXX_DLL_EXPORTS=1");
		PrivateDefinitions.Add("GPR_DLL_EXPORTS=1");
	}
}

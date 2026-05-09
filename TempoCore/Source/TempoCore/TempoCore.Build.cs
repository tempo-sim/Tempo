// Copyright Tempo Simulation, LLC. All Rights Reserved.

using UnrealBuildTool;

public class TempoCore : TempoModuleRules
{
	public TempoCore(ReadOnlyTargetRules Target) : base(Target)
	{
		// Hot reload would duplicate this module's gRPC/protobuf statics across the
		// old and new dlls, leaving callers pointing at stale addresses. The runtime
		// hot-reload listener registered in TempoCore.cpp resets protobuf's
		// generated descriptor pool so other modules can still reload safely.
		bCanHotReload = false;

		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				// Unreal
				"Core",
				"DeveloperSettings",
				// Tempo
				"gRPC",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// Unreal
				"AssetRegistry",
				"ChaosVehicles",
				"CoreUObject",
				"Engine",
				"InputCore",
				"Slate",
				"SlateCore",
				"UMG",
			}
		);

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("HotReload");
		}

		// gRPC and protobuf are statically imported into TempoCore and dynamically
		// re-exported to every other Tempo module. The defines here must match
		// those used to build the vendored libraries — otherwise headers and
		// libraries disagree about which symbols are exported.
		PublicDefinitions.Add("ABSL_BUILD_DLL=1");
		PublicDefinitions.Add("PROTOBUF_USE_DLLS=1");
		// Private side: declspec(dllexport) within TempoCore, dllimport everywhere else.
		PrivateDefinitions.Add("LIBPROTOBUF_EXPORTS=1");
		PrivateDefinitions.Add("LIBPROTOC_EXPORTS=1");
		PrivateDefinitions.Add("GRPC_DLL_EXPORTS=1");
		PrivateDefinitions.Add("GRPCXX_DLL_EXPORTS=1");
		PrivateDefinitions.Add("GPR_DLL_EXPORTS=1");
	}
}

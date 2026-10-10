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
				"AssetRegistry",
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
			// For bridging Tempo's pause state to the PIE editor pause controls (ATempoWorldSettings).
			PrivateDependencyModuleNames.Add("UnrealEd");
		}

		// The defines here must match those used to build the vendored gRPC and protobuf
		// libraries — otherwise headers and libraries disagree about which symbols are exported.
		PublicDefinitions.Add("PROTOBUF_USE_DLLS=1");
		if (gRPC.UsesSharedLibrary(Target))
		{
			// Abseil is in the shared library: declspec(dllimport) everywhere. Its exported data (such
			// as MixingHashState::kSeed) can only be reached through the import, not referred to directly.
			PublicDefinitions.Add("ABSL_CONSUME_DLL=1");
		}
		else
		{
			// A monolithic executable links the static libraries, which were built to be exported
			// from a shared library. The headers are compiled the same way.
			PublicDefinitions.Add("ABSL_BUILD_DLL=1");
			PrivateDefinitions.Add("LIBPROTOBUF_EXPORTS=1");
			PrivateDefinitions.Add("LIBPROTOC_EXPORTS=1");
			PrivateDefinitions.Add("GRPC_DLL_EXPORTS=1");
			PrivateDefinitions.Add("GRPCXX_DLL_EXPORTS=1");
			PrivateDefinitions.Add("GPR_DLL_EXPORTS=1");
		}
	}
}

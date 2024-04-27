// Copyright Tempo Simulation, LLC. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

/// <summary>
/// TempoModuleRules extends ModuleRules to always add Include paths for generated Protobuf code.
/// </summary>
public class TempoModuleRules : ModuleRules
{
	/// <summary>
	/// Constructor. 
	/// </summary>
	/// <param name="Target">Rules for building this target</param>
	public TempoModuleRules(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		string PublicModuleFolder = Path.Combine(ModuleDirectory, "Public");
		string PrivateModuleFolder = Path.Combine(ModuleDirectory, "Private");
		string PublicProtobufIncludes = Path.Combine(PublicModuleFolder, "ProtobufGenerated");
		string PrivateProtobufIncludes = Path.Combine(PrivateModuleFolder, "ProtobufGenerated");

		if (System.IO.Directory.GetFiles(PublicModuleFolder, "*.proto", SearchOption.AllDirectories).Length > 0)
		{
			System.IO.Directory.CreateDirectory(PublicProtobufIncludes);
			PublicIncludePaths.AddRange(
				new string[]
				{
					PublicProtobufIncludes
				}
			);
			System.IO.Directory.CreateDirectory(PrivateProtobufIncludes);
			PrivateIncludePaths.AddRange(
				new string[]
				{
					PrivateProtobufIncludes
				}
			);
		}

		if (System.IO.Directory.GetFiles(PrivateModuleFolder, "*.proto", SearchOption.AllDirectories).Length > 0)
		{
			System.IO.Directory.CreateDirectory(PrivateProtobufIncludes);
			PrivateIncludePaths.AddRange(
				new string[]
				{
					PrivateProtobufIncludes
				}
			);
		}
	}
}

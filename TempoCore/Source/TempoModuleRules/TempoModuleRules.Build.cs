// Copyright Tempo Simulation, LLC. All Rights Reserved.

using Microsoft.Extensions.Logging;
using System.IO;
using System.Reflection;
using UnrealBuildTool;

/// <summary>
/// The base class of every Tempo module's rules. It adds the include paths for a module's
/// generated Protobuf code.
///
/// It is not a module. It is declared in a Build.cs file so that UnrealBuildTool compiles it into
/// the same rules assembly as the modules that derive from it, in a folder of its own because
/// UnrealBuildTool stops looking for modules below a folder where it finds a Build.cs file.
/// </summary>
public class TempoModuleRules : ModuleRules
{
	public TempoModuleRules(ReadOnlyTargetRules Target) : base(Target)
	{
		WarnOnceAboutModifiedUnrealBuildTool();

		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		string PublicModuleFolder = Path.Combine(ModuleDirectory, "Public");
		string PrivateModuleFolder = Path.Combine(ModuleDirectory, "Private");
		string PublicProtobufIncludes = Path.Combine(PublicModuleFolder, "ProtobufGenerated");
		string PrivateProtobufIncludes = Path.Combine(PrivateModuleFolder, "ProtobufGenerated");

		bool bHasPublicProtos = HasProtos(PublicModuleFolder);
		if (bHasPublicProtos)
		{
			Directory.CreateDirectory(PublicProtobufIncludes);
			PublicIncludePaths.Add(PublicProtobufIncludes);
		}

		// Public protos generate private code too (the service implementations).
		if (bHasPublicProtos || HasProtos(PrivateModuleFolder))
		{
			Directory.CreateDirectory(PrivateProtobufIncludes);
			PrivateIncludePaths.Add(PrivateProtobufIncludes);
		}

		// gRPC and Protobuf are in a shared library of their own (or, in a monolithic build, in the
		// executable), which a module that uses them has to link.
		PublicDependencyModuleNames.Add("gRPC");
	}

	private static bool HasProtos(string Folder)
	{
		return Directory.Exists(Folder) && Directory.GetFiles(Folder, "*.proto", SearchOption.AllDirectories).Length > 0;
	}

	// An engine that Tempo's retired engine mods modified still has a TempoModuleRules compiled into
	// UnrealBuildTool. The build works regardless - the compiler prefers this class and says so in a
	// CS0436 warning per Tempo Build.cs - but the engine should go back to the way Epic ships it.
	private static bool bWarnedAboutModifiedUnrealBuildTool = false;

	private void WarnOnceAboutModifiedUnrealBuildTool()
	{
		if (!bWarnedAboutModifiedUnrealBuildTool && typeof(ModuleRules).Assembly.GetType("TempoModuleRules") != null)
		{
			bWarnedAboutModifiedUnrealBuildTool = true;
			Logger.LogWarning("This engine's UnrealBuildTool still contains Tempo's old modifications. (They are what any CS0436 warning " +
				"about a duplicate TempoModuleRules is about.) They are harmless but no longer used; restore the engine to the way Epic " +
				"ships it by verifying or reinstalling it. See Tempo's docs/migration/engine-mods-removal.md.");
		}
	}

	/// <summary>
	/// Whether hot reload should be enabled for this module. UnrealBuildTool keeps this on the rules'
	/// context, which only code inside UnrealBuildTool can reach directly.
	/// </summary>
	protected bool bCanHotReload
	{
		get
		{
			return (bool)GetCanHotReloadProperty(out object RulesContext).GetValue(RulesContext);
		}

		set
		{
			GetCanHotReloadProperty(out object RulesContext).SetValue(RulesContext, value);
		}
	}

	private const BindingFlags AnyInstanceMember = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance;

	private PropertyInfo GetCanHotReloadProperty(out object RulesContext)
	{
		RulesContext = typeof(ModuleRules).GetProperty("Context", AnyInstanceMember)?.GetValue(this);
		PropertyInfo CanHotReloadProperty = RulesContext?.GetType().GetProperty("bCanHotReload", AnyInstanceMember);
		if (CanHotReloadProperty == null)
		{
			throw new BuildException("TempoModuleRules could not find ModuleRules.Context.bCanHotReload. UnrealBuildTool has changed; find another way to it.");
		}
		return CanHotReloadProperty;
	}
}

// Copyright Tempo Simulation, LLC. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net.NetworkInformation;
using System.Text;
using System.Text.RegularExpressions;
using UnrealBuildTool;

public class gRPC : ModuleRules
{
    private UnrealTargetPlatform Platform;
    private UnrealTargetConfiguration Configuration;

    public class ModuleDepPaths
    {
        public readonly string[] HeaderPaths;
        public readonly string[] LibraryPaths;

        public ModuleDepPaths(string[] headerPaths, string[] libraryPaths)
        {
            HeaderPaths = headerPaths;
            LibraryPaths = libraryPaths;
        }
    }

    private IEnumerable<string> FindFilesInDirectory(string dir, string suffix = "")
    {
        List<string> matches = new List<string>();
        foreach (string file in Directory.EnumerateFiles(dir, "*." + suffix, SearchOption.AllDirectories))
        {
            matches.Add(file);
        }

        return matches;
    }

    public ModuleDepPaths GatherDeps()
    {
        List<string> HeaderPaths = new List<string>();
        List<string> LibraryPaths = new List<string>();
        
        HeaderPaths.Add(Path.Combine(ModuleDirectory, "Includes"));

        if (Platform == UnrealTargetPlatform.Win64)
        {
            LibraryPaths.AddRange(FindFilesInDirectory(Path.Combine(ModuleDirectory, "Libraries", "Windows"), "lib"));
        }
        else if (Platform == UnrealTargetPlatform.Mac)
        {
            LibraryPaths.AddRange(FindFilesInDirectory(Path.Combine(ModuleDirectory, "Libraries", "Mac"), "a"));
        }
        else if (Platform == UnrealTargetPlatform.Linux)
        {
            LibraryPaths.AddRange(FindFilesInDirectory(Path.Combine(ModuleDirectory, "Libraries", "Linux"), "a"));
        }
        else
        {
            Console.WriteLine("Unsupported target platform.");
        }

        return new ModuleDepPaths(HeaderPaths.ToArray(), LibraryPaths.ToArray());
    }

    public gRPC(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        PublicDefinitions.Add("GOOGLE_PROTOBUF_NO_RTTI=1");
        PublicDefinitions.Add("GPR_FORBID_UNREACHABLE_CODE=1");
        PublicDefinitions.Add("GRPC_ALLOW_EXCEPTIONS=0");
        PublicDefinitions.Add("PROTOBUF_ENABLE_DEBUG_LOGGING_MAY_LEAK_PII=0");
        PublicDefinitions.Add("GOOGLE_PROTOBUF_INTERNAL_DONATE_STEAL_INLINE=0");

        Platform = Target.Platform;
        Configuration = Target.Configuration;

        ModuleDepPaths moduleDepPaths = GatherDeps();
        PublicIncludePaths.AddRange(moduleDepPaths.HeaderPaths);
        PublicAdditionalLibraries.AddRange(moduleDepPaths.LibraryPaths);

        AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
        AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");
    }
}

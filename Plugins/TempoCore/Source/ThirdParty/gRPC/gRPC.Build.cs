// Copyright Tempo Simulation, LLC. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using UnrealBuildTool;

public class gRPC : ModuleRules
{
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
        return Directory.EnumerateFiles(dir, "*." + suffix, SearchOption.AllDirectories);
    }

    public ModuleDepPaths GatherDeps()
    {
        List<string> HeaderPaths = new List<string>();
        List<string> LibraryPaths = new List<string>();
        
        HeaderPaths.Add(Path.Combine(ModuleDirectory, "Includes"));

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            LibraryPaths.AddRange(FindFilesInDirectory(Path.Combine(ModuleDirectory, "Libraries", "Windows"), "lib"));
            // On Windows the exports.def file contains a list of all the symbols a module that depends on gRPC should re-export.
            LibraryPaths.Add(Path.Combine(ModuleDirectory, "Libraries", "Windows", "exports.def"));
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            LibraryPaths.AddRange(FindFilesInDirectory(Path.Combine(ModuleDirectory, "Libraries", "Mac"), "a"));
            // On Mac the exports.def file contains a list of all the libraries whose symbols a module that depends on gRPC should re-export.
            LibraryPaths.Add(Path.Combine(ModuleDirectory, "Libraries", "Mac", "exports.def"));
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            LibraryPaths.AddRange(FindFilesInDirectory(Path.Combine(ModuleDirectory, "Libraries", "Linux"), "a"));
            // On Linux the exports.def file contains a list of all the libraries whose symbols a module that depends on gRPC should re-export.
            LibraryPaths.Add(Path.Combine(ModuleDirectory, "Libraries", "Linux", "exports.def"));
        }
        else
        {
            Console.WriteLine("Unsupported target platform for module gRPC.");
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

        ModuleDepPaths moduleDepPaths = GatherDeps();
        PublicIncludePaths.AddRange(moduleDepPaths.HeaderPaths);
        PublicAdditionalLibraries.AddRange(moduleDepPaths.LibraryPaths);

        AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
        AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");
    }
}

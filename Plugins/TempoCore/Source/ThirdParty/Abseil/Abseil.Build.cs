// Copyright Tempo Simulation, LLC. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net.NetworkInformation;
using System.Text;
using System.Text.RegularExpressions;
using UnrealBuildTool;

public class Abseil : ModuleRules
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
        if (Directory.Exists(dir))
        {
            string[] files = Directory.GetFiles(dir);
            Regex regex = new Regex(".+\\." + suffix);

            foreach (string file in files)
            {
                if (regex.Match(file).Success)
                    matches.Add(file);
            }
        }

        return matches;
    }

    public ModuleDepPaths GatherDeps()
    {
        string IncludeRoot = Path.Combine(ModuleDirectory, "Includes");
        string LibRoot = Path.Combine(ModuleDirectory, "Libraries");

        List<string> Headers = new List<string>();
        List<string> Libs = new List<string>();

        string PlatformLibRoot = "";

        if (Platform == UnrealTargetPlatform.Win64)
        {
            PlatformLibRoot = Path.Combine(LibRoot, "Windows");
            Libs.AddRange(FindFilesInDirectory(PlatformLibRoot, "lib"));
        }
        else if (Platform == UnrealTargetPlatform.Mac)
        {
            PlatformLibRoot = Path.Combine(LibRoot, "Mac");
            Libs.AddRange(FindFilesInDirectory(PlatformLibRoot, "a"));
        }
        else if (Platform == UnrealTargetPlatform.Linux)
        {
            PlatformLibRoot = Path.Combine(LibRoot, "Linux");
            Libs.AddRange(FindFilesInDirectory(PlatformLibRoot, "a"));
        }
        else
        {
            Console.WriteLine("Unsupported target platform.");
        }

        Headers.Add(IncludeRoot);

        return new ModuleDepPaths(Headers.ToArray(), Libs.ToArray());
    }

    public Abseil(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        Platform = Target.Platform;
        Configuration = Target.Configuration;

        ModuleDepPaths moduleDepPaths = GatherDeps();

        PublicIncludePaths.AddRange(moduleDepPaths.HeaderPaths);

        PublicAdditionalLibraries.AddRange(moduleDepPaths.LibraryPaths);
    }
}

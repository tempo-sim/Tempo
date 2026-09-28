// Copyright Tempo Simulation, LLC. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class gRPC : ModuleRules
{
    // The shared library holding Tempo's one copy of gRPC, Protobuf and Abseil, from a TempoThirdParty release.
    public static string SharedLibraryPath(UnrealTargetPlatform Platform, string ModuleDirectory)
    {
        if (Platform == UnrealTargetPlatform.Win64)
        {
            return Path.Combine(ModuleDirectory, "Binaries", "Windows", "tempogrpc.dll");
        }
        if (Platform == UnrealTargetPlatform.Mac)
        {
            return Path.Combine(ModuleDirectory, "Libraries", "Mac", "libtempogrpc.dylib");
        }
        return Path.Combine(ModuleDirectory, "Libraries", "Linux", "libtempogrpc.so");
    }

    // Whether modules share gRPC, Protobuf and Abseil through the shared library. A monolithic
    // executable holds the one copy itself, and a TempoThirdParty release from before the shared
    // library existed leaves TempoCore to hold it and re-export it (see below).
    public static bool UsesSharedLibrary(ReadOnlyTargetRules Target, string ModuleDirectory)
    {
        return Target.LinkType != TargetLinkType.Monolithic && File.Exists(SharedLibraryPath(Target.Platform, ModuleDirectory));
    }

    public gRPC(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        PublicDefinitions.Add("GOOGLE_PROTOBUF_NO_RTTI=1");
        PublicDefinitions.Add("GPR_FORBID_UNREACHABLE_CODE=1");
        PublicDefinitions.Add("GRPC_ALLOW_EXCEPTIONS=0");
        PublicDefinitions.Add("PROTOBUF_ENABLE_DEBUG_LOGGING_MAY_LEAK_PII=0");
        PublicDefinitions.Add("GOOGLE_PROTOBUF_INTERNAL_DONATE_STEAL_INLINE=0");

        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Includes"));

        string PlatformName;
        string StaticLibraryExtension;
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PlatformName = "Windows";
            StaticLibraryExtension = "lib";
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            PlatformName = "Mac";
            StaticLibraryExtension = "a";
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            PlatformName = "Linux";
            StaticLibraryExtension = "a";
        }
        else
        {
            throw new BuildException("Unsupported target platform for module gRPC.");
        }
        string LibrariesDirectory = Path.Combine(ModuleDirectory, "Libraries", PlatformName);

        // gRPC, Protobuf and Abseil keep global state, so a process must hold exactly one copy of
        // them, which every module then shares.
        bool bUseSharedLibrary = UsesSharedLibrary(Target, ModuleDirectory);
        PublicDefinitions.Add("TEMPO_GRPC_IS_SHARED_LIBRARY=" + (bUseSharedLibrary ? "1" : "0"));
        if (bUseSharedLibrary)
        {
            // The one copy is the shared library, and nothing else may link the static libraries:
            // a linker takes what it finds in them from there, even what the shared library exports,
            // making a second copy.
            string SharedLibrary = SharedLibraryPath(Target.Platform, ModuleDirectory);
            PublicAdditionalLibraries.Add(Target.Platform == UnrealTargetPlatform.Win64 ? Path.Combine(LibrariesDirectory, "tempogrpc.lib") : SharedLibrary);
            RuntimeDependencies.Add(SharedLibrary);
        }
        else
        {
            foreach (string StaticLibrary in Directory.EnumerateFiles(LibrariesDirectory, "*." + StaticLibraryExtension))
            {
                if (Path.GetFileNameWithoutExtension(StaticLibrary) != "tempogrpc")
                {
                    PublicAdditionalLibraries.Add(StaticLibrary);
                }
            }
            // The one copy is in whatever links the static libraries: the executable, or TempoCore,
            // which then has to re-export it all. Tempo's UnrealBuildTool toolchains do that, told by
            // exports.def what to re-export: the libraries to take whole on Mac and Linux, the symbols
            // on Windows.
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesDirectory, "exports.def"));
        }

        AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
        AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");
    }
}

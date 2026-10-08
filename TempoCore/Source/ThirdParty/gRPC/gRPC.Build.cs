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
    // executable holds the one copy itself.
    public static bool UsesSharedLibrary(ReadOnlyTargetRules Target)
    {
        return Target.LinkType != TargetLinkType.Monolithic;
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
        bool bUseSharedLibrary = UsesSharedLibrary(Target);
        PublicDefinitions.Add("TEMPO_GRPC_IS_SHARED_LIBRARY=" + (bUseSharedLibrary ? "1" : "0"));
        if (bUseSharedLibrary)
        {
            // The one copy is the shared library, and nothing else may link the static libraries:
            // a linker takes what it finds in them from there, even what the shared library exports,
            // making a second copy. On Windows what is linked is the import library beside the DLL.
            string SharedLibrary = SharedLibraryPath(Target.Platform, ModuleDirectory);
            string LinkLibrary = Target.Platform == UnrealTargetPlatform.Win64 ? Path.Combine(LibrariesDirectory, "tempogrpc.lib") : SharedLibrary;
            foreach (string RequiredFile in new[] { SharedLibrary, LinkLibrary })
            {
                if (!File.Exists(RequiredFile))
                {
                    throw new BuildException("{0} is missing. Tempo needs a TempoThirdParty gRPC release that includes it: run Tempo's Scripts/SyncDeps.sh.", RequiredFile);
                }
            }
            PublicAdditionalLibraries.Add(LinkLibrary);
            RuntimeDependencies.Add(SharedLibrary);
        }
        else
        {
            // The one copy is in the executable, which links the static libraries like any others.
            int NumStaticLibraries = 0;
            if (Directory.Exists(LibrariesDirectory))
            {
                foreach (string StaticLibrary in Directory.EnumerateFiles(LibrariesDirectory, "*." + StaticLibraryExtension))
                {
                    if (Path.GetFileNameWithoutExtension(StaticLibrary) != "tempogrpc")
                    {
                        PublicAdditionalLibraries.Add(StaticLibrary);
                        ++NumStaticLibraries;
                    }
                }
            }
            if (NumStaticLibraries == 0)
            {
                throw new BuildException("No gRPC static libraries in {0}. Tempo needs a TempoThirdParty gRPC release: run Tempo's Scripts/SyncDeps.sh.", LibrariesDirectory);
            }
        }

        AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
        AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");
    }
}

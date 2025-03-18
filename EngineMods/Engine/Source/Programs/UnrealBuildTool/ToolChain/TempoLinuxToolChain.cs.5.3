// Copyright Tempo Simulation, LLC. All Rights Reserved.

// This file was/will be copied into the UnrealBuildTool directory during Tempo's setup.

using System;
using System.Collections.Generic;
using System.IO;
using EpicGames.Core;
using Microsoft.Extensions.Logging;
using UnrealBuildBase;

namespace UnrealBuildTool
{
    // We want dynamic libraries that link static libraries to re-export all the symbols of those libraries, so that
    // we only have one definition of each in our program. Otherwise, we encounter strange crashes related to global
    // symbols in gRPC and Protobuf.
    //
    // On Linux, we solve this by:
    // 1. The linker will only re-export symbols from static libraries that are used in the so. However we can
    //    force it to re-export symbols from certain libraries with the --whole-archive option. We store a list,
    //    in a .def file, along with the static libraries to specify which ones we want to re-export completely.
    //    The only reason it's not all of them is because some have repeated symbols, and the Mac linker does not
    //    support the --allow-multiple-definition option. The clang linker does, but we want to keep the Linux and
    //    Mac builds as similar as possible.
    class TempoLinuxToolChain : LinuxToolChain
    {
        public TempoLinuxToolChain(ReadOnlyTargetRules Target)
            : this(Target.Architecture, UEBuildPlatform.GetSDK(UnrealTargetPlatform.Linux) as LinuxPlatformSDK, TempoLinuxToolChainClangOptions(Target), Log.Logger)
        {
            
        }

        private TempoLinuxToolChain(UnrealArch InArchitecture, LinuxPlatformSDK InSDK, ClangToolChainOptions InOptions, ILogger InLogger)
            : base(InArchitecture, InSDK, InOptions, InLogger)
        {
            
        }
        
        public override FileItem[] LinkAllFiles(LinkEnvironment LinkEnvironment, bool bBuildImportLibraryOnly,
            IActionGraphBuilder Graph)
        {
            HashSet<string> ExportedLibraries = new HashSet<string>();
            foreach (var Library in LinkEnvironment.Libraries)
            {
                String LibraryPath = Library.ToString();
                if (Library.GetExtension() == ".def")
                {
                    ExportedLibraries = new HashSet<string>(FileReference.ReadAllLines(Library));
                }
            }
            
            foreach (var Library in LinkEnvironment.Libraries)
            {
                String LibraryPath = Library.ToString();
                if (ExportedLibraries.Contains(Library.GetFileName()))
                {
                    LinkEnvironment.AdditionalArguments += $" -Xlinker --whole-archive \"{LibraryPath}\"";
                }
            }

            LinkEnvironment.AdditionalArguments += " -Xlinker --allow-multiple-definition";
            
            LinkEnvironment.Libraries.RemoveAll(Library => Library.GetExtension() == ".def");
            
            return base.LinkAllFiles(LinkEnvironment, bBuildImportLibraryOnly, Graph);
        }
        
        // Copied from LinuxPlatform. Cannot figure out a way to get these from LinuxPlatform.CreateToolChain
        static ClangToolChainOptions TempoLinuxToolChainClangOptions(ReadOnlyTargetRules Target)
        {
            ClangToolChainOptions Options = ClangToolChainOptions.None;

            if (Target.LinuxPlatform.bEnableAddressSanitizer)
            {
                Options |= ClangToolChainOptions.EnableAddressSanitizer;

                if (Target.LinkType != TargetLinkType.Monolithic)
                {
                    Options |= ClangToolChainOptions.EnableSharedSanitizer;
                }
            }
            if (Target.LinuxPlatform.bEnableThreadSanitizer)
            {
                Options |= ClangToolChainOptions.EnableThreadSanitizer;

                if (Target.LinkType != TargetLinkType.Monolithic)
                {
                    throw new BuildException("Thread Sanitizer (TSan) unsupported for non-monolithic builds");
                }
            }
            if (Target.LinuxPlatform.bEnableUndefinedBehaviorSanitizer)
            {
                Options |= ClangToolChainOptions.EnableUndefinedBehaviorSanitizer;

                if (Target.LinkType != TargetLinkType.Monolithic)
                {
                    Options |= ClangToolChainOptions.EnableSharedSanitizer;
                }
            }
            if (Target.LinuxPlatform.bEnableMemorySanitizer)
            {
                Options |= ClangToolChainOptions.EnableMemorySanitizer;

                if (Target.LinkType != TargetLinkType.Monolithic)
                {
                    throw new BuildException("Memory Sanitizer (MSan) unsupported for non-monolithic builds");
                }
            }
            if (Target.LinuxPlatform.bDisableDumpSyms)
            {
                Options |= ClangToolChainOptions.DisableDumpSyms;
            }
            if (Target.LinuxPlatform.bEnableLibFuzzer)
            {
                Options |= ClangToolChainOptions.EnableLibFuzzer;

                if (Target.LinkType != TargetLinkType.Monolithic)
                {
                    throw new BuildException("LibFuzzer is unsupported for non-monolithic builds.");
                }
            }

            if (((LinuxPlatform)UEBuildPlatform.GetBuildPlatform(UnrealTargetPlatform.Linux)).IsLTOEnabled(Target))
            {
                Options |= ClangToolChainOptions.EnableLinkTimeOptimization;

                if (Target.bPreferThinLTO)
                {
                    Options |= ClangToolChainOptions.EnableThinLTO;
                }
            }
            else if (Target.bPreferThinLTO)
            {
                // warn about ThinLTO not being useful on its own
                Log.Logger.LogWarning("Warning: bPreferThinLTO is set, but LTO is disabled. Flag will have no effect");
            }

            if (Target.bUseAutoRTFMCompiler)
            {
                Options |= ClangToolChainOptions.UseAutoRTFMCompiler;
            }

            if (Target.LinuxPlatform.bTuneDebugInfoForLLDB)
            {
                Options |= ClangToolChainOptions.TuneDebugInfoForLLDB;
            }

            if (Target.LinuxPlatform.bPreservePSYM)
            {
                Options |= ClangToolChainOptions.PreservePSYM;
            }

            // Disable color logging if we are on a build machine
            if (Environment.GetEnvironmentVariable("IsBuildMachine") == "1")
            {
                Log.ColorConsoleOutput = false;
            }

            return Options;
        }
    }
}
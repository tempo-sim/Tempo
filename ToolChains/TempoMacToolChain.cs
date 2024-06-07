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
    // On Mac, we solve this by:
    // 1. The linker will only re-export symbols from static libraries that are used in the dylib. However we can
    //    force it to re-export symbols from certain libraries with the -force_load option. We store a list,
    //    in a .def file, along with the static libraries to specify which ones we want to re-export completely.
    //    The only reason it's not all of them is because some have repeated symbols, and the Mac linker does not
    //    support the --allow-multiple-definition option.
    class TempoMacToolChain : MacToolChain
    {
        public TempoMacToolChain(ReadOnlyTargetRules Target, ILogger Logger)
            : this(Target, TempoMacToolChainClangOptions(Target), Logger)
        {
            
        }

        private TempoMacToolChain(ReadOnlyTargetRules Target, ClangToolChainOptions InOptions, ILogger Logger)
            : base(Target, InOptions, Logger)
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
                    LinkEnvironment.AdditionalArguments += $" -force_load \"{LibraryPath}\"";
                }
            }
            
            LinkEnvironment.Libraries.RemoveAll(Library => Library.GetExtension() == ".def");
            
            return base.LinkAllFiles(LinkEnvironment, bBuildImportLibraryOnly, Graph);
        }
        
        // Copied from MacPlatform. Cannot figure out a way to get these from MacPlatform.CreateToolChain
        static ClangToolChainOptions TempoMacToolChainClangOptions(ReadOnlyTargetRules Target)
        {
            ClangToolChainOptions Options = ClangToolChainOptions.None;

            string? AddressSanitizer = Environment.GetEnvironmentVariable("ENABLE_ADDRESS_SANITIZER");
            string? ThreadSanitizer = Environment.GetEnvironmentVariable("ENABLE_THREAD_SANITIZER");
            string? UndefSanitizerMode = Environment.GetEnvironmentVariable("ENABLE_UNDEFINED_BEHAVIOR_SANITIZER");

            if (Target.MacPlatform.bEnableAddressSanitizer || (AddressSanitizer != null && AddressSanitizer == "YES"))
            {
                Options |= ClangToolChainOptions.EnableAddressSanitizer;
            }
            if (Target.MacPlatform.bEnableThreadSanitizer || (ThreadSanitizer != null && ThreadSanitizer == "YES"))
            {
                Options |= ClangToolChainOptions.EnableThreadSanitizer;
            }
            if (Target.MacPlatform.bEnableUndefinedBehaviorSanitizer || (UndefSanitizerMode != null && UndefSanitizerMode == "YES"))
            {
                Options |= ClangToolChainOptions.EnableUndefinedBehaviorSanitizer;
            }
            if (Target.bShouldCompileAsDLL)
            {
                Options |= ClangToolChainOptions.OutputDylib;
            }

            return Options;
        }
    }
}

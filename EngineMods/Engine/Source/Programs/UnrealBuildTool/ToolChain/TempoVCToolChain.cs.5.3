// Copyright Tempo Simulation, LLC. All Rights Reserved.

// This file was/will be copied into the UnrealBuildTool directory during Tempo's setup.

using System;
using System.Collections.Generic;
using System.IO;
using EpicGames.Core;
using UnrealBuildBase;

namespace UnrealBuildTool
{
    // We want dynamic libraries that link static libraries to re-export all the symbols of those libraries, so that
    // we only have one definition of each in our program. Otherwise, we encounter strange crashes related to global
    // symbols in gRPC and Protobuf.
    //
    // On Windows, we solve this by:
    // 1. UnrealBuildTool, for any "cross-referenced" module and always on Windows, explicitly creates an import
    //    library for the dll before the actual link step (and then pushes aside the one created during linking).
    //    VCToolChain does not include dependent static libraries in the import library it explicitly creates,
    //    (even though they will be in the one the linker makes) so we add them ourselves.
    // 2. The linker will only re-export symbols from static libraries that are used in the dll. The option
    //    /WHOLEARCHIVE advertises that it "can" be used to re-export, which would make it equivalent to the
    //    -force_load and --whole-archive options on Mac and Linux, but from my testing it does not get all symbols.
    //    So instead we include a .def file with our ThirdParty dependencies that includes all the symbols from
    //    all the libraries (deduplicated, with many exceptions, its complicated) and explicitly export them all.s
    class TempoVCToolChain : VCToolChain
    {
        public TempoVCToolChain(ReadOnlyTargetRules Target)
            : base(Target, Log.Logger)
        {
            
        }
        
        public override FileItem LinkFiles(LinkEnvironment LinkEnvironment, bool bBuildImportLibraryOnly,
            IActionGraphBuilder Graph)
        {
            foreach (var Library in LinkEnvironment.Libraries)
            {
                String LibraryPath = Library.ToString();

                if (bBuildImportLibraryOnly)
                {
                    if (LibraryPath.Contains("Tempo") && LibraryPath.Contains("Source") && Library.GetExtension() != ".def")
                    {
                        LinkEnvironment.InputFiles.Add(FileItem.GetItemByFileReference(Library));
                    }
                }
                
                if (Library.GetExtension() == ".def")
                {
                    LinkEnvironment.ModuleDefinitionFile = LibraryPath;
                }
            }

            LinkEnvironment.Libraries.RemoveAll(Library => Library.GetExtension() == ".def");
        
            return base.LinkFiles(LinkEnvironment, bBuildImportLibraryOnly, Graph);
        }

        protected override void ModifyFinalLinkArguments(LinkEnvironment LinkEnvironment, List<string> Arguments, bool bBuildImportLibraryOnly)
        {
            base.ModifyFinalLinkArguments(LinkEnvironment, Arguments, bBuildImportLibraryOnly);

            if (LinkEnvironment.ModuleDefinitionFile != null && LinkEnvironment.ModuleDefinitionFile.Length > 0)
            {
                Arguments.RemoveAll(Argument => Argument.StartsWith("/DEF"));
                Arguments.Add($"/DEF:\"{LinkEnvironment.ModuleDefinitionFile}\"");
                // When we specify the symbols explicitly through the def file the linker still finds some of them
                // through the libraries themselves and warns that they've been specified multiple times.
                // I think these warnings are benign, so we ignore them here.
                Arguments.Add("/IGNORE:4197");
            }
        }
    }
}

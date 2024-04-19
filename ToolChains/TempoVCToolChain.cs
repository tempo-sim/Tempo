// Copyright Tempo Simulation, LLC. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.Versioning;
using System.Text;
using EpicGames.Core;
using Microsoft.CodeAnalysis;
using Microsoft.Extensions.Logging;
using UnrealBuildBase;

namespace UnrealBuildTool
{
    class TempoVCToolChain : VCToolChain
    {
        public TempoVCToolChain(ReadOnlyTargetRules Target)
            : base(Target, Log.Logger)
        {
        }

        public override FileItem LinkFiles(LinkEnvironment LinkEnvironment, bool bBuildImportLibraryOnly,
            IActionGraphBuilder Graph)
        {
            if (bBuildImportLibraryOnly)
            {
                foreach (var Library in LinkEnvironment.Libraries)
                {
                    String LibraryPath = Library.ToString();
                    if (LibraryPath.Contains("Source"))
                    {
                        LinkEnvironment.InputFiles.Add(FileItem.GetItemByFileReference(Library));
                    }
                }
            }
            
            return base.LinkFiles(LinkEnvironment, bBuildImportLibraryOnly, Graph);
        }
    }
}

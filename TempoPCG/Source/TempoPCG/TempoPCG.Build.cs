using UnrealBuildTool;

public class TempoPCG : ModuleRules
{
    public TempoPCG(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "PCG",
                "Slate",
                "SlateCore",
                "TempoCoreShared"
            }
        );
    }
}

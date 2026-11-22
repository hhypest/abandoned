using UnrealBuildTool;

public class Abandoned : ModuleRules
{
    public Abandoned(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;

        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("UnrealEd");
        }

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AnimationCore",
            "Core",
            "CoreUObject",
            "Engine",
            "EnhancedInput",
            "InputCore",
            "Json",
            "RenderCore",
            "Slate",
            "SlateCore",
            "UMG",
        });
    }
}

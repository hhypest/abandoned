using UnrealBuildTool;

public class AbandonedTarget : TargetRules
{
    public AbandonedTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
        ExtraModuleNames.Add("Abandoned");
    }
}

using UnrealBuildTool;

public class AbandonedEditorTarget : TargetRules
{
    public AbandonedEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
        ExtraModuleNames.Add("Abandoned");
    }
}

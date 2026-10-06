using UnrealBuildTool;
using System.Collections.Generic;
public class UEBridgeEditorTarget : TargetRules {
    public UEBridgeEditorTarget(TargetInfo Target) : base(Target) {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        ExtraModuleNames.Add("UEBridge");
    }
}

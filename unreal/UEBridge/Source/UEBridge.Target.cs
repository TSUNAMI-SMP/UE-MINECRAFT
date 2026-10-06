using UnrealBuildTool;
using System.Collections.Generic;
public class UEBridgeTarget : TargetRules {
    public UEBridgeTarget(TargetInfo Target) : base(Target) {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        ExtraModuleNames.Add("UEBridge");
    }
}

using UnrealBuildTool;
public class UEBridge : ModuleRules {
    public UEBridge(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "InputCore", "Sockets", "Networking",
            "Json", "Niagara", "GeometryCollectionEngine", "FieldSystemEngine", "ChaosSolverEngine", "ImageWrapper", "RenderCore", "RHI", "ProceduralMeshComponent"
        });
        if (Target.Platform == UnrealTargetPlatform.Win64) {
            PublicSystemLibraries.AddRange(new string[] { "d3d11.lib", "dxgi.lib", "d3dcompiler.lib" });
        }
    }
}

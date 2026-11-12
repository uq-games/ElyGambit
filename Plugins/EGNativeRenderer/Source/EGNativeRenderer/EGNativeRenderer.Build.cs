using UnrealBuildTool;
using System.IO;

public class EGNativeRenderer : ModuleRules
{
    public EGNativeRenderer(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "RHI" });
        PrivateDependencyModuleNames.AddRange(new[] { "RenderCore", "Renderer", "Projects" });
        // Mesh collection and scene uniforms have no equivalent public plugin API.
        PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Private"));
        PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Internal"));
    }
}

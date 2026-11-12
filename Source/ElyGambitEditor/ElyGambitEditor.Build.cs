using UnrealBuildTool;

public class ElyGambitEditor : ModuleRules
{
 public ElyGambitEditor(ReadOnlyTargetRules Target) : base(Target)
 {
  bUseUnity = false;
  PrivateDependencyModuleNames.Add("AudioMixer"); // Board presentation audio capture.
  PrivateDependencyModuleNames.Add("MediaAssets"); // Ending playback integration checks.
  PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
  PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "ElyGambit", "UMG" });
  PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "UMGEditor", "KismetCompiler", "AssetTools", "AssetRegistry", "Json", "MeshDescription", "StaticMeshDescription", "SkeletalMeshDescription", "AnimGraph", "BlueprintGraph", "Slate", "SlateCore", "InputCore", "RenderCore", "RHI", "GameplayAbilities", "GameplayTags", "GameplayTasks", "Niagara", "EGNativeRenderer", "KawaiiPhysics", "KawaiiPhysicsEd" });
 }
}

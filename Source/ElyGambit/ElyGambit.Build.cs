// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ElyGambit : ModuleRules
{
	public ElyGambit(ReadOnlyTargetRules Target) : base(Target)
	{
		// Keep non-unity compilation until both targets have been validated after the module split.
		bUseUnity = false;
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"GameplayAbilities", "GameplayTags", "GameplayTasks", "UMG", "SlateCore"
		});

		// AI/navigation types are used by implementation files only.
		PrivateDependencyModuleNames.AddRange(new string[] { "AIModule", "NavigationSystem", "Slate", "KawaiiPhysics", "RHI", "RenderCore" });
		PrivateDependencyModuleNames.Add("EGNativeRenderer");
		PrivateDependencyModuleNames.AddRange(new string[] { "LevelSequence", "MovieScene", "MediaAssets", "MoviePlayer", "AudioMixer" });
		PrivateDependencyModuleNames.Add("Niagara"); // Isolated Vill-V FXs source preview; no combat dependency.
		if (Target.Platform == UnrealTargetPlatform.Win64) PublicSystemLibraries.Add("dxgi.lib");
		if (Target.bBuildEditor)
		{
            PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "Json", "MeshDescription", "SkeletalMeshDescription" });
		}
	}
}

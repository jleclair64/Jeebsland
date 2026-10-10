// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Jeebsland : ModuleRules
{
	public Jeebsland(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "MeshDescription", "StaticMeshDescription", "ProceduralMeshComponent",
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Jeebsland",
			"Jeebsland/Variant_Platforming",
			"Jeebsland/Variant_Platforming/Animation",
			"Jeebsland/Variant_Combat",
			"Jeebsland/Variant_Combat/AI",
			"Jeebsland/Variant_Combat/Animation",
			"Jeebsland/Variant_Combat/Gameplay",
			"Jeebsland/Variant_Combat/Interfaces",
			"Jeebsland/Variant_Combat/UI",
			"Jeebsland/Variant_SideScrolling",
			"Jeebsland/Variant_SideScrolling/AI",
			"Jeebsland/Variant_SideScrolling/Gameplay",
			"Jeebsland/Variant_SideScrolling/Interfaces",
			"Jeebsland/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}



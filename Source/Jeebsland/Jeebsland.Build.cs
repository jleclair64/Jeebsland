// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Jeebsland : ModuleRules
{
	public Jeebsland(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Compile each .cpp on its own. Several files share helper names at file scope (e.g. FRing),
		// which clash when Unreal merges unchanged files into combined "unity" files.
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[] { "MeshDescription", "StaticMeshDescription", "ProceduralMeshComponent",
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"Slate",
			"SlateCore",
			"HTTP",          // the starting screen asks each world how many players it has
			"HTTPServer"     // and each world answers
		});

		// Password hashing on the server (PBKDF2-SHA256)
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Jeebsland"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}



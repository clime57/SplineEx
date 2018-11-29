// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class SplineExEditor : ModuleRules
{
	public SplineExEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		//PublicIncludePaths.AddRange(
		//	new string[] {
		//		"SplineEx",	
		//		"SplineEx/Public"
		//		// ... add public include paths required here ...
		//	});
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"SplineEx"
				// ... add other public dependencies that you statically link with here ...
			});
			
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });

        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "Slate",
                "SlateCore",
                "UnrealEd",
                "PropertyEditor",
                "EditorStyle",
                //"AIModule",
                "ViewportInteraction",
                "DetailCustomizations"
            }
        );
        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));
    }
}

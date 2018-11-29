using UnrealBuildTool;
using System.IO;

public class SplineEx : ModuleRules {
	public SplineEx(ReadOnlyTargetRules Target) : base(Target) {
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bEnforceIWYU = true;
		//
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"Engine",
				"CoreUObject"
			}
		);
		//
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory,"Public"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory,"Private"));
	}///
}
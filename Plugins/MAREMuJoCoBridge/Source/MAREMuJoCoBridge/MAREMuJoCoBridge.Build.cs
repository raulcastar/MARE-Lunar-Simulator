using System.IO;
using UnrealBuildTool;

public class MAREMuJoCoBridge : ModuleRules
{
	public MAREMuJoCoBridge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Sockets", "Networking", "Json", "UnrealSimClock" });

		// URLab is an optional git submodule. UnrealBuildTool reads these rules even while the
		// plugin is disabled, so they must resolve without it; the bridge is only built with URLab.
		if (File.Exists(Path.Combine(PluginDirectory, "..", "UnrealRoboticsLab", "UnrealRoboticsLab.uplugin")))
		{
			PublicDependencyModuleNames.Add("URLab");
		}
	}
}

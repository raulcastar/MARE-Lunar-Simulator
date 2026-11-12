using UnrealBuildTool;

public class MAREMuJoCoBridge : ModuleRules
{
	public MAREMuJoCoBridge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "URLab" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Sockets", "Networking", "Json", "UnrealSimClock" });
	}
}

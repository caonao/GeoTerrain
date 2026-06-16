using UnrealBuildTool;

public class GeoTerrainEditor : ModuleRules
{
    public GeoTerrainEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "CoreUObject",
            "Engine",
            "Slate",
            "SlateCore",
            "UnrealEd",
            "LevelEditor",
            "ToolMenus",
            "InputCore",
            "EditorFramework",
            // Phase 2: HTTP tile download + image decode + landscape
            "HTTP",
            "ImageWrapper",
            "Json",
            "JsonUtilities",
            "Landscape",
            "LandscapeEditor",
            // Phase 4: foliage + material layers
            "Foliage",
        });
    }
}

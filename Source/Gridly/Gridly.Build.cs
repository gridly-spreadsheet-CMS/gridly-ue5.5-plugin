// Copyright (c) 2021 LocalizeDirect AB

using UnrealBuildTool;

public class Gridly : ModuleRules
{
	public Gridly(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
            "Json",
            "HTTP"
            
        });

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{							
				"InputCore",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",                
                "Serialization",				
				"JsonUtilities"
            }
			);

		if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "Settings",
                    "Localization"
                }
                );
        }
	}
}

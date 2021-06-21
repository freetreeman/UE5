// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class TextToSpeech : ModuleRules
{
	public TextToSpeech(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] 
			{
				"Core",
				"CoreUObject"
			}
		);
	
		PublicIncludePathModuleNames.AddRange(
			new string[] 
			{
			// For access to platform includes to support TTS
			// E.g Mac and IOS TTS requires Apple's frameworks
				"ApplicationCore"
			}
		);
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Engine"
			}
		);
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] 
				{
					"AudioMixer",
					"SignalProcessing",
					"AudioPlatformConfiguration"
				}
			);
			AddEngineThirdPartyPrivateStaticDependencies(Target, "Flite");
		}
		PrivateDefinitions.Add("USING_FLITE=" + (IsFliteUsed() ? "1" : "0"));
	}
	// platforms that inherit from this should override and return true if they intend to use Flite
	protected virtual bool IsFliteUsed()
	{
		// for now only Win64 needs to use Flite. Can be expanded to Android etc in future
		return Target.Platform == UnrealTargetPlatform.Win64;
}
}

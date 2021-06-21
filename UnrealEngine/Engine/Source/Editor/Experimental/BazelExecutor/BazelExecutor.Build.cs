// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class BazelExecutor : ModuleRules
	{
		public BazelExecutor(ReadOnlyTargetRules Target) : base(Target)
		{
			if (Target.Platform != UnrealTargetPlatform.Win64)
			{
				// Grpc is currently only supported for Win64, Linux, & LinuxAArch64, set to external to prevent compiling for any other platform
				Type = ModuleType.External;
				PrecompileForTargets = PrecompileTargetsType.None;
				return;
			}

			bUseUnity = false;

			PrivateDefinitions.Add("GPR_FORBID_UNREACHABLE_CODE=0");

			PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Generated"));

			PrivateDependencyModuleNames.AddRange(
				new string[] {
				"Core",
				"CoreUObject",
				"Settings",
				"Grpc",
				"RemoteExecution",
				}
			);
		}
	}
}

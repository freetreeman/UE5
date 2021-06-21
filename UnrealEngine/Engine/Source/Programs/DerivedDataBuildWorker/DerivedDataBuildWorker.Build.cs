// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// Abstract base class for worker targets.  Not a valid target by itself, hence it is not put into a *.target.cs file.
[SupportedPlatforms(UnrealPlatformClass.Desktop)]
public abstract class DerivedDataBuildWorkerTarget : TargetRules
{
	public DerivedDataBuildWorkerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Program;
		LinkType = TargetLinkType.Monolithic;

		SolutionDirectory = "Programs/BuildWorker";

		// TODO: This is temporary to allow DDCUtils to not cause build errors when referenced
		//		 by the build workers.  In the future the use of DDC should strip out backends
		//		 and DDCUTils, so it will no longer be required to set bLegalToDistributeBinary.
		bLegalToDistributeBinary		= true;
		bUseXGEController				= false;
		bCompileFreeType				= false;
		bLoggingToMemoryEnabled			= true;
		bUseLoggingInShipping			= true;
		bCompileWithAccessibilitySupport= false;
		bWithServerCode					= false;
		bCompileNavmeshClusterLinks		= false;
		bCompileNavmeshSegmentLinks		= false;
		bCompileRecast					= false;
		bCompileICU 					= false;
		bCompileChaos					= false;
		bUseChaos						= false;
		bWithLiveCoding					= false;
		bBuildDeveloperTools			= false;
		bUseMallocProfiler				= false;
		bBuildWithEditorOnlyData		= true;
		bCompileAgainstEngine			= false;
		bCompileAgainstCoreUObject		= false;
		bCompileAgainstApplicationCore	= false;
		bUsesSlate = false;
		bIsBuildingConsoleApplication	= true;
		// TODO: I want to use static CRT in the future, but it causes link issues today likely due to 3rd party libraries
		//bUseStaticCRT					= true;

		WindowsPlatform.bUseBundledDbgHelp = false;
		WindowsPlatform.bPixProfilingEnabled = false;
		WindowsPlatform.bMergeIdenticalCOMDATs = true;
		WindowsPlatform.bStripUnreferencedSymbols = true;

		// Disable logging, as the workers are spawned often and logging will just slow them down
		GlobalDefinitions.Add("ALLOW_LOG_FILE=0");
		// Linking against wer.lib/wer.dll causes XGE to bail when the worker is run on a Windows 8 machine, so turn this off.
		GlobalDefinitions.Add("ALLOW_WINDOWS_ERROR_REPORT_LIB=0");
		// Disable external profiling in TextureBuilder to improve startup time
		GlobalDefinitions.Add("UE_EXTERNAL_PROFILING_ENABLED=0");
	}
}

public class DerivedDataBuildWorker : ModuleRules
{
	public DerivedDataBuildWorker(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePaths.Add("Runtime/Launch/Public");
		PublicIncludePaths.Add("Developer/DerivedDataCache/Public");

		PrivateIncludePaths.Add("Runtime/Launch/Private");		// For LaunchEngineLoop.cpp include

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"Projects",
				"DerivedDataCache",
			});

		if (Target.bCompileAgainstApplicationCore)
		{
			PrivateDependencyModuleNames.Add("ApplicationCore");
		}
	}
}

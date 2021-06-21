// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Text;

namespace UnrealBuildBase
{
	public static class Unreal
	{
		private static DirectoryReference FindRootDirectory()
		{
			if (LocationOverride.RootDirectory != null)
			{
				return LocationOverride.RootDirectory;
			}

			// This base library may be used - and so be launched - from more than one location (at time of writing, UnrealBuildTool and AutomationTool)
			// Programs that use this assembly must be located under "Engine/Binaries/DotNET" and so we look for that sequence of directories in that path of the executing assembly
			
			// Use the EntryAssembly (the application path), rather than the ExecutingAssembly (the library path)
			string AssemblyLocation = Assembly.GetEntryAssembly().GetOriginalLocation();

			DirectoryReference? FoundRootDirectory = DirectoryReference.FindCorrectCase(DirectoryReference.FromString(AssemblyLocation)!);

			// Search up through the directory tree for the deepest instance of the sub-path "Engine/Binaries/DotNET"
			while(FoundRootDirectory != null)
			{
				if (String.Equals("DotNET", FoundRootDirectory.GetDirectoryName()))
				{
					FoundRootDirectory = FoundRootDirectory.ParentDirectory;
					if (FoundRootDirectory != null && String.Equals("Binaries", FoundRootDirectory.GetDirectoryName()))
					{
						FoundRootDirectory = FoundRootDirectory.ParentDirectory;
						if (FoundRootDirectory != null && String.Equals("Engine", FoundRootDirectory.GetDirectoryName()))
						{
							FoundRootDirectory = FoundRootDirectory.ParentDirectory;
							break;
						}
						continue;
					}
					continue;
				}
				FoundRootDirectory = FoundRootDirectory.ParentDirectory;
			}

			if (FoundRootDirectory == null)
			{
				throw new Exception($"The BuildUtilities assembly requires that applications are launched from a path containing \"Engine/Binaries/DotNET\". This application was launched from {Path.GetDirectoryName(AssemblyLocation)}");
			}

			// Confirm that we've found a valid root directory, by testing for the existence of a well-known file
			FileReference ExpectedExistingFile = FileReference.Combine(FoundRootDirectory, "Engine", "Build", "Build.version");
			if (!FileReference.Exists(ExpectedExistingFile))
			{
				throw new Exception($"Expected file \"Engine/Build/Build.version\" was not found at {ExpectedExistingFile.FullName}");
			}

			return FoundRootDirectory;
		}

		private static FileReference FindUnrealBuildTool()
		{
			// todo: use UnrealBuildTool.dll (same on all platforms). Will require changes wherever UnrealBuildTool is invoked.
			string UBTName = "UnrealBuildTool" + RuntimePlatform.ExeExtension;

			// the UnrealBuildTool executable is assumed to be located under {RootDirectory}/Engine/Binaries/DotNET/UnrealBuildTool/
			FileReference UnrealBuildToolPath = FileReference.Combine(EngineDirectory, "Binaries", "DotNET", "UnrealBuildTool", UBTName);
			
			UnrealBuildToolPath = FileReference.FindCorrectCase(UnrealBuildToolPath);

			if (!FileReference.Exists(UnrealBuildToolPath))
			{
				throw new Exception($"Unable to find {UBTName} in the expected location at {UnrealBuildToolPath.FullName}");
			}

			return UnrealBuildToolPath;
		}

		static private DirectoryReference FindDotnetDirectory()
		{
			string HostDotNetDirectoryName;
			switch (RuntimePlatform.Current)
			{
				case RuntimePlatform.Type.Windows: HostDotNetDirectoryName = "Windows"; break;
				case RuntimePlatform.Type.Mac:     HostDotNetDirectoryName = "Mac"; break;
				case RuntimePlatform.Type.Linux:   HostDotNetDirectoryName = "Linux"; break;
				default: throw new Exception("Unknown host platform");
			}

			return DirectoryReference.Combine(EngineDirectory, "Binaries", "ThirdParty", "DotNet", HostDotNetDirectoryName);
		}

		/// <summary>
		/// The full name of the root UE directory
		/// </summary>
		public static readonly DirectoryReference RootDirectory = FindRootDirectory();

		/// <summary>
		/// The full name of the Engine directory
		/// </summary>
		public static readonly DirectoryReference EngineDirectory = DirectoryReference.Combine(RootDirectory, "Engine");

		/// <summary>
		/// The path to UBT
		/// </summary>
		public static readonly FileReference UnrealBuildToolPath = FindUnrealBuildTool();

		/// <summary>
		/// The directory containing the bundled .NET installation
		/// </summary>
		static public readonly DirectoryReference DotnetDirectory = FindDotnetDirectory();

		/// <summary>
		/// The path of the bundled dotnet executable
		/// </summary>
		static public readonly FileReference DotnetPath = FileReference.Combine(DotnetDirectory, "dotnet" + RuntimePlatform.ExeExtension);

		/// <summary>
		/// Whether we're running with engine installed
		/// </summary>
		static private bool? bIsEngineInstalled;

		/// <summary>
		/// Returns true if UnrealBuildTool is running using installed Engine components
		/// </summary>
		/// <returns>True if running using installed Engine components</returns>
		static public bool IsEngineInstalled()
		{
			if (!bIsEngineInstalled.HasValue)
			{
				bIsEngineInstalled = FileReference.Exists(FileReference.Combine(EngineDirectory, "Build", "InstalledBuild.txt"));
			}
			return bIsEngineInstalled.Value;
		}

		public static class LocationOverride 
		{
			/// <summary>
			/// If set, this value will be used to populate Unreal.RootDirectory
			/// </summary>
			public static DirectoryReference? RootDirectory = null;
		}
	}
}

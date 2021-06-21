// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Perforce.Managed
{
	/// <summary>
	/// Interface for a stream snapshot
	/// </summary>
	public abstract class StreamSnapshot
	{
		/// <summary>
		/// Empty snapshot instance
		/// </summary>
		public static StreamSnapshot Empty => new StreamSnapshotFromMemory(new StreamSnapshotBuilder());

		/// <summary>
		/// The root digest
		/// </summary>
		public abstract IoHash Root { get; }

		/// <summary>
		/// Lookup a directory by digest
		/// </summary>
		/// <param name="Hash">The hash value</param>
		/// <returns></returns>
		public abstract StreamDirectoryInfo Lookup(IoHash Hash);
	}

	/// <summary>
	/// Extension methods for IStreamSnapshot
	/// </summary>
	static class StreamSnapshotExtensions
	{
		/// <summary>
		/// Get all the files in this directory
		/// </summary>
		/// <returns>List of files</returns>
		public static List<StreamFileInfo> GetFiles(this StreamSnapshot Snapshot)
		{
			List<StreamFileInfo> Files = new List<StreamFileInfo>();
			AppendFiles(Snapshot, Snapshot.Root, Files);
			return Files;
		}

		/// <summary>
		/// Append the contents of this directory and subdirectories to a list
		/// </summary>
		/// <param name="Files">List to append to</param>
		static void AppendFiles(StreamSnapshot Snapshot, IoHash DirHash, List<StreamFileInfo> Files)
		{
			StreamDirectoryInfo DirectoryInfo = Snapshot.Lookup(DirHash);
			foreach (IoHash SubDirHash in DirectoryInfo.NameToSubDirectory.Values)
			{
				AppendFiles(Snapshot, SubDirHash, Files);
			}
			Files.AddRange(DirectoryInfo.NameToFile.Values);
		}
	}
}

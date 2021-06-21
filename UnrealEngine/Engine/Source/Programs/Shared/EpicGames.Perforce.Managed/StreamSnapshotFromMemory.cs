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
	/// Stores the contents of a stream in memory
	/// </summary>
	public class StreamSnapshotFromMemory : StreamSnapshot
	{
		/// <summary>
		/// The current signature for saved directory objects
		/// </summary>
		static readonly byte[] CurrentSignature = { (byte)'W', (byte)'S', (byte)'D', 3 };

		/// <summary>
		/// The root digest
		/// </summary>
		public override IoHash Root { get; }

		/// <summary>
		/// Map of digest to directory
		/// </summary>
		public IReadOnlyDictionary<IoHash, StreamDirectoryInfo> HashToDirectory { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Root"></param>
		/// <param name="HashToDirectory"></param>
		public StreamSnapshotFromMemory(IoHash Root, Dictionary<IoHash, StreamDirectoryInfo> HashToDirectory)
		{
			this.Root = Root;
			this.HashToDirectory = HashToDirectory;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Builder"></param>
		public StreamSnapshotFromMemory(StreamSnapshotBuilder Builder)
		{
			Dictionary<IoHash, StreamDirectoryInfo> HashToDirectory = new Dictionary<IoHash, StreamDirectoryInfo>();
			this.Root = Builder.Encode(HashToDirectory);
			this.HashToDirectory = HashToDirectory;
		}

		/// <inheritdoc/>
		public override StreamDirectoryInfo Lookup(IoHash Hash)
		{
			return HashToDirectory[Hash];
		}

		/// <summary>
		/// Load a stream directory from a file on disk
		/// </summary>
		/// <param name="InputFile">File to read from</param>
		/// <param name="CancellationToken">Cancellation token</param>
		/// <returns>New StreamDirectoryInfo object</returns>
		public static async Task<StreamSnapshotFromMemory?> TryLoadAsync(FileReference InputFile, CancellationToken CancellationToken)
		{
			byte[] Data = await FileReference.ReadAllBytesAsync(InputFile);
			if (!Data.AsSpan().StartsWith(CurrentSignature))
			{
				return null;
			}

			MemoryReader Reader = new MemoryReader(Data.AsMemory(CurrentSignature.Length));

			IoHash Root = Reader.ReadIoHash();

			int NumItems = Reader.ReadInt32();

			Dictionary<IoHash, StreamDirectoryInfo> HashToDirectory = new Dictionary<IoHash, StreamDirectoryInfo>(NumItems);
			for (int Idx = 0; Idx < NumItems; Idx++)
			{
				IoHash Hash = Reader.ReadIoHash();
				StreamDirectoryInfo Info = Reader.ReadStreamDirectoryInfo();
				HashToDirectory[Hash] = Info;
			}

			return new StreamSnapshotFromMemory(Root, HashToDirectory);
		}

		/// <summary>
		/// Saves the contents of this object to disk
		/// </summary>
		/// <param name="OutputFile">The output file to write to</param>
		public async Task Save(FileReference OutputFile)
		{
			int Length = Digest<Sha1>.Length + sizeof(int) + HashToDirectory.Sum(x => Digest<Sha1>.Length + x.Value.GetSerializedSize());
			byte[] Data = new byte[Length];

			MemoryWriter Writer = new MemoryWriter(Data.AsMemory());
			Writer.WriteIoHash(Root);
			Writer.WriteInt32(HashToDirectory.Count);
			foreach ((IoHash Hash, StreamDirectoryInfo Info) in HashToDirectory)
			{
				Writer.WriteIoHash(Hash);
				Writer.WriteStreamDirectoryInfo(Info);
			}
			Writer.CheckOffset(Data.Length);

			using (FileStream OutputStream = FileReference.Open(OutputFile, FileMode.Create, FileAccess.Write, FileShare.Read))
			{
				await OutputStream.WriteAsync(CurrentSignature, 0, CurrentSignature.Length);
				await OutputStream.WriteAsync(Data, 0, Data.Length);
			}
		}
	}
}

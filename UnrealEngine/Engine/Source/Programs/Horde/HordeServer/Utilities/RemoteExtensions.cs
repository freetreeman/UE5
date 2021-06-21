// Copyright Epic Games, Inc. All Rights Reserved.

using Build.Bazel.Remote.Execution.V2;
using HordeServer.Storage;
using HordeServer.Storage.Primitives;
using Google.Protobuf;
using Google.Protobuf.Reflection;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using EpicGames.Core;

using Digest = Build.Bazel.Remote.Execution.V2.Digest;

namespace HordeServer.Utility
{
	static class RemoteExtensions
	{
		public static IoHash ToHashValue(this Digest Digest)
		{
			return IoHash.Parse(Digest.Hash);
		}

		public static void MergeFrom<T>(this IMessage<T> Message, ReadOnlyMemory<byte> Memory) where T : IMessage<T>
		{
			// TODO: optimize this to avoid array allocation
			Message.MergeFrom(Memory.ToArray());
		}

		public static T ParseFrom<T>(this MessageParser<T> Parser, ReadOnlyMemory<byte> Memory) where T : IMessage<T>
		{
			// TODO: optimize this to avoid array allocation
			return Parser.ParseFrom(Memory.ToArray());
		}

		public static async Task<T> GetProtoMessageAsync<T>(this IStorageService StorageProvider, IoHash HashValue, DateTime Deadline = default) where T : class, IMessage<T>, new()
		{
			T? Result = await TryGetProtoMessageAsync<T>(StorageProvider, HashValue, Deadline);
			if (Result == null)
			{
				throw new KeyNotFoundException($"Unable to get blob {HashValue}");
			}
			return Result;
		}

		public static async Task<T?> TryGetProtoMessageAsync<T>(this IStorageService StorageProvider, IoHash HashValue, DateTime Deadline = default) where T : class, IMessage<T>, new()
		{
			ReadOnlyMemory<byte>? Data = await StorageProvider.TryGetBlobAsync(HashValue, Deadline);
			if (Data == null)
			{
				return null;
			}

			T NewItem = new T();
			NewItem.MergeFrom(Data.Value.ToArray());
			return NewItem;
		}

		public static async Task<IoHash> PutProtoMessageAsync<T>(this IStorageService StorageProvider, IMessage<T> Message) where T : class, IMessage<T>
		{
			HashSet<IoHash> References = new HashSet<IoHash>();
			FindReferences(Message, References);

			byte[] MessageData = Message.ToByteArray(); // TODO: Could generate this directly into the buffer

			IoHash HashValue = IoHash.Compute(MessageData);
			await StorageProvider.PutBlobAsync(HashValue, MessageData);

			return HashValue;
		}

		public static Task<ReadOnlyMemory<byte>> GetBulkDataTreeAsync(this IStorageService StorageProvider, IoHash Hash)
		{
			return StorageProvider.GetBlobAsync(Hash);
		}

		public static async Task<IoHash> PutBulkDataTreeAsync(this IStorageService StorageProvider, ReadOnlyMemory<byte> BulkData)
		{
			IoHash HashValue = IoHash.Compute(BulkData.Span);
			await StorageProvider.PutBlobAsync(HashValue, BulkData);
			return HashValue;
		}

		static void FindReferences(this IMessage Message, HashSet<IoHash> References)
		{
			foreach (FieldDescriptor FieldDescriptor in Message.Descriptor.Fields.InFieldNumberOrder())
			{
				if (FieldDescriptor.FieldType == FieldType.Message)
				{
					object? Value = FieldDescriptor.Accessor.GetValue(Message);
					if (Value != null)
					{
						if (FieldDescriptor.MessageType == Digest.Descriptor)
						{
							Digest Digest = (Digest)Value;
							References.Add(Digest.ToHashValue());
						}
						else
						{
							IMessage SubMessage = (IMessage)Value;
							FindReferences(SubMessage, References);
						}
					}
				}
			}
		}
	}
}

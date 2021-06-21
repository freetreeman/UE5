// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EpicGames.Core;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace EpicGames.Serialization.Tests
{
	[TestClass]
	public class JupiterTests
	{
		[TestMethod]
		public void BuildObject()
		{
			byte[] bytes = File.ReadAllBytes("CompactBinaryObjects/build");

			ReadOnlyMemory<byte> memory = new ReadOnlyMemory<byte>(bytes);
			CbField o = new CbField(memory);

			Assert.AreEqual("BuildAction", o.Name);
			List<CbField> buildActionFields = o.ToList();
			Assert.AreEqual(3, buildActionFields.Count);
			Assert.AreEqual("Function", buildActionFields[0].Name);
			Assert.AreEqual("Constants", buildActionFields[1].Name);
			Assert.AreEqual("Inputs", buildActionFields[2].Name);

			List<CbField> constantsFields = buildActionFields[1].ToList();
			Assert.AreEqual(3, constantsFields.Count);
			Assert.AreEqual("TextureBuildSettings", constantsFields[0].Name);
			Assert.AreEqual("TextureOutputSettings", constantsFields[1].Name);
			Assert.AreEqual("TextureSource", constantsFields[2].Name);

			List<CbField> inputsFields = buildActionFields[2].ToList();
			Assert.AreEqual(1, inputsFields.Count);
			Assert.AreEqual("7587B323422942733DDD048A91709FDE", inputsFields[0].Name);
			Assert.IsTrue(inputsFields[0].IsBinaryAttachment());
			Assert.IsTrue(inputsFields[0].IsAttachment());
			Assert.IsFalse(inputsFields[0].IsObjectAttachment());
			Assert.AreEqual(IoHash.Parse("f855382171a0b1e5a1c653aa6c5121a05cbf4ba0"), inputsFields[0].AsHash());
		}


		[TestMethod]
		public void ReferenceOutput()
		{
			byte[] bytes = File.ReadAllBytes("CompactBinaryObjects/ReferenceOutput");

			ReadOnlyMemory<byte> memory = new ReadOnlyMemory<byte>(bytes);
			CbField o = new CbField(memory);

			Assert.AreEqual("BuildOutput", o.Name);
			List<CbField> buildActionFields = o.ToList();
			Assert.AreEqual(1, buildActionFields.Count);
			CbField payloads = buildActionFields[0];
			List<CbField> payloadFields = payloads.ToList();
			Assert.AreEqual(3, payloadFields.Count);

			Assert.AreEqual(IoHash.Parse("5d8a6dc277c968f0d027c98f879c955c1905c293"), payloadFields[0]["RawHash"]!.AsHash());
			Assert.AreEqual(IoHash.Parse("313f0d0d334100d83aeb1ee2c42794fd087cb0ae"), payloadFields[1]["RawHash"]!.AsHash());
			Assert.AreEqual(IoHash.Parse("c7a03f83c08cdca882110ecf2b5654ee3b09b11e"), payloadFields[2]["RawHash"]!.AsHash());
		}


		[TestMethod]
		public void WriteArray()
		{
			var hash1 = IoHash.Parse("5d8a6dc277c968f0d027c98f879c955c1905c293");
			var hash2 = IoHash.Parse("313f0d0d334100d83aeb1ee2c42794fd087cb0ae");

			CbWriter writer = new CbWriter();
			writer.BeginObject();
			writer.AddUniformArray(new IoHash[] { hash1, hash2 }, CbFieldType.Hash, "needs");
			writer.EndObject();

			byte[] objectData = writer.Save();
			ReadOnlyMemory<byte> memory = new ReadOnlyMemory<byte>(objectData);
			CbField o = new CbField(memory);

			// the top object has no name
			Assert.AreEqual("", o.Name);
			List<CbField> fields = o.ToList();
			Assert.AreEqual(1, fields.Count);
			CbField? needs = o["needs"];
			List<CbField> blobList = needs!.AsArray().ToList();
			IoHash[] blobs = blobList.Select(field => field.AsHash()).ToArray();
			CollectionAssert.AreEqual(new IoHash[] { hash1, hash2 }, blobs);
		}


		[TestMethod]
		public void WriteObject()
		{
			var hash1 = IoHash.Parse("5d8a6dc277c968f0d027c98f879c955c1905c293");

			CbWriter writer = new CbWriter();
			writer.BeginObject();
			writer.AddString("test", "string");
			writer.AddBinaryAttachment(hash1, "hash");
			writer.EndObject();

			byte[] objectData = writer.Save();
			ReadOnlyMemory<byte> memory = new ReadOnlyMemory<byte>(objectData);
			CbField o = new CbField(memory);

			// the object has no name and 2 fields
			Assert.AreEqual("", o.Name);
			List<CbField> fields = o.ToList();
			Assert.AreEqual(2, fields.Count);

			CbField? stringField = o["string"];
			Assert.AreEqual("test", stringField!.AsString());

			CbField? hashField = o["hash"];
			Assert.AreEqual(hash1, hashField!.AsAttachment());
		}
	}
}

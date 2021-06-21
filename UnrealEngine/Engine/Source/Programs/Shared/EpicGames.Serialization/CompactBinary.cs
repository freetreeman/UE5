// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Buffers;
using System.Buffers.Binary;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Text;
using System.Text.Json;

namespace EpicGames.Serialization
{
	/// <summary>
	/// Field types and flags for FCbField[View].
	///
	/// DO NOT CHANGE THE VALUE OF ANY MEMBERS OF THIS ENUM!
	/// BACKWARD COMPATIBILITY REQUIRES THAT THESE VALUES BE FIXED!
	/// SERIALIZATION USES HARD-CODED CONSTANTS BASED ON THESE VALUES!
	/// </summary>
	[Flags]
	public enum CbFieldType : byte
	{
		/// <summary>
		/// A field type that does not occur in a valid object.
		/// </summary>
		None = 0x00,

		/// <summary>
		/// Null. Payload is empty.
		/// </summary>
		Null = 0x01,

		/// <summary>
		/// Object is an array of fields with unique non-empty names.
		///
		/// Payload is a VarUInt byte count for the encoded fields followed by the fields.
		/// </summary>
		Object = 0x02,

		/// <summary>
		/// UniformObject is an array of fields with the same field types and unique non-empty names.
		/// 
		/// Payload is a VarUInt byte count for the encoded fields followed by the fields.
		/// </summary>
		UniformObject = 0x03,

		/// <summary>
		/// Array is an array of fields with no name that may be of different types.
		/// 
		/// Payload is a VarUInt byte count, followed by a VarUInt item count, followed by the fields.
		/// </summary>
		Array = 0x04,

		/// <summary>
		/// UniformArray is an array of fields with no name and with the same field type.
		///
		/// Payload is a VarUInt byte count, followed by a VarUInt item count, followed by field type,
		/// followed by the fields without their field type.
		/// </summary>
		UniformArray = 0x05,

		/// <summary> 
		/// Binary. Payload is a VarUInt byte count followed by the data. 
		/// /// </summary>
		Binary = 0x06,

		/// <summary>
		/// String in UTF-8. Payload is a VarUInt byte count then an unterminated UTF-8 string. 
		/// </summary>
		String = 0x07,

		/// <summary>
		/// Non-negative integer with the range of a 64-bit unsigned integer.
		/// 
		/// Payload is the value encoded as a VarUInt.
		/// </summary>
		IntegerPositive = 0x08,

		/// <summary>
		/// Negative integer with the range of a 64-bit signed integer.
		///
		/// Payload is the ones' complement of the value encoded as a VarUInt.
		/// </summary>
		IntegerNegative = 0x09,

		/// <summary>
		/// Single precision float. Payload is one big endian IEEE 754 binary32 float.
		/// /// </summary>
		Float32 = 0x0a,

		/// <summary>
		/// Double precision float. Payload is one big endian IEEE 754 binary64 float. 
		/// </summary>
		Float64 = 0x0b,

		/// <summary>
		/// Boolean false value. Payload is empty. 
		/// </summary>
		BoolFalse = 0x0c,
		
		/// <summary>
		/// Boolean true value. Payload is empty. 
		/// </summary>
		BoolTrue = 0x0d,

		/// <summary>
		/// CompactBinaryAttachment is a reference to a compact binary attachment stored externally.
		///
		/// Payload is a 160-bit hash digest of the referenced compact binary data.
		/// </summary>
		ObjectAttachment = 0x0e,

		/// <summary>
		/// BinaryAttachment is a reference to a binary attachment stored externally.
		///
		/// Payload is a 160-bit hash digest of the referenced binary data.
		/// </summary>
		BinaryAttachment = 0x0f,

		/// <summary>
		/// Hash. Payload is a 160-bit hash digest. 
		/// </summary>
		Hash = 0x10,

		/// <summary>
		/// UUID/GUID. Payload is a 128-bit UUID as defined by RFC 4122. 
		/// </summary>
		Uuid = 0x11,

		/// <summary>
		/// Date and time between 0001-01-01 00:00:00.0000000 and 9999-12-31 23:59:59.9999999.
		///
		/// Payload is a big endian int64 count of 100ns ticks since 0001-01-01 00:00:00.0000000.
		/// </summary>
		DateTime = 0x12,

		/// <summary>
		/// Difference between two date/time values.
		/// 
		/// Payload is a big endian int64 count of 100ns ticks in the span, and may be negative.
		/// </summary>
		TimeSpan = 0x13,

		/// <summary>
		/// ObjectId is an opaque object identifier. See FCbObjectId.
		///
		/// Payload is a 12-byte object identifier.
		/// </summary>
		ObjectId = 0x14,

		/// <summary>
		/// CustomById identifies the sub-type of its payload by an integer identifier.
		///
		/// Payload is a VarUInt byte count of the sub-type identifier and the sub-type payload, followed
		/// by a VarUInt of the sub-type identifier then the payload of the sub-type.
		/// </summary>
		CustomById = 0x1e,

		/// <summary>
		/// CustomByType identifies the sub-type of its payload by a string identifier.
		///
		/// Payload is a VarUInt byte count of the sub-type identifier and the sub-type payload, followed
		/// by a VarUInt byte count of the unterminated sub-type identifier, then the sub-type identifier
		/// without termination, then the payload of the sub-type.
		/// </summary>
		CustomByName = 0x1f,

		/// <summary>
		/// Reserved for future use as a flag. Do not add types in this range. 
		/// </summary>
		Reserved = 0x20,

		/// <summary>
		/// A transient flag which indicates that the object or array containing this field has stored
		/// the field type before the payload and name. Non-uniform objects and fields will set this.
		/// 
		/// Note: Since the flag must never be serialized, this bit may be repurposed in the future.
		/// </summary>
		HasFieldType = 0x40,

		/// <summary>
		/// A persisted flag which indicates that the field has a name stored before the payload. 
		/// </summary>
		HasFieldName = 0x80,
	}

	/// <summary>
	/// Methods that operate on <see cref="CbFieldType"/>.
	/// </summary>
	public static class CbFieldUtils
	{
		private const CbFieldType SerializedTypeMask = (CbFieldType)0b_1001_1111;
		private const CbFieldType TypeMask = (CbFieldType)0b_0001_1111;

		private const CbFieldType ObjectMask = (CbFieldType)0b_0001_1110;
		private const CbFieldType ObjectBase = (CbFieldType)0b_0000_0010;

		private const CbFieldType ArrayMask = (CbFieldType)0b_0001_1110;
		private const CbFieldType ArrayBase = (CbFieldType)0b_0000_0100;

		private const CbFieldType IntegerMask = (CbFieldType)0b_0011_1110;
		private const CbFieldType IntegerBase = (CbFieldType)0b_0000_1000;

		private const CbFieldType FloatMask = (CbFieldType)0b_0001_1100;
		private const CbFieldType FloatBase = (CbFieldType)0b_0000_1000;

		private const CbFieldType BoolMask = (CbFieldType)0b_0001_1110;
		private const CbFieldType BoolBase = (CbFieldType)0b_0000_1100;

		private const CbFieldType AttachmentMask = (CbFieldType)0b_0001_1110;
		private const CbFieldType AttachmentBase = (CbFieldType)0b_0000_1110;

		/// <summary>
		/// Removes flags from the given type
		/// </summary>
		/// <param name="Type">Type to check</param>
		/// <returns>Type without flag fields</returns>
		public static CbFieldType GetType(CbFieldType Type)
		{
			return Type & TypeMask;
		}

		/// <summary>
		/// Gets the serialized type
		/// </summary>
		/// <param name="Type">Type to check</param>
		/// <returns>Type without flag fields</returns>
		public static CbFieldType GetSerializedType(CbFieldType Type)
		{
			return Type & SerializedTypeMask;
		}

		/// <summary>
		/// Tests if the given field has a type
		/// </summary>
		/// <param name="Type">Type to check</param>
		/// <returns>True if the field has a type</returns>
		public static bool HasFieldType(CbFieldType Type)
		{
			return (Type & CbFieldType.HasFieldType) != 0;
		}

		/// <summary>
		/// Tests if the given field has a name
		/// </summary>
		/// <param name="Type">Type to check</param>
		/// <returns>True if the field has a name</returns>
		public static bool HasFieldName(CbFieldType Type)
		{
			return (Type & CbFieldType.HasFieldName) != 0;
		}

		/// <summary>
		/// Tests if the given field type is none
		/// </summary>
		/// <param name="Type">Type to check</param>
		/// <returns>True if the field is none</returns>
		public static bool IsNone(CbFieldType Type)
		{
			return GetType(Type) == CbFieldType.None;
		}

		/// <summary>
		/// Tests if the given field type is a null value
		/// </summary>
		/// <param name="Type">Type to check</param>
		/// <returns>True if the field is a null</returns>
		public static bool IsNull(CbFieldType Type)
		{
			return GetType(Type) == CbFieldType.Null;
		}

		/// <summary>
		/// Tests if the given field type is an object
		/// </summary>
		/// <param name="Type">Type to check</param>
		/// <returns>True if the field is an object type</returns>
		public static bool IsObject(CbFieldType Type)
		{
			return (Type & ObjectMask) == ObjectBase;
		}

		/// <summary>
		/// Tests if the given field type is an array
		/// </summary>
		/// <param name="Type">Type to check</param>
		/// <returns>True if the field is an array type</returns>
		public static bool IsArray(CbFieldType Type)
		{
			return (Type & ArrayMask) == ArrayBase;
		}

		/// <summary>
		/// Tests if the given field type is binary
		/// </summary>
		/// <param name="Type">Type to check</param>
		/// <returns>True if the field is binary</returns>
		public static bool IsBinary(CbFieldType type)
		{
			return GetType(type) == CbFieldType.Binary;
		}

		/// <summary>
		/// Tests if the given field type is a string
		/// </summary>
		/// <param name="Type">Type to check</param>
		/// <returns>True if the field is an array type</returns>
		public static bool IsString(CbFieldType Type)
		{
			return GetType(Type) == CbFieldType.String;
		}

		/// <summary>
		/// Tests if the given field type is an integer
		/// </summary>
		/// <param name="Type">Type to check</param>
		/// <returns>True if the field is an integer type</returns>
		public static bool IsInteger(CbFieldType Type)
		{
			return (Type & IntegerMask) == IntegerBase;
		}

		/// <summary>
		/// Tests if the given field type is a float (or integer, due to implicit conversion)
		/// </summary>
		/// <param name="Type">Type to check</param>
		/// <returns>True if the field is a float type</returns>
		public static bool IsFloat(CbFieldType Type)
		{
			return (Type & FloatMask) == FloatBase;
		}

		/// <summary>
		/// Tests if the given field type is a boolean
		/// </summary>
		/// <param name="Type">Type to check</param>
		/// <returns>True if the field is an bool type</returns>
		public static bool IsBool(CbFieldType Type)
		{
			return (Type & BoolMask) == BoolBase;
		}

		/// <summary>
		/// Tests if the given field type is a compact binary attachment
		/// </summary>
		/// <param name="Type">Type to check</param>
		/// <returns>True if the field is a compact binary attachment</returns>
		public static bool IsObjectAttachment(CbFieldType type)
		{
			return GetType(type) == CbFieldType.ObjectAttachment;
		}

		/// <summary>
		/// Tests if the given field type is a binary attachment
		/// </summary>
		/// <param name="Type">Type to check</param>
		/// <returns>True if the field is a binary attachment</returns>
		public static bool IsBinaryAttachment(CbFieldType type)
		{
			return GetType(type) == CbFieldType.BinaryAttachment;
		}

		/// <summary>
		/// Tests if the given field type is an attachment
		/// </summary>
		/// <param name="Type">Type to check</param>
		/// <returns>True if the field is an attachment type</returns>
		public static bool IsAttachment(CbFieldType Type)
		{
			return (Type & AttachmentMask) == AttachmentBase;
		}

		/// <summary>
		/// Tests if the given field type is a hash
		/// </summary>
		/// <param name="Type">Type to check</param>
		/// <returns>True if the field is a hash</returns>
		public static bool IsHash(CbFieldType Type)
		{
			return GetType(Type) == CbFieldType.Hash || IsAttachment(Type);
		}

		/// <summary>
		/// Tests if the given field type is a UUID
		/// </summary>
		/// <param name="Type">Type to check</param>
		/// <returns>True if the field is a UUID</returns>
		public static bool IsUuid(CbFieldType type)
		{
			return GetType(type) == CbFieldType.Uuid;
		}

		/// <summary>
		/// Tests if the given field type is a date/time
		/// </summary>
		/// <param name="Type">Type to check</param>
		/// <returns>True if the field is a date/time</returns>
		public static bool IsDateTime(CbFieldType type)
		{
			return GetType(type) == CbFieldType.DateTime;
		}

		/// <summary>
		/// Tests if the given field type is a timespan
		/// </summary>
		/// <param name="Type">Type to check</param>
		/// <returns>True if the field is a timespan</returns>
		public static bool IsTimeSpan(CbFieldType type)
		{
			return GetType(type) == CbFieldType.TimeSpan;
		}

		/// <summary>
		/// Tests if the given field type has fields
		/// </summary>
		/// <param name="Type">Type to check</param>
		/// <returns>True if the field has fields</returns>
		public static bool HasFields(CbFieldType Type)
		{
			CbFieldType NoFlags = GetType(Type);
			return NoFlags >= CbFieldType.Object && NoFlags <= CbFieldType.UniformArray;
		}

		/// <summary>
		/// Tests if the given field type has uniform fields (array/object)
		/// </summary>
		/// <param name="Type">Type to check</param>
		/// <returns>True if the field has uniform fields</returns>
		public static bool HasUniformFields(CbFieldType Type)
		{
			CbFieldType LocalType = GetType(Type);
			return LocalType == CbFieldType.UniformObject || LocalType == CbFieldType.UniformArray;
		}

		/// <summary>
		/// Tests if the type is or may contain fields of any attachment type.
		/// </summary>
		public static bool MayContainAttachments(CbFieldType Type)
		{
			return IsObject(Type) | IsArray(Type) | IsAttachment(Type);
		}
	}

	/// <summary>
	/// Errors that can occur when accessing a field. */
	/// </summary>
	public enum CbFieldError : byte
	{
		/// <summary>
		/// The field is not in an error state.
		/// </summary>
		None,

		/// <summary>
		/// The value type does not match the requested type.
		/// </summary>
		TypeError,

		/// <summary>
		/// The value is out of range for the requested type.
		/// </summary>
		RangeError,
	}

	/// <summary>
	/// An atom of data in the compact binary format.
	///
	/// Accessing the value of a field is always a safe operation, even if accessed as the wrong type.
	/// An invalid access will return a default value for the requested type, and set an error code on
	/// the field that can be checked with GetLastError and HasLastError. A valid access will clear an
	/// error from a previous invalid access.
	///
	/// A field is encoded in one or more bytes, depending on its type and the type of object or array
	/// that contains it. A field of an object or array which is non-uniform encodes its field type in
	/// the first byte, and includes the HasFieldName flag for a field in an object. The field name is
	/// encoded in a variable-length unsigned integer of its size in bytes, for named fields, followed
	/// by that many bytes of the UTF-8 encoding of the name with no null terminator.The remainder of
	/// the field is the payload and is described in the field type enum. Every field must be uniquely
	/// addressable when encoded, which means a zero-byte field is not permitted, and only arises in a
	/// uniform array of fields with no payload, where the answer is to encode as a non-uniform array.
	/// </summary>
	public class CbField : IEquatable<CbField>, IEnumerable<CbField>
	{
		/// <summary>
		/// Default empty field
		/// </summary>
		public static CbField Empty { get; } = new CbField();

		/// <summary>
		/// The field type, with the transient HasFieldType flag if the field contains its type
		/// </summary>
		public CbFieldType TypeWithFlags { get; }

		/// <summary>
		/// Data for this field
		/// </summary>
		public ReadOnlyMemory<byte> Memory { get; }

		/// <summary>
		/// Offset of the name with the memory
		/// </summary>
		public int NameLen;

		/// <summary>
		/// Offset of the payload within the memory
		/// </summary>
		public int PayloadOffset;

		/// <summary>
		/// Error for parsing the current field type
		/// </summary>
		public CbFieldError Error { get; private set; }

		/// <summary>
		/// Default constructor
		/// </summary>
		public CbField()
			: this(ReadOnlyMemory<byte>.Empty, CbFieldType.None)
		{
		}

		/// <summary>
		/// Copy constructor
		/// </summary>
		/// <param name="Other"></param>
		public CbField(CbField Other)
		{
			this.TypeWithFlags = Other.TypeWithFlags;
			this.Memory = Other.Memory;
			this.NameLen = Other.NameLen;
			this.PayloadOffset = Other.PayloadOffset;
			this.Error = Other.Error;
		}

		/// <summary>
		/// Construct a field from a pointer to its data and an optional externally-provided type.
		/// </summary>
		/// <param>Data Pointer to the start of the field data.</param>
		/// <param>Type HasFieldType means that Data contains the type. Otherwise, use the given type.</param>
		public CbField(ReadOnlyMemory<byte> Data, CbFieldType Type = CbFieldType.HasFieldType)
		{
			int Offset = 0;
			if (CbFieldUtils.HasFieldType(Type))
			{
				Type = (CbFieldType)Data.Span[Offset] | CbFieldType.HasFieldType;
				Offset++;
			}

			if (CbFieldUtils.HasFieldName(Type))
			{
				NameLen = (int)BitUtils.ReadVarUInt(Data.Slice(Offset).Span, out int NameLenByteCount);
				Offset += NameLenByteCount + NameLen;
			}

			this.Memory = Data;
			this.TypeWithFlags = Type;
			this.PayloadOffset = Offset;

			Memory = Memory.Slice(0, (int)Math.Min((ulong)Memory.Length, (ulong)PayloadOffset + GetPayloadSize()));
		}

		/// <summary>
		/// Returns the name of the field if it has a name, otherwise an empty view.
		/// </summary>
		public ReadOnlyUtf8String Name => new ReadOnlyUtf8String(Memory.Slice(PayloadOffset - NameLen, NameLen));

		/// <inheritdoc cref="Name"/>
		public ReadOnlyUtf8String GetName() => Name;

		/// <summary>
		/// Access the field as an object. Defaults to an empty object on error. 
		/// </summary>
		/// <returns></returns>
		public CbObject AsObject()
		{
			if (CbFieldUtils.IsObject(TypeWithFlags))
			{
				Error = CbFieldError.None;
				return CbObject.FromFieldNoCheck(this);
			}
			else
			{
				Error = CbFieldError.TypeError;
				return CbObject.Empty;
			}
		}

		/// <summary>
		/// Access the field as an object. Defaults to an empty object on error. 
		/// </summary>
		/// <returns></returns>
		public CbObject AsObjectView() => AsObject();

		/// <summary>
		/// Access the field as an array. Defaults to an empty array on error. 
		/// </summary>
		/// <returns></returns>
		public CbArray AsArray()
		{
			if (CbFieldUtils.IsArray(TypeWithFlags))
			{
				Error = CbFieldError.None;
				return CbArray.FromFieldNoCheck(this);
			}
			else
			{
				Error = CbFieldError.TypeError;
				return CbArray.Empty;
			}
		}

		/// <summary>
		/// Access the field as an array. Defaults to an empty array on error. 
		/// </summary>
		/// <returns></returns>
		public CbArray AsArrayView() => AsArray();

		/// <summary>
		/// Access the field as binary data.
		/// </summary>
		/// <returns></returns>
		public ReadOnlyMemory<byte> AsBinary(ReadOnlyMemory<byte> Default = default)
		{
			if (CbFieldUtils.IsBinary(TypeWithFlags))
			{
				Error = CbFieldError.None;
				return BitUtils.GetVarUIntSizedPayload(GetPayloadView());
			}
			else
			{
				Error = CbFieldError.TypeError;
				return Default;
			}
		}

		/// <summary>
		/// Access the field as binary data.
		/// </summary>
		/// <returns></returns>
		public ReadOnlyMemory<byte> AsBinaryView(ReadOnlyMemory<byte> Default = default) => AsBinary(Default);


		/// <summary>
		/// Access the field as a UTF-8 string. Returns the provided default on error.
		/// </summary>
		/// <param name="Default">Default value to return</param>
		/// <returns></returns>
		public ReadOnlyUtf8String AsString(ReadOnlyUtf8String Default = default)
		{
			if (CbFieldUtils.IsString(TypeWithFlags))
			{
				ulong ValueSize = BitUtils.ReadVarUInt(Payload.Span, out int ValueSizeByteCount);
				if (ValueSize >= (1UL << 31))
				{
					Error = CbFieldError.RangeError;
					return Default;
				}
				else
				{
					Error = CbFieldError.None;
					return new ReadOnlyUtf8String(Payload.Slice(ValueSizeByteCount, (int)ValueSize));
				}
			}
			else
			{
				Error = CbFieldError.TypeError;
				return Default;
			}
		}

		/// <summary>
		/// Access the field as an int8. Returns the provided default on error.
		/// </summary>
		public sbyte AsInt8(sbyte Default = 0)
		{
			return (sbyte)AsInteger((ulong)Default, 7, true);
		}

		/// <summary>
		/// Access the field as an int16. Returns the provided default on error.
		/// </summary>
		public short AsInt16(short Default = 0)
		{
			return (short)AsInteger((ulong)Default, 15, true);
		}

		/// <summary>
		/// Access the field as an int32. Returns the provided default on error.
		/// </summary>
		public int AsInt32(int Default = 0)
		{
			return (int)AsInteger((ulong)Default, 31, true);
		}

		/// <summary>
		/// Access the field as an int64. Returns the provided default on error.
		/// </summary>
		public long AsInt64(long Default = 0)
		{
			return (long)AsInteger((ulong)Default, 63, true);
		}

		/// <summary>
		/// Access the field as an int8. Returns the provided default on error.
		/// </summary>
		public byte AsUInt8(byte Default = 0)
		{
			return (byte)AsInteger(Default, 8, false);
		}

		/// <summary>
		/// Access the field as an int16. Returns the provided default on error.
		/// </summary>
		public ushort AsUInt16(ushort Default = 0)
		{
			return (ushort)AsInteger(Default, 16, false);
		}

		/// <summary>
		/// Access the field as an int32. Returns the provided default on error.
		/// </summary>
		public uint AsUInt32(uint Default = 0)
		{
			return (uint)AsInteger(Default, 32, false);
		}

		/// <summary>
		/// Access the field as an int64. Returns the provided default on error.
		/// </summary>
		public ulong AsUInt64(ulong Default = 0)
		{
			return (ulong)AsInteger(Default, 64, false);
		}

		/// <summary>
		/// Access the field as an integer, checking that it's in the correct range
		/// </summary>
		/// <param name="Default"></param>
		/// <param name="MagnitudeBits"></param>
		/// <param name="IsSigned"></param>
		/// <returns></returns>
		private ulong AsInteger(ulong Default, int MagnitudeBits, bool IsSigned)
		{
			if (CbFieldUtils.IsInteger(TypeWithFlags))
			{
				// A shift of a 64-bit value by 64 is undefined so shift by one less because magnitude is never zero.
				ulong OutOfRangeMask = ~(ulong)1 << (MagnitudeBits - 1);
				ulong IsNegative = (ulong)(byte)(TypeWithFlags) & 1;

				int MagnitudeByteCount;
				ulong Magnitude = BitUtils.ReadVarUInt(Payload.Span, out MagnitudeByteCount);
				ulong Value = Magnitude ^ (ulong)-(long)(IsNegative);

				if ((Magnitude & OutOfRangeMask) == 0 && (IsNegative == 0 || IsSigned))
				{
					Error = CbFieldError.None;
					return Value;
				}
				else
				{
					Error = CbFieldError.RangeError;
					return Default;
				}
			}
			else
			{
				Error = CbFieldError.TypeError;
				return Default;
			}
		}

		/// <summary>
		/// Access the field as a float. Returns the provided default on error.
		/// </summary>
		/// <param name="Default">Default value</param>
		/// <returns>Value of the field</returns>
		public float AsFloat(float Default = 0.0f)
		{
			switch (GetType())
			{
				case CbFieldType.IntegerPositive:
				case CbFieldType.IntegerNegative:
					{
						ulong IsNegative = (ulong)TypeWithFlags & 1;
						ulong OutOfRangeMask = ~((1UL << /*FLT_MANT_DIG*/ 24) - 1);

						int MagnitudeByteCount;
						ulong Magnitude = BitUtils.ReadVarUInt(Payload.Span, out MagnitudeByteCount) + IsNegative;
						bool IsInRange = (Magnitude & OutOfRangeMask) == 0;
						Error = IsInRange ? CbFieldError.None : CbFieldError.RangeError;
						return IsInRange ? (float)((IsNegative != 0) ? (float)-(long)Magnitude : (float)Magnitude) : Default;
					}
				case CbFieldType.Float32:
					Error = CbFieldError.None;
					return BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32BigEndian(Payload.Span));
				case CbFieldType.Float64:
					Error = CbFieldError.RangeError;
					return Default;
				default:
					Error = CbFieldError.TypeError;
					return Default;
			}
		}

		/// <summary>
		/// Access the field as a double. Returns the provided default on error.
		/// </summary>
		/// <param name="Default">Default value</param>
		/// <returns>Value of the field</returns>
		public double AsDouble(double Default = 0.0f)
		{
			switch (GetType())
			{
				case CbFieldType.IntegerPositive:
				case CbFieldType.IntegerNegative:
					{
						ulong IsNegative = (ulong)TypeWithFlags & 1;
						ulong OutOfRangeMask = ~((1UL << /*DBL_MANT_DIG*/ 53) - 1);

						int MagnitudeByteCount;
						ulong Magnitude = BitUtils.ReadVarUInt(Payload.Span, out MagnitudeByteCount) + IsNegative;
						bool IsInRange = (Magnitude & OutOfRangeMask) == 0;
						Error = IsInRange ? CbFieldError.None : CbFieldError.RangeError;
						return IsInRange ? (double)((IsNegative != 0) ? (double)-(long)Magnitude : (double)Magnitude) : Default;
					}
				case CbFieldType.Float32:
					{
						Error = CbFieldError.None;
						return BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32BigEndian(Payload.Span));
					}
				case CbFieldType.Float64:
					{
						Error = CbFieldError.None;
						return BitConverter.Int64BitsToDouble(BinaryPrimitives.ReadInt64BigEndian(Payload.Span));
					}
				default:
					Error = CbFieldError.TypeError;
					return Default;
			}
		}

		/// <summary>
		/// Access the field as a bool. Returns the provided default on error.
		/// </summary>
		/// <param name="Default">Default value</param>
		/// <returns>Value of the field</returns>
		public bool AsBool(bool Default = false)
		{
			switch (GetType())
			{
				case CbFieldType.BoolTrue:
					Error = CbFieldError.None;
					return true;
				case CbFieldType.BoolFalse:
					Error = CbFieldError.None;
					return false;
				default:
					Error = CbFieldError.TypeError;
					return Default;
			}
		}

		/// <summary>
		/// Access the field as a hash referencing an object attachment. Returns the provided default on error.
		/// </summary>
		/// <returns>Value of the field</returns>
		public IoHash AsObjectAttachment() => AsObjectAttachment(IoHash.Zero);

		/// <summary>
		/// Access the field as a hash referencing an object attachment. Returns the provided default on error.
		/// </summary>
		/// <param name="Default">Default value</param>
		/// <returns>Value of the field</returns>
		public IoHash AsObjectAttachment(IoHash Default)
		{
			if (CbFieldUtils.IsObjectAttachment(TypeWithFlags))
			{
				Error = CbFieldError.None;
				return new IoHash(Payload);
			}
			else
			{
				Error = CbFieldError.TypeError;
				return Default;
			}
		}

		/// <summary>
		/// Access the field as a hash referencing a binary attachment. Returns the provided default on error.
		/// </summary>
		/// <param name="Default">Default value</param>
		/// <returns>Value of the field</returns>
		public IoHash AsBinaryAttachment() => AsBinaryAttachment(IoHash.Zero);

		/// <summary>
		/// Access the field as a hash referencing a binary attachment. Returns the provided default on error.
		/// </summary>
		/// <param name="Default">Default value</param>
		/// <returns>Value of the field</returns>
		public IoHash AsBinaryAttachment(IoHash Default)
		{
			if (CbFieldUtils.IsBinaryAttachment(TypeWithFlags))
			{
				Error = CbFieldError.None;
				return new IoHash(Payload);
			}
			else
			{
				Error = CbFieldError.TypeError;
				return Default;
			}
		}

		/// <summary>
		/// Access the field as a hash referencing an attachment. Returns the provided default on error.
		/// </summary>
		/// <param name="Default">Default value</param>
		/// <returns>Value of the field</returns>
		public IoHash AsAttachment() => AsAttachment(IoHash.Zero);

		/// <summary>
		/// Access the field as a hash referencing an attachment. Returns the provided default on error.
		/// </summary>
		/// <param name="Default">Default value</param>
		/// <returns>Value of the field</returns>
		public IoHash AsAttachment(IoHash Default)
		{
			if (CbFieldUtils.IsAttachment(TypeWithFlags))
			{
				Error = CbFieldError.None;
				return new IoHash(Payload);
			}
			else
			{
				Error = CbFieldError.TypeError;
				return Default;
			}
		}

		/// <summary>
		/// Access the field as a hash referencing an attachment. Returns the provided default on error.
		/// </summary>
		/// <returns>Value of the field</returns>
		public IoHash AsHash() => AsHash(IoHash.Zero);

		/// <summary>
		/// Access the field as a hash referencing an attachment. Returns the provided default on error.
		/// </summary>
		/// <param name="Default">Default value</param>
		/// <returns>Value of the field</returns>
		public IoHash AsHash(IoHash Default)
		{
			if (CbFieldUtils.IsHash(TypeWithFlags))
			{
				Error = CbFieldError.None;
				return new IoHash(Payload);
			}
			else
			{
				Error = CbFieldError.TypeError;
				return Default;
			}
		}

		/// <summary>
		/// Access the field as a UUID. Returns a nil UUID on error.
		/// </summary>
		/// <param name="Default">Default value</param>
		/// <returns>Value of the field</returns>
		public Guid AsUuid(Guid Default = default(Guid))
		{
			if (CbFieldUtils.IsUuid(TypeWithFlags))
			{
				Error = CbFieldError.None;

				ReadOnlySpan<byte> Span = Payload.Span;
				uint A = BinaryPrimitives.ReadUInt32BigEndian(Span);
				ushort B = BinaryPrimitives.ReadUInt16BigEndian(Span.Slice(4));
				ushort C = BinaryPrimitives.ReadUInt16BigEndian(Span.Slice(6));

				return new Guid(A, B, C, Span[8], Span[9], Span[10], Span[11], Span[12], Span[13], Span[14], Span[15]);
			}
			else
			{
				Error = CbFieldError.TypeError;
				return Default;
			}
		}

		/// <summary>
		/// Reads a date time as number of ticks from the stream
		/// </summary>
		/// <param name="Default"></param>
		/// <returns></returns>
		public long AsDateTimeTicks(long Default = 0)
		{
			if (CbFieldUtils.IsDateTime(TypeWithFlags))
			{
				Error = CbFieldError.None;
				return BinaryPrimitives.ReadInt64BigEndian(Payload.Span);
			}
			else
			{
				Error = CbFieldError.TypeError;
				return Default;
			}
		}

		/// <summary>
		/// Access the field as a DateTime.
		/// </summary>
		/// <param name="Default"></param>
		/// <returns></returns>
		public DateTime AsDateTime(DateTime Default = default)
		{
			return new DateTime(AsDateTimeTicks(Default.Ticks));
		}

		/// <summary>
		/// Reads a timespan as number of ticks from the stream
		/// </summary>
		/// <param name="Default"></param>
		/// <returns></returns>
		public long AsTimeSpanTicks(long Default = 0)
		{
			if (CbFieldUtils.IsTimeSpan(TypeWithFlags))
			{
				Error = CbFieldError.None;
				return BinaryPrimitives.ReadInt64BigEndian(Payload.Span);
			}
			else
			{
				Error = CbFieldError.TypeError;
				return Default;
			}
		}

		/// <summary>
		/// Reads a timespan as number of ticks from the stream
		/// </summary>
		/// <param name="Default"></param>
		/// <returns></returns>
		public TimeSpan AsTimeSpan(TimeSpan Default = default) => new TimeSpan(AsTimeSpanTicks(Default.Ticks));

		/// <inheritdoc cref="CbFieldUtils.HasFieldName(CbFieldType)"/>
		public bool HasName() => CbFieldUtils.HasFieldName(TypeWithFlags);

		/// <inheritdoc cref="CbFieldUtils.IsNull(CbFieldType)"/>
		public bool IsNull() => CbFieldUtils.IsNull(TypeWithFlags);

		/// <inheritdoc cref="CbFieldUtils.IsObject(CbFieldType)"/>
		public bool IsObject() => CbFieldUtils.IsObject(TypeWithFlags);

		/// <inheritdoc cref="CbFieldUtils.IsArray(CbFieldType)"/>
		public bool IsArray() => CbFieldUtils.IsArray(TypeWithFlags);

		/// <inheritdoc cref="CbFieldUtils.IsBinary(CbFieldType)"/>
		public bool IsBinary() => CbFieldUtils.IsBinary(TypeWithFlags);

		/// <inheritdoc cref="CbFieldUtils.IsString(CbFieldType)"/>
		public bool IsString() => CbFieldUtils.IsString(TypeWithFlags);

		/// <inheritdoc cref="CbFieldUtils.IsInteger(CbFieldType)"/>
		public bool IsInteger() => CbFieldUtils.IsInteger(TypeWithFlags);

		/// <inheritdoc cref="CbFieldUtils.IsFloat(CbFieldType)"/>
		public bool IsFloat() => CbFieldUtils.IsFloat(TypeWithFlags);

		/// <inheritdoc cref="CbFieldUtils.IsBool(CbFieldType)"/>
		public bool IsBool() => CbFieldUtils.IsBool(TypeWithFlags);

		/// <inheritdoc cref="CbFieldUtils.IsObjectAttachment(CbFieldType)"/>
		public bool IsObjectAttachment() => CbFieldUtils.IsObjectAttachment(TypeWithFlags);

		/// <inheritdoc cref="CbFieldUtils.IsBinaryAttachment(CbFieldType)"/>
		public bool IsBinaryAttachment() => CbFieldUtils.IsBinaryAttachment(TypeWithFlags);

		/// <inheritdoc cref="CbFieldUtils.IsAttachment(CbFieldType)"/>
		public bool IsAttachment() => CbFieldUtils.IsAttachment(TypeWithFlags);

		/// <inheritdoc cref="CbFieldUtils.IsHash(CbFieldType)"/>
		public bool IsHash() => CbFieldUtils.IsHash(TypeWithFlags);

		/// <inheritdoc cref="CbFieldUtils.IsUuid(CbFieldType)"/>
		public bool IsUuid() => CbFieldUtils.IsUuid(TypeWithFlags);

		/// <inheritdoc cref="CbFieldUtils.IsDateTime(CbFieldType)"/>
		public bool IsDateTime() => CbFieldUtils.IsDateTime(TypeWithFlags);

		/// <inheritdoc cref="CbFieldUtils.IsTimeSpan(CbFieldType)"/>
		public bool IsTimeSpan() => CbFieldUtils.IsTimeSpan(TypeWithFlags);

		/// <summary>
		/// Whether the field has a value
		/// </summary>
		/// <param name="Field"></param>
		public static explicit operator bool(CbField Field) => Field.HasValue();

		/// <summary>
		/// Whether the field has a value.
		///
		/// All fields in a valid object or array have a value. A field with no value is returned when
		/// finding a field by name fails or when accessing an iterator past the end.
		/// </summary>
		public bool HasValue() => !CbFieldUtils.IsNone(TypeWithFlags);

		/// <summary>
		/// Whether the last field access encountered an error.
		/// </summary>
		public bool HasError() => Error != CbFieldError.None;

		/// <inheritdoc cref="Error"/>
		public CbFieldError GetError() => Error;

		/// <summary>
		/// Returns the size of the field in bytes, including the type and name
		/// </summary>
		/// <returns></returns>
		public int GetSize() => sizeof(CbFieldType) + GetViewNoType().Length;

		/// <summary>
		/// Calculate the hash of the field, including the type and name.
		/// </summary>
		/// <returns></returns>
		public Blake3Hash GetHash()
		{
			Blake3.Hasher Hasher = Blake3.Hasher.New();
			AppendHash(Hasher);

			byte[] Hash = new byte[32];
			Hasher.Finalize(Hash);

			return new Blake3Hash(Hash);
		}

		/// <summary>
		/// Append the hash of the field, including the type and name
		/// </summary>
		/// <param name="Hasher"></param>
		void AppendHash(Blake3.Hasher Hasher)
		{
			byte[] Data = new byte[] { (byte)CbFieldUtils.GetSerializedType(TypeWithFlags) };
			Hasher.Update(Data);
			Hasher.Update(GetViewNoType().Span);
		}

		/// <summary>
		/// Whether this field is identical to the other field.
		/// 
		/// Performs a deep comparison of any contained arrays or objects and their fields. Comparison
		/// assumes that both fields are valid and are written in the canonical format. Fields must be
		/// written in the same order in arrays and objects, and name comparison is case sensitive. If
		/// these assumptions do not hold, this may return false for equivalent inputs. Validation can
		/// be performed with ValidateCompactBinary, except for field order and field name case.
		/// </summary>
		/// <param name="Other"></param>
		/// <returns></returns>
		public bool Equals(CbField? Other)
		{
			return Other != null && CbFieldUtils.GetSerializedType(TypeWithFlags) == CbFieldUtils.GetSerializedType(Other.TypeWithFlags) && GetViewNoType().Span.SequenceEqual(Other.GetViewNoType().Span);
		}

		/// <summary>
		/// Copy the field into a buffer of exactly GetSize() bytes, including the type and name.
		/// </summary>
		/// <param name="Buffer"></param>
		public void CopyTo(Span<byte> Buffer)
		{
			Buffer[0] = (byte)CbFieldUtils.GetSerializedType(TypeWithFlags);
			GetViewNoType().Span.CopyTo(Buffer.Slice(1));
		}

		/// <summary>
		/// Invoke the visitor for every attachment in the field.
		/// </summary>
		/// <param name="Visitor"></param>
		public void IterateAttachments(Action<CbField> Visitor)
		{
			switch (GetType())
			{
				case CbFieldType.Object:
				case CbFieldType.UniformObject:
					CbObject.FromFieldNoCheck(this).IterateAttachments(Visitor);
					break;
				case CbFieldType.Array:
				case CbFieldType.UniformArray:
					CbArray.FromFieldNoCheck(this).IterateAttachments(Visitor);
					break;
				case CbFieldType.ObjectAttachment:
				case CbFieldType.BinaryAttachment:
					Visitor(this);
					break;
			}
		}

		/// <summary>
		/// Try to get a view of the field as it would be serialized, such as by CopyTo.
		///
		/// A view is available if the field contains its type. Access the equivalent for other fields
		/// through FCbField::GetBuffer, FCbField::Clone, or CopyTo.
		/// </summary>
		/// <param name="OutView"></param>
		/// <returns></returns>
		public bool TryGetView(out ReadOnlyMemory<byte> OutView)
		{
			if (CbFieldUtils.HasFieldType(TypeWithFlags))
			{
				OutView = Memory;
				return true;
			}
			else
			{
				OutView = ReadOnlyMemory<byte>.Empty;
				return false;
			}
		}

		/// <summary>
		/// Find a field of an object by case-sensitive name comparison, otherwise a field with no value.
		/// </summary>
		/// <param name="Name"></param>
		/// <returns></returns>
		public CbField this[ReadOnlyUtf8String Name]
		{
			get { return this.FirstOrDefault(Field => Field.Name == Name) ?? CbField.Empty; }
		}

		/// <summary>
		/// Create an iterator for the fields of an array or object, otherwise an empty iterator.
		/// </summary>
		/// <returns></returns>
		public CbFieldIterator CreateIterator() => CreateViewIterator();

		/// <summary>
		/// Create an iterator for the fields of an array or object, otherwise an empty iterator.
		/// </summary>
		/// <returns></returns>
		public CbFieldIterator CreateViewIterator()
		{
			CbFieldType LocalTypeWithFlags = TypeWithFlags;
			if (CbFieldUtils.HasFields(LocalTypeWithFlags))
			{
				ReadOnlyMemory<byte> PayloadBytes = Payload;
				int PayloadSizeByteCount;
				int PayloadSize = (int)BitUtils.ReadVarUInt(PayloadBytes.Span, out PayloadSizeByteCount);
				PayloadBytes = PayloadBytes.Slice(PayloadSizeByteCount);
				int NumByteCount = CbFieldUtils.IsArray(LocalTypeWithFlags) ? (int)BitUtils.MeasureVarUInt(PayloadBytes.Span) : 0;
				if (PayloadSize > NumByteCount)
				{
					PayloadBytes = PayloadBytes.Slice(NumByteCount);

					CbFieldType UniformType = CbFieldType.HasFieldType;
					if (CbFieldUtils.HasUniformFields(TypeWithFlags))
					{
						UniformType = (CbFieldType)PayloadBytes.Span[0];
						PayloadBytes = PayloadBytes.Slice(1);
					}

					return new CbFieldIterator(PayloadBytes, UniformType);
				}
			}
			return new CbFieldIterator(ReadOnlyMemory<byte>.Empty, CbFieldType.HasFieldType);
		}

		/// <inheritdoc/>
		public IEnumerator<CbField> GetEnumerator()
		{
			for (CbFieldIterator Iter = CreateViewIterator(); Iter; Iter.MoveNext())
			{
				yield return Iter.Current;
			}
		}

		/// <inheritdoc/>
		IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

		/// <summary>
		/// Returns a view of the field, including the type and name when present.
		/// </summary>
		/// <returns></returns>
		internal ReadOnlyMemory<byte> GetView()
		{
			return Memory;
		}

		/// <summary>
		/// Returns a view of the name and value payload, which excludes the type.
		/// </summary>
		/// <returns></returns>
		private ReadOnlyMemory<byte> GetViewNoType()
		{
			int NameSize = CbFieldUtils.HasFieldName(TypeWithFlags) ? NameLen + (int)BitUtils.MeasureVarUInt((uint)NameLen) : 0;
			return Memory.Slice(PayloadOffset - NameSize);
		}

		/// <summary>
		/// Accessor for the payload
		/// </summary>
		internal ReadOnlyMemory<byte> Payload => Memory.Slice(PayloadOffset);

		/// <summary>
		/// Returns a view of the value payload, which excludes the type and name.
		/// </summary>
		/// <returns></returns>
		internal ReadOnlyMemory<byte> GetPayloadView() => Memory.Slice(PayloadOffset);

		/// <summary>
		/// Returns the type of the field excluding flags.
		/// </summary>
		internal new CbFieldType GetType() => CbFieldUtils.GetType(TypeWithFlags);

		/// <summary>
		/// Returns the type of the field excluding flags.
		/// </summary>
		internal CbFieldType GetTypeWithFlags() => TypeWithFlags;

		/// <summary>
		/// Returns the size of the value payload in bytes, which is the field excluding the type and name.
		/// </summary>
		/// <param name="Type"></param>
		/// <param name="Payload"></param>
		/// <returns></returns>
		public ulong GetPayloadSize()
		{
			switch (GetType())
			{
				case CbFieldType.None:
				case CbFieldType.Null:
					return 0;
				case CbFieldType.Object:
				case CbFieldType.UniformObject:
				case CbFieldType.Array:
				case CbFieldType.UniformArray:
				case CbFieldType.Binary:
				case CbFieldType.String:
					{
						ulong PayloadSize = BitUtils.ReadVarUInt(Payload.Span, out int BytesRead);
						return PayloadSize + (ulong)BytesRead;
					}
				case CbFieldType.IntegerPositive:
				case CbFieldType.IntegerNegative:
					{
						return (ulong)BitUtils.MeasureVarUInt(Payload.Span);
					}
				case CbFieldType.Float32:
					return 4;
				case CbFieldType.Float64:
					return 8;
				case CbFieldType.BoolFalse:
				case CbFieldType.BoolTrue:
					return 0;
				case CbFieldType.ObjectAttachment:
				case CbFieldType.BinaryAttachment:
				case CbFieldType.Hash:
					return 20;
				case CbFieldType.Uuid:
					return 16;
				case CbFieldType.DateTime:
				case CbFieldType.TimeSpan:
					return 8;
				case CbFieldType.ObjectId:
					return 12;
				default:
					return 0;
			}
		}

		/*
				/// <summary>
				/// Enumerate fields within this field. (Originally CreateFieldViewIterator).
				/// </summary>
				private IEnumerable<CbField> Fields
				{
				   get
				   {
					   for (CbFieldIterator Iterator = CreateViewIterator(); Iterator; Iterator.MoveNext())
					   {
						   yield return Iterator.Current;
					   }
				   }
			   }

			   public CbFieldIterator CreateIterator() => CreateViewIterator();

			   public static CbField MakeView(CbField Field) => Field;
			   public CbField Find(ReadOnlyUtf8String Name) => FindView(Name);
			   public CbField FindIgnoreCase(ReadOnlyUtf8String Name) => FindViewIgnoreCase(Name);
			   public CbField FindView(ReadOnlyUtf8String Name) => this[Name];
			   public CbField FindViewIgnoreCase(ReadOnlyUtf8String Name) => Fields.FirstOrDefault(Field => ReadOnlyUtf8StringComparer.OrdinalIgnoreCase.Equals(Field.Name, Name)) ?? new CbField();

			   /// <summary>
			   /// Fetch a field by index. This operation is O(n); prefer to iterate through fields once.
			   /// </summary>
			   /// <param name="Index"></param>
			   /// <returns></returns>
			   public CbField? this[int Index]
			   {
				   get { return Fields.Skip(Index).FirstOrDefault(); }
			   }
		*/
		#region Mimic inheritance from TCbBufferFactory

		public static CbField Clone(ReadOnlyMemory<byte> Data) => Clone(new CbField(Data));
		public static CbField Clone(CbField Other) => Other;
		public static CbField MakeView(ReadOnlyMemory<byte> Data) => new CbField(Data);
		public static CbField MakeView(CbField Other) => Other;

		#endregion
	}

	public class CbFieldEnumerator : IEnumerator<CbField>
	{
		/// <summary>
		/// The underlying buffer
		/// </summary>
		ReadOnlyMemory<byte> Data;

		/// <summary>
		/// Type for all fields
		/// </summary>
		CbFieldType UniformType { get; }

		/// <inheritdoc/>
		public CbField Current { get; private set; } = null!;

		/// <inheritdoc/>
		object? IEnumerator.Current => Current;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Data"></param>
		/// <param name="UniformType"></param>
		public CbFieldEnumerator(ReadOnlyMemory<byte> Data, CbFieldType UniformType)
		{
			this.Data = Data;
			this.UniformType = UniformType;
		}

		/// <inheritdoc/>
		public void Dispose()
		{
		}

		/// <inheritdoc/>
		public void Reset()
		{
			throw new InvalidOperationException();
		}

		/// <inheritdoc/>
		public bool MoveNext()
		{
			if (Data.Length > 0)
			{
				Current = new CbField(Data, UniformType);
				return true;
			}
			else
			{
				Current = null!;
				return false;
			}
		}

		/// <summary>
		/// Clone this enumerator
		/// </summary>
		/// <returns></returns>
		public CbFieldEnumerator Clone()
		{
			return new CbFieldEnumerator(Data, UniformType);
		}
	}

	/// <summary>
	/// Iterator for fields
	/// </summary>
	public class CbFieldIterator
	{
		/// <summary>
		/// The underlying buffer
		/// </summary>
		ReadOnlyMemory<byte> NextData;

		/// <summary>
		/// Type for all fields
		/// </summary>
		CbFieldType UniformType;

		/// <summary>
		/// The current iterator
		/// </summary>
		public CbField Current { get; private set; } = null!;

		/// <summary>
		/// Default constructor
		/// </summary>
		public CbFieldIterator()
			: this(ReadOnlyMemory<byte>.Empty, CbFieldType.HasFieldType)
		{
		}

		/// <summary>
		/// Constructor for single field iterator
		/// </summary>
		/// <param name="Field"></param>
		private CbFieldIterator(CbField Field)
		{
			NextData = ReadOnlyMemory<byte>.Empty;
			Current = Field;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Data"></param>
		/// <param name="UniformType"></param>
		public CbFieldIterator(ReadOnlyMemory<byte> Data, CbFieldType UniformType)
		{
			this.NextData = Data;
			this.UniformType = UniformType;

			MoveNext();
		}

		/// <summary>
		/// Copy constructor
		/// </summary>
		/// <param name="Other"></param>
		public CbFieldIterator(CbFieldIterator Other)
		{
			this.NextData = Other.NextData;
			this.UniformType = Other.UniformType;
			this.Current = Other.Current;
		}

		/// <summary>
		/// Construct a field range that contains exactly one field.
		/// </summary>
		/// <param name="Field"></param>
		/// <returns></returns>
		public static CbFieldIterator MakeSingle(CbField Field)
		{
			return new CbFieldIterator(Field);
		}

		/// <summary>
		/// Construct a field range from a buffer containing zero or more valid fields.
		/// </summary>
		/// <param name="View">A buffer containing zero or more valid fields.</param>
		/// <param name="Type">HasFieldType means that View contains the type.Otherwise, use the given type.</param>
		/// <returns></returns>
		public static CbFieldIterator MakeRange(ReadOnlyMemory<byte> View, CbFieldType Type = CbFieldType.HasFieldType)
		{
			return new CbFieldIterator(View, Type);
		}

		/// <summary>
		/// Copy the field range into a buffer of exactly GetRangeSize() bytes.
		/// </summary>
		/// <param name="Buffer"></param>
		public void CopyRangeTo(Span<byte> Buffer)
		{
			ReadOnlyMemory<byte> Source;
			if (TryGetRangeView(out Source))
			{
				Source.Span.CopyTo(Buffer);
			}
			else
			{
				for (CbFieldIterator It = new CbFieldIterator(this); It; It.MoveNext())
				{
					int Size = It.Current.GetSize();
					It.Current.CopyTo(Buffer);
					Buffer = Buffer.Slice(Size);
				}
			}
		}

		/// <summary>
		/// Invoke the visitor for every attachment in the field range.
		/// </summary>
		/// <param name="Visitor"></param>
		public void IterateRangeAttachments(Action<CbField> Visitor)
		{
			// Always iterate over non-uniform ranges because we do not know if they contain an attachment.
			if (CbFieldUtils.HasFieldType(Current.GetTypeWithFlags()))
			{
				for (CbFieldIterator It = new CbFieldIterator(this); It; ++It)
				{
					if (CbFieldUtils.MayContainAttachments(It.Current.GetTypeWithFlags()))
					{
						It.Current.IterateAttachments(Visitor);
					}
				}
			}
			// Only iterate over uniform ranges if the uniform type may contain an attachment.
			else
			{
				if (CbFieldUtils.MayContainAttachments(Current.GetTypeWithFlags()))
				{
					for (CbFieldIterator It = new CbFieldIterator(this); It; ++It)
					{
						It.Current.IterateAttachments(Visitor);
					}
				}
			}
		}

		/// <summary>
		/// Try to get a view of every field in the range as they would be serialized.
		///
		/// A view is available if each field contains its type. Access the equivalent for other field
		/// ranges through FCbFieldIterator::CloneRange or CopyRangeTo.
		/// </summary>
		/// <returns></returns>
		bool TryGetRangeView(out ReadOnlyMemory<byte> OutView)
		{
			throw new NotImplementedException();
/*			FMemoryView View;
			if (FieldType::TryGetView(View))
			{
				OutView = MakeMemoryView(View.GetData(), FieldsEnd);
				return true;
			}
			return false;*/
		}

		/// <summary>
		/// Move to the next element
		/// </summary>
		/// <returns></returns>
		public bool MoveNext()
		{
			if (NextData.Length > 0)
			{
				Current = new CbField(NextData, UniformType);
				NextData = NextData.Slice(Current.Memory.Length);
				return true;
			}
			else
			{
				Current = CbField.Empty;
				return false;
			}
		}

		/// <summary>
		/// Test whether the iterator is valid
		/// </summary>
		/// <param name="Iterator"></param>
		public static implicit operator bool(CbFieldIterator Iterator)
		{
			return Iterator.Current.GetType() != CbFieldType.None;
		}

		/// <summary>
		/// Move to the next item
		/// </summary>
		/// <param name="Iterator"></param>
		/// <returns></returns>
		public static CbFieldIterator operator ++(CbFieldIterator Iterator)
		{
			return new CbFieldIterator(Iterator.NextData, Iterator.UniformType);
		}

		public override bool Equals(object? obj)
		{
			throw new NotImplementedException();
		}

		public override int GetHashCode()
		{
			throw new NotImplementedException();
		}

		public static bool operator ==(CbFieldIterator A, CbFieldIterator B)
		{
			return A.Current.Equals(B.Current);
		}
		public static bool operator !=(CbFieldIterator A, CbFieldIterator B)
		{
			return !A.Current.Equals(B.Current);
		}
	}
	/*
	/// <summary>
	/// Iterator for fields
	/// </summary>
	public class CbFieldIterator : IEnumerable<CbField>
	{
		/// <summary>
		/// The underlying buffer
		/// </summary>
		ReadOnlyMemory<byte> Data;

		/// <summary>
		/// Type for all fields
		/// </summary>
		CbFieldType UniformType;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Data"></param>
		/// <param name="UniformType"></param>
		public CbFieldIterator(ReadOnlyMemory<byte> Data, CbFieldType UniformType)
		{
			this.Data = Data;
			this.UniformType = UniformType;
		}

		/// <inheritdoc/>
		public IEnumerator<CbField> GetEnumerator()
		{
			ReadOnlyMemory<byte> LocalData = Data;
			while (LocalData.Length > 0)
			{
				CbField Field = new CbField(LocalData, UniformType);
				yield return Field;
				LocalData = LocalData.Slice(Field.Memory.Length);
			}
		}

		/// <inheritdoc/>
		IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
	}
	*/
	/// <summary>
	/// Array of CbField that have no names.
	///
	/// Accessing a field of the array requires iteration. Access by index is not provided because the
	/// cost of accessing an item by index scales linearly with the index.
	/// </summary>
	public class CbArray : IEnumerable<CbField>
	{
		/// <summary>
		/// The field containing this array
		/// </summary>
		readonly CbField InnerField;

		/// <summary>
		/// Empty array constant
		/// </summary>
		public static CbArray Empty { get; } = new CbArray(new byte[] { (byte)CbFieldType.Array, 1, 0 });

		/// <summary>
		/// Construct an array with no fields
		/// </summary>
		public CbArray()
		{
			InnerField = Empty.InnerField;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Field"></param>
		private CbArray(CbField Field)
		{
			InnerField = Field;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Data"></param>
		/// <param name="Type"></param>
		public CbArray(ReadOnlyMemory<byte> Data, CbFieldType Type = CbFieldType.HasFieldType)
		{
			InnerField = new CbField(Data, Type);
		}

		/// <summary>
		/// Returns the number of items in the array.
		/// </summary>
		/// <returns></returns>
		public int Num()
		{
			ReadOnlyMemory<byte> PayloadBytes = InnerField.Payload;
			PayloadBytes = PayloadBytes.Slice(BitUtils.MeasureVarUInt(PayloadBytes.Span));
			return (int)BitUtils.ReadVarUInt(PayloadBytes.Span, out int NumByteCount);
		}

		/// <summary>
		/// Access the array as an array field.
		/// </summary>
		/// <returns></returns>
		public CbField AsField() => InnerField;

		/// <summary>
		/// Access the array as an array field.
		/// </summary>
		/// <returns></returns>
		public CbField AsFieldView() => InnerField;

		/// <summary>
		/// Construct an array from an array field. No type check is performed!
		/// </summary>
		/// <param name="Field"></param>
		/// <returns></returns>
		public static CbArray FromFieldNoCheck(CbField Field) => new CbArray(Field);

		/// <summary>
		/// Whether the array has any fields.
		/// </summary>
		/// <param name="Array"></param>
		public static explicit operator bool(CbArray Array) => Array.Num() > 0;

		/// <summary>
		/// Returns the size of the array in bytes if serialized by itself with no name.
		/// </summary>
		/// <returns></returns>
		public int GetSize()
		{
			return (int)Math.Min((ulong)sizeof(CbFieldType) + InnerField.GetPayloadSize(), int.MaxValue);
		}

		/// <summary>
		/// Calculate the hash of the array if serialized by itself with no name.
		/// </summary>
		/// <returns></returns>
		public Blake3Hash GetHash()
		{
			Blake3.Hasher Hasher = Blake3.Hasher.New();
			AppendHash(Hasher);

			byte[] Result = new byte[Blake3Hash.NumBytes];
			Hasher.Finalize(Result);

			return new Blake3Hash(Result);
		}

		/// <summary>
		/// Append the hash of the array if serialized by itself with no name.
		/// </summary>
		public void AppendHash(Blake3.Hasher Hasher)
		{
			byte[] SerializedType = new byte[] { (byte)InnerField.GetType() };
			Hasher.Update(SerializedType);
			Hasher.Update(InnerField.Payload.Span);
		}

		/// <inheritdoc/>
		public override bool Equals(object? Obj) => Equals(Obj as CbArray);

		/// <inheritdoc/>
		public override int GetHashCode() => BinaryPrimitives.ReadInt32BigEndian(GetHash().Span);

		/// <summary>
		/// Whether this array is identical to the other array.
		///
		/// Performs a deep comparison of any contained arrays or objects and their fields. Comparison
		/// assumes that both fields are valid and are written in the canonical format.Fields must be
		/// written in the same order in arrays and objects, and name comparison is case sensitive.If
		/// these assumptions do not hold, this may return false for equivalent inputs. Validation can
		/// be done with the All mode to check these assumptions about the format of the inputs.
		/// </summary>
		/// <param name="Other"></param>
		/// <returns></returns>
		public bool Equals(CbArray? Other)
		{
			return Other != null && GetType() == Other.GetType() && GetPayloadView().Span.SequenceEqual(Other.GetPayloadView().Span);
		}

		/// <summary>
		/// Copy the array into a buffer of exactly GetSize() bytes, with no name.
		/// </summary>
		/// <param name="Buffer"></param>
		public void CopyTo(Span<byte> Buffer)
		{
			Buffer[0] = (byte)GetType();
			GetPayloadView().Span.CopyTo(Buffer.Slice(1));
		}

		/** Invoke the visitor for every attachment in the array. */
		public void IterateAttachments(Action<CbField> Visitor) => CreateViewIterator().IterateRangeAttachments(Visitor);

		/// <summary>
		/// Try to get a view of the array as it would be serialized, such as by CopyTo.
		/// 
		/// A view is available if the array contains its type and has no name. Access the equivalent
		/// for other arrays through FCbArray::GetBuffer, FCbArray::Clone, or CopyTo.
		/// </summary>
		public bool TryGetView(out ReadOnlyMemory<byte> OutView)
		{
			if(InnerField.HasName())
			{
				OutView = ReadOnlyMemory<byte>.Empty;
				return false;
			}
			return InnerField.TryGetView(out OutView);
		}

		/// <inheritdoc cref="CbField.CreateIterator"/>
		public CbFieldIterator CreateIterator() => InnerField.CreateIterator();

		/// <inheritdoc cref="CbField.CreateViewIterator"/>
		public CbFieldIterator CreateViewIterator() => InnerField.CreateViewIterator();

		/// <inheritdoc/>
		public IEnumerator<CbField> GetEnumerator() => InnerField.GetEnumerator();

		/// <inheritdoc/>
		IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

		#region Mimic inheritance from CbField

		/// <inheritdoc cref="CbField.GetType"/>
		internal new CbFieldType GetType() => InnerField.GetType();

		/// <inheritdoc cref="CbField.GetPayloadView"/>
		internal ReadOnlyMemory<byte> GetPayloadView() => InnerField.GetPayloadView();

		#endregion

		#region Mimic inheritance from TCbBufferFactory

		public static CbArray Clone(ReadOnlyMemory<byte> Data) => Clone(new CbArray(Data));
		public static CbArray Clone(CbArray Other) => Other;
		public static CbArray MakeView(ReadOnlyMemory<byte> Data) => new CbArray(Data);
		public static CbArray MakeView(CbArray Other) => Other;

		#endregion
	}

	/// <summary>
	/// Array of CbField that have unique names.
	///
	/// Accessing the fields of an object is always a safe operation, even if the requested field does
	/// not exist. Fields may be accessed by name or through iteration. When a field is requested that
	/// is not found in the object, the field that it returns has no value (evaluates to false) though
	/// attempting to access the empty field is also safe, as described by FCbFieldView.
	/// </summary>
	public class CbObject : IEnumerable<CbField>
	{
		/// <summary>
		/// Empty array constant
		/// </summary>
		public static CbObject Empty = CbObject.FromFieldNoCheck(new CbField(new byte[] { (byte)CbFieldType.Object, 0 }));

		/// <summary>
		/// The inner field object
		/// </summary>
		private CbField InnerField;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Buffer"></param>
		private CbObject(CbField Field)
		{
			InnerField = new CbField(Field.Memory, Field.TypeWithFlags);
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Buffer"></param>
		public CbObject(ReadOnlyMemory<byte> Buffer, CbFieldType FieldType = CbFieldType.HasFieldType)
		{
			InnerField = new CbField(Buffer, FieldType);
		}

		/// <inheritdoc cref="FindView(ReadOnlyUtf8String)"/>
		public CbField Find(ReadOnlyUtf8String Name) => FindView(Name);

		/// <summary>
		/// Find a field by case-sensitive name comparison.
		///
		/// The cost of this operation scales linearly with the number of fields in the object. Prefer to
		/// iterate over the fields only once when consuming an object.
		/// </summary>
		/// <param name="Name">The name of the field.</param>
		/// <returns>The matching field if found, otherwise a field with no value.</returns>
		public CbField FindView(ReadOnlyUtf8String Name) => InnerField[Name];

		/// <inheritdoc cref="FindView(ReadOnlyUtf8String)"/>
		public CbField FindIgnoreCase(ReadOnlyUtf8String Name) => FindViewIgnoreCase(Name);

		/// <summary>
		/// Find a field by case-insensitive name comparison.
		/// </summary>
		/// <param name="Name">The name of the field.</param>
		/// <returns>The matching field if found, otherwise a field with no value.</returns>
		public CbField FindViewIgnoreCase(ReadOnlyUtf8String Name) => InnerField.FirstOrDefault(Field => ReadOnlyUtf8StringComparer.OrdinalIgnoreCase.Equals(Field.Name, Name)) ?? new CbField();

		/// <summary>
		/// Find a field by case-sensitive name comparison.
		/// </summary>
		/// <param name="Name">The name of the field.</param>
		/// <returns>The matching field if found, otherwise a field with no value.</returns>
		public CbField this[ReadOnlyUtf8String Name] => InnerField[Name];

		/// <inheritdoc cref="AsFieldView"/>
		public CbField AsField() => InnerField;

		/// <summary>
		/// Access the object as an object field.
		/// </summary>
		/// <returns></returns>
		public CbField AsFieldView() => InnerField;

		/// <summary>
		/// Construct an object from an object field. No type check is performed!
		/// </summary>
		/// <param name="Field"></param>
		/// <returns></returns>
		public static CbObject FromFieldNoCheck(CbField Field) => new CbObject(Field);

		/// <summary>
		/// Whether the object has any fields.
		/// </summary>
		/// <param name="Object"></param>
		public static explicit operator bool(CbObject Object)
		{
			throw new NotImplementedException();
		}

		/// <summary>
		/// Returns the size of the object in bytes if serialized by itself with no name.
		/// </summary>
		/// <returns></returns>
		public int GetSize()
		{
			return sizeof(CbFieldType) + InnerField.Payload.Length;
		}

		/// <summary>
		/// Calculate the hash of the object if serialized by itself with no name.
		/// </summary>
		/// <returns></returns>
		public Blake3Hash GetHash()
		{
			Blake3.Hasher Hasher = Blake3.Hasher.New();
			AppendHash(Hasher);

			byte[] Data = new byte[Blake3Hash.NumBytes];
			Hasher.Finalize(Data);

			return new Blake3Hash(Data);
		}

		/// <summary>
		/// Append the hash of the object if serialized by itself with no name.
		/// </summary>
		/// <param name="Hasher"></param>
		public void AppendHash(Blake3.Hasher Hasher)
		{
			byte[] Temp = new byte[] { (byte)InnerField.GetType() };
			Hasher.Update(Temp);
			Hasher.Update(InnerField.Payload.Span);
		}

		/// <inheritdoc/>
		public override bool Equals(object? Obj) => Equals(Obj as CbObject);

		/// <inheritdoc/>
		public override int GetHashCode() => BinaryPrimitives.ReadInt32BigEndian(GetHash().Span);

		/// <summary>
		/// Whether this object is identical to the other object.
		/// 
		/// Performs a deep comparison of any contained arrays or objects and their fields. Comparison
		/// assumes that both fields are valid and are written in the canonical format. Fields must be
		/// written in the same order in arrays and objects, and name comparison is case sensitive. If
		/// these assumptions do not hold, this may return false for equivalent inputs. Validation can
		/// be done with the All mode to check these assumptions about the format of the inputs.
		/// </summary>
		/// <param name="Other"></param>
		/// <returns></returns>
		public bool Equals(CbObject? Other)
		{
			return Other != null && InnerField.GetType() == Other.InnerField.GetType() && InnerField.Payload.Span.SequenceEqual(Other.InnerField.Payload.Span);
		}

		/// <summary>
		/// Copy the object into a buffer of exactly GetSize() bytes, with no name.
		/// </summary>
		/// <param name="Buffer"></param>
		public void CopyTo(Span<byte> Buffer)
		{
			Buffer[0] = (byte)InnerField.GetType();
			InnerField.Payload.Span.CopyTo(Buffer.Slice(1));
		}

		/// <summary>
		/// Invoke the visitor for every attachment in the object.
		/// </summary>
		/// <param name="Visitor"></param>
		public void IterateAttachments(Action<CbField> Visitor) => CreateViewIterator().IterateRangeAttachments(Visitor);

		/// <summary>
		/// Try to get a view of the object as it would be serialized, such as by CopyTo.
		/// 
		/// A view is available if the object contains its type and has no name. Access the equivalent
		/// for other objects through FCbObject::GetBuffer, FCbObject::Clone, or CopyTo.
		/// </summary>
		/// <param name="OutView"></param>
		/// <returns></returns>
		public bool TryGetView(out ReadOnlyMemory<byte> OutView)
		{
			if (InnerField.HasName())
			{
				OutView = ReadOnlyMemory<byte>.Empty;
				return false;
			}
			return InnerField.TryGetView(out OutView);
		}

		/// <inheritdoc cref="CbField.CreateIterator"/>
		public CbFieldIterator CreateIterator() => InnerField.CreateIterator();

		/// <inheritdoc cref="CbField.CreateViewIterator"/>
		public CbFieldIterator CreateViewIterator() => InnerField.CreateViewIterator();

		/// <inheritdoc/>
		public IEnumerator<CbField> GetEnumerator() => InnerField.GetEnumerator();

		/// <inheritdoc/>
		IEnumerator IEnumerable.GetEnumerator() => InnerField.GetEnumerator();

		/// <summary>
		/// Clone this object
		/// </summary>
		/// <param name="Object"></param>
		/// <returns></returns>
		public static CbObject Clone(CbObject Object) => Object;

		#region Conversion to Json
		/// <summary>
		/// Convert this object to JSON
		/// </summary>
		/// <returns></returns>
		public string ToJson()
		{
			ArrayBufferWriter<byte> Buffer = new ArrayBufferWriter<byte>();
			using (Utf8JsonWriter JsonWriter = new Utf8JsonWriter(Buffer))
			{
				ToJson(JsonWriter);
			}
			return Encoding.UTF8.GetString(Buffer.WrittenMemory.Span);
		}

		/// <summary>
		/// Write this object to JSON
		/// </summary>
		/// <param name="Writer"></param>
		public void ToJson(Utf8JsonWriter Writer)
		{
			Writer.WriteStartObject();
			foreach (CbField Field in InnerField)
			{
				WriteField(Field, Writer);
			}
			Writer.WriteEndObject();
		}

		/// <summary>
		/// Write a single field to a writer
		/// </summary>
		/// <param name="Field"></param>
		/// <param name="Writer"></param>
		private static void WriteField(CbField Field, Utf8JsonWriter Writer)
		{
			if (Field.IsObject())
			{
				Writer.WriteStartObject();
				CbObject Object = Field.AsObject();
				foreach (CbField ObjectField in Object.InnerField)
				{
					WriteField(ObjectField, Writer);
				}
				Writer.WriteEndObject();
			}
			else if (Field.IsArray())
			{
				Writer.WriteStartArray();
				Writer.WriteEndArray();
			}
			else if (Field.IsInteger())
			{
				if (Field.GetType() == CbFieldType.IntegerNegative)
				{
					Writer.WriteNumber(Field.Name.Span, -Field.AsInt64());
				}
				else
				{
					Writer.WriteNumber(Field.Name.Span, Field.AsUInt64());
				}
			}
			else if (Field.IsBool())
			{
				Writer.WriteBoolean(Field.Name.Span, Field.AsBool());
			}
			else if (Field.IsNull())
			{
				Writer.WriteNullValue();
			}
			else if (Field.IsDateTime())
			{
				Writer.WriteString(Field.Name.Span, Field.AsDateTime());
			}
			else if (Field.IsHash())
			{
				Writer.WriteString(Field.Name.Span, StringUtils.FormatUtf8HexString(Field.AsHash().Span).Span);
			}
			else if (Field.IsString())
			{
				Writer.WriteString(Field.Name.Span, Field.AsString().Span);
			}
			else
			{
				throw new NotImplementedException($"Unhandled type {Field.GetType()} when attempting to convert to json");
			}
		}
		#endregion
	}

	/// <summary>
	/// Methods for reading VarUInt values
	/// </summary>
	public static class BitUtils
	{
		/// <summary>
		/// Extracts the payload from a buffer prefixed with a variable-length value
		/// </summary>
		/// <param name="Memory"></param>
		/// <returns></returns>
		public static ReadOnlyMemory<byte> GetVarUIntSizedPayload(ReadOnlyMemory<byte> Memory)
		{
			ulong Length = ReadVarUInt(Memory.Span, out int BytesRead);
			return Memory.Slice(BytesRead, (int)Length);
		}

		/// <summary>
		/// Read a variable-length unsigned integer.
		/// </summary>
		/// <param name="Buffer">A variable-length encoding of an unsigned integer</param>
		/// <param name="BytesRead">The number of bytes consumed from the input</param>
		/// <returns></returns>
		public static ulong ReadVarUInt(ReadOnlySpan<byte> Buffer, out int BytesRead)
		{
			BytesRead = (int)MeasureVarUInt(Buffer);

			ulong Value = (ulong)(Buffer[0] & (0xff >> BytesRead));
			for (int i = 1; i < BytesRead; i++)
			{
				Value <<= 8;
				Value |= Buffer[i];
			}
			return Value;
		}

		/// <summary>
		/// Measure the length in bytes (1-9) of an encoded variable-length integer.
		/// </summary>
		/// <param name="Buffer">A variable-length encoding of an(signed or unsigned) integer.</param>
		/// <returns>The number of bytes used to encode the integer, in the range 1-9.</returns>
		public static int MeasureVarUInt(ReadOnlySpan<byte> Buffer)
		{
			byte b = Buffer[0];
			b = (byte)~b;
			return BitOperations.LeadingZeroCount(b) - 23;
		}

		/// <summary>
		/// Measure the number of bytes (1-5) required to encode the 32-bit input.
		/// </summary>
		/// <param name="Value"></param>
		/// <returns></returns>
		public static int MeasureVarUInt(int Value)
		{
			return MeasureVarUInt((uint)Value);
		}

		/// <summary>
		/// Measure the number of bytes (1-5) required to encode the 32-bit input.
		/// </summary>
		/// <param name="Value"></param>
		/// <returns></returns>
		public static int MeasureVarUInt(uint Value)
		{
			return BitOperations.Log2(Value) / 7 + 1;
		}

		/// <summary>
		/// Measure the number of bytes (1-9) required to encode the 64-bit input.
		/// </summary>
		/// <param name="value"></param>
		/// <returns></returns>
		public static int MeasureVarUInt(ulong value)
		{
			return Math.Min(BitOperations.Log2(value) / 7 + 1, 9);
		}

		/// <summary>
		/// Write a variable-length unsigned integer.
		/// </summary>
		/// <param name="Value">An unsigned integer to encode</param>
		/// <param name="Buffer">A buffer of at least 9 bytes to write the output to.</param>
		/// <param name="BufferOffset"></param>
		/// <returns>The number of bytes used in the output</returns>
		public static int WriteVarUInt(long Value, byte[] Buffer, int BufferOffset = 0)
		{
			return WriteVarUInt((ulong)Value, Buffer, BufferOffset);
		}

		/// <summary>
		/// Write a variable-length unsigned integer.
		/// </summary>
		/// <param name="Value">An unsigned integer to encode</param>
		/// <param name="Buffer">A buffer of at least 9 bytes to write the output to.</param>
		/// <param name="BufferOffset"></param>
		/// <returns>The number of bytes used in the output</returns>
		public static int WriteVarUInt(ulong Value, byte[] Buffer, int BufferOffset = 0)
		{
			int ByteCount = MeasureVarUInt(Value);

			for (uint Idx = 1; Idx < ByteCount; Idx++)
			{
				Buffer[BufferOffset + ByteCount - Idx] = (byte)Value;
				Value >>= 8;
			}
			Buffer[BufferOffset] = (byte)((0xff << (9 - (int)ByteCount)) | (byte)Value);
			return ByteCount;
		}
	}
}

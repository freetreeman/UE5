// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBuildOutput.h"

#include "Algo/AllOf.h"
#include "Algo/BinarySearch.h"
#include "Containers/StringConv.h"
#include "Containers/StringView.h"
#include "Containers/UnrealString.h"
#include "DerivedDataBuildDefinition.h"
#include "DerivedDataBuildKey.h"
#include "DerivedDataBuildPrivate.h"
#include "DerivedDataCacheRecord.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryWriter.h"

namespace UE::DerivedData::Private
{

static FStringView LexToString(EBuildDiagnosticLevel Level)
{
	switch (Level)
	{
	default: return TEXT("Unknown"_SV);
	case EBuildDiagnosticLevel::Error: return TEXT("Error"_SV);
	case EBuildDiagnosticLevel::Warning: return TEXT("Warning"_SV);
	}
}

static void LexFromString(EBuildDiagnosticLevel& OutLevel, FAnsiStringView String)
{
	if (String.Equals("Warning"_ASV, ESearchCase::CaseSensitive))
	{
		OutLevel = EBuildDiagnosticLevel::Warning;
	}
	else
	{
		OutLevel = EBuildDiagnosticLevel::Error;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FBuildOutputBuilderInternal final : public IBuildOutputBuilderInternal
{
public:
	explicit FBuildOutputBuilderInternal(FStringView InName, FStringView InFunction)
		: Name(InName)
		, Function(InFunction)
		, bHasError(false)
		, bHasDiagnostics(false)
	{
		checkf(!Name.IsEmpty(), TEXT("A build output requires a non-empty name."));
		AssertValidBuildFunctionName(Function, Name);
		DiagnosticsWriter.BeginArray();
	}

	~FBuildOutputBuilderInternal() final = default;

	void SetMeta(FCbObject&& InMeta) final { Meta = MoveTemp(InMeta); }
	void AddPayload(const FPayload& Payload) final;
	void AddDiagnostic(const FBuildDiagnostic& Diagnostic) final;
	bool HasError() const final { return bHasError; }
	FBuildOutput Build() final;

	FString Name;
	FString Function;
	FCbObject Meta;
	TArray<FPayload> Payloads;
	FCbField Diagnostics;
	FCbWriter DiagnosticsWriter;
	bool bHasError;
	bool bHasDiagnostics;
};

class FBuildOutputInternal final : public IBuildOutputInternal
{
public:
	explicit FBuildOutputInternal(FBuildOutputBuilderInternal&& Output);
	explicit FBuildOutputInternal(FStringView Name, FStringView Function, const FCbObject& Output, bool& bOutIsValid);
	explicit FBuildOutputInternal(FStringView Name, FStringView Function, const FCacheRecord& Output, bool& bOutIsValid);

	~FBuildOutputInternal() final = default;

	FStringView GetName() const final { return Name; }
	FStringView GetFunction() const final { return Function; }

	const FCbObject& GetMeta() const final { return Meta; }

	const FPayload& GetPayload(const FPayloadId& Id) const final;
	TConstArrayView<FPayload> GetPayloads() const final { return Payloads; }
	void IterateDiagnostics(TFunctionRef<void (const FBuildDiagnostic& Diagnostic)> Visitor) const final;
	bool HasError() const final;

	void Save(FCbWriter& Writer) const final;
	void Save(FCacheRecordBuilder& RecordBuilder) const final;

	bool IsValid() const;

	inline void AddRef() const final
	{
		ReferenceCount.fetch_add(1, std::memory_order_relaxed);
	}

	inline void Release() const final
	{
		if (ReferenceCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
		{
			delete this;
		}
	}

private:
	FString Name;
	FString Function;
	FCbObject Meta;
	TArray<FPayload> Payloads;
	FCbField Diagnostics;
	mutable std::atomic<uint32> ReferenceCount{0};
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FBuildOutputInternal::FBuildOutputInternal(FBuildOutputBuilderInternal&& InOutput)
	: Name(MoveTemp(InOutput.Name))
	, Function(MoveTemp(InOutput.Function))
	, Meta(MoveTemp(InOutput.Meta))
	, Payloads(MoveTemp(InOutput.Payloads))
	, Diagnostics(MoveTemp(InOutput.Diagnostics))
{
	if (InOutput.bHasError)
	{
		Payloads.Empty();
	}
}

FBuildOutputInternal::FBuildOutputInternal(FStringView InName, FStringView InFunction, const FCbObject& InOutput, bool& bOutIsValid)
	: Name(InName)
	, Function(InFunction)
{
	bOutIsValid = false;
	checkf(!Name.IsEmpty(), TEXT("A build output requires a non-empty name."));
	AssertValidBuildFunctionName(Function, Name);
	Meta = InOutput["Meta"_ASV].AsObject();
	Meta.MakeOwned();
	for (FCbFieldView Payload : InOutput["Payloads"_ASV])
	{
		const FPayloadId Id(Payload["Id"_ASV].AsObjectId());
		const FIoHash& RawHash = Payload["RawHash"_ASV].AsAttachment();
		if (Id.IsNull() || RawHash.IsZero() || !Payload["RawSize"_ASV].IsInteger())
		{
			return;
		}
		Payloads.Emplace(Id, RawHash, Payload["RawSize"_ASV].AsUInt64());
	}
	Diagnostics = InOutput["Diagnostics"_ASV];
	Diagnostics.MakeOwned();
	bOutIsValid = IsValid() && (!InOutput.AsView()["Meta"_ASV] || InOutput.AsView()["Meta"_ASV].IsObject());
}

FBuildOutputInternal::FBuildOutputInternal(FStringView InName, FStringView InFunction, const FCacheRecord& InOutput, bool& bOutIsValid)
	: Name(InName)
	, Function(InFunction)
	, Meta(InOutput.GetMeta())
	, Payloads(InOutput.GetAttachmentPayloads())
{
	checkf(!Name.IsEmpty(), TEXT("A build output requires a non-empty name."));
	AssertValidBuildFunctionName(Function, Name);
	if (FSharedBuffer Buffer = InOutput.GetValue())
	{
		Diagnostics = FCbObject(MoveTemp(Buffer))["Diagnostics"_ASV];
	}
	bOutIsValid = IsValid();
}

const FPayload& FBuildOutputInternal::GetPayload(const FPayloadId& Id) const
{
	const int32 Index = Algo::BinarySearchBy(Payloads, Id, &FPayload::GetId);
	return Payloads.IsValidIndex(Index) ? Payloads[Index] : FPayload::Null;
}

void FBuildOutputInternal::IterateDiagnostics(TFunctionRef<void (const FBuildDiagnostic& Diagnostic)> Visitor) const
{
	for (FCbFieldView Diagnostic : Diagnostics.CreateViewIterator())
	{
		EBuildDiagnosticLevel Level;
		LexFromString(Level, Diagnostic["Level"_ASV].AsString());
		const FAnsiStringView Category = Diagnostic["Category"_ASV].AsString();
		const FAnsiStringView Message = Diagnostic["Message"_ASV].AsString();
		Visitor(FBuildDiagnostic{FUTF8ToTCHAR(Category), FUTF8ToTCHAR(Message), Level});
	}
}

bool FBuildOutputInternal::HasError() const
{
	for (FCbFieldView Field : Diagnostics.CreateViewIterator())
	{
		const FCbObjectView Diagnostic = Field.AsObjectView();
		EBuildDiagnosticLevel Level;
		LexFromString(Level, Diagnostic["Level"_ASV].AsString());
		if (Level == EBuildDiagnosticLevel::Error)
		{
			return true;
		}
	}
	return false;
}

void FBuildOutputInternal::Save(FCbWriter& Writer) const
{
	Writer.BeginObject();
	if (!Payloads.IsEmpty())
	{
		Writer.BeginArray("Payloads"_ASV);
		for (const FPayload& Payload : Payloads)
		{
			Writer.BeginObject();
			Writer.AddObjectId("Id"_ASV, FCbObjectId(Payload.GetId()));
			Writer.AddBinaryAttachment("RawHash"_ASV, Payload.GetRawHash());
			Writer.AddInteger("RawSize"_ASV, Payload.GetRawSize());
			Writer.EndObject();
		}
		Writer.EndArray();
	}
	if (Diagnostics)
	{
		Writer.AddField("Diagnostics"_ASV, Diagnostics);
	}
	if (Meta)
	{
		Writer.AddObject("Meta"_ASV, Meta);
	}
	Writer.EndObject();
}

void FBuildOutputInternal::Save(FCacheRecordBuilder& RecordBuilder) const
{
	RecordBuilder.SetMeta(FCbObject(Meta));
	if (Diagnostics)
	{
		TCbWriter<128> Value;
		Value.BeginObject();
		Value.AddField("Diagnostics"_ASV, Diagnostics);
		Value.EndObject();
		RecordBuilder.SetValue(Value.Save().GetBuffer());
	}
	for (const FPayload& Payload : Payloads)
	{
		RecordBuilder.AddAttachment(Payload);
	}
}

bool FBuildOutputInternal::IsValid() const
{
	return IsValidBuildFunctionName(Function)
		&& Algo::AllOf(Payloads, [](const FPayload& Payload) { return !Payload.GetRawHash().IsZero(); })
		&& (!Diagnostics || Diagnostics.IsArray())
		&& Algo::AllOf(Diagnostics.CreateViewIterator(), [](FCbFieldView Field)
			{
				return Field.IsObject()
					&& Field["Level"_ASV].AsString().Len() > 0
					&& Field["Category"_ASV].AsString().Len() > 0
					&& Field["Message"_ASV].AsString().Len() > 0;
			});
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FBuildOutputBuilderInternal::AddPayload(const FPayload& Payload)
{
	checkf(Payload, TEXT("Null payload added in output for build of '%s' by %s."), *Name, *Function);
	const FPayloadId& Id = Payload.GetId();
	const int32 Index = Algo::LowerBoundBy(Payloads, Id, &FPayload::GetId);
	checkf(!(Payloads.IsValidIndex(Index) && Payloads[Index].GetId() == Id),
		TEXT("Duplicate ID %s used by payload for build of '%s' by %s."), *WriteToString<32>(Id), *Name, *Function);
	Payloads.Insert(Payload, Index);
}

void FBuildOutputBuilderInternal::AddDiagnostic(const FBuildDiagnostic& Diagnostic)
{
	bHasError |= Diagnostic.Level == EBuildDiagnosticLevel::Error;
	bHasDiagnostics = true;
	DiagnosticsWriter.BeginObject();
	DiagnosticsWriter.AddString("Level"_ASV, LexToString(Diagnostic.Level));
	DiagnosticsWriter.AddString("Category"_ASV, Diagnostic.Category);
	DiagnosticsWriter.AddString("Message"_ASV, Diagnostic.Message);
	DiagnosticsWriter.EndObject();
}

FBuildOutput FBuildOutputBuilderInternal::Build()
{
	DiagnosticsWriter.EndArray();
	if (bHasDiagnostics)
	{
		Diagnostics = DiagnosticsWriter.Save();
	}
	DiagnosticsWriter.Reset();
	return CreateBuildOutput(new FBuildOutputInternal(MoveTemp(*this)));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FBuildOutput CreateBuildOutput(IBuildOutputInternal* Output)
{
	return FBuildOutput(Output);
}

FBuildOutputBuilder CreateBuildOutputBuilder(IBuildOutputBuilderInternal* OutputBuilder)
{
	return FBuildOutputBuilder(OutputBuilder);
}

FBuildOutputBuilder CreateBuildOutput(FStringView Name, FStringView Function)
{
	return CreateBuildOutputBuilder(new FBuildOutputBuilderInternal(Name, Function));
}

FOptionalBuildOutput LoadBuildOutput(FStringView Name, FStringView Function, const FCbObject& Output)
{
	bool bIsValid = false;
	FOptionalBuildOutput Out = CreateBuildOutput(new FBuildOutputInternal(Name, Function, Output, bIsValid));
	if (!bIsValid)
	{
		Out.Reset();
	}
	return Out;
}

FOptionalBuildOutput LoadBuildOutput(FStringView Name, FStringView Function, const FCacheRecord& Output)
{
	bool bIsValid = false;
	FOptionalBuildOutput Out = CreateBuildOutput(new FBuildOutputInternal(Name, Function, Output, bIsValid));
	if (!bIsValid)
	{
		Out.Reset();
	}
	return Out;
}

} // UE::DerivedData::Private

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/ArrayView.h"
#include "Containers/StringView.h"
#include "Misc/ScopeExit.h"
#include "Templates/Function.h"
#include "Templates/RefCounting.h"
#include "Templates/UniquePtr.h"

class FCbObject;
class FCbWriter;

namespace UE::DerivedData { class FBuildOutput; }
namespace UE::DerivedData { class FBuildOutputBuilder; }
namespace UE::DerivedData { class FCacheRecordBuilder; }
namespace UE::DerivedData { class FPayload; }
namespace UE::DerivedData { struct FBuildDiagnostic; }
namespace UE::DerivedData { struct FPayloadId; }

namespace UE::DerivedData::Private
{

class IBuildOutputInternal
{
public:
	virtual ~IBuildOutputInternal() = default;
	virtual FStringView GetName() const = 0;
	virtual FStringView GetFunction() const = 0;
	virtual const FCbObject& GetMeta() const = 0;
	virtual const FPayload& GetPayload(const FPayloadId& Id) const = 0;
	virtual TConstArrayView<FPayload> GetPayloads() const = 0;
	virtual void IterateDiagnostics(TFunctionRef<void (const FBuildDiagnostic& Diagnostic)> Visitor) const = 0;
	virtual bool HasError() const = 0;
	virtual void Save(FCbWriter& Writer) const = 0;
	virtual void Save(FCacheRecordBuilder& RecordBuilder) const = 0;
	virtual void AddRef() const = 0;
	virtual void Release() const = 0;
};

FBuildOutput CreateBuildOutput(IBuildOutputInternal* Output);

class IBuildOutputBuilderInternal
{
public:
	virtual ~IBuildOutputBuilderInternal() = default;
	virtual void SetMeta(FCbObject&& Meta) = 0;
	virtual void AddPayload(const FPayload& Payload) = 0;
	virtual void AddDiagnostic(const FBuildDiagnostic& Diagnostic) = 0;
	virtual bool HasError() const = 0;
	virtual FBuildOutput Build() = 0;
};

FBuildOutputBuilder CreateBuildOutputBuilder(IBuildOutputBuilderInternal* OutputBuilder);

} // UE::DerivedData::Private

namespace UE::DerivedData
{

/** Level of severity for build diagnostics. */
enum class EBuildDiagnosticLevel : uint8
{
	/** Errors always indicates a failure of the corresponding build. */
	Error,
	/** Warnings are expected to be actionable issues found while executing a build. */
	Warning,
};

/** A build diagnostic is a message logged by a build. */
struct FBuildDiagnostic
{
	/** The name of the category of the diagnostic. */
	FStringView Category;
	/** The message of the diagnostic. */
	FStringView Message;
	/** The level of severity of the diagnostic. */
	EBuildDiagnosticLevel Level;
};

/**
 * A build output is an immutable container of payloads and diagnostics produced by a build.
 *
 * The output will not contain any payloads if it has any errors.
 *
 * The output can be requested without data, which means that the payloads will have null data.
 */
class FBuildOutput
{
public:
	/** Returns the name by which to identify this output for logging and profiling. */
	inline FStringView GetName() const { return Output->GetName(); }

	/** Returns the name of the build function that produced this output. */
	inline FStringView GetFunction() const { return Output->GetFunction(); }

	/** Returns the optional metadata. */
	inline const FCbObject& GetMeta() const { return Output->GetMeta(); }

	/** Returns the payload matching the ID. Null if no match. Buffer is null if skipped. */
	inline const FPayload& GetPayload(const FPayloadId& Id) const { return Output->GetPayload(Id); }

	/** Returns the payloads in the output in order by ID. */
	inline TConstArrayView<FPayload> GetPayloads() const { return Output->GetPayloads(); }

	/** Visits every diagnostic in the order it was recorded. */
	inline void IterateDiagnostics(TFunctionRef<void (const FBuildDiagnostic& Diagnostic)> Visitor) const
	{
		Output->IterateDiagnostics(Visitor);
	}

	/** Returns whether the output has any error diagnostics. */
	inline bool HasError() const { return Output->HasError(); }

	/** Saves the build output to a compact binary object with payloads as attachments. */
	void Save(FCbWriter& Writer) const
	{
		Output->Save(Writer);
	}

	/** Saves the build output to a cache record. */
	void Save(FCacheRecordBuilder& RecordBuilder) const
	{
		Output->Save(RecordBuilder);
	}

private:
	friend class FOptionalBuildOutput;
	friend FBuildOutput Private::CreateBuildOutput(Private::IBuildOutputInternal* Output);

	inline explicit FBuildOutput(Private::IBuildOutputInternal* InOutput)
		: Output(InOutput)
	{
	}

	TRefCountPtr<Private::IBuildOutputInternal> Output;
};

/**
 * A build output builder is used to construct a build output.
 *
 * Create using IBuild::CreateOutput().
 *
 * @see FBuildOutput
 */
class FBuildOutputBuilder
{
public:
	/** Set the metadata for the build output. Holds a reference and is cloned if not owned. */
	inline void SetMeta(FCbObject&& Meta)
	{
		return OutputBuilder->SetMeta(MoveTemp(Meta));
	}

	/** Add a payload to the output. The ID must be unique in this output. */
	inline void AddPayload(const FPayload& Payload)
	{
		OutputBuilder->AddPayload(Payload);
	}

	/** Add an error diagnostic to the output. */
	inline void AddError(FStringView Category, FStringView Message)
	{
		OutputBuilder->AddDiagnostic({Category, Message, EBuildDiagnosticLevel::Error});
	}

	/** Add a warning diagnostic to the output. */
	inline void AddWarning(FStringView Category, FStringView Message)
	{
		OutputBuilder->AddDiagnostic({Category, Message, EBuildDiagnosticLevel::Warning});
	}

	/** Returns whether the output has any error diagnostics. */
	inline bool HasError() const
	{
		return OutputBuilder->HasError();
	}

	/** Build a build output, which makes this builder subsequently unusable. */
	inline FBuildOutput Build()
	{
		ON_SCOPE_EXIT { OutputBuilder = nullptr; };
		return OutputBuilder->Build();
	}

private:
	friend FBuildOutputBuilder Private::CreateBuildOutputBuilder(Private::IBuildOutputBuilderInternal* OutputBuilder);

	/** Construct a build output builder. Use IBuild::CreateOutput(). */
	inline explicit FBuildOutputBuilder(Private::IBuildOutputBuilderInternal* InOutputBuilder)
		: OutputBuilder(InOutputBuilder)
	{
	}

	TUniquePtr<Private::IBuildOutputBuilderInternal> OutputBuilder;
};

/**
 * A build output that can be null.
 *
 * @see FBuildOutput
 */
class FOptionalBuildOutput : private FBuildOutput
{
public:
	inline FOptionalBuildOutput() : FBuildOutput(nullptr) {}

	inline FOptionalBuildOutput(FBuildOutput&& InOutput) : FBuildOutput(MoveTemp(InOutput)) {}
	inline FOptionalBuildOutput(const FBuildOutput& InOutput) : FBuildOutput(InOutput) {}
	inline FOptionalBuildOutput& operator=(FBuildOutput&& InOutput) { FBuildOutput::operator=(MoveTemp(InOutput)); return *this; }
	inline FOptionalBuildOutput& operator=(const FBuildOutput& InOutput) { FBuildOutput::operator=(InOutput); return *this; }

	/** Returns the build output. The caller must check for null before using this accessor. */
	inline const FBuildOutput& Get() const & { return *this; }
	inline FBuildOutput&& Get() && { return MoveTemp(*this); }

	inline bool IsNull() const { return !IsValid(); }
	inline bool IsValid() const { return Output.IsValid(); }
	inline explicit operator bool() const { return IsValid(); }

	inline void Reset() { *this = FOptionalBuildOutput(); }
};

} // UE::DerivedData

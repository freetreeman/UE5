// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComputeDataInterface.generated.h"

struct FComputeKernelPermutationSet;
struct FShaderCompilerEnvironment;
struct FShaderFunctionDefinition;
class FShaderParametersMetadataBuilder;

/** 
 * Compute Data Interface required to compile a Compute Graph. 
 * Compute Kernels require Data Interfaces to fulfill their external functions.
 * Compute Data Interfaces define how Compute Data Providers will actually marshal data in and out of Kernels.
 */
UCLASS(Abstract, Const)
class ENGINE_API UComputeDataInterface : public UObject
{
	GENERATED_BODY()

public:
	/** Gather permutations from the data interface. Any connected kernel will include these in its total compiled permutations. */
	virtual void GetPermutations(FComputeKernelPermutationSet& OutPermutationSet) const {}
	/** Get the data interface functions available to fulfill external inputs of a kernel. */
	virtual void GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const {}
	/** Get the data interface functions available to fulfill external outputs of a kernel. */
	virtual void GetSupportedOutputs(TArray<FShaderFunctionDefinition>& OutFunctions) const {}
	/** Gather the shader metadata exposed by the data provider payload. */
	virtual void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const {}
	/** Gather the shader code for this data provider. */
	virtual void GetHLSL(FString& OutHLSL) const {}
	/** Gather modifications to the compilation environment always required when including this data provider. */
	virtual void ModifyCompilationEnvironment(FShaderCompilerEnvironment& OutEnvironment) const {}

	virtual UClass* GetDataProviderClass() const PURE_VIRTUAL(UComputeDataInterface::GetDataProviderClass, return nullptr; )
};

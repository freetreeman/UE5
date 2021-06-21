// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialLayersFunctions.h"
#include "Misc/Guid.h"
#include "UObject/ObjectMacros.h"
#include "SceneTypes.h"
#include "MaterialCachedData.generated.h"

class UTexture;
class UCurveLinearColor;
class UCurveLinearColorAtlas;
class UFont;
class UMaterialExpression;
class URuntimeVirtualTexture;
class ULandscapeGrassType;
class UMaterialFunctionInterface;
class UMaterialInterface;

/** Stores information about a function that this material references, used to know when the material needs to be recompiled. */
USTRUCT()
struct FMaterialFunctionInfo
{
	GENERATED_USTRUCT_BODY()

	/** Id that the function had when this material was last compiled. */
	UPROPERTY()
	FGuid StateId;

	/** The function which this material has a dependency on. */
	UPROPERTY()
	TObjectPtr<UMaterialFunctionInterface> Function = nullptr;
};

/** Stores information about a parameter collection that this material references, used to know when the material needs to be recompiled. */
USTRUCT()
struct FMaterialParameterCollectionInfo
{
	GENERATED_USTRUCT_BODY()

	/** Id that the collection had when this material was last compiled. */
	UPROPERTY()
	FGuid StateId;

	/** The collection which this material has a dependency on. */
	UPROPERTY()
	TObjectPtr<class UMaterialParameterCollection> ParameterCollection = nullptr;

	bool operator==(const FMaterialParameterCollectionInfo& Other) const
	{
		return StateId == Other.StateId && ParameterCollection == Other.ParameterCollection;
	}
};

USTRUCT()
struct FParameterChannelNames
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = MaterialExpressionVectorParameter)
	FText R;

	UPROPERTY(EditAnywhere, Category = MaterialExpressionVectorParameter)
	FText G;

	UPROPERTY(EditAnywhere, Category = MaterialExpressionVectorParameter)
	FText B;

	UPROPERTY(EditAnywhere, Category = MaterialExpressionVectorParameter)
	FText A;
};

enum class EMaterialParameterType : int32
{
	Scalar,
	Vector,
	Texture,
	Font,
	RuntimeVirtualTexture,

	RuntimeCount, // Runtime parameter types must go above here, and editor-only ones below

#if WITH_EDITORONLY_DATA
	StaticSwitch = RuntimeCount,
	StaticComponentMask,
	// Excluding StaticMaterialLayer due to type specific complications

	Count,
#else
	Count = RuntimeCount,
#endif
};
static const int32 NumMaterialRuntimeParameterTypes = (int32)EMaterialParameterType::RuntimeCount;
#if WITH_EDITORONLY_DATA
static const int32 NumMaterialEditorOnlyParameterTypes = (int32)EMaterialParameterType::Count - (int32)EMaterialParameterType::RuntimeCount;
#endif

USTRUCT()
struct FMaterialCachedParameterEntry
{
	GENERATED_USTRUCT_BODY()

	void Reset();

	// This is used to map FMaterialParameterInfos to indices, which are then used to index various TArrays containing values for each type of parameter
	// (ExpressionGuids and Overrides, along with ScalarValues, VectorValues, etc)
	UPROPERTY()
	TSet<FMaterialParameterInfo> ParameterInfoSet;

	UPROPERTY()
	TArray<FGuid> ExpressionGuids; // editor-only?

};

USTRUCT()
struct FStaticComponentMaskValue
{
	GENERATED_USTRUCT_BODY();

	FStaticComponentMaskValue() : R(false), G(false), B(false), A(false) {}
	FStaticComponentMaskValue(bool InR, bool InG, bool InB, bool InA) : R(InR), G(InG), B(InB), A(InA) {}

	UPROPERTY()
	bool R = false;
	
	UPROPERTY()
	bool G = false;

	UPROPERTY()
	bool B = false;

	UPROPERTY()
	bool A = false;
};

USTRUCT()
struct FMaterialCachedParameters
{
	GENERATED_USTRUCT_BODY()

#if WITH_EDITORONLY_DATA
	inline const FMaterialCachedParameterEntry& GetParameterTypeEntry(EMaterialParameterType Type) const { return Type >= EMaterialParameterType::RuntimeCount ? EditorOnlyEntries[static_cast<int32>(Type) - static_cast<int32>(EMaterialParameterType::RuntimeCount)] : RuntimeEntries[static_cast<int32>(Type)]; }
#else
	inline const FMaterialCachedParameterEntry& GetParameterTypeEntry(EMaterialParameterType Type) const { return RuntimeEntries[static_cast<int32>(Type)]; }
#endif

	inline int32 GetNumParameters(EMaterialParameterType Type) const { return GetParameterTypeEntry(Type).ParameterInfoSet.Num(); }
	int32 FindParameterIndex(EMaterialParameterType Type, const FMemoryImageMaterialParameterInfo& HashedParameterInfo) const;
	const FGuid& GetExpressionGuid(EMaterialParameterType Type, int32 Index) const;
	void GetAllParameterInfoOfType(EMaterialParameterType Type, bool bEmptyOutput, TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const;
	void GetAllGlobalParameterInfoOfType(EMaterialParameterType Type, bool bEmptyOutput, TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const;
	void Reset();

	void AddReferencedObjects(FReferenceCollector& Collector);

	bool GetScalarParameterSliderMinMax(const FMemoryImageMaterialParameterInfo& ParameterInfo, float& OutSliderMin, float& OutSliderMax) const;
	bool IsScalarParameterUsedAsAtlasPosition(const FMemoryImageMaterialParameterInfo& ParameterInfo, bool& OutValue, TSoftObjectPtr<UCurveLinearColor>& OutCurve, TSoftObjectPtr<UCurveLinearColorAtlas>& OutAtlas) const;
	bool IsVectorParameterUsedAsChannelMask(const FMemoryImageMaterialParameterInfo& ParameterInfo, bool& OutValue) const;
	bool GetVectorParameterChannelNames(const FMemoryImageMaterialParameterInfo& ParameterInfo, FParameterChannelNames& OutValue) const;
	bool GetTextureParameterChannelNames(const FMemoryImageMaterialParameterInfo& ParameterInfo, FParameterChannelNames& OutValue) const;

	UPROPERTY()
	FMaterialCachedParameterEntry RuntimeEntries[NumMaterialRuntimeParameterTypes];

	UPROPERTY()
	TArray<float> ScalarValues;

	UPROPERTY()
	TArray<FLinearColor> VectorValues;

	UPROPERTY()
	TArray<TObjectPtr<UTexture>> TextureValues;

	UPROPERTY()
	TArray<TObjectPtr<UFont>> FontValues;

	UPROPERTY()
	TArray<int32> FontPageValues;

	UPROPERTY()
	TArray<TObjectPtr<URuntimeVirtualTexture>> RuntimeVirtualTextureValues;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FMaterialCachedParameterEntry EditorOnlyEntries[NumMaterialEditorOnlyParameterTypes];

	UPROPERTY()
	TArray<bool> StaticSwitchValues;

	UPROPERTY()
	TArray<FStaticComponentMaskValue> StaticComponentMaskValues;

	UPROPERTY()
	TArray<FVector2D> ScalarMinMaxValues;

	UPROPERTY()
	TArray<TObjectPtr<UCurveLinearColor>> ScalarCurveValues;

	UPROPERTY()
	TArray<TObjectPtr<UCurveLinearColorAtlas>> ScalarCurveAtlasValues;

	UPROPERTY()
	TArray<FParameterChannelNames> VectorChannelNameValues;

	UPROPERTY()
	TArray<bool> VectorUsedAsChannelMaskValues;

	UPROPERTY()
	TArray<FParameterChannelNames> TextureChannelNameValues;
#endif // WITH_EDITORONLY_DATA
};

struct FMaterialCachedExpressionContext
{
	FMaterialCachedExpressionContext() : bUpdateFunctionExpressions(true) {}

	bool bUpdateFunctionExpressions;
};

USTRUCT()
struct FMaterialCachedExpressionData
{
	GENERATED_USTRUCT_BODY()
	
	ENGINE_API static const FMaterialCachedExpressionData EmptyData;

	FMaterialCachedExpressionData()
		: bHasRuntimeVirtualTextureOutput(false)
		, bHasSceneColor(false)
		, bHasPerInstanceCustomData(false)
		, bHasPerInstanceRandom(false)
		, bHasVertexInterpolator(false)
	{}

#if WITH_EDITOR
	/** Returns 'false' if update is incomplete, due to missing expression data (stripped from non-editor build) */
	bool UpdateForExpressions(const FMaterialCachedExpressionContext& Context, const TArray<TObjectPtr<UMaterialExpression>>& Expressions, EMaterialParameterAssociation Association, int32 ParameterIndex);
	bool UpdateForFunction(const FMaterialCachedExpressionContext& Context, UMaterialFunctionInterface* Function, EMaterialParameterAssociation Association, int32 ParameterIndex);
	bool UpdateForLayerFunctions(const FMaterialCachedExpressionContext& Context, const FMaterialLayersFunctions& LayerFunctions);
#endif // WITH_EDITOR

	void Reset();

	void AddReferencedObjects(FReferenceCollector& Collector);

	bool IsMaterialAttributePropertyConnected(EMaterialProperty Property) const
	{
		return ((MaterialAttributesPropertyConnectedBitmask >> (uint32)Property) & 0x1) != 0;
	}

	void SetMaterialAttributePropertyConnected(EMaterialProperty Property, bool bIsConnected)
	{
		MaterialAttributesPropertyConnectedBitmask = bIsConnected ? MaterialAttributesPropertyConnectedBitmask | (1 << (uint32)Property) : MaterialAttributesPropertyConnectedBitmask & ~(1 << (uint32)Property);
	}

	UPROPERTY()
	FMaterialCachedParameters Parameters;

	/** Array of all texture referenced by this material */
	UPROPERTY()
	TArray<TObjectPtr<UObject>> ReferencedTextures;

	/** Array of all functions this material depends on. */
	UPROPERTY()
	TArray<FMaterialFunctionInfo> FunctionInfos;

	/** Array of all parameter collections this material depends on. */
	UPROPERTY()
	TArray<FMaterialParameterCollectionInfo> ParameterCollectionInfos;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialFunctionInterface>> DefaultLayers;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialFunctionInterface>> DefaultLayerBlends;

	UPROPERTY()
	TArray<TObjectPtr<ULandscapeGrassType>> GrassTypes;

	UPROPERTY()
	TArray<FName> DynamicParameterNames;

	UPROPERTY()
	TArray<bool> QualityLevelsUsed;

	UPROPERTY()
	uint32 bHasRuntimeVirtualTextureOutput : 1;

	UPROPERTY()
	uint32 bHasSceneColor : 1;

	UPROPERTY()
	uint32 bHasPerInstanceCustomData : 1;

	UPROPERTY()
	uint32 bHasPerInstanceRandom : 1;

	UPROPERTY()
	uint32 bHasVertexInterpolator : 1;

	/** Each bit corresponds to EMaterialProperty connection status. */
	UPROPERTY()
	uint32 MaterialAttributesPropertyConnectedBitmask = 0;
};

USTRUCT()
struct FMaterialInstanceCachedData
{
	GENERATED_USTRUCT_BODY()

	void Initialize(FMaterialCachedExpressionData&& InCachedExpressionData);
	void AddReferencedObjects(FReferenceCollector& Collector);

	UPROPERTY()
	FMaterialCachedParameters Parameters;

	UPROPERTY()
	TArray<TObjectPtr<UObject>> ReferencedTextures;
};

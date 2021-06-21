// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/PimplPtr.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Image/ImageDimensions.h"
#include "Image/ImageBuilder.h"
#include "Sampling/MeshMapBaker.h"
#include "ModelingOperators.h"
#include "MeshOpPreviewHelpers.h"
#include "BakeMeshAttributeMapsTool.generated.h"


// predeclarations
struct FMeshDescription;
class UDynamicMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;
PREDECLARE_GEOMETRY(template<typename RealType> class TMeshTangents);
PREDECLARE_GEOMETRY(class FMeshImageBakingCache);
using UE::Geometry::FImageDimensions;
class IPrimitiveComponentBackedTarget;
class IMeshDescriptionProvider;
class IMaterialProvider;
class FBakeNormalMapOp;
class FBakeOcclusionMapOp;
class FBakeCurvatureMapOp;
class FBakeMeshPropertyMapOp;
class FBakeTexture2DImageMapOp;

/**
 * Tool Builder
 */
UCLASS()
class MESHMODELINGTOOLS_API UBakeMeshAttributeMapsToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

protected:
	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;
};



UENUM(meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true"))
enum class EBakeMapType
{
	None                   = 0 UMETA(Hidden),
	TangentSpaceNormalMap  = 1 << 0,
	AmbientOcclusion       = 1 << 1,
	BentNormal             = 1 << 2,
	Curvature              = 1 << 3,
	Texture2DImage         = 1 << 4,
	NormalImage            = 1 << 5,
	FaceNormalImage        = 1 << 6,
	PositionImage          = 1 << 7,
	MaterialID             = 1 << 8,
	MultiTexture           = 1 << 9,
	Occlusion              = (AmbientOcclusion | BentNormal) UMETA(Hidden),
	All                    = 0x3FF UMETA(Hidden)
};
ENUM_CLASS_FLAGS(EBakeMapType);


UENUM()
enum class EBakeTextureResolution 
{
	Resolution16 = 16 UMETA(DisplayName = "16 x 16"),
	Resolution32 = 32 UMETA(DisplayName = "32 x 32"),
	Resolution64 = 64 UMETA(DisplayName = "64 x 64"),
	Resolution128 = 128 UMETA(DisplayName = "128 x 128"),
	Resolution256 = 256 UMETA(DisplayName = "256 x 256"),
	Resolution512 = 512 UMETA(DisplayName = "512 x 512"),
	Resolution1024 = 1024 UMETA(DisplayName = "1024 x 1024"),
	Resolution2048 = 2048 UMETA(DisplayName = "2048 x 2048"),
	Resolution4096 = 4096 UMETA(DisplayName = "4096 x 4096"),
	Resolution8192 = 8192 UMETA(DisplayName = "8192 x 8192")
};


UENUM()
enum class EBakeMultisampling
{
	None = 1 UMETA(DisplayName = "None"),
	Sample2x2 = 2 UMETA(DisplayName = "2 x 2"),
	Sample4x4 = 4 UMETA(DisplayName = "4 x 4"),
	Sample8x8 = 8 UMETA(DisplayName = "8 x 8"),
	Sample16x16 = 16 UMETA(DisplayName = "16 x 16")
};


UCLASS()
class MESHMODELINGTOOLS_API UBakeMeshAttributeMapsToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** The map types to generate */
	UPROPERTY(EditAnywhere, Category = MapSettings, meta=(Bitmask, BitmaskEnum=EBakeMapType))
	int32 MapTypes = (int32) EBakeMapType::TangentSpaceNormalMap;

	/** The map type index to preview */
	UPROPERTY(EditAnywhere, Category = MapSettings, meta=(ArrayClamp="Result"))
	int MapPreview = 0;

	/** The pixel resolution of the generated map */
	UPROPERTY(EditAnywhere, Category = MapSettings, meta = (TransientToolProperty))
	EBakeTextureResolution Resolution = EBakeTextureResolution::Resolution256;

	/** The multisampling configuration per texel */
	UPROPERTY(EditAnywhere, Category = MapSettings)
	EBakeMultisampling Multisampling = EBakeMultisampling::None;

	UPROPERTY(EditAnywhere, Category = MapSettings)
	bool bUseWorldSpace = false;

	/** Distance to search for the correspondence between the source and target meshes */
	UPROPERTY(EditAnywhere, Category = MapSettings, meta = (ClampMin = "0.001"))
	float Thickness = 3.0;

	/** Which UV layer to use to create the map */
	UPROPERTY(EditAnywhere, Category = MapSettings, meta = (GetOptions = GetUVLayerNamesFunc))
	FString UVLayer;

	UFUNCTION()
	TArray<FString> GetUVLayerNamesFunc();
	UPROPERTY(meta = (TransientToolProperty))
	TArray<FString> UVLayerNamesList;

	UPROPERTY(VisibleAnywhere, Category = MapSettings, meta = (TransientToolProperty))
	TArray<UTexture2D*> Result;

};


UENUM()
enum class ENormalMapSpace
{
	/** Tangent space */
	Tangent UMETA(DisplayName = "Tangent space"),
	/** Object space */
	Object UMETA(DisplayName = "Object space")
};


UCLASS()
class MESHMODELINGTOOLS_API UBakedNormalMapToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

};


UENUM()
enum class EOcclusionMapDistribution
{
	/** Uniform occlusion rays */
	Uniform UMETA(DisplayName = "Uniform"),
	/** Cosine weighted occlusion rays */
	Cosine UMETA(DisplayName = "Cosine")
};


UCLASS()
class MESHMODELINGTOOLS_API UBakedOcclusionMapToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** Number of occlusion rays */
	UPROPERTY(EditAnywhere, Category = OcclusionMap, meta = (UIMin = "1", UIMax = "1024", ClampMin = "0", ClampMax = "50000"))
	int32 OcclusionRays = 16;

	/** Maximum occlusion distance (0 = infinity) */
	UPROPERTY(EditAnywhere, Category = OcclusionMap, meta = (UIMin = "0.0", UIMax = "1000.0", ClampMin = "0.0", ClampMax = "99999999.0"))
	float MaxDistance = 0;

	/** Maximum spread angle of occlusion rays. */
	UPROPERTY(EditAnywhere, Category = OcclusionMap, meta = (UIMin = "0", UIMax = "180.0", ClampMin = "0", ClampMax = "180.0"))
	float SpreadAngle = 180.0;

	/** Angular distribution of occlusion rays in the spread angle. */
	UPROPERTY(EditAnywhere, Category = OcclusionMap)
	EOcclusionMapDistribution Distribution = EOcclusionMapDistribution::Cosine;

	/** Whether or not to apply Gaussian Blur to computed AO Map (recommended) */
	UPROPERTY(EditAnywhere, Category = "OcclusionMap|Ambient Occlusion")
	bool bGaussianBlur = true;

	/** Pixel Radius of Gaussian Blur Kernel */
	UPROPERTY(EditAnywhere, Category = "OcclusionMap|Ambient Occlusion", meta = (UIMin = "0", UIMax = "10.0", ClampMin = "0", ClampMax = "100.0"))
	float BlurRadius = 2.25;

	/** Contribution of AO rays that are within this angle (degrees) from horizontal are attenuated. This reduces faceting artifacts. */
	UPROPERTY(EditAnywhere, Category = "OcclusionMap|Ambient Occlusion", meta = (UIMin = "0", UIMax = "45.0", ClampMin = "0", ClampMax = "89.9"))
	float BiasAngle = 15.0;

	/** Coordinate space of the bent normal map. */
	UPROPERTY(EditAnywhere, Category = "OcclusionMap|Bent Normal")
	ENormalMapSpace NormalSpace = ENormalMapSpace::Tangent;
};



UCLASS()
class MESHMODELINGTOOLS_API UBakedOcclusionMapVisualizationProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = Visualization, meta = (UIMin = "0.0", UIMax = "1.0"))
	float BaseGrayLevel = 1.0;

	/** AO Multiplier in visualization (does not affect output) */
	UPROPERTY(EditAnywhere, Category = Visualization, meta = (UIMin = "0.0", UIMax = "1.0"))
	float OcclusionMultiplier = 1.0;
};



UENUM()
enum class EBakedCurvatureTypeMode
{
	/** Mean Curvature is the average of the Max and Min Principal curvatures */
	MeanAverage,
	/** Max Principal Curvature */
	Max,
	/** Min Principal Curvature */
	Min,
	/** Gaussian Curvature is the product of the Max and Min Principal curvatures */
	Gaussian
};

UENUM()
enum class EBakedCurvatureColorMode
{
	/** Map curvature values to grayscale such that black is negative, grey is zero, and white is positive */
	Grayscale,
	/** Map curvature values to red/blue scale such that red is negative, black is zero, and blue is positive */
	RedBlue,
	/** Map curvature values to red/green/blue scale such that red is negative, green is zero, and blue is positive */
	RedGreenBlue
};

UENUM()
enum class EBakedCurvatureClampMode
{
	/** Include both negative and positive curvatures */
	None,
	/** Clamp negative curvatures to zero */
	Positive,
	/** Clamp positive curvatures to zero */
	Negative
};




UCLASS()
class MESHMODELINGTOOLS_API UBakedCurvatureMapToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** Type of curvature to compute */
	UPROPERTY(EditAnywhere, Category = CurvatureMap)
	EBakedCurvatureTypeMode CurvatureType = EBakedCurvatureTypeMode::MeanAverage;

	/** Color mapping calculated from curvature values */
	UPROPERTY(EditAnywhere, Category = CurvatureMap)
	EBakedCurvatureColorMode ColorMode = EBakedCurvatureColorMode::Grayscale;

	/** Scale the maximum curvature value used to compute the mapping to grayscale/color */
	UPROPERTY(EditAnywhere, Category = CurvatureMap, meta = (UIMin = "0.1", UIMax = "2.0", ClampMin = "0.001", ClampMax = "100.0"))
	float RangeMultiplier = 1.0;

	/** Scale the minimum curvature value used to compute the mapping to grayscale/color (fraction of maximum) */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = CurvatureMap, meta = (UIMin = "0.0", UIMax = "1.0"))
	float MinRangeMultiplier = 0.0;

	/** Clamping to apply to curvature values before scaling to color range */
	UPROPERTY(EditAnywhere, Category = CurvatureMap)
	EBakedCurvatureClampMode Clamping = EBakedCurvatureClampMode::None;

	/** Whether or not to apply Gaussian Blur to computed Map */
	UPROPERTY(EditAnywhere, Category = CurvatureMap)
	bool bGaussianBlur = false;

	/** Pixel Radius of Gaussian Blur Kernel */
	UPROPERTY(EditAnywhere, Category = CurvatureMap, meta = (UIMin = "0", UIMax = "10.0", ClampMin = "0", ClampMax = "100.0"))
	float BlurRadius = 2.25;
};



UCLASS()
class MESHMODELINGTOOLS_API UBakedTexture2DImageProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** The source texture that is to be resampled into a new texture map */
	UPROPERTY(EditAnywhere, Category = Texture2D, meta = (TransientToolProperty))
	UTexture2D* SourceTexture;

	/** The UV layer on the source mesh that corresponds to the SourceTexture */
	UPROPERTY(EditAnywhere, Category = Texture2D)
	int32 UVLayer = 0;
};


UCLASS()
class MESHMODELINGTOOLS_API UBakedMultiTexture2DImageProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	/** For each material ID, the source texture that will be resampled in that material's region*/
	UPROPERTY(EditAnywhere, Category = MultiTexture, meta = (DisplayName = "Material IDs / Source Textures"))
	TMap<int32, UTexture2D*> MaterialIDSourceTextureMap;

	/** UV layer to sample from on the input mesh */
	UPROPERTY(EditAnywhere, Category = MultiTexture)
	int32 UVLayer = 0;

	/** The set of all source textures from all input materials */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, AdvancedDisplay, Category = MultiTexture, meta = (DisplayName = "Source Textures"))
	TArray<UTexture2D*> AllSourceTextures;

};


enum class EBakeOpState
{
	Complete = 0,		// Inputs valid & Result is valid - no-op.
	Evaluate = 1 << 0,	// Inputs valid & Result is invalid - re-evaluate.
	Invalid	 = 1 << 1	// Inputs invalid - pause eval.
};
ENUM_CLASS_FLAGS(EBakeOpState);

/**
 * Detail Map Baking Tool
 */
UCLASS()
class MESHMODELINGTOOLS_API UBakeMeshAttributeMapsTool : public UMultiSelectionTool, public UE::Geometry::IGenericDataOperatorFactory<UE::Geometry::FMeshMapBaker>
{
	GENERATED_BODY()

public:
	UBakeMeshAttributeMapsTool() = default;

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;

	// IGenericDataOperatorFactory API
	virtual TUniquePtr<UE::Geometry::TGenericDataOperator<UE::Geometry::FMeshMapBaker>> MakeNewOperator() override;

protected:
	// need to update bResultValid if these are modified, so we don't publicly expose them. 
	// @todo setters/getters for these

	UPROPERTY()
	UBakeMeshAttributeMapsToolProperties* Settings;

	UPROPERTY()
	UBakedNormalMapToolProperties* NormalMapProps;

	UPROPERTY()
	UBakedOcclusionMapToolProperties* OcclusionMapProps;

	UPROPERTY()
	UBakedCurvatureMapToolProperties* CurvatureMapProps;

	UPROPERTY()
	UBakedTexture2DImageProperties* Texture2DProps;

	UPROPERTY()
	UBakedMultiTexture2DImageProperties* MultiTextureProps;

	UPROPERTY()
	UBakedOcclusionMapVisualizationProperties* VisualizationProps;


protected:
	friend class FBakeMapBaseOp;
	friend class FBakeNormalMapOp;
	friend class FBakeOcclusionMapOp;
	friend class FBakeCurvatureMapOp;
	friend class FBakeTexture2DImageMapOp;
	friend class FBakeMeshPropertyMapOp;
	friend class FBakeMultiTextureOp;
	friend class FMeshMapBakerOp;

	UDynamicMeshComponent* DynamicMeshComponent;

	UPROPERTY()
	UMaterialInstanceDynamic* PreviewMaterial;

    UPROPERTY()
	UMaterialInstanceDynamic* BentNormalPreviewMaterial;

	UPROPERTY()
	UMaterialInstanceDynamic* WorkingPreviewMaterial;
	float SecondsBeforeWorkingMaterial = 0.75;

	TSharedPtr<UE::Geometry::TMeshTangents<double>, ESPMode::ThreadSafe> BaseMeshTangents;
	UE::Geometry::FDynamicMesh3 BaseMesh;
	UE::Geometry::FDynamicMeshAABBTree3 BaseSpatial;

	bool bIsBakeToSelf = false;

	TSharedPtr<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe> DetailMesh;
	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3, ESPMode::ThreadSafe> DetailSpatial;
	int32 DetailMeshTimestamp = 0;
	void UpdateDetailMesh();
	bool bDetailMeshValid = false;

	bool bInputsDirty = false;
	void UpdateResult();

	void UpdateOnModeChange();
	void UpdateVisualization();

	TUniquePtr<TGenericDataBackgroundCompute<UE::Geometry::FMeshMapBaker>> Compute = nullptr;
	void OnMapsUpdated(const TUniquePtr<UE::Geometry::FMeshMapBaker>& NewResult);

	/** @return A single bitfield of map types from an array of map types. */
	EBakeMapType GetMapTypes(const int32& MapTypes) const;
	TArray<EBakeMapType> GetMapTypesArray(const int32& MapTypes) const;

	struct FBakeCacheSettings
	{
		EBakeMapType BakeMapTypes = EBakeMapType::None;
		FImageDimensions Dimensions;
		int32 UVLayer;
		int32 DetailTimestamp;
		float Thickness;
		int32 Multisampling;

		bool operator==(const FBakeCacheSettings& Other) const
		{
			return BakeMapTypes == Other.BakeMapTypes && Dimensions == Other.Dimensions &&
				UVLayer == Other.UVLayer && DetailTimestamp == Other.DetailTimestamp &&
				Thickness == Other.Thickness && Multisampling == Other.Multisampling;
		}
	};
	FBakeCacheSettings CachedBakeCacheSettings;
	TArray<EBakeMapType> ResultTypes;

	EBakeOpState OpState = EBakeOpState::Evaluate;

	UPROPERTY()
	TArray<UTexture2D*> CachedMaps;
	using CachedMapIndex = TMap<EBakeMapType, int32>;
	CachedMapIndex CachedMapIndices;


	struct FNormalMapSettings
	{
		FImageDimensions Dimensions;

		bool operator==(const FNormalMapSettings& Other) const
		{
			return Dimensions == Other.Dimensions;
		}
	};
	FNormalMapSettings CachedNormalMapSettings;
	EBakeOpState UpdateResult_Normal();


	struct FOcclusionMapSettings
	{
		FImageDimensions Dimensions;
		int32 OcclusionRays;
		float MaxDistance;
		float SpreadAngle;
		EOcclusionMapDistribution Distribution;
		float BlurRadius;
		float BiasAngle;
		ENormalMapSpace NormalSpace;

		bool operator==(const FOcclusionMapSettings& Other) const
		{
			return Dimensions == Other.Dimensions &&
				OcclusionRays == Other.OcclusionRays &&
				MaxDistance == Other.MaxDistance &&
				SpreadAngle == Other.SpreadAngle &&
				Distribution == Other.Distribution &&
				BlurRadius == Other.BlurRadius &&
				BiasAngle == Other.BiasAngle &&
				NormalSpace == Other.NormalSpace;
		}
	};
	FOcclusionMapSettings CachedOcclusionMapSettings;
	EBakeOpState UpdateResult_Occlusion();


	struct FCurvatureMapSettings
	{
		FImageDimensions Dimensions;
		int32 RayCount = 1;
		int32 CurvatureType = 0;
		float RangeMultiplier = 1.0;
		float MinRangeMultiplier = 0.0;
		int32 ColorMode = 0;
		int32 ClampMode = 0;
		float MaxDistance = 1.0;
		float BlurRadius = 1.0;

		bool operator==(const FCurvatureMapSettings& Other) const
		{
			return Dimensions == Other.Dimensions && RayCount == Other.RayCount && CurvatureType == Other.CurvatureType && RangeMultiplier == Other.RangeMultiplier && MinRangeMultiplier == Other.MinRangeMultiplier && ColorMode == Other.ColorMode && ClampMode == Other.ClampMode && MaxDistance == Other.MaxDistance && BlurRadius == Other.BlurRadius;
		}
	};
	FCurvatureMapSettings CachedCurvatureMapSettings;
	EBakeOpState UpdateResult_Curvature();


	struct FMeshPropertyMapSettings
	{
		FImageDimensions Dimensions;

		bool operator==(const FMeshPropertyMapSettings& Other) const
		{
			return Dimensions == Other.Dimensions;
		}
	};
	FMeshPropertyMapSettings CachedMeshPropertyMapSettings;
	EBakeOpState UpdateResult_MeshProperty();


	struct FTexture2DImageSettings
	{
		FImageDimensions Dimensions;
		int32 UVLayer = 0;

		bool operator==(const FTexture2DImageSettings& Other) const
		{
			return Dimensions == Other.Dimensions && UVLayer == Other.UVLayer;
		}
	};
	TSharedPtr<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe> CachedTextureImage;
	FTexture2DImageSettings CachedTexture2DImageSettings;
	EBakeOpState UpdateResult_Texture2DImage();


	TMap<int32, TSharedPtr<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe>> CachedMultiTextures;
	EBakeOpState UpdateResult_MultiTexture();


	// empty maps are shown when nothing is computed
	UPROPERTY()
	UTexture2D* EmptyNormalMap;

	UPROPERTY()
	UTexture2D* EmptyColorMapBlack;

	UPROPERTY()
	UTexture2D* EmptyColorMapWhite;

	void InitializeEmptyMaps();

	void GetTexturesFromDetailMesh(const UPrimitiveComponent* DetailComponent);

};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseTools/SingleSelectionMeshEditingTool.h"
#include "PreviewMesh.h"
#include "ModelingOperators.h"
#include "MeshOpPreviewHelpers.h"
#include "ConvertToPolygonsTool.generated.h"

// predeclaration
class UConvertToPolygonsTool;
class FConvertToPolygonsOp;
class UPreviewGeometry;
PREDECLARE_GEOMETRY(class FDynamicMesh3);

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UConvertToPolygonsToolBuilder : public USingleSelectionMeshEditingToolBuilder
{
	GENERATED_BODY()
public:
	virtual USingleSelectionMeshEditingTool* CreateNewTool(const FToolBuilderState & SceneState) const override;
};




UENUM()
enum class EConvertToPolygonsMode
{
	/** Convert based on Angle Tolerance between Face Normals */
	FaceNormalDeviation UMETA(DisplayName = "Face Normal Deviation"),
	/** Create PolyGroups based on UV Islands */
	FromUVIslands  UMETA(DisplayName = "From UV Islands"),
	/** Create Polygroups based on Connected Triangles */
	FromConnectedTris UMETA(DisplayName = "From Connected Tris")
};



UCLASS()
class MESHMODELINGTOOLS_API UConvertToPolygonsToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Strategy to use to group triangles */
	UPROPERTY(EditAnywhere, Category = PolyGroups)
	EConvertToPolygonsMode ConversionMode = EConvertToPolygonsMode::FaceNormalDeviation;

	/** Tolerance for planarity */
	UPROPERTY(EditAnywhere, Category = PolyGroups, meta = (UIMin = "0.001", UIMax = "20.0", ClampMin = "0.0", ClampMax = "90.0", EditCondition = "ConversionMode == EConvertToPolygonsMode::FaceNormalDeviation", EditConditionHides))
	float AngleTolerance = 0.1f;

	/** If true, normals are recomputed per-group, with hard edges at group boundaries */
	UPROPERTY(EditAnywhere, Category = PolyGroups, meta = (EditCondition = "ConversionMode == EConvertToPolygonsMode::FaceNormalDeviation", EditConditionHides))
	bool bCalculateNormals = true;
	
	/** Display each group with a different auto-generated color */
	UPROPERTY(EditAnywhere, Category = Display)
	bool bShowGroupColors = true;
};

UCLASS()
class MESHMODELINGTOOLS_API UConvertToPolygonsOperatorFactory : public UObject, public UE::Geometry::IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<UE::Geometry::FDynamicMeshOperator> MakeNewOperator() override;

	UPROPERTY()
	UConvertToPolygonsTool* ConvertToPolygonsTool;  // back pointer
};

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UConvertToPolygonsTool : public USingleSelectionMeshEditingTool
{
	GENERATED_BODY()

public:
	UConvertToPolygonsTool();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;

	// update parameters in ConvertToPolygonsOp based on current Settings
	void UpdateOpParameters(FConvertToPolygonsOp& ConvertToPolygonsOp) const;

protected:
	
	void OnSettingsModified();

protected:
	UPROPERTY()
	UConvertToPolygonsToolProperties* Settings;

	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* PreviewCompute = nullptr;

	UPROPERTY()
	UPreviewGeometry* PreviewGeometry = nullptr;

protected:
	TSharedPtr<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe> OriginalDynamicMesh;

	// for visualization
	TArray<int> PolygonEdges;
	
	void UpdateVisualization();
};

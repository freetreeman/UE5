// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "InteractiveToolBuilder.h"
#include "Drawing/LineSetComponent.h"
#include "BaseTools/BaseCreateFromSelectedTool.h"

#include "CompositionOps/BooleanMeshesOp.h"

#include "CSGMeshesTool.generated.h"

// predeclarations
PREDECLARE_USE_GEOMETRY_CLASS(FDynamicMesh3);


/**
 * Standard properties of the CSG operation
 */
UCLASS()
class MESHMODELINGTOOLS_API UCSGMeshesToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** The type of operation */
	UPROPERTY(EditAnywhere, Category = Operation)
	ECSGOperation Operation = ECSGOperation::DifferenceAB;

	/** Show boundary edges created by the CSG operation -- often due to numerical error */
	UPROPERTY(EditAnywhere, Category = Operation)
	bool bShowNewBoundaryEdges = true;

	/** Automatically attempt to fill any holes left by CSG (e.g. due to numerical errors) */
	UPROPERTY(EditAnywhere, Category = Operation)
	bool bAttemptFixHoles = false;

	/** Try to collapse extra edges created by the Boolean operation */
	UPROPERTY(EditAnywhere, Category = Operation)
	bool bCollapseExtraEdges = true;

	/** Whether to show a translucent version of the subtracted mesh, to help visualize what is being removed */
	UPROPERTY(EditAnywhere, Category = Display, meta = (EditCondition = "Operation == ECSGOperation::DifferenceAB || Operation == ECSGOperation::DifferenceBA", EditConditionHides))
	bool bShowSubtractedMesh = true;
	
	/** Opacity of translucent version of the selected mesh */
	UPROPERTY(EditAnywhere, Category = Display, meta = (ClampMin = "0", ClampMax = "1", 
		EditCondition = "bShowSubtractedMesh && Operation == ECSGOperation::DifferenceAB || bShowSubtractedMesh && Operation == ECSGOperation::DifferenceBA", EditConditionHides))
	float OpacityOfSubtractedMesh = .2;
	
	/** Color of translucent version of the subtracted mesh */
	UPROPERTY(EditAnywhere, Category = Display, meta = (
		EditCondition = "bShowSubtractedMesh && Operation == ECSGOperation::DifferenceAB || bShowSubtractedMesh && Operation == ECSGOperation::DifferenceBA", EditConditionHides), AdvancedDisplay)
	FLinearColor ColorOfSubtractedMesh = FLinearColor::Black;

	/** If true, only the first mesh will keep its materials assignments; all other triangles will be assigned material 0 */
	UPROPERTY(EditAnywhere, Category = Materials)
	bool bOnlyUseFirstMeshMaterials = false;
};


/**
 * Properties of the trim mode
 */
UCLASS()
class MESHMODELINGTOOLS_API UTrimMeshesToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** Which object to trim */
	UPROPERTY(EditAnywhere, Category = Operation)
	ETrimOperation WhichMesh = ETrimOperation::TrimA;

	/** Whether to remove the surface inside or outside of the trimming geometry */
	UPROPERTY(EditAnywhere, Category = Operation)
	ETrimSide TrimSide = ETrimSide::RemoveInside;

	/** Whether to show a translucent version of the trimming mesh, to help visualize what is being cut */
	UPROPERTY(EditAnywhere, Category = Display)
	bool bShowTrimmingMesh = true;

	/** Opacity of translucent version of the trimming mesh */
	UPROPERTY(EditAnywhere, Category = Display, meta = (EditCondition = "bShowTrimmingMesh", ClampMin = "0", ClampMax = "1"))
	float OpacityOfTrimmingMesh = .2;

	/** Color of translucent version of the trimming mesh */
	UPROPERTY(EditAnywhere, Category = Display, meta = (EditCondition = "bShowTrimmingMesh"), AdvancedDisplay)
	FLinearColor ColorOfTrimmingMesh = FLinearColor::Black;

};



/**
 * Simple Mesh Plane Cutting Tool
 */
UCLASS()
class MESHMODELINGTOOLS_API UCSGMeshesTool : public UBaseCreateFromSelectedTool
{
	GENERATED_BODY()

public:

	UCSGMeshesTool() {}

	void EnableTrimMode();

protected:

	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

	virtual void ConvertInputsAndSetPreviewMaterials(bool bSetPreviewMesh = true) override;

	virtual void SetupProperties() override;
	virtual void SaveProperties() override;
	virtual void SetPreviewCallbacks() override;

	virtual FString GetCreatedAssetName() const;
	virtual FText GetActionName() const;

	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<UE::Geometry::FDynamicMeshOperator> MakeNewOperator() override;

protected:

	void UpdateVisualization();

	UPROPERTY()
	UCSGMeshesToolProperties* CSGProperties;

	UPROPERTY()
	UTrimMeshesToolProperties* TrimProperties;

	TArray<TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe>> OriginalDynamicMeshes;

	UPROPERTY()
	TArray<UPreviewMesh*> OriginalMeshPreviews;

	// Material used to show the otherwise-invisible cutting/trimming mesh
	UPROPERTY()
	UMaterialInstanceDynamic* PreviewsGhostMaterial;

	UPROPERTY()
	ULineSetComponent* DrawnLineSet;

	// for visualization of any errors in the currently-previewed CSG operation
	TArray<int> CreatedBoundaryEdges;

	bool bTrimMode = false;

	virtual int32 GetHiddenGizmoIndex() const;

	// Update visibility of ghostly preview meshes (used to show trimming or subtracting surface)
	void UpdatePreviewsVisibility();

	// update the material of ghostly preview meshes (used to show trimming or subtracting surface)
	void UpdatePreviewsMaterial();
};




UCLASS()
class MESHMODELINGTOOLS_API UCSGMeshesToolBuilder : public UBaseCreateFromSelectedToolBuilder
{
	GENERATED_BODY()

public:

	bool bTrimMode = false;

	virtual TOptional<int32> MaxComponentsSupported() const override
	{
		return TOptional<int32>(2);
	}

	virtual int32 MinComponentsSupported() const override
	{
		return 2;
	}

	virtual UBaseCreateFromSelectedTool* MakeNewToolInstance(UObject* Outer) const override
	{
		UCSGMeshesTool* Tool = NewObject<UCSGMeshesTool>(Outer);
		if (bTrimMode)
		{
			Tool->EnableTrimMode();
		}
		return Tool;
	}
};




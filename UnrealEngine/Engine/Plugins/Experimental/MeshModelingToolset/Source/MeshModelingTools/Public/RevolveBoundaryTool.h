// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "InteractiveToolBuilder.h"
#include "SingleSelectionTool.h"
#include "Mechanics/ConstructionPlaneMechanic.h"
#include "MeshBoundaryToolBase.h"
#include "MeshOpPreviewHelpers.h" //UMeshOpPreviewWithBackgroundCompute
#include "ModelingOperators.h" //IDynamicMeshOperatorFactory
#include "InteractiveTool.h"
#include "InteractiveToolBuilder.h" //UInteractiveToolBuilder
#include "Properties/MeshMaterialProperties.h"
#include "Properties/RevolveProperties.h"
#include "ToolContextInterfaces.h" // FToolBuilderState

#include "RevolveBoundaryTool.generated.h"

// Tool Builder

UCLASS()
class MESHMODELINGTOOLS_API URevolveBoundaryToolBuilder : public USingleSelectionMeshEditingToolBuilder
{
	GENERATED_BODY()

public:
	virtual USingleSelectionMeshEditingTool* CreateNewTool(const FToolBuilderState& SceneState) const override;
};

UCLASS()
class MESHMODELINGTOOLS_API URevolveBoundaryOperatorFactory : public UObject, public UE::Geometry::IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:

	TUniquePtr<UE::Geometry::FDynamicMeshOperator> MakeNewOperator() override;

	UPROPERTY()
	URevolveBoundaryTool* RevolveBoundaryTool;
};

UCLASS()
class MESHMODELINGTOOLS_API URevolveBoundaryToolProperties : public URevolveProperties
{
	GENERATED_BODY()

public:
	
	UPROPERTY(EditAnywhere, Category = RevolveSettings, AdvancedDisplay)
	bool bDisplayOriginalMesh = false;

	UPROPERTY(EditAnywhere, Category = RevolutionAxis)
	FVector AxisOrigin = FVector(0, 0, 0);

	//~ We don't use a rotator for axis orientation because one of the components (roll) 
	//~ will never do anything in the case of our axis.
	UPROPERTY(EditAnywhere, Category = RevolutionAxis, meta = (
		UIMin = -180, UIMax = 180, ClampMin = -180000, ClampMax = 18000))
	float AxisYaw = 0;

	UPROPERTY(EditAnywhere, Category = RevolutionAxis, meta = (
		UIMin = -180, UIMax = 180, ClampMin = -180000, ClampMax = 18000))
	float AxisPitch = 0;

	/** Determines whether the axis control widget snaps to world grid (only relevant if world coordinate mode is active in viewport) .*/
	UPROPERTY(EditAnywhere, Category = RevolutionAxis)
	bool bSnapToWorldGrid = false;
};

/** 
 * Tool that revolves the boundary of a mesh around an axis to create a new mesh. Mainly useful for
 * revolving planar meshes. 
 */
UCLASS()
class MESHMODELINGTOOLS_API URevolveBoundaryTool : public UMeshBoundaryToolBase, public IClickBehaviorTarget
{
	GENERATED_BODY()

public:

	virtual bool CanAccept() const override;

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void OnTick(float DeltaTime) override;
	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

	virtual void OnUpdateModifierState(int ModifierID, bool bIsOn) override;
protected:

	// Support for Ctrl+Clicking a boundary to align the revolution axis to that segment
	bool bAlignAxisOnClick = false;
	int32 AlignAxisModifier = 2;

	UPROPERTY()
	URevolveBoundaryToolProperties* Settings = nullptr;

	UPROPERTY()
	UNewMeshMaterialProperties* MaterialProperties;

	UPROPERTY()
	UConstructionPlaneMechanic* PlaneMechanic = nullptr;

	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* Preview = nullptr;

	FVector3d RevolutionAxisOrigin;
	FVector3d RevolutionAxisDirection;

	void GenerateAsset(const FDynamicMeshOpResult& Result);
	void UpdateRevolutionAxis();
	void StartPreview();

	// IClickBehaviorTarget API
	virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) override;
	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;

	friend class URevolveBoundaryOperatorFactory;
};
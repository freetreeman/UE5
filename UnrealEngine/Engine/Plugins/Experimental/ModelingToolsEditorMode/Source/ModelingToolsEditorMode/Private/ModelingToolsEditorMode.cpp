// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModelingToolsEditorMode.h"
#include "InteractiveTool.h"
#include "InteractiveToolsSelectionStoreSubsystem.h"
#include "ModelingToolsEditorModeToolkit.h"
#include "Toolkits/ToolkitManager.h"
#include "ToolTargetManager.h"
#include "ContextObjectStore.h"
#include "ToolTargets/StaticMeshComponentToolTarget.h"
#include "ToolTargets/VolumeDynamicMeshToolTarget.h"
#include "ToolTargets/VolumeMeshDescriptionToolTarget.h"
//#include "ToolTargets/DynamicMeshComponentToolTarget.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EditorViewportClient.h"
#include "EngineAnalytics.h"
#include "BaseGIzmos/TransformGizmoUtil.h"

#include "Features/IModularFeatures.h"
#include "ModelingModeToolExtensions.h"

//#include "SingleClickTool.h"
//#include "MeshSurfacePointTool.h"
//#include "MeshVertexDragTool.h"
#include "DynamicMeshSculptTool.h"
#include "MeshVertexSculptTool.h"
#include "EditMeshPolygonsTool.h"
#include "DeformMeshPolygonsTool.h"
#include "SubdividePolyTool.h"
#include "GroupEdgeInsertionTool.h"
#include "EdgeLoopInsertionTool.h"
#include "ConvertToPolygonsTool.h"
#include "AddPrimitiveTool.h"
#include "AddPatchTool.h"
#include "RevolveBoundaryTool.h"
#include "SmoothMeshTool.h"
#include "OffsetMeshTool.h"
#include "RemeshMeshTool.h"
#include "SimplifyMeshTool.h"
#include "MeshInspectorTool.h"
#include "WeldMeshEdgesTool.h"
#include "DrawPolygonTool.h"
#include "DrawPolyPathTool.h"
#include "DrawAndRevolveTool.h"
#include "ShapeSprayTool.h"
#include "MergeMeshesTool.h"
#include "VoxelCSGMeshesTool.h"
#include "VoxelSolidifyMeshesTool.h"
#include "VoxelBlendMeshesTool.h"
#include "VoxelMorphologyMeshesTool.h"
#include "PlaneCutTool.h"
#include "MirrorTool.h"
#include "SelfUnionMeshesTool.h"
#include "CSGMeshesTool.h"
#include "CutMeshWithMeshTool.h"
#include "BspConversionTool.h"
#include "MeshToVolumeTool.h"
#include "VolumeToMeshTool.h"
#include "HoleFillTool.h"
#include "PolygonOnMeshTool.h"
#include "DisplaceMeshTool.h"
#include "MeshSpaceDeformerTool.h"
#include "EditNormalsTool.h"
#include "RemoveOccludedTrianglesTool.h"
#include "AttributeEditorTool.h"
#include "TransformMeshesTool.h"
#include "MeshSelectionTool.h"
#include "UVProjectionTool.h"
#include "UVLayoutTool.h"
#include "EditMeshMaterialsTool.h"
#include "EditPivotTool.h"
#include "BakeTransformTool.h"
#include "CombineMeshesTool.h"
#include "AlignObjectsTool.h"
#include "EditUVIslandsTool.h"
#include "BakeMeshAttributeMapsTool.h"
#include "MeshAttributePaintTool.h"
#include "ParameterizeMeshTool.h"
#include "MeshTangentsTool.h"
#include "ProjectToTargetTool.h"
#include "LatticeDeformerTool.h"
#include "SeamSculptTool.h"
#include "MeshGroupPaintTool.h"
#include "TransferMeshTool.h"

#include "Physics/PhysicsInspectorTool.h"
#include "Physics/SetCollisionGeometryTool.h"
#include "Physics/ExtractCollisionGeometryTool.h"
//#include "Physics/EditCollisionGeometryTool.h"

// hair tools
#include "Hair/GroomToMeshTool.h"
#include "Hair/GroomCardsEditorTool.h"
#include "GenerateLODMeshesTool.h"

// asset tools
#include "Tools/GenerateStaticMeshLODAssetTool.h"
#include "Tools/LODManagerTool.h"

#include "EditorModeManager.h"

// stylus support
#include "IStylusInputModule.h"

#include "LevelEditor.h"
#include "IAssetViewport.h"
#include "SLevelViewport.h"

#include "ModelingToolsActions.h"
#include "ModelingToolsManagerActions.h"
#include "ModelingModeAssetUtils.h"
#include "EditorModelingObjectsCreationAPI.h"

#define LOCTEXT_NAMESPACE "UModelingToolsEditorMode"


//#define ENABLE_DEBUG_PRINTING

const FEditorModeID UModelingToolsEditorMode::EM_ModelingToolsEditorModeId = TEXT("EM_ModelingToolsEditorMode");

FDateTime UModelingToolsEditorMode::LastModeStartTimestamp;
FDateTime UModelingToolsEditorMode::LastToolStartTimestamp;

UModelingToolsEditorMode::UModelingToolsEditorMode()
{
	Info = FEditorModeInfo(
		EM_ModelingToolsEditorModeId,
		LOCTEXT("ModelingToolsEditorModeName", "Modeling"),
		FSlateIcon("ModelingToolsStyle", "LevelEditor.ModelingToolsMode", "LevelEditor.ModelingToolsMode.Small"),
		true);
}

UModelingToolsEditorMode::UModelingToolsEditorMode(FVTableHelper& Helper)
{
}

UModelingToolsEditorMode::~UModelingToolsEditorMode()
{
}

bool UModelingToolsEditorMode::ProcessEditDelete()
{
	if (UEdMode::ProcessEditDelete())
	{
		return true;
	}

	// for now we disable deleting in an Accept-style tool because it can result in crashes if we are deleting target object
	if ( GetToolManager()->HasAnyActiveTool() && GetToolManager()->GetActiveTool(EToolSide::Mouse)->HasAccept() )
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("CannotDeleteWarning", "Cannot delete objects while this Tool is active"), EToolMessageLevel::UserWarning);
		return true;
	}

	// If we didn't skip deletion, then we're probably deleting something, so it seems fair to clear
	// the selection.
	if (UInteractiveToolsSelectionStoreSubsystem* ToolSelectionStore = GEngine->GetEngineSubsystem<UInteractiveToolsSelectionStoreSubsystem>())
	{
		ToolSelectionStore->ClearStoredSelection();
	}

	return false;
}


bool UModelingToolsEditorMode::ProcessEditCut()
{
	// for now we disable deleting in an Accept-style tool because it can result in crashes if we are deleting target object
	if (GetToolManager()->HasAnyActiveTool() && GetToolManager()->GetActiveTool(EToolSide::Mouse)->HasAccept())
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("CannotCutWarning", "Cannot cut objects while this Tool is active"), EToolMessageLevel::UserWarning);
		return true;
	}

	// If we're doing a cut, we should clear the tool selection.
	if (UInteractiveToolsSelectionStoreSubsystem* ToolSelectionStore = GEngine->GetEngineSubsystem<UInteractiveToolsSelectionStoreSubsystem>())
	{
		ToolSelectionStore->ClearStoredSelection();
	}

	return false;
}



bool UModelingToolsEditorMode::CanAutoSave() const
{
	// prevent autosave if any tool is active
	return ToolsContext->ToolManager->HasAnyActiveTool() == false;
}

bool UModelingToolsEditorMode::ShouldDrawWidget() const
{ 
	// allow standard xform gizmo if we don't have an active tool
	if (ToolsContext != nullptr && ToolsContext->ToolManager->HasAnyActiveTool())
	{
		return false;
	}

	return UBaseLegacyWidgetEdMode::ShouldDrawWidget(); 
}

void UModelingToolsEditorMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	Super::Tick(ViewportClient, DeltaTime);

	if (Toolkit.IsValid())
	{
		FModelingToolsEditorModeToolkit* ModelingToolkit = (FModelingToolsEditorModeToolkit*)Toolkit.Get();
		ModelingToolkit->EnableShowRealtimeWarning(ViewportClient->IsRealtime() == false);
	}
}

//
// FStylusStateTracker registers itself as a listener for stylus events and implements
// the IToolStylusStateProviderAPI interface, which allows MeshSurfacePointTool implementations
 // to query for the pen pressure.
//
// This is kind of a hack. Unfortunately the current Stylus module is a Plugin so it
// cannot be used in the base ToolsFramework, and we need this in the Mode as a workaround.
//
class FStylusStateTracker : public IStylusMessageHandler, public IToolStylusStateProviderAPI
{
public:
	const IStylusInputDevice* ActiveDevice = nullptr;
	int32 ActiveDeviceIndex = -1;

	bool bPenDown = false;
	float ActivePressure = 1.0;

	FStylusStateTracker()
	{
		UStylusInputSubsystem* StylusSubsystem = GEditor->GetEditorSubsystem<UStylusInputSubsystem>();
		StylusSubsystem->AddMessageHandler(*this);

		ActiveDevice = FindFirstPenDevice(StylusSubsystem, ActiveDeviceIndex);
		bPenDown = false;
	}

	virtual ~FStylusStateTracker()
	{
		UStylusInputSubsystem* StylusSubsystem = GEditor->GetEditorSubsystem<UStylusInputSubsystem>();
		StylusSubsystem->RemoveMessageHandler(*this);
	}

	virtual void OnStylusStateChanged(const FStylusState& NewState, int32 StylusIndex) override
	{
		if (ActiveDevice == nullptr)
		{
			UStylusInputSubsystem* StylusSubsystem = GEditor->GetEditorSubsystem<UStylusInputSubsystem>();
			ActiveDevice = FindFirstPenDevice(StylusSubsystem, ActiveDeviceIndex);
			bPenDown = false;
		}
		if (ActiveDevice != nullptr && ActiveDeviceIndex == StylusIndex)
		{
			bPenDown = NewState.IsStylusDown();
			ActivePressure = NewState.GetPressure();
		}
	}


	bool HaveActiveStylusState() const
	{
		return ActiveDevice != nullptr && bPenDown;
	}

	static const IStylusInputDevice* FindFirstPenDevice(const UStylusInputSubsystem* StylusSubsystem, int32& ActiveDeviceOut)
	{
		int32 NumDevices = StylusSubsystem->NumInputDevices();
		for (int32 k = 0; k < NumDevices; ++k)
		{
			const IStylusInputDevice* Device = StylusSubsystem->GetInputDevice(k);
			const TArray<EStylusInputType>& Inputs = Device->GetSupportedInputs();
			for (EStylusInputType Input : Inputs)
			{
				if (Input == EStylusInputType::Pressure)
				{
					ActiveDeviceOut = k;
					return Device;
				}
			}
		}
		return nullptr;
	}



	// IToolStylusStateProviderAPI implementation
	virtual float GetCurrentPressure() const override
	{
		return (ActiveDevice != nullptr && bPenDown) ? ActivePressure : 1.0f;
	}

};

void UModelingToolsEditorMode::Enter()
{
	UEdMode::Enter();

	// Register builders for tool targets that the mode uses.
	ToolsContext->TargetManager->AddTargetFactory(NewObject<UStaticMeshComponentToolTargetFactory>(ToolsContext->TargetManager));
	ToolsContext->TargetManager->AddTargetFactory(NewObject<UVolumeDynamicMeshToolTargetFactory>(ToolsContext->TargetManager));
	ToolsContext->TargetManager->AddTargetFactory(NewObject<UVolumeMeshDescriptionToolTargetFactory>(ToolsContext->TargetManager));
	//ToolsContext->TargetManager->AddTargetFactory(NewObject<UDynamicMeshComponentToolTargetFactory>(ToolsContext->TargetManager));

	// register stylus event handler
	StylusStateTracker = MakeUnique<FStylusStateTracker>();

	// register gizmo helper
	UE::TransformGizmoUtil::RegisterTransformGizmoContextObject(ToolsContext.Get());

	// register object creation api
	UEditorModelingObjectsCreationAPI* ModelCreationAPI = UEditorModelingObjectsCreationAPI::Register(ToolsContext.Get());
	if (ModelCreationAPI)
	{
		ModelCreationAPI->GetNewAssetPathNameCallback.BindLambda([](const FString& BaseName, const UWorld* TargetWorld, FString SuggestedFolder)
		{
			return UE::Modeling::GetNewAssetPathName(BaseName, TargetWorld, SuggestedFolder);
		});
		MeshCreatedEventHandle = ModelCreationAPI->OnModelingMeshCreated.AddLambda([](const FCreateMeshObjectResult& CreatedInfo) 
		{
			if (CreatedInfo.NewAsset != nullptr)
			{
				UE::Modeling::OnNewAssetCreated(CreatedInfo.NewAsset);
			}
		});
		TextureCreatedEventHandle = ModelCreationAPI->OnModelingTextureCreated.AddLambda([](const FCreateTextureObjectResult& CreatedInfo)
		{
			if (CreatedInfo.NewAsset != nullptr)
			{
				UE::Modeling::OnNewAssetCreated(CreatedInfo.NewAsset);
			}
		});
	}

	const FModelingToolsManagerCommands& ToolManagerCommands = FModelingToolsManagerCommands::Get();

	// register tool set

	//
	// primitive tools
	//
	auto RegisterPrimitiveToolFunc  =
		[this](TSharedPtr<FUICommandInfo> UICommand,
								  FString&& ToolIdentifier,
								  UAddPrimitiveToolBuilder::EMakeMeshShapeType ShapeTypeIn)
	{
		auto AddPrimitiveToolBuilder = NewObject<UAddPrimitiveToolBuilder>();
		AddPrimitiveToolBuilder->ShapeType = ShapeTypeIn;
		RegisterTool(UICommand, ToolIdentifier, AddPrimitiveToolBuilder);
	};
	RegisterPrimitiveToolFunc(ToolManagerCommands.BeginAddBoxPrimitiveTool,
							  TEXT("BeginAddBoxPrimitiveTool"),
							  UAddPrimitiveToolBuilder::EMakeMeshShapeType::Box);
	RegisterPrimitiveToolFunc(ToolManagerCommands.BeginAddCylinderPrimitiveTool,
							  TEXT("BeginAddCylinderPrimitiveTool"),
							  UAddPrimitiveToolBuilder::EMakeMeshShapeType::Cylinder);
	RegisterPrimitiveToolFunc(ToolManagerCommands.BeginAddConePrimitiveTool,
							  TEXT("BeginAddConePrimitiveTool"),
							  UAddPrimitiveToolBuilder::EMakeMeshShapeType::Cone);
	RegisterPrimitiveToolFunc(ToolManagerCommands.BeginAddArrowPrimitiveTool,
							  TEXT("BeginAddArrowPrimitiveTool"),
							  UAddPrimitiveToolBuilder::EMakeMeshShapeType::Arrow);
	RegisterPrimitiveToolFunc(ToolManagerCommands.BeginAddRectanglePrimitiveTool,
							  TEXT("BeginAddRectanglePrimitiveTool"),
							  UAddPrimitiveToolBuilder::EMakeMeshShapeType::Rectangle);
	RegisterPrimitiveToolFunc(ToolManagerCommands.BeginAddDiscPrimitiveTool,
							  TEXT("BeginAddDiscPrimitiveTool"),
							  UAddPrimitiveToolBuilder::EMakeMeshShapeType::Disc);
	RegisterPrimitiveToolFunc(ToolManagerCommands.BeginAddTorusPrimitiveTool,
							  TEXT("BeginAddTorusPrimitiveTool"),
							  UAddPrimitiveToolBuilder::EMakeMeshShapeType::Torus);
	RegisterPrimitiveToolFunc(ToolManagerCommands.BeginAddSpherePrimitiveTool,
							  TEXT("BeginAddSpherePrimitiveTool"),
							  UAddPrimitiveToolBuilder::EMakeMeshShapeType::Sphere);
	RegisterPrimitiveToolFunc(ToolManagerCommands.BeginAddStairsPrimitiveTool,
							  TEXT("BeginAddStairsPrimitiveTool"),
							  UAddPrimitiveToolBuilder::EMakeMeshShapeType::Stairs);

	//
	// make shape tools
	//
	auto AddPatchToolBuilder = NewObject<UAddPatchToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginAddPatchTool, TEXT("BeginAddPatchTool"), AddPatchToolBuilder);

	auto RevolveBoundaryToolBuilder = NewObject<URevolveBoundaryToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginRevolveBoundaryTool, TEXT("BeginRevolveBoundaryTool"), RevolveBoundaryToolBuilder);

	auto DrawPolygonToolBuilder = NewObject<UDrawPolygonToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginDrawPolygonTool, TEXT("BeginDrawPolygonTool"), DrawPolygonToolBuilder);

	auto DrawPolyPathToolBuilder = NewObject<UDrawPolyPathToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginDrawPolyPathTool, TEXT("BeginDrawPolyPathTool"), DrawPolyPathToolBuilder);

	auto DrawAndRevolveToolBuilder = NewObject<UDrawAndRevolveToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginDrawAndRevolveTool, TEXT("BeginDrawAndRevolveTool"), DrawAndRevolveToolBuilder);

	auto ShapeSprayToolBuilder = NewObject<UShapeSprayToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginShapeSprayTool, TEXT("BeginShapeSprayTool"), ShapeSprayToolBuilder);


	//
	// vertex deform tools
	//

	auto MoveVerticesToolBuilder = NewObject<UMeshVertexSculptToolBuilder>();
	MoveVerticesToolBuilder->StylusAPI = StylusStateTracker.Get();
	RegisterTool(ToolManagerCommands.BeginSculptMeshTool, TEXT("BeginSculptMeshTool"), MoveVerticesToolBuilder);

	auto MeshGroupPaintToolBuilder = NewObject<UMeshGroupPaintToolBuilder>();
	MeshGroupPaintToolBuilder->StylusAPI = StylusStateTracker.Get();
	RegisterTool(ToolManagerCommands.BeginMeshGroupPaintTool, TEXT("BeginMeshGroupPaintTool"), MeshGroupPaintToolBuilder);

	RegisterTool(ToolManagerCommands.BeginPolyEditTool, TEXT("BeginPolyEditTool"), NewObject<UEditMeshPolygonsToolBuilder>());
	UEditMeshPolygonsToolBuilder* TriEditBuilder = NewObject<UEditMeshPolygonsToolBuilder>();
	TriEditBuilder->bTriangleMode = true;
	RegisterTool(ToolManagerCommands.BeginTriEditTool, TEXT("BeginTriEditTool"), TriEditBuilder);
	RegisterTool(ToolManagerCommands.BeginPolyDeformTool, TEXT("BeginPolyDeformTool"), NewObject<UDeformMeshPolygonsToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginSmoothMeshTool, TEXT("BeginSmoothMeshTool"), NewObject<USmoothMeshToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginOffsetMeshTool, TEXT("BeginOffsetMeshTool"), NewObject<UOffsetMeshToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginDisplaceMeshTool, TEXT("BeginDisplaceMeshTool"), NewObject<UDisplaceMeshToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginMeshSpaceDeformerTool, TEXT("BeginMeshSpaceDeformerTool"), NewObject<UMeshSpaceDeformerToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginTransformMeshesTool, TEXT("BeginTransformMeshesTool"), NewObject<UTransformMeshesToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginEditPivotTool, TEXT("BeginEditPivotTool"), NewObject<UEditPivotToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginAlignObjectsTool, TEXT("BeginAlignObjectsTool"), NewObject<UAlignObjectsToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginTransferMeshTool, TEXT("BeginTransferMeshTool"), NewObject<UTransferMeshToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginBakeTransformTool, TEXT("BeginBakeTransformTool"), NewObject<UBakeTransformToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginTransformUVIslandsTool, TEXT("BeginTransformUVIslandsTool"), NewObject<UEditUVIslandsToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginLatticeDeformerTool, TEXT("BeginLatticeDeformerTool"), NewObject<ULatticeDeformerToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginSubdividePolyTool, TEXT("BeginSubdividePolyTool"), NewObject<USubdividePolyToolBuilder>());

	UCombineMeshesToolBuilder* CombineMeshesToolBuilder = NewObject<UCombineMeshesToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginCombineMeshesTool, TEXT("BeginCombineMeshesTool"), CombineMeshesToolBuilder);

	UCombineMeshesToolBuilder* DuplicateMeshesToolBuilder = NewObject<UCombineMeshesToolBuilder>();
	DuplicateMeshesToolBuilder->bIsDuplicateTool = true;
	RegisterTool(ToolManagerCommands.BeginDuplicateMeshesTool, TEXT("BeginDuplicateMeshesTool"), DuplicateMeshesToolBuilder);


	ULODManagerToolBuilder* LODManagerToolBuilder = NewObject<ULODManagerToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginLODManagerTool, TEXT("BeginLODManagerTool"), LODManagerToolBuilder);

	UGenerateStaticMeshLODAssetToolBuilder* GenerateSMLODToolBuilder = NewObject<UGenerateStaticMeshLODAssetToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginGenerateStaticMeshLODAssetTool, TEXT("BeginGenerateStaticMeshLODAssetTool"), GenerateSMLODToolBuilder);


	// edit tools


	auto DynaSculptToolBuilder = NewObject<UDynamicMeshSculptToolBuilder>();
	DynaSculptToolBuilder->bEnableRemeshing = true;
	DynaSculptToolBuilder->StylusAPI = StylusStateTracker.Get();
	RegisterTool(ToolManagerCommands.BeginRemeshSculptMeshTool, TEXT("BeginRemeshSculptMeshTool"), DynaSculptToolBuilder);

	RegisterTool(ToolManagerCommands.BeginRemeshMeshTool, TEXT("BeginRemeshMeshTool"), NewObject<URemeshMeshToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginProjectToTargetTool, TEXT("BeginProjectToTargetTool"), NewObject<UProjectToTargetToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginSimplifyMeshTool, TEXT("BeginSimplifyMeshTool"), NewObject<USimplifyMeshToolBuilder>());

	RegisterTool(ToolManagerCommands.BeginGroupEdgeInsertionTool, TEXT("BeginGroupEdgeInsertionTool"), NewObject<UGroupEdgeInsertionToolBuilder>());

	RegisterTool(ToolManagerCommands.BeginEdgeLoopInsertionTool, TEXT("BeginEdgeLoopInsertionTool"), NewObject<UEdgeLoopInsertionToolBuilder>());

	auto EditNormalsToolBuilder = NewObject<UEditNormalsToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginEditNormalsTool, TEXT("BeginEditNormalsTool"), EditNormalsToolBuilder);

	RegisterTool(ToolManagerCommands.BeginEditTangentsTool, TEXT("BeginEditTangentsTool"), NewObject<UMeshTangentsToolBuilder>());

	auto RemoveOccludedTrianglesToolBuilder = NewObject<URemoveOccludedTrianglesToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginRemoveOccludedTrianglesTool, TEXT("BeginRemoveOccludedTrianglesTool"), RemoveOccludedTrianglesToolBuilder);

	RegisterTool(ToolManagerCommands.BeginHoleFillTool, TEXT("BeginHoleFillTool"), NewObject<UHoleFillToolBuilder>());

	auto UVProjectionToolBuilder = NewObject<UUVProjectionToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginUVProjectionTool, TEXT("BeginUVProjectionTool"), UVProjectionToolBuilder);

	auto UVLayoutToolBuilder = NewObject<UUVLayoutToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginUVLayoutTool, TEXT("BeginUVLayoutTool"), UVLayoutToolBuilder);

	auto MergeMeshesToolBuilder = NewObject<UMergeMeshesToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginVoxelMergeTool, TEXT("BeginVoxelMergeTool"), MergeMeshesToolBuilder);

	auto VoxelCSGMeshesToolBuilder = NewObject<UVoxelCSGMeshesToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginVoxelBooleanTool, TEXT("BeginVoxelBooleanTool"), VoxelCSGMeshesToolBuilder);

	auto VoxelSolidifyMeshesToolBuilder = NewObject<UVoxelSolidifyMeshesToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginVoxelSolidifyTool, TEXT("BeginVoxelSolidifyTool"), VoxelSolidifyMeshesToolBuilder);

	auto VoxelBlendMeshesToolBuilder = NewObject<UVoxelBlendMeshesToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginVoxelBlendTool, TEXT("BeginVoxelBlendTool"), VoxelBlendMeshesToolBuilder);

	auto VoxelMorphologyMeshesToolBuilder = NewObject<UVoxelMorphologyMeshesToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginVoxelMorphologyTool, TEXT("BeginVoxelMorphologyTool"), VoxelMorphologyMeshesToolBuilder);

	auto SelfUnionMeshesToolBuilder = NewObject<USelfUnionMeshesToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginSelfUnionTool, TEXT("BeginSelfUnionTool"), SelfUnionMeshesToolBuilder);

	auto CSGMeshesToolBuilder = NewObject<UCSGMeshesToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginMeshBooleanTool, TEXT("BeginMeshBooleanTool"), CSGMeshesToolBuilder);

	auto CutMeshWithMeshToolBuilder = NewObject<UCutMeshWithMeshToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginCutMeshWithMeshTool, TEXT("BeginCutMeshWithMeshTool"), CutMeshWithMeshToolBuilder);

	auto TrimMeshesToolBuilder = NewObject<UCSGMeshesToolBuilder>();
	TrimMeshesToolBuilder->bTrimMode = true;
	RegisterTool(ToolManagerCommands.BeginMeshTrimTool, TEXT("BeginMeshTrimTool"), TrimMeshesToolBuilder);

	RegisterTool(ToolManagerCommands.BeginBspConversionTool, TEXT("BeginBspConversionTool"), NewObject<UBspConversionToolBuilder>());

	auto MeshToVolumeToolBuilder = NewObject<UMeshToVolumeToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginMeshToVolumeTool, TEXT("BeginMeshToVolumeTool"), MeshToVolumeToolBuilder);

	auto VolumeToMeshToolBuilder = NewObject<UVolumeToMeshToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginVolumeToMeshTool, TEXT("BeginVolumeToMeshTool"), VolumeToMeshToolBuilder);

	auto PlaneCutToolBuilder = NewObject<UPlaneCutToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginPlaneCutTool, TEXT("BeginPlaneCutTool"), PlaneCutToolBuilder);

	auto MirrorToolBuilder = NewObject<UMirrorToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginMirrorTool, TEXT("BeginMirrorTool"), MirrorToolBuilder);

	auto PolygonCutToolBuilder = NewObject<UPolygonOnMeshToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginPolygonCutTool, TEXT("BeginPolygonCutTool"), PolygonCutToolBuilder);

	auto GlobalUVGenerateToolBuilder = NewObject<UParameterizeMeshToolBuilder>();
	GlobalUVGenerateToolBuilder->bDoAutomaticGlobalUnwrap = true;
	RegisterTool(ToolManagerCommands.BeginGlobalUVGenerateTool, TEXT("BeginGlobalUVGenerateTool"), GlobalUVGenerateToolBuilder);

	auto GroupUVGenerateToolBuilder = NewObject<UParameterizeMeshToolBuilder>();
	GroupUVGenerateToolBuilder->bDoAutomaticGlobalUnwrap = false;
	RegisterTool(ToolManagerCommands.BeginGroupUVGenerateTool, TEXT("BeginGroupUVGenerateTool"), GroupUVGenerateToolBuilder);

	RegisterTool(ToolManagerCommands.BeginUVSeamEditTool, TEXT("BeginUVSeamEditTool"), NewObject< USeamSculptToolBuilder>());

	auto MeshSelectionToolBuilder = NewObject<UMeshSelectionToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginMeshSelectionTool, TEXT("BeginMeshSelectionTool"), MeshSelectionToolBuilder);

	auto EditMeshMaterialsToolBuilder = NewObject<UEditMeshMaterialsToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginEditMeshMaterialsTool, TEXT("BeginEditMeshMaterialsTool"), EditMeshMaterialsToolBuilder);
	
	RegisterTool(ToolManagerCommands.BeginMeshAttributePaintTool, TEXT("BeginMeshAttributePaintTool"), NewObject<UMeshAttributePaintToolBuilder>());

	auto BakeMeshAttributeMapsToolBuilder = NewObject<UBakeMeshAttributeMapsToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginBakeMeshAttributeMapsTool, TEXT("BeginBakeMeshAttributeMapsTool"), BakeMeshAttributeMapsToolBuilder);

	// analysis tools

	RegisterTool(ToolManagerCommands.BeginMeshInspectorTool, TEXT("BeginMeshInspectorTool"), NewObject<UMeshInspectorToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginWeldEdgesTool, TEXT("BeginWeldEdgesTool"), NewObject<UWeldMeshEdgesToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginPolyGroupsTool, TEXT("BeginPolyGroupsTool"), NewObject<UConvertToPolygonsToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginAttributeEditorTool, TEXT("BeginAttributeEditorTool"), NewObject<UAttributeEditorToolBuilder>());


	// Physics Tools

	RegisterTool(ToolManagerCommands.BeginPhysicsInspectorTool, TEXT("BeginPhysicsInspectorTool"), NewObject<UPhysicsInspectorToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginSetCollisionGeometryTool, TEXT("BeginSetCollisionGeometryTool"), NewObject<USetCollisionGeometryToolBuilder>());
	//RegisterTool(ToolManagerCommands.BeginEditCollisionGeometryTool, TEXT("EditCollisionGeoTool"), NewObject<UEditCollisionGeometryToolBuilder>());

	auto ExtractCollisionGeoToolBuilder = NewObject<UExtractCollisionGeometryToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginExtractCollisionGeometryTool, TEXT("BeginExtractCollisionGeometryTool"), ExtractCollisionGeoToolBuilder);



	// (experimental) hair tools

	UGroomToMeshToolBuilder* GroomToMeshToolBuilder = NewObject<UGroomToMeshToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginGroomToMeshTool, TEXT("BeginGroomToMeshTool"), GroomToMeshToolBuilder);

	RegisterTool(ToolManagerCommands.BeginGroomCardsEditorTool, TEXT("BeginGroomCardsEditorTool"), NewObject<UGroomCardsEditorToolBuilder>());

	UGenerateLODMeshesToolBuilder* GenerateLODMeshesToolBuilder = NewObject<UGenerateLODMeshesToolBuilder>();
	RegisterTool(ToolManagerCommands.BeginGenerateLODMeshesTool, TEXT("BeginGenerateLODMeshesTool"), GenerateLODMeshesToolBuilder);

	// PolyModeling tools
	auto RegisterPolyModelSelectTool = [&](EEditMeshPolygonsToolSelectionMode SelectionMode, TSharedPtr<FUICommandInfo> UICommand, FString StringName)
	{
		UEditMeshPolygonsSelectionModeToolBuilder* SelectionModeBuilder = NewObject<UEditMeshPolygonsSelectionModeToolBuilder>();
		SelectionModeBuilder->SelectionMode = SelectionMode;
		RegisterTool(UICommand, StringName, SelectionModeBuilder);
	};
	RegisterPolyModelSelectTool(EEditMeshPolygonsToolSelectionMode::Faces, ToolManagerCommands.BeginPolyModelTool_FaceSelect, TEXT("PolyEdit_FaceSelect"));
	RegisterPolyModelSelectTool(EEditMeshPolygonsToolSelectionMode::Edges, ToolManagerCommands.BeginPolyModelTool_EdgeSelect, TEXT("PolyEdit_EdgeSelect"));
	RegisterPolyModelSelectTool(EEditMeshPolygonsToolSelectionMode::Vertices, ToolManagerCommands.BeginPolyModelTool_VertexSelect, TEXT("PolyEdit_VertexSelect"));
	RegisterPolyModelSelectTool(EEditMeshPolygonsToolSelectionMode::Loops, ToolManagerCommands.BeginPolyModelTool_LoopSelect, TEXT("PolyEdit_LoopSelect"));
	RegisterPolyModelSelectTool(EEditMeshPolygonsToolSelectionMode::Rings, ToolManagerCommands.BeginPolyModelTool_RingSelect, TEXT("PolyEdit_RingSelect"));
	RegisterPolyModelSelectTool(EEditMeshPolygonsToolSelectionMode::FacesEdgesVertices, ToolManagerCommands.BeginPolyModelTool_AllSelect, TEXT("PolyEdit_AllSelect"));

	auto RegisterPolyModelActionTool = [&](EEditMeshPolygonsToolActions Action, TSharedPtr<FUICommandInfo> UICommand, FString StringName)
	{
		UEditMeshPolygonsActionModeToolBuilder* ActionModeBuilder = NewObject<UEditMeshPolygonsActionModeToolBuilder>();
		ActionModeBuilder->StartupAction = Action;
		RegisterTool(UICommand, StringName, ActionModeBuilder);
	};
	RegisterPolyModelActionTool(EEditMeshPolygonsToolActions::Extrude, ToolManagerCommands.BeginPolyModelTool_Extrude, TEXT("PolyEdit_Extrude"));
	RegisterPolyModelActionTool(EEditMeshPolygonsToolActions::Offset, ToolManagerCommands.BeginPolyModelTool_Offset, TEXT("PolyEdit_Offset"));
	RegisterPolyModelActionTool(EEditMeshPolygonsToolActions::Inset, ToolManagerCommands.BeginPolyModelTool_Inset, TEXT("PolyEdit_Inset"));
	RegisterPolyModelActionTool(EEditMeshPolygonsToolActions::Outset, ToolManagerCommands.BeginPolyModelTool_Outset, TEXT("PolyEdit_Outset"));
	RegisterPolyModelActionTool(EEditMeshPolygonsToolActions::CutFaces, ToolManagerCommands.BeginPolyModelTool_CutFaces, TEXT("PolyEdit_CutFaces"));


	// register extensions
	TArray<IModelingModeToolExtension*> Extensions = IModularFeatures::Get().GetModularFeatureImplementations<IModelingModeToolExtension>(
		IModelingModeToolExtension::GetModularFeatureName());
	if (Extensions.Num() > 0)
	{
		FExtensionToolQueryInfo ExtensionQueryInfo;
		ExtensionQueryInfo.ToolsContext = ToolsContext.Get();
		ExtensionQueryInfo.AssetAPI = nullptr;

		UE_LOG(LogTemp, Log, TEXT("ModelingMode: Found %d Tool Extension Modules"), Extensions.Num());
		for (int32 k = 0; k < Extensions.Num(); ++k)
		{
			// TODO: extension name
			FText ExtensionName = Extensions[k]->GetExtensionName();
			FString ExtensionPrefix = FString::Printf(TEXT("[%d][%s]"), k, *ExtensionName.ToString());

			TArray<FExtensionToolDescription> ToolSet;
			Extensions[k]->GetExtensionTools(ExtensionQueryInfo, ToolSet);
			for (const FExtensionToolDescription& ToolInfo : ToolSet)
			{
				UE_LOG(LogTemp, Log, TEXT("%s - Registering Tool [%s]"), *ExtensionPrefix, *ToolInfo.ToolName.ToString());

				RegisterTool(ToolInfo.ToolCommand, ToolInfo.ToolName.ToString(), ToolInfo.ToolBuilder);
			}
		}
	}


	ToolsContext->ToolManager->SelectActiveToolType(EToolSide::Left, TEXT("DynaSculptTool"));

	// Register modeling mode hotkeys. Note that we use the toolkit command list because we would like the hotkeys
	// to work even when the viewport is not focused, provided that nothing else captures the key presses.
	FModelingModeActionCommands::RegisterCommandBindings(Toolkit->GetToolkitCommands(), [this](EModelingModeActionCommands Command) {
		ModelingModeShortcutRequested(Command);
	});

	// enable realtime viewport override
	ConfigureRealTimeViewportsOverride(true);

	//
	// Engine Analytics
	//

	// Log mode starting
	if (FEngineAnalytics::IsAvailable())
	{
		UModelingToolsEditorMode::LastModeStartTimestamp = FDateTime::UtcNow();
		TArray<FAnalyticsEventAttribute> Attributes;
		Attributes.Add(FAnalyticsEventAttribute(TEXT("Timestamp"), LastModeStartTimestamp.ToString()));
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.MeshModelingMode.Enter"), Attributes);
	}

	// Log tool starting
	ToolsContext->ToolManager->OnToolStarted.AddLambda([this](UInteractiveToolManager* Manager, UInteractiveTool* Tool)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			UModelingToolsEditorMode::LastToolStartTimestamp = FDateTime::UtcNow();
			TArray<FAnalyticsEventAttribute> Attributes;
			Attributes.Add(FAnalyticsEventAttribute(TEXT("DisplayName"), Tool->GetToolInfo().ToolDisplayName.ToString()));
			Attributes.Add(FAnalyticsEventAttribute(TEXT("Timestamp"), LastToolStartTimestamp.ToString()));
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.MeshModelingMode.ToolStarted"), Attributes);
		}
	});

	// Log tool ending
	ToolsContext->ToolManager->OnToolEnded.AddLambda([this](UInteractiveToolManager* Manager, UInteractiveTool* Tool)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			TArray<FAnalyticsEventAttribute> Attributes;
			Attributes.Add(FAnalyticsEventAttribute(TEXT("DisplayName"), Tool->GetToolInfo().ToolDisplayName.ToString()));
			Attributes.Add(FAnalyticsEventAttribute(TEXT("Timestamp"), FDateTime::UtcNow().ToString()));
			const FTimespan ToolUsageDuration = FDateTime::UtcNow() - UModelingToolsEditorMode::LastToolStartTimestamp;
			Attributes.Add(FAnalyticsEventAttribute(TEXT("Duration.Seconds"), static_cast<float>(ToolUsageDuration.GetTotalSeconds())));
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.MeshModelingMode.ToolEnded"), Attributes);
		}
	});
}

void UModelingToolsEditorMode::Exit()
{
	//
	// Engine Analytics
	//
	// Log mode ending
	if (FEngineAnalytics::IsAvailable())
	{
		TArray<FAnalyticsEventAttribute> Attributes;
		Attributes.Add(FAnalyticsEventAttribute(TEXT("Timestamp"), FDateTime::UtcNow().ToString()));
		const FTimespan ModeUsageDuration = FDateTime::UtcNow() - UModelingToolsEditorMode::LastModeStartTimestamp;
		Attributes.Add(FAnalyticsEventAttribute(TEXT("Duration.Seconds"), static_cast<float>(ModeUsageDuration.GetTotalSeconds())));
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.MeshModelingMode.Exit"), Attributes);
	}

	StylusStateTracker = nullptr;

	// TODO: cannot deregister currently because if another mode is also registering, its Enter()
	// will be called before our Exit()
	//UE::TransformGizmoUtil::DeregisterTransformGizmoContextObject(ToolsContext.Get());
	
	// TODO: cannot deregister currently because if another mode is also registering, its Enter()
	// will be called before our Exit()
	UEditorModelingObjectsCreationAPI* ObjectCreationAPI = UEditorModelingObjectsCreationAPI::Find(ToolsContext.Get());
	if (ObjectCreationAPI)
	{
		ObjectCreationAPI->GetNewAssetPathNameCallback.Unbind();
		ObjectCreationAPI->OnModelingMeshCreated.Remove(MeshCreatedEventHandle);
		ObjectCreationAPI->OnModelingTextureCreated.Remove(TextureCreatedEventHandle);
		//UEditorModelingObjectsCreationAPI::Deregister(ToolsContext.Get());		// cannot do currently because of shared ToolsContext, revisit in future
	}

	FModelingModeActionCommands::UnRegisterCommandBindings(Toolkit->GetToolkitCommands());

	// clear realtime viewport override
	ConfigureRealTimeViewportsOverride(false);

	// Call base Exit method to ensure proper cleanup
	UEdMode::Exit();
}

void UModelingToolsEditorMode::CreateToolkit()
{
	Toolkit = MakeShareable(new FModelingToolsEditorModeToolkit);
}

void UModelingToolsEditorMode::OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{
	FModelingToolActionCommands::UpdateToolCommandBinding(Tool, Toolkit->GetToolkitCommands(), false);
	
	if( FEngineAnalytics::IsAvailable() )
	{
		FEngineAnalytics::GetProvider().RecordEvent( TEXT( "Editor.Usage.MeshModelingMode.ToolStarted" ),
													 TEXT( "DisplayName" ),
													 Tool->GetToolInfo().ToolDisplayName.ToString() );
	}
}

void UModelingToolsEditorMode::OnToolEnded(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{
	FModelingToolActionCommands::UpdateToolCommandBinding(Tool, Toolkit->GetToolkitCommands(), true);
	
	if( FEngineAnalytics::IsAvailable() )
	{
		FEngineAnalytics::GetProvider().RecordEvent( TEXT( "Editor.Usage.MeshModelingMode.ToolEnded" ),
													 TEXT( "DisplayName" ),
													 Tool->GetToolInfo().ToolDisplayName.ToString() );
	}
}

void UModelingToolsEditorMode::BindCommands()
{
	const FModelingToolsManagerCommands& ToolManagerCommands = FModelingToolsManagerCommands::Get();
	const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();

	CommandList->MapAction(
		ToolManagerCommands.AcceptActiveTool,
		FExecuteAction::CreateLambda([this]() { ToolsContext->EndTool(EToolShutdownType::Accept); }),
		FCanExecuteAction::CreateLambda([this]() { return ToolsContext->CanAcceptActiveTool(); }),
		FGetActionCheckState(),
		FIsActionButtonVisible::CreateLambda([this]() {return ToolsContext->ActiveToolHasAccept(); }),
		EUIActionRepeatMode::RepeatDisabled
		);

	CommandList->MapAction(
		ToolManagerCommands.CancelActiveTool,
		FExecuteAction::CreateLambda([this]() { ToolsContext->EndTool(EToolShutdownType::Cancel); }),
		FCanExecuteAction::CreateLambda([this]() { return ToolsContext->CanCancelActiveTool(); }),
		FGetActionCheckState(),
		FIsActionButtonVisible::CreateLambda([this]() {return ToolsContext->ActiveToolHasAccept(); }),
		EUIActionRepeatMode::RepeatDisabled
		);

	CommandList->MapAction(
		ToolManagerCommands.CompleteActiveTool,
		FExecuteAction::CreateLambda([this]() { ToolsContext->EndTool(EToolShutdownType::Completed); }),
		FCanExecuteAction::CreateLambda([this]() { return ToolsContext->CanCompleteActiveTool(); }),
		FGetActionCheckState(),
		FIsActionButtonVisible::CreateLambda([this]() {return ToolsContext->CanCompleteActiveTool(); }),
		EUIActionRepeatMode::RepeatDisabled
		);

	// These aren't activated by buttons but have default chords that bind the keypresses to the action.
	CommandList->MapAction(
		ToolManagerCommands.AcceptOrCompleteActiveTool,
		FExecuteAction::CreateLambda([this]() {
			const EToolShutdownType ShutdownType = ToolsContext->CanAcceptActiveTool() ? EToolShutdownType::Accept : EToolShutdownType::Completed;
			ToolsContext->EndTool(ShutdownType);
			}),
		FCanExecuteAction::CreateLambda([this]() {
				return ToolsContext->CanAcceptActiveTool() || ToolsContext->CanCompleteActiveTool();
			}),
		FGetActionCheckState(),
		FIsActionButtonVisible(),
		EUIActionRepeatMode::RepeatDisabled);
	CommandList->MapAction(
		ToolManagerCommands.CancelOrCompleteActiveTool,
		FExecuteAction::CreateLambda([this]() {
			const EToolShutdownType ShutdownType = ToolsContext->CanCancelActiveTool() ? EToolShutdownType::Cancel : EToolShutdownType::Completed;
			ToolsContext->EndTool(ShutdownType);
			}),
		FCanExecuteAction::CreateLambda([this]() {
				return ToolsContext->CanCompleteActiveTool() || ToolsContext->CanCancelActiveTool();
			}),
		FGetActionCheckState(),
		FIsActionButtonVisible(),
		EUIActionRepeatMode::RepeatDisabled);
}

void UModelingToolsEditorMode::ModelingModeShortcutRequested(EModelingModeActionCommands Command)
{
	if (Command == EModelingModeActionCommands::FocusViewToCursor)
	{
		FocusCameraAtCursorHotkey();
	}
}


void UModelingToolsEditorMode::FocusCameraAtCursorHotkey()
{
	FRay Ray = ToolsContext->GetLastWorldRay();

	double NearestHitDist = (double)HALF_WORLD_MAX;
	FVector HitPoint = FVector::ZeroVector;

	// cast ray against visible objects
	FHitResult WorldHitResult;
	if (ToolSceneQueriesUtil::FindNearestVisibleObjectHit(GetWorld(), WorldHitResult, Ray.Origin, Ray.PointAt(HALF_WORLD_MAX)))
	{
		HitPoint = WorldHitResult.ImpactPoint;
		NearestHitDist = (double)Ray.GetParameter(HitPoint);
	}

	// cast ray against tool
	if (GetToolManager()->HasAnyActiveTool())
	{
		UInteractiveTool* Tool = GetToolManager()->GetActiveTool(EToolSide::Mouse);
		IInteractiveToolCameraFocusAPI* FocusAPI = Cast<IInteractiveToolCameraFocusAPI>(Tool);
		if (FocusAPI && FocusAPI->SupportsWorldSpaceFocusPoint())
		{
			FVector ToolHitPoint;
			if (FocusAPI->GetWorldSpaceFocusPoint(Ray, ToolHitPoint))
			{
				double HitDepth = (double)Ray.GetParameter(ToolHitPoint);
				if (HitDepth < NearestHitDist)
				{
					NearestHitDist = HitDepth;
					HitPoint = ToolHitPoint;
				}
			}
		}
	}


	if (NearestHitDist < (double)HALF_WORLD_MAX && GCurrentLevelEditingViewportClient)
	{
		GCurrentLevelEditingViewportClient->CenterViewportAtPoint(HitPoint, false);
	}
}


bool UModelingToolsEditorMode::ComputeBoundingBoxForViewportFocus(AActor* Actor, UPrimitiveComponent* PrimitiveComponent, FBox& InOutBox) const
{
	if (GetToolManager()->HasAnyActiveTool())
	{
		UInteractiveTool* Tool = GetToolManager()->GetActiveTool(EToolSide::Mouse);
		IInteractiveToolCameraFocusAPI* FocusAPI = Cast<IInteractiveToolCameraFocusAPI>(Tool);
		if (FocusAPI && FocusAPI->SupportsWorldSpaceFocusBox() )
		{
			InOutBox = FocusAPI->GetWorldSpaceFocusBox();
			if (InOutBox.IsValid)
			{
				float MaxDimension = InOutBox.GetExtent().GetMax();
				if (MaxDimension > SMALL_NUMBER)
				{
					InOutBox.ExpandBy(MaxDimension * 0.2f);
				}
				else
				{
					InOutBox.ExpandBy(25);
				}
				return true;
			}
		}
	}
	return false;
}


bool UModelingToolsEditorMode::GetPivotForOrbit(FVector& OutPivot) const
{
	if (GCurrentLevelEditingViewportClient)
	{
		OutPivot = GCurrentLevelEditingViewportClient->GetViewTransform().GetLookAt();
		return true;
	}
	return false;
}



void UModelingToolsEditorMode::ConfigureRealTimeViewportsOverride(bool bEnable)
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<ILevelEditor> LevelEditor = LevelEditorModule.GetFirstLevelEditor();
	if (LevelEditor.IsValid())
	{
		TArray<TSharedPtr<SLevelViewport>> Viewports = LevelEditor->GetViewports();
		for (const TSharedPtr<SLevelViewport>& ViewportWindow : Viewports)
		{
			if (ViewportWindow.IsValid())
			{
				FEditorViewportClient& Viewport = ViewportWindow->GetAssetViewportClient();
				const FText SystemDisplayName = LOCTEXT("RealtimeOverrideMessage_ModelingMode", "Modeling Mode");
				if (bEnable)
				{
					Viewport.AddRealtimeOverride(bEnable, SystemDisplayName);
				}
				else
				{
					Viewport.RemoveRealtimeOverride(SystemDisplayName, false);
				}
			}
		}
	}
}



#undef LOCTEXT_NAMESPACE
